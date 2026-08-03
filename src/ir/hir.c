#include "hir.h"
#include "ast.h"
#include "logger.h"
#include "utilities.h"
#include "context.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#define nop create_hir(HIR_NOP)
#define nodat create_data(DATA_NONE)

#define BLOCK_CAPACITY 4
#define IMPORT_NODE_CAPACITY 4

#define TO_STRING_INDENT_SPACE_COUNT 4

static inline HIR create_hir(HIR_Type type) {
    return (HIR){ .type = type };
}

static inline HIR_Data create_data(HIR_Data_Type type) {
    return (HIR_Data){ .type = type };
}

static HIR_Block create_block() {
    return (HIR_Block){ .nodes = malloc(BLOCK_CAPACITY * sizeof(HIR)), .count = 0, .capacity = BLOCK_CAPACITY };
}

static void update_symbol_data_type(Data_Type *symbol_type, Data_Type *new_data_type) {
    Data_Type old_type = *symbol_type;
    *symbol_type = copy_data_type(new_data_type);
    delete_data_type(&old_type);
}

static void push_node(HIR_Block *block, HIR node) {
    if (block->count + 1 > block->capacity) {
        block->capacity *= 2;
        block->nodes = realloc(block->nodes, block->capacity * sizeof(HIR));
    }

    block->nodes[block->count++] = node;
}

static HIR_Block ast_list_to_block(Context *context, List *list) {
    HIR_Block block = create_block();

    for (size_t i = 0; i < list->count; i++)
        push_node(&block, ast_to_hir(context, (AST *)list->items[i]));

    return block;
}

static void delete_block(HIR_Block *block) {
    for (size_t i = 0; i < block->count; i++)
        delete_hir(&block->nodes[i]);

    free(block->nodes);
}

static void delete_data(HIR_Data *data) {
    switch (data->type) {
        case DATA_LOCAL_VARIABLE:
            delete_data_type(&data->local_variable.data_type);
            break;
        case DATA_MATH:
        case DATA_CONDITION:
            for (size_t i = 0; i < data->expression.count; i++)
                delete_data(&data->expression.data[i]);

            free(data->expression.data);
            break;
        case DATA_CALL:
            for (size_t i = 0; i < data->call.argument_count; i++)
                delete_data(&data->call.arguments[i]);
            
            free(data->call.arguments);
            delete_data_type(&data->call.data_type);
            break;
        case DATA_REFERENCE:
            delete_data(data->reference.value);
            free(data->reference.value);
            break;
        case DATA_INDEX:
            delete_data(data->index.base);
            delete_data(data->index.index);
            free(data->index.base);
            free(data->index.index);
            break;
        case DATA_UNARY:
            delete_data(data->unary.value);
            free(data->unary.value);
            break;
        case DATA_CAST:
            delete_data(data->cast.value);
            free(data->cast.value);
            break;
        case DATA_DEREFERENCE:
            delete_data(data->dereference.value);
            free(data->dereference.value);
            break;
        case DATA_STRUCT_MEMBER:
            delete_data(data->struct_member.lhs);
            free(data->struct_member.lhs);
            break;
        case DATA_STRUCT_INITIALIZER:
            for (size_t i = 0; i < data->struct_initializer.value_count; i++)
                delete_data(&data->struct_initializer.values[i]);

            free(data->struct_initializer.values);
            free(data->struct_initializer.annotations);
            free(data->struct_initializer.annotation_lengths);
            delete_data_type(&data->struct_initializer.data_type);
            break;
        default: break;
    }
}

static Data_Type ast_data_type_to_hir_data_type(Context *context, Data_Type *data_type, AST *infer_value, const size_t module_uid);

static void check_data_types_are_compatible(Context *context, const Source *source, const size_t module_uid, Data_Type expected, Data_Type received) {
    // Arrays disolve into pointers.
    if (expected.array_size > 0)
        expected.pointer_count++;

    if (received.array_size > 0)
        received.pointer_count++;

    // 'expected' can sometimes be from an AST symbol that hasn't been converted to its HIR type yet.
    Data_Type hir_expected = ast_data_type_to_hir_data_type(context, &expected, NULL, module_uid);

    // &void can bypass these checks.
    if ((received.primitive_type == PRIM_VOID && received.pointer_count > 0 && hir_expected.pointer_count > 0) ||
            (hir_expected.primitive_type == PRIM_VOID && hir_expected.pointer_count > 0 && received.pointer_count > 0)) {
        delete_data_type(&hir_expected);
        return;
    }

    if (hir_expected.pointer_count != received.pointer_count || 
            ((hir_expected.pointer_count > 0 || received.pointer_count > 0) && !data_types_equal(hir_expected, received))) {

        char *expected_str = data_type_to_string(&hir_expected);
        char *received_str = data_type_to_string(&received);

        log(ERROR_CRITICAL, source->path, source->ln, source->col, 
            "Conflicting pointer types; found '%s' when expecting '%s'\n", received_str, expected_str);

        free(expected_str);
        free(received_str);
    }

    delete_data_type(&hir_expected);
}

