#include "parser.h"
#include "context.h"
#include "ast.h"
#include "token.h"
#include "lexer.h"
#include "list.h"
#include "source.h"
#include "data_type.h"
#include "utilities.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

#define this_token parser->current_token
#define new_ast_arguments this_token->ln, this_token->col, &parser->source, &parser->current_scope, parser->current_module_uid, ++parser->context->node_uid

#define nop new_ast(AST_NOP, new_ast_arguments)
#define err new_ast(AST_ERROR, new_ast_arguments)

#define TOKEN_CAPACITY 8
#define EXPORT_IDENTIFIER_CAPACITY 4
#define IMPORT_PATH_CAPACITY 8
#define SYNC_TOKEN_CAPACITY 17

#define PARSER_NO_FLAGS 0
#define PARSER_NO_LEADING_VALUES 0x01
#define PARSER_NO_LEADING_ASSIGNMENT 0x02
#define PARSER_IN_MATH 0x04
#define PARSER_IN_CONDITION 0x08
#define PARSER_IN_VALUE 0x10
#define PARSER_IN_CALL 0x20
#define PARSER_IN_ACCESS 0x40
// Want to ignore these as much as possible, I only want comments for the documentor.

#define DATA_TYPE_COUNT 15

static const char *data_types[DATA_TYPE_COUNT] = {
    "void", "bool", "char", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64", "isize", "usize"
};

static Parser create_parser(Context *context, char *module, size_t module_length, char *path, size_t path_length,
        char *directory, size_t directory_length, bool tokenize_comments) {
    Lexer lex = create_lexer(path, tokenize_comments);
    Module *mod = new_module(context, module, module_length, path, path_length, directory, directory_length);

    // It's VERY important to use mod->path here and not path parameter as path could be freed
    // if this is an import in resolve_import(). This would then ruin all the symbol paths.
    Parser prs = { .context = context, .source = create_source(mod->path, 1, 1), 
        .current_scope = create_scope(0, 0, 0, 0), .current_module_uid = mod->uid,
        .tokens = malloc(TOKEN_CAPACITY * sizeof(Token)), .token_count = 0, .index = 0, 
        .local_variable_count = 0, .total_unique_scopes = 0, .flags = 0, .tokenize_comments = tokenize_comments };

    Token tok;
    size_t capacity = TOKEN_CAPACITY;

    while ((tok = get_next_token(&lex)).type != TOK_EOF) {
        if (prs.token_count + 2 > capacity) {
            capacity *= 2;
            prs.tokens = realloc(prs.tokens, capacity * sizeof(Token));
        }

        prs.tokens[prs.token_count++] = tok;
    }

    prs.tokens[prs.token_count++] = tok; // EOF
    delete_lexer(&lex);

    prs.current_token = &prs.tokens[0];
    return prs;
}

static void delete_parser(Parser *parser) {
    for (size_t i = 0; i < parser->token_count; i++)
        delete_token(&parser->tokens[i]);

    free(parser->tokens);
    free(parser);
}

static bool is_math_operator(Parser *parser) {
    switch (this_token->type) {
        case TOK_PLUS:
        case TOK_MINUS:
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:
        case TOK_AND:
        case TOK_OR:
        case TOK_XOR:
        case TOK_SHL:
        case TOK_SHR: return true;
        default: return false;
    }
}

static bool is_condition_operator(Parser *parser) {
    switch (this_token->type) {
        case TOK_EQ:
        case TOK_NEQ:
        case TOK_LT:
        case TOK_LTE:
        case TOK_GT:
        case TOK_GTE:
        case TOK_BOOL_AND:
        case TOK_BOOL_OR: return true;
        default: return false;
    }
}

static void step(Parser *parser) {
    if (parser->index + 1 < parser->token_count) {
        parser->current_token = &parser->tokens[++parser->index];
        parser->source.ln = this_token->ln;
        parser->source.col = this_token->col;
        parser->current_scope.index_in_file = parser->index;
    }

    if (parser->tokenize_comments || this_token->type != TOK_COMMENT)
        return;

    while (this_token->type == TOK_COMMENT)
        step(parser);
}

static void skip_until(Parser *parser, const Token_Type *until_types, const size_t until_count) {
    while (this_token->type != TOK_EOF) {
        for (size_t i = 0; i < until_count; i++) {
            if (this_token->type == until_types[i])
                return;
        }

        step(parser);
    }
}

static void skip_to_next_statement(Parser *parser) {
    Token_Type sync_tokens[SYNC_TOKEN_CAPACITY];
    size_t count = 0;

    if (parser->flags & PARSER_IN_CALL) {
        sync_tokens[count++] = TOK_RPAREN;
        sync_tokens[count++] = TOK_COMMA;
    } else if (parser->flags & PARSER_IN_VALUE) {
        sync_tokens[count++] = TOK_RPAREN;
        sync_tokens[count++] = TOK_PLUS;
        sync_tokens[count++] = TOK_MINUS;
        sync_tokens[count++] = TOK_STAR;
        sync_tokens[count++] = TOK_SLASH;
        sync_tokens[count++] = TOK_PERCENT;
        sync_tokens[count++] = TOK_SHL;
        sync_tokens[count++] = TOK_SHR;
        sync_tokens[count++] = TOK_AND;
        sync_tokens[count++] = TOK_OR;
        sync_tokens[count++] = TOK_XOR;
        sync_tokens[count++] = TOK_RSQUARE;
    } else {
        sync_tokens[count++] = TOK_RBRACE;
        sync_tokens[count++] = TOK_SEMICOLON;
    }

    skip_until(parser, sync_tokens, count - 1);
}

static void eat(Parser *parser, const Token_Type type) {
    if (this_token->type == TOK_EOF)
        return; // TODO: Should we log this?

    if (this_token->type != type) {
        log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
            "Expected token type '%s' but found '%s'\n", token_type_to_string(type), token_type_to_string(this_token->type));

        step(parser);
        skip_to_next_statement(parser);
    } else
        step(parser);
}

static Token *peek(Parser *parser, const int offset) {
    assert(offset != 0);
    Token *tok;

    if (parser->index + offset >= parser->token_count)
        tok = &parser->tokens[parser->token_count - 1];
    else if ((int)parser->index + offset < 1)
        tok = &parser->tokens[0];
    else
        tok = &parser->tokens[parser->index + offset];

    if (parser->tokenize_comments || tok->type != TOK_COMMENT)
        return tok;

    return peek(parser, offset > 0 ? offset + 1 : offset - 1);
}

static void reset_current_scope(Parser *parser) {
    parser->current_scope.scope_uid = parser->current_scope.function_uid = 0;
}

