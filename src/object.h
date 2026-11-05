#pragma once

#include <string>

namespace thoom {

enum ObjectOrder {
  kObjectFirst,
  kObjectNormal,
  kObjectLast,
};

class Object {
 public:
  Object() : id(next_id++) {}
  virtual ~Object() = default;

  virtual void step() = 0;
  virtual void save_data() {}

  int id;
  bool deleted = false;

 private:
  inline static int next_id = 0;
};

struct ObjectRequest {
  std::string id;
  std::string options;
};

};  // namespace thoom
