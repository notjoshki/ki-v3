#include "./backend_utilities.h"
#include "string_builder.h"
#include "lir.h"
#include "data_type.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <inttypes.h>

#define LOCAL_VARIABLE_CAPACITY 4

static const char *hardware_registers[HARDWARE_REGISTER_COUNT][16] = {
    { "al", "ax", "eax", "rax" },
    { "bl", "bx", "ebx", "rbx" },
    { "cl", "cx", "ecx", "rcx" },
    { "dl", "dx", "edx", "rdx" },
    { "sil", "si", "esi", "rsi" },
    { "dil", "di", "edi", "rdi" },
    { "r8b", "r8w", "r8d", "r8" },
    { "r9b", "r9w", "r9d", "r9" },
    { "r10b", "r10w", "r10d", "r10" },
    { "r11b", "r11w", "r11d", "r11" },
    { "r12b", "r12w", "r12d", "r12" },
    { "r13b", "r13w", "r13d", "r13" },
    { "r14b", "r14w", "r14d", "r14" },
    { "r15b", "r15w", "r15d", "r15" },
    { "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
      "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15" }
};

State create_state(Context *context) {
    return (State){ .context = context, .stack_reserved = 0, .stack_used = 0, 
        .locals = malloc(LOCAL_VARIABLE_CAPACITY * sizeof(Local_Variable)), 
        .locals_count = 0, .locals_capacity = LOCAL_VARIABLE_CAPACITY,
        .constant_label_count = 0, .stack_cache_count = 0,
        .rodata_section = create_string_builder() };
}

void delete_state(State *state) {
    free(state->locals);
    delete_string_builder(&state->rodata_section);
}

void push_stack_cache(State *state) {
    assert(state->stack_cache_count < STACK_CACHE_CAPACITY);
    state->stack_cache[state->stack_cache_count++] = state->stack_reserved;
    state->stack_cache[state->stack_cache_count++] = state->stack_used;
}

void pop_stack_cache(State *state) {
    assert(state->stack_cache_count >= 2);
    state->stack_used = state->stack_cache[--state->stack_cache_count];
    state->stack_reserved = state->stack_cache[--state->stack_cache_count];
}

char *alter_stack(State *state, const int bytes, const bool reserve_too, const bool alloc_or_dealloc) {
    state->stack_used += bytes;

    if (reserve_too)
        state->stack_reserved += bytes;

    if (!alloc_or_dealloc || !reserve_too)
        return NULL;

    char *code = malloc(32);
    sprintf(code, "%s rsp, %d\n", bytes > 0 ? "sub" : "add", abs(bytes));
    return code;
}

Word_Size primitive_type_to_word_size(const Primitive_Type primitive_type) {
    switch (primitive_type) {
        case PRIM_BOOL:
        case PRIM_I8:
        case PRIM_U8: return BYTE;
        case PRIM_I16:
        case PRIM_U16: return WORD;
        case PRIM_I32:
        case PRIM_U32:
        case PRIM_F32: return DWORD;
        default: return QWORD;
    }
}

const char *word_size_to_string(const Word_Size word) {
    if (word == BYTE)
        return "byte";
    else if (word == WORD)
        return "word";
    else if (word == DWORD)
        return "dword";

    return "qword";
}

void allocate_local_variable_from_call_argument(State *state, size_t uid, Data_Type data_type, LIR_Operand *src) {
    if (state->locals_count + 1 > state->locals_capacity) {
        state->locals_capacity *= 2;
        state->locals = realloc(state->locals, state->locals_capacity * sizeof(Local_Variable));
    }

    state->locals[state->locals_count++] = (Local_Variable){ 
        .uid = uid, .data_type = data_type, .word_size = primitive_type_to_word_size(data_type_to_primitive_type(&data_type)),
        .stack_offset = (src->argument.index - MAX_REGISTER_ARGUMENT_COUNT + 2) * 8 };
}

