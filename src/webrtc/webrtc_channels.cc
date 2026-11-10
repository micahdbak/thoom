#include "webrtc/webrtc_channels.h"

#include <rtc/rtc.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
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

void WebRtcChannels::start_collecting(bool offerer) {
  if (this->current_state != Connection::kDead) {
    LOG(FATAL) << "WebRtcChannels::start_collecting error: already "
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
    LOG(FATAL) << "WebRtcChannels::start_collecting error: "
                  "rtcCreatePeerConnection returned error code: "
               << this->pc;
  }

  rtcSetUserPointer(this->pc, this);
  rtcSetLocalCandidateCallback(this->pc, WebRtcChannels::on_candidate);
  rtcSetLocalDescriptionCallback(this->pc, WebRtcChannels::on_description);
  rtcSetDataChannelCallback(this->pc, WebRtcChannels::on_datachannel);
  rtcSetClosedCallback(this->pc, WebRtcChannels::on_close);
  rtcSetErrorCallback(this->pc, WebRtcChannels::on_error);

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
      LOG(FATAL) << "WebRtcChannels::start_collecting error: "
                    "rtcCreateDataChannelEx returned error code: "
                 << this->reliable_dc.load() << ", "
                 << this->unreliable_dc.load();
    }

    this->setup_channel(this->reliable_dc);
    this->setup_channel(this->unreliable_dc);
  }

  this->reliable_open = false;
  this->unreliable_open = false;
  this->current_state = Connection::kConnecting;
}

void WebRtcChannels::set_remote_description(const std::string& sdp,
                                            const std::string& type) {
  if (this->current_state != Connection::kConnecting || this->pc < 0) {
    LOG(FATAL) << "WebRtcChannels::set_remote_description error: bad state";
  }

  rtcSetRemoteDescription(this->pc, sdp.c_str(), type.c_str());
}

std::pair<std::string, std::string> WebRtcChannels::local_description() {
  std::lock_guard<std::mutex> guard(this->mutex);

  const std::pair<std::string, std::string> result = this->description;
  this->description = std::pair<std::string, std::string>("", "");
  return result;
}

void WebRtcChannels::add_candidate(const std::string& cand,
                                   const std::string& mid) {
  if (this->current_state != Connection::kConnecting || this->pc < 0) {
    LOG(FATAL) << "WebRtcChannels::add_candidate error: bad state";
  }

  rtcAddRemoteCandidate(this->pc, cand.c_str(), mid.c_str());
}

std::pair<std::string, std::string> WebRtcChannels::candidate() {
  std::lock_guard<std::mutex> guard(this->mutex);

  if (this->candidates.empty()) {
    return std::pair<std::string, std::string>("", "");
  }

  const std::pair<std::string, std::string> result = this->candidates.front();
  this->candidates.pop();
  return result;
}

void WebRtcChannels::poll() {
  std::lock_guard<std::mutex> guard(this->mutex);

  while (!this->events.empty()) {
    const Event event = this->events.front();
    this->events.pop();

    switch (event.kind) {
      case kEventOpen: {
        if (event.dc == this->reliable_dc.load()) {
          const int max_payload = rtcMaxMessageSize(event.dc);
          this->max_payload = max_payload > 0 ? max_payload : 0;
          this->reliable_open = true;
        } else if (event.dc == this->unreliable_dc.load()) {
          this->unreliable_open = true;
        }

        if (this->reliable_open && this->unreliable_open) {
          this->current_state = Connection::kReady;
        }
      } break;
      case kEventClosed:
      case kEventFailed:
        this->current_state = Connection::kFailed;
        break;
    }
  }
}

bool WebRtcChannels::receive(Message* message) {
  std::lock_guard<std::mutex> guard(this->mutex);

  if (this->incoming.empty()) {
    return false;
  }

  *message = std::move(this->incoming.front());
  this->incoming.pop_front();
  return true;
}

bool WebRtcChannels::send(const Message& message) {
  if (this->current_state != Connection::kReady) {
    LOG(WARNING) << "WebRtcChannels::send: not connected";
    return false;
  }

  const int dc =
      message.reliable ? this->reliable_dc.load() : this->unreliable_dc.load();
  if (dc < 0) {
    LOG(WARNING) << "WebRtcChannels::send: channel not open";
    return false;
  }

  if (this->max_payload != 0 && message.bytes.size() > this->max_payload) {
    LOG(WARNING) << "WebRtcChannels::send: message is " << message.bytes.size()
                 << " bytes, limit is " << this->max_payload;
    return false;
  }

  const char* data = reinterpret_cast<const char*>(message.bytes.data());
  const int size = static_cast<int>(message.bytes.size());
  return rtcSendMessage(dc, data, size) >= 0;
}

