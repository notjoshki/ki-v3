#include "documentor.h"
#include "ast.h"
#include "parser.h"
#include "context.h"
#include "utilities.h"
#include "string_builder.h"
#include "list.h"
#include "element.h"
#include "resolver.h"
#include "logger.h"
#include "element.h"
#include "data_type.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include <ctype.h>

#define ELEMENT_CAPACITY 4

#define SECTION_BUILDER_CAPACITY 4

typedef struct {
    Context *context;
    Element *elements;
    size_t element_count;
    size_t element_capacity;
} Documentor;

static Documentor create_documentor(Context *context) {
    return (Documentor){ .context = context, .elements = malloc(ELEMENT_CAPACITY * sizeof(Element)),
        .element_count = 0, .element_capacity = ELEMENT_CAPACITY };
}

static void delete_documentor(Documentor *documentor) {
    for (size_t i = 0; i < documentor->element_count; i++)
        delete_element(&documentor->elements[i]);

    free(documentor->elements);
}

static void push_element(Documentor *documentor, Element element) {
    if (documentor->element_count + 1 > documentor->element_capacity) {
        documentor->element_capacity *= 2;
        documentor->elements = realloc(documentor->elements, documentor->element_capacity * sizeof(Element));
    }

    documentor->elements[documentor->element_count++] = element;
}

static Element_Type ast_to_element_type(Documentor *documentor, AST *ast) {
    switch (ast->type) {
        case AST_FUNCTION: return ELEM_FUNCTION;
        case AST_CUSTOM_TYPE: {
            Custom_Type *type = find_custom_type(documentor->context, CUST_STRUCT, 
                ast->custom_type.name, ast->custom_type.name_length, ast->module_uid);
            return type != NULL ? ELEM_STRUCT : ELEM_ENUM;
        }
        case AST_ALIAS: return ELEM_ALIAS;
        default:
            assert(false);
            return ELEM_NONE;
    }
}

static size_t ast_to_group_uid(Documentor *documentor, AST *ast) {
    switch (ast->type) {
        case AST_FUNCTION: return ast->function.group_uid;
        case AST_CUSTOM_TYPE: {
            Custom_Type *type = find_custom_type(documentor->context, CUST_STRUCT, 
                ast->custom_type.name, ast->custom_type.name_length, ast->module_uid);

            if (type == NULL)
                type = find_custom_type(documentor->context, CUST_ENUM, 
                    ast->custom_type.name, ast->custom_type.name_length, ast->module_uid);

            assert(type != NULL);
            return type->group_uid;
        }
        case AST_ALIAS: return ast->alias.lhs->alias_lhs.group_uid;
        default:
            assert(false);
            return ELEM_NONE;
    }
}

static inline void reset_comments(List *comments) {
    comments->count = 0;
}

static void document_elements(Documentor *documentor, AST *root, const bool exported_symbols_only) {
    // Sometimes functions or data types have comments above them to explain stuff.
    // We want this to be included in the documentation.
    List leading_comments = create_list(sizeof(AST *));

    for (size_t i = 0; i < root->root.nodes.count; i++) {
        AST *node = (AST *)root->root.nodes.items[i];

        switch (node->type) {
            case AST_COMMENT:
                push_item(&leading_comments, (AST *)node);
                continue;
            case AST_FUNCTION: {
                Symbol *symbol = get_symbol(documentor->context, node->uid);
                assert(symbol != NULL);

                if (symbol->exported || !exported_symbols_only)
                    break;

                reset_comments(&leading_comments);
                continue;
            }
            case AST_CUSTOM_TYPE: {
                Custom_Type *type = find_custom_type(documentor->context, CUST_STRUCT, 
                    node->custom_type.name, node->custom_type.name_length, node->module_uid);

                if (type == NULL)
                    type = find_custom_type(documentor->context, CUST_ENUM, 
                        node->custom_type.name, node->custom_type.name_length, node->module_uid);

                assert(type != NULL);

                if (type->exported || !exported_symbols_only)
                    break;

                reset_comments(&leading_comments);
                break;
            }
            case AST_ALIAS: {
                Alias *alias = find_alias(documentor->context, node->alias.lhs->alias_lhs.name,
                    node->alias.lhs->alias_lhs.name_length, node->module_uid, false);

                if (alias == NULL)
                    alias = find_alias(documentor->context, node->alias.lhs->alias_lhs.name,
                        node->alias.lhs->alias_lhs.name_length, node->module_uid, true);

                assert(alias != NULL);

                if (alias->exported || !exported_symbols_only)
                    break;

                reset_comments(&leading_comments);
                break;
            }
            default:
                reset_comments(&leading_comments);
                continue;
        }

        push_element(documentor, create_element(ast_to_element_type(documentor, node), 
            node, leading_comments, ast_to_group_uid(documentor, node)));

        leading_comments = create_list(sizeof(AST *));
    }

    delete_list(&leading_comments);
}

