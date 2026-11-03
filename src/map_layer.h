#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "colour.h"
#include "macros.h"
#include "tilesheet.h"

namespace thoom {

struct Tile {
  unsigned int tilesheet, x, y;
};

class MapLayer {
 public:
  MapLayer(int cols, int rows, int tile_width, int tile_height,
           Colour clear_colour);
  ~MapLayer();
  DISALLOW_COPY_AND_MOVE(MapLayer);

  void add_tile(int coord, Tile tile);
  void render_tile(int x, int y, int cols, int tile_width, int tile_height,
                   const std::vector<Tilesheet*>& tilesheets) const;

  std::vector<Tile>** tiles = nullptr;
  SDL_Texture* texture = nullptr;
  Colour clear_colour = kMask;

 private:
  int count = 0;
};

};  // namespace thoom
