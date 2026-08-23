#include "backend.h"
#include "lir.h"
#include "utilities.h"
#include "string_builder.h"
#include "backend_utilities.h"
#include "logger.h"
#include "context.h"
#include "decorators.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

static bool is_memory(LIR_Operand *oper) {
    switch (oper->type) {
        case OPER_FLOAT:
        case OPER_STRING:
        case OPER_LOCAL_VARIABLE:
        case OPER_POINTER: return true;
        case OPER_ARGUMENT: {
            const size_t num = dt_is_float(oper->data_type) ? oper->argument.floats_passed : oper->argument.ints_passed;
            return num >= MAX_REGISTER_ARGUMENT_COUNT;
        }
        default: return false;
    }
}

static inline char float_type_char(const Primitive_Type primitive_type) {
    return primitive_type == PRIM_F32 ? 's' : 'd';
}

static inline char extension_type_to_char(const Primitive_Type primitive_type) {
    return bin_is_unsigned(primitive_type) ? 'z' : 's';
}

size_t struct_data_type_to_size(Context *context, const Data_Type *data_type, const Module *module) {
    Custom_Type *type = find_custom_type(context, CUST_STRUCT, data_type->custom_name, data_type->custom_length, 
        data_type->module_name != NULL ? (size_t)data_type->module_uid : (module == NULL ? 0 : module->uid));

    assert(type != NULL);
    size_t size = 0;

    for (size_t i = 0; i < type->member_count; i++)
        size += primitive_type_to_size(data_type_to_primitive_type(&type->members[i].data_type))
            * (type->members[i].data_type.array_size == 0 ? 1 : type->members[i].data_type.array_size);

    return size;
}

static char *emit_instruction(State *state, LIR_Instruction *inst);

char *emit_assembly(Context *context, LIR *lir, const Symbol *entrypoint, const bool initialize_heap) {
    State state = create_state(context);
    String_Builder builder = create_string_builder();
        
    if (entrypoint != NULL) {
        append_whole_string(&builder, "default abs\nglobal _start\nsection .text\n");

        const Module *module = get_module(context, entrypoint->module_uid);
        char *name = malloc(module->name_length + entrypoint->name_length + 4);
        sprintf(name, "_%.*s__%.*s", (int)module->name_length, module->name,
            (int)entrypoint->name_length, entrypoint->name);

        append_whole_string(&builder, "_start:\n");

        if (initialize_heap)
            append_whole_string(&builder, "call _basic____internal_allocate_heap\n");
        
        append_whole_string(&builder, "call ");
        append_whole_string(&builder, name);
        append_whole_string(&builder, "\n");

        if (initialize_heap)
            append_whole_string(&builder, "call _basic____internal_free_heap\n");

        append_whole_string(&builder, "mov rax, 60\nxor rdi, rdi\nsyscall\n");
        free(name);
    } else
        append_whole_string(&builder, "default abs\nsection .text\n");

    for (size_t i = 0; i < lir->count; i++) {
        char *code = emit_instruction(&state, &lir->instructions[i]);
        append_whole_string(&builder, code);
        free(code);
    }

    delete_state(&state);
    return string_builder_to_cstring(&builder);
}

static char *lir_int_to_string(LIR_Operand *operand) {
    char *code = malloc(32);

    if (operand->data_type.primitive_type == PRIM_U32)
        sprintf(code, "%" PRIu32, operand->int_.u32);
    else if (operand->data_type.primitive_type == PRIM_U64)
        sprintf(code, "%" PRIu64, operand->int_.u64);
    else if (operand->data_type.primitive_type == PRIM_I32)
        sprintf(code, "%" PRId32, operand->int_.i32);
    else
        sprintf(code, "%" PRId64, operand->int_.i64);

    return code;
}

static char *lir_float_to_string(State *state, LIR_Operand *operand) {
    char *code = malloc(32);
    sprintf(code, "%s [.c%zu]", 
        word_size_to_string(primitive_type_to_word_size(data_type_to_primitive_type(&operand->data_type))),
        new_float_label(state, operand->data_type.primitive_type == PRIM_F32, operand->float_.f32, operand->float_.f64));

    return code;
}

static char *lir_string_to_string(State *state, LIR_Operand *operand) {
    char *code = malloc(24);
    sprintf(code, ".c%zu", new_string_label(state, operand->string.string, operand->string.length));
    return code;
}

static char *lir_register_to_string(LIR_Operand *operand) {
    Primitive_Type type = data_type_to_primitive_type(&operand->data_type);

    if (operand->register_.temporary)
        return copy_whole_string(temporary_register(operand->register_.number, type));

    return copy_whole_string(hardware_register_to_string(operand->register_.number, type));
}

static char *lir_local_variable_to_string(State *state, LIR_Operand *operand) {
    Local_Variable *var = find_local_variable(state, operand->local_variable.uid);
    assert(var != NULL);

    char *code = malloc(32);
    sprintf(code, "%s [rbp%+d]", word_size_to_string(var->word_size), var->stack_offset);
    return code;
}

static char *lir_argument_to_string(State *state, LIR_Operand *operand) {
    const Primitive_Type type = data_type_to_primitive_type(&operand->data_type);
    const size_t num = bin_is_float(type) ? operand->argument.floats_passed : operand->argument.ints_passed;

    if (num < MAX_REGISTER_ARGUMENT_COUNT)
        return copy_whole_string(register_argument(num, type));

    // This argument was passed on the stack.

    char *code = malloc(64);

    if (operand->argument.is_caller)
        sprintf(code, "%s [rbp-%zu]", word_size_to_string(primitive_type_to_word_size(type)), 
            state->stack_used + (operand->argument.total_count - num) * 8);
    else
        sprintf(code, "%s [rbp+%zu]", word_size_to_string(primitive_type_to_word_size(type)), 
            (operand->argument.index - MAX_REGISTER_ARGUMENT_COUNT + 2) * 8);

    return code;
}

bool is_register_operand(const LIR_Operand *operand) {
    switch (operand->type) {
        case OPER_REGISTER: return true;
        case OPER_ARGUMENT:
            if (dt_is_float(operand->data_type) && operand->argument.floats_passed < MAX_REGISTER_ARGUMENT_COUNT)
                return true;
            else if (operand->argument.ints_passed < MAX_REGISTER_ARGUMENT_COUNT)
                return true;
            break;
        default: break;
    }

    return false;
}

static char *register_operand_to_string(State *state, LIR_Operand *operand, const Primitive_Type type) {
    assert(is_register_operand(operand));
    operand->data_type.primitive_type = type;

    if (operand->type == OPER_ARGUMENT)
        return lir_argument_to_string(state, operand);
    
    return lir_register_to_string(operand);
}

