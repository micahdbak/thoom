#include "switch/switch.h"

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "switch/bytes.h"
#include "switch/frame.h"
#include "switch/peer_identity.h"
#include "thoom/log.h"
#include "thoom/thoom.h"

namespace thoom {

static const char* reason_name(Switch::Reason reason) {
  switch (reason) {
    case Switch::kReasonNone:
      return "none";
    case Switch::kReasonVersion:
      return "version";
    case Switch::kReasonRole:
      return "role";
    case Switch::kReasonFull:
      return "full";
    case Switch::kReasonProtocol:
      return "protocol";
    case Switch::kReasonTimeout:
      return "timeout";
    case Switch::kReasonTransport:
      return "transport";
    case Switch::kReasonClosed:
      return "closed";
  }

  return "unknown";
}

Switch::~Switch() {
  // A live switch owns the acting pid; destroying it must not leave one set.
  if (this->pid_owned && PeerIdentity::instance != nullptr) {
    PeerIdentity::instance->set_current(THOOM_PID_INVALID);
  }
}

static int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool Switch::host(const Options& options) {
  if (this->state != kIdle) {
    return false;
  }

  this->options = options;
  this->role = kRoleHost;

  if (!this->open_room(&this->token) || this->token.empty()) {
    thoom_log_error("Switch: the room could not be opened");
    PeerIdentity::instance->set_current(THOOM_PID_INVALID);
    this->state = kClosed;
    this->reason = kReasonTransport;
    return false;
  }

  this->room_open = true;
  this->state = kListening;
  PeerIdentity::instance->set_current(THOOM_PID_HOST);
  this->pid_owned = true;
  thoom_log_info("Switch: hosting %s", this->token.c_str());
  return true;
}

bool Switch::join(const Options& options, const char* code) {
  if (this->state != kIdle || code == nullptr || code[0] == '\0') {
    return false;
  }

  this->options = options;
  this->role = kRoleGuest;
  this->token = code;

  if (!this->enter_room(this->token)) {
    thoom_log_error("Switch: the room %s could not be entered",
                    this->token.c_str());
    PeerIdentity::instance->set_current(THOOM_PID_INVALID);
    this->state = kClosed;
    this->reason = kReasonTransport;
    return false;
  }

  this->room_open = true;
  this->state = kJoining;
  PeerIdentity::instance->set_current(THOOM_PID_INVALID);
  thoom_log_info("Switch: joining %s", this->token.c_str());
  return true;
}

void Switch::poll() {
  if (this->state == kClosed) {
    this->shutdown();
    return;
  }

  this->poll_backend();
  this->expire_peers();

  if (this->state == kClosed) {
    this->shutdown();
  }
}

void Switch::close() {
  if (this->state != kClosed) {
    for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
      if (this->peers[i].state == kPeerReady) {
        this->send_frame(&this->peers[i], kFrameBye, nullptr, 0, true);
      }
    }

    this->state = kClosed;
    if (this->reason == kReasonNone) {
      this->reason = kReasonClosed;
    }

    PeerIdentity::instance->set_current(THOOM_PID_INVALID);
    this->pid_owned = false;
    thoom_log_info("Switch: closed: %s", reason_name(this->reason));
  }

  this->shutdown();
}

bool Switch::send(const Datagram& datagram) {
  if (this->state != kListening && this->state != kJoined) {
    return false;
  }

  const Peer* peer = this->find_pid(datagram.peer);
  if (peer == nullptr || peer->state != kPeerReady) {
    return false;
  }

  return this->send_frame(peer, kFrameData, datagram.bytes.data(),
                          static_cast<uint32_t>(datagram.bytes.size()),
                          datagram.reliable);
}

bool Switch::receive(Datagram* datagram) {
  if (this->inbound.empty()) {
    return false;
  }

  *datagram = std::move(this->inbound.front());
  this->inbound.pop_front();
  return true;
}

bool Switch::next_event(Event* event) {
  if (this->events.empty()) {
    return false;
  }

  *event = this->events.front();
  this->events.pop_front();
  return true;
}

Switch::State Switch::get_state() const { return this->state; }

