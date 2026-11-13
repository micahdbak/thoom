#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "SDL3/SDL.h"
#include "object/area.h"
#include "object/entity.h"
#include "object/object.h"
#include "scene/scene.h"
#include "switch/peer_identity.h"
#include "thoom/thoom.h"

class TestObject : public thoom::Object {
 public:
  TestObject() : thoom::Object(nullptr) {}

  void step() override { this->step_count++; }
  void render() override { this->render_count++; }

  int step_count = 0;
  int render_count = 0;
};

class TestNode : public thoom::Entity::Node {
 public:
  TestNode(thoom::Object& parent, std::optional<thoom_nid> nid)
      : thoom::Entity::Node(parent, nid) {}

  void step() override {}

  void receive_event(const thoom::Entity::Event&) override {
    this->event_count++;
  }

  int event_count = 0;
};

class TestEntity : public thoom::Entity {
 public:
  explicit TestEntity(const thoom::Entity::Context& context)
      : thoom::Entity(context) {
    TestEntity::constructor_creator_pid = context.creator_pid;
    TestEntity::constructor_received_gid = context.gid;
    TestEntity::constructor_gid = this->gid;
    TestEntity::constructor_owner_pid = this->owner_pid;
  }

  void step() override {
    this->step_count++;
    if (this->event_count > 0) {
      this->event_seen_in_step = true;
    }
  }

  void render() override { this->render_count++; }

  void receive_event(const thoom::Entity::Event& event) override {
    this->event_count++;
    this->last_kind = event.kind;
    this->last_payload = event.payload;
  }

  int step_count = 0;
  int render_count = 0;
  int event_count = 0;
  uint32_t last_kind = 0;
  std::vector<uint8_t> last_payload;
  bool event_seen_in_step = false;

  inline static thoom_pid constructor_creator_pid = THOOM_PID_INVALID;
  inline static std::optional<thoom_gid> constructor_received_gid;
  inline static thoom_gid constructor_gid = THOOM_GID_INVALID;
  inline static thoom_pid constructor_owner_pid = THOOM_PID_INVALID;
};

static TestObject* last_created_object = nullptr;
static TestEntity* last_created_entity = nullptr;
static TestObject* nested_parent = nullptr;
static TestObject* nested_child = nullptr;
static std::vector<std::string> nested_creation_order;
static std::vector<thoom_gid> replication_creation_order;

thoom::Object* create_test_object(const std::string&) {
  last_created_object = new TestObject();
  return last_created_object;
}

thoom::Object* create_nested_parent(const std::string& options) {
  nested_creation_order.push_back("parent");
  nested_parent = static_cast<TestObject*>(create_test_object(options));
  thoom::Scene::instance->create_object("test_nested_child", "");
  return nested_parent;
}

thoom::Object* create_nested_child(const std::string& options) {
  nested_creation_order.push_back("child");
  nested_child = static_cast<TestObject*>(create_test_object(options));
  return nested_child;
}

thoom::Entity* create_test_entity(const thoom::Entity::Context& context,
                                  const std::string& options) {
  TestEntity* entity = new TestEntity(context);
  last_created_entity = entity;
  replication_creation_order.push_back(entity->gid);

  if (options == "replicate") {
    thoom::EntityRecord record = {"test_entity", "", THOOM_GID_COMPOSE(5, 100),
                                  5, thoom::Entity::Ownership::kFixed};
    thoom::Scene::instance->replicate_entity(record);
  }

  return entity;
}

static const thoom::ObjectDef kObjectDefs[] = {
    {"test_object", create_test_object, thoom::kObjectNormal},
    {"test_nested_parent", create_nested_parent, thoom::kObjectNormal},
    {"test_nested_child", create_nested_child, thoom::kObjectNormal},
    {nullptr, nullptr, thoom::kObjectNormal},
};

static const thoom::EntityDef kEntityDefs[] = {
    {"test_entity",
     create_test_entity,
     {thoom::Entity::Ownership::kFixed, THOOM_PID_INVALID},
     thoom::kObjectNormal},
    {"test_round_robin",
     create_test_entity,
     {thoom::Entity::Ownership::kRoundRobin, THOOM_PID_INVALID},
     thoom::kObjectNormal},
    {nullptr, nullptr, {}, thoom::kObjectNormal},
};

class SceneTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));

    ASSERT_TRUE(thoom_init("thoom scene test", 320, 240, 0, nullptr));
    ASSERT_TRUE(thoom::Scene::init("", true));

    thoom::Scene::instance->register_objects(kObjectDefs);
    thoom::Scene::instance->register_entities(kEntityDefs);

    last_created_object = nullptr;
    last_created_entity = nullptr;
    nested_parent = nullptr;
    nested_child = nullptr;
    nested_creation_order.clear();
    replication_creation_order.clear();
  }

  void TearDown() override {
    thoom::Scene::quit();
    thoom_quit();

    last_created_object = nullptr;
    last_created_entity = nullptr;
  }

  TestObject* create_object(const char* id, const std::string& options = "") {
    last_created_object = nullptr;
    thoom::Scene::instance->create_object(id, options);
    return last_created_object;
  }

  TestEntity* create_entity(const char* id, const std::string& options = "") {
    last_created_entity = nullptr;
    thoom::Scene::instance->create_entity(
        id, options, {thoom::Entity::Ownership::kFixed, THOOM_PID_INVALID});
    return last_created_entity;
  }

  TestEntity* create_entity_with_policy(
      const char* id, const thoom::EntityOptions& entity_options,
      const std::string& options = "") {
    last_created_entity = nullptr;
    thoom::Scene::instance->create_entity(id, options, entity_options);
    return last_created_entity;
  }

  TestEntity* replicate_entity(
      thoom_gid gid, thoom_pid owner_pid,
      thoom::Entity::Ownership ownership = thoom::Entity::Ownership::kFixed,
      const char* id = "test_entity", const std::string& options = "") {
    last_created_entity = nullptr;
    thoom::EntityRecord record = {id, options, gid, owner_pid, ownership};
    thoom::Scene::instance->replicate_entity(record);
    return last_created_entity;
  }

  void tick() {
    SDL_Delay(25);
    thoom::Scene::instance->step();
  }
};

static void become_host() {
  thoom::PeerIdentity::instance->set_current(THOOM_PID_HOST);
}

TEST_F(SceneTest, OfflineEngineHasNoAuthority) {
  EXPECT_EQ(thoom::PeerIdentity::instance->get_current(), THOOM_PID_INVALID);
}

TEST_F(SceneTest, GidCompositionRoundTrips) {
  EXPECT_EQ(THOOM_GID_COMPOSE(THOOM_PID_HOST, 0), 0u);
  EXPECT_EQ(THOOM_GID_PID(0), THOOM_PID_HOST);
  EXPECT_EQ(THOOM_GID_LID(0), 0u);
  EXPECT_NE(THOOM_GID_COMPOSE(THOOM_PID_HOST, 0), THOOM_GID_INVALID);

  const thoom_gid gid = THOOM_GID_COMPOSE(7, 42);
  EXPECT_EQ(THOOM_GID_PID(gid), 7u);
  EXPECT_EQ(THOOM_GID_LID(gid), 42u);

  EXPECT_EQ(THOOM_GID_PID(THOOM_GID_INVALID), THOOM_PID_INVALID);
  EXPECT_EQ(THOOM_GID_LID(THOOM_GID_INVALID), THOOM_LID_MAX);
}

TEST_F(SceneTest, LocalObjectCreationWorksOffline) {
  TestObject* object = this->create_object("test_object");
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(thoom::Scene::instance->find_object(object->lid), object);
  EXPECT_EQ(thoom::Scene::instance->find_entity(THOOM_GID_INVALID), nullptr);
}

TEST_F(SceneTest, NestedIdleCreationAdmitsParentBeforeChild) {
  thoom::Scene::instance->create_object("test_nested_parent", "");

  ASSERT_NE(nested_parent, nullptr);
  ASSERT_NE(nested_child, nullptr);
  EXPECT_EQ(nested_creation_order,
            std::vector<std::string>({"parent", "child"}));
  EXPECT_LT(nested_parent->lid, nested_child->lid);
  EXPECT_EQ(thoom::Scene::instance->find_object(nested_parent->lid),
            nested_parent);
  EXPECT_EQ(thoom::Scene::instance->find_object(nested_child->lid),
            nested_child);
}

TEST_F(SceneTest, EntityCreationNeedsAPid) {
  EXPECT_EQ(this->create_entity("test_entity"), nullptr);
  EXPECT_NE(this->create_object("test_object"), nullptr);
}

TEST_F(SceneTest, ObjectCreationRejectsAnEntityId) {
  become_host();
  EXPECT_EQ(this->create_object("test_entity"), nullptr);
  EXPECT_EQ(last_created_entity, nullptr);
}

TEST_F(SceneTest, EntityCreationRejectsAnObjectId) {
  become_host();
  EXPECT_EQ(this->create_entity("test_object"), nullptr);
  EXPECT_EQ(last_created_object, nullptr);
}

