#include "lir.h"
#include "lir_inlines.h"
#include "logger.h"
#include "hir.h"
#include "token.h"
#include "context.h"
#include "decorators.h"
#include "utilities.h"
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

#define nop (LIR_Operand){ .type = OPER_NONE, .data_type = NO_DATA_TYPE }

#define IR_NO_DEREFERENCE 0x01

#define INSTRUCTION_CAPACITY 16

#define T1_REGISTER_NUMBER 0
#define T2_REGISTER_NUMBER 1

static inline LIR_Operand t1(Data_Type type) {
    return lir_register(type, T1_REGISTER_NUMBER, true);
}

static inline LIR_Operand t2(Data_Type type) {
    return lir_register(type, T2_REGISTER_NUMBER, true);
}

// If a data value requires more than 1 instruction to process then it is complex,
// it may clobber registers and thus ruin things like call arguments and indexes.
static bool is_complex_expression(const HIR_Data *data) {
    switch (data->type) {
        case DATA_CALL:
        case DATA_MATH:
        case DATA_CONDITION:
        case DATA_REFERENCE:
        case DATA_INDEX:
        case DATA_DEREFERENCE:
        case DATA_STRUCT_MEMBER: return true;
        case DATA_CAST: return is_complex_expression(data->cast.value);
        default: return false;
    }
}

static void push_instruction(LIR *lir, LIR_Opcode type, LIR_Operand destination, LIR_Operand source) {
    if (lir->count + 1 > lir->capacity) {
        lir->capacity *= 2;
        lir->instructions = realloc(lir->instructions, lir->capacity * sizeof(LIR_Instruction));
    }

    lir->instructions[lir->count++] = (LIR_Instruction){ 
        .type = type, .destination = destination, .source = source };
}

// This specific instruction needs special treatment because of certain values (arrays and structs)
// not actually being variables, in the sense that they are treated as pointers.
static void push_load(LIR *lir, LIR_Operand destination, LIR_Operand source) {
    if (source.data_type.array_size > 0 || source.data_type.primitive_type == PRIM_CUSTOM)
        push_instruction(lir, source.data_type.pointer_count > 0 || source.type == OPER_POINTER ? LIR_LOAD : LIR_REFERENCE, destination, source);
    else
        push_instruction(lir, LIR_LOAD, destination, source);
}

static void push_hir(LIR *lir, HIR *hir);
static LIR_Operand hir_data_to_operand(LIR *lir, HIR_Data *data);

static LIR_Operand hir_literal_to_lir_operand(HIR_Data *data) {
    LIR_Operand oper = (LIR_Operand){ .data_type = data->literal.data_type };

    if (data->literal.data_type.primitive_type == PRIM_CUSTOM) {
        oper.type = OPER_STRING;
        oper.string.string = data->literal.string.string;
        oper.string.length = data->literal.string.length;
        return oper;
    }

    const Primitive_Type bin = data_type_to_primitive_type(&data->literal.data_type);

    if (bin == PRIM_F32) {
        oper.type = OPER_FLOAT;
        oper.float_.f32 = data->literal.f32;
        return oper;
    } else if (bin == PRIM_F64) {
        oper.type = OPER_FLOAT;
        oper.float_.f64 = data->literal.f64;
        return oper;
    }

    oper.type = OPER_INT;

    if (oper.data_type.primitive_type == PRIM_I32)
        oper.int_.i32 = data->literal.i32;
    else if (oper.data_type.primitive_type == PRIM_I64)
        oper.int_.i64 = data->literal.i64;
    else if (oper.data_type.primitive_type == PRIM_U32)
        oper.int_.u32 = data->literal.u32;
    else
        oper.int_.u64 = data->literal.u64;

    return oper;
}

static inline LIR_Operand hir_local_variable_to_lir_operand(HIR_Data *data) {
    return lir_local_variable(data->local_variable.data_type, data->local_variable.uid, data->local_variable.variable_uid, false);
}

#define HIGHEST_PREC 2
#define MIDDLE_PREC 1
#define LOWEST_PREC 0

static unsigned int math_operator_to_precedence(const Token_Type operator) {
    switch (operator) {
        case TOK_SHL:
        case TOK_SHR:
        case TOK_AND:
        case TOK_OR:
        case TOK_XOR: return LOWEST_PREC;
        case TOK_PLUS:
        case TOK_MINUS: return MIDDLE_PREC;
        default: return HIGHEST_PREC;
    }
}

static LIR_Opcode math_operator_to_opcode(const Token_Type operator) {
    switch (operator) {
        case TOK_PLUS: return LIR_ADD;
        case TOK_MINUS: return LIR_SUB;
        case TOK_STAR: return LIR_MUL;
        case TOK_SLASH: return LIR_DIV;
        case TOK_PERCENT: return LIR_MOD;
        case TOK_AND: return LIR_AND;
        case TOK_OR: return LIR_OR;
        case TOK_XOR: return LIR_XOR;
        case TOK_SHL: return LIR_SHL;
        default: return LIR_SHR;
    }
}

static void do_math_operation(LIR *lir, const Token_Type oper, const Data_Type *type) {
    push_instruction(lir, LIR_POP, t2(*type), nop);
    push_instruction(lir, LIR_POP, t1(*type), nop);
    push_instruction(lir, math_operator_to_opcode(oper), t1(*type), t2(*type));
    push_instruction(lir, LIR_PUSH, nop, t1(*type));
}

