#include "save/save_data.h"

#include <SDL3/SDL.h>
#include <string.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "scene/scene.h"

namespace thoom {

SaveData save;

#define PREF_ORG "CheddarAndFeta"
#define PREF_APP "Game"
#define NO_SUMMARY "..."

std::vector<std::string> SaveData::file_summaries() {
  char save_file_name[1024];
  char* save_root = SDL_GetPrefPath(PREF_ORG, PREF_APP);
  std::vector<std::string> summaries;

  if (save_root == nullptr) {
    LOG(ERROR) << "SaveData::file_summaries error: " << SDL_GetError();
    return summaries;
  }

  for (int i = 0;; i++) {
    // open save file
    snprintf(save_file_name, sizeof(save_file_name), "%ssave%d.txt", save_root,
             i);
    FILE* save_file = fopen(save_file_name, "r");
    if (save_file == NULL) {
      break;
    }

    // read first line of file
    char line[1024];
    if (fgets(line, sizeof(line), save_file) == NULL) {
      LOG(WARNING) << "SaveData::file_summaries: '" << save_file_name
                   << "' has no summary line";
      fclose(save_file);

      // Keep the slot in the list so the numbers after it do not shift.
      summaries.push_back(NO_SUMMARY);
      continue;
    }

    line[strcspn(line, "\n")] = '\0';

    // first line of file is save summary
    summaries.push_back(line);
    fclose(save_file);
  }

  SDL_free(save_root);
  return summaries;
}

// http://www.cse.yorku.ca/~oz/hash.html
static unsigned long _djb2_hash(unsigned long starting_hash, const char* str) {
  unsigned long hash = starting_hash == 0 ? 5381 : starting_hash;
  int c;

  while ((c = *str++) != '\0')
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return hash;
}

void SaveData::write_file(int file_i) {
  if (file_i < 0) {
    LOG(FATAL) << "bad save";
  }

  char save_file_name[1024];
  char* save_root = SDL_GetPrefPath(PREF_ORG, PREF_APP);

  if (save_root == nullptr) {
    LOG(ERROR) << "SaveData::write_file error: " << SDL_GetError();
    return;
  }

  // open file
  snprintf(save_file_name, sizeof(save_file_name), "%ssave%d.txt", save_root,
           file_i);
  SDL_free(save_root);
  FILE* save_file = fopen(save_file_name, "w");
  if (save_file == NULL) {
    LOG(FATAL) << "bad save";
  }

  std::string line;
  unsigned long hash = 0;

  if (this->geti(GAME_DONE)) {
    line = "(Game Complete.)\n";
  } else if (Scene::instance != nullptr && Scene::instance->map != nullptr) {
    line = Scene::instance->map->title + ", " +
           Scene::instance->map->description + "\n";
  } else {
    line = "No map loaded\n";
  }

  hash = _djb2_hash(hash, line.c_str());
  fputs(line.c_str(), save_file);

  // write rest of save data to file
  for (auto pair : this->data) {
    if (pair.first[0] == DONT_WRITE[0]) continue;

    if (pair.first.empty() || pair.second.empty()) continue;

    line = pair.first + "=" + pair.second + "\n";
    hash = _djb2_hash(hash, line.c_str());
    fputs(line.c_str(), save_file);
  }

  // output hash of file to prevent tampering (not super secure, just enough)
  char buff[256];
  snprintf(buff, sizeof(buff), "%lu\n", hash);
  fputs(buff, save_file);

  const bool write_failed = ferror(save_file) != 0;

  if (fclose(save_file) != 0 || write_failed) {
    LOG(ERROR) << "SaveData::write_file error: could not write '"
               << save_file_name << "'";
  }
}

#define MAX_LINE_LENGTH 10240  // 10kb

int SaveData::load_file(int file_i) {
  if (file_i < 0) {
    LOG(FATAL) << "bad save";
  }

  char save_file_name[1024];
  char* save_root = SDL_GetPrefPath(PREF_ORG, PREF_APP);

  if (save_root == nullptr) {
    LOG(ERROR) << "SaveData::load_file error: " << SDL_GetError();
    return LOAD_TAMPER;
  }

  this->data.clear();

  // open file
  snprintf(save_file_name, sizeof(save_file_name), "%ssave%d.txt", save_root,
           file_i);
  SDL_free(save_root);
  FILE* save_file = fopen(save_file_name, "r");
  if (save_file == NULL) {
    return LOAD_NEW;
  }

  char line[MAX_LINE_LENGTH];

  if (fgets(line, sizeof(line), save_file) == NULL) {
    LOG(WARNING) << "SaveData::load_file: '" << save_file_name
                 << "' has no summary line";
    fclose(save_file);
    return LOAD_TAMPER;
  }

  unsigned long hash = _djb2_hash(0, line);

  // for every subsequent line in the file
  while (fgets(line, sizeof(line), save_file) != NULL) {
    // remove last newline
    size_t len = strnlen(line, MAX_LINE_LENGTH);

    if (len == 0) continue;

    char *key = line, *val;
    bool is_kvp = false;

    for (char* ptr = line; *ptr != '\0'; ptr++) {
      if (*ptr == '=') {
        // hash this line; must include the = and \n chars
        hash = _djb2_hash(hash, line);
        *ptr = '\0';
        val = ptr + 1;
        is_kvp = true;
      }
    }

    // remove newline from line so val is just the chars between = and \n
    if (line[len - 1] == '\n') line[len - 1] = '\0';

    if (!is_kvp && line[0] >= '0' && line[0] <= '9') {
      // last line of file - hash
      unsigned long hash_in_file;

      if (1 != sscanf(line, "%lu\n", &hash_in_file)) {
        LOG(FATAL) << "bad save hash";
      }

      fclose(save_file);
      return hash == hash_in_file ? LOAD_SUCCESS : LOAD_TAMPER;
    }

    this->data[key] = val;
  }

  fclose(save_file);

  // there wasn't a hash...?
  return LOAD_TAMPER;
}

void float_to_str(float f, char* str, size_t str_size) {
  if (str_size < 9) return;
  uint32_t bits;
  memcpy(&bits, &f, 4);
  snprintf(str, str_size, "%x", bits);
}

float str_to_float(std::string str) {
  const char* arr = str.c_str();
  uint32_t bits = strtoul(arr, NULL, 16);
  float f;
  memcpy(&f, &bits, 4);
  return f;
}

};  // namespace thoom
