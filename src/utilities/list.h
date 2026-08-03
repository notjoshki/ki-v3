#ifndef LIST_H
#define LIST_H

#include <stdio.h>

typedef struct {
    void **items;
    size_t item_size;
    size_t count;
    size_t capacity;
} List;

List create_list(size_t item_size);
void delete_list(List *list);
void push_item(List *list, void *item);

#endif