static char *label_operand_to_string(LIR_Operand *operand) {
    char *code = malloc(16);
    sprintf(code, ".l%zu", operand->label.number);
    return code;
}

static char *pointer_operand_to_string(LIR_Operand *operand) {
    const Primitive_Type type = data_type_to_primitive_type(&operand->data_type);

    char *code = malloc(32);
    sprintf(code, "%s [%s]", word_size_to_string(primitive_type_to_word_size(type)), 
        temporary_register(operand->pointer.register_.number, PRIM_USIZE));
    return code;
}

static char *sizeof_operand_to_string(State *state, LIR_Operand *operand) {
    char *str = malloc(16);

    if (operand->sizeof_.data_type.primitive_type == PRIM_CUSTOM) {
        if (operand->sizeof_.data_type.pointer_count == 0)
            sprintf(str, "%zu", struct_data_type_to_size(state->context, &operand->sizeof_.data_type, 
                get_module(state->context, (size_t)operand->data_type.module_uid)));
        else
            strcpy(str, "8");
    } else
        sprintf(str, "%zu", primitive_type_to_size(data_type_to_primitive_type(&operand->sizeof_.data_type)));

    return str;
}

static char *lir_operand_to_string(State *state, LIR_Operand *operand) {
    switch (operand->type) {
        case OPER_NONE: return copy_whole_string("0");
        case OPER_INT: return lir_int_to_string(operand);
        case OPER_FLOAT: return lir_float_to_string(state, operand);
        case OPER_STRING: return lir_string_to_string(state, operand);
        case OPER_REGISTER: return lir_register_to_string(operand);
        case OPER_LOCAL_VARIABLE: return lir_local_variable_to_string(state, operand);
        case OPER_ARGUMENT: return lir_argument_to_string(state, operand);
        case OPER_LABEL: return label_operand_to_string(operand);
        case OPER_POINTER: return pointer_operand_to_string(operand);
        case OPER_SIZEOF: return sizeof_operand_to_string(state, operand);
        default: break;
    }

    log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL,
        "No backend value for LIR operand type '%s'\n", lir_operand_type_to_string(operand->type));
    return copy_whole_string("<none>");
}

static char *emit_start_function(State *state, LIR_Instruction *inst) {
    const Module *module = get_module(state->context, inst->source.function.module_uid);
    char *code = malloc(inst->source.function.length + module->name_length + 36);
    sprintf(code, "_%.*s__%.*s:\n%s",
                  (int)module->name_length, module->name, 
                  (int)inst->source.function.length, inst->source.function.name,
                  inst->source.function.flags & DECOR_OMIT_FRAME_POINTER ? "\0" : "push rbp\nmov rbp, rsp\n");

    if (!inst->source.function.exported)
        return code;

    char *global = malloc(strlen(code) + inst->source.function.length + module->name_length + 22);
    sprintf(global, "global _%.*s__%.*s\n%s",
                    (int)module->name_length, module->name, 
                    (int)inst->source.function.length, inst->source.function.name, code);

    free(code);
    return global;
}

static char *emit_end_function(State *state) {
    char *rodata = copy_string(state->rodata_section.data, state->rodata_section.length);
    clear_local_variables(state);
    clear_rodata_section(state);
    clear_stack(state);
    return rodata;
}

static bool is_self_load(LIR_Operand *dst, LIR_Operand *src, char *dst_str, char *src_str) {
    return (dt_is_float(dst->data_type) == dt_is_float(src->data_type) && 
        data_type_bit_sizes_equal(dst->data_type, src->data_type) &&
        strcmp(dst_str, src_str) == 0);
}

static bool should_return_none_from_self_load(LIR_Operand *dst, LIR_Operand *src, char *code, char *dst_str, char *src_str, char *dst_reg_str) {
    if (!is_self_load(dst, src, dst_reg_str, src_str))
        return false;

    free(code);
    free(dst_str);
    free(dst_reg_str);
    free(src_str);
    return true;
}

static char *emit_load_8bit_destination(State *state, LIR_Operand *dst, LIR_Operand *src, char *dst_str, char *src_str,
        Primitive_Type dst_type, Primitive_Type src_type) {
    assert(dst->type == OPER_REGISTER);

    char *code = malloc((strlen(dst_str) * 3) + strlen(src_str) + 64);
    char *dst_reg_str = NULL;

    if (bin_is_float(src_type)) {
        if (src_type == PRIM_BOOL || src_type == PRIM_I8 || src_type == PRIM_U8)
            sprintf(code, "movs%c %s, %s\n"
                          "cvtts%c2si %s, %s\n", 
                          float_type_char(src_type), temporary_register(REGISTER_3, PRIM_I32), src_str, 
                          float_type_char(src_type), dst_str, temporary_register(REGISTER_3, PRIM_I32));
        else
            sprintf(code, "cvtts%c2si %s, %s\n", 
                float_type_char(src_type), temporary_register(dst->register_.number, 
                    dst_type == PRIM_I8 || dst_type == PRIM_I16 || dst_type == PRIM_U8 || 
                    dst_type == PRIM_U16 || dst_type == PRIM_BOOL ? PRIM_I32 : dst_type), src_str);

        free(dst_str);
        free(src_str);
        return code;
    }

    bool extend = false;

    if (dst_type == PRIM_I8) {
        dst_type = PRIM_I64;
        extend = true;
    } else if (dst_type == PRIM_U8 || dst_type == PRIM_BOOL) {
        dst_type = PRIM_U64;
        extend = true;
    }

    dst_reg_str = register_operand_to_string(state, dst, extend ? dst_type : src_type);

    if (should_return_none_from_self_load(dst, src, code, dst_str, src_str, dst_reg_str))
        return calloc(1, sizeof(char));

    if (src->type != OPER_INT && (src_type == PRIM_I8 || src_type == PRIM_I16 || (dst_type == PRIM_I64 && extend)))
        sprintf(code, "movsx %s, %s\n", dst_reg_str, src_str);
    else if (src->type != OPER_INT && (src_type == PRIM_U8 || src_type == PRIM_U16 || (dst_type == PRIM_U64 && extend))) {
        if (primitive_type_to_bit_size(src_type) <= 16)
            sprintf(code, "movzx %s, %s\n", dst_reg_str, src_str);
        else {
            // Can't movzx a 32 bit source operand.
            //code[0] = '\0';
            free(dst_reg_str);
            dst_reg_str = register_operand_to_string(state, dst, src_type);
            sprintf(code, "mov %s, %s\n", dst_reg_str, src_str);
        }
    } else
        sprintf(code, "mov %s, %s\n", dst_reg_str, src_str);

    if (dst_reg_str != NULL)
        free(dst_reg_str);

    free(dst_str);
    free(src_str);
    return code;
}

