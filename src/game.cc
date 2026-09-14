#include "game.h"

#include <algorithm>
#include <iostream>

#include "SDL3/SDL_blendmode.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_timer.h"
#include "audio_playback.h"
#include "bmp_texture.h"
#include "controller.h"
#include "font.h"
#include "net_agent.h"
#include "renderer.h"
#include "save_data.h"
#include "utils.h"

namespace thoom {

std::unordered_map<uint64_t, std::string> debug_obj_ptrs;

Game::Game() {
  this->ticks = SDL_GetTicks();

  Font::load_fonts(this->fonts);

  load_render_functions();  // bmp_texture.h

  init_playback();  // audio_stream.h
}

Game::~Game() {
  this->unload();

  if (this->first_obj != nullptr) {
    delete this->first_obj;
    this->first_obj = nullptr;
  }

  if (this->last_obj != nullptr) {
    delete this->last_obj;
    this->last_obj = nullptr;
  }

  if (net_agent != nullptr) {
    delete net_agent;
    net_agent = nullptr;
  }

  // free factories
  for (auto pair : this->factories) delete pair.second;
  this->factories.clear();

  // free fonts
  for (auto font : this->fonts) delete font;
  this->fonts.clear();

  free_textures();  // bmp_texture.h

  free_playback();  // audio_stream.h
}

void Game::unload() {
  this->save_objects();

  this->deleting_objects = true;

  if (!this->objects.empty()) {
    for (int i = 0; i < this->objects.size(); i++) {
      delete this->objects[i];
      this->objects[i] = nullptr;
    }

    this->objects.clear();
  }

  this->deleting_objects = false;
  this->current_map = "";

  if (this->bg != nullptr) {
    SDL_DestroyTexture(this->bg);
    this->bg = nullptr;
  }

  if (this->fg != nullptr) {
    SDL_DestroyTexture(this->fg);
    this->fg = nullptr;
  }

  if (this->collision != nullptr) {
    free(this->collision);
    this->collision = nullptr;
  }
}

void Game::load_map(const char* map_path) {
  // if cheddar and connected, notify feta that we are changing maps
  if (this->create_objects &&
      this->net_state == NetworkAgent::State::CONNECTED) {
    net_agent->send_message(MSG_MAP + std::string(map_path) + '\n');
  }

  Map map;
  map.read(map_path);

  this->map_title = map.title;
  this->map_description = map.description;

  this->bg = map.bg;
  this->fg = map.fg;
  this->collision = map.collision;
  this->tile_width = map.tile_width;
  this->tile_height = map.tile_height;
  this->cols = map.cols;
  this->rows = map.rows;
  this->bg_r = map.bg_r;
  this->bg_g = map.bg_g;
  this->bg_b = map.bg_b;

  if (this->ambience != map.ambience) {
    load_audio(map.ambience);
    loop_audio(map.ambience);
  }

  this->ambience = map.ambience;

  // assemble appropriately sized quads for each predefined map collider
  for (int i = 0; i < n_MapColliders; i++) {
    // a quad has four vertices
    for (int j = 0; j < 4; j++) {
      this->colliders[i].vertex[j].x =
          float(map.tile_width) * MapColliders[i][j].x;
      this->colliders[i].vertex[j].y =
          float(map.tile_height) * MapColliders[i][j].y;
    }
  }

  this->current_map = map_path;
  this->creating_objects = true;

  if (this->create_objects || std::string(map_path) == "maps/init") {
    for (auto obj : map.objects) this->create_object(obj.first, obj.second);

    while (!this->new_objects.empty()) {
      auto obj = this->new_objects.front();
      this->new_objects.pop();
      this->create_object(obj.first, obj.second);
    }
  }

  this->creating_objects = false;

  map.clear();

  if (save.geti(LOAD_SAVE) == 1) save.data.erase(LOAD_SAVE);

  save.data[LOAD_MAP] = this->current_map;

  this->corner_x = 0;
  this->corner_y = 0;
}

void Game::create_object(const std::string& id, const std::string& options) {
  if (this->factories.find(id) == this->factories.end() && id != FIRST_OBJ &&
      id != LAST_OBJ) {
    std::cerr << "Game::create_object error: '" << id << "' does not exist"
              << std::endl;
    return;
  }

  this->delete_object = false;

  Object* obj = this->factories[id]->create(options);
  bool obj_cancelled = false;

  // object cancelled being created
  if (this->delete_object == true) {
    delete obj;
    obj = nullptr;
    this->delete_object = false;
    obj_cancelled = true;
  }

  if (obj != nullptr) {
    if (id == FIRST_OBJ) {
      this->first_obj = obj;
    } else if (id == LAST_OBJ) {
      this->last_obj = obj;
    } else {
      this->objects.push_back(obj);
    }

    debug_obj_ptrs[(uint64_t)obj] = id;
  } else if (!obj_cancelled) {
    std::cerr << "Game::create_object error: '" << id << "' resulted in nullptr"
              << std::endl;
  }
}

void Game::push_object(const std::string& id, const std::string& options) {
  this->new_objects.push({id, options});
}

void Game::save_objects() {
  for (auto obj : this->objects) obj->save_data();
}

void Game::step() {
  Renderer* renderer = Renderer::instance;

  // create new map
  if (this->map != "") {
    renderer->clear_screen(kBlack);

    this->unload();
    this->load_map(this->map.c_str());
    this->map = "";
    return;
  }

  this->audio.clear();
  ambience_step();

  // get ticks and calculate delta
  Uint64 new_ticks = SDL_GetTicks();
  this->delta = float(new_ticks - this->ticks) / 1000.0f;
  this->ticks = new_ticks;

  this->net_state = net_agent->get_state();

  if (this->first_obj != nullptr) {
    this->first_obj->step();
  }

  // step all objects
  for (auto it = this->objects.begin(); it != this->objects.end();) {
    this->delete_object = false;
    Object* obj = *it;

    if (obj == nullptr) {
      std::cerr << "Game::step: nullptr found in objects" << std::endl;
      it = this->objects.erase(it);
      continue;
    }

    obj->step();

    if (this->delete_object) {
      it = this->objects.erase(it);
      delete obj;
      this->delete_object = false;
      continue;
    } else
      it++;
  }

  // create new objects
  while (!this->new_objects.empty()) {
    auto args = this->new_objects.front();
    this->new_objects.pop();
    this->create_object(args.first, args.second);
  }

  if (this->last_obj != nullptr) {
    this->last_obj->step();
  }

  Colour bg{this->bg_r, this->bg_g, this->bg_b};
  renderer->clear_screen(bg);

  // render background
  SDL_FRect map_src = SDL_FRect{0, 0, THOOM_SCREEN_WIDTH, THOOM_SCREEN_HEIGHT};
  SDL_FRect map_dst = SDL_FRect{0, 0, THOOM_SCREEN_WIDTH, THOOM_SCREEN_HEIGHT};

  if (this->bg != nullptr) {
    make_map_rect(this->corner_x, this->corner_y, this->bg->w, this->bg->h,
                  &map_src, &map_dst);
    renderer->draw_texture(renderer->screen, this->bg, &map_src, &map_dst);
  }

  renderer->draw_sprites(this->sprites, &map_src, this->corner_x,
                         this->corner_y);

  this->sprites.clear();  // clear sprites; next frame will repopulate

  // render foreground
  if (this->fg != nullptr)
    renderer->draw_texture(renderer->screen, this->fg, &map_src, &map_dst);

  if (local_controller.c == 'p' || captured_controller.c == 'p') {
    local_controller.c = NO_CHAR;
    renderer->save_screenshot("screenshot.bmp");
  }
}

void Game::set_view(int x, int y) {
  this->corner_x = x - THOOM_SCREEN_WIDTH / 2;
  this->corner_y = y - THOOM_SCREEN_HEIGHT / 2;
}

void Game::push_sprite(const std::string& tex_id, SDL_Texture* texture,
                       SDL_FRect* src_rect, SDL_FRect* dst_rect,
                       int depth_offset) {
  int sprite_y = 0;

  if (dst_rect != nullptr) sprite_y = (int)dst_rect->y + depth_offset;

  SpriteRender sprite;
  sprite.tex_id = tex_id;
  sprite.texture = texture;
  sprite.src_rect = src_rect;
  sprite.dst_rect = dst_rect;
  sprite.y = sprite_y;

  // std::lower_bound performs a binary search (log n time complexity)
  auto it =
      std::lower_bound(this->sprites.begin(), this->sprites.end(), sprite);
  this->sprites.insert(it, sprite);
}

// below `_sign` and `_point_in_triangle` functions from:
// https://stackoverflow.com/questions/2049582/how-to-determine-if-a-point-is-in-a-2d-triangle

static float _sign(SDL_FPoint p1, SDL_FPoint p2, SDL_FPoint p3) {
  return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

static bool _point_in_triangle(SDL_FPoint pt, SDL_FPoint v1, SDL_FPoint v2,
                               SDL_FPoint v3) {
  float d1, d2, d3;
  bool has_neg, has_pos;

  d1 = _sign(pt, v1, v2);
  d2 = _sign(pt, v2, v3);
  d3 = _sign(pt, v3, v1);

  has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

  return !(has_neg && has_pos);
}

bool Game::point_in_collider(float x, float y) const {
  // no collision
  if (this->collision == nullptr) return false;

  // flooring is intentional
  int _x = int(x) / this->tile_width;
  int _y = int(y) / this->tile_height;

  // out of map is automatic collision
  if (x < 0.0f || y < 0.0f || _x >= this->cols || _y >= this->rows) return true;

  int coord = (_y * this->cols) + _x;

  // no collider at point or invalid collider
  if (this->collision[coord] < 0 || this->collision[coord] >= n_MapColliders)
    return false;

  Quad quad = this->colliders[this->collision[coord]];
  SDL_FPoint point;
  point.x = x - float(_x * this->tile_width);
  point.y = y - float(_y * this->tile_width);

  // return true if the point lies in either triangles making up the collider's
  // quad
  return _point_in_triangle(point, quad.vertex[0], quad.vertex[1],
                            quad.vertex[2]) ||
         _point_in_triangle(point, quad.vertex[2], quad.vertex[3],
                            quad.vertex[0]);
}

bool Game::in_sight(int x0, int y0, int x1, int y1, int* next_x,
                    int* next_y) const {
  x0 = x0 / this->tile_width;
  y0 = y0 / this->tile_width;
  x1 = x1 / this->tile_width;
  y1 = y1 / this->tile_height;

  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  bool next_set = false;

  while (true) {
    // return false if there is a collider in the way
    if (this->collision[(y0 * this->cols) + x0] >= 0) return false;

    int e2 = 2 * error;
    if (e2 >= dy) {
      if (x0 == x1) break;
      error = error + dy;
      x0 = x0 + sx;
    }
    if (e2 <= dx) {
      if (y0 == y1) break;
      error = error + dx;
      y0 = y0 + sy;
    }

    if (!next_set && next_x != NULL && next_y != NULL) {
      *next_x = x0 * this->tile_width;
      *next_y = y0 * this->tile_height;
      next_set = true;
    }
  }

  if (!next_set && next_x != NULL && next_y != NULL) {
    *next_x = x0 * this->tile_width;
    *next_y = y0 * this->tile_height;
    next_set = true;
  }

  // no colliders in the way; target is in sight
  return true;
}

void Game::random_target(int x, int y, int* next_x, int* next_y) const {
  if (next_x == NULL || next_y == NULL) return;  // legit wtf if this happens

  x /= this->tile_width;
  y /= this->tile_height;

  std::vector<std::pair<int, int>> candidates;

  int start_x = THOOM_CLAMP(x - 1, 0, this->cols - 1);
  int start_y = THOOM_CLAMP(y - 1, 0, this->rows - 1);
  int end_x = THOOM_CLAMP(x + 1, 0, this->cols - 1);
  int end_y = THOOM_CLAMP(y + 1, 0, this->rows - 1);

  for (int _x = start_x; _x <= end_x; _x++) {
    for (int _y = start_y; _y <= end_y; _y++) {
      if (_x == x && _y == y) continue;

      if (this->collision[(_y * this->cols) + _x] < 0) {
        candidates.push_back({_x, _y});
      }
    }
  }

  std::pair<int, int> target = candidates[SDL_rand(candidates.size())];
  *next_x = target.first * this->tile_width;
  *next_y = target.second * this->tile_height;
}

};  // namespace thoom
