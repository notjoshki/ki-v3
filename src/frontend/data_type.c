#include "data_type.h"
#include "utilities.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

void delete_data_type(Data_Type *dt) {
    if (dt->module_name != NULL) {
        free(dt->module_name);
        dt->module_name = NULL;
    }

    if (dt->primitive_type == PRIM_CUSTOM) {
        free(dt->custom_name);
        dt->custom_name = NULL;
    }

    if (dt->unresolved_array_size != NULL)
        delete_ast(dt->unresolved_array_size);
}

Data_Type copy_data_type(const Data_Type *source) {
    Data_Type copy = create_data_type(source->primitive_type, source->pointer_count);
    copy.array_size = source->array_size;
    copy.module_length = source->module_length;
    copy.module_uid = source->module_uid;

    if (source->primitive_type == PRIM_CUSTOM && source->custom_name != NULL) {
        copy.custom_name = copy_string(source->custom_name, source->custom_length);
        copy.custom_length = source->custom_length;
    }

    // TOFIX
    if (source->module_length > 0 && source->module_name != NULL) {
        copy.module_name = copy_string(source->module_name, source->module_length);
        copy.module_length = source->module_length;
    }

    return copy;
}

char *data_type_to_string(const Data_Type *data_type) {
    char *str = malloc(data_type->pointer_count + data_type->custom_length + 7);
    str[0] = '\0';

    for (size_t i = 0; i < data_type->pointer_count; i++)
        strcat(str, "&");

    switch (data_type->primitive_type) {
        case PRIM_INFER:
            strcat(str, "infer");
            break;
        case PRIM_VOID:
            strcat(str, "void");
            break;
        case PRIM_BOOL:
            strcat(str, "bool");
            break;
        case PRIM_I8:
            strcat(str, "i8");
            break;
        case PRIM_I16:
            strcat(str, "i16");
            break;
        case PRIM_I32:
            strcat(str, "i32");
            break;
        case PRIM_I64:
            strcat(str, "i64");
            break;
        case PRIM_U8:
            strcat(str, "u8");
            break;
        case PRIM_U16:
            strcat(str, "u16");
            break;
        case PRIM_U32:
            strcat(str, "u32");
            break;
        case PRIM_U64:
            strcat(str, "u64");
            break;
        case PRIM_F32:
            strcat(str, "f32");
            break;
        case PRIM_F64:
            strcat(str, "f64");
            break;
        case PRIM_ISIZE:
            strcat(str, "isize");
            break;
        case PRIM_USIZE:
            strcat(str, "usize");
            break;
        case PRIM_CUSTOM:
            strncat(str, data_type->custom_name, data_type->custom_length);
            str[data_type->pointer_count + data_type->custom_length] = '\0';
            break;
        default:
            assert(false);
            strcat(str, "<none>");
            break;
    }

    return str;
}

Data_Type infer_between_two_types(const Data_Type dt1, const Data_Type dt2) {
    if (dt_is_float(dt1))
        return dt2.primitive_type == PRIM_F64 ? dt2 : dt1;

    if (dt_is_float(dt2))
        return dt2;

    Data_Type infer;

    if (dt_is_unsigned(dt1)) {
        if (!dt_is_unsigned(dt2))
            infer = dt2;
        else
            infer = dt2.primitive_type > dt1.primitive_type ? dt2 : dt1;
    } else if (dt_is_unsigned(dt2))
        // dt1 is signed, different values in the enum, turn them into int values to check their bit size.
        infer = (size_t)dt2.primitive_type - (PRIM_U8 - PRIM_I8) < (size_t)dt1.primitive_type ? dt1 : dt2;
    else
        infer = dt2.primitive_type > dt1.primitive_type ? dt2 : dt1;

    if (dt_is_pointer(dt1) || dt_is_pointer(dt2))
        infer.pointer_count++;

    return infer;
}

Data_Type infer_implicit_data_type(const Data_Type dt) {
    if (dt_is_float(dt) || dt.pointer_count > 0 || dt.array_size > 0 || dt.primitive_type == PRIM_BOOL)
        return copy_data_type(&dt);
    else if (dt.primitive_type == PRIM_I8 || dt.primitive_type == PRIM_I16)
        return create_data_type(PRIM_I32, 0);
    else if (dt.primitive_type == PRIM_U8 || dt.primitive_type == PRIM_U16)
        return create_data_type(PRIM_U32, 0);

    return copy_data_type(&dt);
}
