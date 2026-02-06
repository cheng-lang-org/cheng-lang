#ifndef CHENG_STAGE0C_CORE_TOKEN_H
#define CHENG_STAGE0C_CORE_TOKEN_H

#include <stddef.h>

typedef enum {
    TK_IDENT,
    TK_NUMBER,
    TK_STRING,
    TK_CHAR,
    TK_KEYWORD,
    TK_SYMBOL,
    TK_INDENT,
    TK_DEDENT,
    TK_NEWLINE,
    TK_EOF
} ChengTokenKind;

typedef struct {
    ChengTokenKind kind;
    const char *start;
    size_t len;
    char str_prefix;
    int line;
    int col;
} ChengToken;

typedef struct {
    ChengToken *data;
    size_t len;
    size_t cap;
} ChengTokenList;

void cheng_tokens_init(ChengTokenList *list);
void cheng_tokens_free(ChengTokenList *list);
int cheng_tokens_push(ChengTokenList *list, ChengToken tok);
const char *cheng_token_kind_name(ChengTokenKind kind);

#endif
