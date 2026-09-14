#include "renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <iostream>

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

#define UI_BOX_SIZE 4.0f

void Renderer::init(const char* title, int screen_width, int screen_height) {
  if (Renderer::instance != nullptr) {
    std::cerr << "Renderer::init error: renderer already initialized"
              << std::endl;
    std::exit(1);
  }

  Renderer::instance = new Renderer(title, screen_width, screen_height);
}

void Renderer::shutdown() {
  if (Renderer::instance == nullptr) {
    return;
  }

  delete Renderer::instance;
  Renderer::instance = nullptr;
}

Renderer::Renderer(const char* title, int screen_width, int screen_height)
    : screen_rect{0.0f, 0.0f, 0.0f, 0.0f},
      screen_width(screen_width),
      screen_height(screen_height) {
  bool sdl_init;

  LSAN_DISABLE();
  sdl_init = SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
  LSAN_ENABLE();

  if (!sdl_init) {
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
      this->create_texture(screen_width, screen_height, SDL_PIXELFORMAT_RGB24,
                           SDL_SCALEMODE_NEAREST);

  if (this->screen == nullptr) {
    std::cerr << "SDL_CreateTexture error: " << SDL_GetError() << std::endl;
    std::exit(1);
  }

  this->update_screen_scale();
}

Renderer::~Renderer() {
  SDL_DestroyTexture(this->screen);
  this->screen = nullptr;

  SDL_DestroyRenderer(this->sdl_renderer);
  this->sdl_renderer = nullptr;

  SDL_DestroyWindow(this->sdl_window);
  this->sdl_window = nullptr;

  SDL_Quit();
}

void Renderer::set_window_title(const char* title) {
  SDL_SetWindowTitle(this->sdl_window, title);
}

void Renderer::update_screen_scale() {
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

void Renderer::clear_screen(Colour colour) {
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
  SDL_SetRenderDrawColor(this->sdl_renderer, colour.r, colour.g, colour.b,
                         colour.a);
  SDL_RenderClear(this->sdl_renderer);
}

void Renderer::save_screenshot(const char* file_path) {
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);

  SDL_Surface* screenshot = SDL_RenderReadPixels(this->sdl_renderer, NULL);
  if (screenshot == nullptr) {
    std::cerr << "Renderer::save_screenshot error: " << SDL_GetError()
              << std::endl;
    return;
  }

  if (!SDL_SaveBMP(screenshot, file_path)) {
    std::cerr << "Renderer::save_screenshot error: " << SDL_GetError()
              << std::endl;
  }

  SDL_DestroySurface(screenshot);
}

void Renderer::present(Colour letterbox_colour) {
  SDL_SetRenderTarget(this->sdl_renderer, NULL);
  SDL_SetRenderDrawColor(this->sdl_renderer, letterbox_colour.r,
                         letterbox_colour.g, letterbox_colour.b,
                         letterbox_colour.a);
  SDL_RenderClear(this->sdl_renderer);
  SDL_RenderTexture(this->sdl_renderer, this->screen, NULL, &this->screen_rect);
  SDL_RenderPresent(this->sdl_renderer);
}

SDL_Texture* Renderer::create_texture(int w, int h, SDL_PixelFormat format,
                                      SDL_ScaleMode scale_mode) {
  SDL_Texture* texture = SDL_CreateTexture(this->sdl_renderer, format,
                                           SDL_TEXTUREACCESS_TARGET, w, h);

  if (texture == nullptr) {
    std::cerr << "Renderer::create_texture error: " << SDL_GetError()
              << std::endl;
    return nullptr;
  }

  SDL_SetTextureScaleMode(texture, scale_mode);

  return texture;
}

SDL_Texture* Renderer::create_texture_from_surface(SDL_Surface* surface,
                                                   SDL_ScaleMode scale_mode) {
  SDL_Texture* texture =
      SDL_CreateTextureFromSurface(this->sdl_renderer, surface);

  if (texture == nullptr) {
    std::cerr << "Renderer::create_texture_from_surface error: "
              << SDL_GetError() << std::endl;
    return nullptr;
  }

  SDL_SetTextureScaleMode(texture, scale_mode);

  return texture;
}

SDL_Surface* Renderer::read_pixels(SDL_Texture* texture) {
  if (texture == nullptr) {
    // TODO(micahdbak): warning
    return nullptr;
  }

  SDL_SetRenderTarget(this->sdl_renderer, texture);
  SDL_Surface* surface = SDL_RenderReadPixels(this->sdl_renderer, NULL);
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);

  if (surface == nullptr) {
    std::cerr << "Renderer::read_pixels error: " << SDL_GetError() << std::endl;
  }

  return surface;
}

