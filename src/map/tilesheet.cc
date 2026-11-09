#include "map/tilesheet.h"

#include <SDL3/SDL.h>

#include "absl/log/log.h"
#include "texture/texture.h"

namespace thoom {

Tilesheet::Tilesheet(const char* tilesheet_path, int tile_width,
                     int tile_height) {
  SDL_Surface* tilesheet_surface = SDL_LoadBMP(tilesheet_path);
  if (tilesheet_surface == nullptr) {
    LOG(ERROR) << "Tilesheet::Tilesheet error: '" << tilesheet_path
               << "' does not exist";
    return;
  }

  this->cols = tilesheet_surface->w / tile_width;
  this->rows = tilesheet_surface->h / tile_height;

  this->texture = Texture::instance->create_texture_from_surface(
      tilesheet_surface, SDL_SCALEMODE_LINEAR);
  SDL_DestroySurface(tilesheet_surface);
  tilesheet_surface = nullptr;

  this->path = tilesheet_path;
}

Tilesheet::~Tilesheet() {
  SDL_DestroyTexture(this->texture);
  this->texture = nullptr;
}

};  // namespace thoom
