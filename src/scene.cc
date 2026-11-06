#include "scene.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio.h"
#include "map.h"
#include "object.h"
#include "renderer.h"
#include "save_data.h"
#include "thoom/macros.h"

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

  if (!initial_map.empty()) {
    this->load_map(initial_map, create_objects);
  }
}

Scene::~Scene() {
  this->unload();

  this->state = Scene::kUnloading;

  for (Object* object : this->first_objects) {
    this->objects_by_id.erase(object->id);
    delete object;
  }

  for (Object* object : this->last_objects) {
    this->objects_by_id.erase(object->id);
    delete object;
  }

  this->first_objects.clear();
  this->last_objects.clear();
  this->objects_by_id.clear();

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

  Object* object = it->second.create_object(this, options);

  if (object == nullptr) {
    return;
  }

  if (object->deleted) {
    delete object;
    return;
  }

  switch (it->second.order) {
    case kObjectFirst:
      this->first_objects.push_back(object);
      break;
    case kObjectLast:
      this->last_objects.push_back(object);
      break;
    default:
      this->objects.push_back(object);
      break;
  }

  this->objects_by_id[object->id] = object;
}

Object* Scene::find_object(int id) {
  std::unordered_map<int, Object*>::iterator it = this->objects_by_id.find(id);

  if (it == this->objects_by_id.end()) {
    return nullptr;
  }

  return it->second;
}

void Scene::save_objects() {
  for (Object* object : this->first_objects) object->save_data();
  for (Object* object : this->objects) object->save_data();
  for (Object* object : this->last_objects) object->save_data();
}

void Scene::step() {
  if (!this->next_map.path.empty()) {
    std::string path = this->next_map.path;
    bool create_objects = this->next_map.create_objects;

    this->next_map.path.clear();
    this->next_map.create_objects = true;

    this->unload();
    this->load_map(path, create_objects);
    return;
  }

  this->state = Scene::kRunning;

  uint64_t new_ticks = SDL_GetTicks();
  this->delta = float(new_ticks - this->ticks) / 1000.0f;
  this->ticks = new_ticks;

  for (Object* object : this->first_objects) {
    if (!object->deleted) object->step();
  }

  this->clean_up_objects(this->first_objects);

  for (Object* object : this->objects) {
    if (!object->deleted) object->step();
  }

  this->clean_up_objects(this->objects);

  this->create_new_objects();

  this->clean_up_objects(this->first_objects);
  this->clean_up_objects(this->objects);
  this->clean_up_objects(this->last_objects);

  for (Object* object : this->last_objects) {
    if (!object->deleted) object->step();
  }

  this->clean_up_objects(this->last_objects);

  this->state = Scene::kIdle;
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
}

void Scene::unload() {
  this->save_objects();

  this->state = Scene::kUnloading;

  while (!this->new_objects.empty()) this->new_objects.pop();

  for (Object* object : this->objects) {
    this->objects_by_id.erase(object->id);
    delete object;
  }

  this->objects.clear();

  delete this->map;
  this->map = nullptr;

  this->state = Scene::kIdle;
}

void Scene::clean_up_objects(std::vector<Object*>& objects) {
  std::vector<Object*>::iterator it = objects.begin();

  while (it != objects.end()) {
    Object* object = *it;

    if (!object->deleted) {
      it++;
      continue;
    }

    this->objects_by_id.erase(object->id);
    delete object;
    it = objects.erase(it);
  }
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

    Object* object = it->second.create_object(this, request.options);

    if (object == nullptr) {
      continue;
    }

    if (object->deleted) {
      delete object;
      continue;
    }

    switch (it->second.order) {
      case kObjectFirst:
        this->first_objects.push_back(object);
        break;
      case kObjectLast:
        this->last_objects.push_back(object);
        break;
      default:
        this->objects.push_back(object);
        break;
    }

    this->objects_by_id[object->id] = object;
  }
}

};  // namespace thoom
