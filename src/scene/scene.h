#pragma once

#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "map/map.h"
#include "object.h"
#include "thoom/thoom.h"

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
    bool replicated;
    ObjectOrder order;
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

  void register_gid_object(Object* object);
  void unregister_gid_object(Object* object);

  Object* find_object(thoom_lid lid);
  Object* find_gid_object(thoom_gid gid);

  void save_objects();

  void step();

  State state = kIdle;
  uint64_t ticks = 0;
  float delta = 1.0f / 60.0f;  // simulation time
  Map* map = nullptr;
  thoom_pid local_pid = THOOM_PID_HOST;

 private:
  struct MapRequest {
    std::string path;
    bool create_objects = true;
  };

  void tick();
  void step_objects();
  void destroy_object(Object* object);

  void load_map(const std::string& path, bool create_objects);
  void unload();

  void create_new_objects();
  Object* spawn_object(const ObjectDef& def, const std::string& options);

  MapRequest next_map;

  uint64_t last_frame_ns = 0;
  uint64_t accumulator = 0;
  uint64_t tick_fraction = 0;

  std::vector<Object*> object_lists[kObjectLast + 1];
  std::unordered_map<thoom_lid, Object*> objects_by_lid;
  std::unordered_map<thoom_gid, Object*> objects_by_gid;
  std::unordered_map<std::string, ObjectDef> factories;
  std::queue<ObjectRequest> new_objects;
};

};  // namespace thoom
