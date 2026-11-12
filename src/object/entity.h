#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "object/object.h"
#include "thoom/thoom.h"

namespace thoom {

typedef uint32_t NodeId;

class Entity : public Object {
 public:
  enum class Ownership {
    kFixed,
    kRoundRobin,
  };

  struct Context {
    thoom_pid creator_pid;
    std::optional<thoom_gid> gid;
    thoom_pid owner_pid;
    Ownership ownership;
  };

  class Node : public Object {
   public:
    Node(Object& parent, std::optional<NodeId> id);

    Entity& entity;
    const NodeId node_id;

   private:
    static Entity& root_for(Object& parent);
  };

  explicit Entity(const Context& context);

  Object* find_node(NodeId id);

  const thoom_gid gid;
  thoom_pid owner_pid;
  const Ownership ownership;

 private:
  NodeId issue_node_id();
  void reserve_node_id(Node& node);
  Object* find_descendant(Object& object, NodeId id);

  NodeId next_node_id = 1;
};

struct EntityOptions {
  Entity::Ownership ownership = Entity::Ownership::kRoundRobin;
  thoom_pid owner_pid = THOOM_PID_INVALID;
};

struct EntityRequest {
  std::string id;
  std::string options;
  EntityOptions policy;
};

struct EntityDef {
  const char* id;
  Entity* (*create_entity)(const Entity::Context& context,
                           const std::string& options);
  EntityOptions defaults;
  ObjectOrder order;
};

struct EntityRecord {
  std::string id;
  std::string options;
  thoom_gid gid;
  thoom_pid owner_pid;
  Entity::Ownership ownership;
};

};  // namespace thoom
