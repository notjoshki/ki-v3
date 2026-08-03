#ifndef LEXER_H
#define LEXER_H

#include "token.h"
#include <stdio.h>
#include <stdbool.h>

typedef struct {
    char *path;
    char *source;
    size_t source_length;
    size_t index;
    char current_char;
    size_t current_ln;
    size_t current_col;
    bool tokenize_comments;
} Lexer;

Lexer create_lexer(char *path, bool tokenize_comments);
void delete_lexer(Lexer *lexer);
Token get_next_token(Lexer *lexer);

#endif
