#include "webrtc/webrtc_connection.h"

#include <rtc/rtc.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "connection.h"

#define RELIABLE_LABEL "reliable"
#define UNRELIABLE_LABEL "unreliable"

#define ICE_SERVER_COUNT 2

static const char* ICE_SERVER_URLS[ICE_SERVER_COUNT] = {
    "stun:droppr.net:5051", "turn:droppr:droppr@droppr.net:5051"};

namespace thoom {

WebRtcConnection::~WebRtcConnection() { this->close(); }

void WebRtcConnection::start_collecting(bool offerer) {
  if (this->current_state != Connection::kDead) {
    LOG(FATAL) << "WebRtcConnection::start_collecting error: already "
                  "collecting or alive";
  }

  rtcConfiguration config = {};
  config.iceServers = ICE_SERVER_URLS;
  config.iceServersCount = ICE_SERVER_COUNT;
  config.certificateType = RTC_CERTIFICATE_DEFAULT;
  config.iceTransportPolicy = RTC_TRANSPORT_POLICY_ALL;
  config.enableIceTcp = true;
  config.disableAutoNegotiation = false;

  this->pc = rtcCreatePeerConnection(&config);
  if (this->pc < 0) {
    LOG(FATAL) << "WebRtcConnection::start_collecting error: "
                  "rtcCreatePeerConnection returned error code: "
               << this->pc;
  }

  rtcSetUserPointer(this->pc, this);
  rtcSetLocalCandidateCallback(this->pc, WebRtcConnection::on_candidate);
  rtcSetLocalDescriptionCallback(this->pc, WebRtcConnection::on_description);
  rtcSetDataChannelCallback(this->pc, WebRtcConnection::on_datachannel);
  rtcSetClosedCallback(this->pc, WebRtcConnection::on_close);
  rtcSetErrorCallback(this->pc, WebRtcConnection::on_error);

  if (offerer) {
    rtcDataChannelInit reliable = {};
    this->reliable_dc =
        rtcCreateDataChannelEx(this->pc, RELIABLE_LABEL, &reliable);

    rtcDataChannelInit unreliable = {};
    unreliable.reliability.unreliable = true;
    unreliable.reliability.unordered = true;
    unreliable.reliability.maxRetransmits = 0;
    this->unreliable_dc =
        rtcCreateDataChannelEx(this->pc, UNRELIABLE_LABEL, &unreliable);

    if (this->reliable_dc < 0 || this->unreliable_dc < 0) {
      LOG(FATAL) << "WebRtcConnection::start_collecting error: "
                    "rtcCreateDataChannelEx returned error code: "
                 << this->reliable_dc.load() << ", "
                 << this->unreliable_dc.load();
    }

    this->setup_channel(this->reliable_dc);
    this->setup_channel(this->unreliable_dc);
  }

  this->current_state = Connection::kConnecting;
}

void WebRtcConnection::set_remote_description(const std::string& sdp,
                                              const std::string& type) {
  if (this->current_state != Connection::kConnecting || this->pc < 0) {
    LOG(FATAL) << "WebRtcConnection::set_remote_description error: bad state";
  }

  rtcSetRemoteDescription(this->pc, sdp.c_str(), type.c_str());
}

std::pair<std::string, std::string> WebRtcConnection::local_description() {
  std::lock_guard<std::mutex> guard(this->mutex);

  const std::pair<std::string, std::string> result = this->description;
  this->description = std::pair<std::string, std::string>("", "");
  return result;
}

void WebRtcConnection::add_candidate(const std::string& cand,
                                     const std::string& mid) {
  if (this->current_state != Connection::kConnecting || this->pc < 0) {
    LOG(FATAL) << "WebRtcConnection::add_candidate error: bad state";
  }

  rtcAddRemoteCandidate(this->pc, cand.c_str(), mid.c_str());
}

std::pair<std::string, std::string> WebRtcConnection::candidate() {
  std::lock_guard<std::mutex> guard(this->mutex);

  if (this->candidates.empty()) {
    return std::pair<std::string, std::string>("", "");
  }

  const std::pair<std::string, std::string> result = this->candidates.front();
  this->candidates.pop();
  return result;
}

void WebRtcConnection::poll() {
  std::lock_guard<std::mutex> guard(this->mutex);

  while (!this->events.empty()) {
    const Event event = this->events.front();
    this->events.pop();

    switch (event.kind) {
      case kEventOpen: {
        if (event.dc != this->reliable_dc.load()) {
          break;
        }

        const int max_payload = rtcMaxMessageSize(event.dc);
        this->max_payload = max_payload > 0 ? max_payload : 0;
        this->current_state = Connection::kReady;
      } break;
      case kEventClosed:
      case kEventFailed:
        this->current_state = Connection::kFailed;
        break;
    }
  }
}

bool WebRtcConnection::receive(Message* message) {
  std::lock_guard<std::mutex> guard(this->mutex);

  if (this->incoming.empty()) {
    return false;
  }

  *message = std::move(this->incoming.front());
  this->incoming.pop_front();
  return true;
}

bool WebRtcConnection::send(const Message& message) {
  if (this->current_state != Connection::kReady) {
    LOG(WARNING) << "WebRtcConnection::send: not connected";
    return false;
  }

  const int dc =
      message.reliable ? this->reliable_dc.load() : this->unreliable_dc.load();
  if (dc < 0) {
    LOG(WARNING) << "WebRtcConnection::send: channel not open";
    return false;
  }

  if (this->max_payload != 0 && message.bytes.size() > this->max_payload) {
    LOG(WARNING) << "WebRtcConnection::send: message is "
                 << message.bytes.size() << " bytes, limit is "
                 << this->max_payload;
    return false;
  }

  const char* data = reinterpret_cast<const char*>(message.bytes.data());
  const int size = static_cast<int>(message.bytes.size());
  return rtcSendMessage(dc, data, size) >= 0;
}

Connection::State WebRtcConnection::state() const {
  return this->current_state;
}

void WebRtcConnection::close() {
  if (this->reliable_dc >= 0) {
    rtcDelete(this->reliable_dc);
    this->reliable_dc = -1;
  }

  if (this->unreliable_dc >= 0) {
    rtcDelete(this->unreliable_dc);
    this->unreliable_dc = -1;
  }

  if (this->pc >= 0) {
    rtcDelete(this->pc);
    this->pc = -1;
  }

  this->current_state = Connection::kDead;
  this->max_payload = 0;
}

void WebRtcConnection::wait(uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(this->mutex);
  this->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
    return !this->events.empty() || !this->incoming.empty();
  });
}

