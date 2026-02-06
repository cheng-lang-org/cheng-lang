#ifndef CHENG_STAGE0C_CORE_SEMANTICS_H
#define CHENG_STAGE0C_CORE_SEMANTICS_H

#include "ast.h"
#include "diagnostics.h"

typedef enum {
    CHENG_TYPE_UNKNOWN = 0,
    CHENG_TYPE_VOID,
    CHENG_TYPE_BOOL,
    CHENG_TYPE_INT,
    CHENG_TYPE_FLOAT,
    CHENG_TYPE_STR,
    CHENG_TYPE_CSTRING,
    CHENG_TYPE_PTR,
    CHENG_TYPE_REF,
    CHENG_TYPE_SEQ,
    CHENG_TYPE_TABLE
} ChengTypeId;

int cheng_sem_analyze(ChengNode *root, const char *filename, ChengDiagList *diags);
const char *cheng_type_name(ChengTypeId type);

#endif