static void check_custom_type_for_primitive_and_replace(Data_Type *data_type) {
    // Check for a primitive type, probably an alias.

    if (compare_string(data_type->custom_name, data_type->custom_length, "void", 4))
        data_type->primitive_type = PRIM_VOID;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "bool", 4))
        data_type->primitive_type = PRIM_BOOL;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "char", 4) || 
            compare_string(data_type->custom_name, data_type->custom_length, "u8", 2))
        data_type->primitive_type = PRIM_U8;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "i8", 2))
        data_type->primitive_type = PRIM_I8;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "i16", 3))
        data_type->primitive_type = PRIM_I16;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "i32", 3))
        data_type->primitive_type = PRIM_I32;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "i64", 3))
        data_type->primitive_type = PRIM_I64;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "u16", 3))
        data_type->primitive_type = PRIM_U16;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "u32", 3))
        data_type->primitive_type = PRIM_U32;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "u64", 3))
        data_type->primitive_type = PRIM_U64;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "f32", 3))
        data_type->primitive_type = PRIM_F32;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "f64", 3))
        data_type->primitive_type = PRIM_F64;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "isize", 5))
        data_type->primitive_type = PRIM_ISIZE;
    else if (compare_string(data_type->custom_name, data_type->custom_length, "usize", 5))
        data_type->primitive_type = PRIM_USIZE;
    else
        return;

    free(data_type->custom_name);
}

static size_t get_constant_array_size(Context *context, const Data_Type *data_type, AST *ast) {
    if (ast->type == AST_INT) {
        switch (data_type->primitive_type) {
            case PRIM_I32: return ast->literal.i32;
            case PRIM_I64:
            case PRIM_ISIZE: return ast->literal.i64;
            case PRIM_U8: return ast->literal.u8;
            case PRIM_U32: return ast->literal.u32;
            case PRIM_U64:
            case PRIM_USIZE: return ast->literal.u64;
            default:
                assert(false);
                return 1;
        }
    }

    assert(ast->type == AST_CONSTANT);
    Data_Type dt = get_ast_data_type(context, ast->constant.value);
    size_t size = get_constant_array_size(context, &dt, ast->constant.value);
    delete_data_type(&dt);
    return size;
}

static Data_Type ast_data_type_to_hir_data_type(Context *context, Data_Type *data_type, AST *infer_value, const size_t module_uid) {
    if (data_type->primitive_type == PRIM_INFER) {
        assert(infer_value != NULL);
        return infer_implicit_data_type(get_ast_data_type(context, infer_value));
    }

    size_t array_size;

    if (data_type->unresolved_array_size != NULL) {
        assert(data_type->unresolved_array_size->type == AST_CONSTANT);
        array_size = get_constant_array_size(context, data_type, data_type->unresolved_array_size);
    } else
        array_size = data_type->array_size;

    if (data_type->primitive_type != PRIM_CUSTOM) {
        Data_Type dt = copy_data_type(data_type);
        dt.array_size = array_size;
        return dt;
    }

    Alias *alias = find_alias(context, data_type->custom_name, data_type->custom_length, module_uid, true);

    if (alias != NULL) {
        free(data_type->custom_name);
        data_type->custom_name = copy_string(alias->replacement, alias->replacement_length);
        data_type->custom_length = alias->replacement_length;
        
        Data_Type alias_type = ast_data_type_to_hir_data_type(context, data_type, infer_value, module_uid);
        alias_type.array_size = array_size;
        return alias_type;
    }

    Custom_Type *type = find_custom_type(context, CUST_ENUM, data_type->custom_name, data_type->custom_length, module_uid);

    if (type != NULL) {
        Data_Type enum_type = copy_data_type(&type->enum_data_type);
        enum_type.array_size = array_size;
        return enum_type;
    }

    Data_Type copy = copy_data_type(data_type);
    copy.array_size = array_size;
    check_custom_type_for_primitive_and_replace(&copy);
    return copy;
}

static HIR_Data ast_to_hir_data(Context *context, AST *ast);

static HIR_Data literal_to_hir_data(AST *ast) {
    HIR_Data data = create_data(DATA_LITERAL);
    data.literal.data_type = ast->literal.data_type;

    if (ast->type == AST_STRING) {
        data.literal.string.string = ast->literal.string;
        data.literal.string.length = ast->literal.string_length;
        return data;
    }

    switch (data.literal.data_type.primitive_type) {
        case PRIM_I32:
            data.literal.i32 = ast->literal.i32;
            break;
        case PRIM_I64:
            data.literal.i64 = ast->literal.i64;
            break;
        case PRIM_U32:
            data.literal.u32 = ast->literal.u32;
            break;
        case PRIM_U64:
            data.literal.u64 = ast->literal.u64;
            break;
        case PRIM_F32:
            data.literal.f32 = ast->literal.f32;
            break;
        default:
            data.literal.f64 = ast->literal.f64;
            break;
    }

    return data;
}

