#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#define WARN_NO_FLOAT_DOUBLE_CONVERSION 0x01

#define log(type, path, ln, col, ...) \
    do { \
        log_location(type, path, ln, col); \
        fprintf(stderr, __VA_ARGS__); \
        if (path != LOG_NOFILE) log_source(type, path, ln, col); \
    } while (0)

#define LOG_NOFILE NULL
#define LOG_NOLN 0
#define LOG_NOCOL 0

#define ESC_RED "\x1b[31m"
#define ESC_CYAN "\x1b[36m"
#define ESC_MAGENTA "\x1b[35m"
#define ESC_YELLOW "\x1b[33m"
#define ESC_GREEN "\x1B[32m"
#define ESC_NORMAL "\x1b[0m"
#define ESC_BOLD "\x1b[1m"

typedef enum {
    ERROR_CRITICAL,
    ERROR_WARNING,
    ERROR_INFO
} Error_Type;

void log_location(const Error_Type type, const char *path, const size_t ln, const size_t col);
void log_source(const Error_Type type, const char *path, const size_t ln, size_t col);
size_t get_error_count();
void increment_error_count();

#endif
