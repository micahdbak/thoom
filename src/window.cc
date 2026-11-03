#include "window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <iostream>

namespace thoom {

void Window::init(const char* title, int screen_width, int screen_height) {
  if (Window::instance != nullptr) {
    std::cerr << "Window::init error: window already initialized" << std::endl;
    std::exit(1);
  }

  Window::instance = new Window(title, screen_width, screen_height);
}

void Window::shutdown() {
  if (Window::instance == nullptr) {
    return;
  }

  delete Window::instance;
  Window::instance = nullptr;
}

Window::Window(const char* title, int screen_width, int screen_height)
    : screen_width(screen_width), screen_height(screen_height) {
  if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
    std::exit(1);
  }

  int window_width = screen_width * 2;
  int window_height = screen_height * 2;

  if (!SDL_CreateWindowAndRenderer(title, window_width, window_height,
                                   SDL_WINDOW_RESIZABLE, &this->sdl_window,
                                   &this->sdl_renderer)) {
    std::cerr << "SDL_CreateWindowAndRenderer error: " << SDL_GetError()
              << std::endl;
    std::exit(1);
  }

  std::cout << "Renderer backend: " << SDL_GetRendererName(this->sdl_renderer)
            << std::endl;

  SDL_SetRenderVSync(this->sdl_renderer, 1);

  this->screen =
      SDL_CreateTexture(this->sdl_renderer, SDL_PIXELFORMAT_RGB24,
                        SDL_TEXTUREACCESS_TARGET, screen_width, screen_height);

  if (this->screen == nullptr) {
    std::cerr << "SDL_CreateTexture error: " << SDL_GetError() << std::endl;
    std::exit(1);
  }

  SDL_SetTextureScaleMode(this->screen, SDL_SCALEMODE_NEAREST);

  this->update_screen_scale();
}

Window::~Window() {
  SDL_DestroyTexture(this->screen);
  this->screen = nullptr;

  SDL_DestroyRenderer(this->sdl_renderer);
  this->sdl_renderer = nullptr;

  SDL_DestroyWindow(this->sdl_window);
  this->sdl_window = nullptr;

  SDL_Quit();
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
    std::cerr << "Window::save_screenshot error: " << SDL_GetError()
              << std::endl;
    return;
  }

  if (!SDL_SaveBMP(screenshot, file_path)) {
    std::cerr << "Window::save_screenshot error: " << SDL_GetError()
              << std::endl;
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

SDL_Texture* Window::create_texture(int w, int h, SDL_PixelFormat format,
                                    SDL_ScaleMode scale_mode) {
  SDL_Texture* texture = SDL_CreateTexture(this->sdl_renderer, format,
                                           SDL_TEXTUREACCESS_TARGET, w, h);

  if (texture == nullptr) {
    std::cerr << "Window::create_texture error: " << SDL_GetError()
              << std::endl;
    return nullptr;
  }

  SDL_SetTextureScaleMode(texture, scale_mode);

  return texture;
}

SDL_Texture* Window::create_texture_from_surface(SDL_Surface* surface,
                                                 SDL_ScaleMode scale_mode) {
  SDL_Texture* texture =
      SDL_CreateTextureFromSurface(this->sdl_renderer, surface);

  if (texture == nullptr) {
    std::cerr << "Window::create_texture_from_surface error: " << SDL_GetError()
              << std::endl;
    return nullptr;
  }

  SDL_SetTextureScaleMode(texture, scale_mode);

  return texture;
}

SDL_Texture* Window::create_texture_from_bmp_memory(const void* data,
                                                    size_t size,
                                                    SDL_ScaleMode scale_mode) {
  SDL_IOStream* io = SDL_IOFromConstMem(data, size);
  if (io == nullptr) {
    std::cerr << "Window::create_texture_from_bmp_memory error: "
              << SDL_GetError() << std::endl;
    return nullptr;
  }

  SDL_Surface* surface = SDL_LoadBMP_IO(io, true);
  if (surface == nullptr) {
    std::cerr << "Window::create_texture_from_bmp_memory error: "
              << SDL_GetError() << std::endl;
    return nullptr;
  }

  SDL_Texture* texture = this->create_texture_from_surface(surface, scale_mode);
  SDL_DestroySurface(surface);

  return texture;
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
    std::cerr << "Window::read_pixels error: " << SDL_GetError() << std::endl;
  }

  return surface;
}

};  // namespace thoom
