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

namespace thoom {

static int read_line(FILE* file, char* line, size_t size) {
  if (fgets(line, (int)size, file) == NULL) {
    return 0;
  }

  line[strcspn(line, "\r\n")] = '\0';
  return 1;
}

Map::Map(const char* map_path) {
  std::string map_txt_path;
  std::string map_bin_path;
  FILE* txt_file = NULL;
  FILE* bin_file = NULL;
  char buff[1024];
  char buff2[1024];
  char buff3[1024];
  uint8_t buffer[6];
  int bg_r, bg_g, bg_b;
  int ntilesheets = 0;
  int nobjects = 0;
  int i;

  if (map_path == NULL) {
    fprintf(stderr, THOOM_LOG "null map path.\n");
    exit(1);
  }

  map_txt_path = std::string(map_path) + ".txt";
  map_bin_path = std::string(map_path) + ".bin";

  txt_file = fopen(map_txt_path.c_str(), "r");
  if (txt_file == NULL) goto corrupted;

  /* map title */
  if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;
  this->title = buff;

  /* map description */
  if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;
  this->description = buff;

  /* tile width, tile height, map columns, map rows */

  if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;
  if (sscanf(buff, "%d,%d,%d,%d", &this->tile_width, &this->tile_height,
             &this->cols, &this->rows) != 4) {
    goto corrupted;
  }

  if (this->tile_width <= 0 || this->tile_height <= 0 || this->cols <= 0 ||
      this->rows <= 0)
    goto corrupted;

  /* map background colour */

  if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;
  if (sscanf(buff, "%d,%d,%d", &bg_r, &bg_g, &bg_b) != 3) {
    goto corrupted;
  }

  this->background = Colour(uint8_t(bg_r), uint8_t(bg_g), uint8_t(bg_b));

  /* map ambience track */
  if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;
  this->ambience = buff;

  /* build map layers and colliders */
  this->layers[kBackgroundLayer] =
      new MapLayer(this->cols, this->rows, this->tile_width, this->tile_height,
                   this->background);
  this->layers[kForegroundLayer] = new MapLayer(
      this->cols, this->rows, this->tile_width, this->tile_height, kMask);
  this->collider =
      new Collider(this->tile_width, this->tile_height, this->cols, this->rows);

  /* map tilesheets */

  if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;
  if (sscanf(buff, "%d", &ntilesheets) != 1 || ntilesheets < 0) {
    goto corrupted;
  }

  for (i = 0; i < ntilesheets; i++) {
    if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;

    Tilesheet* tilesheet =
        new Tilesheet(buff, this->tile_width, this->tile_height);

    if (tilesheet->texture == NULL) goto corrupted;

    this->tilesheets.push_back(tilesheet);
  }

  /* map objects */

  if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;
  if (sscanf(buff, "%d", &nobjects) != 1 || nobjects < 0) {
    goto corrupted;
  }

  for (i = 0; i < nobjects; i++) {
    if (!read_line(txt_file, buff, sizeof(buff))) goto corrupted;

    if (sscanf(buff, "%1023s %1023[^\n]", buff2, buff3) != 2) goto corrupted;

    ObjectRequest object;
    object.id = buff2;
    object.options = buff3;
    this->objects.push_back(object);
  }

  fclose(txt_file);
  txt_file = NULL;

  /* map binary data */

  bin_file = fopen(map_bin_path.c_str(), "rb");

  if (bin_file == NULL) goto corrupted;

  while (fread(buffer, 1, sizeof(buffer), bin_file) == sizeof(buffer)) {
    int x = int(buffer[3]);
    int y = int(buffer[4]);
    int coord = (y * this->cols) + x;

    if (coord < 0 || coord >= this->cols * this->rows) goto corrupted;

    switch (buffer[5]) {
      case 'c':
        if (int(buffer[0]) >= n_MapColliders) goto corrupted;

        this->collider->collision[coord] = int(buffer[0]);
        break;

      case 'b':
      case 'f': {
        Tile tile;
        int layer_index;

        tile.tilesheet = int(buffer[0]);
        tile.x = int(buffer[1]);
        tile.y = int(buffer[2]);

        if (tile.tilesheet >= int(this->tilesheets.size()) ||
            tile.x >= this->tilesheets[tile.tilesheet]->cols ||
            tile.y >= this->tilesheets[tile.tilesheet]->rows) {
          goto corrupted;
        }

        layer_index = buffer[5] == 'b' ? kBackgroundLayer : kForegroundLayer;
        this->layers[layer_index]->add_tile(x, y, tile);
      } break;

      default:
        goto corrupted;
    }
  }

  fclose(bin_file);
  bin_file = NULL;
  this->path = map_path;

  for (i = 0; i < this->cols * this->rows; i++) {
    render_tile(i % this->cols, i / this->cols);
  }

  return;

corrupted:
  if (txt_file != NULL) {
    fclose(txt_file);
  }

  if (bin_file != NULL) {
    fclose(bin_file);
  }

  fprintf(stderr, THOOM_LOG "'%s' corrupted.\n", map_path);
  exit(1);
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

bool Map::has_tiles(int x, int y) const {
  if (x < 0 || y < 0 || x >= this->cols || y >= this->rows) {
    return false;
  }

  for (int layer = 0; layer < kLayerCount; layer++) {
    if (this->layers[layer] != nullptr &&
        this->layers[layer]->has_tiles(x, y)) {
      return true;
    }
  }

  return false;
}

bool Map::add_region(int layer, int x, int y, int tilesheet, int src_x,
                     int src_y, int src_w, int src_h) {
  if (layer < 0 || layer >= kLayerCount || this->layers[layer] == nullptr) {
    return false;
  }

  if (tilesheet < 0 || tilesheet >= int(this->tilesheets.size())) {
    return false;
  }

  if (src_w <= 0 || src_h <= 0) {
    return false;
  }

  Tilesheet* sheet = this->tilesheets[tilesheet];

  if (src_x < 0) {
    x -= src_x;
    src_w += src_x;
    src_x = 0;
  }

  if (src_y < 0) {
    y -= src_y;
    src_h += src_y;
    src_y = 0;
  }

  if (src_x >= sheet->cols || src_y >= sheet->rows) {
    return false;
  }

  int width = THOOM_MIN(src_w, sheet->cols - src_x);
  int height = THOOM_MIN(src_h, sheet->rows - src_y);

  if (x < 0) {
    int skip = -x;
    x = 0;
    src_x += skip;
    width -= skip;
  }

  if (y < 0) {
    int skip = -y;
    y = 0;
    src_y += skip;
    height -= skip;
  }

  width = THOOM_MIN(width, this->cols - x);
  height = THOOM_MIN(height, this->rows - y);

  if (width <= 0 || height <= 0) {
    return false;
  }

  bool placed = false;

  for (int dy = 0; dy < height; dy++) {
    for (int dx = 0; dx < width; dx++) {
      Tile tile;
      tile.tilesheet = static_cast<unsigned int>(tilesheet);
      tile.x = static_cast<unsigned int>(src_x + dx);
      tile.y = static_cast<unsigned int>(src_y + dy);

      if (this->layers[layer]->place_tile(x + dx, y + dy, tile)) {
        placed = true;
        this->render_tile(x + dx, y + dy);
      }
    }
  }

  return placed;
}

bool Map::add_tilesheet(const char* path) {
  Tilesheet* tilesheet =
      new Tilesheet(path, this->tile_width, this->tile_height);

  if (tilesheet->texture == nullptr) {
    delete tilesheet;
    return false;
  }

  this->tilesheets.push_back(tilesheet);
  return true;
}

bool Map::remove_tilesheet(int index) {
  if (index < 0 || index >= int(this->tilesheets.size())) {
    return false;
  }

  unsigned int removed = static_cast<unsigned int>(index);

  for (int layer_index = 0; layer_index < kLayerCount; layer_index++) {
    MapLayer* layer = this->layers[layer_index];

    if (layer == nullptr) {
      continue;
    }

    for (int coord = 0; coord < this->cols * this->rows; coord++) {
      std::vector<Tile>* vec = layer->tiles[coord];

      if (vec == nullptr) {
        continue;
      }

      std::vector<Tile>::iterator it = vec->begin();

      while (it != vec->end()) {
        if (it->tilesheet == removed) {
          it = vec->erase(it);
        } else {
          if (it->tilesheet > removed) {
            it->tilesheet--;
          }
          it++;
        }
      }

      if (vec->empty()) {
        delete vec;
        layer->tiles[coord] = nullptr;
      }
    }
  }

  delete this->tilesheets[index];
  this->tilesheets.erase(this->tilesheets.begin() + index);

  for (int i = 0; i < this->cols * this->rows; i++) {
    this->render_tile(i % this->cols, i / this->cols);
  }

  return true;
}

bool Map::add_object(const ObjectRequest& object) {
  if (object.id.empty()) {
    return false;
  }

  this->objects.push_back(object);
  return true;
}

bool Map::remove_object(int index) {
  if (index < 0 || index >= int(this->objects.size())) {
    return false;
  }

  this->objects.erase(this->objects.begin() + index);
  return true;
}

void Map::render_tile(int x, int y) const {
  this->layers[kBackgroundLayer]->render_tile(x, y, this->tilesheets);
  this->layers[kForegroundLayer]->render_tile(x, y, this->tilesheets);
}

};  // namespace thoom
