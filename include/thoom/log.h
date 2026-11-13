#ifndef THOOM_LOG_H
#define THOOM_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define THOOM_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define THOOM_NORETURN __declspec(noreturn)
#else
#define THOOM_NORETURN
#endif

void thoom_log_info(const char* format, ...);

void thoom_log_warn(const char* format, ...);

void thoom_log_error(const char* format, ...);

THOOM_NORETURN void thoom_log_fatal(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif  // THOOM_LOG_H
