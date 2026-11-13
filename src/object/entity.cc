#include "object/entity.h"

#include <cinttypes>
#include <optional>

#include "object/object.h"
#include "thoom/log.h"
#include "thoom/thoom.h"

namespace thoom {

Entity::Node::Node(Object& parent, std::optional<thoom_nid> explicit_nid)
    : Object(&parent),
      entity(root_for(parent)),
      nid(explicit_nid.has_value() ? explicit_nid.value()
                                   : entity.issue_nid()) {
  if (explicit_nid.has_value()) {
    entity.reserve_nid(*this);
  }
}

Entity::NodeKey Entity::Node::key() const {
  return {this->entity.gid, this->nid};
}

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

Entity::NodeKey Entity::key() const { return {this->gid, kRootNid}; }

Object* Entity::find_node(thoom_nid nid) {
  if (nid == kRootNid) {
    return this;
  }

  return this->find_descendant(*this, nid);
}

thoom_nid Entity::issue_nid() {
  if (this->next_nid == 0) {
    thoom_log_fatal("Entity: no node ids remain");
  }

  return this->next_nid++;
}

void Entity::reserve_nid(Node& node) {
  if (node.nid == kRootNid) {
    thoom_log_fatal("Entity: node id zero names the root");
  }

  Object* existing = this->find_node(node.nid);
  if (existing != &node) {
    thoom_log_fatal("Entity: repeated node id %" PRIu32, node.nid);
  }

  if (node.nid >= this->next_nid) {
    this->next_nid = node.nid + 1;
  }
}

Object* Entity::find_descendant(Object& object, thoom_nid nid) {
  for (Object* child : object.get_children()) {
    Node* node = dynamic_cast<Node*>(child);
    if (node != nullptr && node->nid == nid) {
      return child;
    }

    Object* result = this->find_descendant(*child, nid);
    if (result != nullptr) {
      return result;
    }
  }

  return nullptr;
}

};  // namespace thoom
