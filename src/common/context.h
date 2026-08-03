#ifndef CONTEXT_H
#define CONTEXT_H

typedef struct Context Context;

#include "source.h"
#include "data_type.h"
#include "list.h"
#include "scope.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define FIND_IN_ANY_MODULE NULL

#define SYMBOL_CAPACITY 8

#define FIND_IN_ANY_SCOPE NULL
#define NO_SECTION 0

typedef struct AST AST;

struct Module {
    size_t uid;
    char *path;
    size_t path_length;
    char *directory;
    size_t directory_length;
    char *name;
    size_t name_length;
    AST *root;
    List imported_identifiers;
    List imported_groups;
    bool using_all_symbols; // TOFIX: Eww ugly. See resolve_import().
};

typedef enum {
    CUST_STRUCT,
    CUST_ENUM
} Custom_Type_Kind;

typedef union {
    float f32;
    double f64;
    int32_t i32;
} Custom_Type_Enum_Value;

typedef struct Custom_Type Custom_Type;

typedef struct {
    size_t uid;
    Source source;
    char *name;
    size_t name_length;
    Data_Type data_type;
    AST *value;
    Custom_Type_Enum_Value enum_value;
    size_t parent_symbol_uid;
} Custom_Type_Member;

struct Custom_Type {
    size_t uid;
    Custom_Type_Kind type;
    Source source;
    size_t module_uid;
    char *name;
    size_t name_length;
    Custom_Type_Member *members;
    size_t member_count;
    size_t member_capacity;
    Data_Type enum_data_type;
    size_t group_uid;
    bool resolved;
    bool exported;
};

typedef enum {
    ALIAS_DATA,
    ALIAS_TYPE,
    ALIAS_MODULE
} Alias_Symbol_Type;

typedef struct Symbol Symbol;

typedef struct {
    Alias_Symbol_Type kind;

    union {
        Symbol *data;
        Custom_Type *type;
        size_t module_uid;
    };
} Alias_Symbol;

typedef struct {
    Source *source;
    size_t module_uid;
    char *name;
    size_t name_length;
    char *replacement;
    size_t replacement_length;
    size_t group_uid;
    bool exported;
} Alias;

typedef struct {
    Source *source;
    size_t module_uid;
    char *name;
    size_t name_length;
    AST *value;
    size_t group_uid;
    bool exported;
} Constant;

struct Context {
    size_t node_uid;
    Custom_Type *custom_types;
    size_t custom_type_count;
    size_t custom_type_capacity;
    Alias *aliases;
    size_t alias_count;
    size_t alias_capacity;
    Constant *constants;
    size_t constant_count;
    size_t constant_capacity;
    List groups;
    Symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    Module *modules;
    size_t module_count;
    size_t module_capacity;
    size_t warning_flags;
    char *entrypoint_function;
    size_t entrypoint_function_length;
};

typedef enum {
    SYMBOL_FUNCTION,
    SYMBOL_VARIABLE
} Symbol_Type;

typedef union {
    size_t variable_uid;
    List *parameters;
} Symbol_Attribute;

struct Symbol {
    Symbol_Type type;
    Source *source;
    size_t module_uid;
    size_t uid;
    char *name;
    size_t name_length;
    Data_Type *data_type;
    Scope *scope;
    Symbol_Attribute attribute;
    bool exported;
    size_t flags;
    size_t group_uid;
};

static inline Alias_Symbol alias_symbol_data(Symbol *symbol) {
    return (Alias_Symbol){ .kind = ALIAS_DATA, .data = symbol };
}

static inline Alias_Symbol alias_symbol_type(Custom_Type *type) {
    return (Alias_Symbol){ .kind = ALIAS_TYPE, .type = type };
}

static inline Alias_Symbol alias_symbol_module(size_t module_uid) {
    return (Alias_Symbol){ .kind = ALIAS_MODULE, .module_uid = module_uid };
}

Context create_context(char *entrypoint_function);
void delete_context(Context *context, const bool free_modules);

Custom_Type *new_custom_type(Context *context, Custom_Type_Kind type, Source *source, size_t module_uid, char *name, size_t length, size_t group_uid);
bool found_identifier_in_imported_module_identifiers(const char *name, const size_t length, const Module *module);
bool found_group_in_imported_module_groups(const size_t group_uid, const Module *module);
Custom_Type *find_custom_type(Context *context, const Custom_Type_Kind type, const char *name, const size_t length, const size_t module_uid);
Custom_Type *get_custom_type(Context *context, const size_t uid);

// See export_group().
Custom_Type *get_custom_type_by_index_in_module(Context *context, const size_t index, const size_t module_uid, int *out_index);

void push_custom_type_member(Context *context, const size_t type_uid, Source *source, char *name, size_t name_length, Data_Type data_type, AST *unresolved_value);
Custom_Type_Member *find_custom_type_member(Context *context, const size_t type_uid, const char *name, const size_t length);
Custom_Type_Member *get_custom_type_member(Context *context, const size_t type_uid, const size_t uid);
Data_Type infer_enum_members_data_type(const Custom_Type_Member *members, const size_t count);

void push_alias(Context *context, Source *source, size_t module_uid, char *name, size_t name_length,
        char *replacement, const size_t replacement_length, size_t group_uid);
Alias *find_alias(Context *context, const char *name, const size_t length, const size_t module_uid, const bool allow_unexported);

// See export_group().
Alias *get_alias_by_index_in_module(Context *context, const size_t index, const size_t module_uid, int *out_index);

void push_constant(Context *context, Source *source, size_t module_uid, char *name, size_t name_length, AST *value, size_t group_uid);
Constant *find_constant(Context *context, const char *name, const size_t length, const size_t module_uid, const bool allow_unexported);

// See export_group().
Constant *get_constant_by_index_in_module(Context *context, const size_t index, const size_t module_uid, int *out_index);

Module *new_module(Context *context, const char *name, const size_t name_length, const char *path, const size_t path_length, 
    const char *directory, const size_t directory_length);

bool module_is_imported(Context *context, const size_t module_uid);
Module *find_module(Context *context, const char *name, const size_t length);
Module *get_module(Context *context, const size_t index);

void push_group(Context *context, AST *group);
AST *find_group(Context *context, const char *name, const size_t length, const size_t module_uid);
AST *get_group(Context *context, const size_t index);

void push_builtin_data(Context *context, AST *root);

void push_symbol(Context *context, Symbol_Type type, Source *source, size_t module_uid, size_t uid, char *name, size_t name_length, 
    Data_Type *data_type, Scope *scope, size_t group_uid, Symbol_Attribute attribute);

Symbol *find_symbol(Context *context, const Symbol_Type type, const char *name, const size_t length, 
        const Scope *scope, const size_t module_uid);

// See import_to_hir(), export_group().
Symbol *get_function_symbol_by_index_in_module(Context *context, const size_t index, const size_t module_uid, int *out_index);

Symbol *get_symbol(Context *context, const size_t uid);

#endif