static char *emit_store(State *state, LIR_Instruction *inst);
static char *emit_reference(State *state, LIR_Instruction *inst);

static char *emit_load(State *state, LIR_Instruction *inst) {
    assert(is_register_operand(&inst->destination));

    LIR_Operand *dst = &inst->destination;
    LIR_Operand *src = &inst->source;

    const Primitive_Type dst_type = data_type_to_primitive_type(&dst->data_type);
    const Primitive_Type src_type = data_type_to_primitive_type(&src->data_type);

    char *dst_str = lir_operand_to_string(state, dst);
    char *src_str = lir_operand_to_string(state, src);
    char *dst_reg_str = NULL; // We may need to change the bitsize of a register, but don't know at this point.

    // Let's just handle float to int conversions here so we don't have to
    // copy and paste it in every single int case.
    // For bool, i8 and u8 we don't have to mess with extending or moving around sources
    // with lower bit sizes, so we can return early.
    if ((!bin_is_float(dst_type) && bin_is_float(src_type)) || 
            dst_type == PRIM_BOOL || dst_type == PRIM_I8 || dst_type == PRIM_U8)
        return emit_load_8bit_destination(state, dst, src, dst_str, src_str, dst_type, src_type);

    char *code = malloc((strlen(dst_str) * 3) + strlen(src_str) + 64);

    /* I just can't be bothered to fix this damn rel issue.
    if (src->type == OPER_STRING) {
        assert(!bin_is_float(dst_type));
        sprintf(code, "lea %s, [%s]\n", dst_str, src_str);
        free(dst_str);
        free(src_str);
        return code;
    }
    */
    
    switch (dst_type) {
        case PRIM_F32:
        case PRIM_F64:
            if (dst_type == PRIM_F32 && src_type == PRIM_F64)
                sprintf(code, "cvtsd2ss %s, %s\n", dst_str, src_str);
            else if (dst_type == PRIM_F64 && src_type == PRIM_F32)
                sprintf(code, "cvtss2sd %s, %s\n", dst_str, src_str);
            else if (src->type == OPER_INT)
                sprintf(code, "mov %s, %s\n"
                              "cvtsi2s%c %s, %s\n", 
                              temporary_register(REGISTER_3, src_type), src_str,
                              float_type_char(dst_type), dst_str, temporary_register(REGISTER_3, src_type));
            else if (src_type == PRIM_U8 || src_type == PRIM_U16 || src_type == PRIM_I8 || src_type == PRIM_I16)
                sprintf(code, "mov%cx %s, %s\n"
                              "cvtsi2s%c %s, %s\n",
                              extension_type_to_char(src_type), temporary_register(REGISTER_3, PRIM_U32), src_str,
                              float_type_char(dst_type), dst_str, temporary_register(REGISTER_3, PRIM_U32));
            else if (!bin_is_float(src_type))
                sprintf(code, "cvtsi2s%c %s, %s\n", float_type_char(dst_type), dst_str, src_str);
            else
                sprintf(code, "movs%c %s, %s\n", float_type_char(dst_type), dst_str, src_str);
            break;
        case PRIM_I16:
        case PRIM_U16:
            if (src_type == PRIM_I8 || src_type == PRIM_U8) {
                if (src->type == OPER_INT)
                    sprintf(code, "mov %s, %s\n", dst_str, src_str);
                else
                    sprintf(code, "mov%cx %s, %s\n", extension_type_to_char(src_type), dst_str, src_str);
            } else {
                dst_reg_str = register_operand_to_string(state, dst, src_type);

                if (should_return_none_from_self_load(dst, src, code, dst_str, src_str, dst_reg_str))
                    return calloc(1, sizeof(char));

                sprintf(code, "mov %s, %s\n", dst_reg_str, src_str);
            }
            break;
        case PRIM_I32:
        case PRIM_U32:
            if (src_type == PRIM_I8 || src_type == PRIM_U8 || src_type == PRIM_I16 || src_type == PRIM_U16) {
                if (src->type == OPER_INT)
                    sprintf(code, "mov %s, %s\n", dst_str, src_str);
                else
                    sprintf(code, "mov%cx %s, %s\n", extension_type_to_char(src_type), dst_str, src_str);
            } else {
                dst_reg_str = register_operand_to_string(state, dst, src_type);

                if (should_return_none_from_self_load(dst, src, code, dst_str, src_str, dst_reg_str))
                    return calloc(1, sizeof(char));

                sprintf(code, "mov %s, %s\n", dst_reg_str, src_str);
            }
            break;
        default:
            if (src_type == PRIM_I8 || src_type == PRIM_U8 || src_type == PRIM_I16 || src_type == PRIM_U16) {
                if (src->type == OPER_INT)
                    sprintf(code, "mov %s, %s\n", dst_str, src_str);
                else
                    sprintf(code, "mov%cx %s, %s\n", extension_type_to_char(src_type), dst_str, src_str);
            } else if (src_type == PRIM_I32 && src->type != OPER_INT)
                sprintf(code, "movsxd %s, %s\n", dst_str, src_str);
            else {
                dst_reg_str = register_operand_to_string(state, dst, src_type);

                if (should_return_none_from_self_load(dst, src, code, dst_str, src_str, dst_reg_str))
                    return calloc(1, sizeof(char));

                sprintf(code, "mov %s, %s\n", dst_reg_str, src_str);
            }
            break;
    }

    free(dst_str);
    free(src_str);

    if (dst_reg_str != NULL)
        free(dst_reg_str);

    return code;
}

static char *emit_push(State *state, LIR_Instruction *inst);

static char *push_argument_onto_stack(State *state, LIR_Instruction *inst) {
    const size_t num = dt_is_float(inst->destination.data_type) ? 
        inst->destination.argument.floats_passed : inst->destination.argument.ints_passed;

    if (num < MAX_REGISTER_ARGUMENT_COUNT)
        return NULL;

    // the 7th and above call arguments need to be pushed onto the stack.
    LIR_Instruction push = (LIR_Instruction){ .type = LIR_PUSH, 
        .destination = (LIR_Operand){ .type = OPER_NONE, .data_type = NO_DATA_TYPE },
        .source = inst->source };
    return emit_push(state, &push);
}

