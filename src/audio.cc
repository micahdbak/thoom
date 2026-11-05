#include "audio.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "utils.h"

#define NUM_VOICES 32
#define IDLE_THRESHOLD 4096      // 4KiB
#define LOOP_THRESHOLD 4096 * 8  // 32KiB

// Audio within LISTEN_NEAR px of the listener is at full volume. Between
// LISTEN_NEAR and LISTEN_FAR it fades out. Beyond LISTEN_FAR it is silent.
#define LISTEN_NEAR 160.0f
#define LISTEN_FAR 320.0f

#define AUDIO_FAIL(_context)                                        \
  do {                                                              \
    std::cerr << (_context) << ": " << SDL_GetError() << std::endl; \
    std::exit(1);                                                   \
  } while (0)

namespace thoom {

void Audio::init() {
  if (Audio::instance != nullptr) {
    std::cerr << "Audio::init error: audio already initialized" << std::endl;
    std::exit(1);
  }

  Audio::instance = new Audio();
}

void Audio::shutdown() {
  if (Audio::instance == nullptr) {
    return;
  }

  delete Audio::instance;
  Audio::instance = nullptr;
}

Audio::Audio() {
  this->device =
      SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);

  if (this->device == 0) {
    AUDIO_FAIL("Audio::Audio: SDL_OpenAudioDevice");
  }

  if (!SDL_GetAudioDeviceFormat(this->device, &this->spec, nullptr)) {
    AUDIO_FAIL("Audio::Audio: SDL_GetAudioDeviceFormat");
  }

  this->voices.resize(NUM_VOICES, nullptr);

  for (int i = 0; i < NUM_VOICES; i++) {
    this->voices[i] = SDL_CreateAudioStream(&this->spec, &this->spec);

    if (this->voices[i] == nullptr) {
      AUDIO_FAIL("Audio::Audio: SDL_CreateAudioStream");
    }

    SDL_BindAudioStream(this->device, this->voices[i]);
  }
}

Audio::~Audio() {
  if (this->device != 0) {
    SDL_CloseAudioDevice(this->device);
    this->device = 0;
  }

  for (SDL_AudioStream*& voice : this->voices) {
    if (voice != nullptr) {
      SDL_DestroyAudioStream(voice);
      voice = nullptr;
    }
  }

  this->voices.clear();

  for (auto& pair : this->sources) {
    SDL_free(pair.second.buf);
  }

  this->sources.clear();
}

void Audio::load(const std::string& wav_path) {
  if (wav_path.empty() || this->sources.find(wav_path) != this->sources.end()) {
    return;
  }

  Source source;

  if (!SDL_LoadWAV(wav_path.c_str(), &source.spec, &source.buf, &source.len)) {
    AUDIO_FAIL("Audio::load: SDL_LoadWAV");
  }

  this->sources[wav_path] = source;
}

void Audio::set_listener(float x, float y) {
  this->listener_x = x;
  this->listener_y = y;
}

void Audio::play(const std::string& wav_path, float gain, float x, float y,
                 bool from_feta) {
  auto it = this->sources.find(wav_path);

  if (it == this->sources.end()) {
    return;
  }

  if (!from_feta) {
    this->messages.push_back(AudioMsg{wav_path, gain, x, y});
  }

  float dist =
      distance_between_points(this->listener_x, this->listener_y, x, y);

  if (dist > LISTEN_FAR) {
    return;
  }

  if (dist > LISTEN_NEAR) {
    float mult = (dist - LISTEN_NEAR) / (LISTEN_FAR - LISTEN_NEAR);
    gain *= 1.0f - mult;
  }

  gain *= (float)this->volume / 200.0f;

  Source& source = it->second;

  // voices[0] is reserved for ambience
  for (int i = 1; i < NUM_VOICES; i++) {
    if (SDL_GetAudioStreamQueued(this->voices[i]) > IDLE_THRESHOLD) {
      continue;
    }

    SDL_ClearAudioStream(this->voices[i]);

    if (!SDL_SetAudioStreamFormat(this->voices[i], &source.spec, nullptr) ||
        !SDL_SetAudioStreamGain(this->voices[i], gain) ||
        !SDL_PutAudioStreamData(this->voices[i], source.buf, source.len)) {
      AUDIO_FAIL("Audio::play");
    }

    break;
  }
}

void Audio::loop(const std::string& wav_path) {
  if (wav_path == this->ambience_path) {
    return;
  }

  this->ambience_path = wav_path;
  SDL_ClearAudioStream(this->voices[0]);

  if (wav_path.empty()) {
    this->ambience = nullptr;
    return;
  }

  this->load(wav_path);

  auto it = this->sources.find(wav_path);

  if (it == this->sources.end()) {
    return;
  }

  this->ambience = &it->second;
  float music_gain = (float)this->music_volume / 200.0f;

  if (!SDL_SetAudioStreamFormat(this->voices[0], &this->ambience->spec,
                                nullptr) ||
      !SDL_SetAudioStreamGain(this->voices[0], music_gain) ||
      !SDL_PutAudioStreamData(this->voices[0], this->ambience->buf,
                              this->ambience->len)) {
    AUDIO_FAIL("Audio::loop");
  }
}

void Audio::step() {
  this->messages.clear();

  float music_gain = (float)this->music_volume / 200.0f;

  if (this->current_music_volume != this->music_volume) {
    if (!SDL_SetAudioStreamGain(this->voices[0], music_gain)) {
      AUDIO_FAIL("Audio::step");
    }

    this->current_music_volume = this->music_volume;
  }

  // Wait until less than LOOP_THRESHOLD of audio remains, then queue another
  // copy of the ambience track.
  if (this->ambience == nullptr ||
      SDL_GetAudioStreamQueued(this->voices[0]) >= LOOP_THRESHOLD) {
    return;
  }

  if (!SDL_PutAudioStreamData(this->voices[0], this->ambience->buf,
                              this->ambience->len)) {
    AUDIO_FAIL("Audio::step");
  }
}

};  // namespace thoom
