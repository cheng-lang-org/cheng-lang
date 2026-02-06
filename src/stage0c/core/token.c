#include <stdlib.h>
#include <string.h>

#include "token.h"

void cheng_tokens_init(ChengTokenList *list) {
    list->data = NULL;
    list->len = 0;
    list->cap = 0;
}

void cheng_tokens_free(ChengTokenList *list) {
    if (list == NULL) {
        return;
    }
    free(list->data);
    list->data = NULL;
    list->len = 0;
    list->cap = 0;
}

int cheng_tokens_push(ChengTokenList *list, ChengToken tok) {
    if (list->len + 1 > list->cap) {
        size_t next = list->cap == 0 ? 128 : list->cap * 2;
        ChengToken *data = (ChengToken *)realloc(list->data, next * sizeof(ChengToken));
        if (!data) {
            return -1;
        }
        list->data = data;
        list->cap = next;
    }
    list->data[list->len] = tok;
    list->len += 1;
    return 0;
}

const char *cheng_token_kind_name(ChengTokenKind kind) {
    switch (kind) {
        case TK_IDENT:
            return "ident";
        case TK_NUMBER:
            return "number";
        case TK_STRING:
            return "string";
        case TK_CHAR:
            return "char";
        case TK_KEYWORD:
            return "keyword";
        case TK_SYMBOL:
            return "symbol";
        case TK_INDENT:
            return "indent";
        case TK_DEDENT:
            return "dedent";
        case TK_NEWLINE:
            return "newline";
        case TK_EOF:
            return "eof";
    }
    return "unknown";
}
