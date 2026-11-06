#include "input.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "controller.h"
#include "thoom/macros.h"

namespace thoom {

bool Input::init() {
  if (Input::instance != nullptr) {
    fprintf(stderr, THOOM_LOG "Input::init error: input already initialized\n");
    return false;
  }

  Input::instance = new Input();
  return true;
}

void Input::quit() {
  if (Input::instance == nullptr) {
    return;
  }

  delete Input::instance;
  Input::instance = nullptr;
}

Input::Input() {
  this->bindings[Button::MENU] = {SDLK_ESCAPE, "Menu"};
  this->bindings[Button::UP] = {SDLK_UP, "Up"};
  this->bindings[Button::RIGHT] = {SDLK_RIGHT, "Right"};
  this->bindings[Button::DOWN] = {SDLK_DOWN, "Down"};
  this->bindings[Button::LEFT] = {SDLK_LEFT, "Left"};
  this->bindings[Button::SELECT] = {SDLK_RETURN, "Select"};
  this->bindings[Button::CANCEL] = {SDLK_BACKSPACE, "Cancel"};
  this->bindings[Button::TOSS] = {SDLK_T, "Toss"};
  this->bindings[Button::ATTACK] = {SDLK_SPACE, "Attack"};
  this->bindings[Button::DANCE] = {SDLK_RSHIFT, "Dance"};
}

Input::~Input() = default;

void Input::handle_key_event(const SDL_Event& event) {
  if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
    return;
  }

  Button button = this->button_for_keycode(event.key.key);

  if (event.type == SDL_EVENT_KEY_UP) {
    if (button != Button::NULL_BUTTON) {
      this->active->release(button);
    }
    return;
  }

  if (event.key.repeat) {
    return;
  }

  this->active->handle_key_press(event.key.key);

  if (button != Button::NULL_BUTTON) {
    this->active->press(button);
  }
}

void Input::clear_hit_input() {
  this->world.clear_hit_input();
  this->captured.clear_hit_input();
}

void Input::bind_key(Button button, SDL_Keycode keycode) {
  if (button < 0 || button >= Button::DIGIT) {
    return;
  }

  Button owner = this->button_for_keycode(keycode);
  if (owner == button) {
    return;
  }

  SDL_Keycode previous = this->bindings[button].keycode;
  if (owner != Button::NULL_BUTTON) {
    this->bindings[owner].keycode = previous;
  }

  this->bindings[button].keycode = keycode;
}

void Input::set_capture(bool enabled) {
  if (this->capturing == enabled) {
    return;
  }

  this->capturing = enabled;
  this->active = enabled ? &this->captured : &this->world;

  // Keys held when focus moves would stay down with no key up event.
  this->world.clear_input();
  this->captured.clear_input();
}

Button Input::button_for_keycode(SDL_Keycode keycode) const {
  if (keycode >= '0' && keycode <= '9') {
    return Button::DIGIT;
  }

  // DIGIT has no binding entry; its keycodes are handled above.
  for (int i = 0; i < int(Button::DIGIT); i++) {
    if (this->bindings[i].keycode == keycode) {
      return Button(i);
    }
  }

  return Button::NULL_BUTTON;
}

};  // namespace thoom
