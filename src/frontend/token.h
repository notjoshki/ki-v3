#ifndef TOKEN_H
#define TOKEN_H

#include <stdio.h>

typedef enum {
    TOK_EOF,
    TOK_KEYWORD,
    TOK_IDENTIFIER,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_COMMA,
    TOK_EQUAL,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_AND,
    TOK_OR,
    TOK_XOR,
    TOK_SHL,
    TOK_SHR,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,
    TOK_BOOL_NOT,
    TOK_BOOL_AND,
    TOK_BOOL_OR,
    TOK_RANGE,
    TOK_LSQUARE,
    TOK_RSQUARE,
    TOK_TILDE,
    TOK_ACCESS,
    TOK_AT,
    TOK_HASHTAG,
    TOK_COMMENT
} Token_Type;

typedef struct {
    Token_Type type;
    char *value;
    size_t length;
    size_t ln;
    size_t col;
} Token;

static inline Token create_token(Token_Type type, char *value, size_t length, size_t ln, size_t col) {
    return (Token){ .type = type, .value = value, .length = length, .ln = ln, .col = col };
}

void delete_token(Token *token);
char *token_type_to_string(const Token_Type type);
const char *token_to_cstring(Token *token);

#endif
