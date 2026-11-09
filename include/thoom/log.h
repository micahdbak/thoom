#ifndef THOOM_LOG_H
#define THOOM_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void thoom_log_info(const char* format, ...);

void thoom_log_warn(const char* format, ...);

void thoom_log_error(const char* format, ...);

void thoom_log_fatal(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif  // THOOM_LOG_H
