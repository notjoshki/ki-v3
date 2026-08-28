#include "ir_representation.h"
#include "lir.h"
#include "string_builder.h"
#include "decorators.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

static char *lir_operand_to_string(LIR_Operand *operand);

static char *lir_int_to_string(LIR_Operand *operand) {
    char *str = malloc(16);

    if (operand->data_type.primitive_type == PRIM_I32)
        sprintf(str, "%" PRId32, operand->int_.i32);
    else if (operand->data_type.primitive_type == PRIM_I64)
        sprintf(str, "%" PRId64, operand->int_.i64);
    else if (operand->data_type.primitive_type == PRIM_U32)
        sprintf(str, "%" PRIu32, operand->int_.u32);
    else
        sprintf(str, "%" PRIu64, operand->int_.u64);

    return str;
}

static char *lir_float_to_string(LIR_Operand *operand) {
    char *str = malloc(16);

    if (operand->data_type.primitive_type == PRIM_F32)
        sprintf(str, "%f", operand->float_.f32);
    else
        sprintf(str, "%lf", operand->float_.f64);

    return str;
}

static char *lir_string_to_string(LIR_Operand *operand) {
    char *str = malloc(operand->string.length + 3);
    sprintf(str, "\"%.*s\"", (int)operand->string.length, operand->string.string);
    return str;
}

static char *lir_local_variable_to_string(LIR_Operand *operand) {
    char *str = malloc(16);
    sprintf(str, "$%zu", operand->local_variable.variable_uid);
    return str;
}

static char *lir_register_to_string(LIR_Operand *operand) {
    char *str = malloc(24);
    sprintf(str, "%%%zu", operand->register_.number);
    return str;
}

static char *lir_function_to_string(LIR_Operand *operand) {
    char *str = malloc(operand->function.length + 2);
    sprintf(str, "@%s", operand->function.name);
    return str;
}

static char *lir_argument_to_string(LIR_Operand *operand) {
    char *str = malloc(16);
    sprintf(str, "#%zu", operand->argument.index);
    return str;
}

static char *lir_label_to_string(LIR_Operand *operand) {
    char *str = malloc(16);
    sprintf(str, "@%zu", operand->label.number);
    return str;
}

static char *lir_pointer_to_string(LIR_Operand *operand) {
    char *str = malloc(24);
    sprintf(str, "ptr %%%zu", operand->pointer.register_.number);
    return str;
}

static char *lir_sizeof_to_string(LIR_Operand *operand) {
    char *type = data_type_to_string(&operand->sizeof_.data_type);
    char *str = malloc(strlen(type) + 32);
    sprintf(str, "sizeof %s", type);
    free(type);
    return str;
}

static char *lir_operand_to_string(LIR_Operand *operand) {
    switch (operand->type) {
        case OPER_NONE: return calloc(1, sizeof(char));
        case OPER_INT: return lir_int_to_string(operand);
        case OPER_FLOAT: return lir_float_to_string(operand);
        case OPER_STRING: return lir_string_to_string(operand);
        case OPER_LOCAL_VARIABLE: return lir_local_variable_to_string(operand);
        case OPER_REGISTER: return lir_register_to_string(operand);
        case OPER_FUNCTION: return lir_function_to_string(operand);
        case OPER_ARGUMENT: return lir_argument_to_string(operand);
        case OPER_LABEL: return lir_label_to_string(operand);
        case OPER_POINTER: return lir_pointer_to_string(operand);
        case OPER_SIZEOF: return lir_sizeof_to_string(operand);
        default:
            assert(false);
            return calloc(1, sizeof(char));
    }
}

