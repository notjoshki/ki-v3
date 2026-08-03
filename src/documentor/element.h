#ifndef ELEMENT_H
#define ELEMENT_H

#include "context.h"
#include "list.h"
#include "ast.h"

typedef enum {
    ELEM_NONE,
    ELEM_FUNCTION,
    ELEM_STRUCT,
    ELEM_ENUM,
    ELEM_ALIAS
} Element_Type;

typedef struct {
    Element_Type type;
    AST *definition;
    List comments;
    size_t group_uid;
} Element;

static inline Element create_element(Element_Type type, AST *definition, List leading_comments, size_t group_uid) {
    return (Element){ .type = type, .definition = definition, .comments = leading_comments, .group_uid = group_uid };
}

void delete_element(Element *element);

#endif
