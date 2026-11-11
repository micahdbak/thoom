#include "switch/peer_identity.h"

#include "absl/log/log.h"
#include "thoom/thoom.h"

namespace thoom {

bool PeerIdentity::init() {
  if (PeerIdentity::instance != nullptr) {
    LOG(ERROR) << "PeerIdentity::init error: identity already initialized";
    return false;
  }

  PeerIdentity::instance = new PeerIdentity();
  return true;
}

void PeerIdentity::quit() {
  if (PeerIdentity::instance == nullptr) {
    return;
  }

  delete PeerIdentity::instance;
  PeerIdentity::instance = nullptr;
}

PeerIdentity::PeerIdentity() = default;

thoom_pid PeerIdentity::issue() {
  const thoom_pid pid = this->next_pid.fetch_add(1);

  // Host zero is never issued, and the invalid sentinel must stay unreachable.
  if (pid == THOOM_PID_HOST || pid == THOOM_PID_INVALID) {
    return THOOM_PID_INVALID;
  }

  return pid;
}

thoom_pid PeerIdentity::get_current() const { return this->current_pid.load(); }

void PeerIdentity::set_current(thoom_pid new_pid) {
  const thoom_pid previous = this->current_pid.exchange(new_pid);
  if (previous == new_pid) {
    return;
  }

  if (new_pid == THOOM_PID_INVALID) {
    LOG(INFO) << "PeerIdentity: current pid cleared";
    return;
  }

  LOG(INFO) << "PeerIdentity: current pid set to " << new_pid;
}

};  // namespace thoom