static HIR_Data variable_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_LOCAL_VARIABLE);
    data.local_variable.name = ast->variable.name;
    data.local_variable.name_length = ast->variable.name_length;
    //data.local_variable.data_type = *ast->variable.symbol->data_type;
    const Symbol *symbol = get_symbol(context, ast->variable.symbol_uid);
    data.local_variable.data_type = ast_data_type_to_hir_data_type(context, symbol->data_type, NULL, ast->module_uid);
    data.local_variable.variable_uid = symbol->attribute.variable_uid;
    data.local_variable.uid = symbol->uid;
    return data;
}

static HIR_Data parameter_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_LOCAL_VARIABLE);
    data.local_variable.name = ast->parameter.name;
    data.local_variable.name_length = ast->parameter.name_length;
    data.local_variable.data_type = ast_data_type_to_hir_data_type(context, &ast->parameter.data_type, ast->parameter.default_value, ast->module_uid);
    data.local_variable.variable_uid = ast->parameter.variable_uid;
    data.local_variable.uid = ast->uid;
    return data;
}

static HIR_Data math_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_MATH);
    data.expression.data = malloc(ast->math.nodes.count * sizeof(HIR_Data));
    data.expression.count = ast->math.nodes.count;

    for (size_t i = 0; i < ast->math.nodes.count; i++)
        data.expression.data[i] = ast_to_hir_data(context, (AST *)ast->math.nodes.items[i]);

    return data;
}

static HIR_Data operator_to_hir_data(AST *ast) {
    HIR_Data data = create_data(DATA_OPERATOR);
    data.operator.type = OP_MATH;
    data.operator.math = ast->operator.type;
    return data;
}

static HIR_Call call_to_hir(Context *context, AST *ast) {
    const Symbol *symbol = get_symbol(context, ast->call.symbol_uid);
    assert(symbol != NULL);
    List *params = symbol->attribute.parameters;
    assert(params != NULL);

    HIR_Call call = { .name = ast->call.name, .name_length = ast->call.name_length, 
        .module_uid = symbol->module_uid,
        .parameters = params, .arguments = malloc(params->count * sizeof(HIR_Data)), 
        //.data_type = *ast->call.symbol->data_type };
        .data_type = ast_data_type_to_hir_data_type(context, symbol->data_type, NULL, ast->module_uid) };

    for (size_t i = 0; i < params->count; i++) {
        AST *arg;
        
        if (i >= ast->call.arguments.count) {
            arg = ((AST *)params->items[i])->parameter.default_value;
            assert(arg != NULL);
        } else
            arg = (AST *)ast->call.arguments.items[i];

        Data_Type arg_type = ast_data_type_to_hir_data_type(context, &((AST **)params->items)[i]->parameter.data_type, 
            ((AST *)params->items[i])->parameter.default_value, ast->module_uid);

        call.arguments[i] = ast_to_hir_data(context, arg);
        check_data_types_are_compatible(context, &arg->source, arg->module_uid, 
            arg_type, get_hir_data_type(context, &call.arguments[i]));

        delete_data_type(&arg_type);
    }

    call.argument_count = params->count;
    call.flags = symbol->flags;
    return call;
}

static HIR_Data call_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_CALL);
    data.call = call_to_hir(context, ast);
    return data;
}

static HIR_Data condition_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_CONDITION);
    data.expression.data = malloc(ast->condition.nodes.count * sizeof(HIR_Data));
    data.expression.count = ast->condition.nodes.count;

    for (size_t i = 0; i < ast->condition.nodes.count; i++)
        data.expression.data[i] = ast_to_hir_data(context, (AST *)ast->condition.nodes.items[i]);

    return data;
}

static HIR_Data reference_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_REFERENCE);
    data.reference.value = malloc(sizeof(HIR_Data));
    *data.reference.value = ast_to_hir_data(context, ast->reference.value);

    Data_Type dt = get_hir_data_type(context, data.reference.value);

    if (dt.array_size > 0) {
        AST *value = ast->reference.value;
        char *type = data_type_to_string(&dt);
        log(ERROR_CRITICAL, value->source.path, value->source.ln, value->source.col,
            "Invalid reference value to array of type '%s'; no address to point to\n", type);
        free(type);
    }

    return data;
}

static HIR_Data index_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_INDEX);
    data.index.base = malloc(sizeof(HIR_Data));
    *data.index.base = ast_to_hir_data(context, ast->index.base);
    data.index.index = malloc(sizeof(HIR_Data));
    *data.index.index = ast_to_hir_data(context, ast->index.index);
    return data;
}

