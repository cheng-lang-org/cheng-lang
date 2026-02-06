#include <stdlib.h>
#include <string.h>

#include "ast.h"

static char *cheng_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

ChengNode *cheng_node_new(ChengNodeKind kind, int line, int col) {
    ChengNode *node = (ChengNode *)calloc(1, sizeof(ChengNode));
    if (!node) {
        return NULL;
    }
    node->kind = kind;
    node->line = line;
    node->col = col;
    node->type_id = 0;
    node->ident = NULL;
    node->str_val = NULL;
    node->int_val = 0;
    node->float_val = 0.0;
    node->call_style = CHENG_CALL_STYLE_DEFAULT;
    return node;
}

void cheng_node_set_ident(ChengNode *node, const char *text) {
    if (!node) {
        return;
    }
    if (node->ident) {
        free(node->ident);
    }
    node->ident = cheng_strdup(text);
}

void cheng_node_set_str(ChengNode *node, const char *text) {
    if (!node) {
        return;
    }
    if (node->str_val) {
        free(node->str_val);
    }
    node->str_val = cheng_strdup(text);
}

int cheng_node_add(ChengNode *parent, ChengNode *child) {
    if (!parent || !child) {
        return -1;
    }
    if (parent->len + 1 > parent->cap) {
        size_t next = parent->cap == 0 ? 8 : parent->cap * 2;
        ChengNode **kids = (ChengNode **)realloc(parent->kids, next * sizeof(ChengNode *));
        if (!kids) {
            return -1;
        }
        parent->kids = kids;
        parent->cap = next;
    }
    parent->kids[parent->len++] = child;
    return 0;
}

void cheng_node_free(ChengNode *node) {
    if (!node) {
        return;
    }
    for (size_t i = 0; i < node->len; i++) {
        cheng_node_free(node->kids[i]);
    }
    free(node->kids);
    free(node->ident);
    free(node->str_val);
    free(node);
}
