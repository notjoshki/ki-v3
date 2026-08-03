#include "string_builder.h"
#include "compiler.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define DATA_CAPACITY 32

String_Builder create_string_builder() {
    return (String_Builder){ .data = malloc(DATA_CAPACITY + 1), .length = 0, .capacity = DATA_CAPACITY };
}

void delete_string_builder(String_Builder *builder) {
    free(builder->data);
}

static void ensure_builder_has_more_memory(String_Builder *builder, const size_t extra_length) {
    if (builder->length + extra_length + 1 > builder->capacity) {
        while (builder->length + extra_length + 1 > builder->capacity)
            builder->capacity *= 2;

        builder->data = realloc(builder->data, builder->capacity + 1);
    }
}

void append_string(String_Builder *builder, const char *string, const size_t length) {
    ensure_builder_has_more_memory(builder, length);
    memcpy(builder->data + builder->length, string, length);
    builder->length += length;
}

void append_whole_string(String_Builder *builder, const char *string) {
    const size_t len = strlen(string);
    ensure_builder_has_more_memory(builder, len);
    memcpy(builder->data + builder->length, string, len);
    builder->length += len;
}

char *string_builder_to_cstring(String_Builder *builder) {
    builder->data[builder->length] = '\0';
    return builder->data;
}

void insert_char(String_Builder *builder, const char c, const size_t index) {
    ensure_builder_has_more_memory(builder, 1);

    for (int i = builder->length; i > (int)index; i--)
        builder->data[i] = builder->data[i - 1];

    builder->data[index] = c;
    builder->length++;
}

void insert_string(String_Builder *builder, const char *string, const size_t length, const size_t index) {
    ensure_builder_has_more_memory(builder, length);

    for (int i = builder->length + length - 1; i > (int)index; i--)
        builder->data[i] = builder->data[i - length];

    memcpy(builder->data + index, string, length);
    builder->length += length;
}
