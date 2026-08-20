#include "resolver.h"
#include "ast.h"
#include "logger.h"
#include "context.h"
#include "utilities.h"
#include "parser.h"
#include "decorators.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

static void replace_identifier_if_aliased(Context *context, char **identifier, size_t *length, size_t module_uid, Alias_Symbol symbol) {
    Alias *alias = find_alias(context, *identifier, *length, module_uid, false);

    if (alias == NULL)
        return;

    free(*identifier);
    *identifier = copy_string(alias->replacement, alias->replacement_length);
    *length = alias->replacement_length;

    if (symbol.kind == ALIAS_TYPE) {
        if (symbol.type == NULL)
            return;

        symbol.type->name = *identifier;
        symbol.type->name_length = *length;
        return;
    } else if (symbol.kind == ALIAS_MODULE) {
        // TOFIX: Why is this not happening anymore, and why is the compiler still working??
        /*
        assert(false);
        Module *module = find_module(context, *identifier, *length);
        assert(module != NULL);
        //free(module->name);
        //module->name = copy_string(alias->replacement, alias->replacement_length);
        //module->name_length = *length;
        */

        return;
    }

    if (symbol.data == NULL)
        return;

    symbol.data->name = *identifier;
    symbol.data->name_length = *length;
}

static void resolve_value(Context *context, AST **ast);

static void resolve_data_type_array_size(Context *context, Data_Type *data_type) {
    resolve_value(context, &data_type->unresolved_array_size);
    AST *size = data_type->unresolved_array_size;

    if (size->type != AST_INT && size->type != AST_CONSTANT) {
        log(ERROR_CRITICAL, size->source.path, size->source.ln, size->source.col,
            "Non-constant or non-integer array size of type '%s'\n", ast_type_to_string(size->type));
        data_type->array_size = 1;
    }
}

static void resolve_data_type(Context *context, Data_Type *data_type, const Source *source, size_t module_uid) {
    if (data_type->unresolved_array_size != NULL)
        resolve_data_type_array_size(context, data_type);

    if (data_type->primitive_type != PRIM_CUSTOM)
        return;

    if (data_type->module_name != NULL) {
        Module *module = find_module(context, data_type->module_name, data_type->module_length);
        replace_identifier_if_aliased(context, &data_type->module_name, &data_type->module_length, 
            module_uid, alias_symbol_module(module_uid));

        module = find_module(context, data_type->module_name, data_type->module_length);

        if (module == NULL) {
            log(ERROR_CRITICAL, source->path, source->ln, source->col,
                "Undefined module '%.*s'\n", (int)data_type->module_length, data_type->module_name);
            return;
        }

        data_type->module_uid = module->uid;
        module_uid = module->uid;
    }

    Alias *alias = find_alias(context, data_type->custom_name, data_type->custom_length, module_uid, false);

    if (alias != NULL)
        return;

    Custom_Type *type = find_custom_type(context, CUST_ENUM, data_type->custom_name, data_type->custom_length,
        data_type->module_uid != DATA_TYPE_IGNORE_MODULE ? (size_t)data_type->module_uid : module_uid);
    replace_identifier_if_aliased(context, &data_type->custom_name, &data_type->custom_length, 
        module_uid, alias_symbol_type(type));

    if (type != NULL)
        return;

    type = find_custom_type(context, CUST_STRUCT, data_type->custom_name, data_type->custom_length,
        data_type->module_uid != DATA_TYPE_IGNORE_MODULE ? (size_t)data_type->module_uid : module_uid);
    replace_identifier_if_aliased(context, &data_type->custom_name, &data_type->custom_length, 
        module_uid, alias_symbol_type(type));

    if (type != NULL)
        return;

    log(ERROR_CRITICAL, source->path, source->ln, source->col,
        "Undefined data type '%.*s'\n", (int)data_type->custom_length, data_type->custom_name);
}

static void resolve_enum_members(Context *context, Custom_Type *type) {
    Data_Type superior_type = create_data_type(PRIM_I32, 0);

    for (size_t i = 0; i < type->member_count; i++) {
        Custom_Type_Member *member = &type->members[i];

        if (member->value == NULL)
            continue;

        resolve_value(context, &member->value);
        superior_type = infer_between_two_types(superior_type, get_ast_data_type(context, member->value));
    }

    // Will become either i32, f32 or f64 depending on the enum values given, or none.
    superior_type = infer_implicit_data_type(superior_type);
    type->enum_data_type = superior_type;

    const bool is_float = dt_is_float(superior_type);

    double float_counter = 0;
    int32_t int_counter = 0;

    for (size_t i = 0; i < type->member_count; i++) {
        Custom_Type_Member *member = &type->members[i];
        member->data_type = superior_type;
        Custom_Type_Enum_Value value;

        if (member->value != NULL) {
            assert(member->value->type == AST_INT || member->value->type == AST_FLOAT);
            const AST *literal = member->value;

            if (is_float) {
                if (literal->literal.data_type.primitive_type == PRIM_F32)
                    float_counter = (double)literal->literal.f32;
                else if (literal->literal.data_type.primitive_type == PRIM_F64)
                    float_counter = literal->literal.f64;
                else if (literal->literal.data_type.primitive_type == PRIM_U32)
                    float_counter = (double)literal->literal.u32;
                else
                    float_counter = (double)literal->literal.i32;
            } else if (literal->literal.data_type.primitive_type == PRIM_U32)
                int_counter = (int32_t)literal->literal.u32;
            else
                int_counter = literal->literal.i32;
        }

        if (superior_type.primitive_type == PRIM_F32)
            value.f32 = (float)float_counter++;
        else if (superior_type.primitive_type == PRIM_F64)
            value.f64 = (float)float_counter++;
        else
            value.i32 = int_counter++;

        member->enum_value = value;
    }

    type->resolved = true;
}