static char *allocate_variable_if_needed(State *state, LIR_Instruction *inst) {
    if (inst->destination.type != OPER_LOCAL_VARIABLE || find_local_variable(state, inst->destination.local_variable.uid) != NULL)
        return NULL;

    if (inst->destination.local_variable.exists_from_callee) {
        assert(inst->source.type == OPER_ARGUMENT);
        const size_t num = dt_is_float(inst->source.data_type) ? 
            inst->source.argument.floats_passed : inst->source.argument.ints_passed;

        // Check if this argument was passed on the stack.
        if (num >= MAX_REGISTER_ARGUMENT_COUNT) {
            // The stack space was already made by the caller, we don't need to allocate into this stack frame.
            allocate_local_variable_from_call_argument(state, inst->destination.local_variable.uid, inst->destination.data_type, &inst->source);
            // We have already added the local variable to this function scope, so we can return.
            return calloc(1, sizeof(char));
        }
    }

    size_t type_size;

    if (inst->destination.data_type.primitive_type == PRIM_CUSTOM && inst->destination.data_type.pointer_count == 0)
        type_size = struct_data_type_to_size(state->context, &inst->destination.data_type, FIND_IN_ANY_MODULE);
    else
        type_size = primitive_type_to_size(data_type_to_primitive_type(&inst->destination.data_type));

    return allocate_local_variable(state, inst->destination.local_variable.uid, inst->destination.data_type, type_size);
}

static char *emit_store(State *state, LIR_Instruction *inst) {
    LIR_Operand *dst = &inst->destination;
    LIR_Operand *src = &inst->source;
    char *code = NULL;

    if (dst->type == OPER_ARGUMENT && (code = push_argument_onto_stack(state, inst)) != NULL)
        return code;

    char *alloc = allocate_variable_if_needed(state, inst);

    if (src->type == OPER_NONE)
        return alloc == NULL ? calloc(1, sizeof(char)) : alloc;

    char *load = NULL;
    LIR_Operand load_reg;

    if (is_memory(dst) && is_memory(src)){
        // This was probably due to an optimization which removed a load instruction.
        Data_Type type = dst->data_type;

        load_reg = (LIR_Operand){ .type = OPER_REGISTER, .data_type = type, 
            .register_.number = REGISTER_3, .register_.temporary = true };

        LIR_Instruction load_inst = (LIR_Instruction){ .type = LIR_LOAD, .destination = load_reg, .source = *src };
        load = emit_load(state, &load_inst);
        src = &load_reg;
    }

    Primitive_Type type;

    // We want to dereference the pointer data type to store into it.
    if (dst->type == OPER_POINTER)
        type = dst->data_type.primitive_type;
    else
        type = data_type_to_primitive_type(&dst->data_type);

    Primitive_Type src_type = data_type_to_primitive_type(&src->data_type);

    const bool dst_is_register = is_register_operand(dst);
    const bool src_is_register = is_register_operand(src);

    const bool dst_is_float = bin_is_float(type);
    const bool src_is_float = bin_is_float(src_type);

    char *dst_str = lir_operand_to_string(state, dst);
    char *src_str = lir_operand_to_string(state, src);
    code = malloc((strlen(dst_str) + strlen(src_str)) * 2 + 64);

    if (dst_is_float) {
        if (src->type == OPER_FLOAT) {
            if (type == PRIM_F64 && src_type == PRIM_F32)
                sprintf(code, "cvtss2sd %s, %s\n"
                              "movsd %s, %s\n", temporary_register(REGISTER_3, type), src_str,
                              dst_str, temporary_register(REGISTER_3, type));
            else if (type == PRIM_F32 && src_type == PRIM_F64)
                sprintf(code, "cvtsd2ss %s, %s\n"
                              "movss %s, %s\n", temporary_register(REGISTER_3, type), src_str,
                              dst_str, temporary_register(REGISTER_3, type));
            else
                sprintf(code, "movs%c %s, %s\n"
                              "movs%c %s, %s\n", float_type_char(type), temporary_register(REGISTER_3, type), src_str,
                              float_type_char(type), dst_str, temporary_register(REGISTER_3, type));
        } else if (src->type == OPER_INT) {
            if (dst_is_register)
                sprintf(code, "mov %s, %s\n"
                              "cvtsi2s%c %s, %s\n", temporary_register(REGISTER_3, src_type), src_str, 
                              float_type_char(type),
                              dst_str, temporary_register(REGISTER_3, src_type));
            else
                sprintf(code, "mov %s, %s\n"
                              "cvtsi2s%c %s, %s\n"
                              "movs%c %s, %s\n", temporary_register(REGISTER_3, src_type), src_str, float_type_char(type),
                              temporary_register(REGISTER_3, type), temporary_register(REGISTER_3, src_type),
                              float_type_char(type), dst_str, temporary_register(REGISTER_3, type));
        } else if (!src_is_float)
            sprintf(code, "cvtsi2s%c %s, %s\n", float_type_char(type), dst_str, src_str);
        else
            sprintf(code, "movs%c %s, %s\n", float_type_char(type), dst_str, src_str);
    } else if (type == PRIM_BOOL) {
        if (src->type != OPER_REGISTER) {
            const char *reg = temporary_register(REGISTER_3, src_type);
            sprintf(code, "mov %s, %s\n"
                          "test %s, %s\n"
                          "setnz %s\n"
                          "mov %s, %s\n", reg, src_str, reg, reg,
                          temporary_register(REGISTER_3, PRIM_I8), dst_str, temporary_register(REGISTER_3, type));
        } else {
            char *reg = register_operand_to_string(state, src, PRIM_I8);
            sprintf(code, "test %s, %s\n"
                          "setnz %s\n"
                          "mov %s, %s\n", src_str, src_str, reg, dst_str, src_str);
            free(reg);
        }
    } else if (src_is_register) {
        char *reg = register_operand_to_string(state, src, type);

        // Check if we need to store into a higher bit size of the destination register
        // if the source is a higher bit size than the destination.
        const bool dst_is_lower_bit_size = primitive_type_to_bit_size(type) < primitive_type_to_bit_size(src_type);

        if (dst_is_register && dst_is_lower_bit_size) {
            char *dst_reg = register_operand_to_string(state, dst, src_type);

            free(reg);
            reg = register_operand_to_string(state, src, src_type);

            sprintf(code, "mov %s, %s\n", dst_reg, reg);
            free(dst_reg);
        } else if (!dst_is_register && dst_is_lower_bit_size) {
            free(reg);
            src->data_type = dst->data_type;
            reg = register_operand_to_string(state, src, type);
            sprintf(code, "mov %s, %s\n", dst_str, reg);
        } else
            sprintf(code, "mov %s, %s\n", dst_str, reg);

        free(reg);
    } else
        sprintf(code, "mov %s, %s\n", dst_str, src_str);

    free(dst_str);
    free(src_str);

    if (load != NULL) {
        load = realloc(load, strlen(load) + strlen(code ) + 1);
        strcat(load, code);
        free(code);
        code = load;
    }

    if (alloc == NULL)
        return code;

    alloc = realloc(alloc, strlen(alloc) + strlen(code) + 1);
    strcat(alloc, code);
    free(code);
    return alloc;
}