static bool is_data_type(Parser *parser) {
    if (this_token->type != TOK_AND && this_token->type != TOK_IDENTIFIER)
        return false;

    const size_t begin = parser->index;

    while (this_token->type == TOK_AND)
        step(parser);

    bool found = false;

    if (this_token->type == TOK_IDENTIFIER) {
        for (size_t i = 0; i < DATA_TYPE_COUNT; i++) {
            if (compare_string(data_types[i], strlen(data_types[i]), this_token->value, this_token->length)) {
                found = true;
                break;
            }
        }
    }

    parser->index = begin;
    parser->current_token = &parser->tokens[parser->index];
    return found;
}

static AST *parse_value(Parser *parser);

static Data_Type parse_data_type(Parser *parser) {
    Data_Type dt = create_data_type(PRIM_VOID, 0);

    while (this_token->type == TOK_BOOL_AND) {
        dt.pointer_count += 2;
        step(parser);
    }

    if (this_token->type == TOK_AND) {
        dt.pointer_count++;
        step(parser);
    }

    if (peek(parser, 1)->type == TOK_ACCESS) {
        // This is a module access.
        dt.module_name = copy_string(this_token->value, this_token->length);
        dt.module_length = this_token->length;
        eat(parser, TOK_IDENTIFIER);
        step(parser);
    }

    if (compare_string(this_token->value, this_token->length, "bool", 4))
        dt.primitive_type = PRIM_BOOL;
    else if (compare_string(this_token->value, this_token->length, "i8", 2))
        dt.primitive_type = PRIM_I8;
    else if (compare_string(this_token->value, this_token->length, "i16", 3))
        dt.primitive_type = PRIM_I16;
    else if (compare_string(this_token->value, this_token->length, "i32", 3))
        dt.primitive_type = PRIM_I32;
    else if (compare_string(this_token->value, this_token->length, "i64", 3))
        dt.primitive_type = PRIM_I64;
    else if (compare_string(this_token->value, this_token->length, "u8", 2) || 
            compare_string(this_token->value, this_token->length, "char", 4))
        dt.primitive_type = PRIM_U8;
    else if (compare_string(this_token->value, this_token->length, "u16", 3))
        dt.primitive_type = PRIM_U16;
    else if (compare_string(this_token->value, this_token->length, "u32", 3))
        dt.primitive_type = PRIM_U32;
    else if (compare_string(this_token->value, this_token->length, "u64", 3))
        dt.primitive_type = PRIM_U64;
    else if (compare_string(this_token->value, this_token->length, "f32", 3))
        dt.primitive_type = PRIM_F32;
    else if (compare_string(this_token->value, this_token->length, "f64", 3))
        dt.primitive_type = PRIM_F64;
    else if (compare_string(this_token->value, this_token->length, "isize", 5))
        dt.primitive_type = PRIM_ISIZE;
    else if (compare_string(this_token->value, this_token->length, "usize", 5))
        dt.primitive_type = PRIM_USIZE;
    else if (!compare_string(this_token->value, this_token->length, "void", 4)) {
        dt.primitive_type = PRIM_CUSTOM;
        dt.custom_name = copy_string(this_token->value, this_token->length);
        dt.custom_length = this_token->length;
    }

    eat(parser, TOK_IDENTIFIER);

    if (this_token->type != TOK_LSQUARE)
        return dt;

    step(parser);

    if (this_token->type != TOK_INT) {
        dt.array_size = UNRESOLVED_ARRAY_SIZE;
        dt.unresolved_array_size = parse_value(parser);
    } else {
        char *endptr;
        errno = 0;
        dt.array_size = strtoull(token_to_cstring(this_token), &endptr, 10);
        check_literal_conversion(parser->source.path, this_token->ln, this_token->col, errno, this_token->value, endptr);
        step(parser);
    }

    eat(parser, TOK_RSQUARE);
    return dt;
}

static AST *parse(Parser *parser);

static AST *parse_math(Parser *parser, AST *first) {
    parser->flags |= PARSER_IN_MATH;

    if (first == NULL)
        first = parse_value(parser);

    AST *ast = new_ast(AST_MATH, ast_location(first), ++parser->context->node_uid);
    ast->math.nodes = create_list(sizeof(AST *));
    push_item(&ast->math.nodes, (AST *)first);

    while (is_math_operator(parser)) {
        AST *oper = new_ast(AST_OPERATOR, new_ast_arguments);
        oper->operator.type = this_token->type;
        push_item(&ast->math.nodes, (AST *)oper);
        step(parser);
        push_item(&ast->math.nodes, parse_value(parser));
    }

    parser->flags &= ~PARSER_IN_MATH;
    return ast;
}

static bool push_simple_boolean_comparison_if_present(Parser *parser, AST *condition) {
    // If the count is 1 then we know this is a boolean expression, just index at 0 to avoid invalid read.
    AST *last_oper = (AST *)condition->condition.nodes.items[condition->condition.nodes.count == 1 ? 0 : 
        condition->condition.nodes.count - 2];

    if (condition->condition.nodes.count == 1 || last_oper->operator.was_simple_boolean) {
        // Boolean true comparison without '== true'
        AST *oper = new_ast(AST_OPERATOR, new_ast_arguments);
        oper->operator.type = TOK_EQ;
        oper->operator.was_simple_boolean = true;
        push_item(&condition->condition.nodes, oper);

        AST *value = new_ast(AST_BOOL, new_ast_arguments);
        value->bool_value = true;
        push_item(&condition->condition.nodes, value);
        return true;
    }

    return false;
}

static AST *parse_condition(Parser *parser, AST *first) {
    parser->flags |= PARSER_IN_CONDITION;

    if (first == NULL)
        first = parse_value(parser);

    AST *ast = new_ast(AST_CONDITION, ast_location(first), ++parser->context->node_uid);
    ast->condition.nodes = create_list(sizeof(AST *));
    push_item(&ast->condition.nodes, (AST *)first);

    // 0 1  2 3  4 5  6
    // x == 1 || x == 2

    while (is_condition_operator(parser)) {
        bool was_simple_boolean = false;

        if (this_token->type == TOK_BOOL_AND || this_token->type == TOK_BOOL_OR) {
            was_simple_boolean = push_simple_boolean_comparison_if_present(parser, ast);
            /*
            AST *last_oper = (AST *)ast->condition.nodes.items[ast->condition.nodes.count == 1 ? 0 : ast->condition.nodes.count - 2];

            if (ast->condition.nodes.count == 1 || last_oper->operator.was_simple_boolean) {
                // Boolean true or false without == or !=
                AST *oper = new_ast(AST_OPERATOR, new_ast_arguments);
                oper->operator.type = TOK_EQ;
                oper->operator.was_simple_boolean = true;
                push_item(&ast->condition.nodes, oper);

                AST *value = new_ast(AST_BOOL, new_ast_arguments);
                value->bool_value = true;
                push_item(&ast->condition.nodes, value);
                was_simple_boolean = true;
            }
            */
        }

        AST *oper = new_ast(AST_OPERATOR, new_ast_arguments);
        oper->operator.type = this_token->type;
        oper->operator.was_simple_boolean = was_simple_boolean;
        push_item(&ast->condition.nodes, (AST *)oper);
        step(parser);
        push_item(&ast->condition.nodes, parse_value(parser));
    }

    push_simple_boolean_comparison_if_present(parser, ast);
    parser->flags &= ~PARSER_IN_CONDITION;
    return ast;
}

