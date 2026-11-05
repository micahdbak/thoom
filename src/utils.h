#pragma once

namespace thoom {

double distance_between_points(double x1, double y1, double x2, double y2);

int direction_from_dirs(int x_dir, int y_dir);

void dirs_from_direction(int direction, int* x_dir, int* y_dir);

void dir_to_point(float x1, float y1, float x2, float y2, int* x_dir,
                  int* y_dir);

const char* next_line(const char* arr);

};  // namespace thoom
