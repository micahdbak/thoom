#include <gtest/gtest.h>

#include <string>

#include "SDL3/SDL.h"
#include "absl/log/log_sink_registry.h"
#include "thoom/log.h"
#include "thoom/thoom.h"

TEST(ThoomLog, FormatsEveryLevel) {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
  ASSERT_TRUE(thoom_init("thoom log test", 320, 240, false, false, 0, nullptr));

  testing::internal::CaptureStderr();

  thoom_log_info("info %s %d", "text", 42);
  thoom_log_warn("warn %s %d", "text", 7);
  thoom_log_error("error %s %d", "text", 13);
  absl::FlushLogSinks();

  const std::string output = testing::internal::GetCapturedStderr();

  EXPECT_NE(output.find("info text 42"), std::string::npos);
  EXPECT_NE(output.find("warn text 7"), std::string::npos);
  EXPECT_NE(output.find("error text 13"), std::string::npos);

  thoom_quit();
}

TEST(ThoomLog, FatalAborts) {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
  ASSERT_TRUE(thoom_init("thoom log test", 320, 240, false, false, 0, nullptr));

  EXPECT_DEATH(thoom_log_fatal("fatal %s %d", "text", 3), "fatal text 3");

  thoom_quit();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}
