#ifndef DOCUMENTOR_H
#define DOCUMENTOR_H

#include "ast.h"
#include "context.h"
#include <stdio.h>
#include <stdbool.h>

char *generate_documentation(Context *context, AST *root, const bool exported_symbols_only);

#endif