#include "switch/switch.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "switch/bytes.h"
#include "switch/protocol.h"
#include "thoom/thoom.h"

namespace thoom {

static int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool Switch::host(const Options& options) {
  if (this->state_value != kIdle) {
    return false;
  }

  this->options = options;
  this->role = kHostRole;
  this->local_pid_value = THOOM_PID_HOST;

  std::string token;
  if (!this->open_room(&token)) {
    this->state_value = kClosed;
    this->reason_value = kReasonTransport;
    return false;
  }

  this->token_value = token;
  this->room_open = true;
  this->state_value = kListening;
  return true;
}

bool Switch::join(const Options& options, const char* token) {
  if (this->state_value != kIdle || token == nullptr || token[0] == '\0') {
    return false;
  }

  this->options = options;
  this->role = kGuestRole;
  this->token_value = token;
  this->state_value = kJoining;

  if (!this->enter_room(this->token_value)) {
    this->state_value = kClosed;
    this->reason_value = kReasonTransport;
    return false;
  }

  this->room_open = true;
  return true;
}

void Switch::poll() {
  if (this->state_value == kClosed) {
    this->shutdown();
    return;
  }

  this->poll_backend();
  this->expire_peers();

  if (this->state_value == kClosed) {
    this->shutdown();
  }
}

void Switch::close() {
  if (this->state_value != kClosed) {
    for (uint32_t i = 0; i < this->peer_total; i++) {
      if (this->peers[i].state == kPeerReady) {
        this->send_frame(&this->peers[i], kFrameBye, nullptr, 0, true);
      }
    }

    this->state_value = kClosed;
    if (this->reason_value == kReasonNone) {
      this->reason_value = kReasonClosed;
    }
  }

  this->shutdown();
}

bool Switch::send(const Datagram& datagram) {
  if (this->state_value != kListening && this->state_value != kJoined) {
    return false;
  }

  Peer* peer = this->find_pid(datagram.peer);
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

Switch::State Switch::state() const { return this->state_value; }

Switch::Reason Switch::reason() const { return this->reason_value; }

const char* Switch::token() const { return this->token_value.c_str(); }

thoom_pid Switch::local_pid() const { return this->local_pid_value; }

uint32_t Switch::peer_count() const { return this->ready_count(); }

thoom_pid Switch::peer_pid(uint32_t index) const {
  uint32_t seen = 0;

  for (uint32_t i = 0; i < this->peer_total; i++) {
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
  if (this->state_value != kListening && this->state_value != kJoining) {
    this->close_peer(handle);
    return;
  }

  if (this->role == kGuestRole && this->peer_total > 0) {
    this->close_peer(handle);
    return;
  }

  Peer* peer = this->add_peer(handle);
  if (peer == nullptr) {
    this->close_peer(handle);
    return;
  }

  peer->deadline_ms = now_ms() + this->options.handshake_timeout_ms;

  if (this->role == kHostRole) {
    if (!this->can_accept()) {
      this->refuse(peer, kReasonFull);
    }
    return;
  }

  ByteWriter writer;
  writer.write_u8(static_cast<uint8_t>(kGuestRole));
  if (!this->send_frame(peer, kFrameHello, writer.bytes().data(),
                        static_cast<uint32_t>(writer.bytes().size()), true)) {
    this->finish(kReasonTransport);
  }
}

void Switch::on_datagram(PeerHandle handle, const void* data, uint32_t size,
                         bool reliable) {
  if (this->state_value == kClosed) {
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
  const uint32_t index = this->peer_index(handle);
  if (index >= kSwitchMaxPeers) {
    return;
  }

  const bool ready = this->peers[index].state == kPeerReady;
  const thoom_pid pid = this->peers[index].pid;

  this->remove_peer(index);

  if (ready) {
    this->push_event(Event::kLeave, pid, reason);
  }

  if (this->role == kGuestRole) {
    this->finish(reason);
  }
}

bool Switch::can_accept() const {
  return this->peer_total + 1 <= this->options.soft_cap;
}

void Switch::expire_peers() {
  const int64_t now = now_ms();

  for (uint32_t i = 0; i < this->peer_total;) {
    Peer* peer = &this->peers[i];

    if (peer->state == kPeerRefused && now >= peer->deadline_ms) {
      const PeerHandle handle = peer->handle;
      this->remove_peer(i);
      this->close_peer(handle);
      continue;
    }

    if (peer->state == kPeerHandshaking && now >= peer->deadline_ms) {
      const PeerHandle handle = peer->handle;
      if (this->role == kGuestRole) {
        this->finish(kReasonTimeout);
      }
      this->remove_peer(i);
      this->close_peer(handle);
      continue;
    }

    i++;
  }
}

void Switch::shutdown() {
  if (!this->room_open) {
    return;
  }

  this->room_open = false;
  this->peer_total = 0;
  this->close_room();
}

void Switch::finish(Reason reason) {
  if (this->state_value == kClosed) {
    return;
  }

  this->state_value = kClosed;
  this->reason_value = reason;
}

void Switch::refuse(Peer* peer, Reason reason) {
  if (peer->state == kPeerReady) {
    this->push_event(Event::kLeave, peer->pid, reason);
  }

  ByteWriter writer;
  writer.write_u8(static_cast<uint8_t>(reason));
  this->send_frame(peer, kFrameReject, writer.bytes().data(),
                   static_cast<uint32_t>(writer.bytes().size()), true);

  peer->state = kPeerRefused;
  peer->deadline_ms = now_ms() + this->options.reject_flush_ms;
}

void Switch::abandon(Peer* peer) {
  peer->state = kPeerRefused;
  peer->deadline_ms = now_ms();
}

void Switch::violation(Peer* peer, Reason reason) {
  if (this->role == kHostRole) {
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
  if (this->role != kHostRole || peer->state != kPeerHandshaking) {
    this->violation(peer, kReasonProtocol);
    return;
  }

  ByteReader reader(frame.payload, frame.size);
  const uint8_t hello_role = reader.read_u8();
  if (!reader.ok() || reader.remaining() != 0) {
    this->refuse(peer, kReasonProtocol);
    return;
  }

  if (hello_role != static_cast<uint8_t>(kGuestRole)) {
    this->refuse(peer, kReasonRole);
    return;
  }

  if (!this->can_accept()) {
    this->refuse(peer, kReasonFull);
    return;
  }

  const thoom_pid pid = this->next_pid++;

  ByteWriter writer;
  writer.write_u64(pid);
  if (!this->send_frame(peer, kFrameWelcome, writer.bytes().data(),
                        static_cast<uint32_t>(writer.bytes().size()), true)) {
    this->abandon(peer);
    return;
  }

  peer->pid = pid;
  peer->state = kPeerReady;
  this->push_event(Event::kJoin, pid, kReasonNone);
}

void Switch::handle_welcome(Peer* peer, const Frame& frame) {
  if (this->role != kGuestRole || peer->state != kPeerHandshaking) {
    this->violation(peer, kReasonProtocol);
    return;
  }

  ByteReader reader(frame.payload, frame.size);
  const thoom_pid pid = reader.read_u64();
  if (!reader.ok() || reader.remaining() != 0) {
    this->finish(kReasonProtocol);
    return;
  }

  if (pid == THOOM_PID_HOST || pid == THOOM_PID_INVALID) {
    this->finish(kReasonProtocol);
    return;
  }

  this->local_pid_value = pid;
  peer->pid = THOOM_PID_HOST;
  peer->state = kPeerReady;
  this->state_value = kJoined;
  this->push_event(Event::kJoin, THOOM_PID_HOST, kReasonNone);
}

void Switch::handle_reject(Peer* peer, const Frame& frame) {
  if (this->role == kHostRole) {
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

  this->finish(reason);
}

void Switch::handle_bye(Peer* peer) {
  const bool ready = peer->state == kPeerReady;
  const thoom_pid pid = peer->pid;
  const PeerHandle handle = peer->handle;

  this->remove_peer(this->peer_index(handle));
  this->close_peer(handle);

  if (ready) {
    this->push_event(Event::kLeave, pid, kReasonNone);
  }

  if (this->role == kGuestRole) {
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
  const uint32_t index = this->peer_index(handle);
  if (index >= kSwitchMaxPeers) {
    return nullptr;
  }

  return &this->peers[index];
}

Switch::Peer* Switch::find_pid(thoom_pid pid) {
  for (uint32_t i = 0; i < this->peer_total; i++) {
    if (this->peers[i].pid == pid) {
      return &this->peers[i];
    }
  }

  return nullptr;
}

const Switch::Peer* Switch::find_pid(thoom_pid pid) const {
  for (uint32_t i = 0; i < this->peer_total; i++) {
    if (this->peers[i].pid == pid) {
      return &this->peers[i];
    }
  }

  return nullptr;
}

uint32_t Switch::peer_index(PeerHandle handle) const {
  for (uint32_t i = 0; i < this->peer_total; i++) {
    if (this->peers[i].handle == handle) {
      return i;
    }
  }

  return kSwitchMaxPeers;
}

Switch::Peer* Switch::add_peer(PeerHandle handle) {
  if (this->peer_total >= kSwitchMaxPeers) {
    return nullptr;
  }

  Peer* peer = &this->peers[this->peer_total++];
  peer->handle = handle;
  peer->pid = THOOM_PID_INVALID;
  peer->state = kPeerHandshaking;
  peer->deadline_ms = 0;
  return peer;
}

void Switch::remove_peer(uint32_t index) {
  if (index >= this->peer_total) {
    return;
  }

  for (uint32_t i = index; i + 1 < this->peer_total; i++) {
    this->peers[i] = this->peers[i + 1];
  }

  this->peer_total--;
}

uint32_t Switch::ready_count() const {
  uint32_t count = 0;

  for (uint32_t i = 0; i < this->peer_total; i++) {
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
