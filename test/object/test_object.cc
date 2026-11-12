#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "SDL3/SDL.h"
#include "object/entity.h"
#include "object/object.h"
#include "scene/scene.h"
#include "switch/peer_identity.h"
#include "thoom/thoom.h"

class DummyObject : public thoom::Object {
 public:
  DummyObject() : thoom::Object(nullptr) {}

  void step() override {}
};

class DummyEntity : public thoom::Entity {
 public:
  explicit DummyEntity(const thoom::Entity::Context& context)
      : thoom::Entity(context) {}

  ~DummyEntity() override { DummyEntity::destructed_count++; }

  void step() override { this->step_count++; }
  void render() override { this->render_count++; }

  int step_count = 0;
  int render_count = 0;

  inline static int destructed_count = 0;
};

class DummyNode : public thoom::Entity::Node {
 public:
  DummyNode(thoom::Object& parent, std::optional<thoom::NodeId> id)
      : thoom::Entity::Node(parent, id) {}

  ~DummyNode() override { DummyNode::destructed_ids.push_back(this->node_id); }

  void step() override { this->step_count++; }
  void render() override { this->render_count++; }

  int step_count = 0;
  int render_count = 0;

  inline static std::vector<thoom::NodeId> destructed_ids;
};

class DummyAddingEntity : public DummyEntity {
 public:
  explicit DummyAddingEntity(const thoom::Entity::Context& context)
      : DummyEntity(context) {}

  void step() override {
    DummyEntity::step();
    if (this->add_child_on_step) {
      this->add_child_on_step = false;
      new DummyNode(*this, std::nullopt);
    }
  }

  void render() override {
    DummyEntity::render();
    if (this->add_child_on_render) {
      this->add_child_on_render = false;
      new DummyNode(*this, std::nullopt);
    }
  }

  bool add_child_on_step = false;
  bool add_child_on_render = false;
};

static DummyAddingEntity* last_created_entity = nullptr;

thoom::Entity* create_dummy_entity(const thoom::Entity::Context& context,
                                   const std::string&) {
  last_created_entity = new DummyAddingEntity(context);
  return last_created_entity;
}

static const thoom::EntityDef kDummyEntityDefs[] = {
    {"dummy_entity",
     create_dummy_entity,
     {thoom::Entity::Ownership::kFixed, THOOM_PID_INVALID},
     thoom::kObjectNormal},
    {nullptr, nullptr, {}, thoom::kObjectNormal},
};

class DummyTreeEntity : public DummyEntity {
 public:
  explicit DummyTreeEntity(const thoom::Entity::Context& context)
      : DummyEntity(context) {
    DummyNode* child = new DummyNode(*this, std::nullopt);
    new DummyNode(static_cast<thoom::Object&>(*child), std::nullopt);
  }
};

class ObjectTest : public ::testing::Test {
 protected:
  struct Tree {
    DummyNode* first;
    DummyNode* grandchild;
    DummyNode* second;
    DummyNode* great_grandchild;
  };

  void SetUp() override {
    DummyEntity::destructed_count = 0;
    DummyNode::destructed_ids.clear();
  }

  thoom::Entity::Context local_context(
      thoom_pid creator_pid = 7, thoom_pid owner_pid = 7,
      thoom::Entity::Ownership ownership = thoom::Entity::Ownership::kFixed) {
    return {creator_pid, std::nullopt, owner_pid, ownership};
  }

  Tree create_tree(DummyEntity& entity) {
    Tree tree;
    tree.first = new DummyNode(entity, std::nullopt);
    tree.grandchild =
        new DummyNode(static_cast<thoom::Object&>(*tree.first), std::nullopt);
    tree.second = new DummyNode(entity, std::nullopt);
    tree.great_grandchild = new DummyNode(
        static_cast<thoom::Object&>(*tree.grandchild), std::nullopt);
    return tree;
  }
};

TEST_F(ObjectTest, LocalObjectIsAnIndependentRoot) {
  DummyObject object;

  EXPECT_EQ(object.get_parent(), nullptr);
  EXPECT_TRUE(object.get_children().empty());
  EXPECT_FALSE(object.deleted);
}

