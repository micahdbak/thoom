#pragma once

#include <atomic>

#include "thoom/thoom.h"

namespace thoom {

class PeerIdentity {
 public:
  inline static PeerIdentity* instance = nullptr;

  static bool init();
  static void quit();

  PeerIdentity();
  ~PeerIdentity() = default;

  thoom_pid issue();
  thoom_pid get_current() const;
  void set_current(thoom_pid new_pid);

 private:
  std::atomic<thoom_pid> next_pid{1};
  std::atomic<thoom_pid> current_pid{THOOM_PID_INVALID};
};

};  // namespace thoom
