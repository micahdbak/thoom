#pragma once

#include <SDL3/SDL.h>

#include "colour.h"
#include "macros.h"

namespace thoom {

class Window {
 public:
  inline static Window* instance = nullptr;

  static void init(const char* title, int screen_width, int screen_height);
  static void shutdown();

  Window(const char* title, int screen_width, int screen_height);
  ~Window();
  DISALLOW_COPY_AND_MOVE(Window);

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
};

};  // namespace thoom