Switch::Reason Switch::get_reason() const { return this->reason; }

const char* Switch::get_token() const { return this->token.c_str(); }

uint32_t Switch::peer_count() const { return this->ready_count(); }

thoom_pid Switch::peer_pid(uint32_t index) const {
  uint32_t seen = 0;

  for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
    if (this->peers[i].state != kPeerReady) {
      continue;
    }

    if (seen == index) {
      return this->peers[i].pid;
    }

    seen++;
  }

  return THOOM_PID_INVALID;
}

bool Switch::peer_ready(thoom_pid pid) const {
  const Peer* peer = this->find_pid(pid);
  return peer != nullptr && peer->state == kPeerReady;
}

void Switch::on_open(PeerHandle handle) {
  if (this->state != kListening && this->state != kJoining) {
    this->close_peer(handle);
    return;
  }

  if (this->find_handle(handle) != nullptr) {
    return;
  }

  if (this->role == kRoleGuest && this->used_count() > 0) {
    this->close_peer(handle);
    return;
  }

  Peer* peer = this->add_peer(handle);
  if (peer == nullptr) {
    this->close_peer(handle);
    return;
  }

  peer->deadline_ms = now_ms() + this->options.handshake_timeout_ms;
  thoom_log_info("Switch: link %" PRIu64 " opened as %s", handle,
                 this->role == kRoleHost ? "host" : "guest");

  if (this->role == kRoleHost) {
    if (!this->can_accept()) {
      this->refuse(peer, kReasonFull);
    }
    return;
  }

  if (!this->send_hello(peer)) {
    this->finish(kReasonTransport);
  }
}

void Switch::on_datagram(PeerHandle handle, const void* data, uint32_t size,
                         bool reliable) {
  if (this->state == kClosed) {
    return;
  }

  Peer* peer = this->find_handle(handle);
  if (peer == nullptr || peer->state == kPeerRefused) {
    return;
  }

  Frame frame = {};
  if (!decode_frame(data, size, &frame)) {
    this->violation(peer, kReasonProtocol);
    return;
  }

  if (frame.version != this->options.version && frame.kind != kFrameReject) {
    this->violation(peer, kReasonVersion);
    return;
  }

  if (frame.flags != kFrameFlagsNone) {
    this->violation(peer, kReasonProtocol);
    return;
  }

  this->handle_frame(peer, frame, reliable);
}

void Switch::on_drop(PeerHandle handle, Reason reason) {
  Peer* peer = this->find_handle(handle);
  if (peer == nullptr) {
    return;
  }

  const bool ready = peer->state == kPeerReady;
  const thoom_pid pid = peer->pid;

  this->remove_peer(peer);

  thoom_log_info("Switch: link %" PRIu64 " dropped: %s%s", handle,
                 reason_name(reason), ready ? " after ready" : " before ready");

  if (ready) {
    this->push_event(Event::kLeave, pid, reason);
  }

  if (this->role == kRoleGuest) {
    this->finish(reason);
  }
}

bool Switch::can_accept() const {
  const uint32_t players = this->used_count() + 1;
  return players <= this->options.soft_cap;
}

void Switch::expire_peers() {
  const int64_t now = now_ms();

  for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
    Peer* peer = &this->peers[i];
    if (peer->state == kPeerFree) {
      continue;
    }

    const bool expired =
        (peer->state == kPeerRefused || peer->state == kPeerHandshaking) &&
        now >= peer->deadline_ms;
    if (!expired) {
      continue;
    }

    thoom_log_info("Switch: link %" PRIu64 "%s", peer->handle,
                   peer->state == kPeerHandshaking ? " handshake expired"
                                                   : " reject flush expired");

    if (peer->state == kPeerHandshaking && this->role == kRoleGuest) {
      this->finish(kReasonTimeout);
    }

    const PeerHandle handle = peer->handle;
    this->remove_peer(peer);
    this->close_peer(handle);
  }
}

void Switch::shutdown() {
  if (!this->room_open) {
    return;
  }

  this->room_open = false;
  for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
    this->peers[i] = Peer();
  }
  this->close_room();
}