static char *emit_return(State *state, LIR_Instruction *inst) {
    if (inst->source.type == OPER_NONE || inst->source.type == OPER_FUNCTION) {
        if (inst->destination.function.flags & DECOR_OMIT_FRAME_POINTER)
            return copy_whole_string("ret\n");

        return copy_whole_string(state->stack_reserved > 0 ? "leave\nret\n" :
            "pop rbp\nret\n");
    }

    // Load the value into the return register.
    LIR_Operand reg = (LIR_Operand){ .type = OPER_REGISTER, .data_type = inst->destination.data_type, 
        .register_.number = 0, .register_.temporary = false };

    LIR_Instruction load = (LIR_Instruction){ .type = LIR_LOAD, .destination = reg, .source = inst->source };
    char *value = emit_load(state, &load);

    value = realloc(value, strlen(value) + 13);

    if (inst->destination.function.flags & DECOR_OMIT_FRAME_POINTER)
        strcat(value, "ret\n");
    else
        strcat(value, state->stack_reserved > 0 ? "leave\nret\n" : "pop rbp\nret\n");

    return value;
}

static char *load_math_into_matching_data_type_operands(State *state, LIR_Instruction *inst) {
    // To keep up with the optimizer. Sometimes we also get 64 and 32 bits clashing, like the following:
    //     add rax, dword [rbp-4]
    // This could be between a usize and a u32 for example.

    const bool dst_is_mem = is_memory(&inst->destination);
    const bool src_is_mem = is_memory(&inst->source);

    if (!dst_is_mem && data_types_equal(inst->destination.data_type, inst->source.data_type) &&
            data_type_bit_sizes_equal(inst->destination.data_type, inst->source.data_type))
        return NULL;

    Data_Type type = infer_between_two_types(inst->destination.data_type, inst->source.data_type);
    char *load_dst = NULL;
    char *load_src = NULL;

    if (dst_is_mem) {
        LIR_Instruction load_inst = (LIR_Instruction){ .type = LIR_LOAD, 
            .destination = (LIR_Operand){ .type = OPER_REGISTER, .data_type = type, .register_.number = REGISTER_1, .register_.temporary = true },
            .source = inst->destination };

        load_dst = emit_load(state, &load_inst);
        inst->destination = load_inst.destination;
    }

    if (src_is_mem && !data_types_equal(type, inst->source.data_type) &&
            !data_type_bit_sizes_equal(type, inst->source.data_type)) {
        LIR_Instruction load_inst = (LIR_Instruction){ .type = LIR_LOAD, 
            .destination = (LIR_Operand){ .type = OPER_REGISTER, .data_type = type, .register_.number = REGISTER_2, .register_.temporary = true },
            .source = inst->source };

        load_src = emit_load(state, &load_inst);
        inst->source = load_inst.destination;
    }

    if (load_dst != NULL && load_src != NULL) {
        load_dst = realloc(load_dst, strlen(load_dst) + strlen(load_src) + 1);
        strcat(load_dst, load_src);
        free(load_src);
        return load_dst;
    }

    return load_dst != NULL ? load_dst : load_src;
}