char *allocate_local_variable(State *state, size_t uid, Data_Type data_type, const size_t primitive_type_size) {
    if (state->locals_count + 1 > state->locals_capacity) {
        state->locals_capacity *= 2;
        state->locals = realloc(state->locals, state->locals_capacity * sizeof(Local_Variable));
    }

    const size_t size = primitive_type_size;
    state->stack_used += data_type.array_size == 0 ? size : data_type.array_size * size;

    state->locals[state->locals_count++] = (Local_Variable){ 
        .uid = uid, .data_type = data_type, .word_size = primitive_type_to_word_size(data_type_to_primitive_type(&data_type)),
        .stack_offset = -state->stack_used };

    if (state->stack_used <= state->stack_reserved)
        return calloc(1, sizeof(char));

    const size_t before = state->stack_reserved;

    while (state->stack_used > state->stack_reserved || state->stack_reserved % 16 != 0)
        state->stack_reserved++;

    char *align = malloc(32);
    sprintf(align, "sub rsp, %d\n", (int)state->stack_reserved - (int)before);
    return align;
}

Local_Variable *find_local_variable(State *state, const size_t uid) {
    // TODO: Should we instead order the UIDs sorted and 
    // switch this out for a binary search?
    for (size_t i = 0; i < state->locals_count; i++) {
        if (state->locals[i].uid == uid)
            return &state->locals[i];
    }

    return NULL;
}

const char *hardware_register_to_string(const Hardware_Register number, const Primitive_Type primitive_type) {
    if (bin_is_float(primitive_type)) {
        assert(number <= 15);
        return hardware_registers[XMM][number];
    }

    assert(number <= R15);
    return hardware_registers[number][primitive_type_to_word_size(primitive_type)];
}

const char *temporary_register(const size_t number, const Primitive_Type primitive_type) {
    if (bin_is_float(primitive_type)) {
        assert(number <= REGISTER_3);
        return hardware_register_to_string(number + MAX_REGISTER_ARGUMENT_COUNT, primitive_type);
    }

    Hardware_Register reg;

    switch (number) {
        case REGISTER_1:
            reg = RAX;
            break;
        case REGISTER_2:
            reg = RCX;
            break;
        case REGISTER_3:
            reg = RBX;
            break;
        default:
            assert(false);
            reg = RAX;
            break;
    }

    return hardware_register_to_string(reg, primitive_type);
}

const char *register_argument(const size_t number, const Primitive_Type primitive_type) {
    assert(number < MAX_REGISTER_ARGUMENT_COUNT);

    if (bin_is_float(primitive_type))
        return hardware_register_to_string(number, primitive_type);

    Hardware_Register hw;

    switch (number) {
        case 0:
            hw = RDI;
            break;
        case 1:
            hw = RSI;
            break;
        case 2:
            hw = RDX;
            break;
        case 3:
            hw = RCX;
            break;
        case 4:
            hw = R8;
            break;
        default:
            hw = R9;
            break;
    }

    return hardware_register_to_string(hw, primitive_type);
}

size_t new_float_label(State *state, const bool is_32, float f32, double f64) {
    char *code = malloc(64);
    uint64_t bits;

    if (is_32) { 
        memcpy(&bits, &f32, sizeof(f32));
        sprintf(code, ".c%zu: dd %" PRIu32 "\n", state->constant_label_count, (uint32_t)bits);
    } else {
        memcpy(&bits, &f64, sizeof(f64));
        sprintf(code, ".c%zu: dq %" PRIu64 "\n", state->constant_label_count, bits);
    }

    append_whole_string(&state->rodata_section, code);
    free(code);
    return state->constant_label_count++;
}

size_t new_string_label(State *state, const char *string, const size_t length) {
    size_t length_without_backslashes = 0;

    for (size_t i = 0; i < length; i++) {
        length_without_backslashes++;

        if (string[i] == '\\' && i + 1 < length)
            i += 1;
    }

    char *code = malloc(length + 64);
    sprintf(code, ".c%zu: dq %zu\ndq %zu\ndb `%.*s`,0\n", state->constant_label_count, 
        length_without_backslashes, length_without_backslashes, (int)length, string);

    append_whole_string(&state->rodata_section, code);
    free(code);
    return state->constant_label_count++;
}
