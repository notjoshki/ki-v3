#include "ast.h"
#include "scope.h"
#include "source.h"
#include "data_type.h"
#include "context.h"
#include "utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

AST *new_ast(AST_Type type, size_t ln, size_t col, Source *current_source, Scope *current_scope, size_t module_uid, size_t uid) {
    AST *ast = malloc(sizeof(AST));
    ast->type = type;
    ast->source = *current_source;
    ast->module_uid = module_uid;

    // The parser's current line and column are of the next token ahead,
    // so we want to hardcode with the ACTUAL current token.
    ast->source.ln = ln;
    ast->source.col = col;

    ast->scope = *current_scope;
    ast->uid = uid;
    return ast;
}

void delete_ast_list(List *list) {
    for (size_t i = 0; i < list->count; i++)
        delete_ast((AST *)list->items[i]);

    delete_list(list);
}

void delete_ast(AST *ast) {
    switch (ast->type) {
        case AST_ROOT:
            delete_ast_list(&ast->root.nodes);
            break;
        case AST_STRING:
            delete_data_type(&ast->literal.data_type);
            free(ast->literal.string);
            break;
        case AST_IDENTIFIER:
            free(ast->identifier.identifier);
            break;
        case AST_VARIABLE:
            free(ast->variable.name);
            break;
        case AST_PARAMETER:
            free(ast->parameter.name);
            delete_data_type(&ast->parameter.data_type);

            if (ast->parameter.default_value != NULL)
                delete_ast(ast->parameter.default_value);
            break;
        case AST_FUNCTION:
            free(ast->function.name);
            delete_data_type(&ast->function.data_type);
            delete_ast_list(&ast->function.parameters);
            delete_ast_list(&ast->function.body);
            delete_ast_list(&ast->function.decorators);
            break;
        case AST_RETURN:
            if (ast->return_.value != NULL)
                delete_ast(ast->return_.value);
            break;
        case AST_DECLARATION:
            free(ast->declaration.name);
            delete_data_type(&ast->declaration.data_type);

            if (ast->declaration.value != NULL)
                delete_ast(ast->declaration.value);
            break;
        case AST_ASSIGNMENT:
            delete_ast(ast->assignment.lhs);
            delete_ast(ast->assignment.rhs);
            break;
        case AST_MATH:
            delete_ast_list(&ast->math.nodes);
            break;
        case AST_ASM:
            delete_ast(ast->asm_.value);
            break;
        case AST_COMPOUND_MATH:
            delete_ast(ast->compound_math.lhs);
            delete_ast(ast->compound_math.rhs);
            break;
        case AST_CALL:
            free(ast->call.name);
            delete_ast_list(&ast->call.arguments);
            break;
        case AST_CONDITION:
            delete_ast_list(&ast->condition.nodes);
            break;
        case AST_IF:
            delete_ast(ast->if_.condition);
            delete_ast_list(&ast->if_.body);
            delete_ast_list(&ast->if_.else_body);
            break;
        case AST_WHILE:
            delete_ast(ast->while_.condition);
            delete_ast_list(&ast->while_.body);
            break;
        case AST_FOR:
            delete_ast(ast->for_.lhs);
            delete_ast(ast->for_.rhs);

            if (ast->for_.step != NULL)
                delete_ast(ast->for_.step);

            delete_ast_list(&ast->for_.body);
            break;
        case AST_REFERENCE:
            delete_ast(ast->reference.value);
            break;
        case AST_INDEX:
            delete_ast(ast->index.base);
            delete_ast(ast->index.index);
            break;
        case AST_EXPRESSION:
            delete_ast(ast->expression.value);
            break;
        case AST_SIZEOF:
            if (ast->sizeof_.value != NULL)
                delete_ast(ast->sizeof_.value);
            break;
        case AST_UNARY:
            delete_ast(ast->unary.value);
            break;
        case AST_CAST:
            delete_data_type(&ast->cast.data_type);
            delete_ast(ast->cast.value);
            break;
        case AST_DEREFERENCE:
            delete_ast(ast->dereference.value);
            break;
        case AST_ACCESS:
            delete_ast(ast->access.lhs);
            delete_ast(ast->access.rhs);
            break;
        case AST_ENUM_NAME:
            free(ast->enum_name.name);
            break;
        case AST_EXPORT:
            delete_ast_list(&ast->export.identifiers);
            break;
        case AST_IMPORT:
            free(ast->import.path);
            delete_ast_list(&ast->import.from_identifiers);
            delete_ast_list(&ast->import.from_groups);

            if (ast->import.as_name != NULL)
                free(ast->import.as_name);
            break;
        case AST_ALIAS:
            delete_ast(ast->alias.lhs);
            delete_ast(ast->alias.rhs);
            break;
        case AST_CONSTANT:
            free(ast->constant.name);

            if (ast->constant.is_definition)
                delete_ast(ast->constant.value);
            break;
        case AST_DECORATOR:
            free(ast->decorator.name);
            break;
        case AST_GROUP:
            free(ast->group.name);
            break;
        case AST_GROUP_LIST:
            free(ast->group_list.name);
            delete_ast_list(&ast->group_list.statements);
            break;
        case AST_ALIAS_LHS:
            free(ast->alias_lhs.name);
            break;
        case AST_COMMENT:
            free(ast->comment.comment);
            break;
        case AST_STRUCT_INITIALIZER:
            delete_ast_list(&ast->struct_initializer.values);
            delete_ast_list(&ast->struct_initializer.annotations);
            delete_data_type(&ast->struct_initializer.data_type);
            break;
        case AST_STRUCT_NAME:
            free(ast->struct_name.name);
            break;
        default: break;
    }

    free(ast);
}

