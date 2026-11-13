#include "scene/scene.h"

#include <SDL3/SDL.h>

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "audio/audio.h"
#include "colour.h"
#include "hud/hud.h"
#include "map/map.h"
#include "object/entity.h"
#include "object/object.h"
#include "renderer/renderer.h"
#include "save/save_data.h"
#include "switch/peer_identity.h"
#include "thoom/log.h"
#include "thoom/thoom.h"

#define THOOM_TICK_NS 16666667
#define THOOM_MAX_FRAME_NS 250000000

namespace thoom {

bool Scene::init(const std::string& initial_map, bool create_objects) {
  if (Scene::instance == nullptr) {
    new Scene(initial_map, create_objects);
    return true;
  }

  thoom_log_error("Scene::init error: scene already initialized");
  return false;
}

void Scene::quit() {
  if (Scene::instance == nullptr) {
    return;
  }

  delete Scene::instance;
  Scene::instance = nullptr;
}

Scene::Scene(const std::string& initial_map, bool create_objects) {
  Scene::instance = this;

  this->ticks = SDL_GetTicks();
  this->last_frame_ns = SDL_GetTicksNS();

  if (!initial_map.empty()) {
    this->load_map(initial_map, create_objects);
  }
}

Scene::~Scene() {
  this->unload();

  this->state = Scene::kUnloading;

  for (std::vector<Object*>& objects : this->object_lists) {
    for (Object* object : objects) {
      this->destroy_object(object);
    }

    objects.clear();
  }

  this->state = Scene::kIdle;

  if (Scene::instance == this) {
    Scene::instance = nullptr;
  }
}

void Scene::register_object(const ObjectDef& def) {
  if (def.id == nullptr || def.create_object == nullptr) {
    thoom_log_error("Scene::register_object error: incomplete definition");
    return;
  }

  if (this->find_object_definition(def.id) != nullptr ||
      this->find_entity_definition(def.id) != nullptr) {
    thoom_log_fatal("Scene::register_object error: '%s' already exists",
                    def.id);
  }

  this->object_factories.emplace(def.id, def);
}

void Scene::register_objects(const ObjectDef* defs) {
  if (defs == nullptr) {
    thoom_log_error("Scene::register_objects error: no definition table");
    return;
  }

  for (const ObjectDef* def = defs; def->id != nullptr; def++) {
    this->register_object(*def);
  }
}

void Scene::register_entity(const EntityDef& def) {
  if (def.id == nullptr || def.create_entity == nullptr) {
    thoom_log_error("Scene::register_entity error: incomplete definition");
    return;
  }

  if (this->find_object_definition(def.id) != nullptr ||
      this->find_entity_definition(def.id) != nullptr) {
    thoom_log_fatal("Scene::register_entity error: '%s' already exists",
                    def.id);
  }

  this->entity_factories.emplace(def.id, def);
}

void Scene::register_entities(const EntityDef* defs) {
  if (defs == nullptr) {
    thoom_log_error("Scene::register_entities error: no definition table");
    return;
  }

  for (const EntityDef* def = defs; def->id != nullptr; def++) {
    this->register_entity(*def);
  }
}

void Scene::request_map(const std::string& path, bool create_objects) {
  this->next_map.path = path;
  this->next_map.create_objects = create_objects;
}

void Scene::create_object(const std::string& id, const std::string& options) {
  if (this->find_entity_definition(id) != nullptr) {
    thoom_log_error(
        "Scene::create_object error: '%s' is an entity; use "
        "create_entity",
        id.c_str());
    return;
  }

  this->request_creation(ObjectRequest{id, options});
}

void Scene::create_entity(const std::string& id, const std::string& options,
                          const EntityOptions& entity_options) {
  if (this->find_object_definition(id) != nullptr) {
    thoom_log_error(
        "Scene::create_entity error: '%s' is an object; use "
        "create_object",
        id.c_str());
    return;
  }

  this->request_creation(EntityRequest{id, options, entity_options});
}

void Scene::replicate_entity(const EntityRecord& record) {
  this->request_creation(record);
}

Object* Scene::find_object(thoom_lid lid) {
  std::unordered_map<thoom_lid, Object*>::iterator it =
      this->objects_by_lid.find(lid);

  if (it == this->objects_by_lid.end() || it->second->deleted) {
    return nullptr;
  }

  return it->second;
}

Entity* Scene::find_entity(thoom_gid gid) {
  std::unordered_map<thoom_gid, Entity*>::iterator it =
      this->entities_by_gid.find(gid);

  if (it == this->entities_by_gid.end() || it->second->deleted) {
    return nullptr;
  }

  return it->second;
}

void Scene::save_objects() {
  for (std::vector<Object*>& objects : this->object_lists) {
    for (Object* object : objects) {
      Scene::save_tree(*object);
    }
  }
}

void Scene::step() {
  if (!this->next_map.path.empty()) {
    MapRequest request = std::exchange(this->next_map, {});

    this->unload();
    this->load_map(request.path, request.create_objects);

    this->state = Scene::kRunning;
    this->tick();
    this->state = Scene::kIdle;
    return;
  }

  uint64_t now = SDL_GetTicksNS();
  this->accumulator += now - this->last_frame_ns;
  this->last_frame_ns = now;

  if (this->accumulator > THOOM_MAX_FRAME_NS) {
    this->accumulator = THOOM_MAX_FRAME_NS;
  }

  this->state = Scene::kRunning;

  while (this->accumulator >= THOOM_TICK_NS) {
    this->accumulator -= THOOM_TICK_NS;
    this->tick();
  }

  this->state = Scene::kIdle;
}

void Scene::tick() {
  const thoom_pid current_pid = PeerIdentity::instance->get_current();

  // 16 and two thirds of a millisecond per tick, to track 60 ticks per second.
  this->ticks += 16;
  this->tick_fraction += 2;

  if (this->tick_fraction >= 3) {
    this->tick_fraction -= 3;
    this->ticks += 1;
  }

  Renderer::instance->sprites.clear();

  this->step_local_trees(current_pid);
  this->create_requested_objects();
  this->render_trees();
  this->destroy_deleted_trees();
}

void Scene::step_local_trees(thoom_pid current_pid) {
  for (std::vector<Object*>& objects : this->object_lists) {
    for (Object* object : objects) {
      Entity* entity = dynamic_cast<Entity*>(object);
      if (entity != nullptr && entity->owner_pid != current_pid) {
        continue;
      }

      Scene::step_tree(*object);
    }
  }
}

void Scene::render_trees() {
  for (std::vector<Object*>& objects : this->object_lists) {
    for (Object* object : objects) {
      Scene::render_tree(*object);
    }
  }
}

void Scene::destroy_deleted_trees() {
  for (std::vector<Object*>& objects : this->object_lists) {
    for (Object* object : objects) {
      if (!object->deleted) {
        Scene::destroy_deleted_children(*object);
      }
    }

    std::vector<Object*>::iterator it = objects.begin();
    while (it != objects.end()) {
      if (!(*it)->deleted) {
        it++;
        continue;
      }

      this->destroy_object(*it);
      it = objects.erase(it);
    }
  }
}

void Scene::destroy_object(Object* object) {
  this->objects_by_lid.erase(object->lid);

  Entity* entity = dynamic_cast<Entity*>(object);
  if (entity != nullptr) {
    this->entities_by_gid.erase(entity->gid);
  }

  delete object;
}

void Scene::load_map(const std::string& path, bool create_objects) {
  this->state = Scene::kLoading;

  this->map = new Map(path.c_str());
  Audio::instance->loop(this->map->ambience);

  // Objects may set the view while they are created.
  Renderer::instance->reset_view();

  if (create_objects) {
    for (const ObjectRequest& request : this->map->objects) {
      this->create_map_object(request);
    }
  }

  this->create_requested_objects();

  this->state = Scene::kIdle;

  if (save.geti(LOAD_SAVE) == 1) save.data.erase(LOAD_SAVE);

  save.data[LOAD_MAP] = this->map->path;

  this->last_frame_ns = SDL_GetTicksNS();
  this->accumulator = 0;

  if (Hud::instance != nullptr) {
    Renderer::instance->clear(Hud::instance->ui, kMask);
  }
}

void Scene::unload() {
  Renderer::instance->sprites.clear();

  this->save_objects();

  this->state = Scene::kUnloading;

  while (!this->creation_requests.empty()) this->creation_requests.pop();

  // Normal objects belong to the map. Objects in the first and last lists
  // outlive it, so a map change does not remove session objects.
  for (Object* object : this->object_lists[kObjectNormal]) {
    this->destroy_object(object);
  }

  this->object_lists[kObjectNormal].clear();

  delete this->map;
  this->map = nullptr;

  this->state = Scene::kIdle;
}

const ObjectDef* Scene::find_object_definition(const std::string& id) const {
  std::unordered_map<std::string, ObjectDef>::const_iterator it =
      this->object_factories.find(id);
  return it == this->object_factories.end() ? nullptr : &it->second;
}

const EntityDef* Scene::find_entity_definition(const std::string& id) const {
  std::unordered_map<std::string, EntityDef>::const_iterator it =
      this->entity_factories.find(id);
  return it == this->entity_factories.end() ? nullptr : &it->second;
}

void Scene::create_map_object(const ObjectRequest& request) {
  if (this->find_object_definition(request.id) != nullptr) {
    this->create_object(request.id, request.options);
    return;
  }

  if (this->find_entity_definition(request.id) != nullptr) {
    const EntityDef* def = this->find_entity_definition(request.id);
    this->create_entity(request.id, request.options, def->defaults);
    return;
  }

  thoom_log_error("Scene::create_map_object error: '%s' does not exist",
                  request.id.c_str());
}

void Scene::request_creation(CreationRequest request) {
  if (this->state == Scene::kUnloading) {
    thoom_log_warn("Scene: creation request dropped while the scene unloads");
    return;
  }

  this->creation_requests.push(std::move(request));

  if (this->state == Scene::kIdle && !this->draining_creations) {
    this->create_requested_objects();
  }
}

void Scene::process_object_request(const ObjectRequest& request) {
  const ObjectDef* def = this->find_object_definition(request.id);
  if (def == nullptr) {
    thoom_log_error("Scene::create_object error: '%s' does not exist",
                    request.id.c_str());
    return;
  }

  this->instantiate_object(*def, request.options);
}

void Scene::process_entity_request(const EntityRequest& request) {
  const EntityDef* def = this->find_entity_definition(request.id);
  if (def == nullptr) {
    thoom_log_error("Scene::create_entity error: '%s' does not exist",
                    request.id.c_str());
    return;
  }

  const thoom_pid current_pid = PeerIdentity::instance->get_current();
  if (current_pid == THOOM_PID_INVALID) {
    thoom_log_error(
        "Scene::create_entity error: '%s' needs a pid to take "
        "its id from",
        request.id.c_str());
    return;
  }

  if (request.policy.ownership != Entity::Ownership::kFixed &&
      request.policy.ownership != Entity::Ownership::kRoundRobin) {
    thoom_log_error("Scene::create_entity error: invalid ownership");
    return;
  }

  if (request.policy.ownership == Entity::Ownership::kRoundRobin &&
      request.policy.owner_pid != THOOM_PID_INVALID) {
    thoom_log_error(
        "Scene::create_entity error: a round-robin entity "
        "cannot request a fixed pid");
    return;
  }

  thoom_pid owner_pid = request.policy.owner_pid;
  if (request.policy.ownership == Entity::Ownership::kRoundRobin ||
      owner_pid == THOOM_PID_INVALID) {
    owner_pid = current_pid;
  }

  Entity::Context context = {current_pid, std::nullopt, owner_pid,
                             request.policy.ownership};
  this->instantiate_entity(*def, context, request.options);
}

void Scene::process_entity_record(const EntityRecord& record) {
  const EntityDef* def = this->find_entity_definition(record.id);
  if (def == nullptr) {
    thoom_log_error("Scene::replicate_entity error: '%s' does not exist",
                    record.id.c_str());
    return;
  }

  if (record.gid == THOOM_GID_INVALID ||
      THOOM_GID_PID(record.gid) == THOOM_PID_INVALID ||
      record.owner_pid == THOOM_PID_INVALID ||
      (record.ownership != Entity::Ownership::kFixed &&
       record.ownership != Entity::Ownership::kRoundRobin)) {
    thoom_log_error("Scene::replicate_entity error: invalid identity");
    return;
  }

  if (this->entities_by_gid.find(record.gid) != this->entities_by_gid.end()) {
    thoom_log_error("Scene::replicate_entity error: repeated gid %" PRIu64,
                    record.gid);
    return;
  }

  Entity::Context context = {THOOM_GID_PID(record.gid), record.gid,
                             record.owner_pid, record.ownership};
  this->instantiate_entity(*def, context, record.options);
}

void Scene::create_requested_objects() {
  if (this->draining_creations) {
    return;
  }

  this->draining_creations = true;

  // The queue's deque keeps references valid when factories append requests.
  while (!this->creation_requests.empty()) {
    const CreationRequest& request = this->creation_requests.front();

    if (const ObjectRequest* object = std::get_if<ObjectRequest>(&request)) {
      this->process_object_request(*object);
    } else if (const EntityRequest* entity =
                   std::get_if<EntityRequest>(&request)) {
      this->process_entity_request(*entity);
    } else {
      this->process_entity_record(std::get<EntityRecord>(request));
    }

    this->creation_requests.pop();
  }

  this->draining_creations = false;
}

void Scene::instantiate_object(const ObjectDef& def,
                               const std::string& options) {
  Object* object = def.create_object(options);
  if (object == nullptr) {
    thoom_log_error("Scene::create_object error: '%s' factory returned null",
                    def.id);
    return;
  }

  if (dynamic_cast<Entity*>(object) != nullptr) {
    thoom_log_error(
        "Scene::create_object error: '%s' factory did not "
        "return a local root",
        def.id);
    delete object;
    return;
  }

  this->admit_root(object, def.order);
}

void Scene::instantiate_entity(const EntityDef& def,
                               const Entity::Context& context,
                               const std::string& options) {
  Entity* entity = def.create_entity(context, options);
  if (entity == nullptr) {
    thoom_log_error("Scene::create_entity error: '%s' factory returned null",
                    def.id);
    return;
  }

  this->admit_root(entity, def.order);
}

bool Scene::admit_root(Object* object, ObjectOrder order) {
  if (object == nullptr) {
    return false;
  }

  if (object->get_parent() != nullptr) {
    thoom_log_error("Scene: a root cannot have a parent");
    delete object;
    return false;
  }

  if (object->deleted) {
    delete object;
    return false;
  }

  if (this->objects_by_lid.find(object->lid) != this->objects_by_lid.end()) {
    thoom_log_fatal("Scene: repeated lid %" PRIu32, object->lid);
  }

  Entity* entity = dynamic_cast<Entity*>(object);
  if (entity != nullptr &&
      !this->entities_by_gid.emplace(entity->gid, entity).second) {
    thoom_log_error("Scene: repeated gid %" PRIu64, entity->gid);
    delete entity;
    return false;
  }

  this->objects_by_lid.emplace(object->lid, object);
  this->object_lists[order].push_back(object);
  return true;
}

// Index-based traversal tolerates children appended during callbacks.
void Scene::step_tree(Object& object) {
  if (object.deleted) {
    return;
  }

  const size_t child_count = object.get_children().size();
  object.step();
  if (object.deleted) {
    return;
  }

  for (size_t i = 0; i < child_count && !object.deleted; i++) {
    Scene::step_tree(*object.get_children()[i]);
  }
}

void Scene::render_tree(Object& object) {
  if (object.deleted) {
    return;
  }

  const size_t child_count = object.get_children().size();
  object.render();
  if (object.deleted) {
    return;
  }

  for (size_t i = 0; i < child_count && !object.deleted; i++) {
    Scene::render_tree(*object.get_children()[i]);
  }
}

void Scene::save_tree(Object& object) {
  const size_t child_count = object.get_children().size();
  object.save_data();

  for (size_t i = 0; i < child_count; i++) {
    Scene::save_tree(*object.get_children()[i]);
  }
}

void Scene::destroy_deleted_children(Object& object) {
  size_t i = 0;
  while (i < object.get_children().size()) {
    Object* child = object.get_children()[i];
    if (child->deleted) {
      // Deletion detaches the child, so the next child keeps this index.
      delete child;
      continue;
    }

    Scene::destroy_deleted_children(*child);
    i++;
  }
}

};  // namespace thoom