TEST_F(ObjectTest, LocalEntityComposesItsGidFromCreatorAndLid) {
  DummyEntity entity(
      this->local_context(7, 3, thoom::Entity::Ownership::kRoundRobin));

  EXPECT_EQ(entity.gid, THOOM_GID_COMPOSE(7, entity.lid));
  EXPECT_EQ(entity.owner_pid, 3u);
  EXPECT_EQ(entity.ownership, thoom::Entity::Ownership::kRoundRobin);
  EXPECT_EQ(entity.get_parent(), nullptr);
}

TEST_F(ObjectTest, ReceivedEntityUsesItsSuppliedGid) {
  const thoom_gid gid = THOOM_GID_COMPOSE(4, THOOM_LID_MAX - 1);
  thoom::Entity::Context context = this->local_context(4, 2);
  context.gid = gid;
  DummyEntity entity(context);

  EXPECT_EQ(entity.gid, gid);
  EXPECT_NE(entity.lid, THOOM_GID_LID(gid));
  EXPECT_EQ(entity.owner_pid, 2u);
}

TEST_F(ObjectTest, RootsAndNodesShareOnlyTheProcessLocalLidSequence) {
  DummyObject object;
  DummyEntity entity(this->local_context());
  DummyNode node(entity, std::nullopt);

  EXPECT_LT(object.lid, entity.lid);
  EXPECT_LT(entity.lid, node.lid);
}

TEST_F(ObjectTest, NestedTreeTracksParentsChildrenAndRootEntity) {
  DummyEntity entity(this->local_context());
  Tree tree = this->create_tree(entity);

  ASSERT_EQ(entity.get_children().size(), 2u);
  EXPECT_EQ(entity.get_children()[0], tree.first);
  EXPECT_EQ(entity.get_children()[1], tree.second);
  EXPECT_EQ(tree.first->get_parent(), &entity);
  EXPECT_EQ(tree.second->get_parent(), &entity);
  EXPECT_EQ(tree.grandchild->get_parent(), tree.first);
  EXPECT_EQ(tree.great_grandchild->get_parent(), tree.grandchild);

  EXPECT_EQ(&tree.first->entity, &entity);
  EXPECT_EQ(&tree.grandchild->entity, &entity);
  EXPECT_EQ(&tree.second->entity, &entity);
  EXPECT_EQ(&tree.great_grandchild->entity, &entity);

  EXPECT_EQ(tree.first->node_id, 1u);
  EXPECT_EQ(tree.grandchild->node_id, 2u);
  EXPECT_EQ(tree.second->node_id, 3u);
  EXPECT_EQ(tree.great_grandchild->node_id, 4u);
}

TEST_F(ObjectTest, ConstructorBuiltTreeSharesRootIdentity) {
  const thoom_gid gid = THOOM_GID_COMPOSE(9, 81);
  thoom::Entity::Context context = this->local_context(9, 4);
  context.gid = gid;
  DummyTreeEntity entity(context);

  DummyNode* child = dynamic_cast<DummyNode*>(entity.find_node(1));
  DummyNode* grandchild = dynamic_cast<DummyNode*>(entity.find_node(2));
  ASSERT_NE(child, nullptr);
  ASSERT_NE(grandchild, nullptr);
  EXPECT_EQ(child->entity.gid, gid);
  EXPECT_EQ(grandchild->entity.gid, gid);
  EXPECT_EQ(child->entity.owner_pid, 4u);
  EXPECT_EQ(grandchild->entity.owner_pid, 4u);
}

TEST_F(ObjectTest, EntityFindsNodesAtAnyDepth) {
  DummyEntity entity(this->local_context());
  Tree tree = this->create_tree(entity);

  EXPECT_EQ(entity.find_node(0), &entity);
  EXPECT_EQ(entity.find_node(1), tree.first);
  EXPECT_EQ(entity.find_node(2), tree.grandchild);
  EXPECT_EQ(entity.find_node(3), tree.second);
  EXPECT_EQ(entity.find_node(4), tree.great_grandchild);
  EXPECT_EQ(entity.find_node(5), nullptr);
}

