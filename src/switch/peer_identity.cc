#include "switch/peer_identity.h"

#include <cstdint>
#include <mutex>
#include <random>

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

PeerIdentity::PeerIdentity() : random(std::random_device{}()) {
  this->reserved.insert(THOOM_PID_HOST);
}

bool PeerIdentity::reserve(thoom_pid pid) {
  if (pid == THOOM_PID_INVALID) {
    return false;
  }

  std::lock_guard<std::mutex> guard(this->mutex);
  return this->reserved.insert(pid).second;
}

bool PeerIdentity::contains(thoom_pid pid) const {
  std::lock_guard<std::mutex> guard(this->mutex);
  return this->reserved.find(pid) != this->reserved.end();
}

thoom_pid PeerIdentity::propose() {
  std::lock_guard<std::mutex> guard(this->mutex);

  std::uniform_int_distribution<uint32_t> pick(1, THOOM_PID_INVALID - 1);
  return pick(this->random);
}

};  // namespace thoom
