#include "webrtc/webrtc.h"

#include <rtc/rtc.h>

#include <mutex>

namespace thoom {

// for testing; only the first user can init, and last quit
static std::mutex init_mutex;
static int users = 0;

void webrtc_init() {
  std::lock_guard<std::mutex> guard(init_mutex);

  if (users == 0) {
    rtcPreload();
  }

  users++;
}

void webrtc_quit() {
  std::lock_guard<std::mutex> guard(init_mutex);

  if (users == 0) {
    return;
  }

  users--;

  if (users == 0) {
    rtcCleanup();
  }
}

};  // namespace thoom
