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
  this->cols = cols;
  this->rows = rows;
  this->tile_width = tile_width;
  this->tile_height = tile_height;

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
}

MapLayer::~MapLayer() {
  if (this->tiles != nullptr) {
    for (int coord = 0; coord < this->cols * this->rows; coord++) {
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

  this->cols = 0;
  this->rows = 0;
}

int MapLayer::coord(int x, int y) const {
  if (x < 0 || y < 0 || x >= this->cols || y >= this->rows) {
    return -1;
  }

  return (y * this->cols) + x;
}

bool MapLayer::has_tiles(int x, int y) const {
  std::vector<Tile>* vec = this->tiles_at(x, y);
  return vec != nullptr && !vec->empty();
}

std::vector<Tile>* MapLayer::tiles_at(int x, int y) const {
  int coord = this->coord(x, y);

  if (coord < 0) {
    return nullptr;
  }

  return this->tiles[coord];
}

int MapLayer::tile_count(int x, int y) const {
  std::vector<Tile>* vec = this->tiles_at(x, y);

  if (vec == nullptr) {
    return 0;
  }

  return int(vec->size());
}

bool MapLayer::add_tile(int x, int y, Tile tile) {
  int coord = this->coord(x, y);

  if (coord < 0) {
    return false;
  }

  std::vector<Tile>* vec = this->tiles[coord];

  if (vec == nullptr) {
    vec = new std::vector<Tile>();
    this->tiles[coord] = vec;
  }

  vec->push_back(tile);
  return true;
}

bool MapLayer::place_tile(int x, int y, Tile tile) {
  std::vector<Tile>* vec = this->tiles_at(x, y);

  if (vec != nullptr && !vec->empty()) {
    Tile top = vec->back();

    if (top.tilesheet == tile.tilesheet && top.x == tile.x && top.y == tile.y) {
      return false;
    }
  }

  return this->add_tile(x, y, tile);
}

bool MapLayer::remove_tile(int x, int y) {
  int coord = this->coord(x, y);

  if (coord < 0) {
    return false;
  }

  std::vector<Tile>* vec = this->tiles[coord];

  if (vec == nullptr || vec->empty()) {
    return false;
  }

  vec->pop_back();

  if (vec->empty()) {
    delete vec;
    this->tiles[coord] = nullptr;
  }

  return true;
}

void MapLayer::clear_tile(int x, int y) {
  int coord = this->coord(x, y);

  if (coord < 0) {
    return;
  }

  std::vector<Tile>* vec = this->tiles[coord];

  if (vec != nullptr) {
    delete vec;
    this->tiles[coord] = nullptr;
  }
}

bool MapLayer::fill_randomly(std::vector<Tile> tiles) {
  if (tiles.empty()) {
    return false;
  }

  for (int coord = 0; coord < this->cols * this->rows; coord++) {
    std::vector<Tile>* vec = this->tiles[coord];

    if (vec == nullptr) {
      vec = new std::vector<Tile>();
      this->tiles[coord] = vec;
    } else {
      vec->clear();
    }

    vec->push_back(tiles[SDL_rand(int(tiles.size()))]);
  }

  return true;
}

void MapLayer::render_tile(int x, int y,
                           const std::vector<Tilesheet*>& tilesheets) const {
  int coord = this->coord(x, y);

  if (coord < 0) {
    return;
  }

  SDL_FRect dst_rect;
  dst_rect.x = float(this->tile_width * x);
  dst_rect.y = float(this->tile_height * y);
  dst_rect.w = float(this->tile_width);
  dst_rect.h = float(this->tile_height);

  Renderer::instance->draw_rect(this->texture, &dst_rect, this->clear_colour,
                                SDL_BLENDMODE_NONE);

  std::vector<Tile>* vec = this->tiles[coord];
  if (vec == nullptr) return;

  SDL_FRect src_rect;
  src_rect.w = float(this->tile_width);
  src_rect.h = float(this->tile_height);

  // render all tiles in order
  for (auto tile : *vec) {
    src_rect.x = float(this->tile_width * tile.x);
    src_rect.y = float(this->tile_height * tile.y);
    Renderer::instance->draw_texture(this->texture,
                                     tilesheets[tile.tilesheet]->texture,
                                     &src_rect, &dst_rect);
  }
}

};  // namespace thoom