static void resolve_struct_members(Context *context, Custom_Type *type) {
    for (size_t i = 0; i < type->member_count; i++) {
        Custom_Type_Member *member = &type->members[i];
        resolve_data_type(context, &member->data_type, &member->source, type->module_uid);
    }

    type->resolved = true;
}

static void resolve_access(Context *context, AST *ast);

static void resolve_ast_list(Context *context, List *list) {
    for (size_t i = 0; i < list->count; i++) {
        //if (((AST *)list->items[i])->type != AST_IMPORT) // Imports done in resolve_root.
            resolve_ast(context, (AST *)list->items[i]);
    }
}

static AST *resolve_identifier(Context *context, AST *ast) {
    if (ast->identifier.resolved)
        return ast;

    Constant *con = find_constant(context, ast->identifier.identifier, ast->identifier.length, ast->module_uid, true);

    if (con != NULL) {
        AST *c = new_ast(AST_CONSTANT, ast_location(ast), ast->uid);
        c->constant.is_definition = false;
        c->constant.name = ast->identifier.identifier;
        c->constant.name_length = ast->identifier.length;
        c->constant.value = con->value;

        ast->type = AST_NOP;
        delete_ast(ast);
        return c;
    }

    Symbol *sym = find_symbol(context, SYMBOL_VARIABLE, ast->identifier.identifier, ast->identifier.length, &ast->scope, ast->module_uid);
    replace_identifier_if_aliased(context, &ast->identifier.identifier, &ast->identifier.length, 
        ast->module_uid, alias_symbol_data(sym));

    if (sym != NULL) {
        AST *var = new_ast(AST_VARIABLE, ast_location(ast), ast->uid);
        var->variable.name = ast->identifier.identifier;
        var->variable.name_length = ast->identifier.length;
        var->variable.symbol_uid = sym->uid;

        ast->type = AST_NOP;
        delete_ast(ast);
        return var;
    }

    Custom_Type *type = find_custom_type(context, CUST_ENUM, ast->identifier.identifier, ast->identifier.length, ast->module_uid);
    replace_identifier_if_aliased(context, &ast->identifier.identifier, &ast->identifier.length,
        ast->module_uid, alias_symbol_type(type));

    if (type != NULL) {
        AST *name = new_ast(AST_ENUM_NAME, ast_location(ast), ast->uid);
        name->enum_name.name = ast->identifier.identifier;
        name->enum_name.length = ast->identifier.length;
        name->enum_name.symbol_uid = type->uid;

        ast->type = AST_NOP;
        delete_ast(ast);
        return name;
    }

    type = find_custom_type(context, CUST_STRUCT, ast->identifier.identifier, ast->identifier.length, ast->module_uid);
    replace_identifier_if_aliased(context, &ast->identifier.identifier, &ast->identifier.length,
        ast->module_uid, alias_symbol_type(type));

    if (type != NULL) {
        AST *name = new_ast(AST_STRUCT_NAME, ast_location(ast), ast->uid);
        name->struct_name.name = ast->identifier.identifier;
        name->struct_name.length = ast->identifier.length;
        name->struct_name.symbol_uid = type->uid;

        ast->type = AST_NOP;
        delete_ast(ast);
        return name;
    }

    Module *module = find_module(context, ast->identifier.identifier, ast->identifier.length);
    replace_identifier_if_aliased(context, &ast->identifier.identifier, &ast->identifier.length, 
        ast->module_uid, alias_symbol_module(module == NULL ? ast->module_uid : module->uid));

    if (module != NULL) {
        AST *name = new_ast(AST_MODULE_NAME, ast_location(ast), ast->uid);
        name->module_name.module_uid = module->uid;
        delete_ast(ast);
        return name;
    }

    log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
        "Undefined identifier '%.*s'\n", (int)ast->identifier.length, ast->identifier.identifier);

    ast->identifier.resolved = true;
    return ast;
}

static void resolve_parameter(Context *context, AST *ast) {
    ast->parameter.resolved = true;
    resolve_data_type(context, &ast->parameter.data_type, &ast->source, ast->module_uid);

    if (ast->parameter.default_value != NULL)
        resolve_value(context, &ast->parameter.default_value);
}

static void resolve_math(Context *context, AST *ast) {
    for (size_t i = 0; i < ast->math.nodes.count; i++) {
        if (i % 2 == 0) // Skip operators.
            resolve_value(context, (AST **)&ast->math.nodes.items[i]);
    }
}

static void resolve_condition(Context *context, AST *ast) {
    for (size_t i = 0; i < ast->condition.nodes.count; i++) {
        if (i % 2 == 0) // Skip operators.
            resolve_value(context, (AST **)&ast->condition.nodes.items[i]);
    }
}