void Switch::finish(Reason reason) {
  if (this->state == kClosed) {
    return;
  }

  this->state = kClosed;
  this->reason = reason;
  PeerIdentity::instance->set_current(THOOM_PID_INVALID);
  this->pid_owned = false;
  thoom_log_info("Switch: session ended: %s", reason_name(reason));
}

void Switch::refuse(Peer* peer, Reason reason) {
  if (peer->state == kPeerReady) {
    this->push_event(Event::kLeave, peer->pid, reason);
  }

  ByteWriter writer;
  writer.write_u8(static_cast<uint8_t>(reason));
  this->send_frame(peer, kFrameReject, writer.bytes().data(),
                   static_cast<uint32_t>(writer.bytes().size()), true);

  thoom_log_info("Switch: refused link %" PRIu64 ": %s", peer->handle,
                 reason_name(reason));

  peer->state = kPeerRefused;
  peer->deadline_ms = now_ms() + this->options.reject_flush_ms;
}

void Switch::abandon(Peer* peer) {
  peer->state = kPeerRefused;
  peer->deadline_ms = now_ms();
}

void Switch::violation(Peer* peer, Reason reason) {
  if (this->role == kRoleHost) {
    this->refuse(peer, reason);
  } else {
    this->finish(reason);
  }
}

void Switch::handle_frame(Peer* peer, const Frame& frame, bool reliable) {
  switch (frame.kind) {
    case kFrameHello:
      this->handle_hello(peer, frame);
      break;
    case kFrameWelcome:
      this->handle_welcome(peer, frame);
      break;
    case kFrameReject:
      this->handle_reject(peer, frame);
      break;
    case kFrameBye:
      this->handle_bye(peer);
      break;
    case kFrameData:
      this->handle_data(peer, frame, reliable);
      break;
    default:
      this->violation(peer, kReasonProtocol);
      break;
  }
}

void Switch::handle_hello(Peer* peer, const Frame& frame) {
  if (this->role != kRoleHost || peer->state != kPeerHandshaking) {
    this->violation(peer, kReasonProtocol);
    return;
  }

  ByteReader reader(frame.payload, frame.size);
  const uint8_t hello_role = reader.read_u8();
  if (!reader.ok() || reader.remaining() != 0) {
    this->refuse(peer, kReasonProtocol);
    return;
  }

  if (hello_role != kRoleGuest) {
    this->refuse(peer, kReasonRole);
    return;
  }

  if (!this->can_accept()) {
    this->refuse(peer, kReasonFull);
    return;
  }

  const thoom_pid pid = PeerIdentity::instance->issue();
  if (pid == THOOM_PID_INVALID) {
    thoom_log_error("Switch: no pid remains to admit a guest");
    this->refuse(peer, kReasonFull);
    return;
  }

  ByteWriter writer;
  writer.write_u32(pid);
  if (!this->send_frame(peer, kFrameWelcome, writer.bytes().data(),
                        static_cast<uint32_t>(writer.bytes().size()), true)) {
    this->abandon(peer);
    return;
  }

  peer->pid = pid;
  peer->state = kPeerReady;
  thoom_log_info("Switch: admitted guest %" PRIu32, pid);
  this->push_event(Event::kJoin, pid, kReasonNone);
}

bool Switch::send_hello(Peer* peer) {
  ByteWriter writer;
  writer.write_u8(kRoleGuest);
  return this->send_frame(peer, kFrameHello, writer.bytes().data(),
                          static_cast<uint32_t>(writer.bytes().size()), true);
}

void Switch::handle_welcome(Peer* peer, const Frame& frame) {
  if (this->role != kRoleGuest || peer->state != kPeerHandshaking) {
    this->violation(peer, kReasonProtocol);
    return;
  }

  ByteReader reader(frame.payload, frame.size);
  const thoom_pid pid = reader.read_u32();
  if (!reader.ok() || reader.remaining() != 0) {
    this->finish(kReasonProtocol);
    return;
  }

  if (pid == THOOM_PID_HOST || pid == THOOM_PID_INVALID) {
    thoom_log_warn("Switch: welcome carried an unusable pid %" PRIu32, pid);
    this->finish(kReasonProtocol);
    return;
  }

  PeerIdentity::instance->set_current(pid);
  this->pid_owned = true;
  peer->pid = THOOM_PID_HOST;
  peer->state = kPeerReady;
  this->state = kJoined;
  thoom_log_info("Switch: joined as pid %" PRIu32, pid);
  this->push_event(Event::kJoin, THOOM_PID_HOST, kReasonNone);
}