static HIR_Data sizeof_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_OPERATOR);
    data.operator.type = OP_SIZEOF;
    data.operator.sizeof_ = ast->sizeof_.value == NULL ? ast->sizeof_.data_type : get_ast_data_type(context, ast->sizeof_.value);
    return data;
}

static HIR_Data unary_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_UNARY);
    data.unary.type = ast->unary.type;
    data.unary.value = malloc(sizeof(HIR_Data));
    *data.unary.value = ast_to_hir_data(context, ast->unary.value);
    return data;
}

static HIR_Data cast_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_CAST);
    data.cast.data_type = ast->cast.data_type;
    data.cast.value = malloc(sizeof(HIR_Data));
    *data.cast.value = ast_to_hir_data(context, ast->cast.value);
    return data;
}

static HIR_Data dereference_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_DEREFERENCE);
    data.dereference.value = malloc(sizeof(HIR_Data));
    *data.reference.value = ast_to_hir_data(context, ast->dereference.value);
    return data;
}

static HIR_Data enum_member_to_hir_data(Custom_Type_Member *member) {
    Custom_Type_Enum_Value *value = &member->enum_value;
    HIR_Data data = create_data(DATA_LITERAL);
    data.literal.data_type = member->data_type;

    if (member->data_type.primitive_type == PRIM_F32)
        data.literal.f32 = value->f32;
    else if (member->data_type.primitive_type == PRIM_F64)
        data.literal.f64 = value->f64;
    else
        data.literal.i32 = value->i32;

    return data;
}

static HIR_Data member_to_hir_data(Context *context, AST *ast) {
    Custom_Type *type = get_custom_type(context, ast->member.custom_type_symbol_uid);
    Custom_Type_Member *member = get_custom_type_member(context, type->uid, ast->member.member_symbol_uid);

    if (type->type == CUST_ENUM)
        return enum_member_to_hir_data(member);

    HIR_Data data = create_data(DATA_STRUCT_MEMBER);
    data.struct_member.lhs = malloc(sizeof(HIR_Data));
    *data.struct_member.lhs = ast_to_hir_data(context, ast->member.struct_access_lhs);
    data.struct_member.member_symbol_uid = member->uid;
    data.struct_member.custom_type_symbol_uid = type->uid;
    return data;
}

static HIR_Data null_to_hir_data() {
    HIR_Data data = create_data(DATA_LITERAL);
    data.literal.data_type = create_data_type(PRIM_VOID, 1);
    data.literal.u64 = 0;
    return data;
}

static HIR_Data bool_to_hir_data(AST *ast) {
    HIR_Data data = create_data(DATA_LITERAL);
    data.literal.data_type = create_data_type(PRIM_U8, 0);
    data.literal.u8 = ast->bool_value;
    return data;
}

static HIR_Data struct_initializer_to_hir_data(Context *context, AST *ast) {
    HIR_Data data = create_data(DATA_STRUCT_INITIALIZER);
    Data_Type dt = copy_data_type(&ast->struct_initializer.data_type);
    data.struct_initializer.data_type = dt;

    Custom_Type *type = find_custom_type(context, CUST_STRUCT,
        dt.custom_name, dt.custom_length, dt.module_name != NULL ? (size_t)dt.module_uid : ast->module_uid);
    assert(type != NULL);
    data.struct_initializer.custom_type_symbol_uid = type->uid;

    data.struct_initializer.values = malloc(ast->struct_initializer.values.count * sizeof(HIR_Data));
    data.struct_initializer.value_count = ast->struct_initializer.values.count;
    data.struct_initializer.annotations = malloc(ast->struct_initializer.values.count * sizeof(char *));
    data.struct_initializer.annotation_lengths = malloc(ast->struct_initializer.values.count * sizeof(size_t));

    for (size_t i = 0; i < ast->struct_initializer.values.count; i++) {
        data.struct_initializer.values[i] = ast_to_hir_data(context, (AST *)ast->struct_initializer.values.items[i]);
        data.struct_initializer.annotations[i] = ((AST *)ast->struct_initializer.annotations.items[i])->identifier.identifier;
        data.struct_initializer.annotation_lengths[i] = ((AST *)ast->struct_initializer.annotations.items[i])->identifier.length;
    }

    return data;
}