char *ast_type_to_string(const AST_Type type) {
    switch (type) {
        case AST_NOP: return "nop";
        case AST_ERROR: return "error";
        case AST_ROOT: return "root";
        case AST_INT: return "int";
        case AST_FLOAT: return "float";
        case AST_STRING: return "string";
        case AST_IDENTIFIER: return "identifier";
        case AST_VARIABLE: return "variable";
        case AST_PARAMETER: return "parameter";
        case AST_FUNCTION: return "function";
        case AST_RETURN: return "return";
        case AST_DECLARATION: return "declaration";
        case AST_ASSIGNMENT: return "assignment";
        case AST_MATH: return "math";
        case AST_OPERATOR: return "operator";
        case AST_ASM: return "inline assembly";
        case AST_COMPOUND_MATH: return "compound math";
        case AST_CALL: return "call";
        case AST_CONDITION: return "condition";
        case AST_IF: return "if";
        case AST_WHILE: return "while";
        case AST_KEYWORD_STMT: return "keyword statement";
        case AST_FOR: return "for";
        case AST_REFERENCE: return "reference";
        case AST_INDEX: return "index";
        case AST_SIZEOF: return "sizeof";
        case AST_EXPRESSION: return "expression";
        case AST_UNARY: return "unary";
        case AST_CAST: return "cast";
        case AST_DEREFERENCE: return "dereference";
        case AST_ACCESS: return "access";
        case AST_ENUM_NAME: return "enum";
        case AST_MEMBER: return "member";
        case AST_EXPORT: return "export";
        case AST_IMPORT: return "import";
        case AST_MODULE_NAME: return "module";
        case AST_ALIAS: return "alias";
        case AST_CONSTANT: return "constant";
        case AST_DECORATOR: return "decorator";
        case AST_CUSTOM_TYPE: return "custom type declaration";
        case AST_GROUP: return "group";
        case AST_GROUP_LIST: return "group list";
        case AST_ALIAS_LHS: return "alias lhs";
        case AST_COMMENT: return "comment";
        case AST_NULL: return "null";
        case AST_BOOL: return "boolean";
        case AST_STRUCT_INITIALIZER: return "struct initializer";
        case AST_STRUCT_NAME: return "struct";
        default:
            assert(false);
            return "<none>";
    }
}