static void resolve_index(Context *context, AST *ast) {
    resolve_value(context, &ast->index.base);
    resolve_value(context, &ast->index.index);

    AST *index = ast->index.index;

    if (index->type != AST_INT && index->type != AST_FLOAT)
        return;

    Data_Type dt = get_ast_data_type(context, ast->index.base);

    if (dt.array_size == 0)
        return;

    double index_value = 0;
    uint64_t u64_index_value = 0;

    if (index->type == AST_INT) {
        if (index->literal.data_type.primitive_type == PRIM_I32)
            index_value = (double)index->literal.i32;
        else if (index->literal.data_type.primitive_type == PRIM_I64)
            index_value = (double)index->literal.i64;
        else if (index->literal.data_type.primitive_type == PRIM_U32)
            index_value = (double)index->literal.u32;
        else {
            assert(index->literal.data_type.primitive_type == PRIM_U64);
            u64_index_value = index->literal.u64;
        }
    } else if (index->literal.data_type.primitive_type == PRIM_F32)
        index_value = (double)index->literal.f32;
    else
        index_value = (double)index->literal.f64;

    if (index->type == AST_FLOAT)
        log(ERROR_CRITICAL, index->source.path, index->source.ln, index->source.col,
            "Invalid indexing with float constant '%g'\n", index_value);

    if (index->literal.data_type.primitive_type != PRIM_U64) {
        if (index_value < 0)
            log(ERROR_CRITICAL, index->source.path, index->source.ln, index->source.col,
                "Invalid negative indexing with constant '%" PRId64 "'\n", (int64_t)index_value);
        else if ((size_t)index_value >= dt.array_size)
            log(ERROR_CRITICAL, index->source.path, index->source.ln, index->source.col,
                "Out-of-bounds indexing with constant '%" PRIu32 "' on array of size '%zu'\n", (uint32_t)index_value, dt.array_size);
        return;
    }

    assert(index->type == AST_INT);

    if ((size_t)u64_index_value >= dt.array_size)
        log(ERROR_CRITICAL, index->source.path, index->source.ln, index->source.col,
            "Out-of-bounds indexing with constant '%" PRIu64 "' on array of size '%zu'\n", u64_index_value, dt.array_size);
} 

static void resolve_enum_member_access(Context *context, AST *ast) {
    AST *lhs = ast->access.lhs;
    AST *rhs = ast->access.rhs;

    Custom_Type *type = find_custom_type(context, CUST_ENUM, lhs->enum_name.name, lhs->enum_name.length, lhs->module_uid);
    assert(type != NULL);
    assert(rhs->type == AST_IDENTIFIER);

    Custom_Type_Member *member = find_custom_type_member(context, type->uid, rhs->identifier.identifier, rhs->identifier.length);

    if (member == NULL) {
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "No such member '%.*s' in enum '%.*s'\n", (int)rhs->identifier.length, rhs->identifier.identifier,
            (int)type->name_length, type->name);
        return;
    }

    AST *new_rhs = new_ast(AST_MEMBER, ast_location(rhs), rhs->uid);
    new_rhs->member.custom_type_symbol_uid = type->uid;
    new_rhs->member.member_symbol_uid = member->uid;
    new_rhs->member.struct_access_lhs = NULL;

    delete_ast(rhs);
    ast->access.rhs = new_rhs;
}

static void resolve_module_access_rhs(Context *context, AST **rhs, size_t module_uid_prepended) {
    const Module *rhs_module = get_module(context, (*rhs)->module_uid);

    switch ((*rhs)->type) {
        case AST_CALL: {
            Symbol *sym = find_symbol(context, SYMBOL_FUNCTION, (*rhs)->call.name, (*rhs)->call.name_length, &(*rhs)->scope, module_uid_prepended);
            replace_identifier_if_aliased(context, &((*rhs)->call.name), &((*rhs)->call.name_length), 
                (*rhs)->module_uid, alias_symbol_data(sym));

            if (sym == NULL || !sym->exported)
                break;

            if (rhs_module->imported_identifiers.count > 0 && 
                    !found_identifier_in_imported_module_identifiers((*rhs)->call.name, (*rhs)->call.name_length, rhs_module))
                break;

            if (rhs_module->imported_groups.count > 0 && 
                    !found_group_in_imported_module_groups(sym->group_uid, rhs_module))
                break;

            (*rhs)->module_uid = module_uid_prepended;
            break;
        }
        case AST_ACCESS:
        case AST_IDENTIFIER:
            (*rhs)->module_uid = module_uid_prepended;
            break;
        default:
            assert(false);
            break;
    }

    resolve_value(context, rhs);
}

static void resolve_module_access(Context *context, AST *ast) {
    resolve_module_access_rhs(context, &ast->access.rhs, ast->access.lhs->module_name.module_uid);
    /*
    ast->access.rhs->module_uid = ast->access.lhs->module_name.module_uid;
    resolve_value(context, &ast->access.rhs);
    AST *rhs = ast->access.rhs;

    // TOFIX: Only allow access for enum names.
    if (rhs->type != AST_CALL && rhs->type != AST_ACCESS)
        log(ERROR_CRITICAL, rhs->source.path, rhs->source.ln, rhs->source.col,
            "Invalid module access value of type '%s'\n", ast_type_to_string(rhs->type));
    */
}

static void resolve_access(Context *context, AST *ast) {
    // TOFIX: Return if error.
    ast->access.lhs->module_uid = ast->module_uid; // In case a module name was prepended.
    resolve_value(context, &ast->access.lhs);

    if (ast->access.lhs->type == AST_ENUM_NAME) {
        resolve_enum_member_access(context, ast);
        return;
    } else if (ast->access.lhs->type == AST_MODULE_NAME) {
        resolve_module_access(context, ast);
        return;
    }

    AST *lhs = ast->access.lhs;
    AST *rhs = ast->access.rhs;
    Data_Type lhs_type = get_ast_data_type(context, lhs);

    if (lhs_type.primitive_type != PRIM_CUSTOM) {
        // INFER is always a result of a datatype error on the user side so we don't wanna log this error.
        if (lhs_type.primitive_type == PRIM_INFER)
            increment_error_count();
        else {
            char *str = data_type_to_string(&lhs_type);
            log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
                "Accessing non-struct data type of type '%s'\n", str);
            free(str);
        }

        return;
    }

    Custom_Type *type = find_custom_type(context, CUST_STRUCT, lhs_type.custom_name, lhs_type.custom_length, 
        lhs_type.module_uid != DATA_TYPE_IGNORE_MODULE ? (size_t)lhs_type.module_uid : lhs->module_uid);

    if (type == NULL) {
        char *str = data_type_to_string(&lhs_type);
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Accessing non-struct data type of type '%s'\n", str);
        free(str);
        return;
    }

    assert(rhs->type == AST_IDENTIFIER);

    Custom_Type_Member *member = find_custom_type_member(context, type->uid, rhs->identifier.identifier, rhs->identifier.length);

    if (member == NULL) {
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "No such member '%.*s' in struct '%.*s'\n", (int)rhs->identifier.length, rhs->identifier.identifier,
            (int)type->name_length, type->name);
        return;
    }

    AST *new_rhs = new_ast(AST_MEMBER, ast_location(rhs), rhs->uid);
    new_rhs->member.custom_type_symbol_uid = type->uid;
    new_rhs->member.member_symbol_uid = member->uid;
    new_rhs->member.struct_access_lhs = lhs;
    delete_ast(rhs);
    ast->access.rhs = new_rhs;
}

