#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "webrtc/webrtc_connection.h"
#include "webrtc/webrtc_signaller.h"

namespace thoom {

class PeerThread {
 public:
  explicit PeerThread(bool offerer);
  ~PeerThread();

  void set_offerer(bool offerer);

 private:
  void message_loop();

  void no_connection();

  void waiting_for_peer();

  void connected();

  bool apply_event(const WebRtcSignaller::Event& event);

  void start_over();

  bool stop_requested();

  bool is_ready();

  void wait_ready();

  void pause(uint32_t timeout_ms);

  std::atomic<bool> offerer{false};

  std::mutex control_mutex;
  std::condition_variable control_cv;
  bool ready = false;
  bool stop = false;

  bool got_ping = false;
  std::thread* message_thread = nullptr;

  WebRtcConnection connection;
  WebRtcSignaller signaller;
};

};  // namespace thoom
