#include "object/area.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <optional>

#include "object/entity.h"
#include "object/object.h"
#include "thoom/thoom.h"

namespace thoom {

Area::Area(Object& parent, std::optional<thoom_nid> explicit_nid,
           SDL_FRect offset, uint32_t groups)
    : Entity::Node(parent, explicit_nid), offset(offset), groups(groups) {}

SDL_FRect Area::bounds() const {
  return {this->origin_x + this->offset.x, this->origin_y + this->offset.y,
          this->offset.w, this->offset.h};
}

};  // namespace thoom