TEST_F(ObjectTest, NodeIdsAreScopedToTheirEntity) {
  DummyEntity first_entity(this->local_context());
  DummyEntity second_entity(this->local_context());
  DummyNode first_node(first_entity, std::nullopt);
  DummyNode second_node(second_entity, std::nullopt);

  EXPECT_EQ(first_node.node_id, second_node.node_id);
  EXPECT_EQ(first_entity.find_node(1), &first_node);
  EXPECT_EQ(second_entity.find_node(1), &second_node);
  EXPECT_NE(&first_node.entity, &second_node.entity);
}

TEST_F(ObjectTest, NodeIdsAreNotReusedAfterDeletion) {
  DummyEntity entity(this->local_context());
  DummyNode* first = new DummyNode(entity, std::nullopt);
  const thoom::NodeId first_id = first->node_id;

  delete static_cast<thoom::Object*>(first);
  DummyNode* replacement = new DummyNode(entity, std::nullopt);

  EXPECT_NE(replacement->node_id, first_id);
  EXPECT_EQ(entity.find_node(first_id), nullptr);
  EXPECT_EQ(entity.find_node(replacement->node_id), replacement);
  EXPECT_EQ(entity.get_children().size(), 1u);
}

TEST_F(ObjectTest, ExplicitNodeIdAdvancesTheAllocatorWithoutReusingLowIds) {
  DummyEntity entity(this->local_context());
  DummyNode high(entity, 10);
  DummyNode next(entity, std::nullopt);
  DummyNode low(entity, 4);
  DummyNode after_low(entity, std::nullopt);

  EXPECT_EQ(next.node_id, 11u);
  EXPECT_EQ(after_low.node_id, 12u);
  EXPECT_EQ(entity.find_node(10), &high);
  EXPECT_EQ(entity.find_node(11), &next);
  EXPECT_EQ(entity.find_node(4), &low);
  EXPECT_EQ(entity.find_node(12), &after_low);
}

TEST_F(ObjectTest, DeletingALeafDetachesOnlyThatNode) {
  DummyEntity entity(this->local_context());
  Tree tree = this->create_tree(entity);

  delete static_cast<thoom::Object*>(tree.great_grandchild);

  EXPECT_TRUE(tree.grandchild->get_children().empty());
  EXPECT_EQ(entity.find_node(4), nullptr);
  EXPECT_EQ(entity.find_node(1), tree.first);
  EXPECT_EQ(entity.find_node(2), tree.grandchild);
  EXPECT_EQ(entity.find_node(3), tree.second);
  EXPECT_EQ(DummyNode::destructed_ids, std::vector<thoom::NodeId>({4}));
}

TEST_F(ObjectTest, DeletingASubtreeLeavesItsFormerSiblingsIntact) {
  DummyEntity entity(this->local_context());
  Tree tree = this->create_tree(entity);

  delete static_cast<thoom::Object*>(tree.first);

  ASSERT_EQ(entity.get_children().size(), 1u);
  EXPECT_EQ(entity.get_children()[0], tree.second);
  EXPECT_EQ(entity.find_node(1), nullptr);
  EXPECT_EQ(entity.find_node(2), nullptr);
  EXPECT_EQ(entity.find_node(4), nullptr);
  EXPECT_EQ(entity.find_node(3), tree.second);
  EXPECT_EQ(tree.second->get_parent(), &entity);

  std::sort(DummyNode::destructed_ids.begin(), DummyNode::destructed_ids.end());
  EXPECT_EQ(DummyNode::destructed_ids, std::vector<thoom::NodeId>({1, 2, 4}));
}

TEST_F(ObjectTest, DeletedSubtreeCanBeRebuiltWithFreshIds) {
  DummyEntity entity(this->local_context());
  Tree tree = this->create_tree(entity);
  delete static_cast<thoom::Object*>(tree.first);

  DummyNode* replacement = new DummyNode(entity, std::nullopt);
  DummyNode* nested =
      new DummyNode(static_cast<thoom::Object&>(*replacement), std::nullopt);

  EXPECT_EQ(replacement->node_id, 5u);
  EXPECT_EQ(nested->node_id, 6u);
  EXPECT_EQ(entity.find_node(5), replacement);
  EXPECT_EQ(entity.find_node(6), nested);
  EXPECT_EQ(entity.get_children().size(), 2u);
}

