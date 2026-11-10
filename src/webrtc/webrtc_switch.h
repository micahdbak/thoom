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
      kSignalling,
      kCollecting,
      kEstablished,
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

  bool begin(const std::string& code);
  void poll_room();
  void ping();
  bool apply_event(const WebRtcSignaller::Event& event);
  bool signal_outbox();
  void fail_room();

  bool create_link();
  bool create_channels();
  void attach_channel(int dc);
  void detach_channel(int dc);
  void teardown_link();
  void drop_link();

  void check_channels();
  void poll_link();
  void apply_open(bool reliable);
  void flush_pending();
  bool write(const uint8_t* data, uint32_t size, bool reliable);

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
  static void push_event(Link::EventKind kind, int dc, void* user_ptr);

  Room room;
  Link link;

  bool offerer = false;
  bool announced = false;
  std::deque<std::vector<uint8_t>> pending_reliable;
};

};  // namespace thoom
