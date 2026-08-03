#include "optimizer.h"
#include "lir.h"
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#define get_this_isnt \
    LIR_Instruction *this_inst = peek(optimizer, 0)

#define get_insts \
    LIR_Instruction *this_inst = peek(optimizer, 0); \
    LIR_Instruction *next_inst = peek(optimizer, 1)

#define PASS_COUNT 3

typedef struct {
    Context *context;
    LIR *lir;
    size_t index;
} Optimizer;

static inline void omit(LIR_Instruction *inst) {
    inst->type = LIR_NOP;
}

static inline bool is_math_opcode(const LIR_Opcode type) {
    return type >= LIR_ADD && type <= LIR_SHR;
}

static inline bool is_power_of_two(const uint64_t x) {
    return (x != 0) && ((x & (x - 1)) == 0);
}

static inline double lir_float_constant_to_digit(const LIR_Operand *operand) {
    return operand->data_type.primitive_type == PRIM_F32 ? (double)operand->float_.f32 : operand->float_.f64;
}

static inline bool is_constant(const LIR_Operand_Type type) {
    return type == OPER_INT || type == OPER_FLOAT || type == OPER_SIZEOF;
}

int64_t lir_int_constant_to_digit(LIR_Operand *operand) {
    switch (operand->data_type.primitive_type) {
        case PRIM_U8: return (int64_t)operand->int_.u8;
        case PRIM_I32: return (int64_t)operand->int_.i32;
        case PRIM_I64: return operand->int_.i64;
        case PRIM_U32: return (int64_t)operand->int_.u32;
        default: return (int64_t)operand->int_.u64;
    }
}

static void do_primary_pass(Optimizer *optimizer);

void optimize_lir(LIR *lir) {
    Optimizer opt = (Optimizer){ .context = lir->context, .lir = lir, .index = 0 };

    for (unsigned int i = 0; i < PASS_COUNT; i++)
        do_primary_pass(&opt);
}

static LIR_Instruction *peek(Optimizer *optimizer, const int offset) {
    if (optimizer->index + offset >= optimizer->lir->count || (int)optimizer->index + offset < 0)
        return &optimizer->lir->instructions[optimizer->lir->count - 1]; // EOF

    LIR_Instruction *inst = &optimizer->lir->instructions[optimizer->index + offset];

    if (inst->type == LIR_NOP)
        return peek(optimizer, offset > 0 ? offset + 1 : offset - 1);

    return inst;
}

