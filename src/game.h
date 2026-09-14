#pragma once

#include <SDL3/SDL.h>

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "font.h"
#include "map.h"
#include "net_agent.h"
#include "object.h"
#include "sprite.h"

namespace thoom {

// screen related constants
#define THOOM_SCREEN_WIDTH 320
#define THOOM_SCREEN_HEIGHT 240

// forces one object to the be the first/last object run per frame
#define FIRST_OBJ "_first"
#define LAST_OBJ "_last"

class Game {
 public:
  Game();
  ~Game();

  void unload();
  void create_object(const std::string& id, const std::string& options);
  void push_object(const std::string& id, const std::string& options);
  void save_objects();
  void step();

  void set_view(int x, int y);
  void push_sprite(const std::string& tex_id, SDL_Texture* texture,
                   SDL_FRect* src_rect, SDL_FRect* dst_rect, int depth_offset);

  bool point_in_collider(float x, float y) const;
  bool in_sight(int x0, int y0, int x1, int y1, int* next_x,
                int* next_y) const;  // true if no colliders in way
  void random_target(int x, int y, int* next_x, int* next_y)
      const;  // chooses a random tile to move to, with no collider

  // defined per game (e.g., map_editor.cc, init.cc)

  void init();

  // quick funcs

  Object* get_object(int id) {
    for (auto obj : this->objects) {
      if (obj != nullptr && obj->id == id) {
        return obj;
      }
    }

    return nullptr;
  }

  std::string title = "SDL3 Game";
  int argc;
  char** argv;

  Uint64 ticks = 0;
  float delta = 0;

  std::vector<Font*> fonts;

  int corner_x = 0, corner_y = 0;  // set in Game::step
  int tile_width = 32, tile_height = 32, cols = 1, rows = 1;
  int* collision = nullptr;

  std::string map = "";                              // set this to load a map
  std::string current_map = "";                      // readonly
  std::string map_title = "", map_description = "";  // readonly

  bool create_objects = true;  // disable for feta launcher
  bool delete_object = false;  // set to true from an object's step to delete it

  // is set to true when creating objects while loading a map (for use in obj
  // constructors)
  bool creating_objects = false;
  // is set to true when deleting objects before loading a map (for use in obj
  // destructors)
  bool deleting_objects = false;

  // sprite rendering things
  std::vector<SpriteRender> sprites;
  std::vector<SpriteRender> push_sprites;

  NetworkAgent::State net_state = NetworkAgent::State::NO_CONNECTION;

  Uint8 bg_r = 0, bg_g = 0, bg_b = 0;

  struct AudioMsg {
    std::string wav_path;
    float gain, x, y;
  };

  std::vector<AudioMsg> audio;

  // 0..200
  int volume = 100, music_volume = 100;

 private:
  // for Game::texts
  struct Text {
    std::string str;
    int x, y;
  };

  void load_map(const char* map_path);

  // map things
  SDL_Texture *bg = nullptr, *fg = nullptr;
  Quad colliders[n_MapColliders];

  // object things
  std::vector<Object*> objects;
  std::queue<std::pair<std::string, std::string> > new_objects;
  std::unordered_map<std::string, ObjectFactory*> factories;
  Object *first_obj = nullptr, *last_obj = nullptr;

  // ui things
  std::queue<Text> texts;

  std::string ambience;
};

extern Game* game;
extern bool _running;

};  // namespace thoom
