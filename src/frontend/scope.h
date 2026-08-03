#ifndef SCOPE_H
#define SCOPE_H

#include <stdio.h>
#include <stdbool.h>

typedef struct {
    size_t level;
    size_t scope_uid;
    size_t function_uid;
} Scope;

static inline Scope create_scope(size_t level, size_t scope_uid, size_t function_uid) {
    return (Scope){ .level = level, .scope_uid = scope_uid, .function_uid = function_uid };
}

static inline bool is_in_scope(const Scope *s1, const Scope *s2) {
    return s1->function_uid == s2->function_uid && (s1->scope_uid == s2->scope_uid || s1->level < s2->level);
}

/*
0, 0, 0
x := 1;

if 1 {
    1, 1, 0
    y := x;
}

0, 0, 1
y := 2;
*/

#endif
