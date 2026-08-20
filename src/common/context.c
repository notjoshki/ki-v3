#include "context.h"
#include "ast.h"
#include "list.h"
#include "source.h"
#include "utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define CUSTOM_TYPE_CAPACITY 4
#define MEMBER_CAPACITY 4
#define ALIAS_CAPACITY 4
#define CONSTANT_CAPACITY 4
#define MODULE_CAPACITY 50

Context create_context(char *entrypoint_function, bool freestanding) {
    return (Context){ .node_uid = 0, 
        .custom_types = malloc(CUSTOM_TYPE_CAPACITY * sizeof(Custom_Type)),
        .custom_type_count = 0, .custom_type_capacity = CUSTOM_TYPE_CAPACITY, 
        .aliases = malloc(ALIAS_CAPACITY * sizeof(Alias)),
        .alias_count = 0, .alias_capacity = ALIAS_CAPACITY, 
        .constants = malloc(CONSTANT_CAPACITY * sizeof(Constant)),
        .constant_count = 0, .constant_capacity = CONSTANT_CAPACITY,
        .warning_flags = 0, .groups = create_list(sizeof(AST *)),
        .entrypoint_function = entrypoint_function, .entrypoint_function_length = strlen(entrypoint_function),
        .symbols = malloc(SYMBOL_CAPACITY * sizeof(Symbol)),
        .symbol_count = 0, .symbol_capacity = SYMBOL_CAPACITY,
        .modules = malloc(MODULE_CAPACITY * sizeof(Module)),
        .module_count = 0, .module_capacity = MODULE_CAPACITY, .scope_stack_count = 0,
        .freestanding = freestanding };
}

static void delete_custom_type(Custom_Type *type) {
    free(type->name);

    for (size_t i = 0; i < type->member_count; i++) {
        Custom_Type_Member *member = &type->members[i];
        free(member->name);
        delete_data_type(&member->data_type);

        if (member->value != NULL)
            delete_ast(member->value);
    }

    free(type->members);

    if (type->type == CUST_ENUM)
        delete_data_type(&type->enum_data_type);
}

void delete_context(Context *context, const bool free_modules) {
    for (size_t i = 0; i < context->custom_type_count; i++)
        delete_custom_type(&context->custom_types[i]);

    free(context->custom_types);

    for (size_t i = 0; i < context->alias_count; i++)
        free(context->aliases[i].name);

    free(context->aliases);
    delete_ast_list(&context->groups);
    free(context->symbols);

    for (size_t i = 0; i < context->constant_count; i++)
        free(context->constants[i].name);

    free(context->constants);

    if (!free_modules)
        return;
 
    for (size_t i = 0; i < context->module_count; i++) {
        free(context->modules[i].imported_identifiers.items);
        free(context->modules[i].imported_groups.items);
        free(context->modules[i].name);
        free(context->modules[i].path);
        free(context->modules[i].directory);

        assert(context->modules[i].root != NULL);
        delete_ast(context->modules[i].root);
    }

    free(context->modules);
}

Custom_Type *new_custom_type(Context *context, Custom_Type_Kind type, Source *source, size_t module_uid, char *name, size_t length, size_t group_uid) {
    if (context->custom_type_count + 1 >= context->custom_type_capacity) {
        context->custom_type_capacity *= 2;
        context->custom_types = realloc(context->custom_types, context->custom_type_capacity * sizeof(Custom_Type));
    }

    context->custom_types[context->custom_type_count] = 
        (Custom_Type){ .uid = context->custom_type_count, .type = type, .source = *source, .module_uid = module_uid,
            .name = name, .name_length = length, .members = malloc(MEMBER_CAPACITY * sizeof(Custom_Type_Member)),
            .member_count = 0, .member_capacity = MEMBER_CAPACITY, .resolved = false, .exported = false, .group_uid = group_uid };

    return &context->custom_types[context->custom_type_count++];
}

bool found_identifier_in_imported_module_identifiers(const char *name, const size_t length, const Module *module) {
    for (size_t i = 0; i < module->imported_identifiers.count; i++) {
        AST *ident = (AST *)module->imported_identifiers.items[i];

        if (compare_string(name, length, ident->identifier.identifier, ident->identifier.length))
            return true;
    }

    return false;
}

bool found_group_in_imported_module_groups(const size_t group_uid, const Module *module) {
    for (size_t i = 0; i < module->imported_groups.count; i++) {
        AST *group = (AST *)module->imported_groups.items[i];

        if (group->group.group_uid == group_uid)
            return true;
    }

    return false;
}

