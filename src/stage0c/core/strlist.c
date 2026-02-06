#include <stdlib.h>
#include <string.h>

#include "strlist.h"

void cheng_strlist_init(ChengStrList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void cheng_strlist_free(ChengStrList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

int cheng_strlist_has(const ChengStrList *list, const char *item) {
    for (size_t i = 0; i < list->len; i++) {
        if (strcmp(list->items[i], item) == 0) {
            return 1;
        }
    }
    return 0;
}

int cheng_strlist_push_unique(ChengStrList *list, const char *item) {
    if (cheng_strlist_has(list, item)) {
        return 0;
    }
    if (list->len + 1 > list->cap) {
        size_t next = list->cap == 0 ? 8 : list->cap * 2;
        char **items = (char **)realloc(list->items, next * sizeof(char *));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->cap = next;
    }
    list->items[list->len] = strdup(item);
    if (!list->items[list->len]) {
        return -1;
    }
    list->len += 1;
    return 0;
}