static void resolve_struct_initializer(Context *context, AST *ast) {
    resolve_data_type(context, &ast->struct_initializer.data_type, &ast->source, ast->module_uid);
    Custom_Type *type = find_custom_type(context, CUST_STRUCT, ast->struct_initializer.data_type.custom_name,
        ast->struct_initializer.data_type.custom_length, ast->struct_initializer.data_type.module_name != NULL ?
        (size_t)ast->struct_initializer.data_type.module_uid : ast->module_uid);

    for (size_t i = 0; i < ast->struct_initializer.values.count; i++) {
        AST *value = (AST *)ast->struct_initializer.values.items[i];
        resolve_value(context, &value);

        if (type == NULL)
            continue;

        const char *annot = ((AST *)ast->struct_initializer.annotations.items[i])->identifier.identifier;
        const size_t len = ((AST *)ast->struct_initializer.annotations.items[i])->identifier.length;
        Custom_Type_Member *member = find_custom_type_member(context, type->uid, annot, len);

        if (member == NULL)
            log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
                "No such field '%.*s' in struct '%.*s'\n", (int)len, annot, (int)type->name_length, type->name);
    }
}

static void resolve_value(Context *context, AST **ast) {
    // We need an AST** here because we may need to reassign its dereferenced value
    // after it gets resolved, e.g variables and enums.
    // Since we can't change the union, we may need to reassign it to a new_ast().

    if (ast == NULL || *ast == NULL)
        return;

    switch ((*ast)->type) {
        case AST_NOP:
        case AST_INT:
        case AST_FLOAT:
        case AST_VARIABLE:
        case AST_STRING:
        case AST_NULL:
        case AST_BOOL: break;
        case AST_IDENTIFIER:
            *ast = resolve_identifier(context, *ast);
            break;
        case AST_PARAMETER: 
            resolve_parameter(context, *ast);
            break;
        case AST_MATH:
            resolve_math(context, *ast);
            break;
        case AST_CALL:
            resolve_ast(context, *ast);
            break;
        case AST_CONDITION:
            resolve_condition(context, *ast);
            break;
        case AST_REFERENCE:
            resolve_value(context, &((*ast)->reference.value));
            break;
        case AST_INDEX:
            resolve_index(context, *ast);
            break;
        case AST_EXPRESSION:
            resolve_value(context, &((*ast)->expression.value));
            break;
        case AST_SIZEOF:
            resolve_value(context, &((*ast)->sizeof_.value));
            break;
        case AST_UNARY:
            resolve_value(context, &((*ast)->unary.value));
            break;
        case AST_CAST:
            resolve_value(context, &((*ast)->cast.value));
            break;
        case AST_DEREFERENCE:
            resolve_value(context, &((*ast)->dereference.value));
            break;
        case AST_ACCESS:
            resolve_access(context, *ast);
            break;
        case AST_STRUCT_INITIALIZER:
            resolve_struct_initializer(context, *ast);
            break;
        default:
            log(ERROR_CRITICAL, (*ast)->source.path, (*ast)->source.ln, (*ast)->source.col,
                "Invalid value '%s'\n", ast_type_to_string((*ast)->type));
            break;
    }
}

static void resolve_decorator(AST *ast, Symbol *symbol) {
    if (compare_string(ast->decorator.name, ast->declaration.name_length, "ignore_missing_return", 21))
        symbol->flags |= DECOR_IGNORE_MISSING_RETURN;
    else if (compare_string(ast->decorator.name, ast->declaration.name_length, "onetime_arguments", 17))
        symbol->flags |= DECOR_ONETIME_ARGUMENTS;
    else if (compare_string(ast->decorator.name, ast->declaration.name_length, "omit_frame_pointer", 18))
        symbol->flags |= DECOR_OMIT_FRAME_POINTER;
    else if (compare_string(ast->decorator.name, ast->declaration.name_length, "extern", 6))
        symbol->flags |= DECOR_EXTERN_FUNCTION;
    else
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Undefined decorator '%.*s'\n", (int)ast->decorator.name_length, ast->decorator.name);
}

