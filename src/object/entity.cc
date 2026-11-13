#include "object/entity.h"

#include <cinttypes>
#include <optional>

#include "object/object.h"
#include "thoom/log.h"
#include "thoom/thoom.h"

namespace thoom {

Entity& Entity::Node::root_for(Object& parent) {
  Entity* entity = dynamic_cast<Entity*>(&parent);
  if (entity != nullptr) {
    return *entity;
  }

  Node* node = dynamic_cast<Node*>(&parent);
  if (node != nullptr) {
    return node->entity;
  }

  thoom_log_fatal("Entity::Node: parent does not belong to an entity");
}

Entity::Node::Node(Object& parent, std::optional<NodeId> id)
    : Object(&parent),
      entity(root_for(parent)),
      node_id(id.has_value() ? id.value() : entity.issue_node_id()) {
  if (id.has_value()) {
    entity.reserve_node_id(*this);
  }
}

Entity::Entity(const Context& context)
    : Object(nullptr),
      gid(context.gid.has_value()
              ? context.gid.value()
              : THOOM_GID_COMPOSE(context.creator_pid, this->lid)),
      owner_pid(context.owner_pid),
      ownership(context.ownership) {
  if (context.creator_pid == THOOM_PID_INVALID) {
    thoom_log_fatal("Entity: invalid creator pid");
  }
  if (this->gid == THOOM_GID_INVALID) {
    thoom_log_fatal("Entity: invalid gid");
  }
  if (this->owner_pid == THOOM_PID_INVALID) {
    thoom_log_fatal("Entity: invalid owner pid");
  }
}

Object* Entity::find_node(NodeId id) {
  if (id == 0) {
    return this;
  }

  return this->find_descendant(*this, id);
}

NodeId Entity::issue_node_id() {
  if (this->next_node_id == 0) {
    thoom_log_fatal("Entity: no node ids remain");
  }

  return this->next_node_id++;
}

void Entity::reserve_node_id(Node& node) {
  if (node.node_id == 0) {
    thoom_log_fatal("Entity: node id zero names the root");
  }

  Object* existing = this->find_node(node.node_id);
  if (existing != &node) {
    thoom_log_fatal("Entity: repeated node id %" PRIu32, node.node_id);
  }

  if (node.node_id >= this->next_node_id) {
    this->next_node_id = node.node_id + 1;
  }
}

Object* Entity::find_descendant(Object& object, NodeId id) {
  for (Object* child : object.get_children()) {
    Node* node = dynamic_cast<Node*>(child);
    if (node != nullptr && node->node_id == id) {
      return child;
    }

    Object* result = this->find_descendant(*child, id);
    if (result != nullptr) {
      return result;
    }
  }

  return nullptr;
}

};  // namespace thoom