static LIR_Operand struct_member_to_operand(LIR *lir, HIR_Data *data, const bool dereference);

static LIR_Operand hir_math_to_lir_operand(LIR *lir, HIR_Data *hir) {
    HIR_Data *data = hir->expression.data;
    const size_t count = hir->expression.count;
    Data_Type type = infer_hir_data_list_type(lir->context, hir->expression.data, hir->expression.count);

    Token_Type delayed_opers[count];
    size_t delayed_count = 0;

    for (size_t i = 0; i < count; i++) {
        HIR_Data *item = &data[i];

        if (i % 2 != 0) {
            const unsigned int this_prec = math_operator_to_precedence(item->operator.math);

            while (delayed_count > 0 && math_operator_to_precedence(delayed_opers[delayed_count - 1]) >= this_prec)
                do_math_operation(lir, delayed_opers[--delayed_count], &type);

            delayed_opers[delayed_count++] = item->operator.math;
            continue;
        }

        push_load(lir, t1(type), hir_data_to_operand(lir, &data[i]));
        push_instruction(lir, LIR_PUSH, nop, t1(type));
    }

    while (delayed_count > 0)
        do_math_operation(lir, delayed_opers[--delayed_count], &type);

    push_instruction(lir, LIR_POP, t1(type), nop);
    return t1(type);
}

static void handle_complex_call_arguments(LIR *lir, HIR_Call *call, int *max_ints, int *max_floats,
        size_t *complex_arg_indexes, size_t *complex_arg_count) {
    int ints = 0;
    int floats = 0;
    LIR_Operand *complex_args = malloc(call->argument_count * sizeof(LIR_Operand));
    *complex_arg_count = 0;

    for (size_t i = 0; i < call->argument_count; i++) {
        Data_Type dt = ((AST *)call->parameters->items[i])->parameter.data_type;

        if (dt_is_float(dt))
            floats++;
        else
            ints++;
    }

    *max_ints = ints;
    *max_floats = floats;

    for (int i = (int)call->argument_count - 1; i >= 0; i--) {
        Data_Type dt = ((AST *)call->parameters->items[i])->parameter.data_type;

        if (is_complex_expression(&call->arguments[i])) {
            LIR_Operand reg = t1(dt);
            push_load(lir, reg, hir_data_to_operand(lir, &call->arguments[i]));
            push_instruction(lir, LIR_PUSH, nop, reg);

            complex_args[*complex_arg_count] = 
                lir_argument(dt, i, call->argument_count, ints - 1, floats - 1, true);

            complex_arg_indexes[*complex_arg_count] = i;
            *complex_arg_count += 1;
        }

        if (dt_is_float(dt))
            floats--;
        else
            ints--;
    }

    for (int i = (int)(*complex_arg_count) - 1; i >= 0; i--) {
        Data_Type dt = ((AST *)call->parameters->items[i])->parameter.data_type;
        LIR_Operand reg = t1(dt);
        push_instruction(lir, LIR_POP, reg, nop);
        push_instruction(lir, LIR_STORE, complex_args[i], reg);
    }

    free(complex_args);
}

static void push_call(LIR *lir, HIR_Call *call) {
    if (call->argument_count == 0) {
        // Skip all the argument BS.
        push_instruction(lir, LIR_CALL, lir_argument(NO_DATA_TYPE, 0, call->argument_count, 0, 0, true), 
            lir_function(call->name, call->name_length, call->module_uid, call->flags));
        return;
    }

    // We want to process the complex call arguments first so we don't get surprised
    // when loading them and have to cache then uncache every last register that is already loaded.
    int max_ints;
    int max_floats;
    size_t complex_arg_indexes[call->argument_count];
    size_t complex_arg_count = 0;
    handle_complex_call_arguments(lir, call, &max_ints, &max_floats, complex_arg_indexes, &complex_arg_count);

    // Finally we can load the rest of the arguments without worrying that they will get corrupted.
    int ints = max_ints;
    int floats = max_floats;

    for (int i = (int)call->argument_count - 1; i >= 0; i--) {
        bool already_done = false;

        for (size_t j = 0; j < complex_arg_count; j++) {
            if (complex_arg_indexes[j] == (size_t)i) {
                already_done = true;
                break;
            }
        }

        Data_Type dt = ((AST *)call->parameters->items[i])->parameter.data_type;

        if (!already_done) {
            LIR_Operand reg = t1(dt);
            push_load(lir, reg, hir_data_to_operand(lir, &call->arguments[i]));
            push_instruction(lir, LIR_STORE, lir_argument(dt, i, call->argument_count, ints - 1, floats - 1, true), reg);
        }

        if (dt_is_float(dt))
            floats--;
        else
            ints--;
    }

    LIR_Operand id = lir_function(call->name, call->name_length, call->module_uid, call->flags);
    id.data_type = call->data_type;
    push_instruction(lir, LIR_CALL, lir_argument(NO_DATA_TYPE, 0, call->argument_count, 0, 0, true), id);
}

static LIR_Opcode condition_operator_to_opcode(const Token_Type operator) {
    switch (operator) {
        case TOK_EQ: return LIR_SETE;
        case TOK_NEQ: return LIR_SETNE;
        case TOK_LT: return LIR_SETLT;
        case TOK_LTE: return LIR_SETLTE;
        case TOK_GT: return LIR_SETGT;
        case TOK_GTE: return LIR_SETGTE;
        default:
            assert(false);
            return 0;
    }
}