static AST *parse_leading_values(Parser *parser, AST *value) {
    if (parser->flags & PARSER_NO_LEADING_VALUES)
        return value;

    while (this_token->type != TOK_EOF) {
        if (!(parser->flags & PARSER_IN_MATH) && is_math_operator(parser) &&
            // NOTE: Dereferences will get mistaken if this isn't here, as the previous semicolon is skipped in parse().
                peek(parser, -1)->type != TOK_SEMICOLON && peek(parser, 1)->type != TOK_EQUAL) {
            value = parse_math(parser, value);
            continue;
        }

        if (!(parser->flags & PARSER_IN_CONDITION) && !(parser->flags & PARSER_IN_MATH) && 
                is_condition_operator(parser) && peek(parser, 1)->type != TOK_EQUAL) {
            value = parse_condition(parser, value);
            continue;
        }
        
        break;
    }

    return value;
}

static AST *parse_value(Parser *parser) {
    const size_t flags = parser->flags;
    parser->flags |= PARSER_IN_VALUE;
    AST *value = parse(parser);
    parser->flags = flags;
    return parse_leading_values(parser, value);
}

static List parse_parameters(Parser *parser) {
    List params = create_list(sizeof(AST *));
    eat(parser, TOK_LPAREN);

    while (this_token->type != TOK_RPAREN && this_token->type != TOK_EOF) {
        if (params.count > 0) {
            eat(parser, TOK_COMMA);

            if (this_token->type == TOK_RPAREN)
                break;
        }

        AST *param = new_ast(AST_PARAMETER, new_ast_arguments);
        param->parameter.resolved = false;
        param->parameter.variable_uid = parser->local_variable_count++;
        param->parameter.name = copy_string(this_token->value, this_token->length);
        param->parameter.name_length = this_token->length;
        eat(parser, TOK_IDENTIFIER);
        eat(parser, TOK_COLON);

        param->parameter.data_type = parse_data_type(parser);
        push_item(&params, (AST *)param);

        push_symbol(parser->context, SYMBOL_VARIABLE, &param->source, param->module_uid, param->uid, param->parameter.name, param->parameter.name_length, 
            &param->parameter.data_type, &param->scope, NO_SECTION, 
            (Symbol_Attribute){ .variable_uid = param->parameter.variable_uid });

        if (this_token->type != TOK_EQUAL) {
            param->parameter.default_value = NULL;
            continue;
        }

        eat(parser, TOK_EQUAL);
        param->parameter.default_value = parse_value(parser);
    }

    eat(parser, TOK_RPAREN);
    return params;
}

static List parse_body(Parser *parser, bool require_braces) {
    List body = create_list(sizeof(AST *));

    if (this_token->type == TOK_LBRACE) {
        eat(parser, TOK_LBRACE);
        require_braces = true;
    }

    while (this_token->type != TOK_EOF && this_token->type != TOK_RBRACE) {
        push_item(&body, (AST *)parse(parser));

        if (!require_braces)
            break;
    }

    if (require_braces)
        eat(parser, TOK_RBRACE);

    return body;
}

static AST *parse_function(Parser *parser, List decorators, AST *group) {
    reset_current_scope(parser);
    AST *ast = new_ast(AST_FUNCTION, new_ast_arguments);
    parser->current_scope.function_uid = ast->uid;

    ast->function.decorators = decorators;
    ast->function.group_uid = group == NULL ? NO_SECTION : group->group.group_uid;
    ast->function.resolved = false;

    ast->function.name = copy_string(this_token->value, this_token->length);
    ast->function.name_length = this_token->length;
    step(parser);

    ast->function.parameters = parse_parameters(parser);

    if (this_token->type == TOK_COLON) {
        eat(parser, TOK_COLON);
        ast->function.data_type = parse_data_type(parser);
    } else
        ast->function.data_type = create_data_type(PRIM_VOID, 0);

    push_symbol(parser->context, SYMBOL_FUNCTION, &ast->source, ast->module_uid, ast->uid, ast->function.name, ast->function.name_length, 
        &ast->function.data_type, &ast->scope, group == NULL ? NO_SECTION : group->group.group_uid, 
        (Symbol_Attribute){ .parameters = &ast->function.parameters });

    if (this_token->type == TOK_SEMICOLON) {
        ast->function.body = create_list(sizeof(AST *));
        ast->function.no_body = true;
    } else {
        ast->function.body = parse_body(parser, true);
        ast->function.no_body = false;
    }

    parser->local_variable_count = 0;
    reset_current_scope(parser);
    return ast;
}

static AST *parse_return(Parser *parser) {
    AST *ast = new_ast(AST_RETURN, new_ast_arguments);
    eat(parser, TOK_KEYWORD);

    if (this_token->type != TOK_SEMICOLON)
        ast->return_.value = parse_value(parser);
    else
        ast->return_.value = NULL;

    ast->return_.symbol_uid = parser->current_scope.function_uid;
    return ast;
}

static AST *parse_asm(Parser *parser) {
    AST *ast = new_ast(AST_ASM, new_ast_arguments);
    eat(parser, TOK_KEYWORD);
    
    if (this_token->type == TOK_LPAREN) {
        step(parser);
        ast->asm_.value = parse_value(parser);
        eat(parser, TOK_RPAREN);
    } else
        ast->asm_.value = parse_value(parser);
    
    return ast;
}

