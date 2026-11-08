#include "input/controller.h"

#include <SDL3/SDL.h>

namespace thoom {

void Controller::handle_key_press(SDL_Keycode keycode) {
  this->last_keycode = keycode;

  if (keycode >= ' ' && keycode <= '~') {
    this->character = char(keycode);
  }

  if (keycode < '0' || keycode > '9') {
    return;
  }

  // A digit sequence expires one second after its first digit.
  Uint64 ticks = SDL_GetTicks();
  if (ticks - this->digit_ticks > 1000) {
    this->digit_count = 0;
    this->digit_ticks = ticks;
  }

  this->digit = int(keycode - '0');

  if (this->digit_count == 4) {
    for (int i = 0; i < 3; i++) {
      this->digits[i] = this->digits[i + 1];
    }
    this->digit_count = 3;
  }

  this->digits[this->digit_count++] = this->digit;
}

void Controller::press(Button button) {
  this->hit[button] = true;
  this->down[button] = true;
}

void Controller::release(Button button) { this->down[button] = false; }

bool Controller::consume_hit(Button button) {
  if (!this->hit[button]) {
    return false;
  }

  this->hit[button] = false;
  return true;
}

int Controller::held_dx() const {
  return int(this->down[Button::RIGHT]) - int(this->down[Button::LEFT]);
}

int Controller::held_dy() const {
  return int(this->down[Button::DOWN]) - int(this->down[Button::UP]);
}

int Controller::consume_hit_dx() {
  return int(this->consume_hit(Button::RIGHT)) -
         int(this->consume_hit(Button::LEFT));
}

int Controller::consume_hit_dy() {
  return int(this->consume_hit(Button::DOWN)) -
         int(this->consume_hit(Button::UP));
}

bool Controller::cheat_code(int a, int b, int c, int d) const {
  return this->digit_count == 4 && this->digits[0] == a &&
         this->digits[1] == b && this->digits[2] == c && this->digits[3] == d;
}

void Controller::clear_hit_input() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    this->hit[i] = false;
  }

  this->character = '\0';
  this->digit = -1;
  this->last_keycode = SDLK_UNKNOWN;
}

void Controller::clear_input() {
  this->clear_hit_input();

  for (int i = 0; i < NUM_BUTTONS; i++) {
    this->down[i] = false;
  }

  this->digit_count = 0;
  this->digit_ticks = 0;
}

};  // namespace thoom
