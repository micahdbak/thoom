#pragma once

#include <SDL3/SDL.h>

#define THOOM_CLAMP(_x, _min, _max) \
  ((_x) < (_min) ? (_min) : ((_x) > (_max) ? (_max) : (_x)))
#define THOOM_MIN(_a, _b) ((_b) < (_a) ? (_b) : (_a))
#define THOOM_MAX(_a, _b) ((_b) > (_a) ? (_b) : (_a))
#define THOOM_SIGN(_x) ((_x) == 0 ? 0 : ((_x) > 0 ? 1 : -1))
#define THOOM_ABS(_x) ((_x) < 0 ? (-1 * (_x)) : (_x))
#define THOOM_DISTANCE_BETWEEN_POINTS(x1, y1, x2, y2) \
  (sqrt(pow((x2) - (x1), 2) + pow((y2) - (y1), 2)))
#define THOOM_DIAG_MULTIPLIER 0.7071f
#define THOOM_DIAG_MULTIPLIER2 1.4142f

namespace thoom {

static constexpr double PI = SDL_PI_D;

int direction_from_dirs(int x_dir, int y_dir);

void dirs_from_direction(int direction, int* x_dir, int* y_dir);

void dir_to_point(float x1, float y1, float x2, float y2, int* x_dir,
                  int* y_dir);

void make_map_rect(int x, int y, int w, int h, SDL_FRect* src_rect,
                   SDL_FRect* dst_rect);

const char* next_line(const char* arr);

};  // namespace thoom
