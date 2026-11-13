#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "audio/audio.h"
#include "colour.h"
#include "editor.h"
#include "editor_ui.h"
#include "hud/hud.h"
#include "input/input.h"
#include "map/map.h"
#include "object/object.h"
#include "renderer/renderer.h"
#include "scene/scene.h"
#include "thoom/log.h"
#include "thoom/thoom.h"
#include "window/window.h"

bool init(int argc, char** argv);

int main(int argc, char** argv) {
  if (argc < 2) {
    thoom_log_error("Please provide a map to edit via the command line.");
    return 1;
  }

  std::string bin_path = std::string(argv[1]) + ".bin";
  FILE* map_file = std::fopen(bin_path.c_str(), "rb");

  if (map_file != nullptr) {
    std::fclose(map_file);
  } else if (argc < 7) {
    thoom_log_error(
        "To create a new map please provide: <map name> <tw> "
        "<th> <cols> <rows> <fill(0/1)> <fillts(opt)>.");
    return 1;
  }

  struct hud_def hud = {};

  if (!thoom_init("Thoom Engine", THOOM_SCREEN_WIDTH, THOOM_SCREEN_HEIGHT,
                  kBackground.val, &hud)) {
    thoom_log_error("map_editor: could not initialize thoom");
    return 1;
  }

  if (!thoom::Scene::init("", true)) {
    thoom_log_error("map_editor: could not initialize the scene");
    thoom_quit();
    return 1;
  }

  if (!init(argc, argv)) {
    thoom_log_error("map_editor: editor init failed");
    thoom::Scene::quit();
    thoom_quit();
    return 1;
  }

  bool running = true;

  while (running) {
    bool quit = false;
    thoom_poll_events(&quit);

    if (quit) {
      running = false;
      break;
    }

    thoom::Audio::instance->step();

    uint64_t ticks = thoom::Scene::instance->ticks;
    thoom::Scene::instance->step();

    thoom::Map* map = thoom::Scene::instance->map;
    thoom::Renderer::instance->draw_world(map);
    thoom::Hud::instance->render();
    thoom::Hud::instance->draw();

    if (thoom::Input::instance->world.character == 'p') {
      thoom::Window::instance->save_screenshot("screenshot.bmp");
    }

    thoom::Colour bg = map != nullptr ? map->background : thoom::kBlack;
    thoom::Window::instance->present(bg);

    if (thoom::Scene::instance->ticks != ticks) {
      thoom::Input::instance->clear_hit_input();
    }
  }

  thoom::Scene::quit();

  thoom_quit();

  return 0;
}

bool init(int argc, char** argv) {
  if (argc < 2) {
    thoom_log_error("Please provide a map to edit via the command line.");
    return false;
  }

  std::string options;
  for (int i = 1; i < argc; i++) {
    if (i > 1) options += '\n';
    options += argv[i];
  }

  thoom::ObjectDef editor;
  editor.id = "editor";
  editor.create_object = Editor::create_object;
  editor.order = thoom::kObjectNormal;
  thoom::Scene::instance->register_object(editor);
  thoom::Scene::instance->create_object("editor", options);

  std::string title = "Map Editor - " + std::string(argv[1]);
  thoom::Window::instance->set_title(title.c_str());

  return true;
}
