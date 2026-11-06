#include "window.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "colour.h"
#include "thoom/macros.h"

#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
#include <sanitizer/lsan_interface.h>
#define LSAN_DISABLE() __lsan_disable()
#define LSAN_ENABLE() __lsan_enable()
#else
#define LSAN_DISABLE()
#define LSAN_ENABLE()
#endif

namespace thoom {

bool Window::init(const char* title, int screen_width, int screen_height) {
  if (Window::instance != nullptr) {
    fprintf(stderr,
            THOOM_LOG "Window::init error: window already initialized\n");
    return false;
  }

  Window* window = new Window(title, screen_width, screen_height);

  if (!window->ready) {
    delete window;
    return false;
  }

  Window::instance = window;
  return true;
}

void Window::quit() {
  if (Window::instance == nullptr) {
    return;
  }

  delete Window::instance;
  Window::instance = nullptr;
}

Window::Window(const char* title, int screen_width, int screen_height)
    : screen_width(screen_width), screen_height(screen_height) {
  bool sdl_init;

  LSAN_DISABLE();
  sdl_init = SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
  LSAN_ENABLE();

  if (!sdl_init) {
    fprintf(stderr, THOOM_LOG "SDL_Init error: %s\n", SDL_GetError());
    return;
  }

  this->sdl_initialized = true;

  int window_width = screen_width * 2;
  int window_height = screen_height * 2;

  if (!SDL_CreateWindowAndRenderer(title, window_width, window_height,
                                   SDL_WINDOW_RESIZABLE, &this->sdl_window,
                                   &this->sdl_renderer)) {
    fprintf(stderr, THOOM_LOG "SDL_CreateWindowAndRenderer error: %s\n",
            SDL_GetError());
    return;
  }

  printf(THOOM_LOG "Renderer backend: %s\n",
         SDL_GetRendererName(this->sdl_renderer));

  SDL_SetRenderVSync(this->sdl_renderer, 1);

  this->screen =
      SDL_CreateTexture(this->sdl_renderer, SDL_PIXELFORMAT_RGB24,
                        SDL_TEXTUREACCESS_TARGET, screen_width, screen_height);

  if (this->screen == nullptr) {
    fprintf(stderr, THOOM_LOG "SDL_CreateTexture error: %s\n", SDL_GetError());
    return;
  }

  SDL_SetTextureScaleMode(this->screen, SDL_SCALEMODE_NEAREST);

  this->update_screen_scale();
  this->ready = true;
}

Window::~Window() {
  if (this->screen != nullptr) {
    SDL_DestroyTexture(this->screen);
    this->screen = nullptr;
  }

  if (this->sdl_renderer != nullptr) {
    SDL_DestroyRenderer(this->sdl_renderer);
    this->sdl_renderer = nullptr;
  }

  if (this->sdl_window != nullptr) {
    SDL_DestroyWindow(this->sdl_window);
    this->sdl_window = nullptr;
  }

  if (this->sdl_initialized) {
    SDL_Quit();
    this->sdl_initialized = false;
  }
}

void Window::set_title(const char* title) {
  SDL_SetWindowTitle(this->sdl_window, title);
}

void Window::update_screen_scale() {
  int window_width, window_height;
  SDL_GetWindowSizeInPixels(this->sdl_window, &window_width, &window_height);

  float wtoh = float(this->screen_width) / float(this->screen_height);
  float htow = float(this->screen_height) / float(this->screen_width);
  float aspect_ratio = float(window_width) / float(window_height);

  if (aspect_ratio > wtoh) {
    this->screen_rect.w = wtoh * float(window_height);
    this->screen_rect.h = float(window_height);
    this->screen_rect.x = 0.5f * (float(window_width) - this->screen_rect.w);
    this->screen_rect.y = 0.0f;
  } else {
    this->screen_rect.w = float(window_width);
    this->screen_rect.h = htow * float(window_width);
    this->screen_rect.x = 0.0f;
    this->screen_rect.y = 0.5f * (float(window_height) - this->screen_rect.h);
  }
}

void Window::clear_screen(Colour colour) {
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
  SDL_SetRenderDrawColor(this->sdl_renderer, colour.r, colour.g, colour.b,
                         colour.a);
  SDL_RenderClear(this->sdl_renderer);
}

void Window::save_screenshot(const char* file_path) {
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);

  SDL_Surface* screenshot = SDL_RenderReadPixels(this->sdl_renderer, NULL);
  if (screenshot == nullptr) {
    fprintf(stderr, THOOM_LOG "Window::save_screenshot error: %s\n",
            SDL_GetError());
    return;
  }

  if (!SDL_SaveBMP(screenshot, file_path)) {
    fprintf(stderr, THOOM_LOG "Window::save_screenshot error: %s\n",
            SDL_GetError());
  }

  SDL_DestroySurface(screenshot);
}

void Window::present(Colour letterbox_colour) {
  SDL_SetRenderTarget(this->sdl_renderer, NULL);
  SDL_SetRenderDrawColor(this->sdl_renderer, letterbox_colour.r,
                         letterbox_colour.g, letterbox_colour.b,
                         letterbox_colour.a);
  SDL_RenderClear(this->sdl_renderer);
  SDL_RenderTexture(this->sdl_renderer, this->screen, NULL, &this->screen_rect);
  SDL_RenderPresent(this->sdl_renderer);
}

SDL_Surface* Window::read_pixels(SDL_Texture* texture) {
  if (texture == nullptr) {
    // TODO(micahdbak): warning
    return nullptr;
  }

  SDL_SetRenderTarget(this->sdl_renderer, texture);
  SDL_Surface* surface = SDL_RenderReadPixels(this->sdl_renderer, NULL);
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);

  if (surface == nullptr) {
    fprintf(stderr, THOOM_LOG "Window::read_pixels error: %s\n",
            SDL_GetError());
  }

  return surface;
}

};  // namespace thoom