void Switch::handle_reject(Peer* peer, const Frame& frame) {
  if (this->role == kRoleHost) {
    this->refuse(peer, kReasonProtocol);
    return;
  }

  ByteReader reader(frame.payload, frame.size);
  const uint8_t code = reader.read_u8();
  if (!reader.ok() || reader.remaining() != 0) {
    this->finish(kReasonProtocol);
    return;
  }

  Reason reason = static_cast<Reason>(code);
  if (reason < kReasonVersion || reason > kReasonTransport) {
    reason = kReasonProtocol;
  }

  thoom_log_info("Switch: join rejected: %s", reason_name(reason));
  this->finish(reason);
}

void Switch::handle_bye(Peer* peer) {
  const bool ready = peer->state == kPeerReady;
  const thoom_pid pid = peer->pid;
  const PeerHandle handle = peer->handle;

  this->remove_peer(peer);
  this->close_peer(handle);

  if (ready) {
    this->push_event(Event::kLeave, pid, kReasonNone);
  }

  if (this->role == kRoleGuest) {
    this->finish(kReasonClosed);
  }
}

void Switch::handle_data(Peer* peer, const Frame& frame, bool reliable) {
  if (peer->state != kPeerReady) {
    this->violation(peer, kReasonProtocol);
    return;
  }

  Datagram datagram;
  datagram.peer = peer->pid;
  datagram.bytes.assign(frame.payload, frame.payload + frame.size);
  datagram.reliable = reliable;

  this->inbound.push_back(std::move(datagram));
}

bool Switch::send_frame(const Peer* peer, uint8_t kind, const void* payload,
                        uint32_t size, bool reliable) {
  std::vector<uint8_t> bytes;
  if (!encode_frame(this->options.version, kind, kFrameFlagsNone, payload, size,
                    &bytes)) {
    return false;
  }

  return this->send_raw(peer->handle, bytes.data(),
                        static_cast<uint32_t>(bytes.size()), reliable);
}

Switch::Peer* Switch::find_handle(PeerHandle handle) {
  for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
    Peer* peer = &this->peers[i];
    if (peer->state != kPeerFree && peer->handle == handle) {
      return peer;
    }
  }

  return nullptr;
}

const Switch::Peer* Switch::find_pid(thoom_pid pid) const {
  for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
    const Peer* peer = &this->peers[i];
    if (peer->state != kPeerFree && peer->pid == pid) {
      return peer;
    }
  }

  return nullptr;
}

Switch::Peer* Switch::add_peer(PeerHandle handle) {
  for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
    Peer* peer = &this->peers[i];
    if (peer->state != kPeerFree) {
      continue;
    }

    peer->handle = handle;
    peer->pid = THOOM_PID_INVALID;
    peer->state = kPeerHandshaking;
    peer->deadline_ms = 0;
    return peer;
  }

  return nullptr;
}

void Switch::remove_peer(Peer* peer) {
  if (peer == nullptr) {
    return;
  }

  *peer = Peer();
}

uint32_t Switch::used_count() const {
  uint32_t count = 0;

  for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
    if (this->peers[i].state != kPeerFree) {
      count++;
    }
  }

  return count;
}

uint32_t Switch::ready_count() const {
  uint32_t count = 0;

  for (uint32_t i = 0; i < kSwitchMaxPeers; i++) {
    if (this->peers[i].state == kPeerReady) {
      count++;
    }
  }

  return count;
}

void Switch::push_event(Event::Kind kind, thoom_pid peer, Reason reason) {
  Event event;
  event.kind = kind;
  event.peer = peer;
  event.reason = reason;

  this->events.push_back(event);
}

};  // namespace thoom
