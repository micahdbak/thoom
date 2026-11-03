#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "macros.h"

namespace thoom {

class Tilesheet {
 public:
  Tilesheet(const char* tilesheet_path, int tile_width, int tile_height);
  ~Tilesheet();
  DISALLOW_COPY_AND_MOVE(Tilesheet);

  std::string path;
  SDL_Texture* texture = nullptr;
  int cols = 0, rows = 0;
};

};  // namespace thoom