static HIR_Data ast_to_hir_data(Context *context, AST *ast) {
    if (ast == NULL)
        return nodat;

    switch (ast->type) {
        case AST_NOP: return nodat;
        case AST_INT:
        case AST_FLOAT:
        case AST_STRING: return literal_to_hir_data(ast);
        case AST_VARIABLE: return variable_to_hir_data(context, ast);
        case AST_MATH: return math_to_hir_data(context, ast);
        case AST_OPERATOR: return operator_to_hir_data(ast);
        case AST_CALL: return call_to_hir_data(context, ast);
        case AST_PARAMETER: return parameter_to_hir_data(context, ast);
        case AST_CONDITION: return condition_to_hir_data(context, ast);
        case AST_REFERENCE: return reference_to_hir_data(context, ast);
        case AST_INDEX: return index_to_hir_data(context, ast);
        case AST_SIZEOF: return sizeof_to_hir_data(context, ast);
        case AST_EXPRESSION: return ast_to_hir_data(context, ast->expression.value);
        case AST_UNARY: return unary_to_hir_data(context, ast);
        case AST_CAST: return cast_to_hir_data(context, ast);
        case AST_DEREFERENCE: return dereference_to_hir_data(context, ast);
        case AST_ACCESS: return ast_to_hir_data(context, ast->access.rhs);
        case AST_MEMBER: return member_to_hir_data(context, ast);
        case AST_CONSTANT: return ast_to_hir_data(context, ast->constant.value);
        case AST_NULL: return null_to_hir_data();
        case AST_BOOL: return bool_to_hir_data(ast);
        case AST_STRUCT_INITIALIZER: return struct_initializer_to_hir_data(context, ast);
        default: break;
    }

    log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col, 
        "No HIR data for AST type '%s'\n", ast_type_to_string(ast->type));
    return nodat;
}

static HIR root_to_hir(Context *context, AST *ast) {
    HIR hir = create_hir(HIR_BLOCK);
    hir.block = ast_list_to_block(context, &ast->root.nodes);
    return hir;
}

static HIR function_to_hir(Context *context, AST *ast) {
    Symbol *symbol = get_symbol(context, ast->uid);
    assert(symbol != NULL);

    HIR hir = create_hir(HIR_FUNCTION);
    hir.function.name = ast->function.name;
    hir.function.name_length = ast->function.name_length;
    hir.function.module_uid = ast->module_uid;
    hir.function.data_type = ast_data_type_to_hir_data_type(context, &ast->function.data_type, NULL, ast->module_uid);
    // ast->function.data_type;
    hir.function.exported = symbol->exported;
    hir.function.flags = symbol->flags;

    hir.function.parameters = malloc(ast->function.parameters.count * sizeof(HIR_Data));
    hir.function.parameter_count = ast->function.parameters.count;

    for (size_t i = 0; i < ast->function.parameters.count; i++)
        hir.function.parameters[i] = ast_to_hir_data(context, (AST *)ast->function.parameters.items[i]);

    HIR_Block block = ast_list_to_block(context, &ast->function.body);

    if (block.count == 0 || block.nodes[block.count - 1].type != HIR_RETURN) {
        HIR ret = create_hir(HIR_RETURN);
        ret.return_.value = nodat;
        ret.return_.symbol_uid = symbol->uid;
        push_node(&block, ret);
    }

    hir.function.block = block;
    return hir;
}

static HIR return_to_hir(Context *context, AST *ast) {
    const Symbol *symbol = get_symbol(context, ast->return_.symbol_uid);
    assert(symbol != NULL);

    HIR hir = create_hir(HIR_RETURN);
    hir.return_.data_type = *symbol->data_type;
    hir.return_.value = ast_to_hir_data(context, ast->return_.value);
    hir.return_.symbol_uid = symbol->uid;
    return hir;
}

static HIR declaration_to_hir(Context *context, AST *ast) {
    HIR hir = create_hir(HIR_DECLARATION);
    hir.declaration.uid = ast->uid;
    hir.declaration.name = ast->declaration.name;
    hir.declaration.name_length = ast->declaration.name_length;
    hir.declaration.data_type = ast_data_type_to_hir_data_type(context, &ast->declaration.data_type, ast->declaration.value, 
        ast->declaration.data_type.module_name != NULL ? (size_t)ast->declaration.data_type.module_uid : ast->module_uid);

    update_symbol_data_type(&ast->declaration.data_type, &hir.declaration.data_type);

    if (hir.declaration.data_type.pointer_count == 0 && hir.declaration.data_type.primitive_type == PRIM_VOID) {
        char *str = data_type_to_string(&hir.declaration.data_type);
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Illegal declaration type '%s'\n", str);
        free(str);
        ast->declaration.data_type = create_data_type(PRIM_INFER, 0);
    }

    hir.declaration.value = ast_to_hir_data(context, ast->declaration.value);

    if (ast->declaration.value != NULL)
        check_data_types_are_compatible(context, &ast->source, ast->module_uid, 
            hir.declaration.data_type, get_hir_data_type(context, &hir.declaration.value));

    hir.declaration.variable_uid = ast->declaration.variable_uid;
    return hir;
}

