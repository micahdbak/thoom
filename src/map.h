#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "collider.h"
#include "colour.h"
#include "map_layer.h"
#include "object.h"
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

  void write(const char* map_path) const;

  bool has_tiles(int x, int y) const;
  bool add_region(int layer, int x, int y, int tilesheet, int src_x, int src_y,
                  int src_w, int src_h);

  bool add_tilesheet(const char* path);
  bool remove_tilesheet(int index);

  bool add_object(const ObjectRequest& object);
  bool remove_object(int index);

  void render_tile(int x, int y) const;

  std::string path = "";
  std::string title = "Unnamed map", description = "Floor 1";
  std::string ambience = "";

  int tile_width = 32, tile_height = 32, cols = 1, rows = 1;
  Colour background = kBlack;

  std::vector<Tilesheet*> tilesheets;
  MapLayer* layers[kLayerCount] = {};

  std::vector<ObjectRequest> objects;

  Collider* collider = nullptr;
};

};  // namespace thoom
