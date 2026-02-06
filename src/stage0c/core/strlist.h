#ifndef CHENG_STAGE0C_CORE_STRLIST_H
#define CHENG_STAGE0C_CORE_STRLIST_H

#include <stddef.h>

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} ChengStrList;

void cheng_strlist_init(ChengStrList *list);
void cheng_strlist_free(ChengStrList *list);
int cheng_strlist_has(const ChengStrList *list, const char *item);
int cheng_strlist_push_unique(ChengStrList *list, const char *item);

#endif