static HIR assignment_to_hir(Context *context, AST *ast) {
    HIR hir = create_hir(HIR_ASSIGNMENT);
    hir.assignment.lhs = ast_to_hir_data(context, ast->assignment.lhs);
    hir.assignment.rhs = ast_to_hir_data(context, ast->assignment.rhs);

    check_data_types_are_compatible(context, &ast->source, ast->module_uid,
        get_hir_data_type(context, &hir.assignment.lhs), get_hir_data_type(context, &hir.assignment.rhs));
    return hir;
}

static HIR asm_to_hir(AST *ast) {
    HIR hir = create_hir(HIR_ASM);
    hir.asm_.code = ast->asm_.value->literal.string;
    hir.asm_.code_length = ast->asm_.value->literal.string_length;
    return hir;
}

static HIR compound_math_to_hir(Context *context, AST *ast) {
    HIR hir = create_hir(HIR_ASSIGNMENT);
    hir.assignment.lhs = ast_to_hir_data(context, ast->compound_math.lhs);

    HIR_Data data = create_data(DATA_MATH);
    data.expression.data = malloc(3 * sizeof(HIR_Data));
    data.expression.count = 3;
    data.expression.data[0] = ast_to_hir_data(context, ast->compound_math.lhs);
    data.expression.data[1] = create_data(DATA_OPERATOR);
    data.expression.data[1].operator.type = OP_MATH;
    data.expression.data[1].operator.math = ast->compound_math.type;
    data.expression.data[2] = ast_to_hir_data(context, ast->compound_math.rhs);

    hir.assignment.rhs = data;

    check_data_types_are_compatible(context, &ast->source, ast->module_uid,
        get_hir_data_type(context, &hir.assignment.lhs), get_hir_data_type(context, &data));
    return hir;
}

static HIR ast_call_to_hir(Context *context, AST *ast) {
    HIR hir = create_hir(HIR_CALL);
    hir.call = call_to_hir(context, ast);
    return hir;
}

static HIR if_to_hir(Context *context, AST *ast) {
    HIR hir = create_hir(HIR_IF);
    hir.if_.condition = ast_to_hir_data(context, ast->if_.condition);
    hir.if_.block = ast_list_to_block(context, &ast->if_.body);
    hir.if_.else_block = ast_list_to_block(context, &ast->if_.else_body);
    return hir;
}

static HIR while_to_hir(Context *context, AST *ast) {
    HIR hir = create_hir(HIR_WHILE_LOOP);
    hir.while_loop.do_block_first = ast->while_.do_first;
    hir.while_loop.condition = ast_to_hir_data(context, ast->while_.condition);
    hir.while_loop.block = ast_list_to_block(context, &ast->while_.body);
    return hir;
}

static HIR keyword_statement_to_hir(AST *ast) {
    HIR hir = create_hir(HIR_KEYWORD_STMT);
    hir.keyword_statement = ast->keyword_statement;
    return hir;
}

static HIR for_to_hir(Context *context, AST *ast) {
    HIR hir = create_hir(HIR_FOR_LOOP);

    AST *lhs = ast->for_.lhs;
    HIR_Data iter;

    if (lhs->type == AST_DECLARATION) {
        iter = create_data(DATA_LOCAL_VARIABLE);
        iter.local_variable.name = lhs->declaration.name;
        iter.local_variable.name_length = lhs->declaration.name_length;

        if (lhs->declaration.data_type.primitive_type == PRIM_INFER) {
            assert(lhs->declaration.value != NULL);
            lhs->declaration.data_type = infer_implicit_data_type(get_ast_data_type(context, lhs->declaration.value));
        }

        iter.local_variable.data_type = lhs->declaration.data_type;
        iter.local_variable.uid = lhs->uid;
        iter.local_variable.variable_uid = lhs->declaration.variable_uid;
        hir.for_loop.iterator_initializer = ast_to_hir_data(context, lhs->declaration.value);
    } else {
        assert(lhs->type == AST_ASSIGNMENT);
        iter = ast_to_hir_data(context, lhs->assignment.lhs);
    }

    hir.for_loop.iterator = iter;

    HIR_Data_Expression expr = { .data = malloc(3 * sizeof(HIR_Data)), .count = 3 };
    expr.data[0] = iter;
    expr.data[2] = ast_to_hir_data(context, ast->for_.rhs);

    expr.data[1] = create_data(DATA_OPERATOR);
    expr.data[1].operator.type = OP_MATH;

    if (ast->for_.is_reverse)
        expr.data[1].operator.math = ast->for_.range_is_inclusive ? TOK_GTE : TOK_GT;
    else
        expr.data[1].operator.math = ast->for_.range_is_inclusive ? TOK_LTE : TOK_LT;

    hir.for_loop.condition = create_data(DATA_CONDITION);
    hir.for_loop.condition.expression = expr;

    if (ast->for_.step == NULL) {
        HIR_Data inc = create_data(DATA_LITERAL);
        inc.literal.data_type = create_data_type(PRIM_U32, 0);
        inc.literal.i32 = 1;
        hir.for_loop.iterator_increment = inc;
    } else
        hir.for_loop.iterator_increment = ast_to_hir_data(context, ast->for_.step);

    hir.for_loop.decrement_iterator = ast->for_.is_reverse;
    hir.for_loop.block = ast_list_to_block(context, &ast->for_.body);
    return hir;
}

