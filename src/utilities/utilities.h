#ifndef UTILITIES_H
#define UTILITIES_H

#include <stdio.h>
#include <stdbool.h>

char *copy_string(const char *source, const size_t length);
char *copy_whole_string(const char *source);
bool compare_string(const char *lhs, const size_t lhs_length, const char *rhs, const size_t rhs_length);

char *parse_module_path_utility(const char *path, const size_t length, char **out_directory, size_t *out_directory_length, const bool remove_extension);
char *change_file_extension(const char *path, const size_t length, const char *new_extension);

void check_literal_conversion(const char *path, const size_t ln, const size_t col, int no, char *value, char *endptr);

#endif