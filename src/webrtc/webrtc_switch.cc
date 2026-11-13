#include "webrtc/webrtc_switch.h"

#include <rtc/rtc.h>

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "switch/switch.h"
#include "thoom/log.h"
#include "webrtc/webrtc_signaller.h"

#define CONNECT_PING_MS 500

// The host is peer zero on the signalling connection, so a guest always
// addresses that id.
#define HOST_PEER 0

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
    thoom_log_error("WebRtcSwitch::enter_room: invalid connection code");
    return false;
  }

  this->offerer = true;
  return this->begin(token);
}

void WebRtcSwitch::poll_backend() {
  if (this->room.state == Room::kOpen) {
    this->poll_room();
  }

  this->poll_links();
}

bool WebRtcSwitch::send_raw(PeerHandle peer, const uint8_t* data, uint32_t size,
                            bool reliable) {
  const int id = static_cast<int>(peer);
  Link* link = this->find_link(id);

  if (reliable) {
    if (link == nullptr || !link->reliable_open) {
      this->pending_reliable.push_back(
          Pending{id, std::vector<uint8_t>(data, data + size)});
      return true;
    }

    if (link->max_message_size > 0 &&
        size > static_cast<uint32_t>(link->max_message_size)) {
      thoom_log_error("WebRtcSwitch::send_raw: %" PRIu32
                      " bytes exceeds the peer limit of %d",
                      size, link->max_message_size);
      return false;
    }
  } else if (link == nullptr || !link->unreliable_open) {
    return true;
  }

  return this->write(link, data, size, reliable);
}

void WebRtcSwitch::close_peer(PeerHandle peer) {
  Link* link = this->find_link(static_cast<int>(peer));
  if (link != nullptr) {
    this->close_link(link);
  }

  this->drop_pending(static_cast<int>(peer));
}

void WebRtcSwitch::close_room() {
  for (Link& link : this->links) {
    if (link.peer >= 0) {
      this->teardown_link(&link);
    }
  }

  this->pending_reliable.clear();
  this->room.signaller.disconnect();
  this->room.state = Room::kIdle;
  this->room.code.clear();
  this->room.next_ping_ms = 0;
  this->offerer = false;
  this->announced = false;
}

bool WebRtcSwitch::begin(const std::string& code) {
  if (this->room.state != Room::kIdle) {
    thoom_log_warn("WebRtcSwitch::begin: the room is not idle");
    return false;
  }

  if (!this->room.signaller.connect(code)) {
    thoom_log_error("WebRtcSwitch::begin: signalling connect failed");
    this->room.state = Room::kFailed;
    return false;
  }

  this->room.code = code;
  this->room.state = Room::kOpen;
  this->room.next_ping_ms = 0;
  this->announced = false;
  return true;
}

void WebRtcSwitch::poll_room() {
  // The guest reports its peer before the link exists, so a failed rendezvous
  // has somewhere to go and the handshake deadline covers the whole join.
  if (this->offerer && !this->announced) {
    this->announced = true;
    this->on_open(HOST_PEER);
  }

  this->room.signaller.poll();

  const WebRtcSignaller::State state = this->room.signaller.get_state();
  if (state == WebRtcSignaller::kNotConnected) {
    // The signalling server is the front door. Losing it stops new guests
    // from arriving, but the links that already exist keep running.
    thoom_log_error("WebRtcSwitch: the signalling server is gone");
    this->room.state = Room::kFailed;
    return;
  }

  if (state != WebRtcSignaller::kConnected) {
    return;
  }

  this->ping();

  WebRtcSignaller::Event event = {};
  while (this->room.signaller.receive(&event)) {
    if (event.kind != WebRtcSignaller::Event::kFail) {
      this->handle_event(event);
    }
  }

  if (!this->signal_outbox()) {
    thoom_log_error("WebRtcSwitch: a signal could not be sent");
  }
}

void WebRtcSwitch::ping() {
  const int64_t now = now_ms();
  if (now < this->room.next_ping_ms) {
    return;
  }

  this->room.next_ping_ms = now + CONNECT_PING_MS;

  if (this->offerer) {
    const Link* link = this->find_link(HOST_PEER);
    if (link == nullptr || !this->ready(link)) {
      this->room.signaller.send_ping(HOST_PEER);
    }
    return;
  }

  for (const Link& link : this->links) {
    if (link.peer >= 0 && !this->ready(&link)) {
      this->room.signaller.send_ping(link.peer);
    }
  }
}

