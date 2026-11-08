#pragma once

#include <SDL3/SDL.h>

namespace thoom {

enum Button {
  MENU = 0,
  UP,
  RIGHT,
  DOWN,
  LEFT,
  SELECT,
  CANCEL,
  TOSS,
  ATTACK,
  DANCE,
  DIGIT,
  NUM_BUTTONS,
  NULL_BUTTON
};

class Controller {
 public:
  Controller() = default;
  ~Controller() = default;

  void handle_key_press(SDL_Keycode keycode);

  void press(Button button);
  void release(Button button);
  bool consume_hit(Button button);

  int held_dx() const;
  int held_dy() const;
  int consume_hit_dx();
  int consume_hit_dy();

  bool cheat_code(int a, int b, int c, int d) const;

  void clear_hit_input();
  void clear_input();

  bool down[NUM_BUTTONS] = {false};

  char character = '\0';
  int digit = -1;
  SDL_Keycode last_keycode = SDLK_UNKNOWN;

 private:
  bool hit[NUM_BUTTONS] = {false};

  int digits[4] = {0};
  int digit_count = 0;
  Uint64 digit_ticks = 0;
};

};  // namespace thoom