static char *emit_math(State *state, LIR_Instruction *inst) {
    char *load_mem = load_math_into_matching_data_type_operands(state, inst);

    const Primitive_Type dst_type = data_type_to_primitive_type(&inst->destination.data_type);

    // Match datatypes for math, if no float conversions are needed.
    if (bin_is_float(dst_type) == dt_is_float(inst->source.data_type))
        inst->source.data_type = inst->destination.data_type;

    const Primitive_Type src_type = data_type_to_primitive_type(&inst->source.data_type);

    const bool is_float = dt_is_float(inst->destination.data_type);
    const bool is_unsigned = dt_is_unsigned(inst->destination.data_type);

    const bool is_32 = inst->source.data_type.pointer_count == 0 && (dst_type != PRIM_F64 && dst_type != PRIM_I64 &&
        dst_type != PRIM_U64 && dst_type != PRIM_ISIZE && dst_type != PRIM_USIZE);

    const char extension = is_float ? float_type_char(dst_type) : extension_type_to_char(dst_type);

    char *dst = lir_operand_to_string(state, &inst->destination);

    // TODO: We CAN shift with an integer immediate even if we're shifting a float.
    if ((inst->type == LIR_SHL || inst->type == LIR_SHR) && 
            inst->source.type == OPER_REGISTER && !bin_is_float(dst_type)) {
        assert(!bin_is_float(src_type));
        inst->source.data_type.primitive_type = PRIM_I8; // Can only bitshift with 8 bit integers (when doing integer shifting).
    }

    char *src = lir_operand_to_string(state, &inst->source);
    char *code = malloc(strlen(dst) + strlen(src) + 128);

    switch (inst->type) {
        case LIR_ADD:
            if (is_float) {
                if (inst->source.type == OPER_INT)
                    sprintf(code, "mov %s, %s\n"
                                  "cvtsi2s%c %s, %s\n"
                                  "adds%c %s, %s\n", temporary_register(REGISTER_1, PRIM_I64), src, 
                                  extension, temporary_register(REGISTER_3, dst_type), temporary_register(REGISTER_1, PRIM_I64),
                                  extension, dst, temporary_register(REGISTER_3, dst_type));
                else
                    sprintf(code, "adds%c %s, %s\n", extension, dst, src);
            } else
                sprintf(code, "add %s, %s\n", dst, src);
            break;
        case LIR_SUB:
            if (is_float) {
                if (inst->source.type == OPER_INT)
                    sprintf(code, "mov %s, %s\n"
                                  "cvtsi2s%c %s, %s\n"
                                  "subs%c %s, %s\n", temporary_register(REGISTER_1, PRIM_I64), src, 
                                  extension, temporary_register(REGISTER_3, dst_type), temporary_register(REGISTER_1, PRIM_I64),
                                  extension, dst, temporary_register(REGISTER_3, dst_type));
                else
                sprintf(code, "subs%c %s, %s\n", extension, dst, src);
            } else
                sprintf(code, "sub %s, %s\n", dst, src);
            break;
        case LIR_MUL:
            if (is_float) {
                if (!bin_is_float(src_type)) {
                    if (inst->source.type == OPER_INT) {
                        Primitive_Type atleast_32;

                        if (bin_is_unsigned(src_type) && src_type <= PRIM_U16)
                            atleast_32 = PRIM_U32;
                        else if (src_type <= PRIM_I16)
                            atleast_32 = PRIM_I32;
                        else
                            atleast_32 = src_type;

                        const char *reg = temporary_register(REGISTER_1, atleast_32);
                        sprintf(code, "mov %s, %s\n"
                                      "cvtsi2s%c %s, %s\n"
                                      "muls%c %s, %s\n", reg, src, extension, temporary_register(REGISTER_2, dst_type), reg, 
                                      extension, dst, temporary_register(REGISTER_2, dst_type));
                    } else {
                        sprintf(code, "cvtsi2s%c %s, %s\n"
                                      "muls%c %s, %s\n", extension, temporary_register(REGISTER_2, dst_type), src, 
                                      extension, dst, temporary_register(REGISTER_2, dst_type));
                    }
                } else
                    sprintf(code, "muls%c %s, %s\n", extension, dst, src);
            } else if (is_unsigned && (dst_type == PRIM_U64 || dst_type == PRIM_USIZE) && 
                inst->source.type != OPER_INT && inst->source.type != OPER_SIZEOF) {
                // We want to avoid using unsigned mul as much as we can because it can't
                // be propogated with an integer; it requires a register operand.
                //if (inst->source.type != OPER_REGISTER || inst->source.register_.number != RBX)
                //    sprintf(code, "mov %s, %s\n"
                //                  "mul %s\n", hardware_register_to_string(RBX, primitive_type), src, 
                //                  hardware_register_to_string(RBX, primitive_type));
                //else
                    sprintf(code, "mul %s\n", src);
            } else
                sprintf(code, "imul %s, %s\n", dst, src);
            break;
        case LIR_DIV:
            if (is_float) {
                if (!bin_is_float(src_type)) {
                    if (inst->source.type == OPER_INT) {
                        Primitive_Type atleast_32;

                        if (bin_is_unsigned(src_type) && src_type <= PRIM_U16)
                            atleast_32 = PRIM_U32;
                        else if (src_type <= PRIM_I16)
                            atleast_32 = PRIM_I32;
                        else
                            atleast_32 = src_type;

                        const char *reg = temporary_register(REGISTER_1, atleast_32);
                        sprintf(code, "mov %s, %s\n"
                                      "cvtsi2s%c %s, %s\n"
                                      "divs%c %s, %s\n", reg, src, extension, temporary_register(REGISTER_2, dst_type), reg, 
                                      extension, dst, temporary_register(REGISTER_2, dst_type));
                    } else {
                        sprintf(code, "cvtsi2s%c %s, %s\n"
                                      "divs%c %s, %s\n", extension, temporary_register(REGISTER_2, dst_type), src, 
                                      extension, dst, temporary_register(REGISTER_2, dst_type));
                    }
                } else
                    sprintf(code, "divs%c %s, %s\n", extension, dst, src);
            } else if (is_unsigned && inst->source.type != OPER_INT) {
                if (inst->source.type != OPER_REGISTER || inst->source.register_.number != RBX || inst->source.register_.temporary)
                    sprintf(code, "mov %s, %s\n"
                                  "xor rdx, rdx\n"
                                  "div %s\n", hardware_register_to_string(RBX, dst_type), src,
                                  hardware_register_to_string(RBX, dst_type));
                else
                    sprintf(code, "xor rdx, rdx\n"
                                  "div %s\n", src);
            } else
                sprintf(code, "%s\n"
                              "idiv %s\n", is_32 ? "cdq" : "cqo", src);
            break;
        case LIR_MOD: {
            if (is_float) {// This shouldn't happen.
                assert(false);
                code[0] = '\0';
            } else if (!is_unsigned || inst->source.type == OPER_INT)
                sprintf(code, "%s\n"
                              "idiv %s\n"
                              "mov %s, %s\n", is_32 ? "cdq" : "cqo", src,
                              dst, hardware_register_to_string(RDX, dst_type));
            else
                sprintf(code, "xor rdx, rdx\n"
                              "div %s\n"
                              "mov %s, %s\n", src,
                              dst, hardware_register_to_string(RDX, dst_type));
            break;
        }
        case LIR_AND:
            if (is_float)
                sprintf(code, "pand %s, %s\n", dst, src);
            else
                sprintf(code, "and %s, %s\n", dst, src);
            break;
        case LIR_OR:
            if (is_float)
                sprintf(code, "por %s, %s\n", dst, src);
            else
                sprintf(code, "or %s, %s\n", dst, src);
            break;
        case LIR_XOR:
            if (is_float)
                sprintf(code, "pxor %s, %s\n", dst, src);
            else
                sprintf(code, "xor %s, %s\n", dst, src);
            break;
        case LIR_SHL:
            if (is_float) {
                // SHOULD NOT HAPPEN.
                assert(false);
                code[0] = '\0';
            } else
                sprintf(code, "sal %s, %s\n", dst, src);
            break;
        default:
            if (is_float) {
                // SHOULD NOT HAPPEN.
                assert(false);
                code[0] = '\0';
            } else
                sprintf(code, "sar %s, %s\n", dst, src);
            break;
    }

    free(dst);
    free(src);

    if (load_mem == NULL)
        return code;

    load_mem = realloc(load_mem, strlen(load_mem) + strlen(code) + 1);
    strcat(load_mem, code);
    free(code);
    return load_mem;
}

static char *emit_push(State *state, LIR_Instruction *inst) {
    if (is_register_operand(&inst->source) && !dt_is_float(inst->source.data_type))
        inst->source.data_type.primitive_type = PRIM_I64;

    char *src = lir_operand_to_string(state, &inst->source);
    char *code = malloc(strlen(src) + 64);
    const Primitive_Type type = data_type_to_primitive_type(&inst->source.data_type);

    if (dt_is_float(inst->source.data_type)) {
        if (is_register_operand(&inst->source))
            sprintf(code, "sub rsp, 8\n"
                          "movs%c %s [rsp], %s\n", float_type_char(type), word_size_to_string(primitive_type_to_word_size(type)), src);
        else
            sprintf(code, "movs%c %s, %s\n"
                          "sub rsp, 8\n"
                          "movs%c %s [rsp], %s\n", float_type_char(type), temporary_register(REGISTER_3, type), src,
                          float_type_char(type), word_size_to_string(primitive_type_to_word_size(type)), temporary_register(REGISTER_3, type));
    } else if (is_register_operand(&inst->source) || inst->source.type == OPER_INT || inst->source.type == OPER_STRING)
        sprintf(code, "push %s\n", src);
    else
        sprintf(code, "mov %s, %s\n"
                      "push %s\n", temporary_register(REGISTER_3, type), src, temporary_register(REGISTER_3, PRIM_I64));

    free(src);
    alter_stack(state, 8, true, false);
    return code;
}

