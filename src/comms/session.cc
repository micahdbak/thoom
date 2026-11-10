#include "comms/session.h"

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "comms/bytes.h"
#include "comms/messages.h"
#include "comms/protocol.h"
#include "connection.h"
#include "thoom/thoom.h"

namespace thoom {

static int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

Session::Session(Role role, Connection* connection, const Options& options)
    : role(role), connection(connection), options(options) {
  if (role == kHost) {
    this->local_pid = THOOM_PID_HOST;
  }
}

void Session::poll() {
  if (this->state == kClosed) {
    return;
  }

  if (this->state == kWaiting) {
    if (this->connection->state() == Connection::kFailed) {
      this->lose(kReasonTransport);
      return;
    }

    if (this->connection->state() != Connection::kReady) {
      return;
    }

    this->start_handshake();
  }

  if (this->state == kHandshaking) {
    if (this->connection->state() != Connection::kReady) {
      this->lose(kReasonTransport);
      return;
    }

    this->receive_all();

    if (this->state == kHandshaking &&
        now_ms() >= this->handshake_deadline_ms) {
      LOG(WARNING) << "Session: the handshake timed out";
      this->refuse(kReasonTimeout);
    }

    return;
  }

  if (this->state == kReady) {
    if (this->connection->state() != Connection::kReady) {
      this->lose(kReasonTransport);
      return;
    }

    this->receive_all();
    return;
  }

  if (this->state == kRejected) {
    if (this->connection->state() != Connection::kReady ||
        now_ms() >= this->reject_deadline_ms) {
      this->shutdown();
    }
  }
}

bool Session::next_event(Event* event) {
  if (this->events.empty()) {
    return false;
  }

  *event = this->events.front();
  this->events.pop_front();
  return true;
}

bool Session::next_data(Data* data) {
  if (this->data.empty()) {
    return false;
  }

  *data = std::move(this->data.front());
  this->data.pop_front();
  return true;
}

bool Session::send(bool reliable, const void* data, uint32_t size) {
  if (this->state != kReady) {
    return false;
  }

  return this->send_frame(kFrameData, data, size, reliable);
}

void Session::start_handshake() {
  this->state = kHandshaking;
  this->handshake_deadline_ms = now_ms() + this->options.handshake_timeout_ms;

  if (this->role != kGuest) {
    return;
  }

  Hello hello = {};
  hello.role = static_cast<uint8_t>(this->role);

  ByteWriter writer;
  write_hello(&writer, hello);

  if (!this->send_message(kFrameHello, writer, true)) {
    this->lose(kReasonTransport);
  }
}

void Session::receive_all() {
  Message message;

  while (this->connection->receive(&message)) {
    const State before = this->state;
    this->handle(message);

    if (this->state != before) {
      return;
    }
  }
}

void Session::handle(const Message& message) {
  Frame frame = {};

  const DecodeResult result =
      decode_frame(message.bytes.data(),
                   static_cast<uint32_t>(message.bytes.size()), &frame);
  if (result != kDecodeOk) {
    LOG(WARNING) << "Session: malformed frame, decode result " << result;
    this->refuse(kReasonProtocol);
    return;
  }

  if (frame.version != this->options.version) {
    if (frame.kind != kFrameReject) {
      LOG(WARNING) << "Session: peer protocol version "
                   << static_cast<int>(frame.version) << ", expected "
                   << static_cast<int>(this->options.version);
      this->refuse(kReasonVersion);
      return;
    }

    // The reject message is deliberately version-independent, so a peer can
    // report a version mismatch instead of closing silently.
    this->handle_reject(frame);
    return;
  }

  if (frame.flags != kFrameFlagsNone) {
    LOG(WARNING) << "Session: frame flags " << static_cast<int>(frame.flags);
    this->refuse(kReasonProtocol);
    return;
  }

  switch (frame.kind) {
    case kFrameHello:
      this->handle_hello(frame);
      break;
    case kFrameWelcome:
      this->handle_welcome(frame);
      break;
    case kFrameReject:
      this->handle_reject(frame);
      break;
    case kFrameData:
      this->handle_data(message, frame);
      break;
    default:
      LOG(WARNING) << "Session: frame kind " << static_cast<int>(frame.kind);
      this->refuse(kReasonProtocol);
      break;
  }
}

void Session::handle_hello(const Frame& frame) {
  if (this->role != kHost || this->state != kHandshaking) {
    LOG(WARNING) << "Session: unexpected hello";
    this->refuse(kReasonProtocol);
    return;
  }

  ByteReader reader(frame.payload, frame.size);
  Hello hello = {};
  if (!read_hello(&reader, &hello)) {
    this->refuse(kReasonProtocol);
    return;
  }

  if (hello.role != static_cast<uint8_t>(kGuest)) {
    this->refuse(kReasonRole);
    return;
  }

  if (!this->can_accept_peer()) {
    this->refuse(kReasonFull);
    return;
  }

  Welcome welcome = {};
  welcome.pid = this->next_pid++;

  ByteWriter writer;
  write_welcome(&writer, welcome);

  if (!this->send_message(kFrameWelcome, writer, true)) {
    this->lose(kReasonTransport);
    return;
  }

  this->add_peer(welcome.pid);
  this->state = kReady;
  this->push(kJoin, welcome.pid, kReasonNone);
}

void Session::handle_welcome(const Frame& frame) {
  if (this->role != kGuest || this->state != kHandshaking) {
    LOG(WARNING) << "Session: unexpected welcome";
    this->refuse(kReasonProtocol);
    return;
  }

  ByteReader reader(frame.payload, frame.size);
  Welcome welcome = {};
  if (!read_welcome(&reader, &welcome)) {
    this->refuse(kReasonProtocol);
    return;
  }

  if (welcome.pid == THOOM_PID_HOST || welcome.pid == THOOM_PID_INVALID) {
    this->refuse(kReasonProtocol);
    return;
  }

  this->local_pid = welcome.pid;

  this->add_peer(THOOM_PID_HOST);
  this->state = kReady;
  this->push(kJoin, THOOM_PID_HOST, kReasonNone);
}

void Session::handle_reject(const Frame& frame) {
  if (this->state == kReady) {
    this->lose(kReasonProtocol);
    return;
  }

  if (this->state != kHandshaking) {
    return;
  }

  ByteReader reader(frame.payload, frame.size);
  Reject reject = {};
  if (!read_reject(&reader, &reject)) {
    this->refuse(kReasonProtocol);
    return;
  }

  Reason reason = static_cast<Reason>(reject.reason);
  if (reason < kReasonVersion || reason > kReasonTransport) {
    reason = kReasonProtocol;
  }

  this->push(kReject, THOOM_PID_INVALID, reason);
  this->shutdown();
}

void Session::handle_data(const Message& message, const Frame& frame) {
  if (this->state != kReady) {
    LOG(WARNING) << "Session: data before the handshake finished";
    this->refuse(kReasonProtocol);
    return;
  }

  Data data;
  data.bytes.assign(frame.payload, frame.payload + frame.size);
  data.reliable = message.reliable;

  this->data.push_back(std::move(data));
}

bool Session::send_frame(uint8_t kind, const void* payload, uint32_t size,
                         bool reliable) {
  Message message;
  if (!encode_frame(this->options.version, kind, kFrameFlagsNone, payload, size,
                    &message.bytes)) {
    return false;
  }

  message.reliable = reliable;
  return this->connection->send(message);
}

bool Session::send_message(uint8_t kind, const ByteWriter& writer,
                           bool reliable) {
  const std::vector<uint8_t>& bytes = writer.bytes();

  return this->send_frame(kind, bytes.data(),
                          static_cast<uint32_t>(bytes.size()), reliable);
}

void Session::refuse(Reason reason) {
  this->push(kReject, THOOM_PID_INVALID, reason);

  Reject reject = {};
  reject.reason = static_cast<uint8_t>(reason);

  ByteWriter writer;
  write_reject(&writer, reject);
  this->send_message(kFrameReject, writer, true);

  this->state = kRejected;
  this->reject_deadline_ms = now_ms() + this->options.reject_flush_ms;
}

void Session::lose(Reason reason) {
  const bool joined = this->state == kReady;
  const uint64_t peer =
      this->peer_count > 0 ? this->peers[0].pid : THOOM_PID_INVALID;

  this->shutdown();

  if (joined) {
    this->push(kLeave, peer, reason);
  } else {
    this->push(kReject, THOOM_PID_INVALID, reason);
  }
}

void Session::shutdown() {
  this->peer_count = 0;
  this->state = kClosed;
}

bool Session::can_accept_peer() const {
  return this->peer_count + 2 <= this->options.soft_cap;
}

void Session::add_peer(uint64_t pid) {
  if (this->peer_count >= kSessionMaxPeers) {
    return;
  }

  this->peers[this->peer_count].pid = pid;
  this->peer_count++;
}

void Session::push(EventKind kind, uint64_t peer, Reason reason) {
  Event event = {};
  event.kind = kind;
  event.peer = peer;
  event.reason = reason;

  this->events.push_back(event);
}

};  // namespace thoom