static HIR import_to_hir(Context *context, AST *ast) {
    Module *module = get_module(context, ast->import.module_uid);
    module->using_all_symbols = ast->import.use_all_symbols;

    HIR hir = create_hir(HIR_BLOCK);
    hir.block.nodes = malloc(BLOCK_CAPACITY * sizeof(HIR));
    hir.block.count = 0;
    hir.block.capacity = BLOCK_CAPACITY;

    int index = 0;
    
    while (index != -1) {
        Symbol *sym = get_function_symbol_by_index_in_module(context, index, ast->import.module_uid, &index);

        if (index == -1)
            break;

        if (sym == NULL || !sym->exported) {
            index++;
            continue;
        }

        HIR ext = create_hir(HIR_EXTERN);
        ext.extern_.name = sym->name;
        ext.extern_.name_length = sym->name_length;
        ext.extern_.module_uid = sym->module_uid;
        push_node(&hir.block, ext);
        index++;
    }

    return hir;
}

HIR ast_to_hir(Context *context, AST *ast) {
    switch (ast->type) {
        // The following are done in the resolver.
        case AST_CUSTOM_TYPE:
        case AST_EXPORT:
        case AST_ALIAS:
        case AST_CONSTANT:
        case AST_GROUP_LIST:
        // -----
        case AST_NOP: return nop;
        case AST_ROOT: return root_to_hir(context, ast);
        case AST_FUNCTION: return function_to_hir(context, ast);
        case AST_RETURN: return return_to_hir(context, ast);
        case AST_DECLARATION: return declaration_to_hir(context, ast);
        case AST_ASSIGNMENT: return assignment_to_hir(context, ast);
        case AST_ASM: return asm_to_hir(ast);
        case AST_COMPOUND_MATH: return compound_math_to_hir(context, ast);
        case AST_CALL: return ast_call_to_hir(context, ast);
        case AST_IF: return if_to_hir(context, ast);
        case AST_WHILE: return while_to_hir(context, ast);
        case AST_KEYWORD_STMT: return keyword_statement_to_hir(ast);
        case AST_FOR: return for_to_hir(context, ast);
        case AST_ACCESS: return ast_to_hir(context, ast->access.rhs);
        case AST_IMPORT: return import_to_hir(context, ast);
        default: break;
    }

    log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col, 
        "No HIR for AST type '%s'\n", ast_type_to_string(ast->type));
    return nop;
}

void delete_hir(HIR *hir) {
    switch (hir->type) {
        case HIR_BLOCK:
            delete_block(&hir->block);
            break;
        case HIR_FUNCTION:
            for (size_t i = 0; i < hir->function.parameter_count; i++)
                delete_data(&hir->function.parameters[i]);

            delete_data_type(&hir->function.data_type);
            free(hir->function.parameters);
            delete_block(&hir->function.block);
            break;
        case HIR_DECLARATION:
            delete_data_type(&hir->declaration.data_type);
            delete_data(&hir->declaration.value);
            break;
        case HIR_ASSIGNMENT:
            delete_data(&hir->assignment.lhs);
            delete_data(&hir->assignment.rhs);
            break;
        case HIR_RETURN:
            delete_data(&hir->return_.value);
            break;
        case HIR_CALL:
            for (size_t i = 0; i < hir->call.argument_count; i++)
                delete_data(&hir->call.arguments[i]);

            free(hir->call.arguments);
            delete_data_type(&hir->call.data_type);
            break;
        case HIR_IF:
            delete_data(&hir->if_.condition);
            delete_block(&hir->if_.block);
            delete_block(&hir->if_.else_block);
            break;
        case HIR_WHILE_LOOP:
            delete_data(&hir->while_loop.condition);
            delete_block(&hir->while_loop.block);
            break;
        case HIR_FOR_LOOP:
            delete_data(&hir->for_loop.condition);
            delete_block(&hir->for_loop.block);
            delete_data(&hir->for_loop.iterator);
            delete_data(&hir->for_loop.iterator_initializer);
            delete_data(&hir->for_loop.iterator_increment);
            break;
        case HIR_REFERENCE:
            delete_data(hir->reference.value);
            break;
        default: break;
    }
}

