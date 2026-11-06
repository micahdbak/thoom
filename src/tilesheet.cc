#include "tilesheet.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "texture.h"
#include "thoom/macros.h"

namespace thoom {

Tilesheet::Tilesheet(const char* tilesheet_path, int tile_width,
                     int tile_height) {
  SDL_Surface* tilesheet_surface = SDL_LoadBMP(tilesheet_path);
  if (tilesheet_surface == nullptr) {
    fprintf(stderr,
            THOOM_LOG "Tilesheet::Tilesheet error: '%s' does not exist.\n",
            tilesheet_path);
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
