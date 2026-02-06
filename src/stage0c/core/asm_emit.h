#ifndef CHENG_STAGE0C_CORE_ASM_EMIT_H
#define CHENG_STAGE0C_CORE_ASM_EMIT_H

#include "ast.h"
#include "diagnostics.h"

typedef struct AsmModuleFilter AsmModuleFilter;

int cheng_emit_asm(const ChengNode *root,
                   const char *filename,
                   const char *out_path,
                   ChengDiagList *diags,
                   const ChengNode *module_stmts);

AsmModuleFilter *asm_module_filter_create(const ChengNode *module_stmts);
void asm_module_filter_free(AsmModuleFilter *filter);
int asm_module_filter_has_func(const AsmModuleFilter *filter, const char *name);
int asm_module_filter_has_global(const AsmModuleFilter *filter, const char *name);

#endif
