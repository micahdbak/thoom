#pragma once

#include <SDL3/SDL.h>

#include "macros.h"

namespace thoom {

int direction_from_dirs(int x_dir, int y_dir);

void dirs_from_direction(int direction, int* x_dir, int* y_dir);

void dir_to_point(float x1, float y1, float x2, float y2, int* x_dir,
                  int* y_dir);

void make_map_rect(int x, int y, int w, int h, SDL_FRect* src_rect,
                   SDL_FRect* dst_rect);

const char* next_line(const char* arr);

};  // namespace thoom
