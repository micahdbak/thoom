#include "utils.h"

#include <cmath>

#include "game.h"

namespace thoom {

int direction_from_dirs(int x_dir, int y_dir) {
  if (x_dir < 0) {
    return 6 + y_dir;
  } else if (x_dir > 0) {
    return 2 - y_dir;
  } else if (y_dir < 0) {
    return 4;
  } else {
    return 0;
  }
}

void dirs_from_direction(int direction, int* x_dir, int* y_dir) {
  switch (direction) {
    case 0:
      *x_dir = 0, *y_dir = 1;
      break;
    case 1:
      *x_dir = 1, *y_dir = 1;
      break;
    case 2:
      *x_dir = 1, *y_dir = 0;
      break;
    case 3:
      *x_dir = 1, *y_dir = -1;
      break;
    case 4:
      *x_dir = 0, *y_dir = -1;
      break;
    case 5:
      *x_dir = -1, *y_dir = -1;
      break;
    case 6:
      *x_dir = -1, *y_dir = 0;
      break;
    case 7:
      *x_dir = -1, *y_dir = 1;
      break;
    default:
      break;
  }
}

void dir_to_point(float x1, float y1, float x2, float y2, int* x_dir,
                  int* y_dir) {
  float angle = atan2(y2 - y1, x2 - x1);  // [-PI,PI]
  angle += PI;                            // [0,2.0*PI]
  angle /= PI;                            // [0,2.0]
  angle *= 4.0f;                          // [0,8.0]
  angle += 0.5f;                          // [0.5,8.5] - for integer rounding

  switch (int(angle)) {
    case 0:
      *x_dir = -1, *y_dir = 0;
      break;
    case 1:
      *x_dir = -1, *y_dir = -1;
      break;
    case 2:
      *x_dir = 0, *y_dir = -1;
      break;
    case 3:
      *x_dir = 1, *y_dir = -1;
      break;
    case 4:
      *x_dir = 1, *y_dir = 0;
      break;
    case 5:
      *x_dir = 1, *y_dir = 1;
      break;
    case 6:
      *x_dir = 0, *y_dir = 1;
      break;
    case 7:
      *x_dir = -1, *y_dir = 1;
      break;
    case 8:
      *x_dir = -1, *y_dir = 0;
      break;
  }
}

void make_map_rect(int x, int y, int w, int h, SDL_FRect* src_rect,
                   SDL_FRect* dst_rect) {
  if (x < 0) {
    dst_rect->x = float(-1 * x);
    dst_rect->w = float(THOOM_MIN(THOOM_SCREEN_WIDTH, w));
    src_rect->x = 0.0f;
  } else {
    dst_rect->x = 0.0f;
    dst_rect->w = float(THOOM_MIN(THOOM_SCREEN_WIDTH, w - x));
    src_rect->x = float(x);
  }

  if (y < 0) {
    dst_rect->y = float(-1 * y);
    dst_rect->h = float(THOOM_MIN(THOOM_SCREEN_HEIGHT, h));
    src_rect->y = 0.0f;
  } else {
    dst_rect->y = 0.0f;
    dst_rect->h = float(THOOM_MIN(THOOM_SCREEN_HEIGHT, h - y));
    src_rect->y = float(y);
  }

  src_rect->w = dst_rect->w;
  src_rect->h = dst_rect->h;
}

static char null_byte = '\0';

const char* next_line(const char* arr) {
  size_t i = 0;

  while (*arr != '\n' && *arr != '\0' && i < 1024) {
    arr++;
    i++;
  }

  if (i >= 1024) {
    return &null_byte;
  }

  if (*arr == '\n') {
    arr++;
  }

  return arr;
}

};  // namespace thoom
