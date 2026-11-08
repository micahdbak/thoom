#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

#include "colour.h"
#include "thoom/thoom.h"

namespace thoom {

class Hud {
 public:
  static bool init(uint32_t background, const struct hud_def& def);
  static void quit();

  inline static Hud* instance = nullptr;

  Hud(uint32_t background, const struct hud_def& def);
  ~Hud();

  void render();
  void draw();

  void display_notification(const std::string& text);

  SDL_Texture* ui = nullptr;
  SDL_Texture* overlay = nullptr;

 private:
  void draw_controls_menu();
  void draw_peer(bool force_render);
  void draw_notification(bool force_render);

  void (*user_draw)(void* user) = nullptr;
  bool (*user_show_peer)(void* user) = nullptr;
  bool (*user_show_hud)(void* user) = nullptr;
  void (*user_exit_to_main_menu)(void* user) = nullptr;
  void* user_data = nullptr;

  Colour background;
  std::string current_map;
  bool display_controls_menu = false;
  bool displaying_controls_menu = false;
  bool waiting_for_key = false;
  int sel_control = -3;

  uint64_t last_peer_state = 0;
  std::string last_code;
  uint64_t last_drawn_ticks = 0;
  bool displaying_peer = false;
  SDL_FRect peer_rect = {0.0f, 0.0f, 0.0f, 0.0f};

  std::string notification;
  uint64_t notif_ticks = 0;
  bool force_notif_rerender = false;
  bool displaying_notification = false;
  bool second_pass = false;
};

};  // namespace thoom
