#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace thoom {

class Audio {
 public:
  struct AudioMsg {
    std::string wav_path;
    float gain, x, y;
  };

  inline static Audio* instance = nullptr;

  static bool init();
  static void quit();

  Audio();
  ~Audio();

  void load(const std::string& wav_path);
  void set_listener(float x, float y);
  void play(const std::string& wav_path, float gain, float x, float y,
            bool from_feta);
  void loop(const std::string& wav_path);
  void step();

  std::vector<AudioMsg> messages;

  int volume = 100, music_volume = 100;

 private:
  struct Source {
    SDL_AudioSpec spec;
    Uint8* buf;
    Uint32 len;
  };

  SDL_AudioDeviceID device = 0;
  SDL_AudioSpec spec = {};
  std::vector<SDL_AudioStream*> voices;

  std::unordered_map<std::string, Source> sources;
  Source* ambience = nullptr;
  std::string ambience_path;

  float listener_x = 0.0f, listener_y = 0.0f;
  int current_music_volume = 100;
  bool ready = false;
};

};  // namespace thoom
