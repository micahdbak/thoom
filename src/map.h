#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "collider.h"
#include "macros.h"
#include "map_layer.h"
#include "tilesheet.h"

namespace thoom {

class Map {
 public:
  static constexpr int kBackgroundLayer = 0;
  static constexpr int kForegroundLayer = 1;
  static constexpr int kLayerCount = 2;

  explicit Map(const char* map_path);
  Map(int tile_width, int tile_height, int cols, int rows);
  ~Map();
  DISALLOW_COPY_AND_MOVE(Map);

  void write(const char* map_path) const;

  void render_tile(int x, int y) const;
  static void make_map_rect(int x, int y, int w, int h, SDL_FRect* src_rect,
                            SDL_FRect* dst_rect);

  std::string path = "";
  std::string title = "Unnamed map", description = "Floor 1";
  std::string ambience = "";

  int tile_width = 32, tile_height = 32, cols = 1, rows = 1;
  int bg_r = 0, bg_g = 0, bg_b = 0;

  std::vector<Tilesheet*> tilesheets;
  MapLayer* layers[kLayerCount] = {};

  std::vector<std::pair<std::string, std::string>> objects;

  Collider* collider = nullptr;
};

};  // namespace thoom