Connection::State WebRtcChannels::state() const { return this->current_state; }

void WebRtcChannels::close() {
  if (this->reliable_dc >= 0) {
    this->detach_channel(this->reliable_dc);
    rtcDelete(this->reliable_dc);
    this->reliable_dc = -1;
  }

  if (this->unreliable_dc >= 0) {
    this->detach_channel(this->unreliable_dc);
    rtcDelete(this->unreliable_dc);
    this->unreliable_dc = -1;
  }

  if (this->pc >= 0) {
    // Detach first: deleting returns straight into our callbacks, which take
    // locks that libdatachannel holds during deletion.
    rtcSetClosedCallback(this->pc, nullptr);
    rtcSetErrorCallback(this->pc, nullptr);
    rtcSetDataChannelCallback(this->pc, nullptr);
    rtcSetLocalDescriptionCallback(this->pc, nullptr);
    rtcSetLocalCandidateCallback(this->pc, nullptr);
    rtcDelete(this->pc);
    this->pc = -1;
  }

  this->current_state = Connection::kDead;
  this->max_payload = 0;
  this->reliable_open = false;
  this->unreliable_open = false;
}

void WebRtcChannels::wait(uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(this->mutex);
  this->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
    return !this->events.empty() || !this->incoming.empty();
  });
}

void WebRtcChannels::on_description(int, const char* sdp, const char* type,
                                    void* user_ptr) {
  WebRtcChannels* connection = static_cast<WebRtcChannels*>(user_ptr);
  std::lock_guard<std::mutex> guard(connection->mutex);
  connection->description.first = sdp;
  connection->description.second = type;
}

void WebRtcChannels::on_candidate(int, const char* cand, const char* mid,
                                  void* user_ptr) {
  WebRtcChannels* connection = static_cast<WebRtcChannels*>(user_ptr);
  std::lock_guard<std::mutex> guard(connection->mutex);
  connection->candidates.push(std::pair<std::string, std::string>(cand, mid));
}

void WebRtcChannels::on_datachannel(int, int dc, void* user_ptr) {
  WebRtcChannels* connection = static_cast<WebRtcChannels*>(user_ptr);

  char label[32] = {};
  rtcGetDataChannelLabel(dc, label, sizeof(label));

  if (strcmp(label, UNRELIABLE_LABEL) == 0) {
    connection->unreliable_dc = dc;
  } else {
    connection->reliable_dc = dc;
  }

  connection->setup_channel(dc);
}

void WebRtcChannels::on_open(int dc, void* user_ptr) {
  WebRtcChannels* connection = static_cast<WebRtcChannels*>(user_ptr);
  connection->push_event(kEventOpen, dc);
}

void WebRtcChannels::on_close(int dc, void* user_ptr) {
  WebRtcChannels* connection = static_cast<WebRtcChannels*>(user_ptr);
  connection->push_event(kEventClosed, dc);
}

void WebRtcChannels::on_error(int dc, const char* error, void* user_ptr) {
  LOG(ERROR) << "WebRtcChannels::on_error: " << error;
  WebRtcChannels* connection = static_cast<WebRtcChannels*>(user_ptr);
  connection->push_event(kEventFailed, dc);
}

void WebRtcChannels::on_message(int dc, const char* message, int size,
                                void* user_ptr) {
  if (size < 0) {
    LOG(WARNING) << "WebRtcChannels::on_message: dropped text message";
    return;
  }

  WebRtcChannels* connection = static_cast<WebRtcChannels*>(user_ptr);

  Message received;
  received.bytes.assign(message, message + size);
  received.reliable = dc == connection->reliable_dc.load();

  std::lock_guard<std::mutex> guard(connection->mutex);
  connection->incoming.push_back(std::move(received));
  connection->cv.notify_one();
}

void WebRtcChannels::setup_channel(int dc) {
  rtcSetOpenCallback(dc, WebRtcChannels::on_open);
  rtcSetClosedCallback(dc, WebRtcChannels::on_close);
  rtcSetErrorCallback(dc, WebRtcChannels::on_error);
  rtcSetMessageCallback(dc, WebRtcChannels::on_message);
}

void WebRtcChannels::detach_channel(int dc) {
  rtcSetOpenCallback(dc, nullptr);
  rtcSetClosedCallback(dc, nullptr);
  rtcSetErrorCallback(dc, nullptr);
  rtcSetMessageCallback(dc, nullptr);
}

void WebRtcChannels::push_event(EventKind kind, int dc) {
  std::lock_guard<std::mutex> guard(this->mutex);
  this->events.push(Event{kind, dc});
  this->cv.notify_one();
}

};  // namespace thoom
