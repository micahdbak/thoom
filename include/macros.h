#pragma once

#define THOOM_CLAMP(_x, _min, _max) \
  ((_x) < (_min) ? (_min) : ((_x) > (_max) ? (_max) : (_x)))

#define THOOM_MIN(_a, _b) ((_b) < (_a) ? (_b) : (_a))

#define THOOM_MAX(_a, _b) ((_b) > (_a) ? (_b) : (_a))

#define THOOM_SIGN(_x) ((_x) == 0 ? 0 : ((_x) > 0 ? 1 : -1))

#define THOOM_ABS(_x) ((_x) < 0 ? (-1 * (_x)) : (_x))

#define THOOM_DISTANCE_BETWEEN_POINTS(x1, y1, x2, y2) \
  (sqrt(pow((x2) - (x1), 2) + pow((y2) - (y1), 2)))

#define THOOM_SQRT2_F 1.4142135623730951f
#define THOOM_SQRT2_D 1.4142135623730951

#define THOOM_INV_SQRT2_F 0.7071067811865475f
#define THOOM_INV_SQRT2_D 0.7071067811865475

#define THOOM_PI_F 3.141592653589793f
#define THOOM_PI_D 3.141592653589793

#define DISALLOW_COPY_AND_MOVE(class_name)           \
  class_name(const class_name&) = delete;            \
  class_name& operator=(const class_name&) = delete; \
  class_name(class_name&&) = delete;                 \
  class_name& operator=(class_name&&) = delete