static void optimize_stack_operations(Optimizer *optimizer) {
    get_insts;

    if (this_inst->type == LIR_PUSH && next_inst->type == LIR_POP) {
        if (this_inst->source.type == OPER_REGISTER) {
            // push %0
            // pop %0
            // ->
            // ...
            if (lir_registers_equal(&this_inst->source, &next_inst->destination)) {
                omit(this_inst);

                //if (data_types_equal(this_inst->source.data_type, next_inst->destination.data_type))
                    omit(next_inst);
                //else {
                  //  next_inst->type = LIR_LOAD;
                    //next_inst->source = this_inst->source;
                //}

                return;
            }

            // push %0
            // pop %1
            // ->
            // load %1, %0
            omit(this_inst);
            next_inst->type = LIR_STORE; // TODO: Why STORE here and not LOAD?
            next_inst->source = this_inst->source;
            return;
        }

        // push 1
        // pop %0
        // ->
        // load %0, 1
        omit(this_inst);
        next_inst->type = LIR_LOAD;
        next_inst->source = this_inst->source;
        return;
    }

    // load %0, 1
    // push %0
    // ->
    // push 1
    if (this_inst->type == LIR_LOAD && this_inst->destination.type == OPER_REGISTER &&
            next_inst->type == LIR_PUSH && next_inst->source.type == OPER_REGISTER && 
            dt_is_float(this_inst->destination.data_type) == dt_is_float(next_inst->source.data_type) &&
            lir_registers_equal(&this_inst->destination, &next_inst->source)) {

        if (this_inst->source.type != OPER_REGISTER && dt_is_float(this_inst->destination.data_type) == dt_is_float(this_inst->source.data_type)) {
            omit(this_inst);
            next_inst->source = this_inst->source;
            return;
        }
    }

    // push 1
    // (load, store or ref) %1, 2
    // pop %0
    // ->
    // load %0, 1
    // (load, store or ref) %1, 2
    if (this_inst->type == LIR_PUSH && (next_inst->type == LIR_LOAD || next_inst->type == LIR_STORE || next_inst->type == LIR_REFERENCE)) {
        LIR_Instruction *next2 = peek(optimizer, 2);

        if (next2->type == LIR_POP && !lir_registers_equal(&next2->destination, &next_inst->destination)) {
            omit(next2);
            this_inst->type = LIR_LOAD;
            this_inst->destination = next2->destination;
            return;
        }
    }

    // pop %0
    // store $0, %0
    // ->
    // pop $0
    if (this_inst->type == LIR_POP && (next_inst->type == LIR_LOAD || next_inst->type == LIR_STORE) &&
            this_inst->destination.type == OPER_REGISTER && next_inst->source.type == OPER_REGISTER &&
            lir_registers_equal(&this_inst->destination, &next_inst->source) && 
            data_types_equal(this_inst->destination.data_type, next_inst->source.data_type)) {
        omit(this_inst);
        next_inst->type = LIR_POP;
        return;
    }
}