static AST *parse_if(Parser *parser) {
    AST *ast = new_ast(AST_IF, new_ast_arguments);
    step(parser);
    ast->if_.condition = parse_condition(parser, NULL);

    const size_t ooak = parser->current_scope.ooak_uid;
    parser->current_scope.ooak_uid = ++parser->total_unique_scopes;
    parser->current_scope.scope_uid++;

    ast->if_.body = parse_body(parser, true);

    if (compare_string(this_token->value, this_token->length, "else", 4)) {
        parser->current_scope.ooak_uid = ++parser->total_unique_scopes;
        step(parser);
        ast->if_.else_body = parse_body(parser, !compare_string(this_token->value, this_token->length, "if", 2));
    } else 
        ast->if_.else_body = create_list(sizeof(AST *));

    parser->current_scope.ooak_uid = ooak;
    parser->current_scope.scope_uid--;
    return ast;
}

static AST *parse_while(Parser *parser, const bool do_first) {
    AST *ast = new_ast(AST_WHILE, new_ast_arguments);
    step(parser);
    ast->while_.do_first = do_first;

    const size_t ooak = parser->current_scope.ooak_uid;
    parser->current_scope.ooak_uid = ++parser->total_unique_scopes;
    parser->current_scope.scope_uid++;

    if (do_first) {
        ast->while_.body = parse_body(parser, true);

        if (!compare_string(this_token->value, this_token->length, "while", 5)) {
            log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
                "Expected matching 'while' keyword following 'do'\n");
            ast->while_.condition = err;
        } else {
            step(parser);
            ast->while_.condition = parse_condition(parser, NULL);
        }
    } else {
        ast->while_.condition = parse_condition(parser, NULL);
        ast->while_.body = parse_body(parser, true);
    }

    parser->current_scope.ooak_uid = ooak;
    parser->current_scope.scope_uid--;
    return ast;
}

static AST *parse_keyword_statement(Parser *parser, Keyword_Statement type) {
    AST *ast = new_ast(AST_KEYWORD_STMT, new_ast_arguments);
    step(parser);
    ast->keyword_statement = type;
    return ast;
}

static AST *parse_for(Parser *parser) {
    AST *ast = new_ast(AST_FOR, new_ast_arguments);
    step(parser);
    ast->for_.is_reverse = compare_string(this_token->value, this_token->length, "rev", 3);

    if (ast->for_.is_reverse)
        step(parser);

    const size_t ooak = parser->current_scope.ooak_uid;
    parser->current_scope.ooak_uid = ++parser->total_unique_scopes;
    parser->current_scope.scope_uid++;

    ast->for_.lhs = parse(parser); // MUST be a declaration or assignment.
    eat(parser, TOK_RANGE);

    ast->for_.range_is_inclusive = this_token->type == TOK_EQUAL;

    if (ast->for_.range_is_inclusive)
        step(parser);

    ast->for_.rhs = parse_value(parser);

    if (compare_string(this_token->value, this_token->length, "step", 4)) {
        step(parser);
        ast->for_.step = parse_value(parser);
    } else
        ast->for_.step = NULL;

    ast->for_.body = parse_body(parser, true);

    parser->current_scope.ooak_uid = ooak;
    parser->current_scope.scope_uid--;
    return ast;
}

static AST *parse_sizeof(Parser *parser) {
    AST *ast = new_ast(AST_SIZEOF, new_ast_arguments);
    step(parser);
    bool eat_parens = false;

    if (this_token->type == TOK_LPAREN) {
        step(parser);
        eat_parens = true;
    }

    if (is_data_type(parser)) {
        ast->sizeof_.data_type = parse_data_type(parser);
        ast->sizeof_.value = NULL;
    } else {
        const size_t flags = parser->flags;
        parser->flags = PARSER_NO_LEADING_VALUES;
        ast->sizeof_.value = parse_value(parser);
        parser->flags = flags;
    }

    if (eat_parens)
        eat(parser, TOK_RPAREN);

    return ast;
}

static AST *parse_cast(Parser *parser) {
    AST *ast = new_ast(AST_CAST, new_ast_arguments);
    step(parser);
    eat(parser, TOK_LPAREN);
    ast->cast.data_type = parse_data_type(parser);
    ast->cast.value = parse_value(parser);
    
    eat(parser, TOK_RPAREN);
    return ast;
}

static AST *parse_export(Parser *parser) {
    AST *ast = new_ast(AST_EXPORT, new_ast_arguments);
    ast->export.identifiers = create_list(sizeof(AST *));
    step(parser);

    while (ast->export.identifiers.count == 0 || this_token->type == TOK_COMMA) {
        if (ast->export.identifiers.count > 0)
            eat(parser, TOK_COMMA);

        AST *value = parse_value(parser);

        if (value->type != AST_IDENTIFIER && value->type != AST_GROUP)
            log(ERROR_CRITICAL, value->source.path, value->source.ln, value->source.col,
                "Expected identifier or group to export but found '%s'\n", ast_type_to_string(value->type));

        push_item(&ast->export.identifiers, value);
    }

    return ast;
}

static AST *parse_import(Parser *parser, const bool from, const bool using) {
    AST *ast = new_ast(AST_IMPORT, new_ast_arguments);
    step(parser);
    ast->import.as_name = NULL;
    ast->import.use_all_symbols = using;
    ast->import.from_identifiers = create_list(sizeof(AST *));
    ast->import.from_groups = create_list(sizeof(AST *));
    ast->import.path = calloc(2, sizeof(char));
    ast->import.path_length = 0;

#ifdef _WIN32
    const char *delim = "\\";
#else
    const char *delim = "/";
#endif
    const size_t delim_len = strlen(delim);

    while (this_token->type == TOK_ACCESS || this_token->type == TOK_RANGE || this_token->type == TOK_IDENTIFIER) {
        ast->import.path_length = strlen(ast->import.path);

        if (this_token->type == TOK_ACCESS || this_token->type == TOK_RANGE) {
            ast->import.path = realloc(ast->import.path, ast->import.path_length + this_token->length + 8);
            strcat(ast->import.path, this_token->type == TOK_ACCESS ? "/" : "../");
            step(parser);
            continue;
        }

        if (ast->import.path_length > 0 && ast->import.path[ast->import.path_length - 1] != delim[0]) {
            ast->import.path = realloc(ast->import.path, ast->import.path_length + this_token->length + delim_len + 1);
            strcat(ast->import.path, delim);
        } else
            ast->import.path = realloc(ast->import.path, ast->import.path_length + this_token->length + 1);

        strncat(ast->import.path, this_token->value, this_token->length);
        step(parser);
    }

    ast->import.path_length = strlen(ast->import.path);
    ast->import.path = realloc(ast->import.path, ast->import.path_length + 5);
    strcat(ast->import.path, ".ki");
    ast->import.path_length += 3;
    ast->import.path[ast->import.path_length] = '\0';

    if (!from) {
        if (compare_string(this_token->value, this_token->length, "as", 2)) {
            step(parser);
            ast->import.as_name = copy_string(this_token->value, this_token->length);
            ast->import.as_name_length = this_token->length;
            eat(parser, TOK_IDENTIFIER);
        }

        return ast;
    }

    if (this_token->type != TOK_KEYWORD) {
        log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
            "Expected keyword 'import' or 'using' but found '%s'\n", token_type_to_string(this_token->type));
        step(parser);
        return ast;
    }

    if (!compare_string(this_token->value, this_token->length, "import", 6) &&
            !compare_string(this_token->value, this_token->length, "using", 5)) {
        log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
            "Expected keyword 'import' or 'using' but found '%.*s'\n", (int)this_token->length, this_token->value);
        step(parser);
        return ast;
    }

    step(parser);

    while ((ast->import.from_identifiers.count == 0 && ast->import.from_groups.count == 0) || this_token->type == TOK_COMMA) {
        if (ast->import.from_identifiers.count > 0 || ast->import.from_groups.count > 0)
            eat(parser, TOK_COMMA);

        AST *value = parse_value(parser);

        if (value->type == AST_GROUP)
            push_item(&ast->import.from_groups, value);
        else {
            if (value->type != AST_IDENTIFIER)
                log(ERROR_CRITICAL, value->source.path, value->source.ln, value->source.col,
                    "Expected identifier to import but found '%s'\n", ast_type_to_string(value->type));

            push_item(&ast->import.from_identifiers, value);
        }
    }
    
    return ast;
}