static LIR_Operand hir_condition_to_lir_operand(LIR *lir, HIR_Data *hir, const bool as_value) {
    HIR_Data *data = hir->expression.data;
    const size_t count = hir->expression.count;
    Data_Type bool_dt = create_data_type(PRIM_BOOL, 0);
    LIR_Operand true_label;

    // Final condition evaluation, only needed if the condition is more than a single comparison.
    if (count > 3) {
        true_label = lir_label(lir->label_count++);
        push_load(lir, t1(bool_dt), (LIR_Operand){ .type = OPER_INT, .data_type = bool_dt, .int_.u8 = 0 });
        push_instruction(lir, LIR_PUSH, nop, t1(bool_dt));
    }

    LIR_Operand false_label = nop;
    
    if (as_value)
        false_label = lir_label(lir->label_count++);

    for (size_t i = 1; i < count; i += 4) {
        HIR_Data *lhs = &data[i - 1];
        HIR_Data *rhs = &data[i + 1];
        const Token_Type oper = data[i].operator.math;

        // The next and last && or || if present.
        const Token_Type next_chain = i + 2 == count ? 0 : data[i + 2].operator.math;
        const Token_Type last_chain = (int)i - 2 < 0 ? 0 : data[i - 2].operator.math;

        Data_Type type = infer_between_two_types(
            get_hir_data_type(lir->context, lhs), get_hir_data_type(lir->context, rhs));

        push_load(lir, t1(type), hir_data_to_operand(lir, lhs));
        push_instruction(lir, LIR_PUSH, nop, t1(type));

        push_load(lir, t1(type), hir_data_to_operand(lir, rhs));
        push_instruction(lir, LIR_PUSH, nop, t1(type));

        push_instruction(lir, LIR_POP, t2(type), nop);
        push_instruction(lir, LIR_POP, t1(type), nop);

        push_instruction(lir, LIR_COMPARE, t1(type), t2(type));
        push_instruction(lir, condition_operator_to_opcode(oper), t1(bool_dt), nop);

        if (count == 3)
            break;

        push_instruction(lir, LIR_POP, t2(bool_dt), nop);
        push_instruction(lir, last_chain == TOK_BOOL_AND ? LIR_AND : LIR_OR, t1(bool_dt), t2(bool_dt));

        if (next_chain == TOK_BOOL_OR)
            push_instruction(lir, LIR_JMP_TRUE, true_label, t1(bool_dt));
        else {
            assert(false_label.type == OPER_LABEL);
            push_instruction(lir, LIR_JMP_FALSE, false_label, t1(bool_dt));
        }

        push_instruction(lir, LIR_PUSH, nop, t1(bool_dt));
    }

    // If it's not a value then we assume we explicitly handle the else/done labels somewhere else e.g if statement, loop.
    if (count == 3 || !as_value)
        return t1(bool_dt);

    LIR_Operand done_label = lir_label(lir->label_count++);

    push_instruction(lir, LIR_POP, t1(bool_dt), nop);
    push_instruction(lir, LIR_NEW_LABEL, nop, true_label);
    push_instruction(lir, LIR_JMP, done_label, nop);
    push_instruction(lir, LIR_NEW_LABEL, nop, false_label);
    push_load(lir, t1(bool_dt), (LIR_Operand){ .type = OPER_INT, .data_type = create_data_type(PRIM_I32, 0), .int_.i32 = 0 });
    push_instruction(lir, LIR_NEW_LABEL, nop, done_label);
    return t1(bool_dt);
}

static LIR_Operand hir_reference_to_lir_operand(LIR *lir, HIR_Data *data) {
    Data_Type dt = get_hir_data_type(lir->context, data);
    push_instruction(lir, LIR_REFERENCE, t1(dt), hir_data_to_operand(lir, data->reference.value));
    return t1(dt);
}

static LIR_Operand hir_index_to_lir_operand(LIR *lir, HIR_Data *data) {
    // Convert the array type into a pointer.
    Data_Type base_type = get_hir_data_type(lir->context, data->index.base);
    assert(base_type.array_size > 0 || base_type.pointer_count > 0);
    Data_Type item_type = base_type;

    if (base_type.array_size == 0)
        item_type.pointer_count--;

    Data_Type index_type = create_data_type(PRIM_USIZE, 0);
    size_t item_size;

    if (item_type.primitive_type == PRIM_CUSTOM)
        item_size = struct_data_type_to_size(lir->context, &item_type, 
            get_module(lir->context, item_type.module_name != NULL ? (size_t)item_type.module_uid : data->module_uid));
    else
        item_size = primitive_type_to_size(data_type_to_primitive_type(&item_type));

    //push_instruction(lir, LIR_REFERENCE, t1(index_type), hir_data_to_operand(lir, data->index.base));

    //if (data->index.base->type == DATA_STRUCT_MEMBER)
      //  push_load(lir, t1(index_type), struct_member_to_operand(lir, data->index.base, false));
    //els
    const size_t flags = lir->flags;
    lir->flags |= IR_NO_DEREFERENCE;

    push_load(lir, t1(index_type), hir_data_to_operand(lir, data->index.base));

    lir->flags = flags;

    if (data->index.base->type == DATA_STRUCT_MEMBER)
        push_load(lir, t1(index_type), lir_pointer(index_type, T1_REGISTER_NUMBER));

    push_instruction(lir, LIR_PUSH, nop, t1(index_type));

    push_load(lir, t2(index_type), hir_data_to_operand(lir, data->index.index));

    if (data->index.index->type == DATA_STRUCT_MEMBER)
        push_load(lir, t2(index_type), lir_pointer(index_type, T2_REGISTER_NUMBER));

    push_instruction(lir, LIR_MUL, t2(index_type), 
        //(LIR_Operand){ .type = OPER_SIZEOF, .data_type = index_type, .sizeof_.data_type = item_type });
        (LIR_Operand){ .type = OPER_INT, .data_type = index_type, .int_.u64 = item_size });

    push_instruction(lir, LIR_POP, t1(index_type), nop);
    push_instruction(lir, LIR_ADD, t1(index_type), t2(index_type));

    if (lir->flags & IR_NO_DEREFERENCE) {
        item_type.pointer_count++;
        return t1(item_type);
    }

    return lir_pointer(item_type, T1_REGISTER_NUMBER);
}