static char *lir_instruction_to_string_implicit(LIR_Instruction *inst) {
    char *dst = lir_operand_to_string(&inst->destination);
    char *dst_type = data_type_to_string(&inst->destination.data_type);

    char *src = lir_operand_to_string(&inst->source);
    char *src_type = data_type_to_string(&inst->source.data_type);

    char *str = malloc((strlen(dst) * 2) + strlen(src) + strlen(dst_type) + strlen(src_type) + 32);

    switch (inst->type) {
        case LIR_NOP:
        case LIR_EOF:
        case LIR_START_BLOCK:
        case LIR_END_BLOCK:
            str[0] = '\0';
            break;
        case LIR_START_FUNC:
            sprintf(str, "func %s %s {\n", src_type, src);
            break;
        case LIR_END_FUNC:
            strcpy(str, "}\n\n");
            break;
        case LIR_RETURN:
            if (inst->source.type == OPER_NONE)
                strcpy(str, "ret\n");
            else
                sprintf(str, "ret %s %s\n", src_type, src);
            break;
        case LIR_LOAD:
        case LIR_STORE:
            sprintf(str, "%s = %s %s\n", dst, dst_type, src);
            break;
        case LIR_ADD:
            sprintf(str, "%s += %s %s\n", dst, dst_type, src);
            break;
        case LIR_SUB:
            sprintf(str, "%s -= %s %s\n", dst, dst_type, src);
            break;
        case LIR_MUL:
            sprintf(str, "%s *= %s %s\n", dst, dst_type, src);
            break;
        case LIR_DIV:
            sprintf(str, "%s /= %s %s\n", dst, dst_type, src);
            break;
        case LIR_MOD:
            sprintf(str, "%s %%= %s %s\n", dst, dst_type, src);
            break;
        case LIR_AND:
            sprintf(str, "%s &= %s %s\n", dst, dst_type, src);
            break;
        case LIR_OR:
            sprintf(str, "%s |= %s %s\n", dst, dst_type, src);
            break;
        case LIR_XOR:
            sprintf(str, "%s ^= %s %s\n", dst, dst_type, src);
            break;
        case LIR_SHL:
            sprintf(str, "%s <<= %s %s\n", dst, dst_type, src);
            break;
        case LIR_SHR:
            sprintf(str, "%s >>= %s %s\n", dst, dst_type, src);
            break;
        case LIR_PUSH:
            sprintf(str, "push %s %s\n", src_type, src);
            break;
        case LIR_POP:
            sprintf(str, "pop %s %s\n", dst_type, dst);
            break;
        case LIR_ASM:
            sprintf(str, "asm(%s)\n", src);
            break;
        case LIR_CALL:
            if (inst->destination.type == OPER_NONE)
                sprintf(str, "%s %s()\n", src_type, src);
            else
                sprintf(str, "%s = %s %s()\n", dst, src_type, src);
            break;
        case LIR_SETE:
            sprintf(str, ": == %s\n", dst);
            break;
        case LIR_SETNE:
            sprintf(str, ": != %s\n", dst);
            break;
        case LIR_SETLT:
            sprintf(str, ": < %s\n", dst);
            break;
        case LIR_SETLTE:
            sprintf(str, ": <= %s\n", dst);
            break;
        case LIR_SETGT:
            sprintf(str, ": > %s\n", dst);
            break;
        case LIR_SETGTE:
            sprintf(str, ": >= %s\n", dst);
            break;
        case LIR_COMPARE:
            sprintf(str, "%s ? %s %s\n", dst, src_type, src);
            break;
        case LIR_NEW_LABEL:
            sprintf(str, "%s:\n", src);
            break;
        case LIR_JMP:
            sprintf(str, "goto %s\n", dst);
            break;
        case LIR_JMP_TRUE:
            sprintf(str, "goto %s if %s\n", dst, src);
            break;
        case LIR_JMP_FALSE:
            sprintf(str, "goto %s if !%s\n", dst, src);
            break;
        case LIR_REFERENCE:
            sprintf(str, "%s = %s ptr %s\n", dst, src_type, src);
            break;
        case LIR_NEG:
            sprintf(str, "%s = %s -%s\n", dst, dst_type, dst);
            break;
        case LIR_NOT:
            sprintf(str, "%s = %s ~%s\n", dst, dst_type, dst);
            break;
        case LIR_BOOL_NOT:
            sprintf(str, "%s = %s !%s\n", dst, dst_type, dst);
            break;
        case LIR_EXTERN:
            sprintf(str, "extern %s\n", src);
            break;
        default:
            assert(false);
            str[0] = '\0';
            break;
    }

    free(dst_type);
    free(src_type);
    free(dst);
    free(src);
    return str;
}