static char *constant_value_to_string(AST *ast) {
    char *str = malloc(24);

    if (ast->literal.data_type.primitive_type == PRIM_I32)
        sprintf(str, "%" PRId32, ast->literal.i32);
    else if (ast->literal.data_type.primitive_type == PRIM_I64)
        sprintf(str, "%" PRId64, ast->literal.i64);
    else if (ast->literal.data_type.primitive_type == PRIM_U32)
        sprintf(str, "%" PRIu32, ast->literal.u32);
    else if (ast->literal.data_type.primitive_type == PRIM_U64)
        sprintf(str, "%" PRIu64, ast->literal.u64);
    else if (ast->literal.data_type.primitive_type == PRIM_F32)
        sprintf(str, "%f", ast->literal.f32);
    else {
        assert(ast->literal.data_type.primitive_type == PRIM_F64);
        sprintf(str, "%lf", ast->literal.f64);
    }

    return str;
}

static char *comments_to_string(List *comments) {
    char *str = calloc(1, sizeof(char));
    size_t len = 0;

    for (size_t i = 0; i < comments->count; i++) {
        AST *comment = (AST *)comments->items[i];
        char *comment_ptr = comment->comment.comment;
        assert(comment_ptr != NULL);

        // Skip any whitespace at the start of the comment.
        while (isspace(*comment_ptr))
            comment_ptr++;

        str = realloc(str, len + comment->comment.comment_length + 3);
        strcat(str, comment_ptr);

        len += comment->comment.comment_length;

        if (i + 1 < comments->count) {
            strcat(str, "\n");
            len++;
        }
    }

    if (len > 0)
        strcat(str, "\n");

    return str;
}

static char *default_value_to_string(Context *context, AST *value) {
    switch (value->type) {
        case AST_INT:
        case AST_FLOAT: return constant_value_to_string(value);
        case AST_BOOL: return copy_whole_string(value->bool_value ? "true" : "false");
        case AST_NULL: return copy_whole_string("null");
        case AST_CONSTANT: return copy_string(value->constant.name, value->constant.name_length);
        case AST_CUSTOM_TYPE: { // SHOULD be an enum.
            Custom_Type *type = find_custom_type(context, CUST_ENUM, value->custom_type.name, value->custom_type.name_length, value->module_uid);
            assert(type != NULL);
            return copy_string(type->name, type->name_length);
        }
        case AST_ACCESS: {// This should ONLY be an enum.
            assert(value->access.lhs->type == AST_ENUM_NAME);
            assert(value->access.rhs->type == AST_MEMBER);

            Custom_Type *type = get_custom_type(context, value->access.rhs->member.custom_type_symbol_uid);
            assert(type != NULL);
            Custom_Type_Member *member = get_custom_type_member(context, type->uid, value->access.rhs->member.member_symbol_uid);
            assert(member != NULL);

            char *str = malloc(type->name_length + member->name_length + 2);
            sprintf(str, "%.*s.%.*s", (int)type->name_length, type->name, (int)member->name_length, member->name);
            return str;
        } default:
            printf(">>>%s\n", ast_type_to_string(value->type));
            assert(false);
            return copy_whole_string("(none)");
    }
}

