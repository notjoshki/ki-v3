#ifndef RESOLVER_H
#define RESOLVER_H

#include "context.h"
#include "ast.h"

#define KI_LIB_DIRECTORY "/usr/local/share/ki/lib"

void resolve_ast(Context *context, AST *ast);

#endif
