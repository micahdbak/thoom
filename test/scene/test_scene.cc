#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "SDL3/SDL.h"
#include "object.h"
#include "scene/scene.h"
#include "thoom/thoom.h"

namespace {

class TestObject : public thoom::Object {
 public:
  void step() override {}
};

TestObject* last_created = nullptr;

thoom::Object* create_test_object(thoom::Scene*, const std::string&) {
  last_created = new TestObject();
  return last_created;
}

const thoom::Scene::ObjectDef kLocalObjects[] = {
    {"test_local", create_test_object, false, thoom::kObjectNormal},
    {nullptr, nullptr, false, thoom::kObjectNormal},
};

const thoom::Scene::ObjectDef kReplicatedObjects[] = {
    {"test_replicated", create_test_object, true, thoom::kObjectNormal},
    {nullptr, nullptr, false, thoom::kObjectNormal},
};

class SceneTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));

    ASSERT_TRUE(
        thoom_init("thoom scene test", 320, 240, false, false, 0, nullptr));
    ASSERT_TRUE(thoom::Scene::init());

    thoom::Scene::instance->register_objects(kLocalObjects);
    thoom::Scene::instance->register_objects(kReplicatedObjects);

    last_created = nullptr;
  }

  void TearDown() override {
    thoom::Scene::quit();
    thoom_quit();

    last_created = nullptr;
  }

  TestObject* spawn(const char* id) {
    thoom::Scene::instance->create_object(id, "");
    return last_created;
  }
};

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

TEST_F(SceneTest, ReplicatedSpawnTakesGidFromPidAndLid) {
  TestObject* object = this->spawn("test_replicated");
  ASSERT_NE(object, nullptr);

  EXPECT_TRUE(object->replicated);
  EXPECT_EQ(object->owner_pid, thoom::Scene::instance->local_pid);
  EXPECT_EQ(object->gid,
            THOOM_GID_COMPOSE(thoom::Scene::instance->local_pid, object->lid));
  EXPECT_NE(object->gid, THOOM_GID_INVALID);
  EXPECT_EQ(thoom::Scene::instance->find_gid_object(object->gid), object);
}

TEST_F(SceneTest, LocalSpawnHasNoNetworkIdentity) {
  TestObject* object = this->spawn("test_local");
  ASSERT_NE(object, nullptr);

  EXPECT_FALSE(object->replicated);
  EXPECT_EQ(object->gid, THOOM_GID_INVALID);
  EXPECT_EQ(object->owner_pid, THOOM_PID_INVALID);
  EXPECT_EQ(thoom::Scene::instance->find_gid_object(THOOM_GID_INVALID),
            nullptr);
}

TEST_F(SceneTest, EachObjectTakesTheNextLid) {
  TestObject* first = this->spawn("test_replicated");
  ASSERT_NE(first, nullptr);

  TestObject* second = this->spawn("test_replicated");
  ASSERT_NE(second, nullptr);

  EXPECT_EQ(second->lid, first->lid + 1);
  EXPECT_NE(second->gid, first->gid);
}

TEST_F(SceneTest, GidZeroIsAValidLiveId) {
  TestObject* zero = new TestObject();
  zero->replicated = true;
  zero->gid = THOOM_GID_COMPOSE(THOOM_PID_HOST, 0);
  zero->owner_pid = THOOM_PID_HOST;

  EXPECT_TRUE(thoom::Scene::instance->register_gid_object(zero));
  EXPECT_EQ(thoom::Scene::instance->find_gid_object(0), zero);

  thoom::Scene::instance->unregister_gid_object(zero);
  EXPECT_EQ(thoom::Scene::instance->find_gid_object(0), nullptr);

  delete zero;
}

TEST_F(SceneTest, DuplicateGidIsRefused) {
  TestObject* original = this->spawn("test_replicated");
  ASSERT_NE(original, nullptr);

  TestObject* duplicate = new TestObject();
  duplicate->replicated = true;
  duplicate->gid = original->gid;
  duplicate->owner_pid = THOOM_PID_HOST;

  EXPECT_FALSE(thoom::Scene::instance->register_gid_object(duplicate));
  EXPECT_EQ(thoom::Scene::instance->find_gid_object(original->gid), original);

  delete duplicate;
}

TEST_F(SceneTest, ReplicatedSpawnNeedsAPid) {
  thoom::Scene::instance->local_pid = THOOM_PID_INVALID;

  EXPECT_EQ(this->spawn("test_replicated"), nullptr);

  TestObject* local = this->spawn("test_local");
  ASSERT_NE(local, nullptr);
  EXPECT_EQ(local->gid, THOOM_GID_INVALID);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
