#ifndef SOURCE_H
#define SOURCE_H

#include <stdio.h>

typedef struct {
    char *path;
    size_t path_length;
    size_t ln;
    size_t col;
} Source;

static inline Source create_source(char *path, size_t ln, size_t col) {
    return (Source){ .path = path, .ln = ln, .col = col };
}

#endif
