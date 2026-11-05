#include "renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

#include "colour.h"
#include "font.h"
#include "map.h"
#include "sprite.h"
#include "window.h"

namespace thoom {

void Renderer::init() {
  if (Renderer::instance != nullptr) {
    std::cerr << "Renderer::init error: renderer already initialized"
              << std::endl;
    std::exit(1);
  }

  if (Window::instance == nullptr) {
    std::cerr << "Renderer::init error: window not initialized" << std::endl;
    std::exit(1);
  }

  Renderer::instance = new Renderer();
  Font::load_fonts(Renderer::instance->fonts);
}

void Renderer::shutdown() {
  if (Renderer::instance == nullptr) {
    return;
  }

  delete Renderer::instance;
  Renderer::instance = nullptr;
}

Renderer::~Renderer() {
  for (Font* font : this->fonts) delete font;
  this->fonts.clear();
}

void Renderer::set_view(float x, float y) {
  this->view_x = x;
  this->view_y = y;
}

void Renderer::reset_view() {
  this->view_x = float(Window::instance->screen_width) / 2.0f;
  this->view_y = float(Window::instance->screen_height) / 2.0f;
}

float Renderer::world_to_screen_x(float x) const {
  return x - float(this->view_x) + float(Window::instance->screen_width) / 2.0f;
}

float Renderer::world_to_screen_y(float y) const {
  return y - float(this->view_y) +
         float(Window::instance->screen_height) / 2.0f;
}

void Renderer::clear(SDL_Texture* texture, Colour colour) {
  if (texture == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(Window::instance->sdl_renderer, texture);
  SDL_SetRenderDrawColor(Window::instance->sdl_renderer, colour.r, colour.g,
                         colour.b, colour.a);
  SDL_RenderClear(Window::instance->sdl_renderer);
  SDL_SetRenderTarget(Window::instance->sdl_renderer, Window::instance->screen);
}

void Renderer::draw_rect(SDL_Texture* dst, SDL_FRect* rect, Colour colour,
                         SDL_BlendMode blend_mode) {
  if (dst == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(Window::instance->sdl_renderer, dst);
  SDL_SetRenderDrawColor(Window::instance->sdl_renderer, colour.r, colour.g,
                         colour.b, colour.a);
  SDL_SetRenderDrawBlendMode(Window::instance->sdl_renderer, blend_mode);
  SDL_RenderFillRect(Window::instance->sdl_renderer, rect);
  SDL_SetRenderTarget(Window::instance->sdl_renderer, Window::instance->screen);
}

void Renderer::draw_outline(SDL_Texture* dst, SDL_FRect* rect, Colour colour,
                            SDL_BlendMode blend_mode) {
  if (dst == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(Window::instance->sdl_renderer, dst);
  SDL_SetRenderDrawColor(Window::instance->sdl_renderer, colour.r, colour.g,
                         colour.b, colour.a);
  SDL_SetRenderDrawBlendMode(Window::instance->sdl_renderer, blend_mode);
  SDL_RenderRect(Window::instance->sdl_renderer, rect);
  SDL_SetRenderTarget(Window::instance->sdl_renderer, Window::instance->screen);
}

void Renderer::draw_texture(SDL_Texture* dst, SDL_Texture* texture,
                            const SDL_FRect* src_rect,
                            const SDL_FRect* dst_rect) {
  if (dst == nullptr || texture == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(Window::instance->sdl_renderer, dst);
  SDL_RenderTexture(Window::instance->sdl_renderer, texture, src_rect,
                    dst_rect);
  SDL_SetRenderTarget(Window::instance->sdl_renderer, Window::instance->screen);
}

void Renderer::draw_texture_tiled(SDL_Texture* dst, SDL_Texture* texture,
                                  const SDL_FRect* src_rect, float scale,
                                  const SDL_FRect* dst_rect) {
  if (dst == nullptr || texture == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(Window::instance->sdl_renderer, dst);
  SDL_RenderTextureTiled(Window::instance->sdl_renderer, texture, src_rect,
                         scale, dst_rect);
  SDL_SetRenderTarget(Window::instance->sdl_renderer, Window::instance->screen);
}

void Renderer::draw_geometry(SDL_Texture* dst, SDL_Texture* texture,
                             const SDL_Vertex* vertices, int num_vertices,
                             const int* indices, int num_indices) {
  if (dst == nullptr || vertices == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_SetRenderTarget(Window::instance->sdl_renderer, dst);
  SDL_RenderGeometry(Window::instance->sdl_renderer, texture, vertices,
                     num_vertices, indices, num_indices);
  SDL_SetRenderTarget(Window::instance->sdl_renderer, Window::instance->screen);
}

void Renderer::draw_text(SDL_Texture* dst, int font, std::string text, int x,
                         int y, int w, Colour bg) {
  if (dst == nullptr || font < 0 || font >= NUM_FONTS) {
    // TODO(micahdbak): warning
    return;
  }

  Font* draw_font = this->fonts[font];

  SDL_SetRenderTarget(Window::instance->sdl_renderer, dst);

  int _x = x, _y = y, line_height = int(draw_font->src_rect[0].h) + 1;
  int running_width = 0;
  SDL_FRect dst_rect;
  SDL_SetRenderDrawColor(Window::instance->sdl_renderer, bg.r, bg.g, bg.b,
                         bg.a);

  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];

    if (c >= ' ' && c <= '~') {
      SDL_FRect* src_rect = draw_font->src_rect + (c - ' ');

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
              src_rect = draw_font->src_rect + (c - ' ');
              running_width -= int(src_rect->w);
            }
          }
          i++;

          // undo the rendered characters
          if (running_width < rendered_width) {
            SDL_FRect undo_rect = {float(x + running_width), float(_y),
                                   float(rendered_width - running_width),
                                   float(line_height)};
            SDL_RenderFillRect(Window::instance->sdl_renderer, &undo_rect);
          }

          // go to next line
          _x = x;
          _y += line_height;
          running_width = 0;

          // render the previously rendered characters
          while (i < j) {
            c = text[i++];
            src_rect = draw_font->src_rect + (c - ' ');
            running_width += int(src_rect->w);
            dst_rect = {float(_x), float(_y), src_rect->w, src_rect->h};
            _x += int(src_rect->w);
            SDL_RenderTexture(Window::instance->sdl_renderer,
                              draw_font->texture, src_rect, &dst_rect);
          }

          i--;  // continue will increment i
          continue;
        }
      }

      // render character
      dst_rect = {float(_x), float(_y), src_rect->w, src_rect->h};
      SDL_RenderTexture(Window::instance->sdl_renderer, draw_font->texture,
                        src_rect, &dst_rect);
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

  SDL_SetRenderTarget(Window::instance->sdl_renderer, Window::instance->screen);
}

void Renderer::draw_map_background(Map* map) {
  if (map == nullptr) return;

  SDL_Texture* texture = map->layers[Map::kBackgroundLayer]->texture;
  if (texture == nullptr) return;

  SDL_FRect src_rect = {0.0f, 0.0f, float(Window::instance->screen_width),
                        float(Window::instance->screen_height)};
  SDL_FRect dst_rect = src_rect;
  int corner_x = int(this->view_x) - Window::instance->screen_width / 2;
  int corner_y = int(this->view_y) - Window::instance->screen_height / 2;

  Map::make_map_rect(corner_x, corner_y, texture->w, texture->h, &src_rect,
                     &dst_rect);
  this->draw_texture(Window::instance->screen, texture, &src_rect, &dst_rect);
}

void Renderer::draw_map_foreground(Map* map) {
  if (map == nullptr) return;

  SDL_Texture* texture = map->layers[Map::kForegroundLayer]->texture;
  if (texture == nullptr) return;

  SDL_FRect src_rect = {0.0f, 0.0f, float(Window::instance->screen_width),
                        float(Window::instance->screen_height)};
  SDL_FRect dst_rect = src_rect;
  int corner_x = int(this->view_x) - Window::instance->screen_width / 2;
  int corner_y = int(this->view_y) - Window::instance->screen_height / 2;

  Map::make_map_rect(corner_x, corner_y, texture->w, texture->h, &src_rect,
                     &dst_rect);
  this->draw_texture(Window::instance->screen, texture, &src_rect, &dst_rect);
}

void Renderer::push_sprite(const std::string& tex_id, SDL_Texture* texture,
                           SDL_FRect* src_rect, SDL_FRect* dst_rect,
                           int depth_offset) {
  int sprite_y = 0;

  if (dst_rect != nullptr) sprite_y = (int)dst_rect->y + depth_offset;

  SpriteRender sprite;
  sprite.tex_id = tex_id;
  sprite.texture = texture;
  sprite.src_rect = src_rect;
  sprite.dst_rect = dst_rect;
  sprite.y = sprite_y;

  // std::lower_bound performs a binary search (log n time complexity)
  auto it =
      std::lower_bound(this->sprites.begin(), this->sprites.end(), sprite);
  this->sprites.insert(it, sprite);
}

void Renderer::draw_sprites() {
  SDL_SetRenderDrawBlendMode(Window::instance->sdl_renderer,
                             SDL_BLENDMODE_BLEND);

  SDL_FRect viewport = {0.0f, 0.0f, float(Window::instance->screen_width),
                        float(Window::instance->screen_height)};
  int corner_x = int(this->view_x) - Window::instance->screen_width / 2;
  int corner_y = int(this->view_y) - Window::instance->screen_height / 2;

  for (SpriteRender& sprite : this->sprites) {
    if (sprite.dst_rect == nullptr) continue;

    SDL_FRect dst_rect = *sprite.dst_rect;
    dst_rect.x -= corner_x;
    dst_rect.y -= corner_y;

    if (SDL_HasRectIntersectionFloat(&viewport, &dst_rect)) {
      this->draw_texture(Window::instance->screen, sprite.texture,
                         sprite.src_rect, &dst_rect);
    }
  }

  this->sprites.clear();  // clear sprites; next frame will repopulate
}

};  // namespace thoom
