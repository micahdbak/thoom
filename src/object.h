#pragma once

#include <string>

#include "thoom/thoom.h"

namespace thoom {

enum ObjectOrder {
  kObjectFirst,
  kObjectNormal,
  kObjectLast,
};

class Object {
 public:
  Object() : lid(next_lid++) {
    if (!spawning_replicated) {
      return;
    }

    this->replicated = true;
    this->gid = next_gid++;
  }
  virtual ~Object() = default;

  virtual void step() = 0;
  virtual void render() {}
  virtual void save_data() {}

  thoom_lid lid;
  thoom_gid gid = THOOM_GID_NULL;

  thoom_pid owner_pid = THOOM_PID_INVALID;
  bool fixed_owner = false;
  bool replicated = false;

  bool deleted = false;

  inline static bool spawning_replicated = false;

 private:
  inline static thoom_lid next_lid = 0;
  inline static thoom_gid next_gid = 1;
};

struct ObjectRequest {
  std::string id;
  std::string options;
};

};  // namespace thoom