static void optimize_loads_and_stores(Optimizer *optimizer) {
    get_insts;

    // (load or store) %0, %0
    // ->
    // ...
    if ((this_inst->type == LIR_LOAD || this_inst->type == LIR_STORE) && this_inst->destination.type == OPER_REGISTER &&
            this_inst->source.type == OPER_REGISTER && lir_registers_equal(&this_inst->destination, &this_inst->source) &&
            dt_is_float(this_inst->destination.data_type) == dt_is_float(this_inst->source.data_type) &&
            (data_type_bit_sizes_equal(this_inst->destination.data_type, this_inst->source.data_type) ||
            // Pointer types don't match but are both pointers.
            (dt_is_pointer(this_inst->destination.data_type) && dt_is_pointer(this_inst->source.data_type)))) { 
        omit(this_inst);
        return;
    }

    // load %0, (const)
    // store $0, %0
    // ->
    // store $0, (const)
    if (this_inst->type == LIR_LOAD && is_constant(this_inst->source.type) &&
            next_inst->type == LIR_STORE && next_inst->source.type == OPER_REGISTER && 
            lir_registers_equal(&next_inst->source, &this_inst->destination) &&
            dt_is_float(this_inst->destination.data_type) == dt_is_float(next_inst->source.data_type)) {
        omit(this_inst);
        next_inst->source = this_inst->source;
        return;
    }

    // load %0, (possible register argument)
    // store (any), %0
    // ->
    // store (any), (any)
    if (this_inst->type == LIR_LOAD && this_inst->destination.type == OPER_REGISTER &&
            this_inst->source.type == OPER_ARGUMENT && next_inst->type == LIR_STORE && 
            next_inst->source.type == OPER_REGISTER && lir_registers_equal(&this_inst->destination, &next_inst->source)) {
        // TODO: Make sure this works on other platforms other than x86_64.
        omit(this_inst);
        next_inst->source = this_inst->source;
        return;
    }

    // load %0, 1
    // (any) (any), %0
    // ->
    // (any) (any), 1
    // NOTE: This is usually the culprit for breaking load/store sequences.
    if (this_inst->type == LIR_LOAD && this_inst->destination.type == OPER_REGISTER &&
            next_inst->source.type == OPER_REGISTER && lir_registers_equal(&this_inst->destination, &next_inst->source) &&
            dt_is_float(this_inst->destination.data_type) == dt_is_float(next_inst->destination.data_type) &&
            (data_types_equal(this_inst->source.data_type, next_inst->destination.data_type) ||
            data_type_bit_sizes_equal(this_inst->source.data_type, next_inst->destination.data_type))) {
        bool apply = true;

        // Stop same-register conversions getting removed here.
        if (this_inst->source.type == OPER_REGISTER && lir_registers_equal(&this_inst->destination, &this_inst->source) &&
                dt_is_float(this_inst->destination.data_type) != dt_is_float(this_inst->source.data_type))
            apply = false;

        if (apply) {
            omit(this_inst);
            next_inst->source = this_inst->source;
            return;
        }
    }

    // ref %0, x
    // store %1, %0
    // ->
    // ref %1, @x
    if (this_inst->type == LIR_REFERENCE && this_inst->destination.type == OPER_REGISTER &&
            next_inst->type == LIR_STORE && next_inst->destination.type == OPER_REGISTER &&
            next_inst->source.type == OPER_POINTER && next_inst->source.pointer.register_.number == this_inst->destination.register_.number &&
            next_inst->source.pointer.register_.temporary == this_inst->destination.register_.temporary) {
        omit(this_inst);
        next_inst->type = LIR_REFERENCE;
        next_inst->source = this_inst->source;
        return;
    }

    // ref %0, (any)
    // store (non variable/pointer), %0
    // ->
    // ref (non variable/pointer), (any)
    // NOTE: We don't want to omit a store instruction into a variable because the backend
    // creates local variables with the store instruction.
    if (this_inst->type == LIR_REFERENCE && this_inst->destination.type == OPER_REGISTER &&
            next_inst->type == LIR_STORE && next_inst->source.type == OPER_REGISTER &&
            next_inst->destination.type != OPER_LOCAL_VARIABLE && next_inst->destination.type != OPER_POINTER &&
            lir_registers_equal(&this_inst->destination, &next_inst->source)) {
        omit(this_inst);
        next_inst->type = LIR_REFERENCE;
        next_inst->source = this_inst->source;
        return;
    }

    // ref %0, ptr %0
    // ->
    // ...
    if (this_inst->type == LIR_REFERENCE && this_inst->destination.type == OPER_REGISTER &&
            this_inst->source.type == OPER_POINTER && this_inst->destination.register_.number == this_inst->source.pointer.register_.number &&
            this_inst->destination.register_.temporary == this_inst->source.pointer.register_.temporary) {
        omit(this_inst);
        return;
    }

    /*
    // ref %0, ptr @x
    // (load or store) %1, %0
    // ->
    // ref %1, ptr @x
    if (this_inst->type == LIR_REFERENCE && (next_inst->type == LIR_LOAD || next_inst->type == LIR_STORE) &&
            this_inst->destination.type == OPER_REGISTER && next_inst->destination.type == OPER_REGISTER) {
        omit(this_inst);
        next_inst->type = LIR_REFERENCE;
        next_inst->source = this_inst->source;
        return;
    }
    */
}