TEST_F(ObjectTest, EntityDestructionRecursivelyDestroysEveryNodeOnce) {
  DummyEntity* entity = new DummyEntity(this->local_context());
  this->create_tree(*entity);

  delete entity;

  EXPECT_EQ(DummyEntity::destructed_count, 1);
  std::sort(DummyNode::destructed_ids.begin(), DummyNode::destructed_ids.end());
  EXPECT_EQ(DummyNode::destructed_ids,
            std::vector<thoom::NodeId>({1, 2, 3, 4}));
}

TEST_F(ObjectTest, EmptyEntityDestructionDoesNotDestroyAnyNodes) {
  DummyEntity* entity = new DummyEntity(this->local_context());

  delete entity;

  EXPECT_EQ(DummyEntity::destructed_count, 1);
  EXPECT_TRUE(DummyNode::destructed_ids.empty());
}

class ObjectTreeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));

    ASSERT_TRUE(thoom_init("thoom object tree test", 320, 240, 0, nullptr));
    ASSERT_TRUE(thoom::Scene::init("", true));
    thoom::Scene::instance->register_entities(kDummyEntityDefs);

    last_created_entity = nullptr;
    DummyEntity::destructed_count = 0;
    DummyNode::destructed_ids.clear();
  }

  void TearDown() override {
    thoom::Scene::quit();
    thoom_quit();
    last_created_entity = nullptr;
  }

  DummyAddingEntity* create_entity() {
    thoom::PeerIdentity::instance->set_current(THOOM_PID_HOST);
    last_created_entity = nullptr;
    thoom::Scene::instance->create_entity(
        "dummy_entity", "",
        {thoom::Entity::Ownership::kFixed, THOOM_PID_INVALID});
    return last_created_entity;
  }

  DummyAddingEntity* replicate_entity(thoom_gid gid, thoom_pid owner_pid) {
    last_created_entity = nullptr;
    thoom::EntityRecord record = {"dummy_entity", "", gid, owner_pid,
                                  thoom::Entity::Ownership::kFixed};
    thoom::Scene::instance->replicate_entity(record);
    return last_created_entity;
  }

  DummyNode* add_branch(DummyEntity& entity) {
    DummyNode* child = new DummyNode(entity, std::nullopt);
    new DummyNode(static_cast<thoom::Object&>(*child), std::nullopt);
    return child;
  }

  void tick() {
    SDL_Delay(25);
    thoom::Scene::instance->step();
  }
};

TEST_F(ObjectTreeTest, NonOwnedEntityTreeOnlyRenders) {
  thoom::PeerIdentity::instance->set_current(7);
  DummyAddingEntity* entity =
      this->replicate_entity(THOOM_GID_COMPOSE(3, 50), 3);
  ASSERT_NE(entity, nullptr);
  DummyNode* child = this->add_branch(*entity);
  DummyNode* grandchild = dynamic_cast<DummyNode*>(entity->find_node(2));
  ASSERT_NE(grandchild, nullptr);

  this->tick();

  EXPECT_EQ(entity->step_count, 0);
  EXPECT_EQ(child->step_count, 0);
  EXPECT_EQ(grandchild->step_count, 0);
  EXPECT_GT(entity->render_count, 0);
  EXPECT_GT(child->render_count, 0);
  EXPECT_GT(grandchild->render_count, 0);
}

TEST_F(ObjectTreeTest, OwnedEntityTreeStepsAndRenders) {
  DummyAddingEntity* entity = this->create_entity();
  ASSERT_NE(entity, nullptr);
  DummyNode* child = this->add_branch(*entity);
  DummyNode* grandchild = dynamic_cast<DummyNode*>(entity->find_node(2));
  ASSERT_NE(grandchild, nullptr);

  this->tick();

  EXPECT_GT(entity->step_count, 0);
  EXPECT_GT(child->step_count, 0);
  EXPECT_GT(grandchild->step_count, 0);
  EXPECT_GT(entity->render_count, 0);
  EXPECT_GT(child->render_count, 0);
  EXPECT_GT(grandchild->render_count, 0);
}