void WebRtcConnection::on_description(int, const char* sdp, const char* type,
                                      void* user_ptr) {
  WebRtcConnection* connection = static_cast<WebRtcConnection*>(user_ptr);
  std::lock_guard<std::mutex> guard(connection->mutex);
  connection->description.first = sdp;
  connection->description.second = type;
}

void WebRtcConnection::on_candidate(int, const char* cand, const char* mid,
                                    void* user_ptr) {
  WebRtcConnection* connection = static_cast<WebRtcConnection*>(user_ptr);
  std::lock_guard<std::mutex> guard(connection->mutex);
  connection->candidates.push(std::pair<std::string, std::string>(cand, mid));
}

void WebRtcConnection::on_datachannel(int, int dc, void* user_ptr) {
  WebRtcConnection* connection = static_cast<WebRtcConnection*>(user_ptr);

  char label[32] = {};
  rtcGetDataChannelLabel(dc, label, sizeof(label));

  if (strcmp(label, UNRELIABLE_LABEL) == 0) {
    connection->unreliable_dc = dc;
  } else {
    connection->reliable_dc = dc;
  }

  connection->setup_channel(dc);
}

void WebRtcConnection::on_open(int dc, void* user_ptr) {
  WebRtcConnection* connection = static_cast<WebRtcConnection*>(user_ptr);
  connection->push_event(kEventOpen, dc);
}

void WebRtcConnection::on_close(int dc, void* user_ptr) {
  WebRtcConnection* connection = static_cast<WebRtcConnection*>(user_ptr);
  connection->push_event(kEventClosed, dc);
}

void WebRtcConnection::on_error(int dc, const char* error, void* user_ptr) {
  LOG(ERROR) << "WebRtcConnection::on_error: " << error;
  WebRtcConnection* connection = static_cast<WebRtcConnection*>(user_ptr);
  connection->push_event(kEventFailed, dc);
}

void WebRtcConnection::on_message(int dc, const char* message, int size,
                                  void* user_ptr) {
  if (size < 0) {
    LOG(WARNING) << "WebRtcConnection::on_message: dropped text message";
    return;
  }

  WebRtcConnection* connection = static_cast<WebRtcConnection*>(user_ptr);

  Message received;
  received.bytes.assign(message, message + size);
  received.reliable = dc == connection->reliable_dc.load();

  std::lock_guard<std::mutex> guard(connection->mutex);
  connection->incoming.push_back(std::move(received));
  connection->cv.notify_one();
}

void WebRtcConnection::setup_channel(int dc) {
  rtcSetOpenCallback(dc, WebRtcConnection::on_open);
  rtcSetClosedCallback(dc, WebRtcConnection::on_close);
  rtcSetErrorCallback(dc, WebRtcConnection::on_error);
  rtcSetMessageCallback(dc, WebRtcConnection::on_message);
}

void WebRtcConnection::push_event(EventKind kind, int dc) {
  std::lock_guard<std::mutex> guard(this->mutex);
  this->events.push(Event{kind, dc});
  this->cv.notify_one();
}

};  // namespace thoom