static void resolve_function(Context *context, AST *ast) {
    Scope *function_inner_scope = NULL;

    if (ast->function.body.count > 0)
        function_inner_scope = &((AST *)ast->function.body.items[0])->scope;
    else if (ast->function.parameters.count > 0)
        function_inner_scope = &((AST *)ast->function.parameters.items[0])->scope;

    if (function_inner_scope != NULL)
        push_scope(context, function_inner_scope);

    Symbol *sym = find_symbol(context, SYMBOL_FUNCTION, ast->function.name, ast->function.name_length, &ast->scope, ast->module_uid);
    replace_identifier_if_aliased(context, &ast->function.name, &ast->function.name_length, 
        ast->module_uid, alias_symbol_data(sym));
    assert(sym != NULL);

    if (sym->uid != ast->uid) {
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Redefinition of function '%.*s'\n", (int)ast->function.name_length, ast->function.name);

        log(ERROR_INFO, sym->source->path, sym->source->ln, sym->source->col,
            "...function '%.*s' defined here\n", (int)sym->name_length, sym->name);
    } else if (context->entrypoint_function != NULL && 
            compare_string(ast->function.name, ast->function.name_length, context->entrypoint_function, context->entrypoint_function_length))
        sym->exported = true;

    ast->function.resolved = true;
    resolve_data_type(context, &ast->function.data_type, &ast->source, ast->module_uid);

    for (size_t i = 0; i < ast->function.parameters.count; i++) {
        AST *param = ast->function.parameters.items[i];

        if (param->type != AST_PARAMETER) {
            log(ERROR_CRITICAL, param->source.path, param->source.ln, param->source.col,
                "Expected parameter definition but found '%s'\n", ast_type_to_string(param->type));
            resolve_ast(context, param);
            continue;
        }
        
        if (param->parameter.resolved)
            log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
                "Redefinition of parameter '%.*s'\n", (int)ast->parameter.name_length, ast->parameter.name);

        resolve_parameter(context, param);
    }

    resolve_ast_list(context, &ast->function.body);

    for (size_t i = 0; i < ast->function.decorators.count; i++)
        resolve_decorator(((AST **)ast->function.decorators.items)[i], sym);

    AST *return_stmt = NULL;

    for (size_t i = 0; i < ast->function.body.count; i++) {
        if (((AST *)ast->function.body.items[i])->type == AST_RETURN)
            return_stmt = ((AST *)ast->function.body.items[i]);
    }

    if ((ast->function.data_type.primitive_type != PRIM_VOID || ast->function.data_type.pointer_count > 0) && 
            return_stmt == NULL && !(sym->flags & DECOR_IGNORE_MISSING_RETURN)) {
        char *str = data_type_to_string(&ast->function.data_type);
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Missing return statement in function '%.*s' of type '%s'\n", (int)ast->function.name_length, ast->function.name, str);
        free(str);
    }

    if (function_inner_scope != NULL)
        pop_scope(context);

    bool has_extern_decorator = false;

    for (size_t i = 0; i < ast->function.decorators.count; i++) {
        AST *decorator = (AST *)ast->function.decorators.items[i];

        if (compare_string(decorator->decorator.name, decorator->decorator.name_length, "extern", 6)) {
            has_extern_decorator = true;
            break;
        }
    }

    if (ast->function.no_body && !has_extern_decorator)
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Function '%.*s' not declared with a body but not decorated as extern\n", (int)ast->function.name_length, ast->function.name);
    else if (!ast->function.no_body && has_extern_decorator)
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Function '%.*s' declared with a body but also decorated as extern\n", (int)ast->function.name_length, ast->function.name);

}

static void resolve_declaration(Context *context, AST *ast) {
    Symbol *sym = find_symbol(context, SYMBOL_VARIABLE, ast->declaration.name, ast->declaration.name_length, 
        &ast->scope, ast->module_uid);

    replace_identifier_if_aliased(context, &ast->declaration.name, &ast->declaration.name_length, 
        ast->module_uid, alias_symbol_data(sym));

    assert(sym != NULL);

    if (sym->uid != ast->uid) {
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Redefinition of variable '%.*s'; first defined at %s:%zu,%zu\n", (int)ast->declaration.name_length, ast->declaration.name,
            sym->source->path, sym->source->ln, sym->source->col);
    }

    ast->declaration.resolved = true;
    resolve_data_type(context, &ast->declaration.data_type, &ast->source, ast->module_uid);

    if (ast->declaration.value == NULL)
        return;
        
    resolve_value(context, &ast->declaration.value);

    if (ast->declaration.data_type.primitive_type == PRIM_INFER && 
        // If there's been an error it could mean a symbol is missing or something, get_ast_data_type() can then segfault.
            get_error_count() == 0) {
        Data_Type dt = get_ast_data_type(context, ast->declaration.value);
        ast->declaration.data_type = copy_data_type(&dt);
    }
}

static void resolve_assignment(Context *context, AST *ast) {
    resolve_value(context, &ast->assignment.lhs);
    resolve_value(context, &ast->assignment.rhs);
}

static void resolve_asm(Context *context, AST *ast) {
    if (ast->asm_.value->type != AST_STRING)
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Expected string for inline assembly but found '%s'\n", ast_type_to_string(ast->asm_.value->type));

    resolve_value(context, &ast->asm_.value);
}

static void resolve_compound_math(Context *context, AST *ast) {
    resolve_value(context, &ast->compound_math.lhs);
    resolve_value(context, &ast->compound_math.rhs);
}

static void resolve_call_arguments(Context *context, AST *ast) {
    for (size_t i = 0; i < ast->call.arguments.count; i++)
        resolve_value(context, (AST **)&ast->call.arguments.items[i]);
}

static void resolve_call(Context *context, AST *ast) {
    Symbol *sym = find_symbol(context, SYMBOL_FUNCTION, ast->call.name, ast->call.name_length, &ast->scope, ast->module_uid);
    replace_identifier_if_aliased(context, &ast->call.name, &ast->call.name_length, 
        ast->module_uid, alias_symbol_data(sym));

    if (sym == NULL) {
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Undefined function '%.*s'\n", (int)ast->call.name_length, ast->call.name);
        resolve_call_arguments(context, ast);
        return;
    }

    ast->call.symbol_uid = sym->uid;
    size_t required = 0;

    for (size_t i = 0; i < sym->attribute.parameters->count; i++) {
        if (((AST *)sym->attribute.parameters->items[i])->parameter.default_value == NULL)
            required++;
    }

    if (required > ast->call.arguments.count) {
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Missing arguments in call to function '%.*s'; expected %zu but found %zu\n", (int)ast->call.name_length, ast->call.name,
                required, ast->call.arguments.count);

        log(ERROR_INFO, sym->source->path, sym->source->ln, sym->source->col,
            "...function '%.*s' defined here\n", (int)sym->name_length, sym->name);
    } else if (sym->attribute.parameters->count < ast->call.arguments.count) {
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Excessive arguments in call to function '%.*s'; expected %zu but found %zu\n", (int)ast->call.name_length, ast->call.name,
                required, ast->call.arguments.count);

        log(ERROR_INFO, sym->source->path, sym->source->ln, sym->source->col,
            "...function '%.*s' defined here\n", (int)sym->name_length, sym->name);
    }

    resolve_call_arguments(context, ast);
}

