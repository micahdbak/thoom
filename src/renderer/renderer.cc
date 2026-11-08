#include "renderer/renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "colour.h"
#include "map/map.h"
#include "map/map_collider.h"
#include "map/map_layer.h"
#include "renderer/font.h"
#include "texture/texture.h"
#include "thoom/macros.h"
#include "window/window.h"

#define UI_BOX_SIZE 4.0f

namespace thoom {

static const unsigned char kRendererUiBoxBmp[] = {
#embed "renderer_ui_box.bmp"
};

bool Renderer::init() {
  if (Renderer::instance != nullptr) {
    fprintf(stderr,
            THOOM_LOG "Renderer::init error: renderer already initialized\n");
    return false;
  }

  if (Window::instance == nullptr) {
    fprintf(stderr, THOOM_LOG "Renderer::init error: window not initialized\n");
    return false;
  }

  Renderer* renderer = new Renderer();

  if (!Font::load_fonts(renderer->fonts)) {
    delete renderer;
    return false;
  }

  renderer->ui_box = Texture::instance->create_texture_from_bmp_memory(
      kRendererUiBoxBmp, sizeof(kRendererUiBoxBmp), SDL_SCALEMODE_NEAREST);

  if (renderer->ui_box == nullptr) {
    delete renderer;
    return false;
  }

  // A zero view offsets sprites by half a screen until a map load resets it.
  renderer->reset_view();
  Renderer::instance = renderer;
  return true;
}

void Renderer::quit() {
  if (Renderer::instance == nullptr) {
    return;
  }

  delete Renderer::instance;
  Renderer::instance = nullptr;
}

Renderer::~Renderer() {
  for (Font* font : this->fonts) delete font;
  this->fonts.clear();

  if (this->ui_box != nullptr) {
    SDL_DestroyTexture(this->ui_box);
    this->ui_box = nullptr;
  }
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

void Renderer::map_rect(const Map* map, const SDL_FRect* viewport,
                        SDL_FRect* src_rect, SDL_FRect* dst_rect) const {
  if (src_rect == nullptr || dst_rect == nullptr) return;

  src_rect->x = 0.0f;
  src_rect->y = 0.0f;
  src_rect->w = 0.0f;
  src_rect->h = 0.0f;
  *dst_rect = *src_rect;

  if (map == nullptr || viewport == nullptr) return;

  int viewport_x = int(viewport->x);
  int viewport_y = int(viewport->y);
  int viewport_w = int(viewport->w);
  int viewport_h = int(viewport->h);

  if (viewport_w <= 0 || viewport_h <= 0) return;

  int map_width = map->tile_width * map->cols;
  int map_height = map->tile_height * map->rows;
  int corner_x = int(this->view_x) - (viewport_w / 2);
  int corner_y = int(this->view_y) - (viewport_h / 2);

  int left = THOOM_MAX(corner_x, 0);
  int top = THOOM_MAX(corner_y, 0);
  int right = THOOM_MIN(corner_x + viewport_w, map_width);
  int bottom = THOOM_MIN(corner_y + viewport_h, map_height);

  if (right <= left || bottom <= top) return;

  src_rect->x = float(left);
  src_rect->y = float(top);
  src_rect->w = float(right - left);
  src_rect->h = float(bottom - top);

  dst_rect->x = float(viewport_x + (left - corner_x));
  dst_rect->y = float(viewport_y + (top - corner_y));
  dst_rect->w = src_rect->w;
  dst_rect->h = src_rect->h;
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

void Renderer::draw_ui_box(int type, SDL_Texture* dst, SDL_FRect* rect) {
  if (this->ui_box == nullptr || dst == nullptr) {
    // TODO(micahdbak): warning
    return;
  }

  SDL_FRect _dst_rect{0.0f, 0.0f, float(dst->w), float(dst->h)};
  if (rect == nullptr) {
    rect = &_dst_rect;
  }

  // ui boxes are laid out as a 3x3 grid of cells within the ui_box texture
  float x = type * UI_BOX_SIZE * 3.0f;
  float cell = UI_BOX_SIZE;

  SDL_FRect src_rect, dst_rect;

  // top-left corner
  src_rect = {x, 0.0f, cell, cell};
  dst_rect = {rect->x, rect->y, cell, cell};
  this->draw_texture(dst, this->ui_box, &src_rect, &dst_rect);

  // top-right corner
  src_rect = {x + 2.0f * cell, 0.0f, cell, cell};
  dst_rect = {rect->x + rect->w - cell, rect->y, cell, cell};
  this->draw_texture(dst, this->ui_box, &src_rect, &dst_rect);

  // bottom-right corner
  src_rect = {x + 2.0f * cell, cell * 2.0f, cell, cell};
  dst_rect = {rect->x + rect->w - cell, rect->y + rect->h - cell, cell, cell};
  this->draw_texture(dst, this->ui_box, &src_rect, &dst_rect);

  // bottom-left corner
  src_rect = {x, cell * 2.0f, cell, cell};
  dst_rect = {rect->x, rect->y + rect->h - cell, cell, cell};
  this->draw_texture(dst, this->ui_box, &src_rect, &dst_rect);

  // top edge
  src_rect = {x + cell, 0.0f, cell, cell};
  dst_rect = {rect->x + cell, rect->y, rect->w - (2.0f * cell), cell};
  this->draw_texture_tiled(dst, this->ui_box, &src_rect, 1.0f, &dst_rect);

  // right edge
  src_rect = {x + 2.0f * cell, cell, cell, cell};
  dst_rect = {rect->x + rect->w - cell, rect->y + cell, cell,
              rect->h - (2.0f * cell)};
  this->draw_texture_tiled(dst, this->ui_box, &src_rect, 1.0f, &dst_rect);

  // bottom edge
  src_rect = {x + cell, cell * 2.0f, cell, cell};
  dst_rect = {rect->x + cell, rect->y + rect->h - cell, rect->w - (2.0f * cell),
              cell};
  this->draw_texture_tiled(dst, this->ui_box, &src_rect, 1.0f, &dst_rect);

  // left edge
  src_rect = {x, cell, cell, cell};
  dst_rect = {rect->x, rect->y + cell, cell, rect->h - (2.0f * cell)};
  this->draw_texture_tiled(dst, this->ui_box, &src_rect, 1.0f, &dst_rect);

  // middle
  src_rect = {x + cell, cell, cell, cell};
  dst_rect = {rect->x + cell, rect->y + cell, rect->w - (2.0f * cell),
              rect->h - (2.0f * cell)};
  this->draw_texture_tiled(dst, this->ui_box, &src_rect, 1.0f, &dst_rect);
}

void Renderer::draw_map_layer(SDL_Texture* dst, Map* map,
                              const SDL_FRect* viewport, int layer,
                              float alpha) {
  if (dst == nullptr || map == nullptr || viewport == nullptr) return;
  if (layer < 0 || layer >= Map::kLayerCount) return;

  MapLayer* map_layer = map->layers[layer];

  if (map_layer == nullptr || map_layer->texture == nullptr) return;

  SDL_FRect src_rect;
  SDL_FRect dst_rect;
  this->map_rect(map, viewport, &src_rect, &dst_rect);

  if (src_rect.w <= 0.0f || src_rect.h <= 0.0f) return;

  if (alpha < 0.0f) alpha = 0.0f;
  if (alpha > 1.0f) alpha = 1.0f;

  if (alpha < 1.0f) {
    SDL_SetTextureAlphaMod(map_layer->texture, Uint8((alpha * 255.0f) + 0.5f));
  }

  this->draw_texture(dst, map_layer->texture, &src_rect, &dst_rect);

  if (alpha < 1.0f) {
    SDL_SetTextureAlphaMod(map_layer->texture, 0xff);
  }
}

void Renderer::draw_world(Map* map) {
  SDL_FRect viewport = {0.0f, 0.0f, float(Window::instance->screen_width),
                        float(Window::instance->screen_height)};
  Colour background = map != nullptr ? map->background : kBlack;

  Window::instance->clear_screen(background);

  if (map != nullptr) {
    this->draw_map_layer(Window::instance->screen, map, &viewport,
                         Map::kBackgroundLayer, 1.0f);
  }

  this->draw_sprites();

  if (map != nullptr) {
    this->draw_map_layer(Window::instance->screen, map, &viewport,
                         Map::kForegroundLayer, 1.0f);
  }
}

void Renderer::draw_collision_tile(SDL_Texture* dst, Map* map,
                                   const SDL_FRect* viewport, int x, int y,
                                   Colour colour) {
  if (dst == nullptr || map == nullptr || map->collider == nullptr ||
      viewport == nullptr) {
    return;
  }

  int collider = map->collider->get(x, y);

  if (collider < 0) return;

  SDL_FRect src_rect;
  SDL_FRect dst_rect;
  this->map_rect(map, viewport, &src_rect, &dst_rect);

  if (src_rect.w <= 0.0f || src_rect.h <= 0.0f) return;

  SDL_Rect clip;
  clip.x = int(viewport->x);
  clip.y = int(viewport->y);
  clip.w = int(viewport->w);
  clip.h = int(viewport->h);

  SDL_Vertex vertices[4];

  if (!this->build_collision_quad(map, &src_rect, &dst_rect, x, y, collider,
                                  colour, vertices)) {
    return;
  }

  const int indices[6] = {0, 1, 2, 2, 3, 0};
  SDL_Renderer* renderer = Window::instance->sdl_renderer;

  SDL_SetRenderTarget(renderer, dst);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderClipRect(renderer, &clip);
  SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
  SDL_SetRenderClipRect(renderer, nullptr);
  SDL_SetRenderTarget(renderer, Window::instance->screen);
}

void Renderer::draw_collision(SDL_Texture* dst, Map* map,
                              const SDL_FRect* viewport, Colour colour) {
  if (dst == nullptr || map == nullptr || map->collider == nullptr ||
      viewport == nullptr) {
    return;
  }

  SDL_FRect src_rect;
  SDL_FRect dst_rect;
  this->map_rect(map, viewport, &src_rect, &dst_rect);

  if (src_rect.w <= 0.0f || src_rect.h <= 0.0f) return;

  SDL_Rect clip;
  clip.x = int(viewport->x);
  clip.y = int(viewport->y);
  clip.w = int(viewport->w);
  clip.h = int(viewport->h);

  const int indices[6] = {0, 1, 2, 2, 3, 0};
  SDL_Renderer* renderer = Window::instance->sdl_renderer;

  SDL_SetRenderTarget(renderer, dst);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderClipRect(renderer, &clip);

  for (int y = 0; y < map->rows; y++) {
    for (int x = 0; x < map->cols; x++) {
      int collider = map->collider->get(x, y);

      if (collider < 0) continue;

      SDL_Vertex vertices[4];

      if (!this->build_collision_quad(map, &src_rect, &dst_rect, x, y, collider,
                                      colour, vertices)) {
        continue;
      }

      SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
    }
  }

  SDL_SetRenderClipRect(renderer, nullptr);
  SDL_SetRenderTarget(renderer, Window::instance->screen);
}

void Renderer::draw_collision_sheet(SDL_Texture* dst, Map* map,
                                    const SDL_FRect* dst_rect, int cols,
                                    Colour colour) {
  if (dst == nullptr || map == nullptr || map->collider == nullptr ||
      dst_rect == nullptr || cols <= 0) {
    return;
  }

  int count = int(n_MapColliders) + 1;
  int rows = (count + cols - 1) / cols;

  if (rows <= 0 || dst_rect->w <= 0.0f || dst_rect->h <= 0.0f) return;
  if (map->tile_width <= 0 || map->tile_height <= 0) return;

  float cell_width = dst_rect->w / float(cols);
  float cell_height = dst_rect->h / float(rows);
  float scale_x = cell_width / float(map->tile_width);
  float scale_y = cell_height / float(map->tile_height);

  SDL_SetRenderDrawBlendMode(Window::instance->sdl_renderer,
                             SDL_BLENDMODE_BLEND);

  for (int i = 0; i < int(n_MapColliders); i++) {
    int cell_x = (i + 1) % cols;
    int cell_y = (i + 1) / cols;
    float origin_x = dst_rect->x + (float(cell_x) * cell_width);
    float origin_y = dst_rect->y + (float(cell_y) * cell_height);

    const Quad& quad = map->collider->quads[i];
    SDL_Vertex vertices[4];

    for (int j = 0; j < 4; j++) {
      vertices[j].color.r = float(colour.r) / 255.0f;
      vertices[j].color.g = float(colour.g) / 255.0f;
      vertices[j].color.b = float(colour.b) / 255.0f;
      vertices[j].color.a = float(colour.a) / 255.0f;
      vertices[j].position.x = origin_x + (quad.vertex[j].x * scale_x);
      vertices[j].position.y = origin_y + (quad.vertex[j].y * scale_y);
    }

    const int indices[6] = {0, 1, 2, 2, 3, 0};
    this->draw_geometry(dst, nullptr, vertices, 4, indices, 6);
  }
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

bool Renderer::build_collision_quad(Map* map, const SDL_FRect* src_rect,
                                    const SDL_FRect* dst_rect, int x, int y,
                                    int collider, Colour colour,
                                    SDL_Vertex* vertices) const {
  if (collider < 0 || collider >= int(n_MapColliders)) return false;

  SDL_FRect tile_rect;
  tile_rect.x = float(x * map->tile_width);
  tile_rect.y = float(y * map->tile_height);
  tile_rect.w = float(map->tile_width);
  tile_rect.h = float(map->tile_height);

  if (!SDL_HasRectIntersectionFloat(&tile_rect, src_rect)) return false;

  const Quad& quad = map->collider->quads[collider];

  for (int i = 0; i < 4; i++) {
    vertices[i].color.r = float(colour.r) / 255.0f;
    vertices[i].color.g = float(colour.g) / 255.0f;
    vertices[i].color.b = float(colour.b) / 255.0f;
    vertices[i].color.a = float(colour.a) / 255.0f;
    vertices[i].position.x =
        dst_rect->x + (tile_rect.x + quad.vertex[i].x) - src_rect->x;
    vertices[i].position.y =
        dst_rect->y + (tile_rect.y + quad.vertex[i].y) - src_rect->y;
  }

  return true;
}

};  // namespace thoom
