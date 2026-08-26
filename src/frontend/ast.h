#ifndef AST_H
#define AST_H

#include "list.h"
#include "data_type.h"
#include "source.h"
#include "token.h"
#include "context.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define ast_location(ast) ast->source.ln, ast->source.col, &ast->source, &ast->scope, ast->module_uid

typedef enum {
    AST_NOP,
    AST_ERROR,
    AST_ROOT,
    AST_INT,
    AST_FLOAT,
    AST_STRING,
    AST_IDENTIFIER,
    AST_VARIABLE,
    AST_PARAMETER,
    AST_FUNCTION,
    AST_RETURN,
    AST_DECLARATION,
    AST_ASSIGNMENT,
    AST_MATH,
    AST_OPERATOR,
    AST_ASM,
    AST_COMPOUND_MATH,
    AST_CALL,
    AST_CONDITION,
    AST_IF,
    AST_WHILE,
    AST_KEYWORD_STMT,
    AST_FOR,
    AST_REFERENCE,
    AST_INDEX,
    AST_SIZEOF,
    AST_EXPRESSION,
    AST_UNARY,
    AST_CAST,
    AST_DEREFERENCE,
    AST_ACCESS,
    AST_ENUM_NAME,
    AST_MEMBER,
    AST_EXPORT,
    AST_IMPORT,
    AST_MODULE_NAME,
    AST_ALIAS,
    AST_CONSTANT,
    AST_DECORATOR,
    AST_CUSTOM_TYPE,
    AST_GROUP,
    AST_GROUP_LIST,
    AST_ALIAS_LHS,
    AST_COMMENT, // Only for the documentor.
    AST_NULL,
    AST_BOOL,
    AST_STRUCT_INITIALIZER,
    AST_STRUCT_NAME,
    AST_ARRAY_INITIALIZER
} AST_Type;

typedef struct AST AST;

typedef struct {
    List nodes;
} AST_Root;

typedef struct {
    Data_Type data_type;

    union {
        uint8_t u8; // char
        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        float f32;
        double f64;

        struct {
            char *string;
            size_t string_length;
        };
    };
} AST_Literal;

typedef struct {
    char *identifier;
    size_t length;
    bool resolved;
} AST_Identifier;

typedef struct {
    char *name;
    size_t name_length;
    size_t symbol_uid;
} AST_Variable;

typedef struct {
    char *name;
    size_t name_length;
    Data_Type data_type;
    AST *default_value;
    size_t variable_uid;
    size_t symbol_uid;
    bool resolved;
} AST_Parameter;

typedef struct {
    char *name;
    size_t name_length;
    Data_Type data_type;
    List parameters;
    List body;
    List decorators;
    size_t group_uid;
    bool no_body; // For extern functions.
    bool resolved;
} AST_Function;

typedef struct {
    AST *value;
    size_t symbol_uid;
} AST_Return;

typedef struct {
    char *name;
    size_t name_length;
    Data_Type data_type;
    AST *value;
    size_t variable_uid;
    bool resolved;
} AST_Declaration;

typedef struct {
    AST *lhs;
    AST *rhs;
} AST_Assignment;

typedef struct {
    List nodes;
} AST_Math;

typedef struct {
    Token_Type type;
    bool was_simple_boolean;
} AST_Operator;

typedef struct {
    AST *value;
} AST_Asm;

typedef struct {
    Token_Type type;
    AST *lhs;
    AST *rhs;
} AST_Compound_Math;

typedef struct {
    char *name;
    size_t name_length;
    List arguments;
    size_t symbol_uid;
} AST_Call;

typedef struct {
    List nodes;
} AST_Condition;

typedef struct {
    AST *condition;
    List body;
    List else_body;
} AST_If;

typedef struct {
    AST *condition;
    List body;
    bool do_first;
} AST_While;

typedef enum {
    KW_STMT_BREAK,
    KW_STMT_CONTINUE
} Keyword_Statement;

typedef struct {
    AST *lhs;
    AST *rhs;
    AST *step;
    List body;
    bool is_reverse;
    bool range_is_inclusive;
} AST_For;

typedef struct {
    AST *value;
} AST_Reference;

typedef struct {
    AST *base;
    AST *index;
} AST_Index;

