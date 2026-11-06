#include "thoom/thoom.h"

#include "SDL3/SDL.h"
#include "audio.h"
#include "input.h"
#include "peer.h"
#include "renderer.h"
#include "texture.h"
#include "window.h"

bool thoom_init(const char* title, unsigned int screen_width,
                unsigned int screen_height, bool offerer) {
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

  if (!thoom::Peer::init(offerer)) {
    goto quit_input;
  }

  return true;

quit_input:
  thoom::Input::quit();
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

void thoom_poll_events(bool* quit) {
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_WINDOW_RESIZED:
        thoom::Window::instance->update_screen_scale();
        break;

      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
        thoom::Input::instance->handle_key_event(event);
        break;

      case SDL_EVENT_QUIT:
        *quit = true;
        break;

      default:
        // pass
        break;
    }
  }
}

void thoom_quit(void) {
  thoom::Peer::quit();
  thoom::Input::quit();
  thoom::Audio::quit();
  thoom::Renderer::quit();
  thoom::Texture::quit();
  thoom::Window::quit();
}