Custom_Type *find_custom_type(Context *context, const Custom_Type_Kind type, const char *name, const size_t length, const size_t module_uid) {
    Module *module = get_module(context, module_uid);

    for (size_t i = 0; i < context->custom_type_count; i++) {
        Custom_Type *ct = &context->custom_types[i];

        if (ct->type != type || (!ct->exported && module_uid != ct->module_uid) || !compare_string(ct->name, ct->name_length, name, length))
            continue;

        const Module *ct_module = get_module(context, ct->module_uid);
        assert(ct_module != NULL);
        
        if (compare_string(ct_module->name, ct_module->name_length, module->name, module->name_length) ||
                ct_module->using_all_symbols || 
                found_identifier_in_imported_module_identifiers(ct->name, ct->name_length, module) ||
                found_group_in_imported_module_groups(ct->group_uid, module))
            return ct;
    }

    return NULL;
}

Custom_Type *get_custom_type(Context *context, const size_t uid) {
    for (size_t i = 0; i < context->custom_type_count; i++) {
        if (uid == i)
            return &context->custom_types[i];
    }

    return NULL;
}

// See export_group().
Custom_Type *get_custom_type_by_index_in_module(Context *context, const size_t index, const size_t module_uid, int *out_index) {
    if (index >= context->custom_type_count) {
        *out_index = -1;
        return NULL;
    }

    for (size_t i = index; i < context->custom_type_count; i++) {
        Custom_Type *ct = &context->custom_types[i];

        if (ct->module_uid == module_uid) {
            *out_index = i;
            return ct;
        }
    }

    return NULL;
}

void push_custom_type_member(Context *context, const size_t type_uid, Source *source, char *name, size_t name_length, Data_Type data_type, AST *unresolved_value) {
    Custom_Type *type = get_custom_type(context, type_uid);

    if (type->member_count + 1 >= type->member_capacity) {
        type->member_capacity *= 2;
        type->members = realloc(type->members, type->member_capacity * sizeof(Custom_Type_Member));
    }

    type->members[type->member_count] = (Custom_Type_Member){ .uid = type->member_count, .name = name, .name_length = name_length, 
       .source = *source, .data_type = data_type, .value = unresolved_value, .parent_symbol_uid = type->uid };
    type->member_count++;
}

Custom_Type_Member *find_custom_type_member(Context *context, const size_t type_uid, const char *name, const size_t length) {
    const Custom_Type *type = get_custom_type(context, type_uid);

    for (size_t i = 0; i < type->member_count; i++) {
        if (compare_string(type->members[i].name, type->members[i].name_length, name, length))
            return &type->members[i];
    }

    return NULL;
}

Custom_Type_Member *get_custom_type_member(Context *context, const size_t type_uid, const size_t uid) {
    Custom_Type *type = get_custom_type(context, type_uid);

    for (size_t i = 0; i < type->member_count; i++) {
        if (i == uid)
            return &type->members[i];
    }

    return NULL;
}