static LIR_Operand hir_sizeof_to_lir_operand(HIR_Data *data) {
    return (LIR_Operand){ .type = OPER_SIZEOF, .data_type = create_data_type(PRIM_USIZE, 0), .sizeof_.data_type = data->operator.sizeof_ };
}

static LIR_Operand hir_unary_to_operand(LIR *lir, HIR_Data *data) {
    Data_Type type = get_hir_data_type(lir->context, data->unary.value);
    push_load(lir, t1(type), hir_data_to_operand(lir, data->unary.value));

    switch (data->unary.type) {
        case TOK_MINUS:
            push_instruction(lir, LIR_NEG, t1(type), nop);
            return t1(type);
        case TOK_TILDE:
            push_instruction(lir, LIR_NOT, t1(type), nop);
            return t1(type);
        case TOK_BOOL_NOT:
            push_instruction(lir, LIR_BOOL_NOT, t1(type), nop);
            return t1(type);
        default:
            assert(false);
            return nop;
    }
}

static LIR_Operand hir_cast_to_operand(LIR *lir, HIR_Data *data) {
    push_load(lir, t1(data->cast.data_type), hir_data_to_operand(lir, data->cast.value));
    return t1(data->cast.data_type);
}

static LIR_Operand hir_dereference_to_operand(LIR *lir, HIR_Data *data, const bool as_value) {
    const Data_Type ptr_type = get_hir_data_type(lir->context, data->dereference.value);

    const size_t flags = lir->flags;
    lir->flags |= IR_NO_DEREFERENCE;
    push_load(lir, t1(ptr_type), hir_data_to_operand(lir, data->dereference.value));
    lir->flags = flags;

    if (!as_value)
        return t1(ptr_type);

    Data_Type deref_type = get_hir_data_type(lir->context, data);

    if (data->dereference.value->type == DATA_STRUCT_MEMBER)
        push_load(lir, t1(ptr_type), lir_pointer(ptr_type, T1_REGISTER_NUMBER));

    if (lir->flags & IR_NO_DEREFERENCE) 
        return t1(ptr_type);

    //push_instruction(lir, LIR_MUL, t1(deref_type), lir_pointer(deref_type.pointer_count > 0 ? ptr_type : deref_type, T1_REGISTER_NUMBER));
    push_instruction(lir, LIR_LOAD, t1(deref_type), lir_pointer(deref_type, T1_REGISTER_NUMBER));
    return t1(deref_type);
}

static LIR_Operand struct_member_to_operand(LIR *lir, HIR_Data *data, const bool dereference) {
    const Custom_Type *type = get_custom_type(lir->context, data->struct_member.custom_type_symbol_uid);
    const Custom_Type_Member *member_symbol = get_custom_type_member(lir->context, type->uid, data->struct_member.member_symbol_uid);

    Data_Type struct_type = get_hir_data_type(lir->context, data->struct_member.lhs);

    if (struct_type.pointer_count == 0)
        struct_type.pointer_count++;

    push_load(lir, t1(struct_type), hir_data_to_operand(lir, data->dereference.value));

    for (size_t i = 0; i < type->member_count; i++) {
        Custom_Type_Member *member = &type->members[i];

        if (compare_string(member->name, member->name_length, 
                member_symbol->name, member_symbol->name_length))
            break;

        LIR_Operand size = (LIR_Operand){ .type = OPER_SIZEOF, .data_type = struct_type, .sizeof_.data_type = member->data_type };
        push_load(lir, t2(struct_type), size);
        push_instruction(lir, LIR_ADD, t1(struct_type), t2(struct_type));
    }

    Data_Type member_type = member_symbol->data_type;

    //if (!dereference || member_type.pointer_count > 0) {
        //member_type.pointer_count++;
    if (dereference && !(lir->flags & IR_NO_DEREFERENCE))
        push_load(lir, t1(member_type), lir_pointer(member_type, T1_REGISTER_NUMBER));

    return t1(member_type);
}

