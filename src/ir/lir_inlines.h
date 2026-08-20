#ifndef LIR_INLINES_H
#define LIR_INLINES_H

#include "lir.h"
#include "data_type.h"
#include "context.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static inline LIR_Operand lir_string(char *string, size_t length) {
    return (LIR_Operand){ .type = OPER_STRING, .data_type = create_data_type(PRIM_U8, 1), 
        .string.string = string, .string.length = length };
}

static inline LIR_Operand lir_function(char *name, size_t length, int module_uid, size_t flags) {
    return (LIR_Operand){ .type = OPER_FUNCTION, .data_type = NO_DATA_TYPE, 
        .function.name = name, .function.length = length, .function.module_uid = module_uid, 
        .function.exported = false, .function.flags = flags };
}

static inline LIR_Operand lir_register(Data_Type data_type, size_t number, bool temporary) {
    return (LIR_Operand){ .type = OPER_REGISTER, .data_type = data_type, 
        .register_.number = number, .register_.temporary = temporary };
}

static inline LIR_Operand lir_local_variable(Data_Type data_type, size_t uid, size_t variable_uid, bool exists_from_callee) {
    return (LIR_Operand){ .type = OPER_LOCAL_VARIABLE, .data_type = data_type, 
        .local_variable.uid = uid, .local_variable.variable_uid = variable_uid, 
        .local_variable.exists_from_callee = exists_from_callee };
}

static inline LIR_Operand lir_argument(Data_Type data_type, size_t index, size_t total_count, size_t ints_passed, size_t floats_passed, bool is_caller) {
    return (LIR_Operand){ .type = OPER_ARGUMENT, .data_type = data_type, 
        .argument.index = index, .argument.total_count = total_count, 
        .argument.ints_passed = ints_passed, .argument.floats_passed = floats_passed, .argument.is_caller = is_caller };
}

static inline LIR_Operand lir_label(size_t number) {
    return (LIR_Operand){ .type = OPER_LABEL, .data_type = NO_DATA_TYPE, .label.number = number };
}

static inline LIR_Operand lir_end_block_data(bool is_conditional) {
    return (LIR_Operand){ .type = OPER_NONE, .data_type = NO_DATA_TYPE, .end_block_is_conditional = is_conditional };
}

static inline LIR_Operand lir_pointer(Data_Type data_type, size_t register_number) {
    return (LIR_Operand){ .type = OPER_POINTER, .data_type = data_type, 
        .pointer.register_.number = register_number, .pointer.register_.temporary = true };
}

#endif
