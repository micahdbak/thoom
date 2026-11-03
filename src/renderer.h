#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "colour.h"
#include "font.h"
#include "macros.h"
#include "map.h"
#include "sprite.h"

namespace thoom {

class Renderer {
 public:
  inline static Renderer* instance = nullptr;

  static void init();
  static void shutdown();

  Renderer() = default;
  ~Renderer();
  DISALLOW_COPY_AND_MOVE(Renderer);

  void set_view(float x, float y);
  void reset_view();
  float world_to_screen_x(float x) const;
  float world_to_screen_y(float y) const;

  void clear(SDL_Texture* texture, Colour colour);

  void draw_rect(SDL_Texture* dst, SDL_FRect* rect, Colour colour,
                 SDL_BlendMode blend_mode);
  void draw_outline(SDL_Texture* dst, SDL_FRect* rect, Colour colour,
                    SDL_BlendMode blend_mode);
  void draw_texture(SDL_Texture* dst, SDL_Texture* texture,
                    const SDL_FRect* src_rect, const SDL_FRect* dst_rect);
  void draw_texture_tiled(SDL_Texture* dst, SDL_Texture* texture,
                          const SDL_FRect* src_rect, float scale,
                          const SDL_FRect* dst_rect);
  void draw_geometry(SDL_Texture* dst, SDL_Texture* texture,
                     const SDL_Vertex* vertices, int num_vertices,
                     const int* indices, int num_indices);
  void draw_text(SDL_Texture* dst, int font, std::string text, int x, int y,
                 int w, Colour bg);
  void draw_map_background(Map* map);
  void draw_map_foreground(Map* map);
  void push_sprite(const std::string& tex_id, SDL_Texture* texture,
                   SDL_FRect* src_rect, SDL_FRect* dst_rect, int depth_offset);
  void draw_sprites();

  std::vector<Font*> fonts;
  std::vector<SpriteRender> sprites;

 private:
  float view_x = 0.0f, view_y = 0.0f;
};

};  // namespace thoom
