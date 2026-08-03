#ifndef PARSER_H
#define PARSER_H

#include "context.h"
#include "ast.h"
#include <stdio.h>

typedef struct {
    Context *context;
    Source source;
    Scope current_scope;
    size_t current_module_uid;
    Token *tokens;
    size_t token_count;
    size_t index;
    Token *current_token;
    size_t local_variable_count;
    size_t total_unique_scopes;
    size_t flags;
    bool tokenize_comments;
} Parser;

AST *initialize_root(Context *context, char *module, size_t module_length, char *path, size_t path_length,
    char *directory, size_t directory_length, bool tokenize_comments, Parser **out_parser);

AST *parse_root(AST *initialized_root, Parser *parser);

#endif
