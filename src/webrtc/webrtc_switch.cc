#include "webrtc/webrtc_switch.h"

#include <rtc/rtc.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "switch/switch.h"
#include "webrtc/webrtc_signaller.h"

#define CONNECT_PING_MS 500

// A WebRTC session has one peer, so every report and every send uses the same
// handle.
#define LINK_HANDLE 1

#define RELIABLE_LABEL "reliable"
#define UNRELIABLE_LABEL "unreliable"

#define CHANNEL_LABEL_SIZE 32

#define ICE_SERVER_COUNT 2

static const char* ICE_SERVER_URLS[ICE_SERVER_COUNT] = {
    "stun:droppr.net:5051", "turn:droppr:droppr@droppr.net:5051"};

namespace thoom {

// The library holds global state, so the first switch loads it and the last
// switch unloads it, after every rtc object is gone.
static std::mutex library_mutex;
static uint32_t library_users = 0;

static void library_open() {
  std::lock_guard<std::mutex> guard(library_mutex);

  if (library_users == 0) {
    rtcPreload();
  }

  library_users++;
}

static void library_close() {
  std::lock_guard<std::mutex> guard(library_mutex);

  if (library_users == 0) {
    return;
  }

  library_users--;

  if (library_users == 0) {
    rtcCleanup();
  }
}

static int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

WebRtcSwitch::WebRtcSwitch() { library_open(); }

WebRtcSwitch::~WebRtcSwitch() {
  this->close_room();
  library_close();
}

bool WebRtcSwitch::open_room(std::string* token) {
  const std::string code = WebRtcSignaller::make_code();
  if (!this->begin(code)) {
    return false;
  }

  *token = code;
  return true;
}

bool WebRtcSwitch::enter_room(const std::string& token) {
  if (!WebRtcSignaller::valid_code(token)) {
    LOG(ERROR) << "WebRtcSwitch::enter_room: invalid connection code";
    return false;
  }

  this->offerer = true;
  return this->begin(token);
}

void WebRtcSwitch::poll_backend() {
  if (this->room.state == Room::kIdle || this->room.state == Room::kFailed) {
    return;
  }

  this->poll_room();

  if (this->room.state == Room::kIdle || this->room.state == Room::kFailed) {
    return;
  }

  this->poll_link();
}

bool WebRtcSwitch::send_raw(PeerHandle, const uint8_t* data, uint32_t size,
                            bool reliable) {
  if (this->room.state == Room::kIdle || this->room.state == Room::kFailed) {
    return false;
  }

  if (reliable) {
    if (!this->link.reliable_open) {
      this->pending_reliable.emplace_back(data, data + size);
      return true;
    }

    if (this->link.max_message_size > 0 &&
        size > static_cast<uint32_t>(this->link.max_message_size)) {
      LOG(ERROR) << "WebRtcSwitch::send_raw: " << size
                 << " bytes exceeds the peer limit of "
                 << this->link.max_message_size;
      return false;
    }
  } else if (!this->link.unreliable_open) {
    return true;
  }

  return this->write(data, size, reliable);
}

void WebRtcSwitch::close_peer(PeerHandle) {
  const bool signalling =
      this->room.signaller.get_state() != WebRtcSignaller::kNotConnected;

  this->teardown_link();

  if (!signalling) {
    this->room.state = Room::kFailed;
    return;
  }

  if (this->room.state == Room::kCollecting) {
    this->room.state = Room::kSignalling;
    this->room.next_ping_ms = 0;
  }
}

void WebRtcSwitch::close_room() {
  // The base sends a final bye through this link before the room closes, but
  // tearing the connection down can beat that frame to the wire, so a peer may
  // see a clean close as a lost link.
  this->teardown_link();
  this->room.signaller.disconnect();
  this->pending_reliable.clear();
  this->room.state = Room::kIdle;
  this->room.code.clear();
  this->room.next_ping_ms = 0;
  this->offerer = false;
  this->announced = false;
}

bool WebRtcSwitch::begin(const std::string& code) {
  if (this->room.state != Room::kIdle) {
    LOG(WARNING) << "WebRtcSwitch::begin: the room is not idle";
    return false;
  }

  if (!this->room.signaller.connect(code)) {
    LOG(ERROR) << "WebRtcSwitch::begin: signalling connect failed";
    this->room.state = Room::kFailed;
    return false;
  }

  this->room.code = code;
  this->room.state = Room::kSignalling;
  this->room.next_ping_ms = 0;
  this->announced = false;
  return true;
}

void WebRtcSwitch::poll_room() {
  // The guest reports its peer before the link exists, so a failed rendezvous
  // has somewhere to go and the handshake deadline covers the whole join.
  if (this->offerer && !this->announced) {
    this->announced = true;
    this->on_open(LINK_HANDLE);
  }

  this->room.signaller.poll();

  if (this->room.state == Room::kSignalling) {
    const WebRtcSignaller::State state = this->room.signaller.get_state();

    if (state == WebRtcSignaller::kNotConnected) {
      this->fail_room();
      return;
    }

    if (state != WebRtcSignaller::kConnected) {
      return;
    }
  }

  this->ping();

  WebRtcSignaller::Event event = {};
  while (this->room.signaller.receive(&event)) {
    if (event.kind == WebRtcSignaller::Event::kFail) {
      continue;
    }

    if (this->room.state == Room::kSignalling && !this->create_link()) {
      this->fail_room();
      return;
    }

    if (!this->apply_event(event)) {
      this->fail_room();
      return;
    }
  }

  if (this->room.state == Room::kCollecting && !this->signal_outbox()) {
    this->fail_room();
  }
}

void WebRtcSwitch::ping() {
  if (this->room.signaller.get_state() != WebRtcSignaller::kConnected) {
    return;
  }

  const int64_t now = now_ms();
  if (now < this->room.next_ping_ms) {
    return;
  }

  this->room.next_ping_ms = now + CONNECT_PING_MS;
  this->room.signaller.send_ping();
}

bool WebRtcSwitch::apply_event(const WebRtcSignaller::Event& event) {
  switch (event.kind) {
    case WebRtcSignaller::Event::kPing:
    case WebRtcSignaller::Event::kFail:
      return true;
    case WebRtcSignaller::Event::kDescription: {
      const int result = rtcSetRemoteDescription(
          this->link.pc, event.first.c_str(), event.second.c_str());
      if (result < 0) {
        LOG(ERROR) << "WebRtcSwitch: remote description rejected with code "
                   << result;
        return false;
      }
      return true;
    }
    case WebRtcSignaller::Event::kCandidate: {
      const int result = rtcAddRemoteCandidate(
          this->link.pc, event.first.c_str(), event.second.c_str());
      if (result < 0) {
        LOG(WARNING) << "WebRtcSwitch: remote candidate rejected with code "
                     << result;
      }
      return true;
    }
  }

  return true;
}

bool WebRtcSwitch::signal_outbox() {
  if (this->room.signaller.get_state() != WebRtcSignaller::kConnected) {
    return true;
  }

  for (;;) {
    Link::Signal signal;
    {
      std::lock_guard<std::mutex> guard(this->link.mutex);
      if (this->link.outbox.empty()) {
        return true;
      }
      signal = std::move(this->link.outbox.front());
      this->link.outbox.pop_front();
    }

    const bool sent =
        signal.description
            ? this->room.signaller.send_description(signal.first, signal.second)
            : this->room.signaller.send_candidate(signal.first, signal.second);
    if (!sent) {
      LOG(ERROR) << "WebRtcSwitch: signalling send failed";
      return false;
    }
  }
}

void WebRtcSwitch::fail_room() {
  LOG(ERROR) << "WebRtcSwitch: the rendezvous failed";

  this->teardown_link();
  this->room.state = Room::kFailed;
  this->on_drop(LINK_HANDLE, kReasonTransport);
}

bool WebRtcSwitch::create_link() {
  rtcConfiguration config = {};
  config.iceServers = ICE_SERVER_URLS;
  config.iceServersCount = ICE_SERVER_COUNT;
  config.certificateType = RTC_CERTIFICATE_DEFAULT;
  config.iceTransportPolicy = RTC_TRANSPORT_POLICY_ALL;
  config.enableIceTcp = true;

  const int pc = rtcCreatePeerConnection(&config);
  if (pc < 0) {
    LOG(ERROR) << "WebRtcSwitch: rtcCreatePeerConnection failed with code "
               << pc;
    return false;
  }

  this->link.pc = pc;
  rtcSetUserPointer(pc, this);
  rtcSetLocalDescriptionCallback(pc, WebRtcSwitch::on_local_description);
  rtcSetLocalCandidateCallback(pc, WebRtcSwitch::on_local_candidate);
  rtcSetDataChannelCallback(pc, WebRtcSwitch::on_datachannel);

  if (this->offerer) {
    if (!this->create_channels()) {
      return false;
    }
  } else {
    this->on_open(LINK_HANDLE);
  }

  this->room.state = Room::kCollecting;
  return true;
}

bool WebRtcSwitch::create_channels() {
  rtcDataChannelInit reliable_init = {};
  const int reliable =
      rtcCreateDataChannelEx(this->link.pc, RELIABLE_LABEL, &reliable_init);
  if (reliable < 0) {
    LOG(ERROR) << "WebRtcSwitch: reliable channel creation failed with code "
               << reliable;
    return false;
  }

  rtcDataChannelInit unreliable_init = {};
  unreliable_init.reliability.unordered = true;
  unreliable_init.reliability.unreliable = true;
  unreliable_init.reliability.maxRetransmits = 0;
  const int unreliable =
      rtcCreateDataChannelEx(this->link.pc, UNRELIABLE_LABEL, &unreliable_init);
  if (unreliable < 0) {
    LOG(ERROR) << "WebRtcSwitch: unreliable channel creation failed with code "
               << unreliable;
    return false;
  }

  this->link.reliable_dc = reliable;
  this->link.unreliable_dc = unreliable;
  this->attach_channel(reliable);
  this->attach_channel(unreliable);
  return true;
}

void WebRtcSwitch::attach_channel(int dc) {
  rtcSetUserPointer(dc, this);
  rtcSetOpenCallback(dc, WebRtcSwitch::on_channel_open);
  rtcSetClosedCallback(dc, WebRtcSwitch::on_channel_closed);
  rtcSetErrorCallback(dc, WebRtcSwitch::on_channel_error);
  rtcSetMessageCallback(dc, WebRtcSwitch::on_channel_message);
}

void WebRtcSwitch::detach_channel(int dc) {
  if (dc < 0) {
    return;
  }

  // Deleting an object that can still call back deadlocks in the library's own
  // lock, because the callback takes the link mutex during deletion. The
  // deletes also drop the user pointer, so a callback already on its way
  // cannot reach this object afterwards.
  rtcSetOpenCallback(dc, nullptr);
  rtcSetClosedCallback(dc, nullptr);
  rtcSetErrorCallback(dc, nullptr);
  rtcSetMessageCallback(dc, nullptr);
  rtcDeleteDataChannel(dc);
}

void WebRtcSwitch::teardown_link() {
  this->detach_channel(this->link.reliable_dc.load());
  this->detach_channel(this->link.unreliable_dc.load());

  if (this->link.pc >= 0) {
    rtcSetLocalDescriptionCallback(this->link.pc, nullptr);
    rtcSetLocalCandidateCallback(this->link.pc, nullptr);
    rtcSetDataChannelCallback(this->link.pc, nullptr);
    rtcDeletePeerConnection(this->link.pc);
  }

  this->pending_reliable.clear();

  std::lock_guard<std::mutex> guard(this->link.mutex);
  this->link.pc = -1;
  this->link.reliable_dc = -1;
  this->link.unreliable_dc = -1;
  this->link.reliable_open = false;
  this->link.unreliable_open = false;
  this->link.max_message_size = 0;
  this->link.events.clear();
  this->link.inbox.clear();
  this->link.outbox.clear();
}

void WebRtcSwitch::drop_link() {
  // The signaller outlives a handshake that never finished, so a host that
  // loses such a peer can keep its room open for another attempt.
  const bool room_alive =
      this->room.signaller.get_state() != WebRtcSignaller::kNotConnected;

  this->teardown_link();
  this->on_drop(LINK_HANDLE, kReasonTransport);

  if (this->room.state == Room::kIdle) {
    return;
  }

  this->room.state = room_alive ? Room::kSignalling : Room::kFailed;
  this->room.next_ping_ms = 0;
}

void WebRtcSwitch::check_channels() {
  const int reliable = this->link.reliable_dc.load();
  const int unreliable = this->link.unreliable_dc.load();

  // A channel can open or die between its callback being installed and the
  // next poll, so query the library for the state the events did not report.
  if (reliable >= 0 && !this->link.reliable_open && rtcIsOpen(reliable)) {
    this->apply_open(true);
  }

  if (unreliable >= 0 && !this->link.unreliable_open && rtcIsOpen(unreliable)) {
    this->apply_open(false);
  }

  const bool lost = (reliable >= 0 && rtcIsClosed(reliable)) ||
                    (unreliable >= 0 && rtcIsClosed(unreliable));
  if (lost) {
    this->drop_link();
  }
}

void WebRtcSwitch::poll_link() {
  if (this->link.pc < 0) {
    return;
  }

  this->check_channels();

  if (this->link.pc < 0) {
    return;
  }

  std::deque<Link::Event> events;
  std::deque<Link::Message> messages;
  {
    std::lock_guard<std::mutex> guard(this->link.mutex);
    events.swap(this->link.events);
    messages.swap(this->link.inbox);
  }

  bool lost = false;

  for (const Link::Event& event : events) {
    if (event.kind == Link::EventKind::kOpen) {
      this->apply_open(event.reliable);
    } else {
      lost = true;
    }
  }

  // Deliver what arrived before the link went away: the last frame is often a
  // deliberate bye, and dropping it would report a clean close as a failure.
  for (const Link::Message& message : messages) {
    if (this->link.pc < 0) {
      return;
    }

    this->on_datagram(LINK_HANDLE, message.bytes.data(),
                      static_cast<uint32_t>(message.bytes.size()),
                      message.reliable);
  }

  if (lost) {
    LOG(WARNING) << "WebRtcSwitch: a data channel was lost";
    this->drop_link();
  }
}

void WebRtcSwitch::apply_open(bool reliable) {
  if (reliable) {
    if (this->link.reliable_open) {
      return;
    }

    this->link.reliable_open = true;
    const int size = rtcMaxMessageSize(this->link.reliable_dc.load());
    this->link.max_message_size = size > 0 ? size : 0;
    this->flush_pending();
  } else {
    if (this->link.unreliable_open) {
      return;
    }

    this->link.unreliable_open = true;
  }

  if (this->link.reliable_open && this->link.unreliable_open) {
    this->room.state = Room::kEstablished;
    this->room.signaller.disconnect();
  }
}

void WebRtcSwitch::flush_pending() {
  while (!this->pending_reliable.empty()) {
    const std::vector<uint8_t> bytes =
        std::move(this->pending_reliable.front());
    this->pending_reliable.pop_front();

    if (!this->write(bytes.data(), static_cast<uint32_t>(bytes.size()), true)) {
      LOG(ERROR) << "WebRtcSwitch: a buffered frame could not be sent";
    }
  }
}

bool WebRtcSwitch::write(const uint8_t* data, uint32_t size, bool reliable) {
  const int dc = reliable ? this->link.reliable_dc.load()
                          : this->link.unreliable_dc.load();
  if (dc < 0) {
    return false;
  }

  const int result = rtcSendMessage(dc, reinterpret_cast<const char*>(data),
                                    static_cast<int>(size));
  if (result < 0) {
    LOG(WARNING) << "WebRtcSwitch: send failed with code " << result;
    return false;
  }

  return true;
}

void WebRtcSwitch::on_local_description(int, const char* sdp, const char* type,
                                        void* user_ptr) {
  WebRtcSwitch* self = static_cast<WebRtcSwitch*>(user_ptr);

  Link::Signal signal;
  signal.description = true;
  signal.first = sdp != nullptr ? sdp : "";
  signal.second = type != nullptr ? type : "";

  std::lock_guard<std::mutex> guard(self->link.mutex);
  self->link.outbox.push_back(std::move(signal));
}

void WebRtcSwitch::on_local_candidate(int, const char* cand, const char* mid,
                                      void* user_ptr) {
  WebRtcSwitch* self = static_cast<WebRtcSwitch*>(user_ptr);

  Link::Signal signal;
  signal.description = false;
  signal.first = cand != nullptr ? cand : "";
  signal.second = mid != nullptr ? mid : "";

  std::lock_guard<std::mutex> guard(self->link.mutex);
  self->link.outbox.push_back(std::move(signal));
}

void WebRtcSwitch::on_datachannel(int, int dc, void* user_ptr) {
  WebRtcSwitch* self = static_cast<WebRtcSwitch*>(user_ptr);

  char label[CHANNEL_LABEL_SIZE] = {};
  if (rtcGetDataChannelLabel(dc, label, sizeof(label)) < 0) {
    LOG(WARNING) << "WebRtcSwitch: a data channel has no readable label";
    return;
  }

  // Both channels are created by the guest, so the host receives them here.
  // The labels are the only thing that tells the two channels apart.
  if (strcmp(label, RELIABLE_LABEL) == 0 && self->link.reliable_dc.load() < 0) {
    self->link.reliable_dc = dc;
  } else if (strcmp(label, UNRELIABLE_LABEL) == 0 &&
             self->link.unreliable_dc.load() < 0) {
    self->link.unreliable_dc = dc;
  } else {
    LOG(WARNING) << "WebRtcSwitch: ignored data channel " << label;
    return;
  }

  self->attach_channel(dc);
}

void WebRtcSwitch::on_channel_open(int dc, void* user_ptr) {
  WebRtcSwitch::push_event(Link::EventKind::kOpen, dc, user_ptr);
}

void WebRtcSwitch::on_channel_closed(int dc, void* user_ptr) {
  WebRtcSwitch::push_event(Link::EventKind::kLost, dc, user_ptr);
}

void WebRtcSwitch::on_channel_error(int dc, const char* error, void* user_ptr) {
  LOG(ERROR) << "WebRtcSwitch::on_channel_error: " << error;
  WebRtcSwitch::push_event(Link::EventKind::kLost, dc, user_ptr);
}

void WebRtcSwitch::on_channel_message(int dc, const char* message, int size,
                                      void* user_ptr) {
  if (size <= 0) {
    LOG(WARNING) << "WebRtcSwitch: dropped a message that is not a binary "
                 << "payload";
    return;
  }

  WebRtcSwitch* self = static_cast<WebRtcSwitch*>(user_ptr);

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(message);

  Link::Message received;
  received.reliable = dc == self->link.reliable_dc.load();
  received.bytes.assign(bytes, bytes + size);

  std::lock_guard<std::mutex> guard(self->link.mutex);
  self->link.inbox.push_back(std::move(received));
}

void WebRtcSwitch::push_event(Link::EventKind kind, int dc, void* user_ptr) {
  WebRtcSwitch* self = static_cast<WebRtcSwitch*>(user_ptr);

  Link::Event event;
  event.kind = kind;
  event.reliable = dc == self->link.reliable_dc.load();

  std::lock_guard<std::mutex> guard(self->link.mutex);
  self->link.events.push_back(event);
}

};  // namespace thoom