static AST *parse_keyword(Parser *parser) {
    if (compare_string(this_token->value, this_token->length, "return", 6))
        return parse_return(parser);
    else if (compare_string(this_token->value, this_token->length, "asm", 3))
        return parse_asm(parser);
    else if (compare_string(this_token->value, this_token->length, "if", 2))
        return parse_if(parser);
    else if (compare_string(this_token->value, this_token->length, "else", 4)) {
        log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
            "Else statement without matching if statement\n");
        step(parser);
        return err;
    } else if (compare_string(this_token->value, this_token->length, "while", 5))
        return parse_while(parser, false);
    else if (compare_string(this_token->value, this_token->length, "do", 2))
        return parse_while(parser, true);
    else if (compare_string(this_token->value, this_token->length, "break", 5))
        return parse_keyword_statement(parser, KW_STMT_BREAK);
    else if (compare_string(this_token->value, this_token->length, "continue", 8))
        return parse_keyword_statement(parser, KW_STMT_CONTINUE);
    else if (compare_string(this_token->value, this_token->length, "for", 3))
        return parse_for(parser);
    else if (compare_string(this_token->value, this_token->length, "sizeof", 6))
        return parse_sizeof(parser);
    else if (compare_string(this_token->value, this_token->length, "cast", 4))
        return parse_cast(parser);
    else if (compare_string(this_token->value, this_token->length, "export", 6))
        return parse_export(parser);
    else if (compare_string(this_token->value, this_token->length, "import", 6))
        return parse_import(parser, false, false);
    else if (compare_string(this_token->value, this_token->length, "from", 4))
        return parse_import(parser, true, false);
    else if (compare_string(this_token->value, this_token->length, "using", 5))
        return parse_import(parser, false, true);
    else if (compare_string(this_token->value, this_token->length, "null", 4)) {
        AST *ast = new_ast(AST_NULL, new_ast_arguments);
        step(parser);
        return ast;
    } else if (compare_string(this_token->value, this_token->length, "true", 4)) {
        AST *ast = new_ast(AST_BOOL, new_ast_arguments);
        ast->bool_value = true;
        step(parser);
        return ast;
    } else if (compare_string(this_token->value, this_token->length, "false", 5)) {
        AST *ast = new_ast(AST_BOOL, new_ast_arguments);
        ast->bool_value = false;
        step(parser);
        return ast;
    }

    log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col, 
        "Invalid keyword '%.*s'\n", (int)this_token->length, this_token->value);
    step(parser);
    return err;
}

static AST *parse_custom_type(Parser *parser, const bool is_enum, AST *group) {
    AST *ast = new_ast(AST_CUSTOM_TYPE, new_ast_arguments);
    char *name = copy_string(this_token->value, this_token->length);
    size_t length = this_token->length;
    ast->custom_type.name = name;
    ast->custom_type.name_length = length;

    Custom_Type *type = new_custom_type(parser->context, is_enum ? CUST_ENUM : CUST_STRUCT,
        &parser->source, parser->current_module_uid, name, length, group == NULL ? NO_SECTION : group->group.group_uid);

    step(parser);
    step(parser);
    step(parser);
    eat(parser, TOK_LBRACE);

    while (this_token->type != TOK_EOF && this_token->type != TOK_RBRACE) {
        Source source = parser->source;
        char *member_name = copy_string(this_token->value, this_token->length);
        size_t member_length = this_token->length;
        eat(parser, TOK_IDENTIFIER);
        Data_Type data_type = create_data_type(PRIM_VOID, 0);
        AST *default_value = NULL;

        if (this_token->type == TOK_COLON) {
            assert(!is_enum);
            step(parser);
            data_type = parse_data_type(parser);
        } else {
            assert(is_enum);
            // Enum member datatypes will be set in the HIR.
        }

        if (this_token->type == TOK_EQUAL) {
            step(parser);
            default_value = parse_value(parser);
        }

        push_custom_type_member(parser->context, type->uid, &source, member_name, member_length, data_type, default_value);

        if (is_enum && this_token->type != TOK_RBRACE)
            eat(parser, TOK_COMMA);
        else if (!is_enum)
            eat(parser, TOK_SEMICOLON);
    }

    eat(parser, TOK_RBRACE);
    return ast;
}

static AST *parse_alias(Parser *parser, AST *group) {
    AST *ast = new_ast(AST_ALIAS, new_ast_arguments);
    ast->alias.lhs = new_ast(AST_ALIAS_LHS, new_ast_arguments);
    ast->alias.lhs->alias_lhs = (AST_Alias_LHS){ .name = copy_string(this_token->value, this_token->length),
        .name_length = this_token->length, .group_uid = group == NULL ? NO_SECTION : group->group.group_uid, .resolved = false };

    step(parser);
    eat(parser, TOK_COLON);
    eat(parser, TOK_IDENTIFIER);
    ast->alias.rhs = parse_value(parser);
    return ast;
}

