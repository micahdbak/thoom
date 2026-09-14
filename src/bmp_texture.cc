#include "bmp_texture.h"

#include <iostream>
#include <unordered_map>

#include "SDL3/SDL_render.h"
#include "SDL3/SDL_surface.h"
#include "game.h"
#include "renderer.h"
#include "utils.h"

namespace thoom {

static std::unordered_map<std::string, SDL_Texture* (*)(const std::string&)>
    render_functions;
static std::unordered_map<std::string, SDL_Texture*> bmp_textures;

static SDL_Texture* render_cheese(const std::string& args) {
  Renderer* renderer = Renderer::instance;

  int amount = 0;
  if (1 != sscanf(args.c_str(), "%d", &amount)) FATAL_ERROR

  SDL_Texture* cheese_texture = load_bmp_texture("sprites/cheese.bmp");

  int w = 18;
  int h = 12 + ((amount - 1) / 4) * 6;

  SDL_Texture* texture = renderer->create_texture(
      w, h, SDL_PIXELFORMAT_RGBA8888, SDL_SCALEMODE_PIXELART);
  if (texture == nullptr) {
    std::cerr << "render_cheese error: " << SDL_GetError() << std::endl;
    exit(1);
  }

  renderer->clear(texture, kMask);

  int x = 1;
  int y = h - 12;
  SDL_FRect src, dst;
  int _amount = amount;

  do {
    int cheese_i = _amount > 3 ? 3 : _amount - 1;
    _amount -= 4;

    src = {float(cheese_i * 16.0f), 0.0f, 16.0f, 12.0f};
    dst = {float(x), float(y), 16.0f, 12.0f};

    renderer->draw_texture(texture, cheese_texture, &src, &dst);

    y -= 6;
    x += SDL_rand(2) ? 1 : -1;
    if (x != THOOM_CLAMP(x, 0, 2)) x = 1;
  } while (_amount > 0);

  return texture;
}

static SDL_Texture* render_credits(const std::string& args) {
  Renderer* renderer = Renderer::instance;

  int ants = 0, drones = 0, tanks = 0, agents = 0, queen_time = 0;
  if (5 != sscanf(args.c_str(), "%d.%d.%d.%d.%d", &ants, &drones, &tanks,
                  &agents, &queen_time))
    FATAL_ERROR

  SDL_Texture* texture = renderer->create_texture(
      THOOM_SCREEN_WIDTH, THOOM_SCREEN_HEIGHT, SDL_PIXELFORMAT_RGBA8888,
      SDL_SCALEMODE_PIXELART);
  if (texture == nullptr) {
    std::cerr << "render_credits error: " << SDL_GetError() << std::endl;
    exit(1);
  }

  renderer->clear(texture, kMask);

  renderer->draw_text(texture, game->fonts[TITLE_FONT], "Cheddar & Feta", 120,
                      24, 0, Colour(24, 24, 24, 255));
  std::string results =
      std::string("}} Results }}\n\n") + "Ant Kills:\t" + std::to_string(ants) +
      "\tDrone Kills:\t" + std::to_string(drones) + "\n" + "Tank Kills:\t" +
      std::to_string(tanks) + "\tAgent Kills:\t" + std::to_string(agents) +
      "\n\n" + "Time to Kill the Queen:\t\t" + std::to_string(queen_time) +
      " (s)\n" + "Total Time:\t\t\t" + std::to_string(game->ticks / 1000) +
      " (s)\n\n" + "}} Credits }}\n\n" + "Made with { by Micah Baker\n\n" +
      "Thanks for playing!\n(Press [Enter] to return to Main Menu.)";
  renderer->draw_text(texture, game->fonts[DEFAULT_FONT], results, 64, 40, 0,
                      Colour(24, 24, 24, 255));

  return texture;
}

void load_render_functions() {
  render_functions[FUNC_CHEESE] = &render_cheese;
  render_functions[FUNC_CREDITS] = &render_credits;
}

SDL_Texture* load_bmp_texture(const std::string& bmp_path) {
  if (bmp_textures.find(bmp_path) != bmp_textures.end()) {
    return bmp_textures[bmp_path];
  }

  char buff[1024];
  sscanf(bmp_path.c_str(), "%1023[^/]", buff);
  buff[1023] = '\0';

  if (std::string(buff) == "render") {
    size_t offset = strlen(buff) + 1;  // + 1 for '/'
    sscanf(bmp_path.c_str() + offset, "%1023[^/]", buff);
    buff[1023] = '\0';

    if (render_functions.find(buff) == render_functions.end()) {
      std::cerr << "load_bmp_texture error: render function " << buff
                << " does not exist." << std::endl;
      std::exit(1);
    }

    char args[1024];
    offset += strlen(buff) + 1;
    sscanf(bmp_path.c_str() + offset, "%1023[^/]", args);
    args[1023] = '\0';

    SDL_Texture* texture = render_functions[buff](args);
    bmp_textures[bmp_path] = texture;
    return texture;
  }

  SDL_Surface* surface = SDL_LoadBMP(bmp_path.c_str());
  if (surface == nullptr) {
    std::cerr << "SDL_LoadBMP error: " << bmp_path << " does not exist."
              << std::endl;
    exit(1);
  }

  SDL_Texture* texture = Renderer::instance->create_texture_from_surface(
      surface, SDL_SCALEMODE_NEAREST);
  SDL_DestroySurface(surface);
  bmp_textures[bmp_path] = texture;
  return texture;
}

void free_textures() {
  for (auto pair : bmp_textures) {
    SDL_DestroyTexture(pair.second);
  }
}

std::string credits_args(int ants, int drones, int tanks, int agents,
                         int queen_time) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%d.%d.%d.%d.%d", ants, drones, tanks, agents,
           queen_time);
  return std::string(buf);
}

};  // namespace thoom
