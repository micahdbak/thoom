#include "hud/hud.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "audio/audio.h"
#include "colour.h"
#include "input/controller.h"
#include "input/input.h"
#include "peer/peer.h"
#include "renderer/font.h"
#include "renderer/renderer.h"
#include "scene/scene.h"
#include "texture/texture.h"
#include "thoom/macros.h"
#include "thoom/thoom.h"
#include "window/window.h"

#define PEER_PADDING 8
#define PEER_GAP 4

namespace thoom {

bool Hud::init(uint32_t background, const struct hud_def& def) {
  if (Hud::instance != nullptr) {
    fprintf(stderr, THOOM_LOG "Hud::init error: hud already initialized\n");
    return false;
  }

  Hud* hud = new Hud(background, def);
  if (hud->ui == nullptr || hud->overlay == nullptr) {
    delete hud;
    return false;
  }

  Hud::instance = hud;
  return true;
}

void Hud::quit() {
  if (Hud::instance == nullptr) {
    return;
  }

  delete Hud::instance;
  Hud::instance = nullptr;
}

Hud::Hud(uint32_t background, const struct hud_def& def)
    : user_draw(def.draw),
      user_show_peer(def.show_peer),
      user_show_hud(def.show_hud),
      user_exit_to_main_menu(def.exit_to_main_menu),
      user_data(def.user),
      background(background) {
  if (Texture::instance == nullptr || Window::instance == nullptr) {
    fprintf(stderr, THOOM_LOG "Hud::Hud error: engine not initialized\n");
    return;
  }

  this->ui = Texture::instance->create_texture(
      Window::instance->screen_width, Window::instance->screen_height,
      SDL_PIXELFORMAT_RGBA32, SDL_SCALEMODE_LINEAR);
  this->overlay = Texture::instance->create_texture(
      Window::instance->screen_width, Window::instance->screen_height,
      SDL_PIXELFORMAT_RGBA32, SDL_SCALEMODE_LINEAR);

  if (this->ui == nullptr || this->overlay == nullptr) {
    fprintf(stderr, THOOM_LOG "Hud::Hud error: %s\n", SDL_GetError());
    return;
  }

  Renderer::instance->clear(this->ui, kMask);
  Renderer::instance->clear(this->overlay, kMask);
}

Hud::~Hud() {
  if (this->ui != nullptr) {
    SDL_DestroyTexture(this->ui);
    this->ui = nullptr;
  }

  if (this->overlay != nullptr) {
    SDL_DestroyTexture(this->overlay);
    this->overlay = nullptr;
  }
}

void Hud::render() {
  Input* input = Input::instance;

  if (input->active->consume_hit(Button::MENU)) {
    this->display_controls_menu = !this->display_controls_menu;
    this->waiting_for_key = false;
    input->set_capture(this->display_controls_menu);
  }

  std::string current_map =
      Scene::instance->map != nullptr ? Scene::instance->map->path : "";

  if (current_map != this->current_map) {
    this->current_map = current_map;

    if (Scene::instance->map != nullptr) {
      this->display_notification(Scene::instance->map->title + ", " +
                                 Scene::instance->map->description);
    }
  }

  bool force_render = false;

  if (this->display_controls_menu) {
    this->draw_controls_menu();
    return;
  } else if (this->displaying_controls_menu) {
    Renderer::instance->clear(this->overlay, kMask);
    this->displaying_controls_menu = false;
    force_render = true;
  }

  if (this->user_show_hud != nullptr && !this->user_show_hud(this->user_data)) {
    if (!force_render) {
      Renderer::instance->clear(this->overlay, kMask);
    }

    return;
  }

  if (this->user_draw != nullptr) {
    this->user_draw(this->user_data);
  }

  this->draw_peer(force_render);
  this->draw_notification(force_render);
}

void Hud::draw() {
  thoom::Renderer::instance->draw_texture(thoom::Window::instance->screen,
                                          thoom::Hud::instance->ui, NULL, NULL);
  thoom::Renderer::instance->draw_texture(thoom::Window::instance->screen,
                                          thoom::Hud::instance->overlay, NULL,
                                          NULL);
}

void Hud::display_notification(const std::string& text) {
  this->notification = text;
  this->notif_ticks = Scene::instance->ticks;
  this->force_notif_rerender = true;
}

void Hud::draw_controls_menu() {
  Input* input = Input::instance;
  bool should_render = !this->displaying_controls_menu;
  this->displaying_controls_menu = true;

  SDL_Keycode keycode = input->captured.last_keycode;

  if (this->waiting_for_key) {
    if (keycode != SDLK_UNKNOWN) {
      input->bind_key((Button)this->sel_control, keycode);

      should_render = true;
      this->waiting_for_key = false;
    }
  } else {
    int y_dir = input->captured.consume_hit_dy();
    int new_sel_control =
        THOOM_CLAMP(this->sel_control + y_dir, -3, (int)Button::DIGIT - 1);

    if (this->sel_control == -3 &&
        input->captured.consume_hit(Button::SELECT)) {
      if (this->user_exit_to_main_menu != nullptr) {
        this->user_exit_to_main_menu(this->user_data);
      }

      this->display_controls_menu = false;
      this->waiting_for_key = false;
      input->set_capture(false);

      return;
    }

    int x_dir = input->captured.consume_hit_dx();
    if (this->sel_control == -2 && x_dir != 0) {
      Audio::instance->volume =
          THOOM_CLAMP(Audio::instance->volume + (10 * x_dir), 0, 200);
      should_render = true;
    } else if (this->sel_control == -1 && x_dir != 0) {
      Audio::instance->music_volume =
          THOOM_CLAMP(Audio::instance->music_volume + (10 * x_dir), 0, 200);
      should_render = true;
    }

    if (this->sel_control != new_sel_control) {
      this->sel_control = new_sel_control;
      should_render = true;
    } else if (input->captured.consume_hit(Button::SELECT) &&
               this->sel_control >= 0) {
      this->waiting_for_key = true;
      should_render = true;
    }
  }

  if (should_render) {
    Colour bg = kBlack;

    if (Scene::instance->map != nullptr) {
      bg = Scene::instance->map->background;
    }

    bg.a = 128;
    Renderer::instance->clear(this->overlay, bg);

    SDL_FRect config_rect = {8.0f, 8.0f, 112.0f, 168.0f};
    Renderer::instance->draw_ui_box(UI_BOX_CONTAINER, this->overlay,
                                    &config_rect);

    Renderer::instance->draw_text(this->overlay, SMALL_FONT, "Game", 16, 16, 0,
                                  this->background);

    Renderer::instance->draw_text(this->overlay, SMALL_FONT,
                                  "Exit to Main Menu", 20, 28, 0,
                                  this->background);

    if (this->sel_control != -3) {
      SDL_FRect option_rect = {20.0f, 28.0f, 96.0f, 8.0f};
      Renderer::instance->draw_rect(this->overlay, &option_rect,
                                    Colour{24, 24, 24, 96},
                                    SDL_BLENDMODE_BLEND);
    }

    Renderer::instance->draw_text(this->overlay, SMALL_FONT, "Volume Settings",
                                  16, 40, 0, this->background);

    Renderer::instance->draw_text(this->overlay, SMALL_FONT, "Sounds", 20, 52,
                                  0, this->background);
    Renderer::instance->draw_text(this->overlay, SMALL_FONT,
                                  std::to_string(Audio::instance->volume), 52,
                                  52, 0, this->background);

    if (this->sel_control != -2) {
      SDL_FRect sound_rect = {20.0f, 52.0f, 96.0f, 8.0f};
      Renderer::instance->draw_rect(this->overlay, &sound_rect,
                                    Colour{24, 24, 24, 96},
                                    SDL_BLENDMODE_BLEND);
      SDL_FRect volume_rect = {52.0f, 52.0f, 64.0f, 8.0f};
      Renderer::instance->draw_rect(this->overlay, &volume_rect,
                                    Colour{24, 24, 24, 128},
                                    SDL_BLENDMODE_BLEND);
    } else {
      SDL_FRect volume_rect = {52.0f, 52.0f, 64.0f, 8.0f};
      Renderer::instance->draw_rect(this->overlay, &volume_rect,
                                    Colour{24, 24, 24, 96},
                                    SDL_BLENDMODE_BLEND);
    }

    Renderer::instance->draw_text(this->overlay, SMALL_FONT, "Music", 20, 60, 0,
                                  this->background);
    Renderer::instance->draw_text(this->overlay, SMALL_FONT,
                                  std::to_string(Audio::instance->music_volume),
                                  52, 60, 0, this->background);

    if (this->sel_control != -1) {
      SDL_FRect music_rect = {20.0f, 60.0f, 96.0f, 8.0f};
      Renderer::instance->draw_rect(this->overlay, &music_rect,
                                    Colour{24, 24, 24, 96},
                                    SDL_BLENDMODE_BLEND);
      SDL_FRect volume_rect = {52.0f, 60.0f, 64.0f, 8.0f};
      Renderer::instance->draw_rect(this->overlay, &volume_rect,
                                    Colour{24, 24, 24, 128},
                                    SDL_BLENDMODE_BLEND);
    } else {
      SDL_FRect volume_rect = {52.0f, 60.0f, 64.0f, 8.0f};
      Renderer::instance->draw_rect(this->overlay, &volume_rect,
                                    Colour{24, 24, 24, 96},
                                    SDL_BLENDMODE_BLEND);
    }

    Renderer::instance->draw_text(this->overlay, SMALL_FONT, "Controls", 16, 72,
                                  0, this->background);

    int start_y = 84;

    for (int i = 0; i < (int)Button::DIGIT; i++) {
      std::string control_name = input->bindings[i].name;
      std::string input_name = this->waiting_for_key && i == this->sel_control
                                   ? "<Press a Key>"
                                   : SDL_GetKeyName(input->bindings[i].keycode);

      int y = start_y + (i * 8);
      Renderer::instance->draw_text(this->overlay, SMALL_FONT, control_name, 20,
                                    y, 0, this->background);
      Renderer::instance->draw_text(this->overlay, SMALL_FONT, input_name, 52,
                                    y, 0, this->background);

      int control_opacity = 128;
      int input_opacity = 192;

      if (i == this->sel_control) {
        if (this->waiting_for_key) {
          control_opacity = 96;
          input_opacity = 0;
        } else {
          control_opacity = 0;
          input_opacity = 96;
        }
      }

      SDL_FRect control_rect = {20.0f, (float)y, 28.0f, 8.0f};
      SDL_FRect input_rect = {52.0f, (float)y, 64.0f, 8.0f};
      Renderer::instance->draw_rect(
          this->overlay, &control_rect,
          Colour{24, 24, 24, uint8_t(control_opacity)}, SDL_BLENDMODE_BLEND);
      Renderer::instance->draw_rect(this->overlay, &input_rect,
                                    Colour{24, 24, 24, uint8_t(input_opacity)},
                                    SDL_BLENDMODE_BLEND);
    }
  }
}

void Hud::draw_peer(bool force_render) {
  if (this->user_show_peer == nullptr ||
      !this->user_show_peer(this->user_data) || Peer::instance == nullptr) {
    return;
  }

  std::string code = Peer::instance->get_connection_code();
  uint64_t peer_state = Peer::instance->get_state();

  if (force_render || peer_state != this->last_peer_state ||
      code != this->last_code ||
      Scene::instance->ticks - this->last_drawn_ticks > 1000) {
    if (this->peer_rect.w > 0.0f) {
      Renderer::instance->draw_rect(this->overlay, &this->peer_rect, kMask,
                                    SDL_BLENDMODE_NONE);
    }

    switch (peer_state) {
      case Peer::kStateNoConnection: {
        std::string text = "Not Connected";
        int text_w = Renderer::instance->fonts[DEFAULT_FONT]->text_width(text);

        this->peer_rect =
            SDL_FRect{8.0f, 8.0f, float(text_w + (PEER_PADDING * 2)), 16.0f};
        Renderer::instance->draw_ui_box(UI_BOX_MENU_CONT, this->overlay,
                                        &this->peer_rect);
        Renderer::instance->draw_text(this->overlay, DEFAULT_FONT, text,
                                      8 + PEER_PADDING, 12, 0,
                                      this->background);
        this->displaying_peer = true;
      } break;
      case Peer::kStateWaitingForPeer: {
        std::string label = "Code:";
        int label_w =
            Renderer::instance->fonts[DEFAULT_FONT]->text_width(label);
        int code_w = Renderer::instance->fonts[CODE_FONT]->text_width(code);
        int width = (PEER_PADDING * 2) + label_w + PEER_GAP + code_w;

        this->peer_rect = SDL_FRect{8.0f, 8.0f, float(width), 16.0f};
        Renderer::instance->draw_ui_box(UI_BOX_MENU_CONT, this->overlay,
                                        &this->peer_rect);
        Renderer::instance->draw_text(this->overlay, DEFAULT_FONT, label,
                                      8 + PEER_PADDING, 12, 0,
                                      this->background);
        Renderer::instance->draw_text(this->overlay, CODE_FONT, code,
                                      8 + PEER_PADDING + label_w + PEER_GAP, 12,
                                      0, this->background);
        this->displaying_peer = true;
      } break;
      default:
        Renderer::instance->clear(this->overlay, kMask);
        this->peer_rect = SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
        this->displaying_peer = false;
        this->displaying_notification = false;
        break;
    }

    this->last_peer_state = peer_state;
    this->last_code = code;
    this->last_drawn_ticks = Scene::instance->ticks;
  }
}

void Hud::draw_notification(bool force_render) {
  if (this->notification.empty()) return;

  if (force_render || this->force_notif_rerender) {
    Renderer::instance->clear(this->overlay, kMask);
    this->displaying_notification = false;
    this->force_notif_rerender = false;

    if (this->displaying_peer && !this->second_pass) {
      this->last_drawn_ticks = 0;
      this->second_pass = true;
      this->render();
      this->second_pass = false;
    }
  }

  if (!this->displaying_notification) {
    int text_w =
        Renderer::instance->fonts[DEFAULT_FONT]->text_width(this->notification);
    SDL_FRect notif_rect = SDL_FRect{8.0f, 8.0f, (float)(text_w + 12), 16.0f};
    int text_y = 12;

    if (this->displaying_peer) {
      notif_rect.y = this->peer_rect.y + this->peer_rect.h + 2.0f;
      text_y = int(notif_rect.y) + 4;
    }

    Renderer::instance->draw_ui_box(UI_BOX_MENU_CONT, this->overlay,
                                    &notif_rect);
    Renderer::instance->draw_text(this->overlay, DEFAULT_FONT,
                                  this->notification, 14, text_y, 0,
                                  this->background);

    this->displaying_notification = true;
  } else if (Scene::instance->ticks - this->notif_ticks > 3000) {
    Renderer::instance->clear(this->overlay, kMask);
    this->notification.clear();
    this->displaying_notification = false;

    if (this->displaying_peer && !this->second_pass) {
      this->last_drawn_ticks = 0;
      this->second_pass = true;
      this->render();
      this->second_pass = false;
    }
  }
}

};  // namespace thoom