TEST_F(SceneTest, LocalEntityIdentityIsVisibleInTheConstructor) {
  become_host();
  TestEntity* entity = this->create_entity("test_entity");
  ASSERT_NE(entity, nullptr);

  EXPECT_FALSE(TestEntity::constructor_received_gid.has_value());
  EXPECT_EQ(TestEntity::constructor_creator_pid, THOOM_PID_HOST);
  EXPECT_EQ(TestEntity::constructor_gid,
            THOOM_GID_COMPOSE(THOOM_PID_HOST, entity->lid));
  EXPECT_EQ(TestEntity::constructor_owner_pid, THOOM_PID_HOST);
  EXPECT_EQ(entity->gid, TestEntity::constructor_gid);
  EXPECT_EQ(thoom::Scene::instance->find_entity(entity->gid), entity);
}

TEST_F(SceneTest, EachEntityTakesTheNextLid) {
  become_host();
  TestEntity* first = this->create_entity("test_entity");
  ASSERT_NE(first, nullptr);
  TestEntity* second = this->create_entity("test_entity");
  ASSERT_NE(second, nullptr);

  EXPECT_EQ(second->lid, first->lid + 1);
  EXPECT_NE(second->gid, first->gid);
}

TEST_F(SceneTest, ReceivedEntityPreservesItsExactIdentity) {
  thoom::PeerIdentity::instance->set_current(7);
  const thoom_gid gid = THOOM_GID_COMPOSE(3, THOOM_LID_MAX - 1);
  TestEntity* entity = this->replicate_entity(gid, 3);
  ASSERT_NE(entity, nullptr);

  EXPECT_EQ(TestEntity::constructor_creator_pid, 3u);
  ASSERT_TRUE(TestEntity::constructor_received_gid.has_value());
  EXPECT_EQ(TestEntity::constructor_received_gid.value(), gid);
  EXPECT_EQ(entity->gid, gid);
  EXPECT_EQ(entity->owner_pid, 3u);
  EXPECT_NE(entity->lid, THOOM_GID_LID(gid));
}

TEST_F(SceneTest, GidZeroIsAValidReceivedId) {
  thoom::PeerIdentity::instance->set_current(7);
  TestEntity* zero = this->replicate_entity(0, THOOM_PID_HOST);
  ASSERT_NE(zero, nullptr);

  EXPECT_EQ(thoom::Scene::instance->find_entity(0), zero);
}

TEST_F(SceneTest, DuplicateReceivedGidIsRefused) {
  become_host();
  const thoom_gid gid = THOOM_GID_COMPOSE(4, 99);
  TestEntity* original = this->replicate_entity(gid, 4);
  ASSERT_NE(original, nullptr);

  EXPECT_EQ(this->replicate_entity(gid, 4), nullptr);
  EXPECT_EQ(thoom::Scene::instance->find_entity(gid), original);
}

TEST_F(SceneTest, ReplicationProcessesNestedRequestsInOrder) {
  const thoom_gid parent_gid = THOOM_GID_COMPOSE(4, 100);
  const thoom_gid child_gid = THOOM_GID_COMPOSE(5, 100);
  thoom::EntityRecord record = {"test_entity", "replicate", parent_gid, 4,
                                thoom::Entity::Ownership::kFixed};
  thoom::Scene::instance->replicate_entity(record);

  thoom::Entity* parent = thoom::Scene::instance->find_entity(parent_gid);
  thoom::Entity* child = thoom::Scene::instance->find_entity(child_gid);
  ASSERT_NE(parent, nullptr);
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(replication_creation_order,
            std::vector<thoom_gid>({parent_gid, child_gid}));
  EXPECT_LT(parent->lid, child->lid);
}

TEST_F(SceneTest, InvalidReceivedIdentityIsRefused) {
  EXPECT_EQ(this->replicate_entity(THOOM_GID_INVALID, THOOM_PID_HOST), nullptr);
  EXPECT_EQ(this->replicate_entity(THOOM_GID_COMPOSE(1, 1), THOOM_PID_INVALID),
            nullptr);
}

TEST_F(SceneTest, FixedOwnershipDefaultsToTheCurrentPeer) {
  become_host();
  TestEntity* entity = this->create_entity("test_entity");
  ASSERT_NE(entity, nullptr);

  EXPECT_EQ(entity->ownership, thoom::Entity::Ownership::kFixed);
  EXPECT_EQ(entity->owner_pid, THOOM_PID_HOST);
}

