#include "thoom/thoom.h"

#include "audio.h"
#include "input.h"
#include "renderer.h"
#include "texture.h"
#include "window.h"

bool thoom_init(const char* title, unsigned int screen_width,
                unsigned int screen_height) {
  if (!thoom::Window::init(title, int(screen_width), int(screen_height))) {
    return false;
  }

  if (!thoom::Texture::init()) {
    goto quit_window;
  }

  if (!thoom::Renderer::init()) {
    goto quit_texture;
  }

  if (!thoom::Audio::init()) {
    goto quit_renderer;
  }

  if (!thoom::Input::init()) {
    goto quit_audio;
  }

  return true;

quit_audio:
  thoom::Audio::quit();
quit_renderer:
  thoom::Renderer::quit();
quit_texture:
  thoom::Texture::quit();
quit_window:
  thoom::Window::quit();
  return false;
}

void thoom_quit(void) {
  thoom::Input::quit();
  thoom::Audio::quit();
  thoom::Renderer::quit();
  thoom::Texture::quit();
  thoom::Window::quit();
}
