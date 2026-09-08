#include <SDL3/SDL.h>

#include <iostream>

#include "SDL3/SDL_blendmode.h"
#include "bmp_texture.h"
#include "controller.h"
#include "font.h"
#include "game.h"
#include "map.h"
#include "renderer.h"
#include "save_data.h"
#include "utils.h"

namespace thoom {

void Game::draw_icon(SDL_Texture* texture, SDL_FRect icon,
                     SDL_FRect* dst_rect) {
  Renderer::instance->draw_texture(texture, this->icons, &icon, dst_rect);
}

#define ITEM_NONE "item_none"

void Game::draw_hud(SDL_Texture* texture, std::vector<Game::HudItem> items,
                    int sel_item, int health, int max_health) {
  Renderer* renderer = Renderer::instance;
  static SDL_FRect item_list_rect = {0.0f, 0.0f, 0.0f, 0.0f};

  // container ui box
  SDL_FRect hud_rect = {HUD_MAIN_X, HUD_MAIN_Y, 18.0f, 48.0f};
  renderer->draw_ui_box(this->ui_box, BOX_MENU_CONT, texture, &hud_rect);
  renderer->draw_text(texture, this->fonts[SMALL_FONT], "HP:", HUD_MAIN_X + 5,
                      HUD_MAIN_Y + 4, 0, Colour(24, 24, 24, 255));

  SDL_FRect health_rect = {(float)(HUD_MAIN_X + 5), (float)(HUD_MAIN_Y + 11),
                           8.0f, 32.0f};
  int fill_px = ceil(30.0f * (float)health / (float)max_health) + 0.5f;
  SDL_FRect fill_rect = {(float)(HUD_MAIN_X + 6),
                         (float)(HUD_MAIN_Y + 12 + 30 - fill_px), 6.0f,
                         (float)fill_px};
  renderer->draw_rect(texture, &health_rect, Colour{52, 48, 48, 255},
                      SDL_BLENDMODE_NONE);
  renderer->draw_rect(texture, &fill_rect, Colour{240, 80, 64, 255},
                      SDL_BLENDMODE_NONE);

  std::string current_item = ITEM_NONE;
  int item_count = 1;

  if (sel_item >= 0 && sel_item < items.size()) {
    current_item = items[sel_item].item_id;
    item_count = items[sel_item].count;
  }

  SDL_FRect item_src_rect = {0.0f, 0.0f, 16.0f, 16.0f};
  SDL_Texture* item_texture;

  // clear last item listing (possibly a different size, if a new item was
  // picked up intermittently)
  SDL_FRect clear_rect = item_list_rect;
  clear_rect.y -= 4.0f;
  clear_rect.h += 10.0f;
  renderer->draw_rect(texture, &clear_rect, kMask, SDL_BLENDMODE_NONE);

  item_list_rect.x = HUD_ITEMS_X;
  item_list_rect.y = HUD_ITEMS_Y;
  item_list_rect.w = float((items.size() + 1) * 18 + 8);
  item_list_rect.h = 24.0f;

  renderer->draw_ui_box(this->ui_box, BOX_CONTAINER, texture, &item_list_rect);

  int sel_offset = HUD_ITEMS_X + 5 + 18 * (sel_item + 1);
  SDL_FRect sel_outline_rect = {(float)(sel_offset - 1),
                                (float)(HUD_ITEMS_Y + 3), 18.0f, 18.0f};
  renderer->draw_ui_box(this->ui_box, BOX_OUT_SEL, texture, &sel_outline_rect);

  for (int i = -1; i < (int)items.size(); i++) {
    int x_offset = HUD_ITEMS_X + 5 + 18 * (i + 1);
    SDL_FRect item_rect = {(float)x_offset, (float)(HUD_ITEMS_Y + 4), 16.0f,
                           16.0f};

    std::string item_id = i < 0 ? ITEM_NONE : items[i].item_id;
    item_texture = load_bmp_texture("sprites/" + item_id + ".bmp");
    renderer->draw_texture(texture, item_texture, &item_src_rect, &item_rect);

    SDL_FRect hint_rect = {float(x_offset), HUD_ITEMS_Y + 15, 5.0f, 7.0f};
    // renderer->draw_rect(texture, &hint_rect, Colour{24, 24, 24, 128},
    //                     SDL_BLENDMODE_BLEND);
    renderer->draw_text(texture, this->fonts[SMALL_FONT], std::to_string(i + 2),
                        x_offset, HUD_ITEMS_Y + 15, 0, Colour(24, 24, 24, 255));

    int this_item_count = i >= 0 ? items[i].count : 1;
    if (this_item_count > 1) {
      SDL_FRect item_count_icon = {(float)(x_offset + 8),
                                   (float)(HUD_ITEMS_Y - 4), 16.0f, 16.0f};
      this->draw_icon(texture,
                      i == sel_item ? ITEM_COUNT_ICON : ITEM_COUNT_ICON_SHD,
                      &item_count_icon);
      renderer->draw_text(texture, this->fonts[SMALL_FONT],
                          std::to_string(this_item_count),
                          this_item_count > 9 ? x_offset + 11 : x_offset + 13,
                          HUD_ITEMS_Y - 1, 0, Colour(24, 24, 24, 255));
    }
  }
}

// static SDL_FRect _title_rect = SDL_FRect{ 0.0f, 20.0f, 320.0f, 40.0f };
static NetworkAgent::State _last_state = NetworkAgent::State::NO_CONNECTION;
static std::string _last_code = "";
static Uint64 _last_drawn_ticks = 0;
static SDL_FRect _netagent_rect = SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
static SDL_FRect _neticon_rect = SDL_FRect{10.0f, 8.0f, 16.0f, 16.0f};
static bool _displaying_netagent = false;
static bool _displaying_notification = false;
static bool _displaying_controls_menu = false;
static bool _second_pass = false;
static int _sel_control = -3;
static bool _waiting_for_key = false;

void Game::draw_overlay() {
  Renderer* renderer = Renderer::instance;
  bool force_render = false;

  if (this->display_controls_menu) {
    bool should_render = !_displaying_controls_menu;
    _displaying_controls_menu = true;

    if (_waiting_for_key) {
      if (captured_controller.last_input != SDLK_UNKNOWN) {
        SDL_Keycode new_key = captured_controller.last_input;
        Button sel_button = (Button)_sel_control;

        if (ktobutton_map.find(new_key) != ktobutton_map.end()) {
          SDL_Keycode old_key = btokeycode_map[sel_button];
          Button existing_button = ktobutton_map[new_key];

          btokeycode_map[existing_button] = old_key;
          ktobutton_map[old_key] = existing_button;
        } else {
          SDL_Keycode old_key = btokeycode_map[sel_button];
          ktobutton_map.erase(old_key);
        }

        btokeycode_map[sel_button] = new_key;
        ktobutton_map[new_key] = sel_button;

        should_render = true;
        _waiting_for_key = false;
      }
    } else {
      int y_dir =
          captured_controller.is_hit(DOWN) - captured_controller.is_hit(UP);
      int new_sel_control =
          THOOM_CLAMP(_sel_control + y_dir, -3, (int)Button::DIGIT - 1);

      if (captured_controller.is_hit(Button::SELECT) && _sel_control == -3) {
        game->map = "maps/init";
        save.clear();

        // return from this menu
        capture_controls = false;
        this->display_controls_menu = false;
        captured_controller.clear_all();

        return;
      }

      int x_dir =
          captured_controller.is_hit(RIGHT) - captured_controller.is_hit(LEFT);
      if (_sel_control == -2 && x_dir != 0) {
        game->volume = THOOM_CLAMP(game->volume + (10 * x_dir), 0, 200);
        should_render = true;
      } else if (_sel_control == -1 && x_dir != 0) {
        game->music_volume =
            THOOM_CLAMP(game->music_volume + (10 * x_dir), 0, 200);
        should_render = true;
      }

      if (_sel_control != new_sel_control) {
        _sel_control = new_sel_control;
        should_render = true;
      } else if (captured_controller.is_hit(Button::SELECT) &&
                 _sel_control >= 0) {
        captured_controller.last_input = SDLK_UNKNOWN;
        _waiting_for_key = true;
        should_render = true;
      }
    }

    if (should_render) {
      renderer->clear(this->overlay,
                      Colour{this->bg_r, this->bg_g, this->bg_b, 128});

      SDL_FRect config_rect = {8.0f, 8.0f, 112.0f, 168.0f};
      renderer->draw_ui_box(this->ui_box, BOX_CONTAINER, this->overlay,
                            &config_rect);

      renderer->draw_text(this->overlay, this->fonts[SMALL_FONT], "Game", 16,
                          16, 0, Colour(24, 24, 24, 255));

      renderer->draw_text(this->overlay, this->fonts[SMALL_FONT],
                          "Exit to Main Menu", 20, 28, 0,
                          Colour(24, 24, 24, 255));
      if (_sel_control != -3) {
        SDL_FRect option_rect = {20.0f, 28.0f, 96.0f, 8.0f};
        renderer->draw_rect(this->overlay, &option_rect, Colour{24, 24, 24, 96},
                            SDL_BLENDMODE_BLEND);
      }

      renderer->draw_text(this->overlay, this->fonts[SMALL_FONT],
                          "Volume Settings", 16, 40, 0,
                          Colour(24, 24, 24, 255));

      renderer->draw_text(this->overlay, this->fonts[SMALL_FONT], "Sounds", 20,
                          52, 0, Colour(24, 24, 24, 255));
      renderer->draw_text(this->overlay, this->fonts[SMALL_FONT],
                          std::to_string(game->volume), 52, 52, 0,
                          Colour(24, 24, 24, 255));
      if (_sel_control != -2) {
        SDL_FRect sound_rect = {20.0f, 52.0f, 96.0f, 8.0f};
        renderer->draw_rect(this->overlay, &sound_rect, Colour{24, 24, 24, 96},
                            SDL_BLENDMODE_BLEND);
        SDL_FRect volume_rect = {52.0f, 52.0f, 64.0f, 8.0f};
        renderer->draw_rect(this->overlay, &volume_rect,
                            Colour{24, 24, 24, 128}, SDL_BLENDMODE_BLEND);
      } else {
        SDL_FRect volume_rect = {52.0f, 52.0f, 64.0f, 8.0f};
        renderer->draw_rect(this->overlay, &volume_rect, Colour{24, 24, 24, 96},
                            SDL_BLENDMODE_BLEND);
      }

      renderer->draw_text(this->overlay, this->fonts[SMALL_FONT], "Music", 20,
                          60, 0, Colour(24, 24, 24, 255));
      renderer->draw_text(this->overlay, this->fonts[SMALL_FONT],
                          std::to_string(game->music_volume), 52, 60, 0,
                          Colour(24, 24, 24, 255));
      if (_sel_control != -1) {
        SDL_FRect music_rect = {20.0f, 60.0f, 96.0f, 8.0f};
        renderer->draw_rect(this->overlay, &music_rect, Colour{24, 24, 24, 96},
                            SDL_BLENDMODE_BLEND);
        SDL_FRect volume_rect = {52.0f, 60.0f, 64.0f, 8.0f};
        renderer->draw_rect(this->overlay, &volume_rect,
                            Colour{24, 24, 24, 128}, SDL_BLENDMODE_BLEND);
      } else {
        SDL_FRect volume_rect = {52.0f, 60.0f, 64.0f, 8.0f};
        renderer->draw_rect(this->overlay, &volume_rect, Colour{24, 24, 24, 96},
                            SDL_BLENDMODE_BLEND);
      }

      renderer->draw_text(this->overlay, this->fonts[SMALL_FONT], "Controls",
                          16, 72, 0, Colour(24, 24, 24, 255));

      int start_y = 84;

      for (int i = 0; i < (int)Button::DIGIT; i++) {
        std::string control_name = btostring_map[(Button)i];
        std::string input_name =
            _waiting_for_key && i == _sel_control
                ? "<Press a Key>"
                : SDL_GetKeyName(btokeycode_map[(Button)i]);

        int y = start_y + (i * 8);
        renderer->draw_text(this->overlay, this->fonts[SMALL_FONT],
                            control_name, 20, y, 0, Colour(24, 24, 24, 255));
        renderer->draw_text(this->overlay, this->fonts[SMALL_FONT], input_name,
                            52, y, 0, Colour(24, 24, 24, 255));

        int control_opacity = 128;
        int input_opacity = 192;
        if (i == _sel_control) {
          if (_waiting_for_key) {
            control_opacity = 96;
            input_opacity = 0;
          } else {
            control_opacity = 0;
            input_opacity = 96;
          }
        }

        SDL_FRect control_rect = {20.0f, (float)y, 28.0f, 8.0f};
        SDL_FRect input_rect = {52.0f, (float)y, 64.0f, 8.0f};
        renderer->draw_rect(this->overlay, &control_rect,
                            Colour{24, 24, 24, uint8_t(control_opacity)},
                            SDL_BLENDMODE_BLEND);
        renderer->draw_rect(this->overlay, &input_rect,
                            Colour{24, 24, 24, uint8_t(input_opacity)},
                            SDL_BLENDMODE_BLEND);
      }
    }

    return;  // don't display anything else
  } else if (_displaying_controls_menu) {
    renderer->clear(this->overlay, kMask);
    _displaying_controls_menu = false;
    force_render = true;
  }

  if (this->current_map == "maps/splash" || this->current_map == "maps/init" ||
      this->current_map == "maps/dead" || this->current_map == "maps/credits") {
    if (!force_render) {
      force_render = true;
      renderer->clear(this->overlay, kMask);
    }

    return;
  }

  // network agent overlay (only cheddar can "create objects" so that is used to
  // check if cheddar)
  if (game->create_objects && net_agent != nullptr) {
    std::string code = net_agent->get_connection_code();

    // draw if state changed, code changed, or if a second has passed since last
    // rendered
    if (force_render || this->net_state != _last_state || code != _last_code ||
        this->ticks - _last_drawn_ticks > 1000) {
      // clear last overlay
      if (_netagent_rect.w > 0.0f) {
        renderer->draw_rect(this->overlay, &_netagent_rect, kMask,
                            SDL_BLENDMODE_NONE);
      }

      switch (this->net_state) {
        case NetworkAgent::State::NO_CONNECTION:
          _netagent_rect = SDL_FRect{8.0f, 8.0f, 88.0f, 16.0f};
          renderer->draw_ui_box(this->ui_box, BOX_MENU_CONT, this->overlay,
                                &_netagent_rect);
          this->draw_icon(this->overlay, NOT_CONNECTED_ICON, &_neticon_rect);
          renderer->draw_text(this->overlay, this->fonts[DEFAULT_FONT],
                              "Not Connected", 28, 12, 0,
                              Colour(24, 24, 24, 255));
          _displaying_netagent = true;
          break;
        case NetworkAgent::State::WAITING_FOR_PEER:
          _netagent_rect = SDL_FRect{8.0f, 8.0f, 80.0f, 16.0f};
          renderer->draw_ui_box(this->ui_box, BOX_MENU_CONT, this->overlay,
                                &_netagent_rect);
          this->draw_icon(this->overlay, WAITING_FOR_PEER_ICON, &_neticon_rect);
          renderer->draw_text(this->overlay, this->fonts[SMALL_FONT],
                              "Code:", 28, 13, 0, Colour(24, 24, 24, 255));
          renderer->draw_text(this->overlay, this->fonts[CODE_FONT], code, 48,
                              12, 0, Colour(24, 24, 24, 255));
          _displaying_netagent = true;
          break;
        default:
          // clear entire overlay screen
          renderer->clear(this->overlay, kMask);
          _netagent_rect = SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};  // no dst rect
          _displaying_netagent = false;
          _displaying_notification = false;
          break;  // display nothing
      }

      _last_state = this->net_state;
      _last_code = code;
      _last_drawn_ticks = this->ticks;
    }
  }

