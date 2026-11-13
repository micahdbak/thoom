#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "object/object.h"
#include "thoom/thoom.h"

namespace thoom {

class Entity : public Object {
 public:
  static constexpr thoom_nid kRootNid = 0;

  enum class Ownership {
    kFixed,
    kRoundRobin,
  };

  enum class AuthorityScope {
    kAll,
    kLocal,
    kRemote,
  };

  struct Context {
    thoom_pid creator_pid;
    std::optional<thoom_gid> gid;
    thoom_pid owner_pid;
    Ownership ownership;
  };

  struct NodeKey {
    thoom_gid gid = THOOM_GID_INVALID;
    thoom_nid nid = kRootNid;

    bool operator==(const NodeKey& other) const {
      return this->gid == other.gid && this->nid == other.nid;
    }

    bool operator<(const NodeKey& other) const {
      if (this->gid != other.gid) {
        return this->gid < other.gid;
      }

      return this->nid < other.nid;
    }
  };

  struct Event {
    NodeKey target;
    uint32_t kind = 0;
    std::vector<uint8_t> payload;
  };

  class Node : public Object {
   public:
    Node(Object& parent, std::optional<thoom_nid> explicit_nid);

    NodeKey key() const;
    virtual void receive_event(const Event&) {}

    Entity& entity;
    const thoom_nid nid;

   private:
    static Entity& root_for(Object& parent);
  };

  explicit Entity(const Context& context);

  NodeKey key() const;
  Object* find_node(thoom_nid nid);
  virtual void receive_event(const Event&) {}

  const thoom_gid gid;
  thoom_pid owner_pid;
  const Ownership ownership;

 private:
  thoom_nid issue_nid();
  void reserve_nid(Node& node);
  Object* find_descendant(Object& object, thoom_nid nid);

  thoom_nid next_nid = kRootNid + 1;
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
