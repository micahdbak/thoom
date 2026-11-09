#include <gtest/gtest.h>

#include "SDL3/SDL.h"
#include "thoom/thoom.h"

TEST(Thoom, InitAndQuit) {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));

  ASSERT_TRUE(thoom_init("thoom test", 320, 240, false, false, 0, nullptr));
  thoom_quit();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
