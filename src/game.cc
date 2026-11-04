#include "game.h"

#include <iostream>

#include "SDL3/SDL_blendmode.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_timer.h"
#include "audio.h"
#include "bmp_texture.h"
#include "controller.h"
#include "net_agent.h"
#include "renderer.h"
#include "save_data.h"
#include "window.h"

namespace thoom {

std::unordered_map<uint64_t, std::string> debug_obj_ptrs;

Game::Game() {
  this->ticks = SDL_GetTicks();

  load_render_functions();  // bmp_texture.h
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

  free_textures();  // bmp_texture.h
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

void Game::step() {
  // create new map
  if (this->next_map != "") {
    thoom::Window::instance->clear_screen(kBlack);

    this->unload();
    this->load_map(this->next_map.c_str());
    this->next_map = "";
    return;
  }

  Audio::instance->step();

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

  thoom::Window::instance->clear_screen(bg);
  Renderer::instance->draw_map_background(this->map);
  Renderer::instance->draw_sprites();
  Renderer::instance->draw_map_foreground(this->map);

  if (local_controller.c == 'p' || captured_controller.c == 'p') {
    local_controller.c = NO_CHAR;
    Window::instance->save_screenshot("screenshot.bmp");
  }
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

Object* Game::get_object(int id) {
  for (auto obj : this->objects) {
    if (obj != nullptr && obj->id == id) {
      return obj;
    }
  }

  return nullptr;
}

void Game::load_map(const char* map_path) {
  // if cheddar and connected, notify feta that we are changing maps
  if (this->create_objects &&
      this->net_state == NetworkAgent::State::CONNECTED) {
    net_agent->send_message(MSG_MAP + std::string(map_path) + '\n');
  }

  delete this->map;
  this->map = new Map(map_path);

  Audio::instance->loop(this->map->ambience);

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

  Renderer::instance->reset_view();
}

};  // namespace thoom