Data_Type infer_enum_members_data_type(const Custom_Type_Member *members, const size_t count) {
    // Copied from ast.c.
    bool is_float = false;
    bool is_unsigned = false;
    bool is_64 = false;
    size_t highest_ptr_count = 0;

    for (size_t i = 0; i < count; i++) {
        const Data_Type dt = members[i].data_type;

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

void push_alias(Context *context, Source *source, size_t module_uid, char *name, size_t name_length,
        char *replacement, const size_t replacement_length, size_t group_uid) {
    if (context->alias_count + 1 > context->alias_capacity) {
        context->alias_capacity *= 2;
        context->aliases = realloc(context->aliases, context->alias_capacity * sizeof(Alias));
    }

    context->aliases[context->alias_count++] = (Alias){ .source = source, .module_uid = module_uid,
        .name = copy_string(name, name_length), .name_length = name_length, .group_uid = group_uid,
        .replacement = replacement, .replacement_length = replacement_length, .exported = false };
}

Alias *find_alias(Context *context, const char *name, const size_t length, const size_t module_uid, const bool allow_unexported) {
    Module *module = get_module(context, module_uid);

    for (size_t i = 0; i < context->alias_count; i++) {
        Alias *alias = &context->aliases[i];

        if (!compare_string(alias->name, alias->name_length, name, length))
            continue;

        const Module *alias_module = get_module(context, alias->module_uid);
        assert(alias_module != NULL);

        if (compare_string(alias_module->name, alias_module->name_length, module->name, module->name_length) ||
                ((alias_module->using_all_symbols ||
                found_identifier_in_imported_module_identifiers(alias->name, alias->name_length, module) ||
                found_group_in_imported_module_groups(alias->group_uid, module)) &&
                (allow_unexported || alias->exported)))
            return alias;
    }

    return NULL;
}

// See export_group().
Alias *get_alias_by_index_in_module(Context *context, const size_t index, const size_t module_uid, int *out_index) {
    if (index >= context->alias_count) {
        *out_index = -1;
        return NULL;
    }

    for (size_t i = index; i < context->alias_count; i++) {
        Alias *alias = &context->aliases[i];

        if (alias->module_uid == module_uid) {
            *out_index = i;
            return alias;
        }
    }

    return NULL;
}

void push_constant(Context *context, Source *source, size_t module_uid, char *name, size_t name_length, AST *value, size_t group_uid) {
    if (context->constant_count + 1 > context->constant_capacity) {
        context->constant_capacity *= 2;
        context->constants = realloc(context->constants, context->constant_capacity * sizeof(Constant));
    }

    context->constants[context->constant_count++] = (Constant){ .source = source, .module_uid = module_uid, .value = value,
        .name = copy_string(name, name_length), .name_length = name_length, .group_uid = group_uid, .exported = false };
}

Constant *find_constant(Context *context, const char *name, const size_t length, const size_t module_uid, const bool allow_unexported) {
    Module *module = get_module(context, module_uid);

    for (size_t i = 0; i < context->constant_count; i++) {
        Constant *constant = &context->constants[i];

        if (!compare_string(constant->name, constant->name_length, name, length))
            continue;

        const Module *constant_module = get_module(context, constant->module_uid);
        assert(constant_module != NULL);

        if (compare_string(constant_module->name, constant_module->name_length, module->name, module->name_length) ||
                ((constant_module->using_all_symbols ||
                found_identifier_in_imported_module_identifiers(constant->name, constant->name_length, module) ||
                found_group_in_imported_module_groups(constant->group_uid, module)) &&
                (allow_unexported || constant->exported)))
            return constant;
    }

    return NULL;
}

// See export_group().
Constant *get_constant_by_index_in_module(Context *context, const size_t index, const size_t module_uid, int *out_index) {
    if (index >= context->constant_count) {
        *out_index = -1;
        return NULL;
    }

    for (size_t i = index; i < context->constant_count; i++) {
        Constant *constant = &context->constants[i];

        if (constant->module_uid == module_uid) {
            *out_index = i;
            return constant;
        }
    }

    return NULL;
}

Module *new_module(Context *context, const char *name, const size_t name_length, const char *path, const size_t path_length, 
        const char *directory, const size_t directory_length) {
    context->modules[context->module_count] = (Module){ .uid = context->module_count, 
        .name = copy_string(name, name_length), .name_length = name_length,
        .path = copy_string(path, path_length), .path_length = path_length, 
        .directory = copy_string(directory, directory_length), .directory_length = directory_length, .builtin = false,
        .imported_identifiers = create_list(sizeof(AST *)), .imported_groups = create_list(sizeof(AST *)), .using_all_symbols = false, };

    return &context->modules[context->module_count++];
}

bool module_is_imported(Context *context, const size_t module_uid) {
    const Module *module = get_module(context, module_uid);

    for (size_t i = 0; i < context->module_count; i++) {
        if (compare_string(module->path, module->path_length, context->modules[i].path, context->modules[i].path_length))
            return true;
    }

    return false;
}

Module *find_module(Context *context, const char *name, const size_t length) {
    for (size_t i = 0; i < context->module_count; i++) {
        Module *module = &context->modules[i];

        if (compare_string(name, length, module->name, module->name_length))
            return module_is_imported(context, module->uid) ? module : NULL;
    }

    return NULL;
}

Module *get_module(Context *context, const size_t index) {
    return index >= context->module_count ? NULL : &context->modules[index];
}

void push_group(Context *context, AST *group) {
    group->group.group_uid = context->groups.count + 1;
    push_item(&context->groups, (AST *)group);
}

AST *find_group(Context *context, const char *name, const size_t length, const size_t module_uid) {
    for (size_t i = 0; i < context->groups.count; i++) {
        AST *group = (AST *)context->groups.items[i];

        if (group->module_uid == module_uid && compare_string(group->group.name, group->group.name_length, name, length))
            return group;
    }

    return NULL;
}

AST *get_group(Context *context, const size_t index) {
    assert(index <= context->groups.count); 
    // Section UIDs are always 1 ahead.
    return index == 0 ? NULL : (AST *)context->groups.items[index - 1];
}

void push_builtin_data(Context *context, AST *root) {
    Custom_Type *str = new_custom_type(context, CUST_STRUCT, &root->source, root->module_uid, copy_string("string", 6), 6, NO_SECTION);

    push_custom_type_member(context, str->uid, &root->source, 
        copy_string("capacity", 8), 8, create_data_type(PRIM_USIZE, 0), NULL);

    push_custom_type_member(context, str->uid, &root->source, 
        copy_string("length", 6), 6, create_data_type(PRIM_USIZE, 0), NULL);

    push_custom_type_member(context, str->uid, &root->source, 
        copy_string("data", 4), 4, create_data_type(PRIM_U8, 1), NULL);
}

void push_symbol(Context *context, Symbol_Type type, Source *source, size_t module_uid, size_t uid, char *name, size_t name_length, 
        Data_Type *data_type, Scope *scope, size_t group_uid, Symbol_Attribute attribute) {
    if (context->symbol_count + 1 > context->symbol_capacity) {
        context->symbol_capacity *= 2;
        context->symbols = realloc(context->symbols, context->symbol_capacity * sizeof(Symbol));
    }
    
    context->symbols[context->symbol_count++] = (Symbol){ .type = type, .source = source, 
        .module_uid = module_uid, .uid = uid,
        .name = name, .name_length = name_length, .group_uid = group_uid,
        .data_type = data_type, .scope = scope, .attribute = attribute, .exported = false, .flags = 0 };
}

static bool symbol_is_imported(const Symbol *symbol, const Module *module) {
    return found_identifier_in_imported_module_identifiers(symbol->name, symbol->name_length, module) ||
        found_group_in_imported_module_groups(symbol->group_uid, module);
    //return symbol->module_uid_from != -1 && module_is_imported(symbol->module_uid_from);
}

Symbol *find_symbol(Context *context, const Symbol_Type type, const char *name, const size_t length, 
        const Scope *scope, size_t module_uid) {
    const Module *module = get_module(context, module_uid);

    for (size_t i = 0; i < context->symbol_count; i++) {
        Symbol *sym = &context->symbols[i];

        if (sym->type != type || !compare_string(sym->name, sym->name_length, name, length))
            continue;

        const Module *sym_module = get_module(context, sym->module_uid);
        const bool is_using = sym_module->using_all_symbols && sym->exported;
        const bool is_imported = (sym->exported && symbol_is_imported(sym, module));
        const bool in_scope = type == SYMBOL_FUNCTION || (scope != FIND_IN_ANY_SCOPE && 
            is_in_scope(context, sym->scope)); //scope->scope_uid < sym->scope->scope_uid ? scope->scope_uid : sym->scope->scope_uid));
        const bool in_same_module = compare_string(sym_module->name, sym_module->name_length, module->name, module->name_length);
        
        if ((is_using || is_imported || in_same_module || module == FIND_IN_ANY_MODULE) && in_scope)
            return sym;
    }

    return NULL;
}

Symbol *get_function_symbol_by_index_in_module(Context *context, const size_t index, const size_t module_uid, int *out_index) {
    if (index >= context->symbol_count) {
        *out_index = -1;
        return NULL;
    }

    for (size_t i = index; i < context->symbol_count; i++) {
        Symbol *sym = &context->symbols[i];

        if (sym->type == SYMBOL_FUNCTION && sym->module_uid == module_uid) {
            *out_index = i;
            return sym;
        }
    }

    return NULL;
}

Symbol *get_symbol(Context *context, const size_t uid) {
    for (size_t i = 0; i < context->symbol_count; i++) {
        Symbol *sym = &context->symbols[i];

        if (sym->uid == uid)
            return sym;
    }

    return NULL;
}

void push_scope(Context *context, Scope *scope) {
    assert(context->scope_stack_count < SCOPE_STACK_CAPACITY);
    context->scope_stack[context->scope_stack_count++] = *scope;
}

bool is_in_scope(Context *context, const Scope *scope) {
    assert(context->scope_stack_count > 0);

    for (int i = context->scope_stack_count - 1; i >= 0; i--) {
        const Scope *prev = &context->scope_stack[i];

        //if (prev->scope_uid < clamp)
          //  return false;

        if (prev->function_uid == scope->function_uid && prev->ooak_uid <= scope->ooak_uid)
            return true;
    }

    return false;
}
