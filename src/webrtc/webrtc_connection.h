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

class WebRtcConnection : public Connection {
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
  WebRtcConnection() = default;
  ~WebRtcConnection() override;

  void start_collecting(bool offerer);

  void set_remote_description(const std::string& sdp, const std::string& type);

  std::pair<std::string, std::string> local_description();

  void add_candidate(const std::string& cand, const std::string& mid);

  std::pair<std::string, std::string> candidate();

  void poll() override;

  bool receive(Message* message) override;

  bool send(const Message& message) override;

  State state() const override;

  void close() override;

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

  void push_event(EventKind kind, int dc);

  State current_state = kDead;
  int pc = -1;
  size_t max_payload = 0;

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
