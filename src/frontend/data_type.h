#ifndef DATA_TYPE_H
#define DATA_TYPE_H

#include <stdio.h>
#include <stdbool.h>

#define NO_DATA_TYPE create_data_type(PRIM_VOID, 0)

#define DATA_TYPE_IGNORE_MODULE -1

#define UNRESOLVED_ARRAY_SIZE 0xdeadbeef

typedef enum {
    PRIM_INFER,
    PRIM_VOID,
    PRIM_BOOL,
    PRIM_I8,
    PRIM_I16,
    PRIM_I32,
    PRIM_I64,
    PRIM_U8,
    PRIM_U16,
    PRIM_U32,
    PRIM_U64,
    PRIM_F32,
    PRIM_F64,
    PRIM_ISIZE,
    PRIM_USIZE,
    PRIM_CUSTOM
} Primitive_Type;

typedef struct Module Module;
typedef struct AST AST;

typedef struct {
    Primitive_Type primitive_type;
    char *custom_name;
    size_t custom_length;
    char *module_name;
    size_t pointer_count;
    size_t array_size;
    AST *unresolved_array_size;
    size_t module_length;
    int module_uid;
} Data_Type;

static inline bool bin_is_unsigned(const Primitive_Type bt) {
    return (bt >= PRIM_U8 && bt <= PRIM_U64) || bt == PRIM_USIZE || bt == PRIM_BOOL;
}

static inline bool dt_is_unsigned(const Data_Type dt) {
    return bin_is_unsigned(dt.primitive_type);
}

static inline bool bin_is_float(const Primitive_Type bt) {
    return bt == PRIM_F32 || bt == PRIM_F64;
}

static inline bool dt_is_float(const Data_Type dt) {
    return dt.pointer_count == 0 && dt.array_size == 0 && bin_is_float(dt.primitive_type);
}

static inline bool dt_is_pointer(const Data_Type dt) {
    return dt.pointer_count > 0;
}

static inline bool data_types_equal(const Data_Type dt1, const Data_Type dt2) {
    return dt1.primitive_type == dt2.primitive_type && dt1.pointer_count == dt2.pointer_count;
}

static inline Data_Type create_data_type(Primitive_Type primitive_type, size_t pointer_count) {
    return (Data_Type){ .primitive_type = primitive_type, .pointer_count = pointer_count, 
        .array_size = 0, .unresolved_array_size = NULL,
        .custom_length = 0, .module_length = 0, .module_name = NULL, .module_uid = -1 };
}

void delete_data_type(Data_Type *dt);
Data_Type copy_data_type(const Data_Type *source);

char *data_type_to_string(const Data_Type *data_type);

static inline Primitive_Type data_type_to_primitive_type(const Data_Type *data_type) {
    return data_type->pointer_count > 0 ? PRIM_USIZE : data_type->primitive_type;
}

Data_Type infer_between_two_types(const Data_Type dt1, const Data_Type dt2);
Data_Type infer_implicit_data_type(const Data_Type dt);

#endif