static char *function_parameters_to_string(Context *context, List *parameters) {
    char *str = calloc(1, sizeof(char));
    size_t len = 0;

    for (size_t i = 0; i < parameters->count; i++) {
        AST *param = parameters->items[i];
        char *type = data_type_to_string(&param->parameter.data_type);
        const size_t type_len = strlen(type);

        str = realloc(str, len + type_len + param->parameter.name_length + 5);
        strcat(str, param->parameter.name);
        strcat(str, ": ");
        strcat(str, type);
        free(type);

        len += type_len + param->parameter.name_length + 2;

        if (param->parameter.default_value != NULL) {
            char *value = default_value_to_string(context, param->parameter.default_value);
            const size_t value_len = strlen(value);

            str = realloc(str, len + value_len + 6);
            strcat(str, " = ");
            strcat(str, value);
            free(value);
            len += value_len + 3;
        }

        if (i + 1 < parameters->count) {
            strcat(str, ", ");
            len += 2;
        }
    }
    
    return str;
}

static char *function_element_to_string(Context *context, Element *element) {
    char *comments =  comments_to_string(&element->comments);
    AST *def = element->definition;
    char *params = function_parameters_to_string(context, &def->function.parameters);
    char *type = data_type_to_string(&def->function.data_type);

    char *str = malloc((def->function.name_length * 2) + strlen(type) + strlen(params) + strlen(comments) + 64);
    sprintf(str, "### %.*s\n"
                 "%s"
                 "```rs\n"
                 "%.*s(%s): %s;\n"
                 "```\n\n", (int)def->function.name_length, def->function.name,
                comments, (int)def->function.name_length, def->function.name, params, type);

    free(type);
    free(params);
    free(comments);
    return str;
}

static char *struct_element_to_string(Documentor *documentor, Element *element) {
    Custom_Type *type = find_custom_type(documentor->context, CUST_STRUCT, element->definition->custom_type.name,
        element->definition->custom_type.name_length, element->definition->module_uid);
    assert(type != NULL);

    char *comments = comments_to_string(&element->comments);
    char *str = malloc((type->name_length * 2) + strlen(comments) + 64);
    sprintf(str, "### %.*s\n"
                 "%s"
                 "```rs\n"
                 "%.*s: struct {\n", (int)type->name_length, type->name, 
                 comments, (int)type->name_length, type->name);

    free(comments);
    size_t len = strlen(str);

    for (size_t i = 0; i < type->member_count; i++) {
        Custom_Type_Member *member = &type->members[i];
        char *member_type = data_type_to_string(&member->data_type);
        const size_t member_type_len = strlen(member_type);

        str = realloc(str, len + member_type_len + member->name_length + 17);
        strcat(str, "    ");
        strcat(str, member->name);
        strcat(str, ": ");
        strcat(str, member_type);
        free(member_type);
        strcat(str, ";\n");
        len += member_type_len + member->name_length + 8;
    }

    strcat(str, "}\n```\n\n");
    return str;
}

static char *enum_element_to_string(Documentor *documentor, Element *element) {
    Custom_Type *type = find_custom_type(documentor->context, CUST_ENUM, element->definition->custom_type.name,
        element->definition->custom_type.name_length, element->definition->module_uid);
    assert(type != NULL);
    
    char *comments = comments_to_string(&element->comments);
    char *str = malloc((type->name_length * 2) + strlen(comments) + 64);
    sprintf(str, "### %.*s\n"
                 "%s"
                 "```rs\n"
                 "%.*s: enum {\n", (int)type->name_length, type->name, 
                 comments, (int)type->name_length, type->name);

    free(comments);
    size_t len = strlen(str);

    for (size_t i = 0; i < type->member_count; i++) {
        Custom_Type_Member *member = &type->members[i];
        str = realloc(str, len + member->name_length + 14);
        strcat(str, "    ");
        strcat(str, member->name);
        len += member->name_length + 4;

        if (i + 1 < type->member_count) {
            strcat(str, ",");
            len++;
        }

        strcat(str, "\n");
        len++;
    }

    strcat(str, "}\n```\n\n");
    return str;
}

