#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <unordered_map>

#include "macros.h"

namespace thoom {

class Texture {
 public:
  typedef SDL_Texture* (*RenderFunction)(const std::string&);

  inline static Texture* instance = nullptr;

  static void init();
  static void shutdown();

  Texture() = default;
  ~Texture();
  DISALLOW_COPY_AND_MOVE(Texture);

  void register_render_function(const std::string& name, RenderFunction func);
  SDL_Texture* load(const std::string& path);

  SDL_Texture* create_texture(int w, int h, SDL_PixelFormat format,
                              SDL_ScaleMode scale_mode);
  SDL_Texture* create_texture_from_surface(SDL_Surface* surface,
                                           SDL_ScaleMode scale_mode);
  SDL_Texture* create_texture_from_bmp_memory(const void* data, size_t size,
                                              SDL_ScaleMode scale_mode);

 private:
  std::unordered_map<std::string, RenderFunction> render_functions;

  std::unordered_map<std::string, SDL_Texture*> textures;
};

};  // namespace thoom