TEST_F(ObjectTreeTest, NodeCreatedDuringStepJoinsTheTreeSafely) {
  DummyAddingEntity* entity = this->create_entity();
  ASSERT_NE(entity, nullptr);
  entity->add_child_on_step = true;

  this->tick();
  DummyNode* child = dynamic_cast<DummyNode*>(entity->find_node(1));
  ASSERT_NE(child, nullptr);
  EXPECT_GT(child->render_count, 0);

  this->tick();
  EXPECT_GT(child->step_count, 0);
}

TEST_F(ObjectTreeTest, NodeCreatedDuringRenderJoinsTheTreeSafely) {
  DummyAddingEntity* entity = this->create_entity();
  ASSERT_NE(entity, nullptr);
  entity->add_child_on_render = true;

  this->tick();
  DummyNode* child = dynamic_cast<DummyNode*>(entity->find_node(1));
  ASSERT_NE(child, nullptr);

  this->tick();
  EXPECT_GT(child->step_count, 0);
  EXPECT_GT(child->render_count, 0);
}

TEST_F(ObjectTreeTest, DeletedNodeDestroysOnlyItsSubtree) {
  DummyAddingEntity* entity = this->create_entity();
  ASSERT_NE(entity, nullptr);
  DummyNode* child = this->add_branch(*entity);

  child->deleted = true;
  this->tick();

  EXPECT_EQ(DummyNode::destructed_ids.size(), 2u);
  EXPECT_EQ(entity->find_node(1), nullptr);
  EXPECT_EQ(entity->find_node(2), nullptr);
}

TEST_F(ObjectTreeTest, DeletedEntityDestroysItsTree) {
  DummyAddingEntity* entity = this->create_entity();
  ASSERT_NE(entity, nullptr);
  this->add_branch(*entity);

  entity->deleted = true;
  this->tick();

  EXPECT_EQ(DummyEntity::destructed_count, 1);
  EXPECT_EQ(DummyNode::destructed_ids.size(), 2u);
}

TEST_F(ObjectTest, EntityRequiresACreatorPid) {
  thoom::Entity::Context context = this->local_context();
  context.creator_pid = THOOM_PID_INVALID;

  EXPECT_DEATH(DummyEntity entity(context), "invalid creator pid");
}

TEST_F(ObjectTest, EntityRejectsAnInvalidSuppliedGid) {
  thoom::Entity::Context context = this->local_context();
  context.gid = THOOM_GID_INVALID;

  EXPECT_DEATH(DummyEntity entity(context), "invalid gid");
}

TEST_F(ObjectTest, EntityRequiresAnOwnerPid) {
  thoom::Entity::Context context = this->local_context();
  context.owner_pid = THOOM_PID_INVALID;

  EXPECT_DEATH(DummyEntity entity(context), "invalid owner pid");
}

TEST_F(ObjectTest, NodeRequiresAnEntityParent) {
  DummyObject object;

  EXPECT_DEATH(DummyNode node(object, std::nullopt),
               "parent does not belong to an entity");
}

TEST_F(ObjectTest, NodeRejectsTheReservedRootId) {
  DummyEntity entity(this->local_context());

  EXPECT_DEATH(DummyNode node(entity, 0), "node id zero names the root");
}

TEST_F(ObjectTest, NodeRejectsADuplicateId) {
  DummyEntity entity(this->local_context());
  DummyNode original(entity, 1);

  EXPECT_DEATH(DummyNode duplicate(entity, 1), "repeated node id 1");
}

TEST_F(ObjectTest, NodeIdExhaustionIsReported) {
  DummyEntity entity(this->local_context());
  DummyNode last(entity, UINT32_MAX);

  EXPECT_DEATH(DummyNode exhausted(entity, std::nullopt), "no node ids remain");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
