#pragma once

#include <string>
#include <vector>

#include "thoom/thoom.h"

namespace thoom {

enum ObjectOrder {
  kObjectFirst,
  kObjectNormal,
  kObjectLast,
};

class Object {
 public:
  explicit Object(Object* parent);

  Object(const Object&) = delete;
  Object& operator=(const Object&) = delete;

  virtual ~Object();
  virtual void step() = 0;
  virtual void render() {}
  virtual void save_data() {}

  Object* get_parent() const;
  const std::vector<Object*>& get_children() const;

  const thoom_lid lid;
  bool deleted = false;

 private:
  Object* parent = nullptr;
  std::vector<Object*> children;

  static thoom_lid next_lid;
};

struct ObjectDef {
  const char* id;
  Object* (*create_object)(const std::string& options);
  ObjectOrder order;
};

struct ObjectRequest {
  std::string id;
  std::string options;
};

};  // namespace thoom