static void resolve_return(Context *context, AST *ast) {
    Symbol *sym = get_symbol(context, ast->scope.function_uid);
    ast->return_.symbol_uid = ast->scope.function_uid;

    if (sym == NULL)
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Return statement outside of a function\n");

    if (ast->return_.value != NULL)
        resolve_value(context, &ast->return_.value);
}

static void resolve_if(Context *context, AST *ast) {
    resolve_value(context, &ast->if_.condition);

    if (ast->if_.body.count > 0) {
        AST *first = ast->if_.body.items[0];
        push_scope(context, &first->scope);
        resolve_ast_list(context, &ast->if_.body);
        pop_scope(context);
    }

    if (ast->if_.else_body.count > 0) {
        AST *first = ast->if_.else_body.items[0];
        push_scope(context, &first->scope);
        resolve_ast_list(context, &ast->if_.else_body);
        pop_scope(context);
    }
}

static void resolve_while(Context *context, AST *ast) {
    resolve_value(context, &ast->while_.condition);
    push_scope(context, &ast->scope);
    resolve_ast_list(context, &ast->while_.body);
    pop_scope(context);
}

static void resolve_for(Context *context, AST *ast) {
    const AST *lhs = ast->for_.lhs;

    if (lhs->type != AST_DECLARATION && lhs->type != AST_ASSIGNMENT)
        log(ERROR_CRITICAL, lhs->source.path, lhs->source.ln, lhs->source.col,
            "Expected declaration or assignment for iterator variable but found '%s'\n", ast_type_to_string(lhs->type));

    push_scope(context, &ast->scope);
    resolve_ast(context, ast->for_.lhs);
    resolve_value(context, &ast->for_.rhs);

    if (ast->for_.step != NULL)
        resolve_value(context, &ast->for_.step);

    resolve_ast_list(context, &ast->for_.body);
    pop_scope(context);
}

static void resolve_custom_types(Context *context) {
    for (size_t i = 0; i < context->custom_type_count; i++) {
        Custom_Type *type = &context->custom_types[i];

        if (context->custom_types[i].type == CUST_ENUM)
            resolve_enum_members(context, type);
        else
            resolve_struct_members(context, type);
    }
}

static void export_identifier(Context *context, AST *identifier) {
    Alias *alias = find_alias(context, identifier->identifier.identifier, identifier->identifier.length, identifier->module_uid, true);

    if (alias != NULL) {
        //free(identifier->identifier.identifier);
        //identifier->identifier.identifier = copy_string(alias->replacement, alias->replacement_length);
        //identifier->identifier.length = alias->replacement_length;
        alias->exported = true;
        return;
    }

    Constant *con = find_constant(context, identifier->identifier.identifier, identifier->identifier.length, identifier->module_uid, true);

    if (con != NULL) {
        con->exported = true;
        return;
    }

    Symbol *func = find_symbol(context, SYMBOL_FUNCTION, identifier->identifier.identifier, identifier->identifier.length, FIND_IN_ANY_SCOPE, identifier->module_uid);
    replace_identifier_if_aliased(context, &identifier->identifier.identifier, &identifier->identifier.length, 
        identifier->module_uid, alias_symbol_data(func));

    if (func != NULL) {
        func->exported = true;
        return;
    }

    Custom_Type *type = find_custom_type(context, CUST_STRUCT, identifier->identifier.identifier, identifier->identifier.length, identifier->module_uid);
    replace_identifier_if_aliased(context, &identifier->identifier.identifier, &identifier->identifier.length, 
        identifier->module_uid, alias_symbol_type(type));

    if (type == NULL)
        type = find_custom_type(context, CUST_ENUM, identifier->identifier.identifier, identifier->identifier.length, identifier->module_uid);

    if (type != NULL) {
        type->exported = true;
        return;
    }

    const Module *module = get_module(context, identifier->module_uid);
    
    log(ERROR_CRITICAL, identifier->source.path, identifier->source.ln, identifier->source.col,
        "No such function or data type '%.*s' found in module '%.*s'\n", 
        (int)identifier->identifier.length, identifier->identifier.identifier,
        (int)module->name_length, module->name);
}

static void export_group(Context *context, AST *group) {
    AST *group_decl = find_group(context, group->group.name, group->group.name_length, group->module_uid);

    if (group_decl == NULL) {
        log(ERROR_CRITICAL, group->source.path, group->source.ln, group->source.col,
            "No such group '%.*s' to export\n", (int)group->group.name_length, group->group.name);
        return;
    }

    const size_t uid = group_decl->group.group_uid;
    int symbol_index = 0;
    int custom_type_index = 0;
    int alias_index = 0;

    while (symbol_index != -1 || custom_type_index != -1 || alias_index != -1) {
        // Find every function, custom type and alias in this group and export it.

        Symbol *func = get_function_symbol_by_index_in_module(context, symbol_index, group->module_uid, &symbol_index);
        replace_identifier_if_aliased(context, &group->group.name, &group->group.name_length, 
            group->module_uid, alias_symbol_data(func));

        if (func != NULL && func->group_uid == uid) 
            func->exported = true;

        if (symbol_index != -1)
            symbol_index++;

        Custom_Type *type = get_custom_type_by_index_in_module(context, custom_type_index, group->module_uid, &custom_type_index);
        replace_identifier_if_aliased(context, &group->group.name, &group->group.name_length, 
            group->module_uid, alias_symbol_type(type));

        if (type != NULL && type->group_uid == uid)
            type->exported = true;

        if (custom_type_index != -1)
            custom_type_index++;

        Alias *alias = get_alias_by_index_in_module(context, alias_index, group->module_uid, &alias_index);

        if (alias != NULL && alias->group_uid == uid)
            alias->exported = true;

        if (alias_index != -1)
            alias_index++;
    }
}