TEST_F(SceneTest, FixedOwnershipAcceptsAnExplicitPeer) {
  thoom::PeerIdentity::instance->set_current(7);
  TestEntity* entity = this->create_entity_with_policy(
      "test_entity", {thoom::Entity::Ownership::kFixed, 3});
  ASSERT_NE(entity, nullptr);

  EXPECT_EQ(entity->gid, THOOM_GID_COMPOSE(7, entity->lid));
  EXPECT_EQ(entity->owner_pid, 3u);
  EXPECT_EQ(entity->ownership, thoom::Entity::Ownership::kFixed);
}

TEST_F(SceneTest, RoundRobinUsesCurrentOwnerUntilAssignmentIsCoordinated) {
  become_host();
  TestEntity* entity = this->create_entity_with_policy(
      "test_round_robin",
      {thoom::Entity::Ownership::kRoundRobin, THOOM_PID_INVALID});
  ASSERT_NE(entity, nullptr);

  EXPECT_EQ(entity->ownership, thoom::Entity::Ownership::kRoundRobin);
  EXPECT_EQ(entity->owner_pid, THOOM_PID_HOST);
}

TEST_F(SceneTest, RoundRobinRequestRejectsAnExplicitPeer) {
  become_host();
  EXPECT_EQ(this->create_entity_with_policy(
                "test_round_robin", {thoom::Entity::Ownership::kRoundRobin, 3}),
            nullptr);
}

TEST_F(SceneTest, ReceivedRecordUsesExactOwnership) {
  thoom::PeerIdentity::instance->set_current(7);
  TestEntity* entity = this->replicate_entity(
      THOOM_GID_COMPOSE(2, 10), 4, thoom::Entity::Ownership::kRoundRobin);
  ASSERT_NE(entity, nullptr);

  EXPECT_EQ(entity->owner_pid, 4u);
  EXPECT_EQ(entity->ownership, thoom::Entity::Ownership::kRoundRobin);
}

TEST_F(SceneTest, FindNodeResolvesRootsAndNodesByKey) {
  become_host();
  TestEntity* entity = this->create_entity("test_entity");
  ASSERT_NE(entity, nullptr);
  TestNode* node = new TestNode(*entity, 7);

  EXPECT_EQ(entity->key(),
            (thoom::Entity::NodeKey{entity->gid, thoom::Entity::kRootNid}));
  EXPECT_EQ(node->key(), (thoom::Entity::NodeKey{entity->gid, 7}));
  EXPECT_EQ(thoom::Scene::instance->find_node(entity->key()), entity);
  EXPECT_EQ(thoom::Scene::instance->find_node(node->key()), node);
  EXPECT_EQ(thoom::Scene::instance->find_node({entity->gid, 8}), nullptr);
  EXPECT_EQ(thoom::Scene::instance->find_node({THOOM_GID_INVALID, 0}), nullptr);

  node->deleted = true;
  EXPECT_EQ(thoom::Scene::instance->find_node(node->key()), nullptr);
}

TEST_F(SceneTest, FindEntitiesFiltersByAuthorityAndSortsByGid) {
  thoom::PeerIdentity::instance->set_current(7);
  TestEntity* remote = this->create_entity_with_policy(
      "test_entity", {thoom::Entity::Ownership::kFixed, THOOM_PID_HOST});
  TestEntity* local = this->create_entity_with_policy(
      "test_entity", {thoom::Entity::Ownership::kFixed, 7});
  ASSERT_NE(remote, nullptr);
  ASSERT_NE(local, nullptr);
  ASSERT_NE(this->create_object("test_object"), nullptr);

  EXPECT_EQ(thoom::Scene::instance->find_entities(
                thoom::Entity::AuthorityScope::kAll),
            std::vector<thoom::Entity*>({remote, local}));
  EXPECT_EQ(thoom::Scene::instance->find_entities(
                thoom::Entity::AuthorityScope::kLocal),
            std::vector<thoom::Entity*>({local}));
  EXPECT_EQ(thoom::Scene::instance->find_entities(
                thoom::Entity::AuthorityScope::kRemote),
            std::vector<thoom::Entity*>({remote}));
}