static char *lir_instruction_to_string(LIR_Instruction *inst, const bool explicit, const bool show_lib_externs) {
    // This function wasn't decorated, so we know it is a lib extern.
    if (!show_lib_externs && inst->type == LIR_EXTERN && !(inst->source.function.flags & DECOR_EXTERN_FUNCTION))
        return calloc(1, sizeof(char));

    if (!explicit)
        return lir_instruction_to_string_implicit(inst);

    char *dst = lir_operand_to_string(&inst->destination);
    char *dst_type = data_type_to_string(&inst->destination.data_type);

    char *src = lir_operand_to_string(&inst->source);
    char *src_type = data_type_to_string(&inst->source.data_type);

    char *str = malloc((strlen(dst) * 2) + strlen(src) + strlen(dst_type) + strlen(src_type) + 64);

    switch (inst->type) {
        case LIR_NOP:
        case LIR_EOF:
            str[0] = '\0';
            break;
        case LIR_START_FUNC:
            sprintf(str, "func %s %s\n", src_type, src);
            break;
        case LIR_END_FUNC:
            sprintf(str, "end %s %s\n", src_type, src);
            break;
        case LIR_RETURN:
            sprintf(str, "ret %s %s\n", src_type, src);
            break;
        case LIR_LOAD:
            sprintf(str, "load %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_STORE:
            sprintf(str, "store %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_ADD:
            sprintf(str, "add %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_SUB:
            sprintf(str, "sub %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_MUL:
            sprintf(str, "mul %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_DIV:
            sprintf(str, "div %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_MOD:
            sprintf(str, "mod %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_AND:
            sprintf(str, "and %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_OR:
            sprintf(str, "or %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_XOR:
            sprintf(str, "xor %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_SHL:
            sprintf(str, "shl %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_SHR:
            sprintf(str, "shr %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_PUSH:
            sprintf(str, "push %s %s\n", src_type, src);
            break;
        case LIR_POP:
            sprintf(str, "pop %s %s\n", dst_type, dst);
            break;
        case LIR_ASM:
            sprintf(str, "asm(%s)\n", src);
            break;
        case LIR_CALL:
            sprintf(str, "load %s %s, %s call %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_SETE:
            sprintf(str, "sete %s\n", dst);
            break;
        case LIR_SETNE:
            sprintf(str, "setne %s\n", dst);
            break;
        case LIR_SETLT:
            sprintf(str, "setlt %s\n", dst);
            break;
        case LIR_SETLTE:
            sprintf(str, "setlte %s\n", dst);
            break;
        case LIR_SETGT:
            sprintf(str, "setgt %s\n", dst);
            break;
        case LIR_SETGTE:
            sprintf(str, "setgte %s\n", dst);
            break;
        case LIR_COMPARE:
            sprintf(str, "compare %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_NEW_LABEL:
            sprintf(str, "new label %s:\n", src);
            break;
        case LIR_JMP:
            sprintf(str, "jmp %s\n", dst);
            break;
        case LIR_JMP_TRUE:
            sprintf(str, "jmp true %s, %s\n", dst, src);
            break;
        case LIR_JMP_FALSE:
            sprintf(str, "jmp false %s, %s\n", dst, src);
            break;
        case LIR_START_BLOCK:
            strcpy(str, "start block\n");
            break;
        case LIR_END_BLOCK:
            strcpy(str, "end block\n");
            break;
        case LIR_REFERENCE:
            sprintf(str, "ref %s %s, %s %s\n", dst_type, dst, src_type, src);
            break;
        case LIR_NEG:
            sprintf(str, "neg %s %s\n", dst_type, dst);
            break;
        case LIR_NOT:
            sprintf(str, "not %s %s\n", dst_type, dst);
            break;
        case LIR_BOOL_NOT:
            sprintf(str, "bool not %s %s\n", dst_type, dst);
            break;
        case LIR_EXTERN:
            sprintf(str, "extern %s\n", src);
            break;
        default:
            assert(false);
            str[0] = '\0';
            break;
    }

    free(dst_type);
    free(src_type);
    free(dst);
    free(src);
    return str;
}

static void format_lir_string(String_Builder *builder) {
    size_t indents = 0;

    for (size_t i = 0; i < builder->length; i++) {
        if (builder->data[i] == '{') {
            indents++;
            continue;
        } else if (builder->data[i] != '\n')
            continue;

        if (i + 1 < builder->length) {
            if (builder->data[i + 1] == '}')
                indents--;
            else if (builder->data[i + 1] == '@')
                continue; // Don't indent labels.
        }

        for (size_t j = 0; j < indents; j++)
            insert_string(builder, "    ", 4, i + 1);
    }
}

char *lir_to_string(LIR *lir, const bool explicit_, const bool show_lib_externs) {
    String_Builder builder = create_string_builder();

    for (size_t i = 0; i < lir->count; i++) {
        char *str = lir_instruction_to_string(&lir->instructions[i], explicit_, show_lib_externs);
        append_whole_string(&builder, str);
        free(str);
    }

    format_lir_string(&builder);

    if (explicit_)
        builder.data[builder.length - 1] = '\0'; // Remove extra newline.

    return string_builder_to_cstring(&builder);
}
