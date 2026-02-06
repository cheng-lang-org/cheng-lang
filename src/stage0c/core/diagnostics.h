#ifndef CHENG_STAGE0C_CORE_DIAGNOSTICS_H
#define CHENG_STAGE0C_CORE_DIAGNOSTICS_H

#include <stddef.h>

typedef enum {
    CHENG_SV_ERROR = 0,
    CHENG_SV_WARNING = 1,
    CHENG_SV_HINT = 2
} ChengSeverity;

typedef struct {
    ChengSeverity severity;
    char *filename;
    int line;
    int col;
    char *message;
} ChengDiagnostic;

typedef struct {
    ChengDiagnostic *data;
    size_t len;
    size_t cap;
} ChengDiagList;

void cheng_diags_init(ChengDiagList *list);
void cheng_diags_free(ChengDiagList *list);
int cheng_diag_add(ChengDiagList *list,
                   ChengSeverity severity,
                   const char *filename,
                   int line,
                   int col,
                   const char *message);
int cheng_diags_has_error(const ChengDiagList *list);
const char *cheng_severity_label(ChengSeverity sev);

#endif