static void resolve_export(Context *context, AST *ast) {
    for (size_t i = 0; i < ast->export.identifiers.count; i++) {
        AST *item = (AST *)ast->export.identifiers.items[i];

        if (item->type == AST_IDENTIFIER)
            export_identifier(context, item);
        else {
            assert(item->type == AST_GROUP);
            export_group(context, item);
        }
    }
}

static void import_identifier(Context *context, AST *identifier, Module *from_module, Module *into_module) {
    Alias *alias = find_alias(context, identifier->identifier.identifier, identifier->identifier.length, from_module->uid, false);

    if (alias != NULL && alias->module_uid == from_module->uid) {
        push_item(&into_module->imported_identifiers, (AST *)identifier);
        return;
    }

    Constant *con = find_constant(context, identifier->identifier.identifier, identifier->identifier.length, from_module->uid, true);

    if (con != NULL && con->module_uid == from_module->uid) {
        push_item(&into_module->imported_identifiers, (AST *)identifier);
        return;
    }

    Symbol *func = find_symbol(context, SYMBOL_FUNCTION, identifier->identifier.identifier, identifier->identifier.length, FIND_IN_ANY_SCOPE, from_module->uid);
    replace_identifier_if_aliased(context, &identifier->identifier.identifier, &identifier->identifier.length, from_module->uid,
        alias_symbol_data(func));

    if (func != NULL && func->module_uid == from_module->uid) {
        push_item(&into_module->imported_identifiers, (AST *)identifier);
        return;
    }

    Custom_Type *type = find_custom_type(context, CUST_STRUCT, identifier->identifier.identifier, identifier->identifier.length, from_module->uid);
    replace_identifier_if_aliased(context, &identifier->identifier.identifier, &identifier->identifier.length, from_module->uid,
        alias_symbol_type(type));

    if (type == NULL)
        type = find_custom_type(context, CUST_ENUM, identifier->identifier.identifier, identifier->identifier.length, from_module->uid);

    if (type != NULL && type->module_uid == from_module->uid) {
        push_item(&into_module->imported_identifiers, (AST *)identifier);
        return;
    }

    log(ERROR_CRITICAL, identifier->source.path, identifier->source.ln, identifier->source.col,
        "No such skdfs function or data type '%.*s' found in module '%.*s'\n", 
        (int)identifier->identifier.length, identifier->identifier.identifier,
        (int)from_module->name_length, from_module->name);
}

static void import_group(Context *context, AST *group, Module *from_module, Module *into_module) {
    AST *group_decl = find_group(context, group->group.name, group->group.name_length, from_module->uid);

    if (group_decl == NULL) {
        log(ERROR_CRITICAL, group->source.path, group->source.ln, group->source.col,
            "No such group '%.*s' in module '%.*s' to import\n", (int)group->group.name_length, group->group.name,
            (int)from_module->name_length, from_module->name);
        return;
    }

    push_item(&into_module->imported_groups, (AST *)group_decl);
}

static void resolve_import(Context *context, AST *ast) {
    // See if the file is in the same directory relative to the current file it was imported from.
    Module *import_module = get_module(context, ast->module_uid);
    assert(import_module != NULL);
    char *correct_path = NULL;
    size_t correct_path_len;
    FILE *check = NULL;

    // Check for a builtin library.
    if (ast->import.path_length >= 3 && strncmp(ast->import.path, "ki/", 3) == 0) {
        correct_path = malloc(ast->import.path_length + strlen(KI_LIB_DIRECTORY) + 2);
        sprintf(correct_path, "%s/%.*s", KI_LIB_DIRECTORY, (int)ast->import.path_length - 3, ast->import.path + 3);

        correct_path_len = strlen(correct_path);
        check = fopen(correct_path, "r");
    }

    // Not a specified 'with ki.' builtin lib.
    if (check == NULL) {
        if (correct_path != NULL)
            free(correct_path);

        correct_path = malloc(ast->import.path_length + import_module->directory_length + 1);
        sprintf(correct_path, "%.*s%.*s", (int)import_module->directory_length, import_module->directory,
            (int)ast->import.path_length, ast->import.path);

        correct_path_len = strlen(correct_path);

        // Check if this ki file exists.
        check = fopen(correct_path, "r");

        if (check == NULL) {
            // It isn't in the same directory, try finding it in the current working directory.
            free(correct_path);
            correct_path = copy_string(ast->import.path, ast->import.path_length);
            correct_path_len = ast->import.path_length;
            check = fopen(correct_path, "r");

            if (check == NULL) {
                log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col, 
                    "Import path '%.*s' does not exist\n", (int)correct_path_len, correct_path);
                free(correct_path);
                return;
            }
        }
    }

    fclose(check);

    char *directory;
    size_t directory_len;
    char *module_name = parse_module_path_utility(correct_path, correct_path_len, &directory, &directory_len, true);

    Module *module = find_module(context, module_name, strlen(module_name));

    if (module == NULL) {
        const size_t errors_before = get_error_count();
        Parser *parser;
        AST *root = initialize_root(context, module_name, strlen(module_name), correct_path, correct_path_len, directory, directory_len, false, &parser);
        parse_root(root, parser);

        if (get_error_count() == errors_before)
            resolve_ast(context, root);

        Module *root_module = get_module(context, root->module_uid);
        root_module->root = root;

        root_module->builtin = context->freestanding ? false : 
            correct_path_len > 0 && correct_path[0] == '/' && strstr(correct_path, KI_LIB_DIRECTORY) != NULL;

        root_module->using_all_symbols = ast->import.use_all_symbols; // TOFIX: I really don't like this. It's ugly.
        module = root_module;
    } else if (module_is_imported(context, module->uid)) {
        module->using_all_symbols = ast->import.use_all_symbols;
        /*
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Reimporting module '%.*s'\n", (int)module->name_length, module->name);
        */
    }

    free(correct_path);
    free(module_name);
    free(directory);

    ast->import.module_uid = module->uid;

    for (size_t i = 0; i < ast->import.from_identifiers.count; i++)
        import_identifier(context, (AST *)ast->import.from_identifiers.items[i], module, import_module);

    for (size_t i = 0; i < ast->import.from_groups.count; i++)
        import_group(context, (AST *)ast->import.from_groups.items[i], module, import_module);

    if (ast->import.as_name == NULL)
        return;

    push_alias(context, &ast->source, import_module->uid, ast->import.as_name, ast->import.as_name_length,
        module->name, module->name_length, NO_SECTION);
}

