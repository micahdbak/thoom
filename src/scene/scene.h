#pragma once

#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "map/map.h"
#include "object.h"

namespace thoom {

class Scene {
 public:
  enum State {
    kIdle,
    kLoading,
    kRunning,
    kUnloading,
  };

  struct ObjectDef {
    const char* id;
    Object* (*create_object)(Scene* scene, const std::string& options);
    ObjectOrder order = kObjectNormal;
  };

  inline static Scene* instance = nullptr;

  static bool init(const std::string& initial_map = "",
                   bool create_objects = true);
  static void quit();

  Scene(const std::string& initial_map = "", bool create_objects = true);
  ~Scene();

  void register_object(const ObjectDef& def);
  void register_objects(const ObjectDef* defs);

  void request_map(const std::string& path, bool create_objects = true);

  void create_object(const std::string& id, const std::string& options);
  Object* find_object(int id);
  void save_objects();

  void step();

  State state = kIdle;
  uint64_t ticks = 0;
  float delta = 1.0f / 60.0f;  // simulation time
  Map* map = nullptr;

 private:
  struct MapRequest {
    std::string path;
    bool create_objects = true;
  };

  void load_map(const std::string& path, bool create_objects);
  void unload();
  void clean_up_objects(std::vector<Object*>& objects);
  void create_new_objects();
  void tick();
  void step_objects();

  MapRequest next_map;

  uint64_t last_frame_ns = 0;
  uint64_t accumulator = 0;
  uint64_t tick_fraction = 0;

  std::vector<Object*> first_objects;
  std::vector<Object*> objects;
  std::vector<Object*> last_objects;
  std::unordered_map<int, Object*> objects_by_id;
  std::unordered_map<std::string, ObjectDef> factories;
  std::queue<ObjectRequest> new_objects;
};

};  // namespace thoom