void Renderer::clear(SDL_Texture* texture, Colour colour) {
  if (texture == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(this->sdl_renderer, texture);
  SDL_SetRenderDrawColor(this->sdl_renderer, colour.r, colour.g, colour.b,
                         colour.a);
  SDL_RenderClear(this->sdl_renderer);
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
}

void Renderer::draw_rect(SDL_Texture* dst, SDL_FRect* rect, Colour colour,
                         SDL_BlendMode blend_mode) {
  if (dst == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(this->sdl_renderer, dst);
  SDL_SetRenderDrawColor(this->sdl_renderer, colour.r, colour.g, colour.b,
                         colour.a);
  SDL_SetRenderDrawBlendMode(this->sdl_renderer, blend_mode);
  SDL_RenderFillRect(this->sdl_renderer, rect);
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
}

void Renderer::draw_outline(SDL_Texture* dst, SDL_FRect* rect, Colour colour,
                            SDL_BlendMode blend_mode) {
  if (dst == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(this->sdl_renderer, dst);
  SDL_SetRenderDrawColor(this->sdl_renderer, colour.r, colour.g, colour.b,
                         colour.a);
  SDL_SetRenderDrawBlendMode(this->sdl_renderer, blend_mode);
  SDL_RenderRect(this->sdl_renderer, rect);
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
}

void Renderer::draw_texture(SDL_Texture* dst, SDL_Texture* texture,
                            const SDL_FRect* src_rect,
                            const SDL_FRect* dst_rect) {
  if (dst == nullptr || texture == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(this->sdl_renderer, dst);
  SDL_RenderTexture(this->sdl_renderer, texture, src_rect, dst_rect);
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
}

void Renderer::draw_texture_tiled(SDL_Texture* dst, SDL_Texture* texture,
                                  const SDL_FRect* src_rect, float scale,
                                  const SDL_FRect* dst_rect) {
  if (dst == nullptr || texture == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(this->sdl_renderer, dst);
  SDL_RenderTextureTiled(this->sdl_renderer, texture, src_rect, scale,
                         dst_rect);
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
}

void Renderer::draw_geometry(SDL_Texture* dst, SDL_Texture* texture,
                             const SDL_Vertex* vertices, int num_vertices,
                             const int* indices, int num_indices) {
  if (dst == nullptr || vertices == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(this->sdl_renderer, dst);
  SDL_RenderGeometry(this->sdl_renderer, texture, vertices, num_vertices,
                     indices, num_indices);
  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
}

void Renderer::draw_sprites(std::vector<SpriteRender>& sprites,
                            SDL_FRect* viewport, int corner_x, int corner_y) {
  SDL_SetRenderDrawBlendMode(this->sdl_renderer, SDL_BLENDMODE_BLEND);

  for (SpriteRender& sprite : sprites) {
    if (SDL_HasRectIntersectionFloat(viewport, sprite.dst_rect)) {
      SDL_FRect dst_rect = *sprite.dst_rect;
      dst_rect.x -= corner_x;
      dst_rect.y -= corner_y;
      this->draw_texture(this->screen, sprite.texture, sprite.src_rect,
                         &dst_rect);
    }
  }
}

void Renderer::draw_text(SDL_Texture* dst, Font* font, std::string text, int x,
                         int y, int w, Colour bg) {
  if (dst == nullptr || font == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(this->sdl_renderer, dst);

  int _x = x, _y = y, line_height = int(font->src_rect[0].h) + 1;
  int running_width = 0;
  SDL_FRect dst_rect;
  SDL_SetRenderDrawColor(this->sdl_renderer, bg.r, bg.g, bg.b, bg.a);

  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];

    if (c >= ' ' && c <= '~') {
      SDL_FRect* src_rect = font->src_rect + (c - ' ');

      if (w > 0) {
        int rendered_width = running_width;
        running_width += int(src_rect->w);

        // if running too wide, let's back-track and go to the next line
        if (running_width > w) {
          int j = i;
          // back-track to the last space
          while (text[i] != ' ' && i > 0) {
            c = text[i--];
            // subtract the width of each rendered character from running_width
            if (c >= ' ' && c <= '~') {
              src_rect = font->src_rect + (c - ' ');
              running_width -= int(src_rect->w);
            }
          }
          i++;

          // undo the rendered characters
          if (running_width < rendered_width) {
            SDL_FRect undo_rect = {float(x + running_width), float(_y),
                                   float(rendered_width - running_width),
                                   float(line_height)};
            SDL_RenderFillRect(this->sdl_renderer, &undo_rect);
          }

          // go to next line
          _x = x;
          _y += line_height;
          running_width = 0;

          // render the previously rendered characters
          while (i < j) {
            c = text[i++];
            src_rect = font->src_rect + (c - ' ');
            running_width += int(src_rect->w);
            dst_rect = {float(_x), float(_y), src_rect->w, src_rect->h};
            _x += int(src_rect->w);
            SDL_RenderTexture(this->sdl_renderer, font->texture, src_rect,
                              &dst_rect);
          }

          i--;  // continue will increment i
          continue;
        }
      }

      // render character
      dst_rect = {float(_x), float(_y), src_rect->w, src_rect->h};
      SDL_RenderTexture(this->sdl_renderer, font->texture, src_rect, &dst_rect);
      _x += int(src_rect->w);
    } else {
      switch (c) {
        case '\n':
          _x = x;
          _y += line_height;
          running_width = 0;
          break;
        case '\t': {
          int tabline = ((_x / 32) + 1) * 32;
          int rem = tabline - _x;
          running_width += rem;
          _x += rem;
        } break;
        default: /* pass */
          break;
      }
    }
  }

  SDL_SetRenderTarget(this->sdl_renderer, this->screen);
}

};  // namespace thoom
