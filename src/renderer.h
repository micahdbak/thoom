#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "colour.h"
#include "font.h"
#include "macros.h"
#include "sprite.h"

namespace thoom {

class Renderer {
 public:
  inline static Renderer* instance = nullptr;

  static void init(const char* title, int screen_width, int screen_height);
  static void shutdown();

  Renderer(const char* title, int screen_width, int screen_height);
  ~Renderer();
  DISALLOW_COPY_AND_MOVE(Renderer);

  void set_window_title(const char* title);
  void update_screen_scale();
  void clear_screen(Colour colour);
  void save_screenshot(const char* file_path);
  void present(Colour letterbox_colour);

  SDL_Texture* create_texture(int w, int h, SDL_PixelFormat format,
                              SDL_ScaleMode scale_mode);
  SDL_Texture* create_texture_from_surface(SDL_Surface* surface,
                                           SDL_ScaleMode scale_mode);
  SDL_Surface* read_pixels(SDL_Texture* texture);
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
  void draw_text(SDL_Texture* dst, Font* font, std::string text, int x, int y,
                 int w, Colour bg);
  void draw_sprites(std::vector<SpriteRender>& sprites, SDL_FRect* viewport,
                    int corner_x, int corner_y);

  // TODO(micahdbak): make private; expose intent-specific methods instead
  SDL_Renderer* sdl_renderer;
  SDL_Texture* screen;

 private:
  SDL_Window* sdl_window;
  SDL_FRect screen_rect;
  int screen_width, screen_height;
};

};  // namespace thoom
