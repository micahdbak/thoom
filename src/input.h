#pragma once

#include <SDL3/SDL.h>

#include "controller.h"
#include "macros.h"

namespace thoom {

class Input {
 public:
  struct Binding {
    SDL_Keycode keycode = SDLK_UNKNOWN;
    const char* name = "";
  };

  inline static Input* instance = nullptr;

  static void init();
  static void shutdown();

  Input();
  ~Input();
  DISALLOW_COPY_AND_MOVE(Input);

  void handle_key_event(const SDL_Event& event);
  void clear_hit_input();

  void bind_key(Button button, SDL_Keycode keycode);

  void set_capture(bool enabled);

  Controller world;
  Controller captured;
  Controller* active = &this->world;
  bool capturing = false;

  Binding bindings[NUM_BUTTONS];

 private:
  Button button_for_keycode(SDL_Keycode keycode) const;
};

};  // namespace thoom
