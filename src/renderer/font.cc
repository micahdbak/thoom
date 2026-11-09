#include "renderer/font.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/log/log.h"
#include "texture/texture.h"

namespace thoom {

static const unsigned char kMonoFont[] = {
#embed "fonts/mono.bmp"
};

static const unsigned char kSmallFont[] = {
#embed "fonts/small.bmp"
};

static const unsigned char kSmallBoldFont[] = {
#embed "fonts/small_bold.bmp"
};

static const unsigned char kDefaultFont[] = {
#embed "fonts/default.bmp"
};

static const unsigned char kBoldFont[] = {
#embed "fonts/bold.bmp"
};

static const unsigned char kCodeFont[] = {
#embed "fonts/code.bmp"
};

static const unsigned char kCodeGrayFont[] = {
#embed "fonts/code_gray.bmp"
};

static const unsigned char kTitleFont[] = {
#embed "fonts/title.bmp"
};

static const unsigned char kControlsFont[] = {
#embed "fonts/controls.bmp"
};

bool Font::load_fonts(std::vector<Font*>& fonts) {
  if (Texture::instance == nullptr) {
    LOG(ERROR) << "Font::load_fonts error: texture not initialized";
    return false;
  }

  // MONO_FONT = 0
  fonts.push_back(new Font(kMonoFont, sizeof(kMonoFont), 6, 10, 6, {}));

  // SMALL_FONT = 1
  fonts.push_back(new Font(
      kSmallFont, sizeof(kSmallFont), 6, 7, 4,
      {{' ', 3}, {'!', 2}, {'#', 6}, {'%', 5}, {'&', 5}, {'\'', 3}, {'*', 6},
       {',', 3}, {'.', 2}, {':', 2}, {';', 3}, {'<', 6}, {'>', 6},  {'@', 5},
       {'M', 6}, {'N', 5}, {'W', 6}, {'^', 6}, {'`', 3}, {'i', 2},  {'j', 3},
       {'l', 2}, {'m', 6}, {'w', 6}, {'{', 6}, {'|', 6}, {'}', 6},  {'~', 7}}));

  // SM_BOLD_FONT = 2
  fonts.push_back(new Font(
      kSmallBoldFont, sizeof(kSmallBoldFont), 6, 7, 5,
      {{' ', 3}, {'!', 2}, {'"', 4}, {'#', 6}, {'$', 4}, {'\'', 3}, {'(', 4},
       {')', 4}, {'*', 6}, {'+', 4}, {',', 3}, {'.', 2}, {':', 2},  {';', 3},
       {'<', 6}, {'>', 6}, {'M', 6}, {'N', 6}, {'W', 6}, {'^', 6},  {'`', 3},
       {'i', 3}, {'j', 4}, {'l', 3}, {'m', 6}, {'w', 6}, {'x', 6},  {'{', 6},
       {'|', 6}, {'}', 6}, {'~', 7}}));

  // DEFAULT_FONT = 3
  fonts.push_back(new Font(
      kDefaultFont, sizeof(kDefaultFont), 6, 10, 5,
      {{' ', 3}, {'!', 2}, {'"', 4}, {'#', 6}, {'\'', 3}, {'(', 4}, {')', 4},
       {'*', 4}, {'+', 6}, {',', 3}, {'.', 2}, {'1', 4},  {':', 2}, {';', 3},
       {'I', 4}, {'J', 4}, {'M', 6}, {'V', 6}, {'W', 6},  {'X', 6}, {'[', 4},
       {']', 4}, {'^', 4}, {'`', 3}, {'i', 2}, {'j', 3},  {'l', 2}, {'m', 6},
       {'v', 6}, {'w', 6}, {'x', 6}, {'{', 6}, {'|', 6},  {'}', 6}}));

  // BOLD_FONT = 4
  fonts.push_back(new Font(
      kBoldFont, sizeof(kBoldFont), 7, 10, 6,
      {{' ', 4}, {'!', 2}, {'"', 4},  {'$', 5}, {'%', 5}, {'&', 5}, {'\'', 3},
       {'(', 4}, {')', 4}, {'*', 4},  {',', 3}, {'.', 2}, {'/', 5}, {'1', 5},
       {':', 2}, {';', 3}, {'?', 5},  {'I', 5}, {'J', 5}, {'M', 7}, {'N', 7},
       {'T', 5}, {'[', 4}, {'\\', 5}, {']', 4}, {'^', 4}, {'`', 3}, {'i', 5},
       {'j', 4}, {'l', 5}, {'m', 7},  {'w', 7}}));

  // CODE_FONT = 5
  fonts.push_back(new Font(kCodeFont, sizeof(kCodeFont), 6, 10, 6, {}));

  // CODE_GRAY_FONT = 6
  fonts.push_back(new Font(kCodeGrayFont, sizeof(kCodeGrayFont), 6, 10, 6, {}));

  // TITLE_FONT = 7
  fonts.push_back(new Font(
      kTitleFont, sizeof(kTitleFont), 8, 13, 6,
      {{' ', 4}, {'\'', 4}, {',', 4}, {'A', 8}, {'B', 7}, {'C', 7}, {'D', 7},
       {'G', 7}, {'J', 7},  {'K', 7}, {'M', 8}, {'N', 7}, {'O', 7}, {'P', 7},
       {'Q', 7}, {'R', 7},  {'S', 7}, {'U', 7}, {'V', 7}, {'W', 8}, {'X', 8},
       {'Y', 8}, {'Z', 7},  {'f', 5}, {'i', 4}, {'j', 3}, {'l', 4}, {'m', 8},
       {'r', 5}, {'t', 5},  {'w', 8}}));

  // CONTROLS_FONT = 6
  fonts.push_back(
      new Font(kControlsFont, sizeof(kControlsFont), 8, 16, 8, {{'/', 5}}));

  for (Font* font : fonts) {
    if (font->texture == nullptr) {
      return false;
    }
  }

  return true;
}

Font::Font(const void* data, size_t size, int w, int h, int default_w,
           const std::unordered_map<char, int>& special_w) {
  if (Texture::instance == nullptr) {
    LOG(ERROR) << "Font::Font error: texture not initialized";
    return;
  }

  SDL_IOStream* stream = SDL_IOFromConstMem(data, size);
  if (stream == nullptr) {
    LOG(ERROR) << "Font::Font error: SDL_IOFromConstMem: " << SDL_GetError();
    return;
  }

  SDL_Surface* font_surface = SDL_LoadBMP_IO(stream, true);
  if (font_surface == nullptr) {
    LOG(ERROR) << "Font::Font error: SDL_LoadBMP_IO: " << SDL_GetError();
    return;
  }

  this->texture = Texture::instance->create_texture_from_surface(
      font_surface, SDL_SCALEMODE_NEAREST);
  SDL_DestroySurface(font_surface);

  // create all src_rect's
  for (int i = 0; i < NUM_DISPLAYABLE_CHARS; i++) {
    this->src_rect[i].x = float((i % FONT_SHEET_COLS) * w);
    this->src_rect[i].y = float((i / FONT_SHEET_COLS) * h);
    this->src_rect[i].w = float(default_w);
    this->src_rect[i].h = float(h);

    // see if this character has a special width
    char c = char(i) + ' ';
    if (special_w.find(c) != special_w.end()) {
      this->src_rect[i].w = float(special_w.at(c));
    }
  }
}

Font::~Font() {
  if (this->texture != nullptr) {
    SDL_DestroyTexture(this->texture);
    this->texture = nullptr;
  }
}

int Font::text_width(const std::string& text) {
  int w = 0;

  for (char c : text) {
    int i = c - ' ';
    if (i < 0 || i >= NUM_DISPLAYABLE_CHARS) continue;
    w += int(this->src_rect[i].w + 0.1f);
  }

  return w;
}

};  // namespace thoom
