#include "texture/texture.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "thoom/macros.h"
#include "window/window.h"

namespace thoom {

bool Texture::init() {
  if (Texture::instance != nullptr) {
    fprintf(stderr,
            THOOM_LOG "Texture::init error: texture already initialized\n");
    return false;
  }

  Texture::instance = new Texture();
  return true;
}

void Texture::quit() {
  if (Texture::instance == nullptr) {
    return;
  }

  delete Texture::instance;
  Texture::instance = nullptr;
}

Texture::~Texture() {
  for (auto& pair : this->textures) {
    SDL_DestroyTexture(pair.second);
  }

  this->textures.clear();
}

void Texture::register_render_function(const std::string& name,
                                       RenderFunction func) {
  this->render_functions[name] = func;
}

SDL_Texture* Texture::load(const std::string& path) {
  if (this->textures.find(path) != this->textures.end()) {
    return this->textures[path];
  }

  char buff[1024];
  sscanf(path.c_str(), "%1023[^/]", buff);
  buff[1023] = '\0';

  if (std::string(buff) == "render") {
    size_t offset = strlen(buff) + 1;  // + 1 for '/'
    sscanf(path.c_str() + offset, "%1023[^/]", buff);
    buff[1023] = '\0';

    if (this->render_functions.find(buff) == this->render_functions.end()) {
      fprintf(stderr,
              THOOM_LOG
              "Texture::load error: render function %s does not exist.\n",
              buff);
      exit(1);
    }

    char args[1024];
    offset += strlen(buff) + 1;
    sscanf(path.c_str() + offset, "%1023[^/]", args);
    args[1023] = '\0';

    SDL_Texture* texture = this->render_functions[buff](args);
    this->textures[path] = texture;
    return texture;
  }

  SDL_Surface* surface = SDL_LoadBMP(path.c_str());
  if (surface == nullptr) {
    fprintf(stderr, THOOM_LOG "SDL_LoadBMP error: %s does not exist.\n",
            path.c_str());
    exit(1);
  }

  SDL_Texture* texture =
      this->create_texture_from_surface(surface, SDL_SCALEMODE_NEAREST);
  SDL_DestroySurface(surface);
  this->textures[path] = texture;
  return texture;
}

SDL_Texture* Texture::create_texture(int w, int h, SDL_PixelFormat format,
                                     SDL_ScaleMode scale_mode) {
  SDL_Texture* texture = SDL_CreateTexture(
      Window::instance->sdl_renderer, format, SDL_TEXTUREACCESS_TARGET, w, h);

  if (texture == nullptr) {
    fprintf(stderr, THOOM_LOG "Texture::create_texture error: %s\n",
            SDL_GetError());
    return nullptr;
  }

  SDL_SetTextureScaleMode(texture, scale_mode);

  return texture;
}

SDL_Texture* Texture::create_texture_from_surface(SDL_Surface* surface,
                                                  SDL_ScaleMode scale_mode) {
  SDL_Texture* texture =
      SDL_CreateTextureFromSurface(Window::instance->sdl_renderer, surface);

  if (texture == nullptr) {
    fprintf(stderr,
            THOOM_LOG "Texture::create_texture_from_surface error: %s\n",
            SDL_GetError());
    return nullptr;
  }

  SDL_SetTextureScaleMode(texture, scale_mode);

  return texture;
}

SDL_Texture* Texture::create_texture_from_bmp_memory(const void* data,
                                                     size_t size,
                                                     SDL_ScaleMode scale_mode) {
  SDL_IOStream* io = SDL_IOFromConstMem(data, size);
  if (io == nullptr) {
    fprintf(stderr,
            THOOM_LOG "Texture::create_texture_from_bmp_memory error: %s\n",
            SDL_GetError());
    return nullptr;
  }

  SDL_Surface* surface = SDL_LoadBMP_IO(io, true);
  if (surface == nullptr) {
    fprintf(stderr,
            THOOM_LOG "Texture::create_texture_from_bmp_memory error: %s\n",
            SDL_GetError());
    return nullptr;
  }

  SDL_Texture* texture = this->create_texture_from_surface(surface, scale_mode);
  SDL_DestroySurface(surface);

  return texture;
}

};  // namespace thoom