static AST *parse_constant(Parser *parser, AST *group) {
    AST *ast = new_ast(AST_CONSTANT, new_ast_arguments);
    ast->constant.is_definition = true;
    ast->constant.group_uid = group == NULL ? NO_SECTION : group->group.group_uid;
    ast->constant.name = copy_string(this_token->value, this_token->length);
    ast->constant.name_length = this_token->length;
    step(parser);
    eat(parser, TOK_COLON);
    eat(parser, TOK_IDENTIFIER);
    ast->constant.value = parse_value(parser);
    return ast;
}

static AST *parse_declaration(Parser *parser, AST *group) {
    const Token *next2 = peek(parser, 2);

    if (compare_string(next2->value, next2->length, "enum", 4))
        return parse_custom_type(parser, true, group);
    else if (compare_string(next2->value, next2->length, "struct", 6))
        return parse_custom_type(parser, false, group);
    else if (compare_string(next2->value, next2->length, "alias", 5))
        return parse_alias(parser, group);
    else if (compare_string(next2->value, next2->length, "const", 5))
        return parse_constant(parser, group);
    
    if (group != NULL)
        log(ERROR_CRITICAL, group->source.path, group->source.ln, group->source.col,
            "Invalid use of a group for non function or data type\n");

    AST *ast = new_ast(AST_DECLARATION, new_ast_arguments);
    ast->declaration.variable_uid = parser->local_variable_count++;
    ast->declaration.name = copy_string(this_token->value, this_token->length);
    ast->declaration.name_length = this_token->length;
    eat(parser, TOK_IDENTIFIER);
    eat(parser, TOK_COLON);
    const bool infer_type = this_token->type == TOK_EQUAL;

    if (this_token->type != TOK_EQUAL)
        ast->declaration.data_type = parse_data_type(parser);

    if (this_token->type == TOK_EQUAL) {
        step(parser);
        ast->declaration.value = parse_value(parser);
    } else
        ast->declaration.value = NULL;

    if (infer_type)
        ast->declaration.data_type = create_data_type(PRIM_INFER, 0);

    push_symbol(parser->context, SYMBOL_VARIABLE, &ast->source, ast->module_uid, ast->uid, ast->declaration.name, ast->declaration.name_length,
        &ast->declaration.data_type, &ast->scope, NO_SECTION, (Symbol_Attribute){ .variable_uid = ast->declaration.variable_uid });
    return ast;
}

static AST *parse_call(Parser *parser) {
    AST *ast = new_ast(AST_CALL, new_ast_arguments);
    ast->call.name = copy_string(this_token->value, this_token->length);
    ast->call.name_length = this_token->length;
    step(parser);
    ast->call.arguments = create_list(sizeof(AST *));
    step(parser);

    const size_t flags = parser->flags;
    parser->flags |= PARSER_IN_CALL;
    parser->flags &= ~PARSER_NO_LEADING_VALUES;

    while (this_token->type != TOK_EOF && this_token->type != TOK_RPAREN) {
        if (ast->call.arguments.count > 0)
            eat(parser, TOK_COMMA);

        push_item(&ast->call.arguments, parse_value(parser));
    }

    eat(parser, TOK_RPAREN);
    parser->flags = flags;
    return ast;
}

static AST *parse_for_function_or_call(Parser *parser) {
    const size_t begin = parser->index;

    while (this_token->type != TOK_EOF && this_token->type != TOK_RPAREN)
        step(parser);

    step(parser);

    const bool is_func = !(parser->flags & PARSER_IN_VALUE) && (this_token->type == TOK_COLON || this_token->type == TOK_LBRACE);

    parser->index = begin;
    parser->current_token = &parser->tokens[parser->index];
    return is_func ? parse_function(parser, create_list(sizeof(AST *)), NULL) : parse_call(parser);
}

static AST *parse_identifier(Parser *parser) {
    const Token *next = peek(parser, 1);

    if (next->type == TOK_COLON)
        return parse_declaration(parser, NULL);
    else if (next->type == TOK_LPAREN)
        return parse_for_function_or_call(parser); 

    AST *ast = new_ast(AST_IDENTIFIER, new_ast_arguments);
    ast->identifier.resolved = false;
    ast->identifier.identifier = copy_string(this_token->value, this_token->length);
    ast->identifier.length = this_token->length;
    step(parser);
    return ast;
}

static AST *parse_assignment(Parser *parser, AST *lhs) {
    AST *ast = new_ast(AST_ASSIGNMENT, ast_location(lhs), ++parser->context->node_uid);
    ast->assignment.lhs = lhs;
    eat(parser, TOK_EQUAL);
    ast->assignment.rhs = parse_value(parser);
    return ast;
}

static AST *parse_literal(Parser *parser) {
    if (this_token->type == TOK_STRING) {
        AST *ast = new_ast(AST_STRING, new_ast_arguments);

        ast->literal.data_type = create_data_type(PRIM_CUSTOM, 1);
        ast->literal.data_type.custom_name = copy_string("string", 6);
        ast->literal.data_type.custom_length = 6;

        ast->literal.string = copy_string(this_token->value, this_token->length);
        ast->literal.string_length = this_token->length;
        step(parser);
        return ast;
    }

    AST *ast = new_ast(this_token->type == TOK_FLOAT ? AST_FLOAT : AST_INT, new_ast_arguments);
    char *endptr;

    if (this_token->type == TOK_FLOAT) {
        if (tolower(this_token->value[this_token->length - 1]) == 'f') {
            this_token->length--; // Hide the 'f'.
            errno = 0;
            float f32 = strtod(token_to_cstring(this_token), &endptr);
            this_token->length++;
            check_literal_conversion(ast->source.path, ast->source.ln, ast->source.col, errno, this_token->value, endptr);

            ast->literal.f32 = f32;
            ast->literal.data_type = create_data_type(PRIM_F32, 0);
            step(parser);
            return ast;
        }

        errno = 0;
        double f64 = strtod(token_to_cstring(this_token), &endptr);
        check_literal_conversion(ast->source.path, ast->source.ln, ast->source.col, errno, this_token->value, endptr);

        ast->literal.f64 = f64;
        ast->literal.data_type = create_data_type(PRIM_F64, 0);

        step(parser);
        return ast;
    }

    bool is_negative = false;

    for (size_t i = 0; i < this_token->length; i++) {
        if (this_token->value[i] == '-') {
            is_negative = true;
            break;
        }
    }

    if (is_negative) {
        errno = 0;
        int64_t i64 = strtoll(token_to_cstring(this_token), &endptr, 10);
        check_literal_conversion(ast->source.path, ast->source.ln, ast->source.col, errno, this_token->value, endptr);

        if (i64 <= INT32_MAX && i64 >= INT32_MIN) {
            ast->literal.i32 = (int32_t)i64;
            ast->literal.data_type = create_data_type(PRIM_I32, 0);
        } else {
            ast->literal.i64 = i64;
            ast->literal.data_type = create_data_type(PRIM_I64, 0);
        }
    } else {
        errno = 0;
        uint64_t u64 = strtoull(token_to_cstring(this_token), &endptr, 10);
        check_literal_conversion(ast->source.path, ast->source.ln, ast->source.col, errno, this_token->value, endptr);

        if (u64 <= UINT32_MAX) {
            ast->literal.u32 = (uint32_t)u64;
            ast->literal.data_type = create_data_type(PRIM_U32, 0);
        } else {
            ast->literal.u64 = u64;
            ast->literal.data_type = create_data_type(PRIM_U64, 0);
        }
    }

    eat(parser, TOK_INT);
    return ast;
}

