#include "list.h"
#include <stdio.h>
#include <stdlib.h>

#define LIST_CAPACITY 4

List create_list(size_t item_size) {
    List list = (List){ .items = malloc(item_size * LIST_CAPACITY), .item_size = item_size,
        .count = 0, .capacity = LIST_CAPACITY };
    return list;
}

void delete_list(List *list) {
    free(list->items);
}

void push_item(List *list, void *item) {
    if (list->count + 1 > list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, list->capacity * list->item_size);
    }

    list->items[list->count++] = item;
}
