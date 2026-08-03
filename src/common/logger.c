#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define LINE_BUF_CAP 128
#define ERROR_CAPACITY 20

static size_t error_count = 0;

static char *error_type_to_color(const Error_Type type) {
    if (type == ERROR_CRITICAL)
        return ESC_RED;
    else if (type == ERROR_INFO)
        return ESC_YELLOW;

    return ESC_MAGENTA;
}

void log_location(const Error_Type type, const char *path, const size_t ln, const size_t col) {
    if (error_count == ERROR_CAPACITY) {
        fprintf(stderr, "Maximum error count exceeded, aborting\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, ESC_BOLD "%s:", path == LOG_NOFILE ? "ki" : path);

    if (ln != LOG_NOLN && col != LOG_NOCOL)
        fprintf(stderr, "%zu,%zu:", ln, col);

    const char *color = error_type_to_color(type);

    if (type == ERROR_CRITICAL) {
        fprintf(stderr, "%s Error: " ESC_NORMAL, color);
        error_count++;
    } else if (type == ERROR_WARNING)
        fprintf(stderr, "%s Warning: " ESC_NORMAL, color);
    else if (type == ERROR_WARNING)
        fprintf(stderr, "%s Note: " ESC_NORMAL, color);
    else
        fprintf(stderr, "%s Info: " ESC_NORMAL, color);
}

size_t get_error_count() {
    return error_count;
}

void log_source(const Error_Type type, const char *path, const size_t ln, size_t col) {
    if (ln == LOG_NOLN && col == LOG_NOCOL)
        return;

    FILE *file = fopen(path, "r");

    if (file == NULL) {
        fprintf(stderr, "(Failed to retrieve error source)\n");
        return;
    }

    char line_buf[LINE_BUF_CAP + 1];
    size_t cur_ln = 0;
    bool found_ln = false;

    while (fgets(line_buf, LINE_BUF_CAP, file) != NULL) {
        if (++cur_ln == ln) {
            found_ln = true;
            break;
        }
    }

    fclose(file);

    if (!found_ln) {
        fprintf(stderr, "(Failed to retrieve error source)\n");
        return;
    }

    const size_t line_buf_len = strlen(line_buf);

    if (line_buf[line_buf_len - 1] == '\n')
        line_buf[line_buf_len - 1] = '\0';

    char *unpadded_ptr = line_buf;

    while (isspace(*unpadded_ptr)) {
        unpadded_ptr++;
        col--;
    }

    fprintf(stderr, ESC_BOLD ESC_CYAN "    %s\n" ESC_NORMAL, unpadded_ptr);

    for (size_t i = 0; i < col + 3; i++)
        fputc(' ', stderr);

    fprintf(stderr, ESC_BOLD "%s^\n" ESC_NORMAL, error_type_to_color(type));
}