static void resolve_alias(Context *context, AST *ast) {
    AST *lhs = ast->alias.lhs;
    AST *rhs = ast->alias.rhs;

    if (lhs->type != AST_ALIAS_LHS)
        log(ERROR_CRITICAL, lhs->source.path, lhs->source.ln, lhs->source.col,
            "Expected alias identifier for alias LHS value but found '%s'\n", ast_type_to_string(lhs->type));

    if (rhs->type != AST_IDENTIFIER)
        log(ERROR_CRITICAL, rhs->source.path, rhs->source.ln, rhs->source.col,
            "Expected identifier for alias RHS value but found '%s'\n", ast_type_to_string(rhs->type));

    if (lhs->type != AST_ALIAS_LHS || rhs->type != AST_IDENTIFIER)
        return;

    push_alias(context, &lhs->source, lhs->module_uid, lhs->identifier.identifier, lhs->identifier.length,
        rhs->identifier.identifier, rhs->identifier.length, lhs->alias_lhs.group_uid);
}

static void resolve_root(Context *context, AST *ast) {
    // Resolve aliases first.
    for (size_t i = 0; i < ast->root.nodes.count; i++) {
        //if (((AST *)ast->root.nodes.items[i])->type == AST_IMPORT)
          //  resolve_import(context, (AST *)ast->root.nodes.items[i]);
        //else
        if (((AST *)ast->root.nodes.items[i])->type == AST_ALIAS)
            resolve_alias(context, (AST *)ast->root.nodes.items[i]);
    }

    resolve_custom_types(context);
    resolve_ast_list(context, &ast->root.nodes);
}

static void resolve_constant(Context *context, AST *ast) {
    if (ast->constant.is_definition)
        resolve_value(context, &ast->constant.value);

    Constant *con = find_constant(context, ast->constant.name, ast->constant.name_length, ast->module_uid, true);

    if (con != NULL)
        log(ERROR_CRITICAL, ast->source.path, ast->source.ln, ast->source.col,
            "Redefinition of constant '%.*s'; first defined at %s:%zu:%zu\n", (int)ast->constant.name_length, ast->constant.name,
            con->source->path, con->source->ln, con->source->col);
    else
        push_constant(context, &ast->source, ast->module_uid, ast->constant.name, ast->constant.name_length,
            ast->constant.value, ast->constant.group_uid);
}

static void resolve_group_list(Context *context, AST *ast) {
    for (size_t i = 0; i < ast->group_list.statements.count; i++) {
        AST *stmt = ast->group_list.statements.items[i];

        if (stmt->type == AST_CONSTANT)
            stmt->constant.group_uid = ast->group_list.group_uid;
        else if (stmt->type == AST_ALIAS)
            stmt->alias.lhs->alias_lhs.group_uid = ast->group_list.group_uid;
        else
            log(ERROR_CRITICAL, stmt->source.path, stmt->source.ln, stmt->source.col,
                "Invalid statement '%s' in group list '%.*s'; only constants and aliases are allowed\n",
                ast_type_to_string(stmt->type), (int)ast->group_list.name_length, ast->group_list.name);

        resolve_ast(context, stmt);
    }
}

void resolve_ast(Context *context, AST *ast) {
    switch (ast->type) {
        case AST_ROOT:
            resolve_root(context, ast);
            break;
        case AST_FUNCTION:
            resolve_function(context, ast);
            break;
        case AST_DECLARATION:
            resolve_declaration(context, ast);
            break;
        case AST_ASSIGNMENT:
            resolve_assignment(context, ast);
            break;
        case AST_ASM:
            resolve_asm(context, ast);
            break;
        case AST_COMPOUND_MATH:
            resolve_compound_math(context, ast);
            break;
        case AST_CALL:
            resolve_call(context, ast);
            break;
        case AST_RETURN:
            resolve_return(context, ast);
            break;
        case AST_IF:
            resolve_if(context, ast);
            break;
        case AST_WHILE:
            resolve_while(context, ast);
            break;
        case AST_FOR:
            resolve_for(context, ast);
            break;
        case AST_ACCESS:
            resolve_access(context, ast);
            break;
        case AST_EXPORT:
            resolve_export(context, ast);
            break;
        case AST_CONSTANT:
            resolve_constant(context, ast);
            break;
        case AST_GROUP_LIST:
            resolve_group_list(context, ast);
            break;
        case AST_IMPORT:
            resolve_import(context, ast);
            break;
        default: break;
    }
}
