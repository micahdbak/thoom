#include "object/object.h"

#include <algorithm>
#include <vector>

#include "thoom/log.h"
#include "thoom/thoom.h"

namespace thoom {

thoom_lid Object::next_lid = 0;

Object::Object(Object* parent) : lid(next_lid++), parent(parent) {
  if (next_lid == 0) {
    thoom_log_fatal("Object: no lids remain");
  }

  if (parent != nullptr) {
    parent->children.push_back(this);
  }
}

Object::~Object() {
  while (!this->children.empty()) {
    delete this->children.back();
  }

  if (this->parent == nullptr) {
    return;
  }

  std::vector<Object*>& siblings = this->parent->children;
  std::vector<Object*>::iterator it =
      std::find(siblings.begin(), siblings.end(), this);
  if (it != siblings.end()) {
    siblings.erase(it);
  }
}

Object* Object::get_parent() const { return this->parent; }

const std::vector<Object*>& Object::get_children() const {
  return this->children;
}

};  // namespace thoom