char *hir_type_to_string(const HIR_Type type) {
    switch (type) {
        case HIR_NOP: return "nop";
        case HIR_BLOCK: return "block";
        case HIR_FUNCTION: return "function";
        case HIR_DECLARATION: return "declaration";
        case HIR_ASSIGNMENT: return "assignment";
        case HIR_ASM: return "inline assembly";
        case HIR_CALL: return "call";
        case HIR_RETURN: return "return";
        case HIR_IF: return "if";
        case HIR_WHILE_LOOP: return "while loop";
        case HIR_FOR_LOOP: return "for loop";
        case HIR_KEYWORD_STMT: return "keyword statement";
        case HIR_REFERENCE: return "reference";
        case HIR_EXTERN: return "extern";
        default:
            assert(false);
            return "<none>";
    }
}

char *hir_data_type_to_string(const HIR_Data_Type type) {
    switch (type) {
        case DATA_NONE: return "none";
        case DATA_LITERAL: return "literal";
        case DATA_LOCAL_VARIABLE: return "local variable";
        case DATA_MATH: return "math";
        case DATA_OPERATOR: return "operator";
        case DATA_CALL: return "call";
        case DATA_CONDITION: return "condition";
        case DATA_REFERENCE: return "pointer";
        case DATA_INDEX: return "index";
        case DATA_UNARY: return "unary";
        case DATA_CAST: return "cast";
        case DATA_DEREFERENCE: return "dereference";
        case DATA_STRUCT_MEMBER: return "struct member";
        case DATA_STRUCT_INITIALIZER: return "struct initializer";
        default:
            assert(false);
            return "<none>";
    }
}

Data_Type get_hir_data_type(Context *context, const HIR_Data *data) {
    switch (data->type) {
        case DATA_NONE: return NO_DATA_TYPE;
        case DATA_LITERAL: return data->literal.data_type;
        case DATA_LOCAL_VARIABLE: return data->local_variable.data_type;
        case DATA_OPERATOR:
            assert(data->operator.type == OP_SIZEOF);
            return create_data_type(PRIM_USIZE, 0);
        case DATA_MATH: return infer_hir_data_list_type(context, data->expression.data, data->expression.count);
        case DATA_CALL: return data->call.data_type;
        case DATA_CONDITION: return create_data_type(PRIM_BOOL, 0);
        case DATA_REFERENCE: {
            Data_Type dt = get_hir_data_type(context, data->reference.value);
            dt.pointer_count++;
            return dt;
        }
        case DATA_INDEX: {
            Data_Type dt = get_hir_data_type(context, data->index.base);
            assert(dt.pointer_count > 0 || dt.array_size > 0);

            if (dt.array_size > 0)
                dt.array_size = 0;
            else
                dt.pointer_count--;

            return dt;
        }
        case DATA_UNARY: return get_hir_data_type(context, data->unary.value);
        case DATA_CAST: return data->cast.data_type;
        case DATA_DEREFERENCE: {
            Data_Type dt = get_hir_data_type(context, data->dereference.value);
            assert(dt.pointer_count > 0);
            dt.pointer_count--;
            return dt;
        }
        case DATA_STRUCT_MEMBER: {
            Custom_Type_Member *member = get_custom_type_member(context, data->struct_member.custom_type_symbol_uid, data->struct_member.member_symbol_uid);
            assert(member != NULL);
            return member->data_type;
        }
        case DATA_STRUCT_INITIALIZER: return data->struct_initializer.data_type;
        default:
            assert(false);
            return NO_DATA_TYPE;
    }
}

Data_Type infer_hir_data_list_type(Context *context, const HIR_Data *data, const size_t count) {
    // Used for math expressions and conditions. Any pointer type found converts this into a USIZE.
    bool is_float = false;
    bool is_unsigned = false;
    bool is_64 = false;

    for (size_t i = 0; i < count; i++) {
        if (data[i].type == DATA_OPERATOR)
            continue;

        const Data_Type dt = get_hir_data_type(context, &data[i]);

        if (dt.pointer_count > 0)
            return create_data_type(PRIM_USIZE, 0);

        if (dt_is_float(dt)) {
            is_float = true;

            if (dt.primitive_type == PRIM_F64) {
                // f64 is the highest 'prioritizing' type.
                is_64 = true;
                break;
            }

            continue;
        }

        if (dt_is_unsigned(dt))
            is_unsigned = true;

        if (dt.primitive_type == PRIM_I64 || dt.primitive_type == PRIM_U64 || 
                dt.primitive_type == PRIM_USIZE || dt.primitive_type == PRIM_ISIZE)
            is_64 = true;
    }

    if (is_float)
        return create_data_type(is_64 ? PRIM_F64 : PRIM_F32, 0);
    else if (is_64)
        return create_data_type(is_unsigned ? PRIM_U64 : PRIM_I64, 0);

    return create_data_type(is_unsigned ? PRIM_U32 : PRIM_I32, 0);
}
