#pragma once

#include <mutex>
#include <random>
#include <unordered_set>

#include "thoom/thoom.h"

namespace thoom {

class PeerIdentity {
 public:
  inline static PeerIdentity* instance = nullptr;

  static bool init();
  static void quit();

  PeerIdentity();
  ~PeerIdentity() = default;

  bool reserve(thoom_pid pid);
  bool contains(thoom_pid pid) const;
  thoom_pid propose();

 private:
  mutable std::mutex mutex;
  std::unordered_set<thoom_pid> reserved;
  std::mt19937 random;
};

};  // namespace thoom
