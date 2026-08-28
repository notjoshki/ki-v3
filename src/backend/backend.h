#ifndef BACKEND_H
#define BACKEND_H

#include "lir.h"
#include "data_type.h"
#include "context.h"
#include <stdio.h>
#include <stdbool.h>

char *emit_assembly(Context *context, LIR *lir, const Symbol *entrypoint, const bool initialize_heap, const bool show_lib_externs);

// The following functions are defined in the backend but utilized by the optimizer.

size_t struct_data_type_to_size(Context *context, const Data_Type *data_type, const Module *module);
size_t primitive_type_to_size(const Primitive_Type type);
size_t primitive_type_to_bit_size(const Primitive_Type type);
size_t data_type_to_bit_size(const Data_Type *dt);
bool data_type_bit_sizes_equal(const Data_Type dt1, const Data_Type dt2);
bool primitive_type_bit_sizes_equal(const Primitive_Type primitive_type1, const Primitive_Type primitive_type2);

#endif
