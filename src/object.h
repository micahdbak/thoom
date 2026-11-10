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
  struct SpawnIdentity {
    bool replicated;
    thoom_pid pid;
  };

  Object() : lid(next_lid++) {
    if (!spawning_identity.replicated) {
      return;
    }

    this->replicated = true;
    this->owner_pid = spawning_identity.pid;
    this->gid = THOOM_GID_COMPOSE(spawning_identity.pid, this->lid);
  }
  virtual ~Object() = default;

  virtual void step() = 0;
  virtual void render() {}
  virtual void save_data() {}

  thoom_lid lid;
  thoom_gid gid = THOOM_GID_INVALID;

  thoom_pid owner_pid = THOOM_PID_INVALID;
  bool fixed_owner = false;
  bool replicated = false;

  bool deleted = false;

  inline static SpawnIdentity spawning_identity = {false, THOOM_PID_INVALID};

  inline static thoom_lid next_lid = 0;
};

struct ObjectRequest {
  std::string id;
  std::string options;
};

};  // namespace thoom