static LIR_Operand hir_data_to_operand(LIR *lir, HIR_Data *data) {
    switch (data->type) {
        case DATA_NONE: return nop;
        case DATA_LITERAL: return hir_literal_to_lir_operand(data);
        case DATA_LOCAL_VARIABLE: return hir_local_variable_to_lir_operand(data);
        case DATA_OPERATOR:
            assert(data->operator.type == OP_SIZEOF);
            return hir_sizeof_to_lir_operand(data);
        case DATA_MATH: return hir_math_to_lir_operand(lir, data);
        case DATA_CALL:
            push_call(lir, &data->call);
            return lir_register(data->call.data_type, T1_REGISTER_NUMBER, false);
        case DATA_CONDITION: return hir_condition_to_lir_operand(lir, data, true);
        case DATA_REFERENCE: return hir_reference_to_lir_operand(lir, data);
        case DATA_INDEX: return hir_index_to_lir_operand(lir, data);
        case DATA_UNARY: return hir_unary_to_operand(lir, data);
        case DATA_CAST: return hir_cast_to_operand(lir, data);
        case DATA_DEREFERENCE: return hir_dereference_to_operand(lir, data, true);
        case DATA_STRUCT_MEMBER: return struct_member_to_operand(lir, data, true);
        default: break;
    }

    log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL,
        "No LIR for HIR data type '%s'\n", hir_data_type_to_string(data->type));
    return nop;
}

static void push_block(LIR *lir, HIR_Block *block) {
    push_instruction(lir, LIR_START_BLOCK, nop, nop);

    for (size_t i = 0; i < block->count; i++)
        push_hir(lir, &block->nodes[i]);

    push_instruction(lir, LIR_END_BLOCK, nop, lir_end_block_data(false));
}

static void push_function(LIR *lir, HIR *hir) {
    lir->label_count = 0;

    LIR_Operand id = lir_function(hir->function.name, hir->function.name_length, hir->function.module_uid, hir->function.flags);
    id.data_type = hir->function.data_type;
    id.function.exported = hir->function.exported;
    push_instruction(lir, LIR_START_FUNC, nop, id);

    if (!(hir->function.flags & DECOR_ONETIME_ARGUMENTS)) {
        const HIR_Data *params = hir->function.parameters;
        size_t ints = 0;
        size_t floats = 0;

        for (size_t i = 0; i < hir->function.parameter_count; i++) {
            Data_Type dt = params[i].local_variable.data_type;
            push_instruction(lir, LIR_STORE, lir_local_variable(dt, params[i].local_variable.uid, params[i].local_variable.variable_uid, true),
                lir_argument(dt, i, hir->function.parameter_count, ints, floats, false));

            if (dt_is_float(dt))
                floats++;
            else
                ints++;
        }
    }

    for (size_t i = 0; i < hir->function.block.count; i++)
        push_hir(lir, &hir->function.block.nodes[i]);

    push_instruction(lir, LIR_END_FUNC, nop, id);
}

static void push_return(LIR *lir, HIR *hir) {
    const Symbol *symbol = get_symbol(lir->context, hir->return_.symbol_uid);
    assert(symbol != NULL);
    LIR_Operand dst = lir_function(symbol->name, symbol->name_length, symbol->module_uid, symbol->flags);
    dst.data_type = hir->return_.data_type;
    push_instruction(lir, LIR_RETURN, dst, hir_data_to_operand(lir, &hir->return_.value));
}

static void push_assignment(LIR *lir, HIR *hir);

static void store_struct_initializer(LIR *lir, HIR_Data *lhs, HIR_Data *rhs) {
    const Custom_Type *type = get_custom_type(lir->context, rhs->struct_initializer.custom_type_symbol_uid);

    for (size_t i = 0; i < rhs->struct_initializer.value_count; i++) {
        const Custom_Type_Member *member = find_custom_type_member(lir->context, type->uid,
            rhs->struct_initializer.annotations[i], rhs->struct_initializer.annotation_lengths[i]);// get_custom_type_member(lir->context, type->uid, i);
        assert(member != NULL);

        HIR_Data hir_member = (HIR_Data){ .type = DATA_STRUCT_MEMBER, .struct_member = {
            .custom_type_symbol_uid = type->uid, .member_symbol_uid = member->uid, .lhs = lhs 
        } };

        HIR hir = (HIR){ .type = HIR_ASSIGNMENT, .assignment.lhs = hir_member, .assignment.rhs =
            rhs->struct_initializer.values[i] };

        push_assignment(lir, &hir);
    }
}

static void push_declaration(LIR *lir, HIR *hir) {
    LIR_Operand var = lir_local_variable(hir->declaration.data_type, hir->declaration.uid, hir->declaration.variable_uid, false);

    if (hir->declaration.value.type == DATA_NONE) {
        push_instruction(lir, LIR_STORE, var, nop);
        return;
    } else if (hir->declaration.value.type == DATA_STRUCT_INITIALIZER) {
        push_instruction(lir, LIR_STORE, var, nop);

        HIR_Data hir_var = (HIR_Data){ .type = DATA_LOCAL_VARIABLE, .local_variable = {
            .data_type = hir->declaration.data_type, .name = hir->declaration.name, .name_length = hir->declaration.name_length,
            .uid = hir->declaration.uid, .variable_uid = hir->declaration.variable_uid
        } };
        store_struct_initializer(lir, &hir_var, &hir->declaration.value);
        return;
    }

    LIR_Operand src = hir_data_to_operand(lir, &hir->declaration.value);
    push_load(lir, t1(hir->declaration.data_type), src);
    push_instruction(lir, LIR_STORE, var, t1(hir->declaration.data_type));
}