  if (!this->notification.empty()) {
    if (force_render || this->force_notif_rerender) {
      renderer->clear(this->overlay, kMask);
      _displaying_notification = false;
      this->force_notif_rerender = false;

      if (_displaying_netagent && !_second_pass) {
        _last_drawn_ticks = 0;
        _second_pass = true;
        this->draw_overlay();
        _second_pass = false;
      }
    }

    if (!_displaying_notification) {
      int text_w = this->fonts[DEFAULT_FONT]->text_width(this->notification);
      SDL_FRect notif_rect = SDL_FRect{8.0f, 8.0f, (float)(text_w + 12), 16.0f};
      int text_y = 12;

      if (_displaying_netagent) {
        notif_rect.y = 26.0f;
        text_y = 30;
      }

      renderer->draw_ui_box(this->ui_box, BOX_MENU_CONT, this->overlay,
                            &notif_rect);
      renderer->draw_text(this->overlay, this->fonts[DEFAULT_FONT],
                          this->notification, 14, text_y, 0,
                          Colour(24, 24, 24, 255));

      _displaying_notification = true;
    } else if (this->ticks - this->notif_ticks > 3000) {
      renderer->clear(this->overlay, kMask);
      this->notification.clear();
      _displaying_notification = false;

      if (_displaying_netagent && !_second_pass) {
        _last_drawn_ticks = 0;
        _second_pass = true;
        this->draw_overlay();
        _second_pass = false;
      }
    }
  }
}

};  // namespace thoom
