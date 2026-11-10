#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <queue>
#include <string>
#include <utility>

#include "connection.h"

namespace thoom {

class WebRtcChannels {
  enum EventKind {
    kEventOpen = 0,
    kEventClosed,
    kEventFailed,
  };

  struct Event {
    EventKind kind;
    int dc;
  };

 public:
  WebRtcChannels() = default;
  ~WebRtcChannels() = default;

  void start_collecting(bool offerer);

  void set_remote_description(const std::string& sdp, const std::string& type);

  std::pair<std::string, std::string> local_description();

  void add_candidate(const std::string& cand, const std::string& mid);

  std::pair<std::string, std::string> candidate();

  void poll();

  bool receive(Message* message);

  bool send(const Message& message);

  Connection::State state() const;

  void close();

  void wait(uint32_t timeout_ms);

 private:
  static void on_description(int, const char* sdp, const char* type,
                             void* user_ptr);

  static void on_candidate(int, const char* cand, const char* mid,
                           void* user_ptr);

  static void on_datachannel(int, int dc, void* user_ptr);

  static void on_open(int dc, void* user_ptr);

  static void on_close(int dc, void* user_ptr);

  static void on_error(int dc, const char* error, void* user_ptr);

  static void on_message(int dc, const char* message, int size, void* user_ptr);

  void setup_channel(int dc);

  void detach_channel(int dc);

  void push_event(EventKind kind, int dc);

  Connection::State current_state = Connection::kDead;
  int pc = -1;
  size_t max_payload = 0;
  bool reliable_open = false;
  bool unreliable_open = false;

  std::mutex mutex;
  std::condition_variable cv;
  std::queue<Event> events;
  std::deque<Message> incoming;
  std::pair<std::string, std::string> description;
  std::queue<std::pair<std::string, std::string>> candidates;

  std::atomic<int> reliable_dc{-1};
  std::atomic<int> unreliable_dc{-1};
};

};  // namespace thoom
