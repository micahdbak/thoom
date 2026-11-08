#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include "peer/peer_connection.h"
#include "peer/peer_signaller.h"

namespace thoom {

class PeerThread {
 public:
  PeerThread(bool offerer);
  ~PeerThread();

  void set_offerer(bool offerer);

 private:
  bool should_stop();
  void message_loop();

  void start_over();
  void no_connection();
  void waiting_for_peer();
  void connected();

  std::atomic<bool> offerer{false};
  std::atomic<bool> ready{false};
  bool got_ping = false;

  std::thread* message_thread = nullptr;
  bool stop_message_loop = false;
  std::mutex stop_message_loop_mutex;

  PeerConnection connection;
  PeerSignaller signaller;
};

};  // namespace thoom
