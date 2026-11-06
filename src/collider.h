#pragma once

#include <SDL3/SDL.h>

#include <cstddef>

namespace thoom {

// clang-format off
const struct ColliderPoint {
  float x, y;
} MapColliders[][4] = {
  {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}},  // full square

  {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f}},  // short tri NE
  {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f}, {0.0f, 0.5f}},  // half square N
  {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.5f}},  // horiz fat tri NE
  {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}},  // tri NW
  {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}},  // tri NE
  {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f}, {0.0f, 1.0f}},  // horiz fat tri NW
  {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.5f}},  // short tri NW

  {{1.0f, 0.5f}, {1.0f, 1.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}},  // short tri SE
  {{0.0f, 0.5f}, {1.0f, 0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f}},  // half square S
  {{0.0f, 0.5f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}},  // horiz fat tri SE
  {{0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}},  // tri SW
  {{1.0f, 0.0f}, {1.0f, 0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f}},  // tri SE
  {{0.0f, 0.0f}, {1.0f, 0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f}},  // horiz fat tri SW
  {{0.0f, 0.5f}, {1.0f, 1.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}},  // short tri SW

  {{1.0f, 0.0f}, {1.0f, 0.5f}, {1.0f, 1.0f}, {0.5f, 1.0f}},  // skinny tri SE
  {{0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 1.0f}},  // half square E
  {{0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}},  // vert fat tri SE
  {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 1.0f}},  // vert fat tri NE
  {{0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.5f}, {1.0f, 1.0f}},  // skinny tri NE

  {{0.0f, 0.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.5f}},  // skinny tri SW
  {{0.0f, 0.0f}, {0.5f, 0.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}},  // half square W
  {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}},  // vert fat tri SW
  {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}},  // vert fat tri NW
  {{0.0f, 0.0f}, {0.5f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.5f}},  // skinny tri NW

  {{0.0f, 0.0f}, {1.0f, 0.5f}, {1.0f, 1.0f}, {0.0f, 0.5f}},  // NE/SW
  {{1.0f, 0.0f}, {1.0f, 0.5f}, {0.0f, 1.0f}, {0.0f, 0.5f}},  // SE/NW

  {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 1.0f}, {0.5f, 1.0f}},  // NE/SW
  {{0.5f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}, {0.0f, 1.0f}},  // SE/NW

  {{0.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.5f}, {1.0f, 1.0f}},  // NE
  {{1.0f, 0.0f}, {1.0f, 0.5f}, {0.5f, 1.0f}, {0.0f, 1.0f}},  // SE
  {{0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 1.0f}, {0.0f, 0.5f}},  // SW
  {{0.0f, 0.5f}, {0.5f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}},  // NW

  {{0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.1f}, {1.0f, 0.5f}},  // NE
  {{1.0f, 0.5f}, {1.0f, 0.9f}, {1.0f, 1.0f}, {0.5f, 1.0f}},  // SE
  {{0.0f, 0.5f}, {0.5f, 1.0f}, {0.1f, 1.0f}, {0.0f, 1.0f}},  // SW
  {{0.0f, 0.0f}, {0.1f, 0.0f}, {0.5f, 0.0f}, {0.0f, 0.5f}},  // NW

  {{0.0f, 0.0f}, {0.5f, 0.0f}, {0.5f, 0.75f}, {0.0f, 1.0f}},  // special
  {{0.5f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.75f}},  // special
};
// clang-format on

const size_t n_MapColliders = sizeof(MapColliders) / sizeof(MapColliders[0]);

struct Quad {
  SDL_FPoint vertex[4];
};

class Collider {
 public:
  Collider(int tile_width, int tile_height, int cols, int rows);
  ~Collider();

  int get(int x, int y) const;
  bool set(int x, int y, int collider);

  bool point_in_collider(float x, float y) const;
  bool in_sight(int x0, int y0, int x1, int y1, int* next_x, int* next_y) const;
  void random_target(int x, int y, int* next_x, int* next_y) const;

  int* collision = nullptr;
  Quad* quads = nullptr;

 private:
  int tile_width, tile_height, cols, rows;
};

};  // namespace thoom