static void push_pointer_assignment(LIR *lir, HIR *hir) {
    assert(hir->assignment.lhs.type == DATA_INDEX || hir->assignment.lhs.type == DATA_DEREFERENCE);

    if (hir->assignment.rhs.type == DATA_STRUCT_INITIALIZER) {
        store_struct_initializer(lir, &hir->assignment.lhs, &hir->assignment.rhs);
        return;
    }

    //const bool is_deref = hir->assignment.lhs.type == DATA_DEREFERENCE;

    // Dereferences and indexes require calculating the correct pointer location first, 
    // which will corrupt the value already loaded.
    Data_Type lhs_type = get_hir_data_type(lir->context, &hir->assignment.lhs);
    //const Data_Type deref_type = lhs_type; // Dereferences will modify the above data type.

    //if (lhs_type.array_size > 0)
      //  lhs_type.array_size = 0;

    // Dereference needs the pointer to store into, not the value in the pointer.
    //LIR_Operand lhs = //is_deref ? hir_dereference_to_operand(lir, &hir->assignment.lhs, false) : 
     //   hir_data_to_operand(lir, &hir->assignment.lhs);

    //if (is_deref)
      //  dt.pointer_count++;

    //if (is_deref)
        //push_load(lir, t1(lhs_type), lhs);
    //else
      //  push_instruction(lir, LIR_REFERENCE, t1(dt), lhs);

    const size_t flags = lir->flags;
    lir->flags |= IR_NO_DEREFERENCE;
    LIR_Operand lhs = hir_data_to_operand(lir, &hir->assignment.lhs);
    lir->flags = flags;
    push_load(lir, t1(lhs_type), lhs);

    //push_load(lir, t1(lhs_type), lhs);

    // We may be referencing a pointer, thus creating &&type.
    // Like for example loading a pointer struct member from a stack struct.
    // We now need to dereference to get the actual pointer.
    // TODO

    if (!is_complex_expression(&hir->assignment.rhs)) {
        push_load(lir, t2(lhs_type), hir_data_to_operand(lir, &hir->assignment.rhs));
        //push_instruction(lir, LIR_STORE, lir_pointer(is_deref ? deref_type : dt, T1_REGISTER_NUMBER), t2(dt));
        push_instruction(lir, LIR_STORE, lir_pointer(lhs_type, T1_REGISTER_NUMBER), t2(lhs_type));
        return;
    }

    push_instruction(lir, LIR_PUSH, nop, t1(lhs_type));
    push_load(lir, t1(lhs_type), hir_data_to_operand(lir, &hir->assignment.rhs));
    push_instruction(lir, LIR_POP, t2(lhs_type), nop);
    //push_instruction(lir, LIR_STORE, lir_pointer(is_deref ? deref_type : dt, T2_REGISTER_NUMBER), t1(dt));
    push_instruction(lir, LIR_STORE, lir_pointer(lhs_type, T2_REGISTER_NUMBER), t1(lhs_type));
}

static void push_struct_member_assignment(LIR *lir, HIR *hir) {
    LIR_Operand struct_ptr = struct_member_to_operand(lir, &hir->assignment.lhs, false);
    push_load(lir, t1(struct_ptr.data_type), struct_ptr);

    const HIR_Data *lhs = &hir->assignment.lhs;
    Data_Type dt = get_custom_type_member(lir->context, lhs->struct_member.custom_type_symbol_uid, lhs->struct_member.member_symbol_uid)->data_type;
    //hir->assignment.lhs.struct_member.member_symbol->data_type;

    if (!is_complex_expression(&hir->assignment.rhs)) {
        push_load(lir, t2(dt), hir_data_to_operand(lir, &hir->assignment.rhs));
        push_instruction(lir, LIR_STORE, lir_pointer(dt, T1_REGISTER_NUMBER), t2(dt));
        return;
    }

    push_instruction(lir, LIR_PUSH, nop, t1(struct_ptr.data_type));
    push_load(lir, t1(dt), hir_data_to_operand(lir, &hir->assignment.rhs));
    push_instruction(lir, LIR_POP, t2(struct_ptr.data_type), nop);
    push_instruction(lir, LIR_STORE, lir_pointer(dt, T2_REGISTER_NUMBER), t1(dt));
}

static void push_assignment(LIR *lir, HIR *hir) {
    if (hir->assignment.lhs.type == DATA_INDEX || hir->assignment.lhs.type == DATA_DEREFERENCE) {
        push_pointer_assignment(lir, hir);
        return;
    } else if (hir->assignment.lhs.type == DATA_STRUCT_MEMBER) {
        push_struct_member_assignment(lir, hir);
        return;
    } else if (hir->assignment.rhs.type == DATA_STRUCT_INITIALIZER) {
        store_struct_initializer(lir, &hir->assignment.lhs, &hir->assignment.rhs);
        return;
    }

    Data_Type dt = get_hir_data_type(lir->context, &hir->assignment.lhs);
    push_load(lir, t1(dt), hir_data_to_operand(lir, &hir->assignment.rhs));
    push_instruction(lir, LIR_STORE, hir_data_to_operand(lir, &hir->assignment.lhs), t1(dt));
}

