#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>

#include "object/entity.h"
#include "object/object.h"
#include "thoom/thoom.h"

namespace thoom {

struct AreaQuery {
  SDL_FRect bounds = {};
  uint32_t groups = 0;
  Entity::AuthorityScope authority = Entity::AuthorityScope::kAll;
};

class Area : public Entity::Node {
 public:
  Area(Object& parent, std::optional<thoom_nid> explicit_nid, SDL_FRect offset,
       uint32_t groups);

  void step() override {}

  SDL_FRect bounds() const;

  float origin_x = 0.0f;
  float origin_y = 0.0f;
  SDL_FRect offset;

  uint32_t groups;
  bool enabled = true;
};

};  // namespace thoom