void WebRtcSwitch::handle_event(const WebRtcSignaller::Event& event) {
  Link* link = this->find_link(event.peer);

  if (event.kind == WebRtcSignaller::Event::kGone) {
    if (link != nullptr) {
      this->close_link(link);
    }

    this->on_drop(static_cast<PeerHandle>(event.peer), kReasonClosed);
    return;
  }

  if (link == nullptr) {
    link = this->create_link(event.peer);
    if (link == nullptr) {
      thoom_log_warn("WebRtcSwitch: no link for signalling peer %d",
                     event.peer);
      return;
    }

    if (!this->offerer) {
      // The guest is waiting to hear that the host exists before it spends a
      // peer connection on the room.
      this->room.signaller.send_ping(event.peer);
    }
  }

  switch (event.kind) {
    case WebRtcSignaller::Event::kDescription: {
      const int result = rtcSetRemoteDescription(link->pc, event.first.c_str(),
                                                 event.second.c_str());
      if (result < 0) {
        thoom_log_error(
            "WebRtcSwitch: remote description rejected with "
            "code %d",
            result);
      }
    } break;
    case WebRtcSignaller::Event::kCandidate: {
      const int result = rtcAddRemoteCandidate(link->pc, event.first.c_str(),
                                               event.second.c_str());
      if (result < 0) {
        thoom_log_warn(
            "WebRtcSwitch: remote candidate rejected with code "
            "%d",
            result);
      }
    } break;
    case WebRtcSignaller::Event::kPing:
    case WebRtcSignaller::Event::kFail:
    case WebRtcSignaller::Event::kGone:
      break;
  }
}

bool WebRtcSwitch::signal_outbox() {
  for (Link& link : this->links) {
    if (link.peer < 0) {
      continue;
    }

    for (;;) {
      Link::Signal signal;
      {
        std::lock_guard<std::mutex> guard(link.mutex);
        if (link.outbox.empty()) {
          break;
        }
        signal = std::move(link.outbox.front());
        link.outbox.pop_front();
      }

      const bool sent = signal.description
                            ? this->room.signaller.send_description(
                                  link.peer, signal.first, signal.second)
                            : this->room.signaller.send_candidate(
                                  link.peer, signal.first, signal.second);
      if (!sent) {
        return false;
      }
    }
  }

  return true;
}

WebRtcSwitch::Link* WebRtcSwitch::create_link(int peer) {
  Link* link = this->free_link();
  if (link == nullptr) {
    return nullptr;
  }

  rtcConfiguration config = {};
  config.iceServers = ICE_SERVER_URLS;
  config.iceServersCount = ICE_SERVER_COUNT;
  config.certificateType = RTC_CERTIFICATE_DEFAULT;
  config.iceTransportPolicy = RTC_TRANSPORT_POLICY_ALL;
  config.enableIceTcp = true;

  const int pc = rtcCreatePeerConnection(&config);
  if (pc < 0) {
    thoom_log_error(
        "WebRtcSwitch: rtcCreatePeerConnection failed with "
        "code %d",
        pc);
    return nullptr;
  }

  link->peer = peer;
  link->pc = pc;

  rtcSetUserPointer(pc, link);
  rtcSetLocalDescriptionCallback(pc, WebRtcSwitch::on_local_description);
  rtcSetLocalCandidateCallback(pc, WebRtcSwitch::on_local_candidate);
  rtcSetDataChannelCallback(pc, WebRtcSwitch::on_datachannel);

  if (this->offerer) {
    if (!this->create_channels(link)) {
      this->teardown_link(link);
      return nullptr;
    }
  } else {
    this->on_open(static_cast<PeerHandle>(peer));
  }

  return link;
}

bool WebRtcSwitch::create_channels(Link* link) {
  rtcDataChannelInit reliable_init = {};
  const int reliable =
      rtcCreateDataChannelEx(link->pc, RELIABLE_LABEL, &reliable_init);
  if (reliable < 0) {
    thoom_log_error(
        "WebRtcSwitch: reliable channel creation failed with "
        "code %d",
        reliable);
    return false;
  }

  rtcDataChannelInit unreliable_init = {};
  unreliable_init.reliability.unordered = true;
  unreliable_init.reliability.unreliable = true;
  unreliable_init.reliability.maxRetransmits = 0;
  const int unreliable =
      rtcCreateDataChannelEx(link->pc, UNRELIABLE_LABEL, &unreliable_init);
  if (unreliable < 0) {
    thoom_log_error(
        "WebRtcSwitch: unreliable channel creation failed with "
        "code %d",
        unreliable);
    return false;
  }

  link->reliable_dc = reliable;
  link->unreliable_dc = unreliable;
  WebRtcSwitch::attach_channel(reliable, link);
  WebRtcSwitch::attach_channel(unreliable, link);
  return true;
}

WebRtcSwitch::Link* WebRtcSwitch::find_link(int peer) {
  for (Link& link : this->links) {
    if (link.peer == peer) {
      return &link;
    }
  }

  return nullptr;
}

