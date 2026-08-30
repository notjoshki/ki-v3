#ifndef LIR_H
#define LIR_H

#include "hir.h"
#include "data_type.h"
#include "context.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    OPER_NONE,
    OPER_INT,
    OPER_FLOAT,
    OPER_FUNCTION,
    OPER_REGISTER,
    OPER_LOCAL_VARIABLE,
    OPER_STRING,
    OPER_ARGUMENT,
    OPER_LABEL,
    OPER_POINTER,
    OPER_SIZEOF
} LIR_Operand_Type;

typedef union {
    uint8_t u8;
    int32_t i32;
    int64_t i64;
    uint32_t u32;
    uint64_t u64;
} LIR_Int;

typedef union {
    float f32;
    double f64;
} LIR_Float;

typedef struct {
    char *string;
    size_t length;
} LIR_String;

typedef struct {
    char *name;
    size_t length;
    int module_uid;
    bool exported;
    size_t flags;
} LIR_Function;

typedef struct {
    size_t number;
    bool temporary;
} LIR_Register;

typedef struct {
    size_t uid;
    size_t variable_uid;
    bool exists_from_callee;
} LIR_Variable;

typedef struct {
    size_t index;
    size_t total_count;
    size_t ints_passed;
    size_t floats_passed;
    bool is_caller;
} LIR_Argument;

typedef struct {
    size_t number;
} LIR_Label;

typedef struct {
    LIR_Register register_;
} LIR_Pointer;

typedef struct {
    Data_Type data_type;
} LIR_Sizeof;

typedef struct {
    LIR_Operand_Type type;
    Data_Type data_type;
    size_t module_uid;

    union {
        LIR_Int int_;
        LIR_Float float_;
        LIR_String string;
        LIR_Function function;
        LIR_Register register_;
        LIR_Variable local_variable;
        LIR_Argument argument;
        LIR_Label label;
        bool end_block_is_conditional;
        LIR_Pointer pointer;
        LIR_Sizeof sizeof_;
    };
} LIR_Operand;

typedef enum {
    LIR_EOF,
    LIR_NOP,
    LIR_START_FUNC,
    LIR_END_FUNC,
    LIR_RETURN,
    LIR_LOAD,
    LIR_STORE,
    LIR_ADD,
    LIR_SUB,
    LIR_MUL,
    LIR_DIV,
    LIR_MOD,
    LIR_AND,
    LIR_OR,
    LIR_XOR,
    LIR_SHL,
    LIR_SHR,
    LIR_PUSH,
    LIR_POP,
    LIR_ASM,
    LIR_CALL,
    LIR_SETE,
    LIR_SETNE,
    LIR_SETLT,
    LIR_SETLTE,
    LIR_SETGT,
    LIR_SETGTE,
    LIR_COMPARE,
    LIR_NEW_LABEL,
    LIR_JMP,
    LIR_JMP_TRUE,
    LIR_JMP_FALSE,
    LIR_START_BLOCK,
    LIR_END_BLOCK,
    LIR_REFERENCE,
    LIR_NEG,
    LIR_NOT,
    LIR_BOOL_NOT,
    LIR_EXTERN
} LIR_Opcode;

typedef struct {
    LIR_Opcode type;
    LIR_Operand destination;
    LIR_Operand source;
} LIR_Instruction;

typedef struct {
    Context *context;
    LIR_Instruction *instructions;
    size_t count;
    size_t capacity;
    size_t label_count;
    size_t current_break_label;
    size_t current_continue_label;
    size_t flags;
} LIR;

static inline bool lir_registers_equal(const LIR_Operand *op1, const LIR_Operand *op2) {
    return op1->register_.number == op2->register_.number && op1->register_.temporary == op2->register_.temporary;
}

LIR hir_to_lir(Context *context, HIR *hir);
void delete_lir(LIR *lir);
char *lir_type_to_string(const LIR_Opcode type);
char *lir_operand_type_to_string(const LIR_Operand_Type type);

#endif
