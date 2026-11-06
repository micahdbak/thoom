#include "map_layer.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "colour.h"
#include "renderer.h"
#include "texture.h"
#include "thoom/macros.h"
#include "tilesheet.h"

namespace thoom {

MapLayer::MapLayer(int cols, int rows, int tile_width, int tile_height,
                   Colour clear_colour) {
  size_t nbytes = sizeof(std::vector<Tile>*) * cols * rows;
  this->tiles = (std::vector<Tile>**)malloc(nbytes);

  // zero the allocated memory
  memset(this->tiles, 0, nbytes);

  this->texture = Texture::instance->create_texture(
      cols * tile_width, rows * tile_height, SDL_PIXELFORMAT_RGBA8888,
      SDL_SCALEMODE_LINEAR);

  if (this->texture == nullptr) {
    fprintf(stderr, THOOM_LOG "SDL_CreateTexture error: %s\n", SDL_GetError());
    exit(1);
  }

  Renderer::instance->clear(this->texture, clear_colour);

  this->clear_colour = clear_colour;
  this->count = cols * rows;
}

MapLayer::~MapLayer() {
  if (this->tiles != nullptr) {
    for (int coord = 0; coord < this->count; coord++) {
      std::vector<Tile>* vec = this->tiles[coord];
      if (vec != nullptr) delete vec;
    }

    free(this->tiles);
    this->tiles = nullptr;
  }

  if (this->texture != nullptr) {
    SDL_DestroyTexture(this->texture);
    this->texture = nullptr;
  }

  this->count = 0;
}

void MapLayer::add_tile(int coord, Tile tile) {
  std::vector<Tile>* vec = this->tiles[coord];

  if (vec == nullptr) {
    vec = new std::vector<Tile>();
    this->tiles[coord] = vec;
  }

  vec->push_back(tile);
}

void MapLayer::render_tile(int x, int y, int cols, int tile_width,
                           int tile_height,
                           const std::vector<Tilesheet*>& tilesheets) const {
  SDL_FRect dst_rect;
  dst_rect.x = float(tile_width * x);
  dst_rect.y = float(tile_width * y);
  dst_rect.w = float(tile_width);
  dst_rect.h = float(tile_height);

  Renderer::instance->draw_rect(this->texture, &dst_rect, this->clear_colour,
                                SDL_BLENDMODE_NONE);

  std::vector<Tile>* vec = this->tiles[(y * cols) + x];
  if (vec == nullptr) return;

  SDL_FRect src_rect;
  src_rect.w = float(tile_width);
  src_rect.h = float(tile_height);

  // render all tiles in order
  for (auto tile : *vec) {
    src_rect.x = float(tile_width * tile.x);
    src_rect.y = float(tile_height * tile.y);
    Renderer::instance->draw_texture(this->texture,
                                     tilesheets[tile.tilesheet]->texture,
                                     &src_rect, &dst_rect);
  }
}

};  // namespace thoom