static char *alias_element_to_string(Documentor *documentor, Element *element) {
    AST *lhs = element->definition->alias.lhs;
    AST *rhs = element->definition->alias.rhs;

    Alias *alias = find_alias(documentor->context, lhs->alias_lhs.name, lhs->alias_lhs.name_length, lhs->module_uid, true);
    assert(alias != NULL);

    char *comments = comments_to_string(&element->comments);
    char *str = malloc((alias->name_length * 2) + strlen(comments) + rhs->identifier.length + 64);
    sprintf(str, "### %.*s\n"
                 "%s"
                 "```rs\n"
                 "%.*s: alias %s;\n"
                 "```\n", (int)alias->name_length, alias->name, 
                 comments, (int)alias->name_length, alias->name, rhs->identifier.identifier);

    free(comments);
    return str;
}

static char *element_to_markdown(Documentor *documentor, Element *element) {
    switch (element->type) {
        case ELEM_FUNCTION: return function_element_to_string(documentor->context, element);
        case ELEM_STRUCT: return struct_element_to_string(documentor, element);
        case ELEM_ENUM: return enum_element_to_string(documentor, element);
        case ELEM_ALIAS: return alias_element_to_string(documentor, element);
        default:
            assert(false);
            return calloc(1, sizeof(char));
    }
}

char *generate_documentation(Context *context, AST *root, const bool exported_symbols_only) {
    Documentor doc = create_documentor(context);
    document_elements(&doc, root, exported_symbols_only);

    // Sort documentation elements by their groups.
    String_Builder *builders = malloc(SECTION_BUILDER_CAPACITY * sizeof(String_Builder));
    builders[0] = create_string_builder(); // This will be reserved for elements without a group.

    size_t *known_groups = malloc(SECTION_BUILDER_CAPACITY * sizeof(size_t));
    known_groups[0] = 0;

    size_t group_count = 1;
    size_t group_capacity = SECTION_BUILDER_CAPACITY;

    for (size_t i = 0; i < doc.element_count; i++) {
        Element *elem = &doc.elements[i];
        char *str = element_to_markdown(&doc, elem);

        if (group_count + 1 > group_capacity) {
            group_capacity *= 2;
            builders = realloc(builders, group_capacity * sizeof(String_Builder));
            known_groups = realloc(known_groups, group_capacity * sizeof(size_t));
        }

        size_t builder_index = 0;

        if (group_count <= elem->group_uid) {
            // Unknown group, add another.
            known_groups[group_count] = elem->group_uid;
            builder_index = group_count++;
            builders[builder_index] = create_string_builder();
        } else {
            for (size_t j = 0; j < group_count; j++) {
                if (known_groups[j] == elem->group_uid) {
                    builder_index = known_groups[j];
                    break;
                }
            }
        }

        append_whole_string(&builders[builder_index], str);
        free(str);
    }

    String_Builder final = create_string_builder();
    const Module *module = get_module(context, root->module_uid);

    char *module_name = malloc(module->name_length + 32);
    sprintf(module_name, "# %.*s.ki\n\n", (int)module->name_length, module->name);
    append_whole_string(&final, module_name);
    free(module_name);

    for (size_t i = 0; i < group_count; i++) {
        AST *group = get_group(doc.context, known_groups[i]);
        char *header;

        if (group == NULL) {
            // This must be the elements not in a group.
            header = calloc(1, sizeof(char));
        } else {
            header = malloc(group->group.name_length + 12);
            sprintf(header, "## %.*s\n\n", (int)group->group.name_length, group->group.name);
        }

        append_whole_string(&final, header);
        free(header);

        append_string(&final, builders[i].data, builders[i].length);
        delete_string_builder(&builders[i]);
    }

    free(builders);
    free(known_groups);
    delete_documentor(&doc);
    return string_builder_to_cstring(&final);
}