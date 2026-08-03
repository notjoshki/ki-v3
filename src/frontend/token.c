#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

void delete_token(Token *token) {
    // These types were allocated since they weren't given a constant token value.
    if (token->type == TOK_IDENTIFIER || token->type == TOK_KEYWORD || token->type == TOK_COMMENT ||
            token->type == TOK_INT || token->type == TOK_FLOAT || token->type == TOK_STRING)
        free(token->value);
}

char *token_type_to_string(const Token_Type type) {
    switch (type) {
        case TOK_EOF: return "eof";
        case TOK_KEYWORD: return "keyword";
        case TOK_IDENTIFIER: return "identifier";
        case TOK_INT: return "int";
        case TOK_FLOAT: return "float";
        case TOK_STRING: return "string";
        case TOK_LPAREN: return "lparen";
        case TOK_RPAREN: return "rparen";
        case TOK_LBRACE: return "lbrace";
        case TOK_RBRACE: return "rbrace";
        case TOK_COLON: return "colon";
        case TOK_SEMICOLON: return "semicolon";
        case TOK_COMMA: return "comma";
        case TOK_EQUAL: return "equal";
        case TOK_PLUS: return "plus";
        case TOK_MINUS: return "minus";
        case TOK_STAR: return "star";
        case TOK_SLASH: return "slash";
        case TOK_PERCENT: return "percent";
        case TOK_AND: return "and";
        case TOK_OR: return "or";
        case TOK_XOR: return "xor";
        case TOK_SHL: return "shift left";
        case TOK_SHR: return "shift right";
        case TOK_EQ: return "boolean equal";
        case TOK_NEQ: return "boolean not equal";
        case TOK_LT: return "less than";
        case TOK_LTE: return "less than or equal";
        case TOK_GT: return "greater than";
        case TOK_GTE: return "greater than or equal";
        case TOK_BOOL_NOT: return "boolean not";
        case TOK_BOOL_AND: return "boolean and";
        case TOK_BOOL_OR: return "boolean or";
        case TOK_RANGE: return "range";
        case TOK_LSQUARE: return "lsquare";
        case TOK_RSQUARE: return "rsquare";
        case TOK_TILDE: return "tilde";
        case TOK_ACCESS: return "access";
        case TOK_AT: return "at";
        case TOK_HASHTAG: return "hashtag";
        case TOK_COMMENT: return "comment";
        default:
            assert(false);
            return "<none>";
    }
}

const char *token_to_cstring(Token *token) {
    token->value[token->length] = '\0';
    return token->value;
}