static AST *parse_compound_math(Parser *parser, AST *lhs) {
    AST *ast = new_ast(AST_COMPOUND_MATH, new_ast_arguments);
    ast->compound_math.type = this_token->type;
    ast->compound_math.lhs = lhs;
    step(parser);
    step(parser);
    ast->compound_math.rhs = parse_value(parser);
    return ast;
}

static AST *parse_reference(Parser *parser) {
    AST *ast = new_ast(AST_REFERENCE, new_ast_arguments);
    step(parser);
    ast->reference.value = parse_value(parser);
    return ast;
}

static AST *parse_index(Parser *parser, AST *value) {
    AST *ast = new_ast(AST_INDEX, ast_location(value), ++parser->context->node_uid);
    ast->index.base = value;
    step(parser);
    ast->index.index = parse_value(parser);
    eat(parser, TOK_RSQUARE);
    return ast;
}

static AST *parse_expression(Parser *parser) {
    AST *ast = new_ast(AST_EXPRESSION, new_ast_arguments);
    step(parser);

    const size_t flags = parser->flags;
    parser->flags = 0;
    ast->expression.value = parse_value(parser);
    parser->flags = flags;

    eat(parser, TOK_RPAREN);
    return ast;
}

static AST *parse_unary(Parser *parser) {
    AST *ast = new_ast(AST_UNARY, new_ast_arguments);
    ast->unary.type = this_token->type;
    step(parser);

    // Unaries should only apply to it's own value, e.g -x + 1, unary only applies to x.
    const size_t flags = parser->flags;
    parser->flags = PARSER_NO_LEADING_VALUES;
    ast->unary.value = parse_value(parser);
    parser->flags = flags;
    return ast;
}

static AST *parse_dereference(Parser *parser) {
    AST *ast = new_ast(AST_DEREFERENCE, new_ast_arguments);
    step(parser);

    const size_t flags = parser->flags;
    parser->flags = PARSER_NO_LEADING_ASSIGNMENT;
    ast->dereference.value = parse_value(parser);
    parser->flags = flags;
    return ast;
}

static AST *parse_access(Parser *parser, AST *lhs) {
    AST *ast = new_ast(AST_ACCESS, ast_location(lhs), ++parser->context->node_uid);
    ast->access.lhs = lhs;
    step(parser);

    // We don't want this to apply in a statement, e.g a module function call because
    // this flag will also apply to the function arguments, and mess things up.
    const size_t flags = parser->flags;
    parser->flags |= PARSER_NO_LEADING_ASSIGNMENT | PARSER_NO_LEADING_VALUES;
    ast->access.rhs = parse(parser);
    parser->flags = flags;

    if (this_token->type != TOK_SEMICOLON)
        return parse_leading_values(parser, ast);

    return ast;
}

static AST *parse_decorator(Parser *parser) {
    AST *ast = new_ast(AST_DECORATOR, new_ast_arguments);
    step(parser);
    ast->decorator.name = copy_string(this_token->value, this_token->length);
    ast->decorator.name_length = this_token->length;
    eat(parser, TOK_IDENTIFIER);
    return ast;
}

static AST *parse_group_preceded_statement(Parser *parser, List decorators);

static AST *parse_decorator_preceded_function(Parser *parser, AST *group, List decorators) {
    // Sections and decorators can be passed in any order, some decorators may already have been defined,
    // or maybe they weren't in that case we would've just passed create_list().

    while (this_token->type == TOK_AT)
        push_item(&decorators, (AST *)parse_decorator(parser));

    if (this_token->type == TOK_HASHTAG)
        return parse_group_preceded_statement(parser, decorators);

    return parse_function(parser, decorators, group);
}

static AST *parse_group_list(Parser *parser, AST *group) {
    AST *ast = new_ast(AST_GROUP_LIST, ast_location(group), group->uid);
    ast->group_list.name = copy_string(group->group.name, group->group.name_length);
    ast->group_list.name_length = group->group.name_length;
    ast->group_list.statements = parse_body(parser, true);
    ast->group_list.group_uid = group->group.group_uid;
    return ast;
}

static AST *parse_group(Parser *parser) {
    AST *ast = new_ast(AST_GROUP, new_ast_arguments);
    step(parser);
    ast->group.name = copy_string(this_token->value, this_token->length);
    ast->group.name_length = this_token->length;
    eat(parser, TOK_IDENTIFIER);
    return ast;
}

static AST *parse_group_preceded_statement(Parser *parser, List decorators) {
    AST *ast = parse_group(parser);

    if (ast->type == AST_GROUP) {
        AST *symbol = find_group(parser->context, ast->group.name, ast->group.name_length, parser->current_module_uid);
        if (symbol == NULL)
            push_group(parser->context, ast);
        else {
            delete_ast(ast);
            ast = symbol;
        }

        if (this_token->type == TOK_LBRACE) {
            // TODO: Implement decorators mixed with group lists?
            delete_ast_list(&decorators);
            return parse_group_list(parser, ast);
        }
    }

    if (this_token->type == TOK_AT)
        return parse_decorator_preceded_function(parser, ast, decorators);
    else if (peek(parser, 1)->type == TOK_COLON) {
        // TODO: Allow declarations to have some kind of decorators.
        delete_ast_list(&decorators);
        return parse_declaration(parser, ast);
    }

    return parse_function(parser, decorators, ast);
}

static AST *parse_comment(Parser *parser) {
    AST *ast = new_ast(AST_COMMENT, new_ast_arguments);
    ast->comment.comment = copy_string(this_token->value, this_token->length);
    ast->comment.comment_length = this_token->length;
    step(parser);
    return ast;
}

