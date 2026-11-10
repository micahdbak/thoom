#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "comms/bytes.h"
#include "comms/protocol.h"
#include "connection.h"
#include "thoom/thoom.h"

namespace thoom {

constexpr uint32_t kSessionMaxPeers = 10;
constexpr uint32_t kSessionSoftCap = 2;
constexpr uint32_t kSessionHandshakeTimeoutMs = 5000;
constexpr uint32_t kSessionRejectFlushMs = 250;

class Session {
 public:
  enum Role {
    kHost = 0,
    kGuest,
  };

  enum State {
    kWaiting = 0,
    kHandshaking,
    kReady,
    kRejected,
    kClosed,
  };

  enum EventKind {
    kJoin = 0,
    kLeave,
    kReject,
  };

  enum Reason {
    kReasonNone = 0,
    kReasonVersion,
    kReasonRole,
    kReasonFull,
    kReasonProtocol,
    kReasonTimeout,
    kReasonTransport,
  };

  struct Options {
    uint8_t version = kProtocolVersion;
    uint32_t soft_cap = kSessionSoftCap;
    uint32_t handshake_timeout_ms = kSessionHandshakeTimeoutMs;
    uint32_t reject_flush_ms = kSessionRejectFlushMs;
  };

  struct Event {
    EventKind kind;
    uint64_t peer;
    Reason reason;
  };

  struct Data {
    std::vector<uint8_t> bytes;
    bool reliable;
  };

  Session(Role role, Connection* connection, const Options& options);
  ~Session() = default;

  void poll();

  bool next_event(Event* event);
  bool next_data(Data* data);

  bool send(bool reliable, const void* data, uint32_t size);

  Role role;
  State state = kWaiting;
  uint64_t local_pid = THOOM_PID_INVALID;

 private:
  struct Peer {
    uint64_t pid;
  };

  void start_handshake();
  void receive_all();
  void handle(const Message& message);
  void handle_hello(const Frame& frame);
  void handle_welcome(const Frame& frame);
  void handle_reject(const Frame& frame);
  void handle_data(const Message& message, const Frame& frame);

  bool send_frame(uint8_t kind, const void* payload, uint32_t size,
                  bool reliable);
  bool send_message(uint8_t kind, const ByteWriter& writer, bool reliable);

  void refuse(Reason reason);
  void lose(Reason reason);
  void shutdown();

  bool can_accept_peer() const;
  void add_peer(uint64_t pid);
  void push(EventKind kind, uint64_t peer, Reason reason);

  Connection* connection;
  Options options;

  uint64_t next_pid = 1;
  Peer peers[kSessionMaxPeers];
  uint32_t peer_count = 0;

  int64_t handshake_deadline_ms = 0;
  int64_t reject_deadline_ms = 0;

  std::deque<Event> events;
  std::deque<Data> data;
};

};  // namespace thoom
