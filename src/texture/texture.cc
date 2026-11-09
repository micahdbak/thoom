#include "texture/texture.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "absl/log/log.h"
#include "window/window.h"

namespace thoom {

bool Texture::init() {
  if (Texture::instance != nullptr) {
    LOG(ERROR) << "Texture::init error: texture already initialized";
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
  if (this->render_functions.find(name) != this->render_functions.end()) {
    LOG(WARNING) << "Texture::register_render_function: '" << name
                 << "' is already registered";
  }

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
      LOG(FATAL) << "Texture::load error: render function " << buff
                 << " does not exist";
    }

    char args[1024];
    offset += strlen(buff) + 1;
    sscanf(path.c_str() + offset, "%1023[^/]", args);
    args[1023] = '\0';

    SDL_Texture* texture = this->render_functions[buff](args);

    if (texture == nullptr) {
      LOG(ERROR) << "Texture::load error: render function " << buff
                 << " returned no texture for " << path;
    }

    this->textures[path] = texture;
    return texture;
  }

  SDL_Surface* surface = SDL_LoadBMP(path.c_str());
  if (surface == nullptr) {
    LOG(FATAL) << "SDL_LoadBMP error: " << path << " does not exist";
  }

  SDL_Texture* texture =
      this->create_texture_from_surface(surface, SDL_SCALEMODE_NEAREST);
  SDL_DestroySurface(surface);

  if (texture == nullptr) {
    LOG(ERROR) << "Texture::load error: could not create a texture for "
               << path;
  }

  this->textures[path] = texture;
  return texture;
}

SDL_Texture* Texture::create_texture(int w, int h, SDL_PixelFormat format,
                                     SDL_ScaleMode scale_mode) {
  SDL_Texture* texture = SDL_CreateTexture(
      Window::instance->sdl_renderer, format, SDL_TEXTUREACCESS_TARGET, w, h);

  if (texture == nullptr) {
    LOG(ERROR) << "Texture::create_texture error: " << SDL_GetError();
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
    LOG(ERROR) << "Texture::create_texture_from_surface error: "
               << SDL_GetError();
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
    LOG(ERROR) << "Texture::create_texture_from_bmp_memory error: "
               << SDL_GetError();
    return nullptr;
  }

  SDL_Surface* surface = SDL_LoadBMP_IO(io, true);
  if (surface == nullptr) {
    LOG(ERROR) << "Texture::create_texture_from_bmp_memory error: "
               << SDL_GetError();
    return nullptr;
  }

  SDL_Texture* texture = this->create_texture_from_surface(surface, scale_mode);
  SDL_DestroySurface(surface);

  return texture;
}

};  // namespace thoom
