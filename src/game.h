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

#include "map.h"
#include "net_agent.h"
#include "object.h"

namespace thoom {

#define THOOM_SCREEN_WIDTH 320
#define THOOM_SCREEN_HEIGHT 240

#define FIRST_OBJ "_first"
#define LAST_OBJ "_last"

class Game {
 public:
  Game();
  ~Game();

  void init();

  void unload();
  void step();

  void create_object(const std::string& id, const std::string& options);
  void push_object(const std::string& id, const std::string& options);
  void save_objects();
  Object* get_object(int id);

  int argc;
  char** argv;

  Uint64 ticks = 0;
  float delta = 0;

  std::string next_map = "";
  Map* map = nullptr;

  bool create_objects = true;
  bool delete_object = false;
  bool creating_objects = false;
  bool deleting_objects = false;

  NetworkAgent::State net_state = NetworkAgent::State::NO_CONNECTION;

  struct AudioMsg {
    std::string wav_path;
    float gain, x, y;
  };

  std::vector<AudioMsg> audio;
  int volume = 100, music_volume = 100;

 private:
  void load_map(const char* map_path);

  std::vector<Object*> objects;
  std::queue<std::pair<std::string, std::string>> new_objects;
  std::unordered_map<std::string, ObjectFactory*> factories;
  Object *first_obj = nullptr, *last_obj = nullptr;

  std::string ambience;
};

extern Game* game;

};  // namespace thoom
