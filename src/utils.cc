#include "utils.h"

#include <cmath>

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
  angle += SDL_PI_D;                      // [0,2.0*PI]
  angle /= SDL_PI_D;                      // [0,2.0]
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
