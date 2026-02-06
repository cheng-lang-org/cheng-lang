#ifndef CHENG_STAGE0C_CORE_MACRO_EXPAND_H
#define CHENG_STAGE0C_CORE_MACRO_EXPAND_H

#include "ast.h"
#include "diagnostics.h"

int cheng_expand_macros(ChengNode *root, const char *filename, ChengDiagList *diags);

#endif