typedef struct {
    AST *value;
    Data_Type data_type;
} AST_Sizeof;

typedef struct {
    AST *value;
} AST_Expression;

typedef struct {
    Token_Type type;
    AST *value;
} AST_Unary;

typedef struct {
    Data_Type data_type;
    AST *value;
} AST_Cast;

typedef struct {
    AST *value;
} AST_Dereference;

typedef struct {
    AST *lhs;
    AST *rhs;
} AST_Access;

typedef struct {
    char *name;
    size_t length;
    size_t symbol_uid;
} AST_Enum_Name;

typedef struct {
    size_t custom_type_symbol_uid;
    size_t member_symbol_uid;
    AST *struct_access_lhs;
} AST_Member;

typedef struct {
    List identifiers;
} AST_Export;

typedef struct {
    char *path;
    size_t path_length;
    List from_identifiers;
    List from_groups;
    char *as_name;
    size_t as_name_length;
    bool use_all_symbols;
    size_t module_uid; // Found in resolver.
} AST_Import;

typedef struct {
    size_t module_uid;
} AST_Module_Name;

typedef struct {
    AST *lhs;
    AST *rhs;
} AST_Alias;

typedef struct {
    bool is_definition;
    char *name;
    size_t name_length;
    AST *value;
    size_t group_uid;
} AST_Constant;

typedef struct {
    char *name;
    size_t name_length;
} AST_Decorator;

// Purely for the resolver to detect a custom type declaration,
// these members are readonly.
typedef struct {
    char *name;
    size_t name_length;
} AST_Custom_Type;

typedef struct {
    char *name;
    size_t name_length;
    size_t group_uid;
} AST_Group;

typedef struct {
    char *name;
    size_t name_length;
    size_t group_uid;
    List statements;
} AST_Group_List;

typedef struct {
    char *name;
    size_t name_length;
    size_t group_uid;
    bool resolved;
} AST_Alias_LHS;

typedef struct {
    char *comment;
    size_t comment_length;
} AST_Comment;

typedef struct {
    List values;
    List annotations;
    Data_Type data_type;
} AST_Struct_Intializer;

typedef struct {
    char *name;
    size_t length;
    size_t symbol_uid;
} AST_Struct_Name;

typedef struct {
    List values;
    Data_Type data_type;
} AST_Array_Initializer;

struct AST {
    AST_Type type;
    Scope scope;
    Source source;
    size_t module_uid;
    size_t uid;

    union {
        AST_Root root;
        AST_Literal literal;
        AST_Identifier identifier;
        AST_Variable variable;
        AST_Parameter parameter;
        AST_Function function;
        AST_Return return_;
        AST_Declaration declaration;
        AST_Assignment assignment;
        AST_Math math;
        AST_Operator operator;
        AST_Asm asm_;
        AST_Compound_Math compound_math;
        AST_Call call;
        AST_Condition condition;
        AST_If if_;
        AST_While while_;
        Keyword_Statement keyword_statement;
        AST_For for_;
        AST_Reference reference;
        AST_Index index;
        AST_Sizeof sizeof_;
        AST_Expression expression;
        AST_Unary unary;
        AST_Cast cast;
        AST_Dereference dereference;
        AST_Access access;
        AST_Enum_Name enum_name;
        AST_Member member;
        AST_Export export;
        AST_Import import;
        AST_Module_Name module_name;
        AST_Alias alias;
        AST_Constant constant;
        AST_Decorator decorator;
        AST_Custom_Type custom_type;
        AST_Group group;
        AST_Group_List group_list;
        AST_Alias_LHS alias_lhs;
        AST_Comment comment;
        bool bool_value;
        AST_Struct_Intializer struct_initializer;
        AST_Struct_Name struct_name;
        AST_Array_Initializer array_initializer;
    };
};

AST *new_ast(AST_Type type, size_t ln, size_t col, Source *current_source, Scope *current_scope, size_t module_uid, size_t uid);
void delete_ast_list(List *list);
void delete_ast(AST *ast);
char *ast_type_to_string(const AST_Type type);

// I wanted to put these in data_type.c but header files are a pain in the ass.
Data_Type get_ast_data_type(Context *context, AST *ast);
Data_Type infer_ast_list_data_type(Context *context, List *list);

#endif
