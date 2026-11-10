#pragma once

#include <deque>
#include <mutex>
#include <string>

namespace thoom {

class WebRtcSignaller {
 public:
  enum State {
    kNotConnected = 0,
    kConnecting,
    kConnected,
  };

  struct Event {
    enum Kind {
      kPing = 0,
      kDescription,
      kCandidate,
      kFail,
    };

    Kind kind;
    std::string first;
    std::string second;
  };

  WebRtcSignaller() = default;
  ~WebRtcSignaller() = default;

  static std::string make_code();
  static bool valid_code(const std::string& code);

  bool connect(const std::string& code);
  void disconnect();
  void poll();
  State get_state() const;

  bool send_ping();
  bool send_description(const std::string& sdp, const std::string& type);
  bool send_candidate(const std::string& cand, const std::string& mid);
  bool receive(Event* event);

 private:
  static void on_open(int, void* user_ptr);
  static void on_close(int, void* user_ptr);
  static void on_error(int, const char* error, void* user_ptr);
  static void on_message(int, const char* message, int size, void* user_ptr);

  bool send_message(const std::string& message);
  void push_message(const std::string& message);
  void assemble_events();

  int ws = -1;
  State state = kNotConnected;

  std::mutex mutex;
  std::deque<std::string> raw;
  std::deque<Event> events;
  bool opened = false;
  bool closed = false;
};

};  // namespace thoom