static char *emit_pop(State *state, LIR_Instruction *inst) {
    if (!dt_is_float(inst->destination.data_type))
        inst->destination.data_type.primitive_type = PRIM_I64;

    char *dst = lir_operand_to_string(state, &inst->destination);
    char *code = malloc(strlen(dst) + 64);
    const Primitive_Type type = data_type_to_primitive_type(&inst->destination.data_type);

    if (dt_is_float(inst->destination.data_type)) {
        if (is_register_operand(&inst->destination))
            sprintf(code, "movs%c %s, %s [rsp]\n"
                          "add rsp, 8\n", float_type_char(type), dst, word_size_to_string(primitive_type_to_word_size(type)));
        else
            sprintf(code, "movs%c %s, %s [rsp]\n"
                          "add rsp, 8\n"
                          "movs%c %s, %s\n", float_type_char(type), temporary_register(REGISTER_3, type),
                          word_size_to_string(primitive_type_to_word_size(type)), 
                          float_type_char(type), dst, temporary_register(REGISTER_3, type));
    } else if (is_register_operand(&inst->destination))
        sprintf(code, "pop %s\n", dst);
    else
        sprintf(code, "pop %s\n"
                      "mov %s, %s\n", temporary_register(REGISTER_3, PRIM_I64), dst, temporary_register(REGISTER_3, type));

    free(dst);
    alter_stack(state, -8, true, false);
    return code;
}

static char *emit_inline_asm(LIR_Instruction *inst) {
    const LIR_String *str = &inst->source.string;
    char *code = malloc(str->length + 2);

    if (strchr(str->string, '\\') == NULL) {
        sprintf(code, "%s\n", str->string);
        return code;
    }

    size_t len = 0;

    // Need to convert escape sequences.
    for (size_t i = 0; i < str->length; i++) {
        if (str->string[i] != '\\' || i + 1 == str->length) {
            code[len++] = str->string[i];
            continue;
        }

        i++;
        char esc;

        switch (str->string[i]) {
            case 'n':
                esc = 10;
                break;
            case 't':
                esc = 9;
                break;
            case 'r':
                esc = 13;
                break;
            case '0':
                esc = 0;
                break;
            case '\\':
            case '"':
            case '\'':
                esc = (int)str->string[i];
                break;
            default:
                assert(false);
                esc = 0;
                break;
        }

        code[len++] = esc;
    }

    code[len] = '\n';
    code[len + 1] = '\0';
    return code;
}

static char *emit_call(State *state, LIR_Instruction *inst) {
    char *code;

    if (inst->source.function.module_uid == -1) {// extern function
        code = malloc(inst->source.function.length + 64);
        sprintf(code, "call %.*s\n", (int)inst->source.function.length, inst->source.function.name);
    } else {
        Module *module = get_module(state->context, inst->source.function.module_uid);
        
        code = malloc(module->name_length + 
            module->name_length + inst->source.function.length + 64);

        sprintf(code, "call _%.*s__%.*s\n", 
            (int)module->name_length, module->name, 
            (int)inst->source.function.length, inst->source.function.name);
    }

    if (inst->destination.argument.total_count <= MAX_REGISTER_ARGUMENT_COUNT)
        return code;

    char *dealloc = alter_stack(state, -(int)((inst->destination.argument.total_count - MAX_REGISTER_ARGUMENT_COUNT) * 8), true, true);
    assert(dealloc != NULL);
    strcat(code, dealloc);
    free(dealloc);
    return code;
}

static char *emit_set(State *state, LIR_Instruction *inst) {
    const bool is_float = dt_is_float(inst->source.data_type);
    char *dst = lir_operand_to_string(state, &inst->destination);
    char *code = malloc(strlen(dst) + 11);

    switch (inst->type) {
        case LIR_SETE:
            sprintf(code, "sete %s\n", dst);
            break;
        case LIR_SETNE:
            sprintf(code, "setne %s\n", dst);
            break;
        case LIR_SETLT:
            sprintf(code, "set%c %s\n", is_float ? 'b' : 'l', dst);
            break;
        case LIR_SETLTE:
            sprintf(code, "set%ce %s\n", is_float ? 'b' : 'l', dst);
            break;
        case LIR_SETGT:
            sprintf(code, "set%c %s\n", is_float ? 'a' : 'g', dst);
            break;
        default:
            sprintf(code, "set%ce %s\n", is_float ? 'a' : 'g', dst);
            break;
    }

    free(dst);
    return code;
}

static char *emit_compare(State *state, LIR_Instruction *inst) {
    const Primitive_Type type = data_type_to_primitive_type(&inst->destination.data_type);
    char *dst = lir_operand_to_string(state, &inst->destination);
    char *src = lir_operand_to_string(state, &inst->source);
    char *code = malloc(strlen(dst) + strlen(src) + 15);

    if (type == PRIM_F32)
        sprintf(code, "comiss %s, %s\n", dst, src);
    else if (type == PRIM_F64)
        sprintf(code, "comisd %s, %s\n", dst, src);
    else
        sprintf(code, "cmp %s, %s\n", dst, src);

    free(dst);
    free(src);
    return code;
}

static char *emit_new_label(LIR_Instruction *inst) {
    char *code = malloc(16);
    sprintf(code, ".l%zu:\n", inst->source.label.number);
    return code;
}

static char *emit_jmp(State *state, LIR_Instruction *inst) {
    char *dst = lir_operand_to_string(state, &inst->destination);
    char *code = malloc(strlen(dst) + 8);
    sprintf(code, "jmp %s\n", dst);
    free(dst);
    return code;
}

static char *emit_jmp_bool(State *state, LIR_Instruction *inst) {
    char *load = NULL;
    LIR_Operand reg; 

    if (!is_register_operand(&inst->source)) {
        reg = (LIR_Operand){ .type = OPER_REGISTER, .data_type = inst->source.data_type, 
            .register_.number = REGISTER_3, .register_.temporary = true };

        LIR_Instruction load_inst = (LIR_Instruction){ .type = LIR_LOAD, .destination = reg, .source = inst->source };
        load = emit_load(state, &load_inst);
        inst->source = reg;
    }

    char *dst = lir_operand_to_string(state, &inst->destination);
    char *src = lir_operand_to_string(state, &inst->source);

    char *code = malloc(strlen(dst) + (strlen(src) * 2) + 20);
    sprintf(code, "test %s, %s\n"
                  "j%s %s\n", src, src, inst->type == LIR_JMP_TRUE ? "nz" : "z", dst);

    free(dst);
    free(src);

    if (load == NULL)
        return code;

    load = realloc(load, strlen(load) + strlen(code) + 1);
    strcat(load, code);
    free(code);
    return load;
}

static char *emit_start_block(State *state) {
    push_stack_cache(state);
    return calloc(1, sizeof(char));
}

