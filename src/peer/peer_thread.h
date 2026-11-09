#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "peer/peer.h"
#include "webrtc/webrtc_connection.h"
#include "webrtc/webrtc_signaller.h"

namespace thoom {

class PeerThread {
 public:
  PeerThread(Peer* peer, bool offerer);
  ~PeerThread();

  void set_offerer(bool offerer);

 private:
  void message_loop();

  void no_connection();

  void waiting_for_peer();

  void connected();

  bool apply_event(const WebRtcSignaller::Event& event);

  void shut_down();

  void start_over();

  bool stop_requested();

  bool is_ready();

  void wait_ready();

  void pause(uint32_t timeout_ms);

  Peer* peer = nullptr;
  std::atomic<bool> offerer{false};

  std::mutex control_mutex;
  std::condition_variable control_cv;
  bool ready = false;
  bool stop = false;

  bool got_ping = false;
  std::thread* message_thread = nullptr;

  WebRtcConnection* connection = nullptr;
  WebRtcSignaller* signaller = nullptr;
};

};  // namespace thoom
