#ifndef THOOM_THOOM_H
#define THOOM_THOOM_H

#ifdef __cplusplus
extern "C" {
#endif

struct hud_def {
  void (*draw)(void* user);
  bool (*show_peer)(void* user);
  bool (*show_hud)(void* user);
  void (*exit_to_main_menu)(void* user);
  void* user;
};

bool thoom_init(const char* title, unsigned int screen_width,
                unsigned int screen_height, bool peer, bool offerer,
                unsigned long background, const struct hud_def* def);

void thoom_poll_events(bool* quit);

void thoom_quit(void);

#ifdef __cplusplus
}
#endif

#endif  // THOOM_THOOM_H