WebRtcSwitch::Link* WebRtcSwitch::free_link() {
  for (Link& link : this->links) {
    if (link.peer < 0) {
      return &link;
    }
  }

  return nullptr;
}

bool WebRtcSwitch::ready(const Link* link) const {
  return link->reliable_open && link->unreliable_open;
}

void WebRtcSwitch::poll_links() {
  for (Link& link : this->links) {
    if (link.peer >= 0) {
      this->poll_link(&link);
    }
  }
}

void WebRtcSwitch::poll_link(Link* link) {
  this->check_channels(link);

  if (link->peer < 0) {
    return;
  }

  const PeerHandle handle = static_cast<PeerHandle>(link->peer);

  std::deque<Link::Event> events;
  std::deque<Link::Message> messages;
  {
    std::lock_guard<std::mutex> guard(link->mutex);
    events.swap(link->events);
    messages.swap(link->inbox);
  }

  bool lost = false;

  for (const Link::Event& event : events) {
    if (event.kind == Link::EventKind::kOpen) {
      this->apply_open(link, event.reliable);
    } else {
      lost = true;
    }
  }

  // Deliver what arrived before the link went away: the last frame can be a
  // deliberate bye, and dropping it would report a clean close as a failure.
  for (const Link::Message& message : messages) {
    if (link->peer < 0) {
      return;
    }

    this->on_datagram(handle, message.bytes.data(),
                      static_cast<uint32_t>(message.bytes.size()),
                      message.reliable);
  }

  if (lost && link->peer >= 0) {
    thoom_log_warn("WebRtcSwitch: the link to peer %d was lost", link->peer);
    this->drop_link(link);
  }
}

void WebRtcSwitch::check_channels(Link* link) {
  const int reliable = link->reliable_dc.load();
  const int unreliable = link->unreliable_dc.load();

  // A channel can open or die between its callback being installed and the
  // next poll, so query the library for the state the events did not report.
  if (reliable >= 0 && !link->reliable_open && rtcIsOpen(reliable)) {
    this->apply_open(link, true);
  }

  if (unreliable >= 0 && !link->unreliable_open && rtcIsOpen(unreliable)) {
    this->apply_open(link, false);
  }

  const bool lost = (reliable >= 0 && rtcIsClosed(reliable)) ||
                    (unreliable >= 0 && rtcIsClosed(unreliable));
  if (lost) {
    this->drop_link(link);
  }
}

void WebRtcSwitch::apply_open(Link* link, bool open_reliable) {
  if (open_reliable) {
    if (link->reliable_open) {
      return;
    }

    link->reliable_open = true;
    const int size = rtcMaxMessageSize(link->reliable_dc.load());
    link->max_message_size = size > 0 ? size : 0;
    this->flush_pending(link);
    return;
  }

  link->unreliable_open = true;
}

void WebRtcSwitch::teardown_link(Link* link) {
  WebRtcSwitch::detach_channel(link->reliable_dc.load());
  WebRtcSwitch::detach_channel(link->unreliable_dc.load());

  if (link->pc >= 0) {
    rtcSetLocalDescriptionCallback(link->pc, nullptr);
    rtcSetLocalCandidateCallback(link->pc, nullptr);
    rtcSetDataChannelCallback(link->pc, nullptr);
    rtcDeletePeerConnection(link->pc);
  }

  std::lock_guard<std::mutex> guard(link->mutex);
  link->peer = -1;
  link->pc = -1;
  link->reliable_dc = -1;
  link->unreliable_dc = -1;
  link->reliable_open = false;
  link->unreliable_open = false;
  link->max_message_size = 0;
  link->events.clear();
  link->inbox.clear();
  link->outbox.clear();
}

void WebRtcSwitch::close_link(Link* link) {
  const int peer = link->peer;

  this->teardown_link(link);
  this->drop_pending(peer);
}

void WebRtcSwitch::drop_link(Link* link) {
  const int peer = link->peer;

  this->close_link(link);
  this->on_drop(static_cast<PeerHandle>(peer), kReasonTransport);
}

void WebRtcSwitch::flush_pending(Link* link) {
  std::deque<Pending> kept;

  while (!this->pending_reliable.empty()) {
    Pending pending = std::move(this->pending_reliable.front());
    this->pending_reliable.pop_front();

    if (pending.peer != link->peer) {
      kept.push_back(std::move(pending));
      continue;
    }

    if (!this->write(link, pending.bytes.data(),
                     static_cast<uint32_t>(pending.bytes.size()), true)) {
      thoom_log_error("WebRtcSwitch: a buffered frame could not be sent");
    }
  }

  this->pending_reliable = std::move(kept);
}

