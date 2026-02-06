#ifndef CHENG_STAGE0C_CORE_LEXER_H
#define CHENG_STAGE0C_CORE_LEXER_H

#include <stddef.h>

#include "diagnostics.h"
#include "token.h"

typedef struct {
    char *content;
    size_t len;
    ChengTokenList tokens;
    ChengDiagList diags;
} ChengLexed;

int cheng_lex_file(const char *path, ChengLexed *out);
int cheng_lex_string(const char *content, const char *filename, ChengLexed *out);
void cheng_lexed_free(ChengLexed *out);

#endif
