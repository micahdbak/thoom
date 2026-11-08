#include "scene/scene.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/audio.h"
#include "colour.h"
#include "hud/hud.h"
#include "map/map.h"
#include "object.h"
#include "renderer/renderer.h"
#include "save/save_data.h"
#include "thoom/macros.h"
#include "thoom/thoom.h"

#define THOOM_TICK_NS 16666667
#define THOOM_MAX_FRAME_NS 250000000

namespace thoom {

bool Scene::init(const std::string& initial_map, bool create_objects) {
  if (Scene::instance != nullptr) {
    fprintf(stderr, THOOM_LOG "Scene::init error: scene already initialized\n");
    return false;
  }

  // Scene::Scene sets Scene::instance internally
  new Scene(initial_map, create_objects);
  return true;
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
  if (def.id == nullptr) {
    return;
  }

  if (this->factories.find(def.id) != this->factories.end()) {
    fprintf(stderr,
            THOOM_LOG "Scene::register_object error: '%s' already exists\n",
            def.id);
    exit(1);
  }

  this->factories[def.id] = def;
}

void Scene::register_objects(const ObjectDef* defs) {
  if (defs == nullptr) {
    return;
  }

  for (const ObjectDef* def = defs; def->id != nullptr; def++) {
    this->register_object(*def);
  }
}

void Scene::request_map(const std::string& path, bool create_objects) {
  this->next_map.path = path;
  this->next_map.create_objects = create_objects;
}

void Scene::create_object(const std::string& id, const std::string& options) {
  if (this->state == Scene::kUnloading) {
    return;
  }

  if (this->state == Scene::kRunning || this->state == Scene::kLoading) {
    ObjectRequest request;
    request.id = id;
    request.options = options;
    this->new_objects.push(request);
    return;
  }

  std::unordered_map<std::string, ObjectDef>::iterator it =
      this->factories.find(id);

  if (it == this->factories.end()) {
    fprintf(stderr,
            THOOM_LOG "Scene::create_object error: '%s' does not exist\n",
            id.c_str());
    return;
  }

  this->spawn_object(it->second, options);
}

void Scene::register_gid_object(Object* object) {
  if (object == nullptr) {
    return;
  }

  if (!object->replicated) {
    if (object->gid != THOOM_GID_NULL ||
        object->owner_pid != THOOM_PID_INVALID) {
      fprintf(stderr, THOOM_LOG
              "Scene::register_gid_object error: local object has network "
              "identity\n");
      exit(1);
    }

    return;
  }

  if (object->gid == THOOM_GID_NULL) {
    fprintf(stderr, THOOM_LOG
            "Scene::register_gid_object error: replicated object has no gid\n");
    exit(1);
  }

  if (object->owner_pid == THOOM_PID_INVALID) {
    fprintf(stderr, THOOM_LOG
            "Scene::register_gid_object error: replicated object has no "
            "owner\n");
    exit(1);
  }

  this->objects_by_gid[object->gid] = object;
}

void Scene::unregister_gid_object(Object* object) {
  if (object == nullptr || object->gid == THOOM_GID_NULL) {
    return;
  }

  this->objects_by_gid.erase(object->gid);
}

Object* Scene::find_object(thoom_lid lid) {
  std::unordered_map<thoom_lid, Object*>::iterator it =
      this->objects_by_lid.find(lid);

  if (it == this->objects_by_lid.end() || it->second->deleted) {
    return nullptr;
  }

  return it->second;
}

Object* Scene::find_gid_object(thoom_gid gid) {
  std::unordered_map<thoom_gid, Object*>::iterator it =
      this->objects_by_gid.find(gid);

  if (it == this->objects_by_gid.end() || it->second->deleted) {
    return nullptr;
  }

  return it->second;
}

void Scene::save_objects() {
  for (std::vector<Object*>& objects : this->object_lists) {
    for (Object* object : objects) object->save_data();
  }
}

void Scene::step() {
  if (!this->next_map.path.empty()) {
    std::string path = this->next_map.path;
    bool create_objects = this->next_map.create_objects;

    this->next_map.path.clear();
    this->next_map.create_objects = true;

    this->unload();
    this->load_map(path, create_objects);

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
  // 16 and two thirds of a millisecond per tick, to track 60 ticks per second.
  this->ticks += 16;
  this->tick_fraction += 2;

  if (this->tick_fraction >= 3) {
    this->tick_fraction -= 3;
    this->ticks += 1;
  }

  Renderer::instance->sprites.clear();

  // step all objects which should be simulated locally
  for (std::vector<Object*>& objects : this->object_lists) {
    for (Object* object : objects) {
      if (object->deleted) continue;
      if (object->replicated && object->owner_pid != this->local_pid) continue;

      object->step();
    }
  }

  this->create_new_objects();

  // render all objects (even non-owned ones)
  for (std::vector<Object*>& objects : this->object_lists) {
    for (Object* object : objects) {
      if (!object->deleted) object->render();
    }
  }

  // destroy any objects that were marked for deletion
  for (std::vector<Object*>& objects : this->object_lists) {
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
  this->unregister_gid_object(object);
  delete object;
}

void Scene::load_map(const std::string& path, bool create_objects) {
  this->state = Scene::kLoading;

  this->map = new Map(path.c_str());
  Audio::instance->loop(this->map->ambience);

  // Objects may set the view while they are created.
  Renderer::instance->reset_view();

  if (create_objects) {
    for (const ObjectRequest& object : this->map->objects) {
      this->create_object(object.id, object.options);
    }
  }

  this->create_new_objects();

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

  while (!this->new_objects.empty()) this->new_objects.pop();

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

void Scene::create_new_objects() {
  while (!this->new_objects.empty()) {
    ObjectRequest request = this->new_objects.front();
    this->new_objects.pop();

    std::unordered_map<std::string, ObjectDef>::iterator it =
        this->factories.find(request.id);

    if (it == this->factories.end()) {
      fprintf(stderr,
              THOOM_LOG
              "Scene::create_new_objects error: '%s' does not exist\n",
              request.id.c_str());
      continue;
    }

    this->spawn_object(it->second, request.options);
  }
}

Object* Scene::spawn_object(const ObjectDef& def, const std::string& options) {
  // Objects take their gid in the constructor, so constructors can already
  // reference their own object. Replication is decided by the definition.
  bool was_spawning_replicated = Object::spawning_replicated;
  Object::spawning_replicated = def.replicated;

  Object* object = def.create_object(this, options);

  Object::spawning_replicated = was_spawning_replicated;

  if (object == nullptr) {
    return nullptr;
  }

  if (object->deleted) {
    delete object;
    return nullptr;
  }

  if (object->replicated && object->owner_pid == THOOM_PID_INVALID) {
    object->owner_pid = this->local_pid;
  }

  this->object_lists[def.order].push_back(object);

  this->objects_by_lid[object->lid] = object;
  this->register_gid_object(object);

  return object;
}

};  // namespace thoom
