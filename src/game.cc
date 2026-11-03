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
  delete this->map;
  this->map = nullptr;
}

void Game::load_map(const char* map_path) {
  // if cheddar and connected, notify feta that we are changing maps
  if (this->create_objects &&
      this->net_state == NetworkAgent::State::CONNECTED) {
    net_agent->send_message(MSG_MAP + std::string(map_path) + '\n');
  }

  delete this->map;
  this->map = new Map(map_path);

  if (this->ambience != this->map->ambience) {
    load_audio(this->map->ambience);
    loop_audio(this->map->ambience);
  }

  this->ambience = this->map->ambience;

  this->creating_objects = true;

  if (this->create_objects || std::string(map_path) == "maps/init") {
    for (auto obj : this->map->objects)
      this->create_object(obj.first, obj.second);

    while (!this->new_objects.empty()) {
      auto obj = this->new_objects.front();
      this->new_objects.pop();
      this->create_object(obj.first, obj.second);
    }
  }

  this->creating_objects = false;

  if (save.geti(LOAD_SAVE) == 1) save.data.erase(LOAD_SAVE);

  save.data[LOAD_MAP] = this->map->path;

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
  if (this->next_map != "") {
    renderer->clear_screen(kBlack);

    this->unload();
    this->load_map(this->next_map.c_str());
    this->next_map = "";
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

  Colour bg = kBlack;

  if (this->map != nullptr) {
    bg = Colour{uint8_t(this->map->bg_r), uint8_t(this->map->bg_g),
                uint8_t(this->map->bg_b)};
  }

  renderer->clear_screen(bg);

  // render background
  SDL_FRect map_src = SDL_FRect{0, 0, THOOM_SCREEN_WIDTH, THOOM_SCREEN_HEIGHT};
  SDL_FRect map_dst = SDL_FRect{0, 0, THOOM_SCREEN_WIDTH, THOOM_SCREEN_HEIGHT};

  if (this->map != nullptr &&
      this->map->layers[Map::kBackgroundLayer]->texture != nullptr) {
    SDL_Texture* bg_texture = this->map->layers[Map::kBackgroundLayer]->texture;
    Map::make_map_rect(this->corner_x, this->corner_y, bg_texture->w,
                       bg_texture->h, &map_src, &map_dst);
    renderer->draw_texture(renderer->screen, bg_texture, &map_src, &map_dst);
  }

  renderer->draw_sprites(this->sprites, &map_src, this->corner_x,
                         this->corner_y);

  this->sprites.clear();  // clear sprites; next frame will repopulate

  // render foreground
  if (this->map != nullptr &&
      this->map->layers[Map::kForegroundLayer]->texture != nullptr) {
    renderer->draw_texture(renderer->screen,
                           this->map->layers[Map::kForegroundLayer]->texture,
                           &map_src, &map_dst);
  }

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

};  // namespace thoom
