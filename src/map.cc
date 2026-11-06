#include "map.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "collider.h"
#include "colour.h"
#include "map_layer.h"
#include "object.h"
#include "thoom/macros.h"
#include "tilesheet.h"
#include "window.h"

namespace thoom {

Map::Map(const char* map_path) {
  if (map_path == nullptr) {
    fprintf(stderr, THOOM_LOG "null map path.\n");
    exit(1);
  }

  std::string map_txt_path = std::string(map_path) + ".txt";
  std::string map_bin_path = std::string(map_path) + ".bin";

  FILE* txt_file = fopen(map_txt_path.c_str(), "r");
  if (txt_file == NULL) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  char buff[1024], buff2[1024];

  if (feof(txt_file)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  fgets(buff, sizeof(buff), txt_file);
  sscanf(buff, "%1023[^\n]", buff2);
  buff2[1023] = '\0';
  this->title = buff2;

  if (feof(txt_file)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  fgets(buff, sizeof(buff), txt_file);
  sscanf(buff, "%1023[^\n]", buff2);
  buff2[1023] = '\0';
  this->description = buff2;

  if (feof(txt_file)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  fgets(buff, sizeof(buff), txt_file);
  if (4 != sscanf(buff, "%d,%d,%d,%d", &this->tile_width, &this->tile_height,
                  &this->cols, &this->rows)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  if (tile_width <= 0 || tile_height <= 0) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }

  if (feof(txt_file)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  fgets(buff, sizeof(buff), txt_file);
  int bg_r, bg_g, bg_b;
  if (3 != sscanf(buff, "%d,%d,%d", &bg_r, &bg_g, &bg_b)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  this->background = Colour(uint8_t(bg_r), uint8_t(bg_g), uint8_t(bg_b));

  if (feof(txt_file)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  fgets(buff, sizeof(buff), txt_file);
  buff[strlen(buff) - 1] = '\0';  // remove newline
  this->ambience = buff;

  // allocate the necessary memory
  this->layers[kBackgroundLayer] =
      new MapLayer(this->cols, this->rows, this->tile_width, this->tile_height,
                   this->background);
  this->layers[kForegroundLayer] = new MapLayer(
      this->cols, this->rows, this->tile_width, this->tile_height, kMask);

  this->collider =
      new Collider(this->tile_width, this->tile_height, this->cols, this->rows);

  if (feof(txt_file)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  fgets(buff, sizeof(buff), txt_file);
  int ntilesheets = 0;
  if (1 != sscanf(buff, "%d", &ntilesheets)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }

  for (int i = 0; i < ntilesheets; i++) {
    if (feof(txt_file)) {
      fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
      exit(1);
    }
    fgets(buff, sizeof(buff), txt_file);
    sscanf(buff, "%1023[^\n]", buff2);
    buff2[1023] = '\0';
    Tilesheet* tilesheet =
        new Tilesheet(buff2, this->tile_width, this->tile_height);
    if (tilesheet->texture == nullptr) {
      fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
      exit(1);
    }
    this->tilesheets.push_back(tilesheet);
  }

  if (feof(txt_file)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  fgets(buff, sizeof(buff), txt_file);
  int nobjects = 0;
  if (1 != sscanf(buff, "%d", &nobjects)) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }

  for (int i = 0; i < nobjects; i++) {
    if (feof(txt_file)) {
      fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
      exit(1);
    }
    fgets(buff, sizeof(buff), txt_file);
    char buff3[1024];
    if (2 != sscanf(buff, "%1023s %1023[^\n]", buff2, buff3)) {
      fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
      exit(1);
    }
    buff2[1023] = buff3[1023] = '\0';
    ObjectRequest object;
    object.id = buff2;
    object.options = buff3;
    this->objects.push_back(object);
  }

  fclose(txt_file);
  FILE* bin_file = fopen(map_bin_path.c_str(), "rb");
  if (bin_file == NULL) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  uint8_t buffer[6];
  size_t nbytes;

  // read tiles
  while (fread(buffer, 1, 6, bin_file) == 6) {
    int x = int(buffer[3]);
    int y = int(buffer[4]);
    int coord = (y * this->cols) + x;
    if (coord >= this->cols * this->rows) {
      fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
      exit(1);
    }

    // collider
    if (buffer[5] == 'c') {
      // make sure the collider actually exists
      if (int(buffer[0]) >= n_MapColliders) {
        fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
        exit(1);
      }

      this->collider->collision[coord] = int(buffer[0]);
      // buffer[1] and buffer[2] are redundant
      continue;
    }

    Tile tile;
    tile.tilesheet = int(buffer[0]);
    tile.x = int(buffer[1]);
    tile.y = int(buffer[2]);

    // make sure nothing is corrupted
    if (tile.tilesheet >= this->tilesheets.size() ||
        tile.x >= this->tilesheets[tile.tilesheet]->cols ||
        tile.y >= this->tilesheets[tile.tilesheet]->rows) {
      fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
      exit(1);
    }

    // background or foreground tile
    int layer_index;
    if (buffer[5] == 'b') {
      layer_index = kBackgroundLayer;
    } else if (buffer[5] == 'f') {
      layer_index = kForegroundLayer;
    } else {
      fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
      exit(1);
    }

    this->layers[layer_index]->add_tile(coord, tile);
  }

  // done reading file
  fclose(bin_file);
  this->path = map_path;

  // render all tiles read from the file
  for (int i = 0; i < this->cols * this->rows; i++) {
    render_tile(i % this->cols, i / this->cols);
  }
}

Map::Map(int tile_width, int tile_height, int cols, int rows) {
  this->tile_width = tile_width;
  this->tile_height = tile_height;
  this->cols = cols;
  this->rows = rows;

  this->layers[kBackgroundLayer] =
      new MapLayer(this->cols, this->rows, this->tile_width, this->tile_height,
                   this->background);
  this->layers[kForegroundLayer] = new MapLayer(
      this->cols, this->rows, this->tile_width, this->tile_height, kMask);

  this->collider =
      new Collider(this->tile_width, this->tile_height, this->cols, this->rows);
}

Map::~Map() {
  for (Tilesheet* tilesheet : this->tilesheets) delete tilesheet;
  this->tilesheets.clear();

  delete this->layers[kBackgroundLayer];
  this->layers[kBackgroundLayer] = nullptr;
  delete this->layers[kForegroundLayer];
  this->layers[kForegroundLayer] = nullptr;

  delete this->collider;
  this->collider = nullptr;
}

static inline void write_tile(uint8_t* buffer, Tile tile, int x, int y,
                              char ground, FILE* file) {
  buffer[0] = uint8_t(tile.tilesheet);
  buffer[1] = uint8_t(tile.x);
  buffer[2] = uint8_t(tile.y);
  buffer[3] = uint8_t(x);
  buffer[4] = uint8_t(y);
  buffer[5] = uint8_t(ground);
  fwrite(buffer, 1, 6, file);
}

void Map::write(const char* map_path) const {
  std::string map_txt_path = std::string(map_path) + ".txt";
  std::string map_bin_path = std::string(map_path) + ".bin";

  FILE* txt_file = fopen(map_txt_path.c_str(), "w");
  if (txt_file == nullptr) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }

  fprintf(txt_file, "%s\n%s\n", this->title.c_str(), this->description.c_str());
  fprintf(txt_file, "%d,%d,%d,%d\n", this->tile_width, this->tile_height,
          this->cols, this->rows);
  fprintf(txt_file, "%d,%d,%d\n", int(this->background.r),
          int(this->background.g), int(this->background.b));
  fprintf(txt_file, "%s\n", this->ambience.c_str());
  fprintf(txt_file, "%d\n", int(this->tilesheets.size()));

  // write tilesheet info
  for (Tilesheet* tilesheet : this->tilesheets) {
    fprintf(txt_file, "%s\n", tilesheet->path.c_str());
  }

  fprintf(txt_file, "%d\n", int(this->objects.size()));

  // write objects
  for (const ObjectRequest& object : this->objects) {
    fprintf(txt_file, "%s %s\n", object.id.c_str(), object.options.c_str());
  }

  fclose(txt_file);
  FILE* bin_file = fopen(map_bin_path.c_str(), "wb");
  if (bin_file == nullptr) {
    fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
    exit(1);
  }
  uint8_t buffer[1024];

  // write tiles
  for (int i = 0; i < this->cols * this->rows; i++) {
    int x = i % this->cols, y = i / this->cols;
    // colliders
    int c = this->collider->collision[i];
    if (c >= 0) {
      buffer[0] = uint8_t(c);
      buffer[1] = 'c';
      buffer[2] = 'c';
      buffer[3] = uint8_t(x);
      buffer[4] = uint8_t(y);
      buffer[5] = uint8_t('c');
      fwrite(buffer, 1, 6, bin_file);
    }

    // background tiles
    std::vector<Tile>* bg_vec = this->layers[kBackgroundLayer]->tiles[i];
    if (bg_vec != nullptr) {
      for (auto tile : *bg_vec) write_tile(buffer, tile, x, y, 'b', bin_file);
    }

    // foreground tiles
    std::vector<Tile>* fg_vec = this->layers[kForegroundLayer]->tiles[i];
    if (fg_vec != nullptr) {
      for (auto tile : *fg_vec) write_tile(buffer, tile, x, y, 'f', bin_file);
    }
  }

  fclose(bin_file);
}

void Map::render_tile(int x, int y) const {
  this->layers[kBackgroundLayer]->render_tile(
      x, y, this->cols, this->tile_width, this->tile_height, this->tilesheets);
  this->layers[kForegroundLayer]->render_tile(
      x, y, this->cols, this->tile_width, this->tile_height, this->tilesheets);
}

void Map::make_map_rect(int x, int y, int w, int h, SDL_FRect* src_rect,
                        SDL_FRect* dst_rect) {
  if (x < 0) {
    dst_rect->x = float(-1 * x);
    dst_rect->w = float(THOOM_MIN(THOOM_SCREEN_WIDTH, w));
    src_rect->x = 0.0f;
  } else {
    dst_rect->x = 0.0f;
    dst_rect->w = float(THOOM_MIN(THOOM_SCREEN_WIDTH, w - x));
    src_rect->x = float(x);
  }

  if (y < 0) {
    dst_rect->y = float(-1 * y);
    dst_rect->h = float(THOOM_MIN(THOOM_SCREEN_HEIGHT, h));
    src_rect->y = 0.0f;
  } else {
    dst_rect->y = 0.0f;
    dst_rect->h = float(THOOM_MIN(THOOM_SCREEN_HEIGHT, h - y));
    src_rect->y = float(y);
  }

  src_rect->w = dst_rect->w;
  src_rect->h = dst_rect->h;
}

};  // namespace thoom
