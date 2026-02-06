#ifndef CHENG_STAGE0C_CORE_PARSER_H
#define CHENG_STAGE0C_CORE_PARSER_H

#include <stddef.h>

#include "diagnostics.h"
#include "ast.h"
#include "strlist.h"
#include "token.h"

typedef struct {
    ChengTokenList *tokens;
    size_t pos;
    const char *filename;
    ChengDiagList *diags;
} ChengParser;

void cheng_parser_init(ChengParser *p,
                       ChengTokenList *tokens,
                       const char *filename,
                       ChengDiagList *diags);
ChengNode *cheng_parse_module(ChengParser *p);
int cheng_parse_imports(ChengParser *p, ChengStrList *out);
int cheng_collect_imports_from_ast(const ChengNode *root, ChengStrList *out);

#endif