// ONLY USE TYPE FUNCTIONS AFTER RESOLVING!!!

Data_Type get_ast_data_type(Context *context, AST *ast) {
    switch (ast->type) {
        case AST_ERROR:
        case AST_NOP: return NO_DATA_TYPE;
        case AST_INT:
        case AST_FLOAT:
        case AST_STRING: return ast->literal.data_type;
        case AST_VARIABLE: {
            Symbol *symbol = get_symbol(context, ast->variable.symbol_uid);
            assert(symbol != NULL);
            return *symbol->data_type;
        }
        case AST_MATH: return infer_ast_list_data_type(context, &ast->math.nodes);
        case AST_CALL: {
            Symbol *symbol = get_symbol(context, ast->call.symbol_uid);
            assert(symbol != NULL);
            return *symbol->data_type;
        }
        case AST_CONDITION: return create_data_type(PRIM_BOOL, 0);
        case AST_REFERENCE: {
            Data_Type dt = get_ast_data_type(context, ast->reference.value);
            dt.pointer_count++;
            return dt;
        }
        case AST_INDEX: {
            Data_Type dt = get_ast_data_type(context, ast->index.base);
            assert(dt.pointer_count > 0 || dt.array_size > 0);

            if (dt.array_size > 0)
                dt.array_size = 0;
            else
                dt.pointer_count--;

            return dt;
        }
        case AST_SIZEOF: return create_data_type(PRIM_USIZE, 0);
        case AST_EXPRESSION: return get_ast_data_type(context, ast->expression.value);
        case AST_UNARY: return get_ast_data_type(context, ast->unary.value);
        case AST_CAST: return ast->cast.data_type;
        case AST_DEREFERENCE: {
            Data_Type dt = get_ast_data_type(context, ast->dereference.value);
            assert(dt.pointer_count > 0);
            dt.pointer_count--;
            return dt;
        }
        case AST_CONSTANT: return get_ast_data_type(context, ast->constant.value);
        case AST_ACCESS: return get_ast_data_type(context, ast->access.rhs);
        case AST_ENUM_NAME: {
            Data_Type dt = create_data_type(PRIM_CUSTOM, 0);
            dt.custom_name = ast->enum_name.name;
            dt.custom_length = ast->enum_name.length;
            return dt;
        }
        case AST_MEMBER: {
            Custom_Type_Member *member = get_custom_type_member(context, ast->member.custom_type_symbol_uid, ast->member.member_symbol_uid);
            return member->data_type;
        }
        case AST_NULL: return create_data_type(PRIM_VOID, 1);
        case AST_BOOL: return create_data_type(PRIM_BOOL, 0);
        case AST_STRUCT_INITIALIZER: return ast->struct_initializer.data_type;
        case AST_STRUCT_NAME: {
            Custom_Type *type = get_custom_type(context, ast->struct_name.symbol_uid);
            assert(type != NULL);
            Data_Type dt = create_data_type(PRIM_CUSTOM, 0);
            dt.custom_name = type->name;
            dt.custom_length = type->name_length;
            dt.module_uid = type->module_uid;
            return dt;
        }
        default:
            assert(false);
            return NO_DATA_TYPE;
    }
}

Data_Type infer_ast_list_data_type(Context *context, List *list) {
    bool is_float = false;
    bool is_unsigned = false;
    bool is_64 = false;
    size_t highest_ptr_count = 0;

    for (size_t i = 0; i < list->count; i++) {
        if (((AST *)list->items[i])->type == AST_OPERATOR)
            continue;

        const Data_Type dt = get_ast_data_type(context, (AST *)list->items[i]);

        if (dt.pointer_count > highest_ptr_count)
            highest_ptr_count = dt.pointer_count;

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
        return create_data_type(is_unsigned ? PRIM_U64 : PRIM_I64, highest_ptr_count);

    return create_data_type(is_unsigned ? PRIM_U32 : PRIM_I32, highest_ptr_count);
}
