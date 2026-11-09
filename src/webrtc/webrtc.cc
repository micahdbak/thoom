#include "webrtc/webrtc.h"

#include <rtc/rtc.h>

namespace thoom {

void webrtc_init() { rtcPreload(); }

void webrtc_quit() { rtcCleanup(); }

};  // namespace thoom