static void push_if(LIR *lir, HIR *hir) {
    Data_Type bool_dt = create_data_type(PRIM_BOOL, 0);
    const bool has_else = hir->if_.else_block.count > 0;

    LIR_Operand else_label;
    LIR_Operand done_label;

    if (!has_else)
        done_label = lir_label(lir->label_count++);
    else {
        else_label = lir_label(lir->label_count++);
        done_label = lir_label(lir->label_count++);
    }

    push_load(lir, t1(bool_dt), hir_data_to_operand(lir, &hir->if_.condition));
    push_instruction(lir, LIR_JMP_FALSE, has_else ? else_label : done_label, t1(bool_dt));
    push_block(lir, &hir->if_.block);

    if (has_else) {
        push_instruction(lir, LIR_JMP, done_label, nop);
        push_instruction(lir, LIR_NEW_LABEL, nop, else_label);
        push_block(lir, &hir->if_.else_block);
    }

    push_instruction(lir, LIR_NEW_LABEL, nop, done_label);
}

static void push_while_loop(LIR *lir, HIR *hir) {
    Data_Type bool_dt = create_data_type(PRIM_BOOL, 0);
    LIR_Operand condition_label = lir_label(lir->label_count++);
    LIR_Operand body_label = lir_label(lir->label_count++);
    LIR_Operand done_label = lir_label(lir->label_count++);

    const size_t prev_break_label = lir->current_break_label;
    const size_t prev_continue_label = lir->current_continue_label;

    lir->current_continue_label = condition_label.label.number;
    lir->current_break_label = done_label.label.number;

    if (hir->while_loop.do_block_first) {
        push_instruction(lir, LIR_NEW_LABEL, nop, body_label);
        push_block(lir, &hir->while_loop.block);
    }

    push_instruction(lir, LIR_NEW_LABEL, nop, condition_label);
    push_load(lir, t1(bool_dt), hir_data_to_operand(lir, &hir->while_loop.condition));

    if (!hir->while_loop.do_block_first) {
        push_instruction(lir, LIR_JMP_FALSE, done_label, t1(bool_dt));
        push_instruction(lir, LIR_NEW_LABEL, nop, body_label);
        push_block(lir, &hir->while_loop.block);
        push_instruction(lir, LIR_JMP, condition_label, t1(bool_dt));
    } else
        push_instruction(lir, LIR_JMP_TRUE, body_label, t1(bool_dt));

    push_instruction(lir, LIR_NEW_LABEL, nop, done_label);

    lir->current_break_label = prev_break_label;
    lir->current_continue_label = prev_continue_label;
}

static void push_for_loop(LIR *lir, HIR *hir) {
    Data_Type bool_dt = create_data_type(PRIM_BOOL, 0);
    LIR_Operand condition_label = lir_label(lir->label_count++);
    LIR_Operand body_label = lir_label(lir->label_count++);
    LIR_Operand iter_inc_label; // Only used in for loops.
    LIR_Operand done_label = lir_label(lir->label_count++);

    const size_t prev_break_label = lir->current_break_label;
    const size_t prev_continue_label = lir->current_continue_label;

    // If the iterator fields are not a NOP, we know this is a for loop, we need to
    // generate the iterator statement and also increment it after each loop.
    LIR_Operand iter;
    Data_Type iter_dt;

    // As we want the iterator variable to go out of scope after the for loop,
    // we need to use a start and end block.
    push_instruction(lir, LIR_START_BLOCK, nop, nop);

    iter_inc_label = body_label; // We'll just reuse the body label as it is unusued in for loops.
    lir->current_continue_label = iter_inc_label.label.number;

    iter = hir_data_to_operand(lir, &hir->for_loop.iterator);
    iter_dt = iter.data_type;

    push_load(lir, t1(iter_dt), hir_data_to_operand(lir, &hir->for_loop.iterator_initializer));
    push_instruction(lir, LIR_STORE, iter, t1(iter_dt));

    lir->current_break_label = done_label.label.number;

    push_instruction(lir, LIR_NEW_LABEL, nop, condition_label);
    push_load(lir, t1(bool_dt), hir_data_to_operand(lir, &hir->for_loop.condition));

    push_instruction(lir, LIR_JMP_FALSE, done_label, t1(bool_dt));
    push_block(lir, &hir->for_loop.block);

    push_instruction(lir, LIR_NEW_LABEL, nop, iter_inc_label);
    push_load(lir, t1(iter_dt), iter);
    push_instruction(lir, LIR_PUSH, nop, t1(iter_dt));

    push_load(lir, t1(iter_dt), hir_data_to_operand(lir, &hir->for_loop.iterator_increment));
    push_instruction(lir, LIR_PUSH, nop, t1(iter_dt));

    push_instruction(lir, LIR_POP, t2(iter_dt), nop);
    push_instruction(lir, LIR_POP, t1(iter_dt), nop);
    push_instruction(lir, hir->for_loop.decrement_iterator ? LIR_SUB : LIR_ADD, t1(iter_dt), t2(iter_dt));
    push_instruction(lir, LIR_STORE, iter, t1(iter_dt));
    push_instruction(lir, LIR_JMP, condition_label, nop);

    push_instruction(lir, LIR_NEW_LABEL, nop, done_label);
    push_instruction(lir, LIR_END_BLOCK, nop, nop);

    lir->current_break_label = prev_break_label;
    lir->current_continue_label = prev_continue_label;
}

