#include <stdlib.h>
#include <string.h>

#include "diagnostics.h"

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

void cheng_diags_init(ChengDiagList *list) {
    list->data = NULL;
    list->len = 0;
    list->cap = 0;
}

void cheng_diags_free(ChengDiagList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        free(list->data[i].filename);
        free(list->data[i].message);
    }
    free(list->data);
    list->data = NULL;
    list->len = 0;
    list->cap = 0;
}

int cheng_diag_add(ChengDiagList *list,
                   ChengSeverity severity,
                   const char *filename,
                   int line,
                   int col,
                   const char *message) {
    if (list->len + 1 > list->cap) {
        size_t next = list->cap == 0 ? 16 : list->cap * 2;
        ChengDiagnostic *data =
            (ChengDiagnostic *)realloc(list->data, next * sizeof(ChengDiagnostic));
        if (!data) {
            return -1;
        }
        list->data = data;
        list->cap = next;
    }
    ChengDiagnostic *d = &list->data[list->len];
    d->severity = severity;
    d->filename = cheng_strdup(filename ? filename : "");
    d->line = line;
    d->col = col;
    d->message = cheng_strdup(message ? message : "");
    if (!d->filename || !d->message) {
        free(d->filename);
        free(d->message);
        return -1;
    }
    list->len += 1;
    return 0;
}

int cheng_diags_has_error(const ChengDiagList *list) {
    for (size_t i = 0; i < list->len; i++) {
        if (list->data[i].severity == CHENG_SV_ERROR) {
            return 1;
        }
    }
    return 0;
}

const char *cheng_severity_label(ChengSeverity sev) {
    switch (sev) {
        case CHENG_SV_ERROR:
            return "error";
        case CHENG_SV_WARNING:
            return "warning";
        case CHENG_SV_HINT:
            return "hint";
    }
    return "error";
}
