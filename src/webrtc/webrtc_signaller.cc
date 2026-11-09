#include "webrtc/webrtc_signaller.h"

#include <rtc/rtc.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include "absl/log/log.h"

// #define SIGNALLING_URL "ws://localhost:8080/sc/"
#define SIGNALLING_URL "wss://cheese.micahdb.com/sc/"

#define CANDIDATE_TAG "candidate"
#define DESCRIPTION_TAG "description"
#define FAIL_TAG "fail"
#define PING_TAG "ping"

#define TEXT_MESSAGE -1

namespace thoom {

WebRtcSignaller::~WebRtcSignaller() { this->disconnect(); }

bool WebRtcSignaller::connect(const std::string& code, bool offerer) {
  if (this->state_value != WebRtcSignaller::kNotConnected) {
    LOG(WARNING) << "WebRtcSignaller::connect: already connecting or connected";
    return false;
  }

  {
    std::lock_guard<std::mutex> guard(this->mutex);
    this->raw.clear();
    this->events.clear();
    this->opened = false;
    this->closed = false;
  }

  const std::string url = SIGNALLING_URL + code;
  this->ws = rtcCreateWebSocket(url.c_str());
  if (this->ws < 0) {
    LOG(ERROR)
        << "WebRtcSignaller::connect: rtcCreateWebSocket returned error code: "
        << this->ws;
    return false;
  }

  rtcSetUserPointer(this->ws, this);
  rtcSetOpenCallback(this->ws, WebRtcSignaller::on_open);
  rtcSetClosedCallback(this->ws, WebRtcSignaller::on_close);
  rtcSetErrorCallback(this->ws, WebRtcSignaller::on_error);
  rtcSetMessageCallback(this->ws, WebRtcSignaller::on_message);

  this->state_value = WebRtcSignaller::kConnecting;
  return true;
}

void WebRtcSignaller::disconnect() {
  if (this->ws >= 0) {
    rtcDelete(this->ws);
    this->ws = -1;
  }

  this->state_value = WebRtcSignaller::kNotConnected;
}

void WebRtcSignaller::poll() {
  std::lock_guard<std::mutex> guard(this->mutex);

  if (this->opened) {
    this->opened = false;
    this->state_value = WebRtcSignaller::kConnected;
  }

  if (this->closed) {
    this->closed = false;
    this->state_value = WebRtcSignaller::kNotConnected;
  }

  this->assemble_events();
}

bool WebRtcSignaller::send_ping() { return this->send_message(PING_TAG); }

bool WebRtcSignaller::send_description(const std::string& sdp,
                                       const std::string& type) {
  return this->send_message(DESCRIPTION_TAG) && this->send_message(sdp) &&
         this->send_message(type);
}

bool WebRtcSignaller::send_candidate(const std::string& cand,
                                     const std::string& mid) {
  return this->send_message(CANDIDATE_TAG) && this->send_message(cand) &&
         this->send_message(mid);
}

bool WebRtcSignaller::send_fail() { return this->send_message(FAIL_TAG); }

bool WebRtcSignaller::receive(Event* event) {
  std::lock_guard<std::mutex> guard(this->mutex);

  if (this->events.empty()) {
    return false;
  }

  *event = this->events.front();
  this->events.pop_front();
  return true;
}

void WebRtcSignaller::wait(uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(this->mutex);
  this->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
    return !this->events.empty() || !this->raw.empty() || this->opened ||
           this->closed;
  });
}

WebRtcSignaller::State WebRtcSignaller::state() const {
  return this->state_value;
}

void WebRtcSignaller::on_open(int, void* user_ptr) {
  WebRtcSignaller* signaller = static_cast<WebRtcSignaller*>(user_ptr);
  std::lock_guard<std::mutex> guard(signaller->mutex);
  signaller->opened = true;
  signaller->cv.notify_one();
}

void WebRtcSignaller::on_close(int, void* user_ptr) {
  WebRtcSignaller* signaller = static_cast<WebRtcSignaller*>(user_ptr);
  std::lock_guard<std::mutex> guard(signaller->mutex);
  signaller->closed = true;
  signaller->cv.notify_one();
}

void WebRtcSignaller::on_error(int, const char* error, void* user_ptr) {
  LOG(ERROR) << "WebRtcSignaller::on_error: " << error;
  WebRtcSignaller* signaller = static_cast<WebRtcSignaller*>(user_ptr);
  std::lock_guard<std::mutex> guard(signaller->mutex);
  signaller->closed = true;
  signaller->cv.notify_one();
}

void WebRtcSignaller::on_message(int, const char* message, int size,
                                 void* user_ptr) {
  WebRtcSignaller* signaller = static_cast<WebRtcSignaller*>(user_ptr);

  if (size < 0) {
    signaller->push_message(std::string(message, -size - 1));
    return;
  }

  signaller->push_message(std::string(message, size));
}

bool WebRtcSignaller::send_message(const std::string& message) {
  if (this->state_value != WebRtcSignaller::kConnected || this->ws < 0) {
    LOG(WARNING) << "WebRtcSignaller::send_message: not connected";
    return false;
  }

  return rtcSendMessage(this->ws, message.c_str(), TEXT_MESSAGE) >= 0;
}

void WebRtcSignaller::push_message(const std::string& message) {
  std::lock_guard<std::mutex> guard(this->mutex);
  this->raw.push_back(message);
  this->cv.notify_one();
}

void WebRtcSignaller::assemble_events() {
  while (!this->raw.empty()) {
    const std::string tag = this->raw.front();

    Event event = {};
    size_t payload_fields = 0;

    if (tag == DESCRIPTION_TAG) {
      event.kind = Event::kDescription;
      payload_fields = 2;
    } else if (tag == CANDIDATE_TAG) {
      event.kind = Event::kCandidate;
      payload_fields = 2;
    } else if (tag == PING_TAG) {
      event.kind = Event::kPing;
    } else if (tag == FAIL_TAG) {
      event.kind = Event::kFail;
    } else {
      this->raw.pop_front();
      continue;
    }

    if (this->raw.size() < payload_fields + 1) {
      return;
    }

    this->raw.pop_front();

    if (payload_fields > 0) {
      event.first = this->raw.front();
      this->raw.pop_front();
    }

    if (payload_fields > 1) {
      event.second = this->raw.front();
      this->raw.pop_front();
    }

    this->events.push_back(event);
  }
}

};  // namespace thoom
