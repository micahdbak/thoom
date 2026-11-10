#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "switch/switch.h"
#include "webrtc/webrtc_signaller.h"

namespace thoom {

class WebRtcSwitch : public Switch {
 public:
  WebRtcSwitch();
  ~WebRtcSwitch() override;

 protected:
  bool open_room(std::string* token) override;
  bool enter_room(const std::string& token) override;
  void poll_backend() override;
  bool send_raw(PeerHandle peer, const uint8_t* data, uint32_t size,
                bool reliable) override;
  void close_peer(PeerHandle peer) override;
  void close_room() override;

 private:
  struct Room {
    enum State {
      kIdle = 0,
      kOpen,
      kFailed,
    };

    WebRtcSignaller signaller;
    std::string code;
    State state = kIdle;
    int64_t next_ping_ms = 0;
  };

  struct Link {
    enum class EventKind {
      kOpen = 0,
      kLost,
    };

    struct Event {
      EventKind kind;
      bool reliable;
    };

    struct Message {
      bool reliable;
      std::vector<uint8_t> bytes;
    };

    struct Signal {
      bool description;
      std::string first;
      std::string second;
    };

    int peer = -1;
    int pc = -1;
    std::atomic<int> reliable_dc{-1};
    std::atomic<int> unreliable_dc{-1};
    bool reliable_open = false;
    bool unreliable_open = false;
    int max_message_size = 0;

    std::mutex mutex;
    std::deque<Event> events;
    std::deque<Message> inbox;
    std::deque<Signal> outbox;
  };

  struct Pending {
    int peer;
    std::vector<uint8_t> bytes;
  };

  bool begin(const std::string& code);
  void poll_room();
  void ping();
  void handle_event(const WebRtcSignaller::Event& event);
  bool signal_outbox();

  Link* create_link(int peer);
  bool create_channels(Link* link);
  Link* find_link(int peer);
  Link* free_link();
  bool ready(const Link* link) const;

  void poll_links();
  void poll_link(Link* link);
  void check_channels(Link* link);
  void apply_open(Link* link, bool reliable);
  void teardown_link(Link* link);
  void close_link(Link* link);
  void drop_link(Link* link);

  void flush_pending(Link* link);
  void drop_pending(int peer);
  bool write(Link* link, const uint8_t* data, uint32_t size, bool reliable);

  static void attach_channel(int dc, Link* link);
  static void detach_channel(int dc);

  static void on_local_description(int, const char* sdp, const char* type,
                                   void* user_ptr);
  static void on_local_candidate(int, const char* cand, const char* mid,
                                 void* user_ptr);
  static void on_datachannel(int pc, int dc, void* user_ptr);
  static void on_channel_open(int dc, void* user_ptr);
  static void on_channel_closed(int dc, void* user_ptr);
  static void on_channel_error(int dc, const char* error, void* user_ptr);
  static void on_channel_message(int dc, const char* message, int size,
                                 void* user_ptr);
  static void push_event(Link* link, Link::EventKind kind, int dc);

  Room room;
  Link links[kSwitchMaxPeers];

  bool offerer = false;
  bool announced = false;
  std::deque<Pending> pending_reliable;
};

};  // namespace thoom
