#ifndef BACKEND_UTILITIES_H
#define BACKEND_UTILITIES_H

#include "data_type.h"
#include "string_builder.h"
#include "lir.h"
#include "context.h"
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#define STACK_CACHE_CAPACITY 99

#define MAX_REGISTER_ARGUMENT_COUNT 6

enum {
    REGISTER_1, // 1 and 2 are commonly used in the IR.
    REGISTER_2,
    // This is a reserved register; IR doesn't use it, it's for backend temporary usage without clobbering IR registers.
    REGISTER_3
};

typedef enum {
    RAX,
    RBX,
    RCX,
    RDX,
    RSI,
    RDI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    XMM,
    HARDWARE_REGISTER_COUNT
} Hardware_Register;

typedef enum {
    BYTE,
    WORD,
    DWORD,
    QWORD
} Word_Size;

typedef struct {
    size_t uid;
    Data_Type data_type;
    Word_Size word_size;
    int stack_offset;
} Local_Variable;

typedef struct {
    Context *context;
    size_t stack_reserved;
    size_t stack_used;
    size_t stack_cache[STACK_CACHE_CAPACITY];
    size_t stack_cache_count;
    Local_Variable *locals;
    size_t locals_count;
    size_t locals_capacity;
    size_t constant_label_count;
    String_Builder rodata_section;
} State;

State create_state(Context *context);
void delete_state(State *state);

static inline void clear_local_variables(State *state) {
    state->locals_count = 0;
}

static inline void clear_rodata_section(State *state) {
    state->rodata_section.length = 0;
}

static inline void clear_stack(State *state) {
    state->stack_used = state->stack_reserved = state->stack_cache_count = 0;
}

// Stack management for blocks.
void push_stack_cache(State *state);
void pop_stack_cache(State *state);

// Only returns an allocated string if alloc_or_dealloc && reserve_too.
char *alter_stack(State *state, const int bytes, const bool reserve_too, const bool alloc_or_dealloc);

Word_Size primitive_type_to_word_size(const Primitive_Type primitive_type);
const char *word_size_to_string(const Word_Size word);

char *allocate_local_variable(State *state, size_t uid, Data_Type data_type, const size_t primitive_type_size);
void allocate_local_variable_from_call_argument(State *state, size_t uid, Data_Type data_type, LIR_Operand *src);
Local_Variable *find_local_variable(State *state, const size_t uid);

const char *hardware_register_to_string(const Hardware_Register number, const Primitive_Type primitive_type);
const char *temporary_register(const size_t number, const Primitive_Type primitive_type);
const char *register_argument(const size_t number, const Primitive_Type primitive_type);

size_t new_float_label(State *state, const bool is_32, float f32, double f64);
size_t new_string_label(State *state, const char *string, const size_t length);

#endif