TEST_F(SceneTest, FindAreasFiltersGroupsAuthorityAndSortsByKey) {
  thoom::PeerIdentity::instance->set_current(7);
  TestEntity* remote = this->create_entity_with_policy(
      "test_entity", {thoom::Entity::Ownership::kFixed, THOOM_PID_HOST});
  TestEntity* local = this->create_entity_with_policy(
      "test_entity", {thoom::Entity::Ownership::kFixed, 7});
  ASSERT_NE(remote, nullptr);
  ASSERT_NE(local, nullptr);

  thoom::Area* remote_area =
      new thoom::Area(*remote, 1, SDL_FRect{0.0f, 0.0f, 16.0f, 16.0f}, 1);
  thoom::Area* local_area =
      new thoom::Area(*local, 2, SDL_FRect{0.0f, 0.0f, 16.0f, 16.0f}, 2);
  thoom::Area* first_local_area =
      new thoom::Area(*local, 1, SDL_FRect{0.0f, 0.0f, 16.0f, 16.0f}, 2);
  thoom::Area* disabled =
      new thoom::Area(*local, 3, SDL_FRect{0.0f, 0.0f, 16.0f, 16.0f}, 2);
  thoom::Area* outside =
      new thoom::Area(*local, 4, SDL_FRect{32.0f, 32.0f, 16.0f, 16.0f}, 2);
  disabled->enabled = false;

  thoom::AreaQuery query;
  query.bounds = SDL_FRect{0.0f, 0.0f, 8.0f, 8.0f};
  query.groups = 2;
  query.authority = thoom::Entity::AuthorityScope::kLocal;

  EXPECT_EQ(thoom::Scene::instance->find_areas(query),
            std::vector<thoom::Area*>({first_local_area, local_area}));

  query.groups = 1;
  query.authority = thoom::Entity::AuthorityScope::kAll;
  EXPECT_EQ(thoom::Scene::instance->find_areas(query),
            std::vector<thoom::Area*>({remote_area}));

  EXPECT_NE(disabled, nullptr);
  EXPECT_NE(outside, nullptr);
}

TEST_F(SceneTest, QueuedEventAppliesBeforeStep) {
  become_host();
  TestEntity* entity = this->create_entity("test_entity");
  ASSERT_NE(entity, nullptr);

  thoom::Entity::Event event;
  event.target = entity->key();
  event.kind = 9;
  event.payload = {1, 2, 3};
  thoom::Scene::instance->queue_event(event);

  EXPECT_EQ(entity->event_count, 0);
  this->tick();
  EXPECT_EQ(entity->event_count, 1);
  EXPECT_EQ(entity->last_kind, 9u);
  EXPECT_EQ(entity->last_payload, std::vector<uint8_t>({1, 2, 3}));
  EXPECT_TRUE(entity->event_seen_in_step);
}

TEST_F(SceneTest, EventTargetsNodesAndDropsForNonOwners) {
  thoom::PeerIdentity::instance->set_current(7);
  TestEntity* remote = this->create_entity_with_policy(
      "test_entity", {thoom::Entity::Ownership::kFixed, THOOM_PID_HOST});
  TestEntity* local = this->create_entity_with_policy(
      "test_entity", {thoom::Entity::Ownership::kFixed, 7});
  ASSERT_NE(remote, nullptr);
  ASSERT_NE(local, nullptr);
  TestNode* node = new TestNode(*local, 1);

  thoom::Entity::Event remote_event;
  remote_event.target = remote->key();
  thoom::Scene::instance->queue_event(remote_event);

  thoom::Entity::Event node_event;
  node_event.target = node->key();
  thoom::Scene::instance->queue_event(node_event);

  this->tick();
  EXPECT_EQ(remote->event_count, 0);
  EXPECT_EQ(node->event_count, 1);
}

TEST_F(SceneTest, NonOwnedEntityOnlyRenders) {
  thoom::PeerIdentity::instance->set_current(7);
  TestEntity* entity = this->replicate_entity(THOOM_GID_COMPOSE(3, 50), 3);
  ASSERT_NE(entity, nullptr);

  this->tick();

  EXPECT_EQ(entity->step_count, 0);
  EXPECT_GT(entity->render_count, 0);
}

TEST_F(SceneTest, OwnedEntityStepsAndRenders) {
  become_host();
  TestEntity* entity = this->create_entity("test_entity");
  ASSERT_NE(entity, nullptr);

  this->tick();

  EXPECT_GT(entity->step_count, 0);
  EXPECT_GT(entity->render_count, 0);
}

TEST_F(SceneTest, DeletedEntityIsUnregistered) {
  become_host();
  TestEntity* entity = this->create_entity("test_entity");
  ASSERT_NE(entity, nullptr);
  const thoom_gid gid = entity->gid;
  const thoom_lid lid = entity->lid;

  entity->deleted = true;
  this->tick();

  EXPECT_EQ(thoom::Scene::instance->find_entity(gid), nullptr);
  EXPECT_EQ(thoom::Scene::instance->find_object(lid), nullptr);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
