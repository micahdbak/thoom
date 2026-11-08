#ifndef THOOM_THOOM_H
#define THOOM_THOOM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t thoom_lid;
typedef uint64_t thoom_gid;
typedef uint64_t thoom_pid;

#define THOOM_GID_NULL ((thoom_gid)0)
#define THOOM_PID_HOST ((thoom_pid)0)
#define THOOM_PID_INVALID (~(thoom_pid)0)

struct hud_def {
  void (*draw)(void* user);
  bool (*show_peer)(void* user);
  bool (*show_hud)(void* user);
  void (*exit_to_main_menu)(void* user);
  void* user;
};

bool thoom_init(const char* title, uint32_t screen_width,
                uint32_t screen_height, bool peer, bool offerer,
                uint32_t background, const struct hud_def* def);

void thoom_poll_events(bool* quit);

void thoom_quit(void);

#ifdef __cplusplus
}
#endif

#endif  // THOOM_THOOM_H
