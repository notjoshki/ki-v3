#ifndef HIR_H
#define HIR_H

#include "ast.h"
#include "data_type.h"
#include "token.h"
#include "list.h"
#include "context.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    OP_MATH,
    OP_SIZEOF
} Operator_Type;

typedef enum {
    DATA_NONE,
    DATA_LITERAL,
    DATA_LOCAL_VARIABLE,
    DATA_MATH,
    DATA_OPERATOR,
    DATA_CALL,
    DATA_CONDITION,
    DATA_REFERENCE,
    DATA_INDEX,
    DATA_UNARY,
    DATA_CAST,
    DATA_DEREFERENCE,
    DATA_STRUCT_MEMBER,
    DATA_STRUCT_INITIALIZER,
    DATA_ARRAY_INITIALIZER
} HIR_Data_Type;

typedef struct HIR_Data HIR_Data;

typedef struct {
    Data_Type data_type;

    union {
        uint8_t u8;
        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        float f32;
        double f64;

        struct {
            char *string;
            size_t length;
        } string;
    };
} HIR_Data_Literal;

typedef struct {
    char *name;
    size_t name_length;
    Data_Type data_type;
    size_t variable_uid;
    size_t uid;
} HIR_Data_Local_Variable;

typedef struct {
    HIR_Data *data;
    size_t count;
} HIR_Data_Expression;

typedef struct {
    Operator_Type type;

    union {
        Token_Type math;
        Data_Type sizeof_;
    };
} HIR_Data_Operator;

typedef struct {
    char *name;
    size_t name_length;
    int module_uid;
    HIR_Data *arguments;
    size_t argument_count;
    Data_Type data_type;
    List *parameters;
    size_t flags;
} HIR_Call;

// TODO: These following structs need malloc'd fields because
// HIR_Data hasn't been defined yet. I don't like this.

typedef struct {
    HIR_Data *value;
} HIR_Data_Reference;

typedef struct {
    HIR_Data *base;
    HIR_Data *index;
} HIR_Data_Index;

typedef struct {
    Token_Type type;
    HIR_Data *value;
} HIR_Data_Unary;

typedef struct {
    Data_Type data_type;
    HIR_Data *value;
} HIR_Data_Cast;

typedef struct {
    HIR_Data *value;
} HIR_Data_Dereference;

typedef struct {
    HIR_Data *lhs;
    size_t custom_type_symbol_uid;
    size_t member_symbol_uid; 
} HIR_Data_Struct_Member;

typedef struct {
    HIR_Data *values;
    size_t value_count;
    char **annotations;
    size_t *annotation_lengths;
    Data_Type data_type;
    size_t custom_type_symbol_uid;
} HIR_Data_Struct_Initializer;

typedef struct {
    HIR_Data *values;
    size_t value_count;
    Data_Type data_type;
} HIR_Data_Array_Initializer;

struct HIR_Data {
    HIR_Data_Type type;
    size_t scope_uid;
    size_t module_uid;

    union {
        HIR_Data_Literal literal;
        HIR_Data_Local_Variable local_variable;
        HIR_Data_Expression expression;
        HIR_Data_Operator operator;
        HIR_Call call;
        HIR_Data_Reference reference;
        HIR_Data_Index index;
        HIR_Data_Unary unary;
        HIR_Data_Cast cast;
        HIR_Data_Dereference dereference;
        HIR_Data_Struct_Member struct_member;
        HIR_Data_Struct_Initializer struct_initializer;
        HIR_Data_Array_Initializer array_initializer;
    };
};

typedef enum {
    HIR_NOP,
    HIR_BLOCK,
    HIR_FUNCTION,
    HIR_RETURN,
    HIR_DECLARATION,
    HIR_ASSIGNMENT,
    HIR_ASM,
    HIR_CALL,
    HIR_IF,
    HIR_WHILE_LOOP,
    HIR_FOR_LOOP,
    HIR_KEYWORD_STMT,
    HIR_REFERENCE,
    HIR_EXTERN
} HIR_Type;

typedef struct HIR HIR;

typedef struct {
    HIR *nodes;
    size_t count;
    size_t capacity;
} HIR_Block;

typedef struct {
    char *name;
    size_t name_length;
    size_t module_uid;
    Data_Type data_type;
    HIR_Data *parameters;
    size_t parameter_count;
    HIR_Block block;
    bool exported;
    size_t flags;
} HIR_Function;

typedef struct {
    Data_Type data_type;
    HIR_Data value;
    size_t symbol_uid;
} HIR_Return;

typedef struct {
    char *name;
    size_t name_length;
    Data_Type data_type;
    HIR_Data value;
    size_t variable_uid;
    size_t uid;
} HIR_Declaration;

typedef struct {
    HIR_Data lhs;
    HIR_Data rhs;
} HIR_Assignment;

typedef struct {
    char *code;
    size_t code_length;
} HIR_Asm;

typedef struct {
    HIR_Data condition;
    HIR_Block block;
    HIR_Block else_block;
} HIR_If;

// Originally I wanted just 1 loop struct, but it got pretty convoluted.

typedef struct {
    HIR_Data condition;
    HIR_Block block;
    bool do_block_first;
} HIR_While_Loop;

typedef struct {
    HIR_Data condition;
    HIR_Block block;
    HIR_Data iterator;
    HIR_Data iterator_initializer;
    HIR_Data iterator_increment;
    bool decrement_iterator;
} HIR_For_Loop;

typedef struct {
    char *name;
    size_t name_length;
    size_t module_uid;
} HIR_Extern;

struct HIR {
    HIR_Type type;

    union {
        HIR_Block block;
        HIR_Function function;
        HIR_Return return_;
        HIR_Declaration declaration;
        HIR_Assignment assignment;
        HIR_Asm asm_;
        HIR_Call call;
        HIR_If if_;
        HIR_While_Loop while_loop;
        HIR_For_Loop for_loop;
        Keyword_Statement keyword_statement;
        HIR_Data_Reference reference;
        HIR_Extern extern_;
    };
};

HIR ast_to_hir(Context *context, AST *ast);
void delete_hir(HIR *hir);
char *hir_type_to_string(const HIR_Type type);
char *hir_data_type_to_string(const HIR_Data_Type type);
Data_Type get_hir_data_type(Context *context, const HIR_Data *data);
Data_Type infer_hir_data_list_type(Context *context, const HIR_Data *data, const size_t count);

#endif
