#include "thoom/log.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "absl/log/log.h"

static std::string format_message(const char* format, va_list args) {
  va_list copy;
  va_copy(copy, args);
  const int size = vsnprintf(nullptr, 0, format, copy);
  va_end(copy);

  if (size <= 0) {
    return std::string();
  }

  std::vector<char> buffer(size + 1);
  vsnprintf(buffer.data(), buffer.size(), format, args);
  return std::string(buffer.data(), size);
}

void thoom_log_info(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);

  LOG(INFO) << message;
}

void thoom_log_warn(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);

  LOG(WARNING) << message;
}

void thoom_log_error(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);

  LOG(ERROR) << message;
}

void thoom_log_fatal(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const std::string message = format_message(format, args);
  va_end(args);

  LOG(FATAL) << message;
}
