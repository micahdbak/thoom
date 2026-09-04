#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace thoom {

void init_playback();
void free_playback();
void load_audio(const std::string& wav_path);
void set_listener(float x, float y);
void play_audio(const std::string& wav_path, float gain, float x, float y,
                bool from_feta);
void loop_audio(const std::string& wav_path);
void ambience_step();

};  // namespace thoom
