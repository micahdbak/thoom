#include "thoom/thoom.h"

#include <cstdint>

#include "SDL3/SDL.h"
#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log_sink_registry.h"
#include "audio/audio.h"
#include "hud/hud.h"
#include "input/input.h"
#include "peer/peer.h"
#include "renderer/renderer.h"
#include "switch/peer_identity.h"
#include "texture/texture.h"
#include "window/window.h"

// Abseil has no logging shutdown, and initializing twice is an error, so the
// first successful init wins.
static bool log_initialized = false;

bool thoom_init(const char* title, uint32_t screen_width,
                uint32_t screen_height, uint32_t background,
                const struct hud_def* def) {
  if (!log_initialized) {
    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    log_initialized = true;
  }

  struct hud_def default_def = {};
  const struct hud_def& hud = def != nullptr ? *def : default_def;

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

  if (!thoom::PeerIdentity::init()) {
    goto quit_input;
  }

  if (!thoom::Hud::init(background, hud)) {
    goto quit_identity;
  }

  return true;

quit_identity:
  thoom::PeerIdentity::quit();
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

bool thoom_room_init(bool host) { return thoom::Peer::init(host); }

void thoom_room_quit(void) { thoom::Peer::quit(); }

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
  thoom::Hud::quit();
  thoom::Peer::quit();
  thoom::PeerIdentity::quit();
  thoom::Input::quit();
  thoom::Audio::quit();
  thoom::Renderer::quit();
  thoom::Texture::quit();
  thoom::Window::quit();

  absl::FlushLogSinks();
}
