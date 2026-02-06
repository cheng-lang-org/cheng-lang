#ifndef CHENG_STAGE0C_CORE_C_EMIT_H
#define CHENG_STAGE0C_CORE_C_EMIT_H

#include "ast.h"
#include "diagnostics.h"

typedef struct {
    const char *path;
    const ChengNode *stmts;
} ChengCModuleInfo;

int cheng_emit_c(const ChengNode *root,
                 const char *filename,
                 const char *out_path,
                 ChengDiagList *diags,
                 const ChengNode *module_stmts,
                 const ChengCModuleInfo *modules,
                 size_t modules_len);

#endif