static char *emit_end_block(State *state, LIR_Instruction *inst) {
    const size_t cur_reserved = state->stack_reserved;
    const size_t cur_used = state->stack_reserved;
    pop_stack_cache(state);

    if (cur_reserved == state->stack_reserved) {
        if (inst->source.end_block_is_conditional) {
            push_stack_cache(state);
            state->stack_reserved = cur_reserved;
            state->stack_used = cur_used;
        }

        return calloc(1, sizeof(char));
    }

    assert(state->stack_reserved < cur_reserved);
    char *code = malloc(32);
    sprintf(code, "add rsp, %zu\n", cur_reserved - state->stack_reserved);

    if (inst->source.end_block_is_conditional) {
        push_stack_cache(state);
        state->stack_reserved = cur_reserved;
        state->stack_used = cur_used;
    }

    return code;
}

static char *emit_reference(State *state, LIR_Instruction *inst) {
    // Must be a 64 bit register.
    if (inst->destination.type == OPER_REGISTER)
        inst->destination.data_type.primitive_type = PRIM_ISIZE;

    char *dst = lir_operand_to_string(state, &inst->destination);
    char *src = lir_operand_to_string(state, &inst->source);
    char *src_origin = src;

    // Word annotations don't really matter but I prefer bare addresses.
    char *past_word = strchr(src, ' ');

    if (past_word != NULL)
        // src has a word annotation, increment past it.
        src = past_word + 1;

    char *code = malloc(strlen(dst) + strlen(src_origin) + 12);
    sprintf(code, "lea %s, %s\n", dst, src);

    free(dst);
    free(src_origin);
    return code;
}

static char *emit_neg(State *state, LIR_Instruction *inst) {
    char *dst = lir_operand_to_string(state, &inst->destination);
    char *code = malloc(strlen(dst) + 64);
    const Primitive_Type type = data_type_to_primitive_type(&inst->destination.data_type);

    if (bin_is_float(type)) {
        const char *reg = temporary_register(REGISTER_3, type);
        sprintf(code, "pcmpeqd %s, %s\n"
                      "pxor %s, %s\n", reg, reg, dst, reg);
    } else
        sprintf(code, "neg %s\n", dst);

    free(dst);
    return code;
}

static char *emit_not(LIR_Instruction *inst) {
    assert(is_register_operand(&inst->destination));
    char *code = malloc(16);
    sprintf(code, "neg %s\n", hardware_register_to_string(inst->destination.register_.number, PRIM_I8));
    return code;
}

static char *emit_bool_not(State *state, LIR_Instruction *inst) {
    char *dst = lir_operand_to_string(state, &inst->destination);
    char *code = malloc(strlen(dst) * 3 + 21);
    sprintf(code, "test %s, %s\n"
                  "setz %s\n", dst, dst, dst);
    free(dst);
    return code;
}

static char *emit_extern(State *state, LIR_Instruction *inst) {
    Module *module = NULL;

    if (inst->source.function.module_uid != -1)
        module = get_module(state->context, inst->source.function.module_uid);

    char *code;

    if (inst->source.function.module_uid == -1 || module == NULL) {
        code = malloc(inst->source.function.length + 19);
        sprintf(code, "extern %.*s\n", (int)inst->source.function.length, inst->source.function.name);
        return code;
    }
    
    code = malloc(module->name_length + inst->source.function.length + 20);
    sprintf(code, "extern _%.*s__%.*s\n", (int)module->name_length, module->name,
        (int)inst->source.function.length, inst->source.function.name);

    return code;
}

static char *emit_instruction(State *state, LIR_Instruction *inst) {
    switch (inst->type) {
        case LIR_NOP:
        case LIR_EOF: return calloc(1, sizeof(char));
        case LIR_START_FUNC: return emit_start_function(state, inst);
        case LIR_END_FUNC: return emit_end_function(state);
        case LIR_RETURN: return emit_return(state, inst);
        case LIR_LOAD: return emit_load(state, inst);
        case LIR_STORE: return emit_store(state, inst);
        case LIR_ADD:
        case LIR_SUB:
        case LIR_MUL:
        case LIR_DIV:
        case LIR_MOD:
        case LIR_AND:
        case LIR_OR:
        case LIR_XOR:
        case LIR_SHL:
        case LIR_SHR: return emit_math(state, inst);
        case LIR_PUSH: return emit_push(state, inst);
        case LIR_POP: return emit_pop(state, inst);
        case LIR_ASM: return emit_inline_asm(inst);
        case LIR_CALL: return emit_call(state, inst);
        case LIR_SETE:
        case LIR_SETNE:
        case LIR_SETLT:
        case LIR_SETLTE:
        case LIR_SETGT:
        case LIR_SETGTE: return emit_set(state, inst);
        case LIR_COMPARE: return emit_compare(state, inst);
        case LIR_NEW_LABEL: return emit_new_label(inst);
        case LIR_JMP: return emit_jmp(state, inst);
        case LIR_JMP_TRUE:
        case LIR_JMP_FALSE: return emit_jmp_bool(state, inst);
        case LIR_START_BLOCK: return emit_start_block(state);
        case LIR_END_BLOCK: return emit_end_block(state, inst);
        case LIR_REFERENCE: return emit_reference(state, inst);
        case LIR_NEG: return emit_neg(state, inst);
        case LIR_NOT: return emit_not(inst);
        case LIR_BOOL_NOT: return emit_bool_not(state, inst);
        case LIR_EXTERN: return emit_extern(state, inst);
        default: break;
    }

    log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL,
        "No backend emission for LIR type '%s'\n", lir_type_to_string(inst->type));
    return calloc(1, sizeof(char));
}

size_t primitive_type_to_size(const Primitive_Type type) {
    switch (type) {
        case PRIM_VOID:
        case PRIM_BOOL:
        case PRIM_I8:
        case PRIM_U8: return 1;
        case PRIM_I16:
        case PRIM_U16: return 2;
        case PRIM_I32:
        case PRIM_U32:
        case PRIM_F32: return 4;
        default: return 8;
    }
}

size_t primitive_type_to_bit_size(const Primitive_Type type) {
    return primitive_type_to_size(type) * 8;
}

size_t data_type_to_bit_size(const Data_Type *dt) {
    return primitive_type_to_bit_size(data_type_to_primitive_type(dt));
}

bool data_type_bit_sizes_equal(const Data_Type dt1, const Data_Type dt2) {
    return primitive_type_bit_sizes_equal(data_type_to_primitive_type(&dt1), data_type_to_primitive_type(&dt2));
}

bool primitive_type_bit_sizes_equal(const Primitive_Type primitive_type1, const Primitive_Type primitive_type2) {
    return primitive_type_to_size(primitive_type1) == primitive_type_to_size(primitive_type2);
}
