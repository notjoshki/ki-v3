#ifndef STRING_BUILDER_H
#define STRING_BUILDER_H

#include <stdio.h>
#include <stdbool.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} String_Builder;

String_Builder create_string_builder();
void delete_string_builder(String_Builder *builder);
void append_string(String_Builder *builder, const char *string, const size_t length);
void append_whole_string(String_Builder *builder, const char *string);
char *string_builder_to_cstring(String_Builder *builder);
void insert_char(String_Builder *builder, const char c, const size_t index);
void insert_string(String_Builder *builder, const char *string, const size_t length, const size_t index);

#endif
