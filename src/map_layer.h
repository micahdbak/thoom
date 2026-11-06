#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "colour.h"
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

  int coord(int x, int y) const;
  bool has_tiles(int x, int y) const;
  std::vector<Tile>* tiles_at(int x, int y) const;
  int tile_count(int x, int y) const;

  bool add_tile(int x, int y, Tile tile);
  bool place_tile(int x, int y, Tile tile);
  bool remove_tile(int x, int y);
  void clear_tile(int x, int y);
  bool fill_randomly(std::vector<Tile> tiles);

  void render_tile(int x, int y,
                   const std::vector<Tilesheet*>& tilesheets) const;

  int cols = 0, rows = 0, tile_width = 0, tile_height = 0;
  std::vector<Tile>** tiles = nullptr;
  SDL_Texture* texture = nullptr;
  Colour clear_colour = kMask;
};

};  // namespace thoom
