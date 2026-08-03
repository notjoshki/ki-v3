#include "utilities.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

char *copy_string(const char *source, const size_t length) {
    char *copy = malloc(length + 1);
    memcpy(copy, source, length);
    copy[length] = '\0';
    return copy;
}

char *copy_whole_string(const char *source) {
    const size_t len = strlen(source);
    char *copy = malloc(len + 1);
    memcpy(copy, source, len);
    copy[len] = '\0';
    return copy;
}

bool compare_string(const char *lhs, const size_t lhs_length, const char *rhs, const size_t rhs_length) {
    if (lhs_length != rhs_length)
        return false;

    return memcmp(lhs, rhs, lhs_length) == 0;
}

static char *remove_file_extension(const char *path, const size_t length) {
    char *last_dot = strrchr(path, '.');
    char *copy = malloc(length + 1);

    if (last_dot == NULL)
        strcpy(copy, path);
    else {
        size_t diff = (size_t)(last_dot - path);
        strncpy(copy, path, diff);
        copy[diff] = '\0';
    }

    return copy;
}

char *parse_module_path_utility(const char *path, const size_t length, char **out_directory, size_t *out_directory_length, const bool remove_extension) {
    *out_directory = malloc(length + 1);
    *out_directory[0] = '\0';
    *out_directory_length = length;

    char *copy = copy_string(path, length);
    char *tok = strtok(copy, "/");
    char *last_tok = NULL;

    while (tok != NULL) {
        // We don't want to remove the first '/' if it is the first character in the path.
        if (last_tok == NULL && copy[0] == '/')
            strcat(*out_directory, "/");
        else if (last_tok != NULL) {
            strcat(*out_directory, last_tok);
            strcat(*out_directory, "/");
        }

        last_tok = tok;
        tok = strtok(NULL, "/");
    }

    char *basename = remove_extension ? remove_file_extension(last_tok == NULL ? copy : last_tok, strlen(last_tok == NULL ? copy : last_tok)) :
        copy_whole_string(last_tok == NULL ? copy : last_tok);
    free(copy);
    return basename;
}

char *change_file_extension(const char *path, const size_t length, const char *new_extension) {
    char *new = remove_file_extension(path, length);
    new = realloc(new, length + strlen(new_extension) + 2);
    strcat(new, ".");
    strcat(new, new_extension);
    return new;
}

void check_literal_conversion(const char *path, const size_t ln, const size_t col, int no, char *value, char *endptr) {
    if (no == ERANGE || value == endptr)
        log(ERROR_CRITICAL, path, ln, col, "Literal conversion failed: %s\n", strerror(no));
    else if (*endptr != '\0')
        log(ERROR_CRITICAL, path, ln, col, "Literal conversion failed\n");
}