static void optimize_math_operations(Optimizer *optimizer) {
    get_insts;

    if (this_inst->source.type == OPER_SIZEOF) {
        size_t size;

        if (this_inst->source.sizeof_.data_type.primitive_type == PRIM_CUSTOM)
            size = struct_data_type_to_size(optimizer->context, &this_inst->source.sizeof_.data_type, 
                get_module(optimizer->context, (size_t)this_inst->source.data_type.module_uid));
        else
            size = primitive_type_to_size(data_type_to_primitive_type(&this_inst->source.sizeof_.data_type));

        this_inst->source = (LIR_Operand){ .type = OPER_INT, .data_type = create_data_type(PRIM_USIZE, 0), .int_.u64 = size };
    }

    // (add, sub, shl or shr) %0, 0
    // ->
    // ...
    if ((this_inst->type == LIR_ADD || this_inst->type == LIR_SUB || this_inst->type == LIR_SHL || this_inst->type == LIR_SHR) && 
            ((this_inst->source.type == OPER_INT && lir_int_constant_to_digit(&this_inst->source) == 0) ||
            (this_inst->source.type == OPER_FLOAT && lir_float_constant_to_digit(&this_inst->source) == 0))) {
        omit(this_inst);
        return;
    }

    // (mul or div) %0, 1
    // ->
    // ...
    if ((this_inst->type == LIR_MUL || this_inst->type == LIR_DIV) && 
            ((this_inst->source.type == OPER_INT && lir_int_constant_to_digit(&this_inst->source) == 1) ||
            (this_inst->source.type == OPER_FLOAT && lir_float_constant_to_digit(&this_inst->source) == 1))) {
        omit(this_inst);
        return;
    }

    // (mul or div) %0, (2, 4, 8, 16...)
    // ->
    // (shl or shr) %0, (1, 2, 3, 4...)
    if ((this_inst->type == LIR_MUL || this_inst->type == LIR_DIV) && this_inst->destination.type == OPER_REGISTER &&
            is_constant(this_inst->source.type) && !dt_is_float(this_inst->destination.data_type)) {
        uint64_t power;
        bool is_integer = true;

        if (this_inst->source.type == OPER_FLOAT) {
            double value = lir_float_constant_to_digit(&this_inst->source);
            power = (uint64_t)value;
            is_integer = (double)power == value;
        } else
            power = (uint64_t)lir_int_constant_to_digit(&this_inst->source);

        if (is_integer && is_power_of_two(power)) {
            this_inst->type = this_inst->type == LIR_MUL ? LIR_SHL : LIR_SHR;
            this_inst->source = (LIR_Operand){ .type = OPER_INT, .data_type = create_data_type(PRIM_U32, 0),
                .int_.u32 = (uint32_t)log2(power) };
            return;
        }
    }

    // store %1, %0
    // pop %0
    // (add or mul) %0, %1
    // ->
    // pop %1
    // (add or mul) %0, %1
    // ...
    if (this_inst->type == LIR_STORE && this_inst->destination.type == OPER_REGISTER && this_inst->source.type == OPER_REGISTER &&
            next_inst->type == LIR_POP && next_inst->destination.type == OPER_REGISTER && 
            lir_registers_equal(&this_inst->source, &next_inst->destination)) {
        LIR_Instruction *next2 = peek(optimizer, 2);

        if ((next2->type == LIR_ADD || next2->type == LIR_MUL) && lir_registers_equal(&next2->destination, &next_inst->destination) &&
                lir_registers_equal(&next2->source, &this_inst->destination)) {
            omit(this_inst);
            next_inst->destination = this_inst->destination;
            return;
        }
    }

    // store %1, 1
    // add %0, %1
    // NOTE: This usually happens from previous optimizations, but doesn't fit the requirements for constant folding.
    if (this_inst->type == LIR_STORE && this_inst->destination.type == OPER_REGISTER &&
            is_constant(this_inst->source.type) && is_math_opcode(next_inst->type) && 
            next_inst->destination.type == OPER_REGISTER && lir_registers_equal(&next_inst->source, &this_inst->destination)) {
        omit(this_inst);
        next_inst->source = this_inst->source;
        return;
    }
}

// TOFIX: These double functions COULD cause problems for u64 and i64 stuff if they're close to the limits as
// f64 won't cover the u64 high values and f64 isn't accurate for i64.
static double calculate_constant_math(const LIR_Opcode type, const double lhs, const double rhs) {
    switch (type) {
        case LIR_ADD: return lhs + rhs;
        case LIR_SUB: return lhs - rhs;
        case LIR_MUL: return lhs * rhs;
        case LIR_DIV: return lhs / rhs;
        case LIR_MOD: return (int64_t)lhs / (int64_t)rhs;
        case LIR_AND: return (int64_t)lhs & (int64_t)rhs;
        case LIR_OR: return (int64_t)lhs | (int64_t)rhs;
        case LIR_XOR: return (int64_t)lhs ^ (int64_t)rhs;
        case LIR_SHL: return (int64_t)lhs << (int64_t)rhs;
        default: return (int64_t)lhs >> (int64_t)rhs;
    }
}

