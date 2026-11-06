#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "colour.h"
#include "font.h"
#include "map.h"
#include "sprite.h"

namespace thoom {

class Renderer {
 public:
  inline static Renderer* instance = nullptr;

  static bool init();
  static void quit();

  Renderer() = default;
  ~Renderer();

  void set_view(float x, float y);
  void reset_view();
  float world_to_screen_x(float x) const;
  float world_to_screen_y(float y) const;

  void map_rect(const Map* map, const SDL_FRect* viewport, SDL_FRect* src_rect,
                SDL_FRect* dst_rect) const;

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

  void draw_map_layer(SDL_Texture* dst, Map* map, const SDL_FRect* viewport,
                      int layer, float alpha);
  void draw_world(Map* map);

  void draw_collision_tile(SDL_Texture* dst, Map* map,
                           const SDL_FRect* viewport, int x, int y,
                           Colour colour);
  void draw_collision(SDL_Texture* dst, Map* map, const SDL_FRect* viewport,
                      Colour colour);
  void draw_collision_sheet(SDL_Texture* dst, Map* map,
                            const SDL_FRect* dst_rect, int cols, Colour colour);

  void push_sprite(const std::string& tex_id, SDL_Texture* texture,
                   SDL_FRect* src_rect, SDL_FRect* dst_rect, int depth_offset);
  void draw_sprites();

  std::vector<Font*> fonts;
  std::vector<SpriteRender> sprites;

 private:
  bool build_collision_quad(Map* map, const SDL_FRect* src_rect,
                            const SDL_FRect* dst_rect, int x, int y,
                            int collider, Colour colour,
                            SDL_Vertex* vertices) const;

  float view_x = 0.0f, view_y = 0.0f;
};

};  // namespace thoom
