#pragma once

#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "map/map.h"
#include "object/entity.h"
#include "object/object.h"
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

  inline static Scene* instance = nullptr;

  static bool init(const std::string& initial_map, bool create_objects);
  static void quit();

  Scene(const std::string& initial_map, bool create_objects);
  ~Scene();

  void register_object(const ObjectDef& def);
  void register_objects(const ObjectDef* defs);
  void register_entity(const EntityDef& def);
  void register_entities(const EntityDef* defs);

  void request_map(const std::string& path, bool create_objects);

  void create_object(const std::string& id, const std::string& options);
  void create_entity(const std::string& id, const std::string& options,
                     const EntityOptions& entity_options);
  void replicate_entity(const EntityRecord& record);

  Object* find_object(thoom_lid lid);
  Entity* find_entity(thoom_gid gid);

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

  typedef std::variant<ObjectRequest, EntityRequest, EntityRecord>
      CreationRequest;

  void tick();
  void step_local_trees(thoom_pid current_pid);
  void render_trees();
  void destroy_deleted_trees();
  void destroy_object(Object* object);

  void load_map(const std::string& path, bool create_objects);
  void unload();

  const ObjectDef* find_object_definition(const std::string& id) const;
  const EntityDef* find_entity_definition(const std::string& id) const;

  void create_map_object(const ObjectRequest& request);
  void request_creation(CreationRequest request);
  void process_object_request(const ObjectRequest& request);
  void process_entity_request(const EntityRequest& request);
  void process_entity_record(const EntityRecord& record);
  void create_requested_objects();
  void instantiate_object(const ObjectDef& def, const std::string& options);
  void instantiate_entity(const EntityDef& def, const Entity::Context& context,
                          const std::string& options);
  bool admit_root(Object* object, ObjectOrder order);

  static void step_tree(Object& object);
  static void render_tree(Object& object);
  static void save_tree(Object& object);
  static void destroy_deleted_children(Object& object);

  MapRequest next_map;

  uint64_t last_frame_ns = 0;
  uint64_t accumulator = 0;
  uint64_t tick_fraction = 0;
  bool draining_creations = false;

  std::vector<Object*> object_lists[kObjectLast + 1];
  std::unordered_map<thoom_lid, Object*> objects_by_lid;
  std::unordered_map<thoom_gid, Entity*> entities_by_gid;
  std::unordered_map<std::string, ObjectDef> object_factories;
  std::unordered_map<std::string, EntityDef> entity_factories;
  std::queue<CreationRequest> creation_requests;
};

};  // namespace thoom