static double get_lir_constant_value(LIR_Operand *operand) {
    return dt_is_float(operand->data_type) ? lir_float_constant_to_digit(operand) : lir_int_constant_to_digit(operand);
}

static void optimize_constant_folding(Optimizer *optimizer) {
    get_insts;

    // load %0, 1
    // add %0, 2
    // ...etc
    // ->
    // load %0, 3
    if (this_inst->type == LIR_LOAD && is_math_opcode(next_inst->type) &&
            (this_inst->source.type == OPER_INT || this_inst->source.type == OPER_FLOAT) &&
            (next_inst->source.type == OPER_INT || next_inst->source.type == OPER_FLOAT) &&
            next_inst->destination.type == OPER_REGISTER && this_inst->destination.type == OPER_REGISTER &&
            lir_registers_equal(&this_inst->destination, &next_inst->destination)) {
        const double result = calculate_constant_math(next_inst->type,
            get_lir_constant_value(&this_inst->source), get_lir_constant_value(&next_inst->source));

        omit(next_inst);

        if (dt_is_float(this_inst->destination.data_type))
            this_inst->source = (LIR_Operand){ .type = OPER_FLOAT, .data_type = create_data_type(PRIM_F64, 0), .float_.f64 = result };
        else
            this_inst->source = (LIR_Operand){ .type = OPER_INT, .data_type = create_data_type(PRIM_I64, 0), .int_.i64 = (int64_t)result };

        return;
    }

    // add %0, 1
    // add %0, 2
    // ->
    // add %0, 3
    if (is_math_opcode(this_inst->type) && is_math_opcode(next_inst->type) && this_inst->destination.type == OPER_REGISTER &&
            next_inst->destination.type == OPER_REGISTER && lir_registers_equal(&this_inst->destination, &next_inst->destination) &&
            (this_inst->source.type == OPER_INT || this_inst->source.type == OPER_FLOAT) &&
            (next_inst->source.type == OPER_INT || next_inst->source.type == OPER_FLOAT) &&
            dt_is_float(this_inst->destination.data_type) == dt_is_float(next_inst->destination.data_type)) {
        const double result = calculate_constant_math(next_inst->type,
            get_lir_constant_value(&this_inst->source), get_lir_constant_value(&next_inst->source));

        omit(this_inst);

        if (dt_is_float(next_inst->destination.data_type))
            next_inst->source = (LIR_Operand){ .type = OPER_FLOAT, .data_type = create_data_type(PRIM_F64, 0), .float_.f64 = result };
        else
            next_inst->source = (LIR_Operand){ .type = OPER_INT, .data_type = create_data_type(PRIM_I64, 0), .int_.i64 = (int64_t)result };

        return;
    }
}

/*
static void optimize_sizeof(Optimizer *optimizer) {
    get_this_isnt;

    if (this_inst->source.type == OPER_SIZEOF) {
        LIR_Operand constant = (LIR_Operand){ .type = OPER_INT, .data_type = create_data_type(PRIM_U64, 0), 
            .int_.u64 = primitive_type_to_size(data_type_to_primitive_type(&this_inst->source.sizeof_.data_type)) };
        this_inst->source = constant;
    }
}
*/

static void do_primary_pass(Optimizer *optimizer) {
    for (optimizer->index = 0; optimizer->index < optimizer->lir->count; optimizer->index++) {
        optimize_stack_operations(optimizer);
        optimize_loads_and_stores(optimizer);
        optimize_math_operations(optimizer);
        optimize_constant_folding(optimizer);
    }
}
