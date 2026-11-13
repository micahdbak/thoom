#include "webrtc/webrtc_signaller.h"

#include <rtc/rtc.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>

#include "thoom/log.h"

#define SIGNALLING_URL "wss://cheese.micahdb.com/sc/"

#define SIGNALLING_URL_ENV "THOOM_SIGNALLING_URL"

#define CANDIDATE_TAG "candidate"
#define DESCRIPTION_TAG "description"
#define FAIL_TAG "fail"
#define GONE_TAG "gone"
#define PEER_FORMAT "peer %d"
#define PING_TAG "ping"

#define TEXT_MESSAGE -1

#define CODE_LENGTH 6
static const char CODE_CHARSET[] = "023456789ABCEFHLPRUXY";

namespace thoom {

std::string WebRtcSignaller::make_code() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, sizeof(CODE_CHARSET) - 2);

  std::string code;
  for (int i = 0; i < CODE_LENGTH; i++) {
    code.push_back(CODE_CHARSET[dis(gen)]);
  }

  return code;
}

bool WebRtcSignaller::valid_code(const std::string& code) {
  if (code.size() != CODE_LENGTH) {
    return false;
  }

  for (const char c : code) {
    if ((c < 'A' || c > 'Z') && (c < '0' || c > '9')) {
      return false;
    }
  }

  return true;
}

bool WebRtcSignaller::connect(const std::string& code) {
  if (this->state != WebRtcSignaller::kNotConnected || this->ws >= 0) {
    thoom_log_warn("WebRtcSignaller::connect: already connecting or connected");
    return false;
  }

  {
    std::lock_guard<std::mutex> guard(this->mutex);
    this->raw.clear();
    this->events.clear();
    this->pending_peer = 0;
    this->opened = false;
    this->closed = false;
  }

  const char* override_url = getenv(SIGNALLING_URL_ENV);
  const std::string url =
      std::string(override_url != nullptr ? override_url : SIGNALLING_URL) +
      code;
  this->ws = rtcCreateWebSocket(url.c_str());
  if (this->ws < 0) {
    thoom_log_error(
        "WebRtcSignaller::connect: rtcCreateWebSocket returned "
        "error code: %d",
        this->ws);
    return false;
  }

  rtcSetUserPointer(this->ws, this);
  rtcSetOpenCallback(this->ws, WebRtcSignaller::on_open);
  rtcSetClosedCallback(this->ws, WebRtcSignaller::on_close);
  rtcSetErrorCallback(this->ws, WebRtcSignaller::on_error);
  rtcSetMessageCallback(this->ws, WebRtcSignaller::on_message);

  this->state = WebRtcSignaller::kConnecting;
  return true;
}

void WebRtcSignaller::disconnect() {
  if (this->ws >= 0) {
    // Deleting a socket that still has callbacks makes libdatachannel fire the
    // closed callback while it holds its own lock, and the callback locks that
    // same lock. Detach first.
    rtcSetOpenCallback(this->ws, nullptr);
    rtcSetClosedCallback(this->ws, nullptr);
    rtcSetErrorCallback(this->ws, nullptr);
    rtcSetMessageCallback(this->ws, nullptr);
    rtcDelete(this->ws);
    this->ws = -1;
  }

  this->state = WebRtcSignaller::kNotConnected;
}

void WebRtcSignaller::poll() {
  bool was_closed = false;

  {
    std::lock_guard<std::mutex> guard(this->mutex);

    if (this->opened) {
      this->opened = false;
      this->state = WebRtcSignaller::kConnected;
    }

    if (this->closed) {
      this->closed = false;
      was_closed = true;
    }

    this->assemble_events();
  }

  // rtcCreateWebSocket starts connecting before a callback can be installed, so
  // a fast handshake can finish before on_open is registered. Query the socket
  // so a missed event cannot leave the signaller connecting forever.
  if (this->ws >= 0 && this->state == WebRtcSignaller::kConnecting) {
    if (rtcIsOpen(this->ws)) {
      std::lock_guard<std::mutex> guard(this->mutex);
      this->state = WebRtcSignaller::kConnected;
    } else if (rtcIsClosed(this->ws)) {
      std::lock_guard<std::mutex> guard(this->mutex);
      was_closed = true;
    }
  }

  // Release the socket outside the lock: a leaked one would be destroyed under
  // the library's own lock at rtcCleanup and could never report back.
  if (was_closed) {
    this->disconnect();
  }
}

WebRtcSignaller::State WebRtcSignaller::get_state() const {
  return this->state;
}

bool WebRtcSignaller::send_ping(int peer) {
  return this->send_envelope(peer) && this->send_message(PING_TAG);
}

bool WebRtcSignaller::send_description(int peer, const std::string& sdp,
                                       const std::string& type) {
  return this->send_envelope(peer) && this->send_message(DESCRIPTION_TAG) &&
         this->send_message(sdp) && this->send_message(type);
}

bool WebRtcSignaller::send_candidate(int peer, const std::string& cand,
                                     const std::string& mid) {
  return this->send_envelope(peer) && this->send_message(CANDIDATE_TAG) &&
         this->send_message(cand) && this->send_message(mid);
}

bool WebRtcSignaller::receive(Event* event) {
  std::lock_guard<std::mutex> guard(this->mutex);

  if (this->events.empty()) {
    return false;
  }

  *event = this->events.front();
  this->events.pop_front();
  return true;
}

void WebRtcSignaller::on_open(int, void* user_ptr) {
  WebRtcSignaller* signaller = static_cast<WebRtcSignaller*>(user_ptr);
  std::lock_guard<std::mutex> guard(signaller->mutex);
  signaller->opened = true;
}

void WebRtcSignaller::on_close(int, void* user_ptr) {
  WebRtcSignaller* signaller = static_cast<WebRtcSignaller*>(user_ptr);
  std::lock_guard<std::mutex> guard(signaller->mutex);
  signaller->closed = true;
}

void WebRtcSignaller::on_error(int, const char* error, void* user_ptr) {
  thoom_log_error("WebRtcSignaller::on_error: %s", error);
  WebRtcSignaller* signaller = static_cast<WebRtcSignaller*>(user_ptr);
  std::lock_guard<std::mutex> guard(signaller->mutex);
  signaller->closed = true;
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

bool WebRtcSignaller::send_envelope(int peer) {
  char envelope[32] = {};
  snprintf(envelope, sizeof(envelope), PEER_FORMAT, peer);
  return this->send_message(envelope);
}

bool WebRtcSignaller::send_message(const std::string& message) {
  if (this->state != WebRtcSignaller::kConnected || this->ws < 0) {
    thoom_log_warn("WebRtcSignaller::send_message: not connected");
    return false;
  }

  return rtcSendMessage(this->ws, message.c_str(), TEXT_MESSAGE) >= 0;
}

void WebRtcSignaller::push_message(const std::string& message) {
  std::lock_guard<std::mutex> guard(this->mutex);
  this->raw.push_back(message);
}

void WebRtcSignaller::assemble_events() {
  while (!this->raw.empty()) {
    const std::string tag = this->raw.front();

    int peer = 0;
    if (sscanf(tag.c_str(), PEER_FORMAT, &peer) == 1) {
      this->pending_peer = peer;
      this->raw.pop_front();
      continue;
    }

    Event event = {};
    event.peer = this->pending_peer;
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
    } else if (tag == GONE_TAG) {
      event.kind = Event::kGone;
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
