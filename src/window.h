#pragma once

#include <SDL3/SDL.h>

#include "colour.h"

namespace thoom {

#define THOOM_SCREEN_WIDTH 320
#define THOOM_SCREEN_HEIGHT 240

class Window {
 public:
  inline static Window* instance = nullptr;

  static bool init(const char* title, int screen_width, int screen_height);
  static void quit();

  Window(const char* title, int screen_width, int screen_height);
  ~Window();

  void set_title(const char* title);
  void update_screen_scale();
  void clear_screen(Colour colour);
  void save_screenshot(const char* file_path);
  void present(Colour letterbox_colour);

  SDL_Surface* read_pixels(SDL_Texture* texture);

  SDL_Window* sdl_window = nullptr;
  SDL_Renderer* sdl_renderer = nullptr;
  SDL_Texture* screen = nullptr;
  int screen_width = 0, screen_height = 0;
  SDL_FRect screen_rect = {};

 private:
  bool ready = false;
  bool sdl_initialized = false;
};

};  // namespace thoom
