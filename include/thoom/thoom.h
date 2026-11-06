#ifndef THOOM_THOOM_H
#define THOOM_THOOM_H

#ifdef __cplusplus
extern "C" {
#endif

bool thoom_init(const char* title, unsigned int screen_width,
                unsigned int screen_height, bool offerer);

void thoom_poll_events(bool* quit);

void thoom_quit(void);

#ifdef __cplusplus
}
#endif

#endif  // THOOM_THOOM_H
