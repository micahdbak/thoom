#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "switch/frame.h"
#include "thoom/thoom.h"

namespace thoom {

constexpr uint32_t kSwitchMaxPeers = 10;

struct Datagram {
  thoom_pid peer;
  std::vector<uint8_t> bytes;
  bool reliable;
};

class Switch {
 public:
  enum State {
    kIdle = 0,
    kListening,
    kJoining,
    kJoined,
    kClosed,
  };

  enum Reason {
    kReasonNone = 0,
    kReasonVersion,
    kReasonRole,
    kReasonFull,
    kReasonProtocol,
    kReasonTimeout,
    kReasonTransport,
    kReasonClosed,
  };

  struct Event {
    enum Kind {
      kJoin = 0,
      kLeave,
    };

    Kind kind;
    thoom_pid peer;
    Reason reason;
  };

  struct Options {
    uint8_t version = kProtocolVersion;
    uint32_t soft_cap = 2;
    uint32_t handshake_timeout_ms = 5000;
    uint32_t reject_flush_ms = 250;
  };

  Switch() = default;
  virtual ~Switch() = default;

  bool host(const Options& options);
  bool join(const Options& options, const char* token);
  void poll();
  void close();

  bool send(const Datagram& datagram);
  bool receive(Datagram* datagram);
  bool next_event(Event* event);

  State state() const;
  Reason reason() const;
  const char* token() const;
  thoom_pid local_pid() const;
  uint32_t peer_count() const;
  thoom_pid peer_pid(uint32_t index) const;
  bool peer_ready(thoom_pid pid) const;

 protected:
  typedef uint64_t PeerHandle;

  virtual bool open_room(std::string* token) = 0;
  virtual bool enter_room(const std::string& token) = 0;
  virtual void poll_backend() = 0;
  virtual bool send_raw(PeerHandle peer, const uint8_t* data, uint32_t size,
                        bool reliable) = 0;
  virtual void close_peer(PeerHandle peer) = 0;
  virtual void close_room() = 0;

  void on_open(PeerHandle handle);
  void on_datagram(PeerHandle handle, const void* data, uint32_t size,
                   bool reliable);
  void on_drop(PeerHandle handle, Reason reason);

 private:
  enum PeerState {
    kPeerFree = 0,
    kPeerHandshaking,
    kPeerReady,
    kPeerRefused,
  };

  struct Peer {
    PeerHandle handle = 0;
    thoom_pid pid = THOOM_PID_INVALID;
    PeerState state = kPeerFree;
    int64_t deadline_ms = 0;
  };

  bool can_accept() const;
  void expire_peers();
  void shutdown();
  void finish(Reason reason);
  void refuse(Peer* peer, Reason reason);
  void abandon(Peer* peer);
  void violation(Peer* peer, Reason reason);

  void handle_frame(Peer* peer, const Frame& frame, bool reliable);
  void handle_hello(Peer* peer, const Frame& frame);
  void handle_welcome(Peer* peer, const Frame& frame);
  void handle_reject(Peer* peer, const Frame& frame);
  void handle_bye(Peer* peer);
  void handle_data(Peer* peer, const Frame& frame, bool reliable);

  bool send_frame(const Peer* peer, uint8_t kind, const void* payload,
                  uint32_t size, bool reliable);

  Peer* find_handle(PeerHandle handle);
  const Peer* find_pid(thoom_pid pid) const;
  Peer* add_peer(PeerHandle handle);
  void remove_peer(Peer* peer);
  uint32_t used_count() const;
  uint32_t ready_count() const;
  void push_event(Event::Kind kind, thoom_pid peer, Reason reason);

  Options options;
  State state_value = kIdle;
  Reason reason_value = kReasonNone;
  uint8_t role = kRoleHost;
  bool room_open = false;
  std::string token_value;
  thoom_pid local_pid_value = THOOM_PID_INVALID;
  uint64_t next_pid = 1;

  Peer peers[kSwitchMaxPeers];

  std::deque<Event> events;
  std::deque<Datagram> inbound;
};

};  // namespace thoom