void WebRtcSwitch::drop_pending(int peer) {
  std::deque<Pending> kept;

  while (!this->pending_reliable.empty()) {
    Pending pending = std::move(this->pending_reliable.front());
    this->pending_reliable.pop_front();

    if (pending.peer != peer) {
      kept.push_back(std::move(pending));
    }
  }

  this->pending_reliable = std::move(kept);
}

bool WebRtcSwitch::write(Link* link, const uint8_t* data, uint32_t size,
                         bool reliable) {
  const int dc =
      reliable ? link->reliable_dc.load() : link->unreliable_dc.load();
  if (dc < 0) {
    return false;
  }

  const int result = rtcSendMessage(dc, reinterpret_cast<const char*>(data),
                                    static_cast<int>(size));
  if (result < 0) {
    thoom_log_warn("WebRtcSwitch: send failed with code %d", result);
    return false;
  }

  return true;
}

void WebRtcSwitch::attach_channel(int dc, Link* link) {
  rtcSetUserPointer(dc, link);
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
  // cannot reach the link afterwards.
  rtcSetOpenCallback(dc, nullptr);
  rtcSetClosedCallback(dc, nullptr);
  rtcSetErrorCallback(dc, nullptr);
  rtcSetMessageCallback(dc, nullptr);
  rtcDeleteDataChannel(dc);
}

void WebRtcSwitch::on_local_description(int, const char* sdp, const char* type,
                                        void* user_ptr) {
  Link* link = static_cast<Link*>(user_ptr);

  Link::Signal signal;
  signal.description = true;
  signal.first = sdp != nullptr ? sdp : "";
  signal.second = type != nullptr ? type : "";

  std::lock_guard<std::mutex> guard(link->mutex);
  link->outbox.push_back(std::move(signal));
}

void WebRtcSwitch::on_local_candidate(int, const char* cand, const char* mid,
                                      void* user_ptr) {
  Link* link = static_cast<Link*>(user_ptr);

  Link::Signal signal;
  signal.description = false;
  signal.first = cand != nullptr ? cand : "";
  signal.second = mid != nullptr ? mid : "";

  std::lock_guard<std::mutex> guard(link->mutex);
  link->outbox.push_back(std::move(signal));
}

void WebRtcSwitch::on_datachannel(int, int dc, void* user_ptr) {
  Link* link = static_cast<Link*>(user_ptr);

  char label[CHANNEL_LABEL_SIZE] = {};
  if (rtcGetDataChannelLabel(dc, label, sizeof(label)) < 0) {
    thoom_log_warn("WebRtcSwitch: a data channel has no readable label");
    return;
  }

  // Both channels are created by the guest, so the host receives them here.
  // The labels are the only thing that tells the two channels apart.
  if (strcmp(label, RELIABLE_LABEL) == 0 && link->reliable_dc.load() < 0) {
    link->reliable_dc = dc;
  } else if (strcmp(label, UNRELIABLE_LABEL) == 0 &&
             link->unreliable_dc.load() < 0) {
    link->unreliable_dc = dc;
  } else {
    thoom_log_warn("WebRtcSwitch: ignored data channel %s", label);
    return;
  }

  WebRtcSwitch::attach_channel(dc, link);
}

void WebRtcSwitch::on_channel_open(int dc, void* user_ptr) {
  WebRtcSwitch::push_event(static_cast<Link*>(user_ptr), Link::EventKind::kOpen,
                           dc);
}

void WebRtcSwitch::on_channel_closed(int dc, void* user_ptr) {
  WebRtcSwitch::push_event(static_cast<Link*>(user_ptr), Link::EventKind::kLost,
                           dc);
}

void WebRtcSwitch::on_channel_error(int dc, const char* error, void* user_ptr) {
  thoom_log_error("WebRtcSwitch::on_channel_error: %s", error);
  WebRtcSwitch::push_event(static_cast<Link*>(user_ptr), Link::EventKind::kLost,
                           dc);
}

void WebRtcSwitch::on_channel_message(int dc, const char* message, int size,
                                      void* user_ptr) {
  if (size <= 0) {
    thoom_log_warn(
        "WebRtcSwitch: dropped a message that is not a binary payload");
    return;
  }

  Link* link = static_cast<Link*>(user_ptr);
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(message);

  Link::Message received;
  received.reliable = dc == link->reliable_dc.load();
  received.bytes.assign(bytes, bytes + size);

  std::lock_guard<std::mutex> guard(link->mutex);
  link->inbox.push_back(std::move(received));
}

void WebRtcSwitch::push_event(Link* link, Link::EventKind kind, int dc) {
  Link::Event event;
  event.kind = kind;
  event.reliable = dc == link->reliable_dc.load();

  std::lock_guard<std::mutex> guard(link->mutex);
  link->events.push_back(event);
}

};  // namespace thoom