static void push_keyword_statement(LIR *lir, HIR *hir) {
    switch (hir->keyword_statement) {
        case KW_STMT_BREAK:
            push_instruction(lir, LIR_END_BLOCK, nop, lir_end_block_data(true));
            push_instruction(lir, LIR_JMP, lir_label(lir->current_break_label), nop);
            break;
        case KW_STMT_CONTINUE:
            push_instruction(lir, LIR_END_BLOCK, nop, lir_end_block_data(true));
            push_instruction(lir, LIR_JMP, lir_label(lir->current_continue_label), nop);
            break;
        default:
            assert(false);
            break;
    }
}

static void push_hir(LIR *lir, HIR *hir) {
    switch (hir->type) {
        case HIR_NOP: break;
        case HIR_BLOCK:
            push_block(lir, &hir->block);
            break;
        case HIR_FUNCTION:
            push_function(lir, hir);
            break;
        case HIR_RETURN:
            push_return(lir, hir);
            break;
        case HIR_DECLARATION:
            push_declaration(lir, hir);
            break;
        case HIR_ASSIGNMENT:
            push_assignment(lir, hir);
            break;
        case HIR_ASM:
            push_instruction(lir, LIR_ASM, nop, lir_string(hir->asm_.code, hir->asm_.code_length));
            break;
        case HIR_CALL:
            push_call(lir, &hir->call);
            break;
        case HIR_IF:
            push_if(lir, hir);
            break;
        case HIR_WHILE_LOOP:
            push_while_loop(lir, hir);
            break;
        case HIR_FOR_LOOP:
            push_for_loop(lir, hir);
            break;
        case HIR_KEYWORD_STMT:
            push_keyword_statement(lir, hir);
            break;
        case HIR_EXTERN:
            push_instruction(lir, LIR_EXTERN, nop, lir_function(hir->extern_.name, hir->extern_.name_length, hir->extern_.module_uid, 0));
            break;
        default:
            log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL, 
                "No LIR instruction for HIR type '%s'\n", hir_type_to_string(hir->type));
            break;
    }
}

LIR hir_to_lir(Context *context, HIR *hir) {
    LIR lir = { .context = context, .instructions = malloc(INSTRUCTION_CAPACITY * sizeof(LIR_Instruction)), 
        .count = 0, .capacity = INSTRUCTION_CAPACITY, .label_count = 0,
        .current_break_label = 0, .current_continue_label = 0, .flags = 0 };

    for (size_t i = 0; i < hir->block.count; i++)
        push_hir(&lir, &hir->block.nodes[i]);

    push_instruction(&lir, LIR_EOF, nop, nop);
    return lir;
}

void delete_lir(LIR *lir) {
    free(lir->instructions);
}

char *lir_type_to_string(const LIR_Opcode type) {
    switch (type) {
        case LIR_EOF: return "eof";
        case LIR_NOP: return "nop";
        case LIR_START_FUNC: return "start function";
        case LIR_END_FUNC: return "end function";
        case LIR_RETURN: return "return";
        case LIR_LOAD: return "load";
        case LIR_STORE: return "store";
        case LIR_ADD: return "add";
        case LIR_SUB: return "sub";
        case LIR_MUL: return "mul";
        case LIR_DIV: return "div";
        case LIR_MOD: return "mod";
        case LIR_AND: return "and";
        case LIR_OR: return "or";
        case LIR_XOR: return "xor";
        case LIR_SHL: return "shl";
        case LIR_SHR: return "shr";
        case LIR_PUSH: return "push";
        case LIR_POP: return "pop";
        case LIR_ASM: return "inline assembly";
        case LIR_CALL: return "call";
        case LIR_SETE: return "sete";
        case LIR_SETNE: return "setne";
        case LIR_SETLT: return "setlt";
        case LIR_SETLTE: return "setlte";
        case LIR_SETGT: return "setgt";
        case LIR_SETGTE: return "setgte";
        case LIR_COMPARE: return "compare";
        case LIR_NEW_LABEL: return "new label";
        case LIR_JMP: return "jmp";
        case LIR_JMP_TRUE: return "jmp true";
        case LIR_JMP_FALSE: return "jmp false";
        case LIR_START_BLOCK: return "start block";
        case LIR_END_BLOCK: return "end block";
        case LIR_REFERENCE: return "reference";
        case LIR_NEG: return "neg";
        case LIR_NOT: return "not";
        case LIR_BOOL_NOT: return "boolean not";
        case LIR_EXTERN: return "extern";
        default:
            assert(false);
            return "<none>";
    }
}

char *lir_operand_type_to_string(const LIR_Operand_Type type) {
    switch (type) {
        case OPER_NONE: return "none";
        case OPER_INT: return "int";
        case OPER_FLOAT: return "float";
        case OPER_STRING: return "string";
        case OPER_FUNCTION: return "identifier";
        case OPER_REGISTER: return "register";
        case OPER_LOCAL_VARIABLE: return "local variable";
        case OPER_ARGUMENT: return "argument";
        case OPER_LABEL: return "label";
        case OPER_POINTER: return "pointer";
        case OPER_SIZEOF: return "sizeof";
        default:
            assert(false);
            return "<none>";
    }
}