static AST *parse_struct_initializer(Parser *parser) {
    AST *ast = new_ast(AST_STRUCT_INITIALIZER, new_ast_arguments);
    ast->struct_initializer.values = create_list(sizeof(AST *));
    ast->struct_initializer.annotations = create_list(sizeof(AST *));

    if (this_token->type != TOK_LBRACE) {
        log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
            "Expected left brace to open struct initializer but found '%s'\n", token_type_to_string(this_token->type));
        ast->struct_initializer.data_type = NO_DATA_TYPE;
        return ast;
    }

    eat(parser, TOK_LBRACE);
    size_t loops = 0;

    while (this_token->type != TOK_EOF && this_token->type != TOK_RBRACE) {
        if (loops > 0)
            eat(parser, TOK_COMMA);

        if (this_token->type == TOK_RBRACE)
            break;

        if (this_token->type != TOK_ACCESS) {
            log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
                "Expected dot for field annotation but found '%s'\n", token_type_to_string(this_token->type));

            // TOFIX: I would like to jump to the next value here, until a comma,
            // but we won't know if thats part of the initializer or a value, e.g a call.
            while (this_token->type != TOK_EOF && this_token->type != TOK_RBRACE)
                step(parser);

            break;
        }

        step(parser);

        AST *annot = new_ast(AST_IDENTIFIER, new_ast_arguments);
        annot->identifier.identifier = copy_string(this_token->value, this_token->length);
        annot->identifier.length = this_token->length;

        if (this_token->type != TOK_IDENTIFIER)
            log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
                "Expected identifier for field annotation but found '%s'\n", token_type_to_string(this_token->type));

        push_item(&ast->struct_initializer.annotations, annot);
        step(parser);
        eat(parser, TOK_EQUAL);
        push_item(&ast->struct_initializer.values, parse_value(parser));

        loops++; // Just in case a value or annotation was missing we still eat a comma.
    }

    eat(parser, TOK_RBRACE);

    if (ast->struct_initializer.values.count == 0)
        log(ERROR_CRITICAL, parser->source.path, ast->source.ln, ast->source.col, "Empty struct initializer\n");

    eat(parser, TOK_COLON);
    ast->struct_initializer.data_type = parse_data_type(parser);
    return ast;
}

static AST *parse_array_initializer(Parser *parser) {
    AST *ast = new_ast(AST_ARRAY_INITIALIZER, new_ast_arguments);
    ast->array_initializer.values = create_list(sizeof(AST *));
    ast->array_initializer.data_type = create_data_type(PRIM_VOID, 1);

    eat(parser, TOK_LSQUARE);

    while (this_token->type != TOK_EOF && this_token->type != TOK_RSQUARE) {
        if (ast->array_initializer.values.count > 0) {
            eat(parser, TOK_COMMA);

            if (this_token->type == TOK_EOF || this_token->type == TOK_RSQUARE)
                break;
        }

        push_item(&ast->array_initializer.values, parse_value(parser));
    }

    eat(parser, TOK_RSQUARE);
    return ast;
}

static AST *parse(Parser *parser) {
    AST *stmt;

    switch (this_token->type) {
        case TOK_EOF:
            stmt = nop;
            break;
        case TOK_KEYWORD:
            stmt = parse_keyword(parser);
            break;
        case TOK_IDENTIFIER:
            stmt = parse_identifier(parser);
            break;
        case TOK_INT:
        case TOK_FLOAT:
        case TOK_STRING:
            stmt = parse_literal(parser);
            break;
        case TOK_AND:
            stmt = parse_reference(parser);
            break;
        case TOK_LPAREN:
            stmt = parse_expression(parser);
            break;
        case TOK_MINUS:
        case TOK_TILDE:
        case TOK_BOOL_NOT:
            stmt = parse_unary(parser);
            break;
        case TOK_STAR:
            stmt = parse_dereference(parser);
            break;
        case TOK_AT:
            stmt = parse_decorator_preceded_function(parser, NULL, create_list(sizeof(AST *)));
            break;
        case TOK_HASHTAG:
            if (parser->flags & PARSER_IN_VALUE) // e.g, in export statement.
                stmt = parse_group(parser);
            else
                stmt = parse_group_preceded_statement(parser, create_list(sizeof(AST *)));
            break;
        case TOK_COMMENT:
            stmt = parse_comment(parser);
            break;
        case TOK_LBRACE:
            stmt = parse_struct_initializer(parser);
            break;
        case TOK_LSQUARE:
            stmt = parse_array_initializer(parser);
            break;
        default:
            log(ERROR_CRITICAL, parser->source.path, this_token->ln, this_token->col,
                "Invalid statement '%s'\n", token_type_to_string(this_token->type));
            stmt = err;
            step(parser);
            break;
    }

    while (this_token->type != TOK_EOF) {
        if (this_token->type == TOK_ACCESS) {
            stmt = parse_access(parser, stmt);
            continue;
        }

        if (parser->flags & PARSER_NO_LEADING_VALUES)
            break;

        if (!(parser->flags & PARSER_NO_LEADING_ASSIGNMENT) && !(parser->flags & PARSER_IN_VALUE)) {
            if (this_token->type == TOK_EQUAL) {
                stmt = parse_assignment(parser, stmt);
                continue;
            }

            if (is_math_operator(parser) && peek(parser, 1)->type == TOK_EQUAL) {
                stmt = parse_compound_math(parser, stmt);
                continue;
            }
        }

        if (this_token->type == TOK_LSQUARE) {
            stmt = parse_index(parser, stmt);
            continue;
        }

        break;
    }

    if (!(parser->flags & PARSER_IN_VALUE)) {
        while (this_token->type == TOK_SEMICOLON)
            step(parser);
    }

    return stmt;
}

AST *initialize_root(Context *context, char *module, size_t module_length, char *path, size_t path_length,
        char *directory, size_t directory_length, bool tokenize_comments, Parser **out_parser) {
    // Originally this was a stack allocated parser.
    Parser *prs = malloc(sizeof(Parser));
    *prs = create_parser(context, module, module_length, path, path_length, directory, directory_length, tokenize_comments);

    AST *root = new_ast(AST_ROOT, 1, 1, &prs->source, &prs->current_scope, prs->current_module_uid, ++context->node_uid);
    push_builtin_data(context, root);
    root->root.nodes = create_list(sizeof(AST *));
    *out_parser = prs;
    return root;
}

AST *parse_root(AST *initialized_root, Parser *parser) {
    while (parser->current_token->type != TOK_EOF)
        push_item(&initialized_root->root.nodes, (AST *)parse(parser));

    delete_parser(parser);
    return initialized_root;
}

