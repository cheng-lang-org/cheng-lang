#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm_emit.h"
#include "asm_emit_layout.h"
#include "asm_emit_runtime.h"
#include "semantics.h"
#include "strlist.h"

int emit_a64_elf_file(FILE *output,
                      const ChengNode *root,
                      const char *filename,
                      ChengDiagList *diags,
                      AsmLayout *layout,
                      const AsmModuleFilter *module_filter);
int emit_a64_darwin_file(FILE *output,
                         const ChengNode *root,
                         const char *filename,
                         ChengDiagList *diags,
                         AsmLayout *layout,
                         const AsmModuleFilter *module_filter);

static const ChengNode *strip_paren_expr(const ChengNode *node);
static int hard_rt_is_const_int(const ChengNode *expr, int64_t *out_value);

typedef enum {
    ASM_TARGET_UNKNOWN = 0,
    ASM_TARGET_X86_64,
    ASM_TARGET_AARCH64,
    ASM_TARGET_RISCV64
} AsmTarget;

typedef enum {
    ASM_ABI_UNKNOWN = 0,
    ASM_ABI_DARWIN,
    ASM_ABI_ELF,
    ASM_ABI_COFF
} AsmAbi;

typedef enum {
    ASM_RET_UNKNOWN = 0,
    ASM_RET_I32,
    ASM_RET_I64,
    ASM_RET_BOOL
} AsmRetKind;

typedef struct {
    int valid;
    int is_bool;
    int64_t value;
} ConstVal;

typedef struct {
    const char *name;
    int offset;
    const ChengNode *type_expr;
} AsmVar;

typedef struct {
    AsmVar *items;
    size_t len;
    size_t cap;
} AsmVarTable;

typedef struct {
    char *name;
    const ChengNode *type_expr;
} AsmGlobalType;

typedef struct {
    AsmGlobalType *items;
    size_t len;
    size_t cap;
} AsmGlobalTypeTable;

struct AsmModuleFilter {
    ChengStrList funcs;
    ChengStrList generic_funcs;
    ChengStrList globals;
};

typedef struct {
    FILE *output;
    const char *filename;
    ChengDiagList *diags;
    int label_counter;
    int mangle_underscore;
    AsmLayout *layout;
    int hard_rt;
    AsmGlobalTypeTable globals;
    const AsmModuleFilter *module_filter;
    char *module_prefix;
    const ChengNode *root;
} AsmEmitContext;

typedef struct {
    AsmEmitContext *context;
    AsmVarTable locals;
    AsmRetKind ret_kind;
    const ChengNode *ret_type_expr;
    int ret_is_result;
    int stack_size;
    int end_label;
    int loop_start_labels[64];
    int loop_end_labels[64];
    size_t loop_depth;
} AsmFuncContext;

typedef struct {
    const ChengNode *decl;
    const char *name;
    int emit;
    int scanned;
} AsmFuncEntry;

typedef struct {
    AsmFuncEntry *items;
    size_t len;
    size_t cap;
} AsmFuncTable;

static void add_diag(ChengDiagList *diags, const char *filename, const ChengNode *node, const char *msg) {
    int line = 1;
    int col = 1;
    if (node) {
        line = node->line;
        col = node->col;
    }
    const char *use_msg = msg;
    char tagged[512];
    if (msg && strncmp(msg, "hard realtime asm backend", 25) == 0) {
        uint32_t hash = 2166136261u;
        for (const unsigned char *p = (const unsigned char *)msg; *p; p++) {
            hash ^= *p;
            hash *= 16777619u;
        }
        snprintf(tagged, sizeof(tagged), "HRT-%08x: %s", hash, msg);
        use_msg = tagged;
    }
    cheng_diag_add(diags, CHENG_SV_ERROR, filename, line, col, use_msg);
}

static int asm_str_eq_ci(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int asm_env_is_truthy(const char *value) {
    if (!value || value[0] == '\0') {
        return 0;
    }
    if (strcmp(value, "1") == 0) {
        return 1;
    }
    if (strcmp(value, "0") == 0) {
        return 0;
    }
    if (asm_str_eq_ci(value, "true") || asm_str_eq_ci(value, "yes") || asm_str_eq_ci(value, "on")) {
        return 1;
    }
    if (asm_str_eq_ci(value, "false") || asm_str_eq_ci(value, "no") || asm_str_eq_ci(value, "off")) {
        return 0;
    }
    return 0;
}

static int asm_env_is_hard_rt(void) {
    const char *value = getenv("CHENG_ASM_RT");
    if (!value || value[0] == '\0') {
        value = getenv("CHENG_HARD_RT");
    }
    if (!value || value[0] == '\0') {
        return 0;
    }
    if (asm_str_eq_ci(value, "hard") || asm_str_eq_ci(value, "hard-rt") || asm_str_eq_ci(value, "hard_rt")) {
        return 1;
    }
    return asm_env_is_truthy(value);
}

static int hard_rt_profile_level(void) {
    const char *value = getenv("CHENG_HRT_PROFILE");
    if (!value || value[0] == '\0') {
        value = getenv("CHENG_ASM_HRT_PROFILE");
    }
    if (!value || value[0] == '\0') {
        return 1;
    }
    if (asm_str_eq_ci(value, "v2") || strcmp(value, "2") == 0) {
        return 2;
    }
    if (asm_str_eq_ci(value, "v1") || strcmp(value, "1") == 0) {
        return 1;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end && end != value) {
        if (parsed < 1) {
            return 1;
        }
        return (int)parsed;
    }
    return 1;
}

typedef struct {
    size_t input_lines;
    size_t output_lines;
    size_t removed_dup_jump;
    size_t removed_fallthrough_jump;
    size_t removed_unreachable;
} AsmOptStats;

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} AsmLineList;

typedef struct {
    const char *start;
    size_t len;
} AsmSpan;

static int asm_opt_enabled(void) {
    const char *value = getenv("CHENG_ASM_OPT");
    if (!value || value[0] == '\0') {
        return 0;
    }
    if (strcmp(value, "0") == 0 || asm_str_eq_ci(value, "off") || asm_str_eq_ci(value, "none")) {
        return 0;
    }
    if (asm_str_eq_ci(value, "min") || asm_str_eq_ci(value, "basic")) {
        return 1;
    }
    return asm_env_is_truthy(value);
}

static void asm_opt_stats_init(AsmOptStats *stats) {
    if (!stats) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

static void asm_lines_init(AsmLineList *lines) {
    if (!lines) {
        return;
    }
    memset(lines, 0, sizeof(*lines));
}

static void asm_lines_free(AsmLineList *lines) {
    if (!lines) {
        return;
    }
    for (size_t i = 0; i < lines->len; i++) {
        free(lines->items[i]);
    }
    free(lines->items);
    lines->items = NULL;
    lines->len = 0;
    lines->cap = 0;
}

static int asm_lines_push(AsmLineList *lines, char *line) {
    if (!lines || !line) {
        return -1;
    }
    if (lines->len >= lines->cap) {
        size_t next_cap = lines->cap ? lines->cap * 2 : 128;
        char **next_items = (char **)realloc(lines->items, next_cap * sizeof(char *));
        if (!next_items) {
            return -1;
        }
        lines->items = next_items;
        lines->cap = next_cap;
    }
    lines->items[lines->len++] = line;
    return 0;
}

static char *asm_read_line(FILE *f) {
    if (!f) {
        return NULL;
    }
    size_t cap = 256;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return NULL;
    }
    int ch = 0;
    while ((ch = fgetc(f)) != EOF) {
        if (len + 1 >= cap) {
            size_t next_cap = cap * 2;
            char *next_buf = (char *)realloc(buf, next_cap);
            if (!next_buf) {
                free(buf);
                return NULL;
            }
            buf = next_buf;
            cap = next_cap;
        }
        buf[len++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }
    if (len == 0 && ch == EOF) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

static void asm_trim_span(const char *line, const char **out_start, const char **out_end) {
    const char *start = line ? line : "";
    const char *end = start + strlen(start);
    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    if (out_start) {
        *out_start = start;
    }
    if (out_end) {
        *out_end = end;
    }
}

static int asm_span_is_empty(const char *start, const char *end) {
    return !start || !end || start >= end;
}

static int asm_span_is_comment(const char *start, const char *end) {
    if (asm_span_is_empty(start, end)) {
        return 0;
    }
    if (start[0] == '#' || start[0] == ';') {
        return 1;
    }
    if (start[0] == '/' && (start + 1) < end && start[1] == '/') {
        return 1;
    }
    return 0;
}

static int asm_span_is_label(const char *start, const char *end, AsmSpan *out_label) {
    if (asm_span_is_empty(start, end)) {
        return 0;
    }
    if (end[-1] != ':') {
        return 0;
    }
    if (out_label) {
        out_label->start = start;
        out_label->len = (size_t)(end - start - 1);
    }
    return 1;
}

static int asm_span_is_directive(const char *start, const char *end) {
    if (asm_span_is_empty(start, end)) {
        return 0;
    }
    return start[0] == '.';
}

static AsmSpan asm_span_read_token(const char *start, const char *end) {
    AsmSpan tok = {0};
    const char *p = start;
    while (p < end && !isspace((unsigned char)*p)) {
        p++;
    }
    tok.start = start;
    tok.len = (size_t)(p - start);
    return tok;
}

static int asm_span_eq(AsmSpan a, AsmSpan b) {
    if (a.len != b.len) {
        return 0;
    }
    if (a.len == 0) {
        return 1;
    }
    return memcmp(a.start, b.start, a.len) == 0;
}

static int asm_span_eq_cstr(AsmSpan span, const char *text) {
    if (!text) {
        return 0;
    }
    size_t len = strlen(text);
    if (span.len != len) {
        return 0;
    }
    return memcmp(span.start, text, len) == 0;
}

static int asm_span_is_ret(const char *start, const char *end) {
    AsmSpan op = asm_span_read_token(start, end);
    if (op.len < 3) {
        return 0;
    }
    return strncmp(op.start, "ret", 3) == 0;
}

static int asm_span_is_uncond_jump(const char *start, const char *end, AsmTarget target, AsmSpan *out_label) {
    AsmSpan op = asm_span_read_token(start, end);
    if (op.len == 0) {
        return 0;
    }
    int is_jump = 0;
    if (target == ASM_TARGET_X86_64) {
        if (op.len >= 3 && strncmp(op.start, "jmp", 3) == 0) {
            is_jump = 1;
        }
    } else if (target == ASM_TARGET_AARCH64) {
        if (asm_span_eq_cstr(op, "b")) {
            is_jump = 1;
        }
    } else if (target == ASM_TARGET_RISCV64) {
        if (asm_span_eq_cstr(op, "j")) {
            is_jump = 1;
        }
    }
    if (!is_jump) {
        return 0;
    }
    const char *p = op.start + op.len;
    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    if (p >= end) {
        return 0;
    }
    if (*p == '*' || *p == '%') {
        return 0;
    }
    const char *label_start = p;
    while (p < end && !isspace((unsigned char)*p)) {
        p++;
    }
    if (p == label_start) {
        return 0;
    }
    if (out_label) {
        out_label->start = label_start;
        out_label->len = (size_t)(p - label_start);
    }
    return 1;
}

static int asm_jump_is_fallthrough(const AsmLineList *lines, size_t index, AsmSpan target) {
    if (!lines || index + 1 >= lines->len) {
        return 0;
    }
    for (size_t i = index + 1; i < lines->len; i++) {
        const char *line = lines->items[i];
        const char *start = NULL;
        const char *end = NULL;
        asm_trim_span(line, &start, &end);
        if (asm_span_is_empty(start, end) || asm_span_is_comment(start, end)) {
            continue;
        }
        AsmSpan label = {0};
        if (asm_span_is_label(start, end, &label)) {
            return asm_span_eq(label, target);
        }
        if (asm_span_is_directive(start, end)) {
            return 0;
        }
        return 0;
    }
    return 0;
}

static const char *asm_target_name(AsmTarget target) {
    switch (target) {
        case ASM_TARGET_X86_64:
            return "x86_64";
        case ASM_TARGET_AARCH64:
            return "aarch64";
        case ASM_TARGET_RISCV64:
            return "riscv64";
        default:
            return "unknown";
    }
}

static int asm_opt_write_report(const char *report_path,
                                const char *asm_path,
                                AsmTarget target,
                                const AsmOptStats *stats) {
    if (!report_path || report_path[0] == '\0' || !stats) {
        return 0;
    }
    FILE *f = fopen(report_path, "w");
    if (!f) {
        return -1;
    }
    fprintf(f, "# asm opt report v1\n");
    if (asm_path) {
        fprintf(f, "asm=%s\n", asm_path);
    }
    fprintf(f, "target=%s\n", asm_target_name(target));
    fprintf(f, "input_lines=%zu\n", stats->input_lines);
    fprintf(f, "output_lines=%zu\n", stats->output_lines);
    fprintf(f, "removed_dup_jump=%zu\n", stats->removed_dup_jump);
    fprintf(f, "removed_fallthrough_jump=%zu\n", stats->removed_fallthrough_jump);
    fprintf(f, "removed_unreachable=%zu\n", stats->removed_unreachable);
    fprintf(f, "rules=drop_dup_jump,drop_fallthrough_jump,drop_unreachable\n");
    fclose(f);
    return 0;
}

static int asm_optimize_file(const char *path,
                             AsmTarget target,
                             const char *filename,
                             ChengDiagList *diags,
                             AsmOptStats *stats) {
    if (!path || path[0] == '\0') {
        add_diag(diags, filename, NULL, "missing asm path for optimization");
        return -1;
    }
    FILE *in = fopen(path, "r");
    if (!in) {
        add_diag(diags, filename, NULL, "failed to read asm output for optimization");
        return -1;
    }
    AsmLineList lines;
    asm_lines_init(&lines);
    char *line = NULL;
    int ok = 1;
    while ((line = asm_read_line(in)) != NULL) {
        if (asm_lines_push(&lines, line) != 0) {
            ok = 0;
            free(line);
            break;
        }
    }
    fclose(in);
    if (!ok) {
        asm_lines_free(&lines);
        add_diag(diags, filename, NULL, "failed to allocate asm optimization buffer");
        return -1;
    }
    if (stats) {
        stats->input_lines = lines.len;
    }
    size_t path_len = strlen(path);
    char *tmp_path = (char *)malloc(path_len + 16);
    if (!tmp_path) {
        asm_lines_free(&lines);
        add_diag(diags, filename, NULL, "failed to allocate asm optimization path");
        return -1;
    }
    snprintf(tmp_path, path_len + 16, "%s.opt.tmp", path);
    FILE *out = fopen(tmp_path, "w");
    if (!out) {
        free(tmp_path);
        asm_lines_free(&lines);
        add_diag(diags, filename, NULL, "failed to write optimized asm output");
        return -1;
    }
    int after_uncond = 0;
    int last_jump_valid = 0;
    AsmSpan last_jump = {0};
    for (size_t i = 0; i < lines.len; i++) {
        const char *raw = lines.items[i];
        const char *start = NULL;
        const char *end = NULL;
        asm_trim_span(raw, &start, &end);
        if (asm_span_is_empty(start, end) || asm_span_is_comment(start, end)) {
            fputs(raw, out);
            if (stats) {
                stats->output_lines++;
            }
            continue;
        }
        AsmSpan label = {0};
        if (asm_span_is_label(start, end, &label)) {
            fputs(raw, out);
            if (stats) {
                stats->output_lines++;
            }
            after_uncond = 0;
            last_jump_valid = 0;
            continue;
        }
        if (asm_span_is_directive(start, end)) {
            fputs(raw, out);
            if (stats) {
                stats->output_lines++;
            }
            continue;
        }
        AsmSpan jump_target = {0};
        if (after_uncond) {
            if (stats) {
                stats->removed_unreachable++;
            }
            continue;
        }
        if (asm_span_is_uncond_jump(start, end, target, &jump_target)) {
            if (last_jump_valid && asm_span_eq(last_jump, jump_target)) {
                if (stats) {
                    stats->removed_dup_jump++;
                }
                continue;
            }
            if (asm_jump_is_fallthrough(&lines, i, jump_target)) {
                if (stats) {
                    stats->removed_fallthrough_jump++;
                }
                continue;
            }
            last_jump = jump_target;
            last_jump_valid = 1;
            after_uncond = 1;
            fputs(raw, out);
            if (stats) {
                stats->output_lines++;
            }
            continue;
        }
        if (asm_span_is_ret(start, end)) {
            after_uncond = 1;
            last_jump_valid = 0;
            fputs(raw, out);
            if (stats) {
                stats->output_lines++;
            }
            continue;
        }
        last_jump_valid = 0;
        fputs(raw, out);
        if (stats) {
            stats->output_lines++;
        }
    }
    fclose(out);
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        free(tmp_path);
        asm_lines_free(&lines);
        add_diag(diags, filename, NULL, "failed to replace optimized asm output");
        return -1;
    }
    free(tmp_path);
    const char *report_path = getenv("CHENG_ASM_OPT_REPORT");
    if (asm_opt_write_report(report_path, path, target, stats) != 0) {
        asm_lines_free(&lines);
        add_diag(diags, filename, NULL, "failed to write asm optimization report");
        return -1;
    }
    asm_lines_free(&lines);
    return 0;
}

static const char *asm_mangle_base(const char *name) {
    if (!name) {
        return "empty";
    }
    if (strcmp(name, "[]") == 0) {
        return "bracket";
    }
    if (strcmp(name, "[]=") == 0) {
        return "bracketEq";
    }
    if (strcmp(name, "..") == 0) {
        return "dotdot";
    }
    if (strcmp(name, "..<") == 0) {
        return "dotdotless";
    }
    if (strcmp(name, "=>") == 0) {
        return "arrow";
    }
    if (strcmp(name, ".") == 0) {
        return "dot";
    }
    if (name[0] == '\0') {
        return "empty";
    }
    return name;
}

static void asm_var_table_init(AsmVarTable *table) {
    if (!table) {
        return;
    }
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static void asm_var_table_free(AsmVarTable *table) {
    if (!table) {
        return;
    }
    free(table->items);
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static int asm_var_table_reserve(AsmVarTable *table, size_t extra) {
    if (!table) {
        return -1;
    }
    size_t needed = table->len + extra;
    if (needed <= table->cap) {
        return 0;
    }
    size_t next_cap = table->cap == 0 ? 8 : table->cap * 2;
    while (next_cap < needed) {
        next_cap *= 2;
    }
    AsmVar *next_items = (AsmVar *)realloc(table->items, next_cap * sizeof(AsmVar));
    if (!next_items) {
        return -1;
    }
    table->items = next_items;
    table->cap = next_cap;
    return 0;
}

static AsmVar *asm_var_table_find_entry(AsmVarTable *table, const char *name) {
    if (!table || !name) {
        return NULL;
    }
    for (size_t index = 0; index < table->len; index++) {
        if (table->items[index].name && strcmp(table->items[index].name, name) == 0) {
            return &table->items[index];
        }
    }
    return NULL;
}

static int asm_var_table_find(const AsmVarTable *table, const char *name, int *out_offset) {
    if (!table || !name) {
        return 0;
    }
    for (size_t index = 0; index < table->len; index++) {
        if (table->items[index].name && strcmp(table->items[index].name, name) == 0) {
            if (out_offset) {
                *out_offset = table->items[index].offset;
            }
            return 1;
        }
    }
    return 0;
}

static int asm_var_table_add(AsmVarTable *table, const char *name, const ChengNode *type_expr) {
    if (!table || !name) {
        return -1;
    }
    AsmVar *existing = asm_var_table_find_entry(table, name);
    if (existing) {
        if (!existing->type_expr && type_expr && type_expr->kind != NK_EMPTY) {
            existing->type_expr = type_expr;
        }
        return 0;
    }
    if (asm_var_table_reserve(table, 1) != 0) {
        return -1;
    }
    table->items[table->len].name = name;
    table->items[table->len].offset = 0;
    table->items[table->len].type_expr = (type_expr && type_expr->kind != NK_EMPTY) ? type_expr : NULL;
    table->len++;
    return 0;
}

static void asm_global_table_init(AsmGlobalTypeTable *table) {
    if (!table) {
        return;
    }
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static void asm_global_table_free(AsmGlobalTypeTable *table) {
    if (!table) {
        return;
    }
    for (size_t index = 0; index < table->len; index++) {
        free(table->items[index].name);
    }
    free(table->items);
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static int asm_global_table_reserve(AsmGlobalTypeTable *table, size_t extra) {
    if (!table) {
        return -1;
    }
    size_t needed = table->len + extra;
    if (needed <= table->cap) {
        return 0;
    }
    size_t next_cap = table->cap == 0 ? 8 : table->cap * 2;
    while (next_cap < needed) {
        next_cap *= 2;
    }
    AsmGlobalType *next_items = (AsmGlobalType *)realloc(table->items, next_cap * sizeof(AsmGlobalType));
    if (!next_items) {
        return -1;
    }
    table->items = next_items;
    table->cap = next_cap;
    return 0;
}

static AsmGlobalType *asm_global_table_find(AsmGlobalTypeTable *table, const char *name) {
    if (!table || !name) {
        return NULL;
    }
    for (size_t index = 0; index < table->len; index++) {
        if (table->items[index].name && strcmp(table->items[index].name, name) == 0) {
            return &table->items[index];
        }
    }
    return NULL;
}

static int asm_global_table_add(AsmGlobalTypeTable *table, const char *name, const ChengNode *type_expr) {
    if (!table || !name) {
        return -1;
    }
    AsmGlobalType *existing = asm_global_table_find(table, name);
    if (existing) {
        if (!existing->type_expr && type_expr && type_expr->kind != NK_EMPTY) {
            existing->type_expr = type_expr;
        }
        return 0;
    }
    if (asm_global_table_reserve(table, 1) != 0) {
        return -1;
    }
    char *dup = strdup(name);
    if (!dup) {
        return -1;
    }
    table->items[table->len].name = dup;
    table->items[table->len].type_expr = (type_expr && type_expr->kind != NK_EMPTY) ? type_expr : NULL;
    table->len++;
    return 0;
}

static void format_for_index_name(const ChengNode *for_node, char *buf, size_t buf_len);

static int asm_next_label(AsmEmitContext *context) {
    if (!context) {
        return -1;
    }
    int label = context->label_counter;
    context->label_counter++;
    return label;
}

static char *asm_sanitize_ident(const char *name) {
    if (!name) {
        return strdup("anon");
    }
    size_t n = strlen(name);
    if (n == 0) {
        return strdup("anon");
    }
    char *out = (char *)malloc(n + 2);
    if (!out) {
        return NULL;
    }
    size_t pos = 0;
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) {
        out[pos++] = '_';
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (isalnum(c) || c == '_') {
            out[pos++] = (char)c;
        } else {
            out[pos++] = '_';
        }
    }
    out[pos] = '\0';
    return out;
}

static char *asm_sanitize_key(const char *s) {
    if (!s || s[0] == '\0') {
        return strdup("anon");
    }
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) {
        return NULL;
    }
    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '_') {
            out[pos++] = (char)c;
        } else {
            out[pos++] = '_';
        }
    }
    if (pos == 0) {
        free(out);
        return strdup("anon");
    }
    out[pos] = '\0';
    return out;
}

static uint64_t asm_fnv1a64(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    if (!s) {
        return h;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= (uint64_t)(*p);
        h *= 1099511628211ULL;
    }
    return h;
}

static const char *asm_path_basename(const char *path) {
    if (!path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    size_t len = strlen(base);
    if (len >= 6 && strcmp(base + len - 6, ".cheng") == 0) {
        len -= 6;
    }
    if (len == 0) {
        return base;
    }
    return base;
}

static char *asm_module_prefix_from_path(const char *path) {
    if (!path) {
        return strdup("mod");
    }
    const char *base = asm_path_basename(path);
    char *base_key = asm_sanitize_key(base);
    if (!base_key) {
        return strdup("mod");
    }
    uint64_t h = asm_fnv1a64(path);
    char hash_buf[32];
    snprintf(hash_buf, sizeof(hash_buf), "m%08llx", (unsigned long long)(h & 0xffffffffULL));
    size_t needed = strlen(base_key) + strlen(hash_buf) + 2;
    char *raw = (char *)malloc(needed);
    if (!raw) {
        free(base_key);
        return NULL;
    }
    snprintf(raw, needed, "%s_%s", base_key, hash_buf);
    free(base_key);
    char *san = asm_sanitize_ident(raw);
    free(raw);
    return san ? san : NULL;
}

static char *asm_module_prefix_from_env(const AsmModuleFilter *module_filter) {
    const char *prefix_env = getenv("CHENG_ASM_MODULE_PREFIX");
    if (!prefix_env || prefix_env[0] == '\0') {
        return NULL;
    }
    if (!module_filter) {
        return NULL;
    }
    const char *module_path = getenv("CHENG_ASM_MODULE");
    if (!module_path || module_path[0] == '\0') {
        return NULL;
    }
    return asm_module_prefix_from_path(module_path);
}

static int asm_should_prefix_symbol(const AsmEmitContext *context, const char *name) {
    if (!context || !name || !context->module_prefix || !context->module_filter) {
        return 0;
    }
    if (asm_module_filter_has_func(context->module_filter, name)) {
        return 1;
    }
    if (asm_module_filter_has_global(context->module_filter, name)) {
        return 1;
    }
    return 0;
}

static void asm_emit_label(FILE *output, int label) {
    fprintf(output, ".L%d:\n", label);
}

static const char *asm_mangle_symbol(const AsmEmitContext *context,
                                     const char *name,
                                     char *buf,
                                     size_t buf_len) {
    if (!name) {
        name = "";
    }
    const char *base = asm_mangle_base(name);
    int need_prefix = asm_should_prefix_symbol(context, name);
    int need_underscore = context && context->mangle_underscore;
    if (!need_prefix && !need_underscore) {
        return base;
    }
    if (!buf || buf_len == 0) {
        return base;
    }
    if (need_prefix) {
        int n;
        if (need_underscore) {
            n = snprintf(buf, buf_len, "_%s__%s", context->module_prefix, base);
        } else {
            n = snprintf(buf, buf_len, "%s__%s", context->module_prefix, base);
        }
        if (n < 0 || (size_t)n >= buf_len) {
            return base;
        }
        return buf;
    }
    if (snprintf(buf, buf_len, "_%s", base) < 0) {
        return base;
    }
    return buf;
}

static void asm_func_table_init(AsmFuncTable *table) {
    if (!table) {
        return;
    }
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static void asm_func_table_free(AsmFuncTable *table) {
    if (!table) {
        return;
    }
    free(table->items);
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static int asm_func_table_reserve(AsmFuncTable *table, size_t extra) {
    if (!table) {
        return -1;
    }
    size_t needed = table->len + extra;
    if (needed <= table->cap) {
        return 0;
    }
    size_t next_cap = table->cap == 0 ? 8 : table->cap * 2;
    while (next_cap < needed) {
        next_cap *= 2;
    }
    AsmFuncEntry *next_items = (AsmFuncEntry *)realloc(table->items, next_cap * sizeof(AsmFuncEntry));
    if (!next_items) {
        return -1;
    }
    table->items = next_items;
    table->cap = next_cap;
    return 0;
}

static AsmFuncEntry *asm_func_table_find(AsmFuncTable *table, const char *name) {
    if (!table || !name) {
        return NULL;
    }
    for (size_t index = 0; index < table->len; index++) {
        if (table->items[index].name && strcmp(table->items[index].name, name) == 0) {
            return &table->items[index];
        }
    }
    return NULL;
}

static int asm_func_table_add(AsmFuncTable *table, const ChengNode *decl) {
    if (!table || !decl || decl->kind != NK_FN_DECL || decl->len < 1) {
        return -1;
    }
    const ChengNode *name_node = decl->kids[0];
    if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
        return -1;
    }
    AsmFuncEntry *existing = asm_func_table_find(table, name_node->ident);
    if (existing) {
        existing->decl = decl;
        existing->name = name_node->ident;
        existing->emit = 0;
        existing->scanned = 0;
        return 0;
    }
    if (asm_func_table_reserve(table, 1) != 0) {
        return -1;
    }
    table->items[table->len].decl = decl;
    table->items[table->len].name = name_node->ident;
    table->items[table->len].emit = 0;
    table->items[table->len].scanned = 0;
    table->len++;
    return 0;
}

static int asm_func_decl_has_body(const ChengNode *decl) {
    if (!decl || decl->kind != NK_FN_DECL || decl->len < 4) {
        return 0;
    }
    const ChengNode *body = decl->kids[3];
    if (!body || body->kind == NK_EMPTY) {
        return 0;
    }
    if (body->kind == NK_STMT_LIST && body->len == 0) {
        return 0;
    }
    return 1;
}

static int asm_func_is_runtime_name(const char *name) {
    const char *runtime_sym = NULL;
    return asm_runtime_lookup_symbol(name, &runtime_sym);
}

static int hard_rt_check_reserved_ident(const ChengNode *node,
                                        const char *filename,
                                        ChengDiagList *diags);
static int hard_rt_is_const_int(const ChengNode *expr, int64_t *out_value);

static const ChengNode *call_target_node(const ChengNode *callee) {
    const ChengNode *node = callee;
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_BRACKET_EXPR && node->len > 0) {
        node = node->kids[0];
    }
    if (node && node->kind == NK_DOT_EXPR && node->len > 1) {
        node = node->kids[1];
    }
    return node;
}

static int asm_func_is_importc_name(const ChengStrList *list, const char *name) {
    if (!list || !name) {
        return 0;
    }
    return cheng_strlist_has(list, name);
}

static int asm_func_entry_should_emit(const AsmFuncEntry *entry, const ChengStrList *importc) {
    if (!entry || !entry->emit) {
        return 0;
    }
    if (asm_func_is_runtime_name(entry->name) || asm_func_is_importc_name(importc, entry->name)) {
        return 0;
    }
    if (!asm_func_decl_has_body(entry->decl)) {
        return 0;
    }
    return 1;
}

static void asm_func_table_mark_calls(const ChengNode *node,
                                      AsmFuncTable *table,
                                      const ChengStrList *importc,
                                      int *changed) {
    if (!node || !table) {
        return;
    }
    if (node->kind == NK_CALL && node->len > 0 && node->kids[0]) {
        const ChengNode *callee = call_target_node(node->kids[0]);
        const char *callee_name =
            callee && (callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) && callee->ident ? callee->ident : NULL;
        AsmFuncEntry *entry = callee_name ? asm_func_table_find(table, callee_name) : NULL;
        if (entry && !entry->emit) {
            if (asm_func_is_runtime_name(entry->name) ||
                asm_func_is_importc_name(importc, entry->name) ||
                !asm_func_decl_has_body(entry->decl)) {
                goto continue_walk;
            }
            entry->emit = 1;
            if (changed) {
                *changed = 1;
            }
        }
    }
continue_walk:
    for (size_t index = 0; index < node->len; index++) {
        asm_func_table_mark_calls(node->kids[index], table, importc, changed);
    }
}

static int hex_digit_val(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static int str_contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || needle[0] == '\0') {
        return 0;
    }
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i]) {
            unsigned char hc = (unsigned char)p[i];
            unsigned char nc = (unsigned char)needle[i];
            if ((unsigned char)tolower(hc) != (unsigned char)tolower(nc)) {
                break;
            }
            i++;
        }
        if (i == nlen) {
            return 1;
        }
    }
    return 0;
}

static int decode_char_lit(const char *text, int *out) {
    if (!text || !out) {
        return 0;
    }
    size_t len = strlen(text);
    if (len == 1) {
        *out = (unsigned char)text[0];
        return 1;
    }
    if (len == 2 && text[0] == '\\') {
        switch (text[1]) {
            case 'n': *out = 10; return 1;
            case 'r': *out = 13; return 1;
            case 't': *out = 9; return 1;
            case '0': *out = 0; return 1;
            case '\\': *out = 92; return 1;
            case '"': *out = 34; return 1;
            case '\'': *out = 39; return 1;
            default:
                *out = (unsigned char)text[1];
                return 0;
        }
    }
    if (len == 4 && text[0] == '\\' && text[1] == 'x') {
        int a = hex_digit_val(text[2]);
        int b = hex_digit_val(text[3]);
        if (a >= 0 && b >= 0) {
            *out = (a * 16) + b;
            return 1;
        }
    }
    if (len > 0) {
        *out = (unsigned char)text[0];
    }
    return 0;
}

static int is_simple_expr_stmt_kind(int kind) {
    switch (kind) {
        case NK_CALL:
        case NK_DOT_EXPR:
        case NK_BRACKET_EXPR:
        case NK_CAST:
        case NK_IDENT:
        case NK_STR_LIT:
        case NK_INT_LIT:
        case NK_BOOL_LIT:
        case NK_NIL_LIT:
        case NK_CHAR_LIT:
        case NK_INFIX:
        case NK_PREFIX:
            return 1;
        default:
            return 0;
    }
}

static const ChengNode *find_main_fn(const ChengNode *root) {
    if (!root) {
        return NULL;
    }
    const ChengNode *list = root;
    if (root->kind == NK_MODULE && root->len > 0) {
        list = root->kids[0];
    }
    if (!list || list->kind != NK_STMT_LIST) {
        return NULL;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt || stmt->kind != NK_FN_DECL || stmt->len < 4) {
            continue;
        }
        const ChengNode *name = stmt->kids[0];
        if (name && name->kind == NK_IDENT && name->ident && strcmp(name->ident, "main") == 0) {
            return stmt;
        }
    }
    return NULL;
}

static const ChengNode *extract_return_expr(const ChengNode *body) {
    if (!body) {
        return NULL;
    }
    const ChengNode *stmt = body;
    if (body->kind == NK_STMT_LIST && body->len == 1) {
        stmt = body->kids[0];
    }
    if (!stmt) {
        return NULL;
    }
    if (stmt->kind == NK_RETURN && stmt->len > 0) {
        return stmt->kids[0];
    }
    if (is_simple_expr_stmt_kind(stmt->kind)) {
        return stmt;
    }
    return NULL;
}

static int eval_const_expr(const ChengNode *expr, ConstVal *out, const char *filename, ChengDiagList *diags) {
    if (!expr || !out) {
        add_diag(diags, filename, expr, "asm backend expects a constant expression");
        return 0;
    }
    switch (expr->kind) {
        case NK_PAR:
            if (expr->len > 0) {
                return eval_const_expr(expr->kids[0], out, filename, diags);
            }
            add_diag(diags, filename, expr, "empty paren expression");
            return 0;
        case NK_INT_LIT:
            out->valid = 1;
            out->is_bool = 0;
            out->value = expr->int_val;
            return 1;
        case NK_CHAR_LIT: {
            int ch = 0;
            if (!decode_char_lit(expr->str_val, &ch)) {
                add_diag(diags, filename, expr, "invalid char literal");
                return 0;
            }
            out->valid = 1;
            out->is_bool = 0;
            out->value = (int64_t)ch;
            return 1;
        }
        case NK_BOOL_LIT: {
            int val = 0;
            if (expr->ident && strcmp(expr->ident, "true") == 0) {
                val = 1;
            }
            out->valid = 1;
            out->is_bool = 1;
            out->value = val;
            return 1;
        }
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0] || !expr->kids[0]->ident) {
                add_diag(diags, filename, expr, "invalid prefix expression");
                return 0;
            }
            const char *op = expr->kids[0]->ident;
            ConstVal rhs = {0};
            if (!eval_const_expr(expr->kids[1], &rhs, filename, diags)) {
                return 0;
            }
            if (strcmp(op, "!") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (rhs.value == 0) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, "+") == 0) {
                *out = rhs;
                out->is_bool = 0;
                return 1;
            }
            if (strcmp(op, "-") == 0) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = -rhs.value;
                return 1;
            }
            if (strcmp(op, "~") == 0) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = ~rhs.value;
                return 1;
            }
            add_diag(diags, filename, expr, "unsupported prefix operator in asm backend");
            return 0;
        }
        case NK_INFIX: {
            if (expr->len < 3 || !expr->kids[0] || !expr->kids[0]->ident) {
                add_diag(diags, filename, expr, "invalid infix expression");
                return 0;
            }
            const char *op = expr->kids[0]->ident;
            ConstVal lhs = {0};
            ConstVal rhs = {0};
            if (!eval_const_expr(expr->kids[1], &lhs, filename, diags) ||
                !eval_const_expr(expr->kids[2], &rhs, filename, diags)) {
                return 0;
            }
            int64_t a = lhs.value;
            int64_t b = rhs.value;
            if (strcmp(op, "&&") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (a != 0 && b != 0) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, "||") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (a != 0 || b != 0) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, "==") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (a == b) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, "!=") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (a != b) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, "<") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (a < b) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, "<=") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (a <= b) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, ">") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (a > b) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, ">=") == 0) {
                out->valid = 1;
                out->is_bool = 1;
                out->value = (a >= b) ? 1 : 0;
                return 1;
            }
            if (strcmp(op, "+") == 0) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = a + b;
                return 1;
            }
            if (strcmp(op, "-") == 0) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = a - b;
                return 1;
            }
            if (strcmp(op, "*") == 0) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = a * b;
                return 1;
            }
            if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
                if (b == 0) {
                    add_diag(diags, filename, expr, "division by zero in const expression");
                    return 0;
                }
                out->valid = 1;
                out->is_bool = 0;
                out->value = a / b;
                return 1;
            }
            if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
                if (b == 0) {
                    add_diag(diags, filename, expr, "modulo by zero in const expression");
                    return 0;
                }
                out->valid = 1;
                out->is_bool = 0;
                out->value = a % b;
                return 1;
            }
            if (strcmp(op, "&") == 0) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = a & b;
                return 1;
            }
            if (strcmp(op, "|") == 0) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = a | b;
                return 1;
            }
            if (strcmp(op, "^") == 0) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = a ^ b;
                return 1;
            }
            if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
                if (b < 0 || b > 63) {
                    add_diag(diags, filename, expr, "shift out of range in const expression");
                    return 0;
                }
                out->valid = 1;
                out->is_bool = 0;
                if (strcmp(op, "<<") == 0) {
                    out->value = a << b;
                } else {
                    out->value = a >> b;
                }
                return 1;
            }
            add_diag(diags, filename, expr, "unsupported infix operator in asm backend");
            return 0;
        }
        default:
            add_diag(diags, filename, expr, "asm backend only supports constant expressions");
            return 0;
    }
}

static AsmRetKind ret_kind_from_type(const ChengNode *node) {
    if (!node) {
        return ASM_RET_UNKNOWN;
    }
    if (node->kind == NK_IDENT && node->ident) {
        if (strcmp(node->ident, "int32") == 0) {
            return ASM_RET_I32;
        }
        if (strcmp(node->ident, "int64") == 0) {
            return ASM_RET_I64;
        }
        if (strcmp(node->ident, "bool") == 0) {
            return ASM_RET_BOOL;
        }
        if (strcmp(node->ident, "char") == 0) {
            return ASM_RET_I32;
        }
    }
    if (node->kind == NK_EMPTY) {
        return ASM_RET_UNKNOWN;
    }
    return ASM_RET_UNKNOWN;
}

static AsmTarget detect_target(const char *target) {
    if (target && target[0] != '\0') {
        if (str_contains_ci(target, "x86_64") || str_contains_ci(target, "amd64")) {
            return ASM_TARGET_X86_64;
        }
        if (str_contains_ci(target, "aarch64") || str_contains_ci(target, "arm64")) {
            return ASM_TARGET_AARCH64;
        }
        if (str_contains_ci(target, "riscv64")) {
            return ASM_TARGET_RISCV64;
        }
        return ASM_TARGET_UNKNOWN;
    }
#if defined(__x86_64__) || defined(_M_X64)
    return ASM_TARGET_X86_64;
#elif defined(__aarch64__)
    return ASM_TARGET_AARCH64;
#elif defined(__riscv) && (__riscv_xlen == 64)
    return ASM_TARGET_RISCV64;
#else
    return ASM_TARGET_UNKNOWN;
#endif
}

static AsmAbi detect_abi(const char *target, const char *abi_env) {
    if (abi_env && abi_env[0] != '\0') {
        if (str_contains_ci(abi_env, "darwin") || str_contains_ci(abi_env, "mac") || str_contains_ci(abi_env, "ios")) {
            return ASM_ABI_DARWIN;
        }
        if (str_contains_ci(abi_env, "coff") || str_contains_ci(abi_env, "windows") ||
            str_contains_ci(abi_env, "mingw") || str_contains_ci(abi_env, "msvc")) {
            return ASM_ABI_COFF;
        }
        if (str_contains_ci(abi_env, "elf") || str_contains_ci(abi_env, "linux") ||
            str_contains_ci(abi_env, "android") || str_contains_ci(abi_env, "ohos") ||
            str_contains_ci(abi_env, "openharmony") || str_contains_ci(abi_env, "harmony")) {
            return ASM_ABI_ELF;
        }
    }
    if (target && target[0] != '\0') {
        if (str_contains_ci(target, "apple") || str_contains_ci(target, "darwin") ||
            str_contains_ci(target, "macos") || str_contains_ci(target, "ios")) {
            return ASM_ABI_DARWIN;
        }
        if (str_contains_ci(target, "windows") || str_contains_ci(target, "win32") ||
            str_contains_ci(target, "mingw") || str_contains_ci(target, "msvc")) {
            return ASM_ABI_COFF;
        }
        if (str_contains_ci(target, "android") || str_contains_ci(target, "linux") ||
            str_contains_ci(target, "gnu") || str_contains_ci(target, "musl") ||
            str_contains_ci(target, "elf") || str_contains_ci(target, "ohos") ||
            str_contains_ci(target, "openharmony") || str_contains_ci(target, "harmony")) {
            return ASM_ABI_ELF;
        }
    }
#if defined(_WIN32)
    return ASM_ABI_COFF;
#elif defined(__APPLE__)
    return ASM_ABI_DARWIN;
#else
    return ASM_ABI_ELF;
#endif
}

static void emit_aarch64_imm(FILE *f, const char *reg, uint64_t value, int is32) {
    uint16_t part0 = (uint16_t)(value & 0xffff);
    uint16_t part1 = (uint16_t)((value >> 16) & 0xffff);
    uint16_t part2 = (uint16_t)((value >> 32) & 0xffff);
    uint16_t part3 = (uint16_t)((value >> 48) & 0xffff);
    fprintf(f, "  movz %s, #%u\n", reg, (unsigned)part0);
    if (part1 != 0) {
        fprintf(f, "  movk %s, #%u, lsl #16\n", reg, (unsigned)part1);
    }
    if (!is32) {
        if (part2 != 0) {
            fprintf(f, "  movk %s, #%u, lsl #32\n", reg, (unsigned)part2);
        }
        if (part3 != 0) {
            fprintf(f, "  movk %s, #%u, lsl #48\n", reg, (unsigned)part3);
        }
    }
}

static const ChengNode *get_top_level_list(const ChengNode *root) {
    if (!root) {
        return NULL;
    }
    if (root->kind == NK_MODULE && root->len > 0) {
        return root->kids[0];
    }
    return root;
}

static const ChengNode *find_fn_decl(const ChengNode *root, const char *name) {
    if (!root || !name || name[0] == '\0') {
        return NULL;
    }
    const ChengNode *list = get_top_level_list(root);
    if (!list || list->kind != NK_STMT_LIST) {
        return NULL;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt || stmt->kind != NK_FN_DECL || stmt->len == 0) {
            continue;
        }
        const ChengNode *decl_name = stmt->kids[0];
        if (decl_name && decl_name->kind == NK_IDENT && decl_name->ident &&
            strcmp(decl_name->ident, name) == 0) {
            return stmt;
        }
    }
    return NULL;
}

static const char *pragma_importc_name(const ChengNode *node) {
    if (!node || node->kind != NK_PRAGMA || node->len == 0) {
        return NULL;
    }
    const ChengNode *expr = node->kids[0];
    if (!expr || expr->kind != NK_CALL || expr->len < 2) {
        return NULL;
    }
    const ChengNode *callee = expr->kids[0];
    if (!callee || callee->kind != NK_IDENT || !callee->ident) {
        return NULL;
    }
    if (strcmp(callee->ident, "importc") != 0) {
        return NULL;
    }
    const ChengNode *arg = expr->kids[1];
    if (arg && arg->kind == NK_CALL_ARG) {
        if (arg->len > 1) {
            arg = arg->kids[1];
        } else if (arg->len > 0) {
            arg = arg->kids[0];
        }
    }
    while (arg && arg->kind == NK_PAR && arg->len == 1) {
        arg = arg->kids[0];
    }
    if (!arg || arg->kind != NK_STR_LIT || !arg->str_val) {
        return NULL;
    }
    return arg->str_val;
}

static void collect_importc_names(const ChengNode *list, ChengStrList *out) {
    if (!list || list->kind != NK_STMT_LIST || !out) {
        return;
    }
    const char *pending = NULL;
    const ChengNode *last_fn = NULL;
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_PRAGMA) {
            const char *imp = pragma_importc_name(stmt);
            if (imp) {
                if (last_fn && stmt->line == last_fn->line) {
                    if (last_fn->len > 0 && last_fn->kids[0] && last_fn->kids[0]->kind == NK_IDENT &&
                        last_fn->kids[0]->ident) {
                        cheng_strlist_push_unique(out, last_fn->kids[0]->ident);
                    }
                    pending = NULL;
                } else {
                    pending = imp;
                }
            }
            continue;
        }
        if (stmt->kind == NK_FN_DECL && pending) {
            if (stmt->len > 0 && stmt->kids[0] && stmt->kids[0]->kind == NK_IDENT &&
                stmt->kids[0]->ident) {
                cheng_strlist_push_unique(out, stmt->kids[0]->ident);
            }
            pending = NULL;
            last_fn = stmt;
            continue;
        }
        if (stmt->kind == NK_FN_DECL) {
            last_fn = stmt;
            continue;
        }
        pending = NULL;
        last_fn = NULL;
    }
}

static int asm_is_generic_params(const ChengNode *node) {
    return node && node->kind == NK_GENERIC_PARAMS && node->len > 0;
}

static int asm_is_generic_fn_decl(const ChengNode *node) {
    if (!node || node->kind != NK_FN_DECL || node->len <= 4) {
        return 0;
    }
    return asm_is_generic_params(node->kids[4]);
}

static const ChengNode *pattern_ident_node(const ChengNode *pattern) {
    if (!pattern) {
        return NULL;
    }
    if (pattern->kind == NK_PATTERN && pattern->len > 0) {
        const ChengNode *maybe_ident = pattern->kids[0];
        if (maybe_ident && maybe_ident->kind == NK_IDENT) {
            return maybe_ident;
        }
    }
    if (pattern->kind == NK_IDENT) {
        return pattern;
    }
    return NULL;
}

AsmModuleFilter *asm_module_filter_create(const ChengNode *module_stmts) {
    AsmModuleFilter *filter = (AsmModuleFilter *)calloc(1, sizeof(*filter));
    if (!filter) {
        return NULL;
    }
    cheng_strlist_init(&filter->funcs);
    cheng_strlist_init(&filter->generic_funcs);
    cheng_strlist_init(&filter->globals);
    if (!module_stmts || module_stmts->kind != NK_STMT_LIST) {
        return filter;
    }
    for (size_t i = 0; i < module_stmts->len; i++) {
        const ChengNode *stmt = module_stmts->kids[i];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_STMT_LIST) {
            for (size_t j = 0; j < stmt->len; j++) {
                const ChengNode *child = stmt->kids[j];
                if (!child) {
                    continue;
                }
                if (child->kind == NK_FN_DECL && child->len > 0 &&
                    child->kids[0] && child->kids[0]->kind == NK_IDENT &&
                    child->kids[0]->ident) {
                    cheng_strlist_push_unique(&filter->funcs, child->kids[0]->ident);
                    if (asm_is_generic_fn_decl(child)) {
                        const char *base = asm_mangle_base(child->kids[0]->ident);
                        if (base && base[0] != '\0') {
                            cheng_strlist_push_unique(&filter->generic_funcs, base);
                        }
                    }
                } else if (child->kind == NK_VAR || child->kind == NK_LET || child->kind == NK_CONST) {
                    const ChengNode *ident = pattern_ident_node(child->len > 0 ? child->kids[0] : NULL);
                    if (ident && ident->ident) {
                        cheng_strlist_push_unique(&filter->globals, ident->ident);
                    }
                }
            }
            continue;
        }
        if (stmt->kind == NK_FN_DECL && stmt->len > 0 &&
            stmt->kids[0] && stmt->kids[0]->kind == NK_IDENT &&
            stmt->kids[0]->ident) {
            cheng_strlist_push_unique(&filter->funcs, stmt->kids[0]->ident);
            if (asm_is_generic_fn_decl(stmt)) {
                const char *base = asm_mangle_base(stmt->kids[0]->ident);
                if (base && base[0] != '\0') {
                    cheng_strlist_push_unique(&filter->generic_funcs, base);
                }
            }
        } else if (stmt->kind == NK_VAR || stmt->kind == NK_LET || stmt->kind == NK_CONST) {
            const ChengNode *ident = pattern_ident_node(stmt->len > 0 ? stmt->kids[0] : NULL);
            if (ident && ident->ident) {
                cheng_strlist_push_unique(&filter->globals, ident->ident);
            }
        }
    }
    return filter;
}

void asm_module_filter_free(AsmModuleFilter *filter) {
    if (!filter) {
        return;
    }
    cheng_strlist_free(&filter->funcs);
    cheng_strlist_free(&filter->generic_funcs);
    cheng_strlist_free(&filter->globals);
    free(filter);
}

int asm_module_filter_has_func(const AsmModuleFilter *filter, const char *name) {
    if (!filter || !name) {
        return 0;
    }
    if (cheng_strlist_has((ChengStrList *)&filter->funcs, name)) {
        return 1;
    }
    for (size_t i = 0; i < filter->generic_funcs.len; i++) {
        const char *prefix = filter->generic_funcs.items[i];
        if (!prefix || prefix[0] == '\0') {
            continue;
        }
        size_t len = strlen(prefix);
        if (strncmp(name, prefix, len) == 0 && name[len] == '_') {
            return 1;
        }
    }
    return 0;
}

int asm_module_filter_has_global(const AsmModuleFilter *filter, const char *name) {
    if (!filter || !name) {
        return 0;
    }
    return cheng_strlist_has((ChengStrList *)&filter->globals, name);
}

static int asm_module_filter_write(const AsmModuleFilter *filter,
                                   const char *module_path,
                                   const char *out_path,
                                   ChengDiagList *diags,
                                   const char *filename,
                                   const ChengNode *node) {
    if (!filter || !out_path || out_path[0] == '\0') {
        return 0;
    }
    FILE *out = fopen(out_path, "w");
    if (!out) {
        add_diag(diags, filename, node, "failed to write module symbol output");
        return -1;
    }
    fprintf(out, "# cheng modsym v1\n");
    if (module_path && module_path[0] != '\0') {
        fprintf(out, "module=%s\n", module_path);
    }
    for (size_t i = 0; i < filter->funcs.len; i++) {
        const char *name = filter->funcs.items[i];
        if (name && name[0] != '\0') {
            fprintf(out, "func %s\n", name);
        }
    }
    for (size_t i = 0; i < filter->generic_funcs.len; i++) {
        const char *name = filter->generic_funcs.items[i];
        if (name && name[0] != '\0') {
            fprintf(out, "generic_func %s\n", name);
        }
    }
    for (size_t i = 0; i < filter->globals.len; i++) {
        const char *name = filter->globals.items[i];
        if (name && name[0] != '\0') {
            fprintf(out, "global %s\n", name);
        }
    }
    fclose(out);
    return 0;
}

static int collect_global_types_from_stmt(const ChengNode *stmt,
                                          AsmGlobalTypeTable *globals,
                                          const char *filename,
                                          ChengDiagList *diags);

static int collect_global_types(const ChengNode *list,
                                AsmGlobalTypeTable *globals,
                                const char *filename,
                                ChengDiagList *diags) {
    if (!list || !globals) {
        return 0;
    }
    if (list->kind != NK_STMT_LIST) {
        return collect_global_types_from_stmt(list, globals, filename, diags);
    }
    for (size_t index = 0; index < list->len; index++) {
        if (collect_global_types_from_stmt(list->kids[index], globals, filename, diags) != 0) {
            return -1;
        }
    }
    return 0;
}

static int collect_global_types_from_stmt(const ChengNode *stmt,
                                          AsmGlobalTypeTable *globals,
                                          const char *filename,
                                          ChengDiagList *diags) {
    if (!stmt || !globals) {
        return 0;
    }
    switch (stmt->kind) {
        case NK_VAR:
        case NK_LET:
        case NK_CONST: {
            if (stmt->len < 1) {
                return 0;
            }
            const ChengNode *pattern = stmt->kids[0];
            const ChengNode *ident = pattern_ident_node(pattern);
            if (!ident || !ident->ident) {
                return 0;
            }
            const ChengNode *type_expr = stmt->len > 1 ? stmt->kids[1] : NULL;
            if (asm_global_table_add(globals, ident->ident, type_expr) != 0) {
                add_diag(diags, filename, stmt, "failed to collect global type");
                return -1;
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int collect_locals_from_stmt(const ChengNode *stmt,
                                    AsmVarTable *locals,
                                    const char *filename,
                                    ChengDiagList *diags);
static void format_case_tmp_name(const ChengNode *case_node, char *buf, size_t buf_len);
static const char *case_pattern_ident(const ChengNode *pattern);
static int case_pattern_is_wildcard(const char *name);
static void format_case_tmp_name(const ChengNode *case_node, char *buf, size_t buf_len);
static const char *case_pattern_ident(const ChengNode *pattern);
static int case_pattern_is_wildcard(const char *name);

static int collect_case_pattern_bindings(const ChengNode *pattern,
                                         AsmVarTable *locals,
                                         const char *filename,
                                         ChengDiagList *diags) {
    if (!pattern || !locals) {
        return 0;
    }
    if (pattern->kind == NK_PATTERN) {
        for (size_t i = 0; i < pattern->len; i++) {
            if (collect_case_pattern_bindings(pattern->kids[i], locals, filename, diags) != 0) {
                return -1;
            }
        }
        return 0;
    }
    const char *name = case_pattern_ident(pattern);
    if (!name || case_pattern_is_wildcard(name)) {
        return 0;
    }
    if (asm_var_table_add(locals, name, NULL) != 0) {
        add_diag(diags, filename, pattern, "failed to allocate case binding");
        return -1;
    }
    return 0;
}

static int collect_locals_from_list(const ChengNode *list,
                                    AsmVarTable *locals,
                                    const char *filename,
                                    ChengDiagList *diags) {
    if (!list) {
        return 0;
    }
    if (list->kind != NK_STMT_LIST) {
        return collect_locals_from_stmt(list, locals, filename, diags);
    }
    for (size_t index = 0; index < list->len; index++) {
        if (collect_locals_from_stmt(list->kids[index], locals, filename, diags) != 0) {
            return -1;
        }
    }
    return 0;
}

static int collect_locals_from_stmt(const ChengNode *stmt,
                                    AsmVarTable *locals,
                                    const char *filename,
                                    ChengDiagList *diags) {
    if (!stmt) {
        return 0;
    }
    switch (stmt->kind) {
        case NK_VAR:
        case NK_LET:
        case NK_CONST: {
            if (stmt->len < 1) {
                add_diag(diags, filename, stmt, "invalid local declaration");
                return -1;
            }
            const ChengNode *pattern = stmt->kids[0];
            const ChengNode *ident = pattern_ident_node(pattern);
            if (!ident || !ident->ident) {
                add_diag(diags, filename, stmt, "unsupported local pattern in asm backend");
                return -1;
            }
            const ChengNode *type_expr = stmt->len > 1 ? stmt->kids[1] : NULL;
            if (asm_var_table_add(locals, ident->ident, type_expr) != 0) {
                add_diag(diags, filename, stmt, "failed to allocate local");
                return -1;
            }
            return 0;
        }
        case NK_IF: {
            if (stmt->len > 1) {
                if (collect_locals_from_list(stmt->kids[1], locals, filename, diags) != 0) {
                    return -1;
                }
            }
            if (stmt->len > 2 && stmt->kids[2] && stmt->kids[2]->kind == NK_ELSE && stmt->kids[2]->len > 0) {
                if (collect_locals_from_list(stmt->kids[2]->kids[0], locals, filename, diags) != 0) {
                    return -1;
                }
            }
            return 0;
        }
        case NK_DEFER: {
            if (stmt->len > 0) {
                return collect_locals_from_list(stmt->kids[0], locals, filename, diags);
            }
            return 0;
        }
        case NK_WHILE: {
            if (stmt->len > 1) {
                return collect_locals_from_list(stmt->kids[1], locals, filename, diags);
            }
            return 0;
        }
        case NK_FOR: {
            if (stmt->len < 3) {
                add_diag(diags, filename, stmt, "invalid for statement");
                return -1;
            }
            const ChengNode *pattern = stmt->kids[0];
            const ChengNode *ident = pattern_ident_node(pattern);
            if (!ident || !ident->ident) {
                add_diag(diags, filename, stmt, "unsupported for binding pattern in asm backend");
                return -1;
            }
            if (asm_var_table_add(locals, ident->ident, NULL) != 0) {
                add_diag(diags, filename, stmt, "failed to allocate loop variable");
                return -1;
            }
            {
                char idx_name[64];
                format_for_index_name(stmt, idx_name, sizeof(idx_name));
                if (!asm_var_table_find_entry(locals, idx_name)) {
                    char *idx_dup = strdup(idx_name);
                    if (!idx_dup) {
                        add_diag(diags, filename, stmt, "failed to allocate loop index");
                        return -1;
                    }
                    if (asm_var_table_add(locals, idx_dup, NULL) != 0) {
                        free(idx_dup);
                        add_diag(diags, filename, stmt, "failed to allocate loop index");
                        return -1;
                    }
                }
            }
            return collect_locals_from_list(stmt->kids[2], locals, filename, diags);
        }
        case NK_CASE: {
            char tmp_name[64];
            format_case_tmp_name(stmt, tmp_name, sizeof(tmp_name));
            if (!asm_var_table_find_entry(locals, tmp_name)) {
                char *tmp_dup = strdup(tmp_name);
                if (!tmp_dup) {
                    add_diag(diags, filename, stmt, "failed to allocate case temp");
                    return -1;
                }
                if (asm_var_table_add(locals, tmp_dup, NULL) != 0) {
                    add_diag(diags, filename, stmt, "failed to allocate case temp");
                    return -1;
                }
            }
            for (size_t i = 1; i < stmt->len; i++) {
                const ChengNode *branch = stmt->kids[i];
                if (!branch) {
                    continue;
                }
                if (branch->kind == NK_OF_BRANCH) {
                    if (branch->len > 0) {
                        if (collect_case_pattern_bindings(branch->kids[0], locals, filename, diags) != 0) {
                            return -1;
                        }
                    }
                    if (branch->len > 1) {
                        if (collect_locals_from_list(branch->kids[1], locals, filename, diags) != 0) {
                            return -1;
                        }
                    }
                } else if (branch->kind == NK_ELSE) {
                    if (branch->len > 0) {
                        if (collect_locals_from_list(branch->kids[0], locals, filename, diags) != 0) {
                            return -1;
                        }
                    }
                }
            }
            return 0;
        }
        case NK_STMT_LIST:
            return collect_locals_from_list(stmt, locals, filename, diags);
        default:
            return 0;
    }
}

static int assign_stack_offsets(AsmVarTable *locals) {
    if (!locals) {
        return 0;
    }
    int offset = 0;
    for (size_t index = 0; index < locals->len; index++) {
        offset -= 8;
        locals->items[index].offset = offset;
    }
    int stack_size = -offset;
    if (stack_size < 0) {
        stack_size = 0;
    }
    if (stack_size % 16 != 0) {
        stack_size += (16 - (stack_size % 16));
    }
    return stack_size;
}

static void emit_x86_64_imm(FILE *output, int64_t value) {
    if (value >= INT32_MIN && value <= INT32_MAX) {
        fprintf(output, "  movl $%lld, %%eax\n", (long long)value);
    } else {
        fprintf(output, "  movabs $%lld, %%rax\n", (long long)value);
    }
}

static const ChengNode *strip_paren_expr(const ChengNode *node) {
    const ChengNode *cur = node;
    while (cur && cur->kind == NK_PAR && cur->len > 0) {
        cur = cur->kids[0];
    }
    return cur;
}

static void format_for_index_name(const ChengNode *for_node, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return;
    }
    int line = for_node ? for_node->line : 0;
    int col = for_node ? for_node->col : 0;
    snprintf(buf, buf_len, "__for_idx_%d_%d", line, col);
}

static void format_case_tmp_name(const ChengNode *case_node, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return;
    }
    int line = case_node ? case_node->line : 0;
    int col = case_node ? case_node->col : 0;
    snprintf(buf, buf_len, "__case_sel_%d_%d", line, col);
}

static const char *case_pattern_ident(const ChengNode *pattern) {
    const ChengNode *node = strip_paren_expr(pattern);
    if (node && node->kind == NK_IDENT && node->ident) {
        return node->ident;
    }
    return NULL;
}

static int case_pattern_is_wildcard(const char *name) {
    return name && strcmp(name, "_") == 0;
}

static const ChengNode *strip_type_wrappers(const ChengNode *type_expr, int *out_is_ptr) {
    const ChengNode *cur = strip_paren_expr(type_expr);
    int is_ptr = 0;
    while (cur && (cur->kind == NK_PTR_TY || cur->kind == NK_REF_TY || cur->kind == NK_VAR_TY)) {
        is_ptr = 1;
        if (cur->len == 0) {
            break;
        }
        cur = strip_paren_expr(cur->kids[0]);
    }
    if (out_is_ptr) {
        *out_is_ptr = is_ptr;
    }
    return cur;
}

static int type_expr_is_seq(const ChengNode *type_expr) {
    const ChengNode *cur = strip_type_wrappers(type_expr, NULL);
    if (!cur || cur->kind != NK_BRACKET_EXPR || cur->len == 0) {
        return 0;
    }
    const ChengNode *base = cur->kids[0];
    if (base && base->kind == NK_IDENT && base->ident) {
        return strcmp(base->ident, "seq") == 0 ||
            strcmp(base->ident, "seqT") == 0 ||
            strcmp(base->ident, "seq_fixed") == 0;
    }
    return 0;
}

static const char *type_expr_bracket_base_ident(const ChengNode *type_expr) {
    const ChengNode *cur = strip_type_wrappers(type_expr, NULL);
    if (!cur || cur->kind != NK_BRACKET_EXPR || cur->len == 0) {
        return NULL;
    }
    const ChengNode *base = cur->kids[0];
    if (base && base->kind == NK_IDENT && base->ident) {
        return base->ident;
    }
    return NULL;
}

static const ChengNode *type_expr_bracket_arg(const ChengNode *type_expr, size_t index) {
    const ChengNode *cur = strip_type_wrappers(type_expr, NULL);
    if (!cur || cur->kind != NK_BRACKET_EXPR || cur->len <= index) {
        return NULL;
    }
    return cur->kids[index];
}

static int hard_rt_name_has_numeric_suffix(const char *name, const char *prefix) {
    if (!name || !prefix) {
        return 0;
    }
    size_t plen = strlen(prefix);
    if (strncmp(name, prefix, plen) != 0) {
        return 0;
    }
    const char *last = strrchr(name, '_');
    if (!last || last < name + plen) {
        return 0;
    }
    last++;
    if (*last == '\0') {
        return 0;
    }
    for (const char *cur = last; *cur != '\0'; cur++) {
        if (*cur < '0' || *cur > '9') {
            return 0;
        }
    }
    return 1;
}

static const char *hard_rt_seq_prefix(const char *name) {
    if (!name) {
        return NULL;
    }
    if (strncmp(name, "seq_", 4) == 0) {
        return "seq_";
    }
    if (strncmp(name, "seqT_", 5) == 0) {
        return "seqT_";
    }
    return NULL;
}

static int type_expr_is_named_generic(const ChengNode *type_expr, const char *name) {
    const ChengNode *cur = strip_type_wrappers(type_expr, NULL);
    if (!cur || cur->kind != NK_BRACKET_EXPR || cur->len == 0) {
        return 0;
    }
    const ChengNode *base = cur->kids[0];
    if (!base || base->kind != NK_IDENT || !base->ident) {
        return 0;
    }
    return strcmp(base->ident, name) == 0;
}

static int type_expr_is_string(const ChengNode *type_expr, int *out_is_cstring) {
    const ChengNode *cur = strip_type_wrappers(type_expr, NULL);
    if (!cur || cur->kind != NK_IDENT || !cur->ident) {
        return 0;
    }
    if (strcmp(cur->ident, "str") == 0) {
        if (out_is_cstring) {
            *out_is_cstring = 0;
        }
        return 1;
    }
    if (strcmp(cur->ident, "cstring") == 0) {
        if (out_is_cstring) {
            *out_is_cstring = 1;
        }
        return 1;
    }
    return 0;
}

static int type_name_is_result(const char *name) {
    if (!name) {
        return 0;
    }
    if (strcmp(name, "Result") == 0) {
        return 1;
    }
    return strncmp(name, "Result_", 7) == 0;
}

static int type_expr_is_result(const ChengNode *type_expr) {
    const ChengNode *cur = strip_type_wrappers(type_expr, NULL);
    if (!cur) {
        return 0;
    }
    if (cur->kind == NK_IDENT && cur->ident) {
        return type_name_is_result(cur->ident);
    }
    if (cur->kind == NK_BRACKET_EXPR && cur->len > 0) {
        const ChengNode *base = cur->kids[0];
        if (base && base->kind == NK_IDENT && base->ident) {
            return strcmp(base->ident, "Result") == 0;
        }
    }
    return 0;
}

static int type_expr_is_indirect(const AsmEmitContext *context, const ChengNode *type_expr) {
    if (!context || !context->layout || !type_expr) {
        return 0;
    }
    size_t size = 0;
    size_t align = 0;
    if (!asm_layout_type_info(context->layout, type_expr, &size, &align)) {
        return 0;
    }
    return size > 8;
}

static const char *type_name_from_type_expr(const ChengNode *type_expr) {
    const ChengNode *cur = strip_type_wrappers(type_expr, NULL);
    while (cur && cur->kind == NK_DOT_EXPR && cur->len > 1) {
        cur = cur->kids[1];
    }
    if (!cur || cur->kind != NK_IDENT || !cur->ident) {
        return NULL;
    }
    return cur->ident;
}

static int orc_mm_enabled(void) {
    const char *mm = getenv("CHENG_MM");
    if (!mm || mm[0] == '\0') {
        const char *arc = getenv("CHENG_ARC");
        if (!arc || arc[0] == '\0') {
            return 1;
        }
        return !asm_str_eq_ci(arc, "0");
    }
    if (asm_str_eq_ci(mm, "off") || asm_str_eq_ci(mm, "none") || strcmp(mm, "0") == 0) {
        return 0;
    }
    return 1;
}

static int hard_rt_mm_disabled(void) {
    const char *mm = getenv("CHENG_MM");
    if (mm && mm[0] != '\0') {
        return asm_str_eq_ci(mm, "off") || asm_str_eq_ci(mm, "none") || strcmp(mm, "0") == 0;
    }
    const char *arc = getenv("CHENG_ARC");
    if (arc && arc[0] != '\0') {
        return asm_str_eq_ci(arc, "off") || asm_str_eq_ci(arc, "none") || strcmp(arc, "0") == 0;
    }
    return 0;
}

static int orc_expr_is_borrowed(const ChengNode *expr) {
    const ChengNode *cur = strip_paren_expr(expr);
    if (!cur) {
        return 0;
    }
    if (cur->kind == NK_CALL_ARG) {
        if (cur->len > 1) {
            return orc_expr_is_borrowed(cur->kids[1]);
        }
        if (cur->len > 0) {
            return orc_expr_is_borrowed(cur->kids[0]);
        }
        return 0;
    }
    if (cur->kind == NK_PAR && cur->len == 1) {
        return orc_expr_is_borrowed(cur->kids[0]);
    }
    if (cur->kind == NK_CAST) {
        if (cur->len > 1) {
            return orc_expr_is_borrowed(cur->kids[1]);
        }
        if (cur->len > 0) {
            return orc_expr_is_borrowed(cur->kids[0]);
        }
    }
    if (cur->kind == NK_PREFIX && cur->len > 1 && cur->kids[0] && cur->kids[0]->ident) {
        if (strcmp(cur->kids[0]->ident, "*") == 0) {
            return 1;
        }
    }
    if (cur->kind == NK_IDENT || cur->kind == NK_SYMBOL) {
        return 1;
    }
    if (cur->kind == NK_DOT_EXPR || cur->kind == NK_BRACKET_EXPR ||
        cur->kind == NK_HIDDEN_DEREF || cur->kind == NK_DEREF_EXPR) {
        return 1;
    }
    return 0;
}

static int orc_name_has_prefix(const char *name, const char *prefix) {
    if (!name || !prefix) {
        return 0;
    }
    size_t plen = strlen(prefix);
    return strncmp(name, prefix, plen) == 0;
}

static int orc_type_is_managed(const ChengNode *type_expr) {
    if (!type_expr) {
        return 0;
    }
    int is_cstring = 0;
    if (type_expr_is_string(type_expr, &is_cstring)) {
        return !is_cstring;
    }
    const char *name = type_name_from_type_expr(type_expr);
    if (name) {
        if (orc_name_has_prefix(name, "str_fixed") ||
            orc_name_has_prefix(name, "seq_fixed") ||
            orc_name_has_prefix(name, "Table_fixed") ||
            orc_name_has_prefix(name, "table_fixed")) {
            return 0;
        }
        if (orc_name_has_prefix(name, "seq_") ||
            orc_name_has_prefix(name, "Table_") ||
            orc_name_has_prefix(name, "table_")) {
            return 1;
        }
    }
    const char *base = type_expr_bracket_base_ident(type_expr);
    if (!base) {
        return 0;
    }
    if (strcmp(base, "seq_fixed") == 0 || strcmp(base, "Table_fixed") == 0 || strcmp(base, "table_fixed") == 0 ||
        strcmp(base, "str_fixed") == 0) {
        return 0;
    }
    if (strcmp(base, "seq") == 0 || strcmp(base, "seqT") == 0) {
        return 1;
    }
    if (strcmp(base, "Table") == 0 || strcmp(base, "table") == 0) {
        return 1;
    }
    return 0;
}

static const AsmDataItem *asm_find_global_item(const AsmEmitContext *context, const char *name) {
    if (!context || !context->layout || !name) {
        return NULL;
    }
    return asm_layout_find_data_item(context->layout, name);
}

static const char *asm_data_symbol_name(const AsmEmitContext *context,
                                        const AsmDataItem *item,
                                        char *buf,
                                        size_t buf_len) {
    if (!item || !item->name) {
        return "";
    }
    if (item->is_global) {
        return asm_mangle_symbol(context, item->name, buf, buf_len);
    }
    return item->name;
}

static int is_type_call_target(const AsmEmitContext *context, const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_PTR_TY || node->kind == NK_REF_TY || node->kind == NK_VAR_TY ||
        node->kind == NK_FN_TY || node->kind == NK_SET_TY || node->kind == NK_TUPLE_TY) {
        return 1;
    }
    if (!context || !context->layout) {
        return 0;
    }
    size_t size = 0;
    size_t align = 0;
    if (asm_layout_type_info(context->layout, node, &size, &align)) {
        return 1;
    }
    return 0;
}

static const ChengNode *call_arg_value(const ChengNode *arg, int *out_named) {
    if (!arg) {
        return NULL;
    }
    if (arg->kind == NK_CALL_ARG) {
        if (arg->len > 1) {
            if (out_named) {
                *out_named = 1;
            }
            return arg->kids[1];
        }
        if (arg->len > 0) {
            return arg->kids[0];
        }
        return NULL;
    }
    return arg;
}

static const char *x86_64_arg_reg(size_t index) {
    switch (index) {
        case 0: return "%rdi";
        case 1: return "%rsi";
        case 2: return "%rdx";
        case 3: return "%rcx";
        case 4: return "%r8";
        case 5: return "%r9";
        default: return NULL;
    }
}

typedef enum {
    ASM_ACCESS_BYTE = 0,
    ASM_ACCESS_WORD,
    ASM_ACCESS_DWORD,
    ASM_ACCESS_QWORD
} AsmAccessKind;

typedef enum {
    ASM_INDEX_NONE = 0,
    ASM_INDEX_ARRAY,
    ASM_INDEX_SEQ,
    ASM_INDEX_STRING
} AsmIndexKind;

static AsmAccessKind asm_access_kind_from_size(size_t size) {
    if (size <= 1) {
        return ASM_ACCESS_BYTE;
    }
    if (size <= 2) {
        return ASM_ACCESS_WORD;
    }
    if (size <= 4) {
        return ASM_ACCESS_DWORD;
    }
    return ASM_ACCESS_QWORD;
}

static int expr_is_string_like(AsmFuncContext *func_ctx, const ChengNode *expr);
static int expr_is_seq_like(AsmFuncContext *func_ctx, const ChengNode *expr);

static int hard_rt_importc_allowed(const char *name) {
    if (!name) {
        return 0;
    }
    return strcmp(name, "ptr_add") == 0 ||
           strcmp(name, "load_ptr") == 0 ||
           strcmp(name, "store_ptr") == 0 ||
           strcmp(name, "c_strlen") == 0 ||
           strcmp(name, "c_strcmp") == 0 ||
           strcmp(name, "c_memcpy") == 0 ||
           strcmp(name, "c_memset") == 0 ||
           strcmp(name, "c_memcmp") == 0;
}

static int hard_rt_is_builtin_conversion(const char *name) {
    if (!name) {
        return 0;
    }
    const char *names[] = {
        "int", "int8", "int16", "int32", "int64",
        "uint8", "uint16", "uint32", "uint64",
        "float", "float64", "char"
    };
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
        const char *base = names[index];
        size_t len = strlen(base);
        if (strncmp(name, base, len) == 0 &&
            (name[len] == '\0' || name[len] == '_')) {
            return 1;
        }
    }
    return 0;
}

static int expr_contains_call(AsmFuncContext *func_ctx, const ChengNode *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->kind == NK_CALL) {
        return 1;
    }
    if (expr->kind == NK_DOT_EXPR && expr->len >= 2) {
        const ChengNode *member = expr->kids[1];
        if (member && member->kind == NK_IDENT && member->ident &&
            strcmp(member->ident, "len") == 0 && expr_is_string_like(func_ctx, expr->kids[0])) {
            return 1;
        }
    }
    for (size_t index = 0; index < expr->len; index++) {
        if (expr_contains_call(func_ctx, expr->kids[index])) {
            return 1;
        }
    }
    return 0;
}

static const ChengNode *var_type_expr_from_ident(AsmFuncContext *func_ctx, const ChengNode *ident) {
    if (!func_ctx || !ident || ident->kind != NK_IDENT || !ident->ident) {
        return NULL;
    }
    AsmVar *entry = asm_var_table_find_entry(&func_ctx->locals, ident->ident);
    if (!entry) {
        AsmGlobalType *global = asm_global_table_find(&func_ctx->context->globals, ident->ident);
        if (global) {
            return global->type_expr;
        }
        return NULL;
    }
    return entry->type_expr;
}

static const ChengNode *call_return_type_expr(AsmFuncContext *func_ctx, const ChengNode *call) {
    if (!func_ctx || !call || call->kind != NK_CALL) {
        return NULL;
    }
    if (!func_ctx->context || !func_ctx->context->root) {
        return NULL;
    }
    if (call->len == 0 || !call->kids[0]) {
        return NULL;
    }
    const ChengNode *callee = call_target_node(call->kids[0]);
    if (!callee || !(callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) || !callee->ident) {
        return NULL;
    }
    const ChengNode *decl = find_fn_decl(func_ctx->context->root, callee->ident);
    if (!decl || decl->kind != NK_FN_DECL || decl->len < 3) {
        return NULL;
    }
    return decl->kids[2];
}

static const ChengNode *expr_type_expr(AsmFuncContext *func_ctx, const ChengNode *expr) {
    const ChengNode *base = strip_paren_expr(expr);
    if (!base) {
        return NULL;
    }
    if (base->kind == NK_IDENT) {
        return var_type_expr_from_ident(func_ctx, base);
    }
    if (base->kind == NK_CALL) {
        return call_return_type_expr(func_ctx, base);
    }
    return NULL;
}

static const char *result_type_name_from_expr(AsmFuncContext *func_ctx, const ChengNode *expr) {
    const ChengNode *type_expr = expr_type_expr(func_ctx, expr);
    if (!type_expr) {
        return NULL;
    }
    return type_name_from_type_expr(type_expr);
}

static int expr_is_string_like(AsmFuncContext *func_ctx, const ChengNode *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->type_id == CHENG_TYPE_STR || expr->type_id == CHENG_TYPE_CSTRING) {
        return 1;
    }
    if (!func_ctx) {
        return 0;
    }
    const ChengNode *base = strip_paren_expr(expr);
    if (base && base->kind == NK_IDENT) {
        return type_expr_is_string(var_type_expr_from_ident(func_ctx, base), NULL);
    }
    return 0;
}

static AsmIndexKind asm_index_kind_for_expr(AsmFuncContext *func_ctx,
                                            const ChengNode *base,
                                            const ChengNode **out_elem_type) {
    if (out_elem_type) {
        *out_elem_type = NULL;
    }
    if (!func_ctx || !base) {
        return ASM_INDEX_NONE;
    }
    if (expr_is_string_like(func_ctx, base)) {
        return ASM_INDEX_STRING;
    }
    const ChengNode *probe = strip_paren_expr(base);
    const ChengNode *type_expr = NULL;
    if (probe && probe->kind == NK_IDENT) {
        type_expr = var_type_expr_from_ident(func_ctx, probe);
    }
    const char *base_name = type_expr_bracket_base_ident(type_expr);
    if (base_name) {
        if (strcmp(base_name, "array") == 0) {
            if (out_elem_type) {
                *out_elem_type = type_expr_bracket_arg(type_expr, 1);
            }
            return ASM_INDEX_ARRAY;
        }
        if (strcmp(base_name, "seq") == 0 ||
            strcmp(base_name, "seqT") == 0 ||
            strcmp(base_name, "seq_fixed") == 0) {
            if (out_elem_type) {
                *out_elem_type = type_expr_bracket_arg(type_expr, 1);
            }
            return ASM_INDEX_SEQ;
        }
    }
    if (expr_is_seq_like(func_ctx, base)) {
        return ASM_INDEX_SEQ;
    }
    return ASM_INDEX_NONE;
}

static size_t asm_elem_size_for_type(AsmEmitContext *context, const ChengNode *type_expr) {
    size_t size = 8;
    size_t align = 8;
    if (!context || !context->layout || !type_expr) {
        return size;
    }
    if (asm_layout_type_info(context->layout, type_expr, &size, &align) && size > 0) {
        return size;
    }
    return 8;
}

static int hard_rt_check_calls_in_node(const ChengNode *node,
                                       const ChengStrList *importc,
                                       const char *filename,
                                       ChengDiagList *diags) {
    if (!node) {
        return 0;
    }
    int rc = 0;
    if (node->kind == NK_IDENT) {
        if (hard_rt_check_reserved_ident(node, filename, diags) != 0) {
            rc = -1;
        }
    }
    if (node->kind == NK_SEQ_LIT || node->kind == NK_TABLE_LIT) {
        add_diag(diags, filename, node, "hard realtime asm backend does not allow dynamic literals");
        rc = -1;
    }
    if (node->kind == NK_CALL && node->len > 0 && node->kids[0]) {
        const ChengNode *callee = call_target_node(node->kids[0]);
        const char *callee_name =
            callee && (callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) && callee->ident ? callee->ident : NULL;
        if (callee_name) {
            if (strcmp(callee_name, "arcNew") == 0 || strcmp(callee_name, "arcClone") == 0 ||
                strcmp(callee_name, "share_mt") == 0 || strcmp(callee_name, "mutexNew") == 0 ||
                strcmp(callee_name, "rwLockNew") == 0 || strcmp(callee_name, "atomicNew") == 0) {
                add_diag(diags, filename, node, "hard realtime asm backend does not allow concurrency calls");
                rc = -1;
            }
            const char *runtime_sym = NULL;
            if (asm_runtime_lookup_symbol(callee_name, &runtime_sym)) {
                add_diag(diags, filename, node, "hard realtime asm backend does not allow runtime calls");
                rc = -1;
            } else if (asm_func_is_importc_name(importc, callee_name) &&
                       !hard_rt_importc_allowed(callee_name)) {
                add_diag(diags, filename, node, "hard realtime asm backend does not allow importc calls");
                rc = -1;
            }
        }
    }
    for (size_t index = 0; index < node->len; index++) {
        if (hard_rt_check_calls_in_node(node->kids[index], importc, filename, diags) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static const char *hard_rt_kind_name(ChengNodeKind kind) {
    switch (kind) {
        case NK_ERROR: return "error";
        case NK_EMPTY: return "empty";
        case NK_MODULE: return "module";
        case NK_STMT_LIST: return "statement list";
        case NK_IMPORT_STMT: return "import";
        case NK_IMPORT_AS: return "import-as";
        case NK_IMPORT_GROUP: return "import-group";
        case NK_INCLUDE_STMT: return "include";
        case NK_FN_DECL: return "fn declaration";
        case NK_ITERATOR_DECL: return "iterator declaration";
        case NK_MACRO_DECL: return "macro declaration";
        case NK_TEMPLATE_DECL: return "template declaration";
        case NK_FORMAL_PARAMS: return "formal params";
        case NK_IDENT_DEFS: return "ident defs";
        case NK_TYPE_DECL: return "type declaration";
        case NK_OBJECT_DECL: return "object declaration";
        case NK_ENUM_DECL: return "enum declaration";
        case NK_ENUM_FIELD_DECL: return "enum field";
        case NK_CONCEPT_DECL: return "concept declaration";
        case NK_TRAIT_DECL: return "trait declaration";
        case NK_REF_TY: return "ref type";
        case NK_PTR_TY: return "ptr type";
        case NK_VAR_TY: return "var type";
        case NK_TUPLE_TY: return "tuple type";
        case NK_SET_TY: return "set type";
        case NK_FN_TY: return "fn type";
        case NK_REC_LIST: return "record list";
        case NK_REC_CASE: return "record case";
        case NK_GENERIC_PARAMS: return "generic params";
        case NK_LET: return "let";
        case NK_VAR: return "var";
        case NK_CONST: return "const";
        case NK_PRAGMA: return "pragma";
        case NK_ANNOTATION: return "annotation";
        case NK_RETURN: return "return";
        case NK_YIELD: return "yield";
        case NK_BREAK: return "break";
        case NK_CONTINUE: return "continue";
        case NK_DEFER: return "defer";
        case NK_IF: return "if";
        case NK_WHEN: return "when";
        case NK_WHILE: return "while";
        case NK_FOR: return "for";
        case NK_CASE: return "case";
        case NK_BLOCK: return "block";
        case NK_OF_BRANCH: return "of branch";
        case NK_ELSE: return "else";
        case NK_ASGN: return "assignment";
        case NK_FAST_ASGN: return "fast assignment";
        case NK_CALL: return "call";
        case NK_INFIX: return "infix";
        case NK_PREFIX: return "prefix";
        case NK_POSTFIX: return "postfix";
        case NK_CAST: return "cast";
        case NK_DOT_EXPR: return "dot expression";
        case NK_BRACKET_EXPR: return "bracket expression";
        case NK_CURLY_EXPR: return "curly expression";
        case NK_PAR: return "paren";
        case NK_TUPLE_LIT: return "tuple literal";
        case NK_SEQ_LIT: return "seq literal";
        case NK_TABLE_LIT: return "table literal";
        case NK_CURLY: return "curly";
        case NK_BRACKET: return "bracket";
        case NK_RANGE: return "range";
        case NK_PATTERN: return "pattern";
        case NK_CALL_ARG: return "call arg";
        case NK_GUARD: return "guard";
        case NK_LAMBDA: return "lambda";
        case NK_DO: return "do";
        case NK_IF_EXPR: return "if expression";
        case NK_WHEN_EXPR: return "when expression";
        case NK_CASE_EXPR: return "case expression";
        case NK_COMPREHENSION: return "comprehension";
        case NK_HIDDEN_DEREF: return "hidden deref";
        case NK_DEREF_EXPR: return "deref";
        case NK_IDENT: return "identifier";
        case NK_SYMBOL: return "symbol";
        case NK_INT_LIT: return "int literal";
        case NK_FLOAT_LIT: return "float literal";
        case NK_STR_LIT: return "string literal";
        case NK_CHAR_LIT: return "char literal";
        case NK_BOOL_LIT: return "bool literal";
        case NK_NIL_LIT: return "nil literal";
        default: return "unknown";
    }
}

static const char *hard_rt_case_pattern_error(const AsmFuncContext *func_ctx) {
    if (func_ctx && func_ctx->context && func_ctx->context->hard_rt) {
        return "hard realtime asm backend requires simple patterns";
    }
    return "unsupported case pattern in asm backend";
}

static int hard_rt_case_pattern_const(const ChengNode *pattern, int64_t *out_value) {
    const ChengNode *node = strip_paren_expr(pattern);
    if (!node) {
        return 0;
    }
    int64_t value = 0;
    if (hard_rt_is_const_int(node, &value)) {
        if (out_value) {
            *out_value = value;
        }
        return 1;
    }
    if (node->kind == NK_BOOL_LIT) {
        value = (node->ident && strcmp(node->ident, "true") == 0) ? 1 : 0;
        if (out_value) {
            *out_value = value;
        }
        return 1;
    }
    if (node->kind == NK_NIL_LIT) {
        if (out_value) {
            *out_value = 0;
        }
        return 1;
    }
    if (node->kind == NK_CHAR_LIT) {
        int ch = 0;
        if (!decode_char_lit(node->str_val, &ch)) {
            return 0;
        }
        if (out_value) {
            *out_value = ch;
        }
        return 1;
    }
    return 0;
}

static int hard_rt_check_simple_pattern(const ChengNode *node,
                                        const char *filename,
                                        ChengDiagList *diags,
                                        int profile) {
    if (!node || node->kind != NK_PATTERN || node->len == 0) {
        return 0;
    }
    if (profile < 2) {
        if (node->len != 1 || !node->kids[0] || node->kids[0]->kind != NK_IDENT) {
            add_diag(diags, filename, node, "hard realtime asm backend requires simple identifier patterns");
            return -1;
        }
        if (hard_rt_check_reserved_ident(node->kids[0], filename, diags) != 0) {
            return -1;
        }
        return 0;
    }
    for (size_t i = 0; i < node->len; i++) {
        const ChengNode *pat = node->kids[i];
        if (!pat) {
            continue;
        }
        const ChengNode *probe = strip_paren_expr(pat);
        if (probe && probe->kind == NK_IDENT) {
            if (hard_rt_check_reserved_ident(probe, filename, diags) != 0) {
                return -1;
            }
            continue;
        }
        if (!hard_rt_case_pattern_const(pat, NULL)) {
            add_diag(diags, filename, node, "hard realtime asm backend requires simple patterns");
            return -1;
        }
    }
    return 0;
}

static int hard_rt_check_case_pattern_expr(const ChengNode *pattern,
                                           const char *filename,
                                           ChengDiagList *diags,
                                           int profile) {
    if (!pattern) {
        add_diag(diags, filename, pattern, "hard realtime asm backend requires simple patterns");
        return -1;
    }
    if (pattern->kind == NK_PATTERN) {
        return hard_rt_check_simple_pattern(pattern, filename, diags, profile);
    }
    const ChengNode *probe = strip_paren_expr(pattern);
    if (probe && probe->kind == NK_IDENT) {
        if (hard_rt_check_reserved_ident(probe, filename, diags) != 0) {
            return -1;
        }
        return 0;
    }
    if (hard_rt_case_pattern_const(pattern, NULL)) {
        return 0;
    }
    add_diag(diags, filename, pattern, "hard realtime asm backend requires simple patterns");
    return -1;
}

static int hard_rt_check_call_args(const ChengNode *node,
                                   const char *filename,
                                   ChengDiagList *diags) {
    if (!node || node->kind != NK_CALL) {
        return 0;
    }
    for (size_t i = 1; i < node->len; i++) {
        const ChengNode *arg = node->kids[i];
        if (arg && arg->kind == NK_CALL_ARG && arg->len > 1) {
            add_diag(diags, filename, arg, "hard realtime asm backend does not allow named arguments");
            return -1;
        }
    }
    return 0;
}

static int hard_rt_check_reserved_ident(const ChengNode *node,
                                        const char *filename,
                                        ChengDiagList *diags) {
    if (!node || node->kind != NK_IDENT || !node->ident) {
        return 0;
    }
    if (strcmp(node->ident, "async") == 0 || strcmp(node->ident, "await") == 0) {
        add_diag(diags, filename, node, "hard realtime asm backend does not allow async/await");
        return -1;
    }
    return 0;
}

static int hard_rt_check_pragma_node(const ChengNode *node,
                                     const char *filename,
                                     ChengDiagList *diags,
                                     int profile,
                                     int top_level) {
    if (!node || node->kind != NK_PRAGMA || node->len == 0) {
        return 0;
    }
    const ChengNode *expr = node->kids[0];
    const ChengNode *callee = NULL;
    if (expr && expr->kind == NK_CALL && expr->len > 0) {
        callee = call_target_node(expr->kids[0]);
    } else if (expr && expr->kind == NK_IDENT) {
        callee = expr;
    }
    if (callee && callee->kind == NK_IDENT && callee->ident) {
        const char *name = callee->ident;
        if (strcmp(name, "task") == 0 || strcmp(name, "period") == 0 ||
            strcmp(name, "deadline") == 0 || strcmp(name, "priority") == 0) {
            if (profile < 2) {
                add_diag(diags, filename, node, "hard realtime asm backend does not allow @task annotations");
                return -1;
            }
            if (!top_level) {
                add_diag(diags, filename, node,
                         "hard realtime asm backend requires @task annotations to precede a function");
                return -1;
            }
            return 0;
        }
    }
    return 0;
}

typedef struct {
    const ChengNode *anchor;
    int pending;
    int has_task;
    int has_period;
    int has_deadline;
    int has_priority;
    int has_wcet;
} HardRtTaskPragmaState;

static void hard_rt_task_pragmas_reset(HardRtTaskPragmaState *state) {
    if (!state) {
        return;
    }
    state->anchor = NULL;
    state->pending = 0;
    state->has_task = 0;
    state->has_period = 0;
    state->has_deadline = 0;
    state->has_priority = 0;
    state->has_wcet = 0;
}

static int hard_rt_pragma_extract(const ChengNode *node,
                                  const char **out_name,
                                  const ChengNode **out_arg,
                                  int *out_arg_count) {
    if (out_name) {
        *out_name = NULL;
    }
    if (out_arg) {
        *out_arg = NULL;
    }
    if (out_arg_count) {
        *out_arg_count = 0;
    }
    if (!node || node->kind != NK_PRAGMA || node->len == 0) {
        return 0;
    }
    const ChengNode *expr = node->kids[0];
    if (!expr) {
        return 0;
    }
    const ChengNode *callee = NULL;
    const ChengNode *arg = NULL;
    int arg_count = 0;
    if (expr->kind == NK_CALL && expr->len > 0) {
        callee = call_target_node(expr->kids[0]);
        arg_count = (int)expr->len - 1;
        if (expr->len > 1) {
            arg = expr->kids[1];
            if (arg && arg->kind == NK_CALL_ARG) {
                if (arg->len > 1) {
                    arg = arg->kids[1];
                } else if (arg->len > 0) {
                    arg = arg->kids[0];
                } else {
                    arg = NULL;
                }
            }
        }
    } else if (expr->kind == NK_IDENT) {
        callee = expr;
    }
    if (!callee || callee->kind != NK_IDENT || !callee->ident) {
        return 0;
    }
    if (out_name) {
        *out_name = callee->ident;
    }
    if (out_arg) {
        *out_arg = arg;
    }
    if (out_arg_count) {
        *out_arg_count = arg_count;
    }
    return 1;
}

static int hard_rt_task_signature_ok(const ChengNode *fn_node) {
    if (!fn_node || fn_node->kind != NK_FN_DECL || fn_node->len < 4) {
        return 0;
    }
    const ChengNode *params = fn_node->kids[1];
    if (params && params->kind == NK_FORMAL_PARAMS && params->len > 0) {
        return 0;
    }
    const ChengNode *ret_type = fn_node->kids[2];
    const ChengNode *ret_base = strip_paren_expr(ret_type);
    if (!ret_base || ret_base->kind != NK_IDENT || !ret_base->ident) {
        return 0;
    }
    return strcmp(ret_base->ident, "int32") == 0;
}

static int hard_rt_collect_task_pragma(const ChengNode *stmt,
                                       HardRtTaskPragmaState *state,
                                       const char *filename,
                                       ChengDiagList *diags) {
    const char *name = NULL;
    const ChengNode *arg = NULL;
    int arg_count = 0;
    if (!hard_rt_pragma_extract(stmt, &name, &arg, &arg_count) || !name) {
        return 0;
    }
    if (strcmp(name, "task") == 0) {
        if (!state->pending) {
            state->anchor = stmt;
        }
        state->pending = 1;
        state->has_task = 1;
        return 1;
    }
    if (strcmp(name, "period") == 0) {
        if (!state->pending) {
            state->anchor = stmt;
        }
        state->pending = 1;
        state->has_period = 1;
        if (arg_count != 1 || !hard_rt_is_const_int(arg, NULL)) {
            add_diag(diags, filename, stmt, "hard realtime asm backend expects @period(<const int>)");
            return -1;
        }
        return 1;
    }
    if (strcmp(name, "deadline") == 0) {
        if (!state->pending) {
            state->anchor = stmt;
        }
        state->pending = 1;
        state->has_deadline = 1;
        if (arg_count != 1 || !hard_rt_is_const_int(arg, NULL)) {
            add_diag(diags, filename, stmt, "hard realtime asm backend expects @deadline(<const int>)");
            return -1;
        }
        return 1;
    }
    if (strcmp(name, "priority") == 0) {
        if (!state->pending) {
            state->anchor = stmt;
        }
        state->pending = 1;
        state->has_priority = 1;
        if (arg_count != 1 || !hard_rt_is_const_int(arg, NULL)) {
            add_diag(diags, filename, stmt, "hard realtime asm backend expects @priority(<const int>)");
            return -1;
        }
        return 1;
    }
    if (strcmp(name, "wcet") == 0) {
        if (!state->pending) {
            state->anchor = stmt;
        }
        state->pending = 1;
        state->has_wcet = 1;
        return 1;
    }
    return 0;
}

static int hard_rt_check_task_pragmas_in_list(const ChengNode *list,
                                              const char *filename,
                                              ChengDiagList *diags,
                                              int profile) {
    if (!list || list->kind != NK_STMT_LIST || profile < 2) {
        return 0;
    }
    HardRtTaskPragmaState state;
    hard_rt_task_pragmas_reset(&state);
    int rc = 0;
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_PRAGMA) {
            int collected = hard_rt_collect_task_pragma(stmt, &state, filename, diags);
            if (collected < 0) {
                rc = -1;
            }
            if (collected > 0) {
                continue;
            }
            continue;
        }
        if (state.pending) {
            if (stmt->kind != NK_FN_DECL) {
                add_diag(diags, filename, state.anchor ? state.anchor : stmt,
                         "hard realtime asm backend requires @task annotations to precede a function");
                rc = -1;
                hard_rt_task_pragmas_reset(&state);
                continue;
            }
            if (state.has_task) {
                if (!state.has_period || !state.has_deadline || !state.has_priority) {
                    add_diag(diags, filename, stmt,
                             "hard realtime asm backend requires @period/@deadline/@priority for @task");
                    rc = -1;
                }
                if (!hard_rt_task_signature_ok(stmt)) {
                    add_diag(diags, filename, stmt,
                             "hard realtime asm backend requires @task function signature: fn <name>(): int32");
                    rc = -1;
                }
            } else if (state.has_period || state.has_deadline || state.has_priority) {
                add_diag(diags, filename, stmt,
                         "hard realtime asm backend requires @task for @period/@deadline/@priority");
                rc = -1;
            }
            hard_rt_task_pragmas_reset(&state);
        }
    }
    if (state.pending) {
        add_diag(diags, filename, state.anchor ? state.anchor : list,
                 "hard realtime asm backend requires @task annotations to precede a function");
        rc = -1;
    }
    return rc;
}

static int hard_rt_check_prefix_op(const ChengNode *node,
                                   const char *filename,
                                   ChengDiagList *diags) {
    if (!node || node->kind != NK_PREFIX || node->len < 1 || !node->kids[0] || !node->kids[0]->ident) {
        return 0;
    }
    const char *op = node->kids[0]->ident;
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
        strcmp(op, "!") == 0 || strcmp(op, "~") == 0 ||
        strcmp(op, "&") == 0 || strcmp(op, "*") == 0) {
        return 0;
    }
    add_diag(diags, filename, node, "hard realtime asm backend does not allow this prefix operator");
    return -1;
}

static int hard_rt_check_infix_op(const ChengNode *node,
                                  const char *filename,
                                  ChengDiagList *diags) {
    if (!node || node->kind != NK_INFIX || node->len < 1 || !node->kids[0] || !node->kids[0]->ident) {
        return 0;
    }
    const char *op = node->kids[0]->ident;
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
        strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0 ||
        strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0 ||
        strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0 ||
        strcmp(op, "..") == 0 || strcmp(op, "..<") == 0 ||
        strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, ">=") == 0 ||
        strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        return 0;
    }
    add_diag(diags, filename, node, "hard realtime asm backend does not allow this infix operator");
    return -1;
}

static int hard_rt_check_profile_node(const ChengNode *node,
                                      const char *filename,
                                      ChengDiagList *diags,
                                      int profile) {
    if (!node) {
        return 0;
    }
    int rc = 0;
    switch (node->kind) {
        case NK_ERROR:
            add_diag(diags, filename, node, "hard realtime asm backend does not allow parse errors");
            return -1;
        case NK_EMPTY:
        case NK_MODULE:
        case NK_STMT_LIST:
        case NK_IMPORT_STMT:
        case NK_IMPORT_AS:
        case NK_IMPORT_GROUP:
        case NK_INCLUDE_STMT:
        case NK_FN_DECL:
        case NK_FORMAL_PARAMS:
        case NK_IDENT_DEFS:
        case NK_TYPE_DECL:
        case NK_OBJECT_DECL:
        case NK_ENUM_DECL:
        case NK_ENUM_FIELD_DECL:
        case NK_REF_TY:
        case NK_PTR_TY:
        case NK_VAR_TY:
        case NK_REC_LIST:
        case NK_LET:
        case NK_VAR:
        case NK_CONST:
        case NK_PRAGMA:
        case NK_ANNOTATION:
        case NK_RETURN:
        case NK_BREAK:
        case NK_CONTINUE:
        case NK_IF:
        case NK_WHILE:
        case NK_FOR:
        case NK_BLOCK:
        case NK_ELSE:
        case NK_ASGN:
        case NK_CALL:
        case NK_INFIX:
        case NK_PREFIX:
        case NK_CAST:
        case NK_PAR:
        case NK_PATTERN:
        case NK_CALL_ARG:
        case NK_HIDDEN_DEREF:
        case NK_DOT_EXPR:
        case NK_IDENT:
        case NK_SYMBOL:
        case NK_INT_LIT:
        case NK_CHAR_LIT:
        case NK_BOOL_LIT:
        case NK_NIL_LIT:
            break;
        case NK_CASE:
        case NK_OF_BRANCH: {
            if (profile < 2) {
                char msg[128];
                snprintf(msg, sizeof(msg), "hard realtime asm backend does not allow %s",
                         hard_rt_kind_name(node->kind));
                add_diag(diags, filename, node, msg);
                rc = -1;
            }
            break;
        }
        case NK_ITERATOR_DECL:
        case NK_MACRO_DECL:
        case NK_TEMPLATE_DECL:
        case NK_CONCEPT_DECL:
        case NK_TRAIT_DECL:
        case NK_TUPLE_TY:
        case NK_SET_TY:
        case NK_FN_TY:
        case NK_REC_CASE:
        case NK_GENERIC_PARAMS:
        case NK_YIELD:
        case NK_DEFER:
        case NK_WHEN:
        case NK_FAST_ASGN:
        case NK_POSTFIX:
        case NK_BRACKET_EXPR:
        case NK_CURLY_EXPR:
        case NK_TUPLE_LIT:
        case NK_SEQ_LIT:
        case NK_TABLE_LIT:
        case NK_CURLY:
        case NK_BRACKET:
        case NK_RANGE:
        case NK_GUARD:
        case NK_LAMBDA:
        case NK_DO:
        case NK_IF_EXPR:
        case NK_WHEN_EXPR:
        case NK_CASE_EXPR:
        case NK_COMPREHENSION:
        case NK_DEREF_EXPR:
        case NK_FLOAT_LIT:
        case NK_STR_LIT: {
            char msg[128];
            snprintf(msg, sizeof(msg), "hard realtime asm backend does not allow %s", hard_rt_kind_name(node->kind));
            add_diag(diags, filename, node, msg);
            return -1;
        }
        default: {
            char msg[128];
            snprintf(msg, sizeof(msg), "hard realtime asm backend does not allow %s", hard_rt_kind_name(node->kind));
            add_diag(diags, filename, node, msg);
            return -1;
        }
    }
    if (node->kind == NK_PATTERN) {
        if (hard_rt_check_simple_pattern(node, filename, diags, profile) != 0) {
            rc = -1;
        }
    }
    if (node->kind == NK_CALL) {
        if (hard_rt_check_call_args(node, filename, diags) != 0) {
            rc = -1;
        }
    }
    if (node->kind == NK_IDENT) {
        if (hard_rt_check_reserved_ident(node, filename, diags) != 0) {
            rc = -1;
        }
    }
    if (node->kind == NK_PRAGMA) {
        if (hard_rt_check_pragma_node(node, filename, diags, profile, 0) != 0) {
            rc = -1;
        }
    }
    if (node->kind == NK_PREFIX) {
        if (hard_rt_check_prefix_op(node, filename, diags) != 0) {
            rc = -1;
        }
    }
    if (node->kind == NK_INFIX) {
        if (hard_rt_check_infix_op(node, filename, diags) != 0) {
            rc = -1;
        }
    }
    size_t skip_index = (size_t)-1;
    if (node->kind == NK_OF_BRANCH && profile >= 2) {
        const ChengNode *pattern = node->len > 0 ? node->kids[0] : NULL;
        if (hard_rt_check_case_pattern_expr(pattern, filename, diags, profile) != 0) {
            rc = -1;
        }
        skip_index = 0;
    }
    if (node->kind == NK_IDENT_DEFS ||
        node->kind == NK_VAR ||
        node->kind == NK_LET ||
        node->kind == NK_CONST) {
        skip_index = 1;
    }
    for (size_t i = 0; i < node->len; i++) {
        if (i == skip_index) {
            continue;
        }
        if (hard_rt_check_profile_node(node->kids[i], filename, diags, profile) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static int hard_rt_is_const_int(const ChengNode *expr, int64_t *out_value) {
    const ChengNode *node = strip_paren_expr(expr);
    if (!node) {
        return 0;
    }
    if (node->kind == NK_INT_LIT) {
        if (out_value) {
            *out_value = node->int_val;
        }
        return 1;
    }
    if (node->kind == NK_PREFIX && node->len > 1 && node->kids[0] && node->kids[0]->ident &&
        node->kids[1] && node->kids[1]->kind == NK_INT_LIT) {
        int64_t val = node->kids[1]->int_val;
        if (strcmp(node->kids[0]->ident, "-") == 0) {
            val = -val;
            if (out_value) {
                *out_value = val;
            }
            return 1;
        }
        if (strcmp(node->kids[0]->ident, "+") == 0) {
            if (out_value) {
                *out_value = val;
            }
            return 1;
        }
    }
    return 0;
}

static int pragma_bound_value(const ChengNode *node,
                              int64_t *out_bound,
                              const char *filename,
                              ChengDiagList *diags) {
    if (!node || node->kind != NK_PRAGMA || node->len == 0) {
        return 0;
    }
    const ChengNode *expr = node->kids[0];
    if (!expr || expr->kind != NK_CALL || expr->len < 2) {
        return 0;
    }
    const ChengNode *callee = expr->kids[0];
    if (!callee || callee->kind != NK_IDENT || !callee->ident || strcmp(callee->ident, "bound") != 0) {
        return 0;
    }
    const ChengNode *arg = expr->kids[1];
    if (arg && arg->kind == NK_CALL_ARG) {
        if (arg->len > 1) {
            arg = arg->kids[1];
        } else if (arg->len > 0) {
            arg = arg->kids[0];
        }
    }
    if (!hard_rt_is_const_int(arg, out_bound)) {
        add_diag(diags, filename, node, "hard realtime asm backend expects @bound(<const int>)");
        return -1;
    }
    return 1;
}

static int hard_rt_for_iter_is_bounded(const ChengNode *iter) {
    const ChengNode *node = strip_paren_expr(iter);
    if (!node || node->kind != NK_INFIX || node->len < 3 || !node->kids[0]) {
        return 0;
    }
    const ChengNode *op_node = node->kids[0];
    const char *op = op_node && op_node->ident ? op_node->ident : NULL;
    if (!op || (strcmp(op, "..") != 0 && strcmp(op, "..<") != 0)) {
        return 0;
    }
    return hard_rt_is_const_int(node->kids[2], NULL);
}

static int hard_rt_check_loops_in_stmt(const ChengNode *stmt,
                                       const char *filename,
                                       ChengDiagList *diags);

static int hard_rt_check_loops_in_list(const ChengNode *list,
                                       const char *filename,
                                       ChengDiagList *diags) {
    if (!list) {
        return 0;
    }
    if (list->kind != NK_STMT_LIST) {
        return hard_rt_check_loops_in_stmt(list, filename, diags);
    }
    int rc = 0;
    int pending_bound = 0;
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_PRAGMA) {
            int64_t bound = 0;
            int pragma = pragma_bound_value(stmt, &bound, filename, diags);
            if (pragma < 0) {
                rc = -1;
                pending_bound = 0;
            } else {
                pending_bound = pragma > 0 ? 1 : 0;
            }
            continue;
        }
        if (stmt->kind == NK_WHILE) {
            if (!pending_bound) {
                add_diag(diags, filename, stmt, "hard realtime asm backend requires bounded loops (@bound)");
                rc = -1;
            }
            pending_bound = 0;
            if (stmt->len > 1) {
                if (hard_rt_check_loops_in_list(stmt->kids[1], filename, diags) != 0) {
                    rc = -1;
                }
            }
            continue;
        }
        if (stmt->kind == NK_FOR) {
            int bounded = 0;
            if (stmt->len > 1) {
                bounded = hard_rt_for_iter_is_bounded(stmt->kids[1]);
            }
            if (!bounded && !pending_bound) {
                add_diag(diags, filename, stmt, "hard realtime asm backend requires bounded for-range or @bound");
                rc = -1;
            }
            pending_bound = 0;
            if (stmt->len > 2) {
                if (hard_rt_check_loops_in_list(stmt->kids[2], filename, diags) != 0) {
                    rc = -1;
                }
            }
            continue;
        }
        pending_bound = 0;
        if (hard_rt_check_loops_in_stmt(stmt, filename, diags) != 0) {
            rc = -1;
        }
    }
    return rc;
}

static int hard_rt_check_loops_in_stmt(const ChengNode *stmt,
                                       const char *filename,
                                       ChengDiagList *diags) {
    if (!stmt) {
        return 0;
    }
    switch (stmt->kind) {
        case NK_STMT_LIST:
            return hard_rt_check_loops_in_list(stmt, filename, diags);
        case NK_IF: {
            int rc = 0;
            if (stmt->len > 1) {
                if (hard_rt_check_loops_in_list(stmt->kids[1], filename, diags) != 0) {
                    rc = -1;
                }
            }
            if (stmt->len > 2 && stmt->kids[2] && stmt->kids[2]->kind == NK_ELSE &&
                stmt->kids[2]->len > 0) {
                if (hard_rt_check_loops_in_list(stmt->kids[2]->kids[0], filename, diags) != 0) {
                    rc = -1;
                }
            }
            return rc;
        }
        case NK_WHILE:
            add_diag(diags, filename, stmt, "hard realtime asm backend requires bounded loops (@bound)");
            if (stmt->len > 1) {
                return hard_rt_check_loops_in_list(stmt->kids[1], filename, diags);
            }
            return -1;
        case NK_FOR:
            add_diag(diags, filename, stmt, "hard realtime asm backend requires bounded for-range or @bound");
            if (stmt->len > 2) {
                return hard_rt_check_loops_in_list(stmt->kids[2], filename, diags);
            }
            return -1;
        default:
            return 0;
    }
}

static int hard_rt_type_is_fixed_seq(const ChengNode *type_expr);
static int hard_rt_type_is_table(const ChengNode *type_expr);
static int hard_rt_type_is_fixed_table(const ChengNode *type_expr);
static int hard_rt_type_is_fixed_string(const ChengNode *type_expr);

static int hard_rt_type_is_dynamic(const ChengNode *type_expr) {
    if (!type_expr) {
        return 0;
    }
    const char *name = type_name_from_type_expr(type_expr);
    const char *seq_prefix = hard_rt_seq_prefix(name);
    if (type_expr_is_seq(type_expr) || seq_prefix) {
        return hard_rt_type_is_fixed_seq(type_expr) ? 0 : 1;
    }
    if (hard_rt_type_is_fixed_string(type_expr)) {
        return 0;
    }
    if (type_expr_is_string(type_expr, NULL)) {
        return 1;
    }
    if (hard_rt_type_is_table(type_expr)) {
        return hard_rt_type_is_fixed_table(type_expr) ? 0 : 1;
    }
    if (!name) {
        return 0;
    }
    if (strcmp(name, "str") == 0 || strcmp(name, "cstring") == 0) {
        return 1;
    }
    return 0;
}

static int hard_rt_type_is_concurrency(const ChengNode *type_expr) {
    if (!type_expr) {
        return 0;
    }
    if (type_expr_is_named_generic(type_expr, "Arc") ||
        type_expr_is_named_generic(type_expr, "Mutex") ||
        type_expr_is_named_generic(type_expr, "RwLock") ||
        type_expr_is_named_generic(type_expr, "Atomic")) {
        return 1;
    }
    const char *name = type_name_from_type_expr(type_expr);
    if (!name) {
        return 0;
    }
    return strcmp(name, "Arc") == 0 || strcmp(name, "Mutex") == 0 ||
           strcmp(name, "RwLock") == 0 || strcmp(name, "Atomic") == 0;
}

static int hard_rt_type_is_float(const ChengNode *type_expr) {
    const char *name = type_name_from_type_expr(type_expr);
    if (!name) {
        return 0;
    }
    return strcmp(name, "float") == 0 || strcmp(name, "float32") == 0 ||
           strcmp(name, "float64") == 0 || strcmp(name, "double") == 0;
}

static int hard_rt_type_is_fixed_seq(const ChengNode *type_expr) {
    const char *base = type_expr_bracket_base_ident(type_expr);
    if (base && strcmp(base, "seq_fixed") == 0) {
        return 1;
    }
    if (type_expr_is_seq(type_expr)) {
        const ChengNode *cap = type_expr_bracket_arg(type_expr, 2);
        return cap ? hard_rt_is_const_int(cap, NULL) : 0;
    }
    const char *name = type_name_from_type_expr(type_expr);
    if (name && strncmp(name, "seq_fixed", 9) == 0) {
        return 1;
    }
    const char *prefix = hard_rt_seq_prefix(name);
    if (prefix) {
        return hard_rt_name_has_numeric_suffix(name, prefix);
    }
    return 0;
}

static int hard_rt_type_is_table(const ChengNode *type_expr) {
    const char *base = type_expr_bracket_base_ident(type_expr);
    if (base && (strcmp(base, "Table") == 0 || strcmp(base, "table") == 0)) {
        return 1;
    }
    const char *name = type_name_from_type_expr(type_expr);
    if (name && (strcmp(name, "Table") == 0 || strcmp(name, "table") == 0 ||
                 strncmp(name, "Table_", 6) == 0 || strncmp(name, "table_", 6) == 0)) {
        return 1;
    }
    return 0;
}

static int hard_rt_type_is_fixed_table(const ChengNode *type_expr) {
    const char *base = type_expr_bracket_base_ident(type_expr);
    if (base && (strcmp(base, "Table_fixed") == 0 || strcmp(base, "table_fixed") == 0)) {
        return 1;
    }
    if (hard_rt_type_is_table(type_expr)) {
        const ChengNode *cap = type_expr_bracket_arg(type_expr, 2);
        if (cap) {
            return hard_rt_is_const_int(cap, NULL);
        }
    }
    const char *name = type_name_from_type_expr(type_expr);
    if (name && (strncmp(name, "Table_fixed", 11) == 0 || strncmp(name, "table_fixed", 11) == 0)) {
        return 1;
    }
    if (hard_rt_name_has_numeric_suffix(name, "Table_") ||
        hard_rt_name_has_numeric_suffix(name, "table_")) {
        return 1;
    }
    return 0;
}

static int hard_rt_type_is_fixed_string(const ChengNode *type_expr) {
    const char *base = type_expr_bracket_base_ident(type_expr);
    if (!base || (strcmp(base, "str") != 0 && strcmp(base, "cstring") != 0)) {
        return 0;
    }
    const ChengNode *cap = type_expr_bracket_arg(type_expr, 1);
    return cap ? hard_rt_is_const_int(cap, NULL) : 0;
}

static int hard_rt_check_type_expr(const ChengNode *type_expr,
                                   const char *filename,
                                   ChengDiagList *diags,
                                   const ChengNode *node) {
    if (hard_rt_type_is_concurrency(type_expr)) {
        add_diag(diags, filename, node ? node : type_expr,
                 "hard realtime asm backend does not allow concurrency types");
        return -1;
    }
    if (hard_rt_type_is_dynamic(type_expr)) {
        add_diag(diags, filename, node ? node : type_expr,
                 "hard realtime asm backend does not allow dynamic container types");
        return -1;
    }
    if (hard_rt_type_is_float(type_expr)) {
        add_diag(diags, filename, node ? node : type_expr,
                 "hard realtime asm backend does not allow float types");
        return -1;
    }
    return 0;
}

static int hard_rt_check_stmt_types(const ChengNode *stmt,
                                    const char *filename,
                                    ChengDiagList *diags) {
    if (!stmt) {
        return 0;
    }
    switch (stmt->kind) {
        case NK_VAR:
        case NK_LET:
        case NK_CONST: {
            const ChengNode *type_expr = stmt->len > 1 ? stmt->kids[1] : NULL;
            return hard_rt_check_type_expr(type_expr, filename, diags, stmt);
        }
        case NK_IF: {
            int rc = 0;
            if (stmt->len > 1) {
                if (hard_rt_check_stmt_types(stmt->kids[1], filename, diags) != 0) {
                    rc = -1;
                }
            }
            if (stmt->len > 2 && stmt->kids[2] && stmt->kids[2]->kind == NK_ELSE &&
                stmt->kids[2]->len > 0) {
                if (hard_rt_check_stmt_types(stmt->kids[2]->kids[0], filename, diags) != 0) {
                    rc = -1;
                }
            }
            return rc;
        }
        case NK_WHILE:
            if (stmt->len > 1) {
                return hard_rt_check_stmt_types(stmt->kids[1], filename, diags);
            }
            return 0;
        case NK_FOR:
            if (stmt->len > 2) {
                return hard_rt_check_stmt_types(stmt->kids[2], filename, diags);
            }
            return 0;
        case NK_STMT_LIST: {
            int rc = 0;
            for (size_t i = 0; i < stmt->len; i++) {
                if (hard_rt_check_stmt_types(stmt->kids[i], filename, diags) != 0) {
                    rc = -1;
                }
            }
            return rc;
        }
        default:
            return 0;
    }
}

static int hard_rt_check_function_types(const ChengNode *fn_node,
                                        const char *filename,
                                        ChengDiagList *diags) {
    if (!fn_node || fn_node->kind != NK_FN_DECL || fn_node->len < 4) {
        return 0;
    }
    int rc = 0;
    const ChengNode *params = fn_node->kids[1];
    const ChengNode *ret_type = fn_node->kids[2];
    const ChengNode *body = fn_node->kids[3];
    if (params && params->kind == NK_FORMAL_PARAMS) {
        for (size_t index = 0; index < params->len; index++) {
            const ChengNode *param = params->kids[index];
            const ChengNode *param_type = param && param->len > 1 ? param->kids[1] : NULL;
            if (hard_rt_check_type_expr(param_type, filename, diags, param) != 0) {
                rc = -1;
            }
        }
    }
    if (hard_rt_check_type_expr(ret_type, filename, diags, fn_node) != 0) {
        rc = -1;
    }
    if (hard_rt_check_stmt_types(body, filename, diags) != 0) {
        rc = -1;
    }
    return rc;
}

static int hard_rt_visit_recursion(const ChengNode *body,
                                   const AsmFuncTable *table,
                                   const ChengStrList *importc,
                                   int *state,
                                   const char *filename,
                                   ChengDiagList *diags);

static int hard_rt_visit_function(size_t index,
                                  const AsmFuncTable *table,
                                  const ChengStrList *importc,
                                  int *state,
                                  const char *filename,
                                  ChengDiagList *diags) {
    if (!table || index >= table->len) {
        return 0;
    }
    if (state[index] == 2) {
        return 0;
    }
    if (state[index] == 1) {
        const ChengNode *decl = table->items[index].decl;
        if (hard_rt_is_builtin_conversion(table->items[index].name)) {
            return 0;
        }
        add_diag(diags, filename, decl, "hard realtime asm backend does not allow recursion");
        return -1;
    }
    state[index] = 1;
    const AsmFuncEntry *entry = &table->items[index];
    if (hard_rt_is_builtin_conversion(entry->name)) {
        state[index] = 2;
        return 0;
    }
    if (entry->decl && entry->decl->len > 3) {
        const ChengNode *body = entry->decl->kids[3];
        if (hard_rt_visit_recursion(body, table, importc, state, filename, diags) != 0) {
            return -1;
        }
    }
    state[index] = 2;
    return 0;
}

static int hard_rt_visit_recursion(const ChengNode *body,
                                   const AsmFuncTable *table,
                                   const ChengStrList *importc,
                                   int *state,
                                   const char *filename,
                                   ChengDiagList *diags) {
    if (!body) {
        return 0;
    }
    int rc = 0;
    if (body->kind == NK_CALL && body->len > 0 && body->kids[0]) {
        const ChengNode *callee = call_target_node(body->kids[0]);
        const char *callee_name =
            callee && (callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) && callee->ident ? callee->ident : NULL;
        AsmFuncEntry *entry = callee_name ? asm_func_table_find((AsmFuncTable *)table, callee_name) : NULL;
        if (entry && entry->emit &&
            !asm_func_is_runtime_name(entry->name) &&
            !asm_func_is_importc_name(importc, entry->name) &&
            asm_func_decl_has_body(entry->decl)) {
            size_t idx = (size_t)(entry - table->items);
            if (hard_rt_visit_function(idx, table, importc, state, filename, diags) != 0) {
                rc = -1;
            }
        }
    }
    for (size_t index = 0; index < body->len; index++) {
        if (hard_rt_visit_recursion(body->kids[index], table, importc, state, filename, diags) != 0) {
            rc = -1;
        }
    }
    return rc;
}
static int hard_rt_check_string_len_node(AsmFuncContext *func_ctx,
                                         const ChengNode *node,
                                         const char *filename,
                                         ChengDiagList *diags) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_DOT_EXPR && node->len >= 2) {
        const ChengNode *member = node->kids[1];
        if (member && member->kind == NK_IDENT && member->ident &&
            strcmp(member->ident, "len") == 0 && expr_is_string_like(func_ctx, node->kids[0])) {
            add_diag(diags, filename, node, "hard realtime asm backend does not allow string.len");
            return -1;
        }
    }
    for (size_t index = 0; index < node->len; index++) {
        if (hard_rt_check_string_len_node(func_ctx, node->kids[index], filename, diags) != 0) {
            return -1;
        }
    }
    return 0;
}

static int hard_rt_check_function_string_len(const ChengNode *fn_node,
                                             const char *filename,
                                             ChengDiagList *diags) {
    if (!fn_node || fn_node->kind != NK_FN_DECL || fn_node->len < 4) {
        return 0;
    }
    const ChengNode *params = fn_node->kids[1];
    const ChengNode *body = fn_node->kids[3];
    AsmFuncContext func_ctx = {0};
    asm_var_table_init(&func_ctx.locals);
    if (params && params->kind == NK_FORMAL_PARAMS && params->len > 0) {
        for (size_t param_index = 0; param_index < params->len; param_index++) {
            const ChengNode *param = params->kids[param_index];
            if (!param || param->len < 1 || !param->kids[0] ||
                param->kids[0]->kind != NK_IDENT || !param->kids[0]->ident) {
                add_diag(diags, filename, param, "unsupported parameter in asm backend");
                asm_var_table_free(&func_ctx.locals);
                return -1;
            }
            const ChengNode *param_type = param->len > 1 ? param->kids[1] : NULL;
            if (asm_var_table_add(&func_ctx.locals, param->kids[0]->ident, param_type) != 0) {
                add_diag(diags, filename, param, "failed to allocate parameter");
                asm_var_table_free(&func_ctx.locals);
                return -1;
            }
        }
    }
    if (collect_locals_from_list(body, &func_ctx.locals, filename, diags) != 0) {
        asm_var_table_free(&func_ctx.locals);
        return -1;
    }
    int rc = hard_rt_check_string_len_node(&func_ctx, body, filename, diags);
    asm_var_table_free(&func_ctx.locals);
    return rc;
}

static int hard_rt_check(const ChengNode *root,
                         const char *filename,
                         ChengDiagList *diags) {
    const char *atomic = getenv("CHENG_MM_ATOMIC");
    if (!atomic || atomic[0] == '\0' || strcmp(atomic, "0") != 0) {
        add_diag(diags, filename, root, "hard realtime asm backend requires CHENG_MM_ATOMIC=0");
        return -1;
    }
    if (!hard_rt_mm_disabled()) {
        add_diag(diags, filename, root, "hard realtime asm backend requires CHENG_MM=off");
        return -1;
    }
    int profile = hard_rt_profile_level();
    const ChengNode *list = get_top_level_list(root);
    if (!list || list->kind != NK_STMT_LIST) {
        return 0;
    }
    int rc = 0;
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_PRAGMA) {
            if (hard_rt_check_pragma_node(stmt, filename, diags, profile, 1) != 0) {
                rc = -1;
            }
        }
        if (stmt->kind != NK_PRAGMA &&
            stmt->kind != NK_FN_DECL &&
            stmt->kind != NK_TYPE_DECL &&
            stmt->kind != NK_OBJECT_DECL &&
            stmt->kind != NK_ENUM_DECL) {
            if (hard_rt_check_profile_node(stmt, filename, diags, profile) != 0) {
                rc = -1;
            }
        }
    }
    ChengStrList importc;
    cheng_strlist_init(&importc);
    collect_importc_names(list, &importc);
    AsmFuncTable table;
    asm_func_table_init(&table);
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (stmt && stmt->kind == NK_FN_DECL) {
            if (asm_func_table_add(&table, stmt) != 0) {
                asm_func_table_free(&table);
                cheng_strlist_free(&importc);
                add_diag(diags, filename, stmt, "failed to collect function declarations");
                return -1;
            }
        }
    }
    AsmFuncEntry *main_entry = asm_func_table_find(&table, "main");
    if (!main_entry) {
        asm_func_table_free(&table);
        cheng_strlist_free(&importc);
        add_diag(diags, filename, root, "asm backend requires a main function");
        return -1;
    }
    if (asm_func_is_importc_name(&importc, "main")) {
        add_diag(diags, filename, main_entry->decl, "hard realtime asm backend does not allow importc main");
        rc = -1;
    }
    main_entry->emit = 1;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (size_t index = 0; index < table.len; index++) {
            AsmFuncEntry *entry = &table.items[index];
            if (entry->emit && !entry->scanned) {
                entry->scanned = 1;
                if (asm_func_is_runtime_name(entry->name) ||
                    asm_func_is_importc_name(&importc, entry->name) ||
                    !asm_func_decl_has_body(entry->decl)) {
                    continue;
                }
                if (entry->decl && entry->decl->len > 3) {
                    const ChengNode *body = entry->decl->kids[3];
                    asm_func_table_mark_calls(body, &table, &importc, &changed);
                }
            }
        }
    }
    if (hard_rt_check_task_pragmas_in_list(list, filename, diags, profile) != 0) {
        rc = -1;
    }
    if (hard_rt_check_stmt_types(list, filename, diags) != 0) {
        rc = -1;
    }
    if (hard_rt_check_loops_in_list(list, filename, diags) != 0) {
        rc = -1;
    }
    int *rec_state = (int *)calloc(table.len, sizeof(int));
    if (!rec_state && table.len > 0) {
        asm_func_table_free(&table);
        cheng_strlist_free(&importc);
        add_diag(diags, filename, root, "hard realtime asm backend failed to allocate recursion state");
        return -1;
    }
    for (size_t index = 0; index < table.len; index++) {
        AsmFuncEntry *entry = &table.items[index];
        if (!entry->emit) {
            continue;
        }
        if (asm_func_is_runtime_name(entry->name)) {
            add_diag(diags, filename, entry->decl, "hard realtime asm backend does not allow runtime functions");
            rc = -1;
        }
        if (asm_func_is_importc_name(&importc, entry->name)) {
            add_diag(diags, filename, entry->decl, "hard realtime asm backend does not allow importc functions");
            rc = -1;
        }
        if (entry->decl && entry->decl->len > 3) {
            const ChengNode *body = entry->decl->kids[3];
            if (hard_rt_check_profile_node(entry->decl, filename, diags, profile) != 0) {
                rc = -1;
            }
            if (hard_rt_check_calls_in_node(body, &importc, filename, diags) != 0) {
                rc = -1;
            }
            if (hard_rt_check_function_string_len(entry->decl, filename, diags) != 0) {
                rc = -1;
            }
            if (hard_rt_check_function_types(entry->decl, filename, diags) != 0) {
                rc = -1;
            }
            if (hard_rt_check_loops_in_list(body, filename, diags) != 0) {
                rc = -1;
            }
        }
    }
    for (size_t index = 0; index < table.len; index++) {
        AsmFuncEntry *entry = &table.items[index];
        if (!entry->emit) {
            continue;
        }
        if (asm_func_is_runtime_name(entry->name) || asm_func_is_importc_name(&importc, entry->name)) {
            continue;
        }
        if (!asm_func_decl_has_body(entry->decl)) {
            continue;
        }
        if (hard_rt_visit_function(index, &table, &importc, rec_state, filename, diags) != 0) {
            rc = -1;
            break;
        }
    }
    free(rec_state);
    asm_func_table_free(&table);
    cheng_strlist_free(&importc);
    return rc;
}

static int expr_is_seq_like(AsmFuncContext *func_ctx, const ChengNode *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->type_id == CHENG_TYPE_SEQ) {
        return 1;
    }
    const ChengNode *base = strip_paren_expr(expr);
    if (base && base->kind == NK_IDENT) {
        const ChengNode *type_expr = var_type_expr_from_ident(func_ctx, base);
        if (type_expr_is_seq(type_expr)) {
            return 1;
        }
        const char *type_name = type_name_from_type_expr(type_expr);
        if (type_name && strncmp(type_name, "seq_", 4) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *expr_type_name(AsmFuncContext *func_ctx, const ChengNode *expr) {
    const ChengNode *base = strip_paren_expr(expr);
    if (base && base->kind == NK_IDENT) {
        return type_name_from_type_expr(var_type_expr_from_ident(func_ctx, base));
    }
    return NULL;
}

static int asm_layout_field_info(AsmLayout *layout,
                                 const char *type_name,
                                 const char *field_name,
                                 size_t *out_offset,
                                 size_t *out_size) {
    if (!layout || !type_name || !field_name || !out_offset || !out_size) {
        return 0;
    }
    const AsmTypeLayout *type_layout = asm_layout_find_type(layout, type_name);
    if (!type_layout || !type_layout->fields) {
        return 0;
    }
    for (size_t index = 0; index < type_layout->field_len; index++) {
        const AsmFieldLayout *field = &type_layout->fields[index];
        if (field->name && field_name) {
            if (strcmp(field->name, field_name) == 0) {
                *out_offset = field->offset;
                *out_size = field->size;
                return 1;
            }
            size_t field_len = strlen(field_name);
            size_t field_base = field_len;
            while (field_base > 0 && isdigit((unsigned char)field_name[field_base - 1])) {
                field_base--;
            }
            if (field_base > 0 && field_name[field_base - 1] == '.') {
                field_base--;
            } else {
                field_base = field_len;
            }
            size_t name_len = strlen(field->name);
            size_t name_base = name_len;
            while (name_base > 0 && isdigit((unsigned char)field->name[name_base - 1])) {
                name_base--;
            }
            if (name_base > 0 && field->name[name_base - 1] == '.') {
                name_base--;
            } else {
                name_base = name_len;
            }
            if (field_base > 0 && name_base > 0 &&
                field_base == name_base &&
                strncmp(field->name, field_name, field_base) == 0) {
                *out_offset = field->offset;
                *out_size = field->size;
                return 1;
            }
        }
    }
    return 0;
}

typedef struct {
    size_t ok_offset;
    size_t ok_size;
    size_t value_offset;
    size_t value_size;
    size_t err_offset;
    size_t err_size;
} ResultFieldInfo;

static int result_field_info(AsmEmitContext *context, const char *type_name, ResultFieldInfo *out) {
    if (!context || !context->layout || !type_name || !out) {
        return 0;
    }
    if (!asm_layout_field_info(context->layout, type_name, "ok", &out->ok_offset, &out->ok_size)) {
        return 0;
    }
    if (!asm_layout_field_info(context->layout, type_name, "value", &out->value_offset, &out->value_size)) {
        return 0;
    }
    if (!asm_layout_field_info(context->layout, type_name, "err", &out->err_offset, &out->err_size)) {
        return 0;
    }
    return 1;
}

static int emit_expr_x86_64(AsmFuncContext *func_ctx, const ChengNode *expr);
static int emit_addr_x86_64(AsmFuncContext *func_ctx,
                            const ChengNode *expr,
                            AsmAccessKind *out_kind);
static int emit_index_addr_x86_64(AsmFuncContext *func_ctx,
                                  const ChengNode *expr,
                                  AsmAccessKind *out_kind,
                                  int *out_is_byte);
static void emit_orc_call_x86_64(AsmFuncContext *func_ctx,
                                 const char *name,
                                 const char *value_reg,
                                 int preserve_rax);

static void emit_orc_call_x86_64(AsmFuncContext *func_ctx,
                                 const char *name,
                                 const char *value_reg,
                                 int preserve_rax) {
    if (!func_ctx || !name || !value_reg) {
        return;
    }
    FILE *output = func_ctx->context->output;
    if (preserve_rax) {
        fprintf(output, "  pushq %%rax\n");
    }
    if (strcmp(value_reg, "%rdi") != 0) {
        fprintf(output, "  movq %s, %%rdi\n", value_reg);
    }
    const char *call_sym = name;
    const char *runtime_sym = NULL;
    if (asm_runtime_lookup_symbol(name, &runtime_sym)) {
        call_sym = runtime_sym;
    }
    char sym_buf[256];
    call_sym = asm_mangle_symbol(func_ctx->context, call_sym, sym_buf, sizeof(sym_buf));
    fprintf(output, "  call %s\n", call_sym);
    if (preserve_rax) {
        fprintf(output, "  popq %%rax\n");
    }
}

static int emit_dot_addr_x86_64(AsmFuncContext *func_ctx,
                                const ChengNode *expr,
                                AsmAccessKind *out_kind) {
    if (!func_ctx || !expr || expr->kind != NK_DOT_EXPR || expr->len < 2) {
        return -1;
    }
    const ChengNode *base = expr->kids[0];
    const ChengNode *member = expr->kids[1];
    if (!member || member->kind != NK_IDENT || !member->ident) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                 "unsupported dot expression in asm backend");
        return -1;
    }
    const ChengNode *base_for_type = base;
    if (base_for_type && (base_for_type->kind == NK_HIDDEN_DEREF || base_for_type->kind == NK_DEREF_EXPR)) {
        if (base_for_type->len > 0) {
            base_for_type = base_for_type->kids[0];
        }
    } else if (base_for_type && base_for_type->kind == NK_PREFIX && base_for_type->len > 1 &&
               base_for_type->kids[0] && base_for_type->kids[0]->ident &&
               strcmp(base_for_type->kids[0]->ident, "*") == 0) {
        base_for_type = base_for_type->kids[1];
    }
    base_for_type = strip_paren_expr(base_for_type);
    const char *type_name = expr_type_name(func_ctx, base_for_type);
    size_t offset = 0;
    size_t size = 8;
    int have_field = 0;
    if (type_name && func_ctx->context->layout) {
        have_field = asm_layout_field_info(func_ctx->context->layout,
                                           type_name,
                                           member->ident,
                                           &offset,
                                           &size);
    }
    if (!have_field) {
        if (expr_is_seq_like(func_ctx, base_for_type ? base_for_type : base)) {
            if (strcmp(member->ident, "len") == 0) {
                offset = 0;
                size = 4;
                have_field = 1;
            } else if (strcmp(member->ident, "cap") == 0) {
                offset = 4;
                size = 4;
                have_field = 1;
            } else if (strcmp(member->ident, "buffer") == 0) {
                offset = 8;
                size = 8;
                have_field = 1;
            }
        }
    }
    if (!have_field) {
        if (strcmp(member->ident, "len") == 0) {
            offset = 0;
            size = 4;
            have_field = 1;
        } else if (strcmp(member->ident, "cap") == 0) {
            offset = 4;
            size = 4;
            have_field = 1;
        } else if (strcmp(member->ident, "buffer") == 0) {
            offset = 8;
            size = 8;
            have_field = 1;
        }
    }
    if (!have_field) {
        char msg[128];
        snprintf(msg, sizeof(msg), "unsupported dot field %s.%s in asm backend",
                 type_name ? type_name : "<?>",
                 member && member->ident ? member->ident : "?");
        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr, msg);
        return -1;
    }
    const ChengNode *base_addr = strip_paren_expr(base);
    int base_is_indirect = 0;
    if (base_addr && base_addr->kind == NK_IDENT) {
        const ChengNode *type_expr = var_type_expr_from_ident(func_ctx, base_addr);
        if (type_expr_is_result(type_expr) || type_expr_is_indirect(func_ctx->context, type_expr)) {
            base_is_indirect = 1;
        }
    }
    if (base_addr && base_addr->kind == NK_DOT_EXPR) {
        if (emit_dot_addr_x86_64(func_ctx, base_addr, NULL) != 0) {
            return -1;
        }
    } else if (base_addr && (base_addr->kind == NK_HIDDEN_DEREF || base_addr->kind == NK_DEREF_EXPR)) {
        if (base_addr->len < 1 || !base_addr->kids[0]) {
            add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                     "invalid deref base in dot expression");
            return -1;
        }
        if (emit_expr_x86_64(func_ctx, base_addr->kids[0]) != 0) {
            return -1;
        }
    } else if (base_addr && base_addr->kind == NK_PREFIX && base_addr->len > 1 &&
               base_addr->kids[0] && base_addr->kids[0]->ident &&
               strcmp(base_addr->kids[0]->ident, "*") == 0) {
        if (emit_expr_x86_64(func_ctx, base_addr->kids[1]) != 0) {
            return -1;
        }
    } else if (base_addr && base_addr->kind == NK_IDENT) {
        if (base_is_indirect) {
            if (emit_expr_x86_64(func_ctx, base_addr) != 0) {
                return -1;
            }
        } else {
            if (emit_addr_x86_64(func_ctx, base_addr, NULL) != 0) {
                return -1;
            }
        }
    } else {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                 "unsupported dot base expression in asm backend");
        return -1;
    }
    if (offset != 0) {
        fprintf(func_ctx->context->output, "  addq $%zu, %%rax\n", offset);
    }
    if (out_kind) {
        *out_kind = asm_access_kind_from_size(size);
    }
    return 0;
}

static int emit_addr_x86_64(AsmFuncContext *func_ctx,
                            const ChengNode *expr,
                            AsmAccessKind *out_kind) {
    if (!func_ctx || !expr) {
        return -1;
    }
    switch (expr->kind) {
        case NK_PAR:
            if (expr->len > 0) {
                return emit_addr_x86_64(func_ctx, expr->kids[0], out_kind);
            }
            return -1;
        case NK_IDENT: {
            int offset = 0;
            if (asm_var_table_find(&func_ctx->locals, expr->ident, &offset)) {
                fprintf(func_ctx->context->output, "  leaq %d(%%rbp), %%rax\n", offset);
                if (out_kind) {
                    *out_kind = ASM_ACCESS_QWORD;
                }
                return 0;
            }
            const AsmDataItem *global = asm_find_global_item(func_ctx->context, expr->ident);
            if (global && global->is_global) {
                char sym_buf[256];
                const char *sym = asm_data_symbol_name(func_ctx->context, global, sym_buf, sizeof(sym_buf));
                fprintf(func_ctx->context->output, "  leaq %s(%%rip), %%rax\n", sym);
                if (out_kind) {
                    *out_kind = asm_access_kind_from_size(global->size);
                }
                return 0;
            }
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown identifier '%s' in asm backend",
                         expr->ident ? expr->ident : "?");
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr, msg);
            }
            return -1;
        }
        case NK_DOT_EXPR:
            return emit_dot_addr_x86_64(func_ctx, expr, out_kind);
        case NK_BRACKET_EXPR: {
            int is_byte = 0;
            AsmAccessKind access = ASM_ACCESS_QWORD;
            if (emit_index_addr_x86_64(func_ctx, expr, &access, &is_byte) != 0) {
                return -1;
            }
            if (is_byte) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "string indexing is not assignable in asm backend");
                return -1;
            }
            if (out_kind) {
                *out_kind = access;
            }
            return 0;
        }
        case NK_HIDDEN_DEREF:
        case NK_DEREF_EXPR: {
            if (expr->len < 1 || !expr->kids[0]) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid deref expression");
                return -1;
            }
            if (emit_expr_x86_64(func_ctx, expr->kids[0]) != 0) {
                return -1;
            }
            if (out_kind) {
                *out_kind = ASM_ACCESS_QWORD;
            }
            return 0;
        }
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0] || !expr->kids[0]->ident) {
                return -1;
            }
            const char *op = expr->kids[0]->ident;
            if (strcmp(op, "*") == 0) {
                if (emit_expr_x86_64(func_ctx, expr->kids[1]) != 0) {
                    return -1;
                }
                if (out_kind) {
                    *out_kind = ASM_ACCESS_QWORD;
                }
                return 0;
            }
            if (strcmp(op, "&") == 0) {
                return emit_addr_x86_64(func_ctx, expr->kids[1], out_kind);
            }
            break;
        }
        default:
            break;
    }
    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
             "unsupported address expression in asm backend");
    return -1;
}

static int emit_index_addr_x86_64(AsmFuncContext *func_ctx,
                                  const ChengNode *expr,
                                  AsmAccessKind *out_kind,
                                  int *out_is_byte) {
    if (!func_ctx || !expr || expr->kind != NK_BRACKET_EXPR || expr->len < 2) {
        add_diag(func_ctx ? func_ctx->context->diags : NULL,
                 func_ctx ? func_ctx->context->filename : NULL,
                 expr,
                 "invalid bracket expression in asm backend");
        return -1;
    }
    if (expr->len > 2) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                 "asm backend does not support multi-index bracket expressions");
        return -1;
    }
    const ChengNode *base = expr->kids[0];
    const ChengNode *index = expr->kids[1];
    const ChengNode *elem_type = NULL;
    AsmIndexKind kind = asm_index_kind_for_expr(func_ctx, base, &elem_type);
    if (kind == ASM_INDEX_NONE) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                 "unsupported bracket expression in asm backend");
        return -1;
    }
    size_t elem_size = 8;
    int is_byte = 0;
    if (kind == ASM_INDEX_STRING) {
        elem_size = 1;
        is_byte = 1;
    } else {
        elem_size = asm_elem_size_for_type(func_ctx->context, elem_type);
        if (elem_size == 0) {
            elem_size = 8;
        }
    }
    if (out_kind) {
        *out_kind = asm_access_kind_from_size(elem_size);
    }
    if (out_is_byte) {
        *out_is_byte = is_byte;
    }
    FILE *output = func_ctx->context->output;
    if (kind == ASM_INDEX_STRING) {
        if (emit_expr_x86_64(func_ctx, base) != 0) {
            return -1;
        }
        fprintf(output, "  pushq %%rax\n");
        if (emit_expr_x86_64(func_ctx, index) != 0) {
            fprintf(output, "  addq $8, %%rsp\n");
            return -1;
        }
        fprintf(output, "  popq %%rcx\n");
        fprintf(output, "  addq %%rax, %%rcx\n");
        fprintf(output, "  movq %%rcx, %%rax\n");
        return 0;
    }
    if (emit_addr_x86_64(func_ctx, base, NULL) != 0) {
        return -1;
    }
    if (kind == ASM_INDEX_SEQ) {
        fprintf(output, "  movq 8(%%rax), %%rax\n");
    }
    fprintf(output, "  pushq %%rax\n");
    if (emit_expr_x86_64(func_ctx, index) != 0) {
        fprintf(output, "  addq $8, %%rsp\n");
        return -1;
    }
    fprintf(output, "  popq %%rcx\n");
    if (elem_size != 1) {
        fprintf(output, "  movq $%zu, %%r11\n", elem_size);
        fprintf(output, "  imulq %%r11, %%rax\n");
    }
    fprintf(output, "  addq %%rcx, %%rax\n");
    return 0;
}

static int emit_store_x86_64(AsmFuncContext *func_ctx,
                             const ChengNode *lhs,
                             const ChengNode *rhs) {
    if (!func_ctx || !lhs || !rhs) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    if (expr_contains_call(func_ctx, lhs)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, lhs,
                 "call in assignment target is not supported in asm backend");
        return -1;
    }
    if (orc_mm_enabled()) {
        const ChengNode *lhs_base = strip_paren_expr(lhs);
        if (lhs_base && (lhs_base->kind == NK_IDENT || lhs_base->kind == NK_SYMBOL)) {
            const ChengNode *type_expr = var_type_expr_from_ident(func_ctx, lhs_base);
            if (orc_type_is_managed(type_expr)) {
                if (emit_expr_x86_64(func_ctx, rhs) != 0) {
                    return -1;
                }
                if (orc_expr_is_borrowed(rhs)) {
                    emit_orc_call_x86_64(func_ctx, "memRetain", "%rax", 1);
                }
                fprintf(output, "  movq %%rax, %%r10\n");
                if (emit_addr_x86_64(func_ctx, lhs_base, NULL) != 0) {
                    return -1;
                }
                fprintf(output, "  movq (%%rax), %%r11\n");
                fprintf(output, "  movq %%r10, (%%rax)\n");
                emit_orc_call_x86_64(func_ctx, "memRelease", "%r11", 0);
                return 0;
            }
        }
    }
    if (emit_expr_x86_64(func_ctx, rhs) != 0) {
        return -1;
    }
    fprintf(output, "  pushq %%rax\n");
    AsmAccessKind access = ASM_ACCESS_QWORD;
    if (emit_addr_x86_64(func_ctx, lhs, &access) != 0) {
        fprintf(output, "  addq $8, %%rsp\n");
        return -1;
    }
    fprintf(output, "  popq %%r11\n");
    switch (access) {
        case ASM_ACCESS_BYTE:
            fprintf(output, "  movb %%r11b, (%%rax)\n");
            break;
        case ASM_ACCESS_WORD:
            fprintf(output, "  movw %%r11w, (%%rax)\n");
            break;
        case ASM_ACCESS_DWORD:
            fprintf(output, "  movl %%r11d, (%%rax)\n");
            break;
        case ASM_ACCESS_QWORD:
        default:
            fprintf(output, "  movq %%r11, (%%rax)\n");
            break;
    }
    return 0;
}

static int emit_result_unwrap_x86_64(AsmFuncContext *func_ctx, const ChengNode *value) {
    if (!func_ctx || !value) {
        return -1;
    }
    const char *type_name = result_type_name_from_expr(func_ctx, value);
    if (!type_name || !type_name_is_result(type_name)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, value,
                 "Result/? expects a Result value in asm backend");
        return -1;
    }
    if (func_ctx->ret_is_result && func_ctx->ret_type_expr) {
        const char *ret_name = type_name_from_type_expr(func_ctx->ret_type_expr);
        if (ret_name && strcmp(ret_name, type_name) != 0) {
            add_diag(func_ctx->context->diags, func_ctx->context->filename, value,
                     "Result/? type mismatch in asm backend");
            return -1;
        }
    }
    ResultFieldInfo info;
    if (!result_field_info(func_ctx->context, type_name, &info)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, value,
                 "Result/? missing layout info in asm backend");
        return -1;
    }
    if (emit_expr_x86_64(func_ctx, value) != 0) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    fprintf(output, "  movq %%rax, %%r11\n");
    switch (asm_access_kind_from_size(info.ok_size)) {
        case ASM_ACCESS_BYTE:
            fprintf(output, "  movzbl %zu(%%r11), %%eax\n", info.ok_offset);
            break;
        case ASM_ACCESS_WORD:
            fprintf(output, "  movzwl %zu(%%r11), %%eax\n", info.ok_offset);
            break;
        case ASM_ACCESS_DWORD:
            fprintf(output, "  movl %zu(%%r11), %%eax\n", info.ok_offset);
            break;
        case ASM_ACCESS_QWORD:
        default:
            fprintf(output, "  movq %zu(%%r11), %%rax\n", info.ok_offset);
            break;
    }
    int err_label = asm_next_label(func_ctx->context);
    int done_label = asm_next_label(func_ctx->context);
    fprintf(output, "  cmpq $0, %%rax\n");
    fprintf(output, "  je .L%d\n", err_label);
    switch (asm_access_kind_from_size(info.value_size)) {
        case ASM_ACCESS_BYTE:
            fprintf(output, "  movzbl %zu(%%r11), %%eax\n", info.value_offset);
            break;
        case ASM_ACCESS_WORD:
            fprintf(output, "  movzwl %zu(%%r11), %%eax\n", info.value_offset);
            break;
        case ASM_ACCESS_DWORD:
            fprintf(output, "  movl %zu(%%r11), %%eax\n", info.value_offset);
            break;
        case ASM_ACCESS_QWORD:
        default:
            fprintf(output, "  movq %zu(%%r11), %%rax\n", info.value_offset);
            break;
    }
    fprintf(output, "  jmp .L%d\n", done_label);
    asm_emit_label(output, err_label);
    if (func_ctx->ret_is_result) {
        fprintf(output, "  movq %%r11, %%rax\n");
        fprintf(output, "  jmp .L%d\n", func_ctx->end_label);
    } else {
        if (asm_access_kind_from_size(info.err_size) == ASM_ACCESS_QWORD) {
            fprintf(output, "  movq %zu(%%r11), %%rdi\n", info.err_offset);
        } else if (asm_access_kind_from_size(info.err_size) == ASM_ACCESS_DWORD) {
            fprintf(output, "  movl %zu(%%r11), %%edi\n", info.err_offset);
        } else {
            fprintf(output, "  movq %zu(%%r11), %%rdi\n", info.err_offset);
        }
        emit_orc_call_x86_64(func_ctx, "panic", "%rdi", 0);
        emit_x86_64_imm(output, 0);
        fprintf(output, "  jmp .L%d\n", func_ctx->end_label);
    }
    asm_emit_label(output, done_label);
    return 0;
}

static int emit_expr_infix_x86_64(AsmFuncContext *func_ctx, const ChengNode *expr) {
    if (!func_ctx || !expr || expr->len < 3 || !expr->kids[0] || !expr->kids[0]->ident) {
        return -1;
    }
    const char *op = expr->kids[0]->ident;
    const ChengNode *lhs = expr->kids[1];
    const ChengNode *rhs = expr->kids[2];
    if (emit_expr_x86_64(func_ctx, lhs) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  pushq %%rax\n");
    if (emit_expr_x86_64(func_ctx, rhs) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  popq %%r10\n");
    if (strcmp(op, "+") == 0) {
        fprintf(func_ctx->context->output, "  addq %%r10, %%rax\n");
        return 0;
    }
    if (strcmp(op, "-") == 0) {
        fprintf(func_ctx->context->output, "  subq %%rax, %%r10\n");
        fprintf(func_ctx->context->output, "  movq %%r10, %%rax\n");
        return 0;
    }
    if (strcmp(op, "*") == 0) {
        fprintf(func_ctx->context->output, "  imulq %%r10, %%rax\n");
        return 0;
    }
    if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
        fprintf(func_ctx->context->output, "  movq %%rax, %%r11\n");
        fprintf(func_ctx->context->output, "  movq %%r10, %%rax\n");
        fprintf(func_ctx->context->output, "  cqo\n");
        fprintf(func_ctx->context->output, "  idivq %%r11\n");
        return 0;
    }
    if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
        fprintf(func_ctx->context->output, "  movq %%rax, %%r11\n");
        fprintf(func_ctx->context->output, "  movq %%r10, %%rax\n");
        fprintf(func_ctx->context->output, "  cqo\n");
        fprintf(func_ctx->context->output, "  idivq %%r11\n");
        fprintf(func_ctx->context->output, "  movq %%rdx, %%rax\n");
        return 0;
    }
    if (strcmp(op, "&") == 0) {
        fprintf(func_ctx->context->output, "  andq %%r10, %%rax\n");
        return 0;
    }
    if (strcmp(op, "|") == 0) {
        fprintf(func_ctx->context->output, "  orq %%r10, %%rax\n");
        return 0;
    }
    if (strcmp(op, "^") == 0) {
        fprintf(func_ctx->context->output, "  xorq %%r10, %%rax\n");
        return 0;
    }
    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
        fprintf(func_ctx->context->output, "  movq %%rax, %%rcx\n");
        fprintf(func_ctx->context->output, "  movq %%r10, %%rax\n");
        if (strcmp(op, "<<") == 0) {
            fprintf(func_ctx->context->output, "  shlq %%cl, %%rax\n");
        } else {
            fprintf(func_ctx->context->output, "  sarq %%cl, %%rax\n");
        }
        return 0;
    }
    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        fprintf(func_ctx->context->output, "  cmpq $0, %%r10\n");
        fprintf(func_ctx->context->output, "  setne %%al\n");
        fprintf(func_ctx->context->output, "  movzbl %%al, %%r11d\n");
        fprintf(func_ctx->context->output, "  cmpq $0, %%rax\n");
        fprintf(func_ctx->context->output, "  setne %%al\n");
        fprintf(func_ctx->context->output, "  movzbl %%al, %%eax\n");
        if (strcmp(op, "&&") == 0) {
            fprintf(func_ctx->context->output, "  andl %%r11d, %%eax\n");
        } else {
            fprintf(func_ctx->context->output, "  orl %%r11d, %%eax\n");
        }
        return 0;
    }
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
        fprintf(func_ctx->context->output, "  cmpq %%rax, %%r10\n");
        if (strcmp(op, "==") == 0) {
            fprintf(func_ctx->context->output, "  sete %%al\n");
        } else if (strcmp(op, "!=") == 0) {
            fprintf(func_ctx->context->output, "  setne %%al\n");
        } else if (strcmp(op, "<") == 0) {
            fprintf(func_ctx->context->output, "  setl %%al\n");
        } else if (strcmp(op, "<=") == 0) {
            fprintf(func_ctx->context->output, "  setle %%al\n");
        } else if (strcmp(op, ">") == 0) {
            fprintf(func_ctx->context->output, "  setg %%al\n");
        } else {
            fprintf(func_ctx->context->output, "  setge %%al\n");
        }
        fprintf(func_ctx->context->output, "  movzbl %%al, %%eax\n");
        return 0;
    }
    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
             "unsupported infix operator in asm backend");
    return -1;
}

static int emit_expr_x86_64(AsmFuncContext *func_ctx, const ChengNode *expr) {
    if (!func_ctx || !expr) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    switch (expr->kind) {
        case NK_PAR:
            if (expr->len > 0) {
                return emit_expr_x86_64(func_ctx, expr->kids[0]);
            }
            return -1;
        case NK_INT_LIT:
            emit_x86_64_imm(output, expr->int_val);
            return 0;
        case NK_NIL_LIT:
            emit_x86_64_imm(output, 0);
            return 0;
        case NK_CHAR_LIT: {
            int ch = 0;
            if (!decode_char_lit(expr->str_val, &ch)) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid char literal");
                return -1;
            }
            emit_x86_64_imm(output, (int64_t)ch);
            return 0;
        }
        case NK_BOOL_LIT: {
            int val = 0;
            if (expr->ident && strcmp(expr->ident, "true") == 0) {
                val = 1;
            }
            emit_x86_64_imm(output, (int64_t)val);
            return 0;
        }
        case NK_STR_LIT: {
            const char *label = NULL;
            if (func_ctx->context->layout) {
                label = asm_layout_string_label(func_ctx->context->layout,
                                                expr->str_val ? expr->str_val : "");
            }
            if (!label) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "missing string literal in asm layout");
                return -1;
            }
            fprintf(output, "  leaq %s(%%rip), %%rax\n", label);
            return 0;
        }
        case NK_IDENT: {
            int offset = 0;
            if (asm_var_table_find(&func_ctx->locals, expr->ident, &offset)) {
                fprintf(output, "  movq %d(%%rbp), %%rax\n", offset);
                return 0;
            }
            const AsmDataItem *global = asm_find_global_item(func_ctx->context, expr->ident);
            if (global && global->is_global) {
                char sym_buf[256];
                const char *sym = asm_data_symbol_name(func_ctx->context, global, sym_buf, sizeof(sym_buf));
                switch (asm_access_kind_from_size(global->size)) {
                    case ASM_ACCESS_BYTE:
                        fprintf(output, "  movzbl %s(%%rip), %%eax\n", sym);
                        break;
                    case ASM_ACCESS_WORD:
                        fprintf(output, "  movzwl %s(%%rip), %%eax\n", sym);
                        break;
                    case ASM_ACCESS_DWORD:
                        fprintf(output, "  movl %s(%%rip), %%eax\n", sym);
                        break;
                    case ASM_ACCESS_QWORD:
                    default:
                        fprintf(output, "  movq %s(%%rip), %%rax\n", sym);
                        break;
                }
                return 0;
            }
            if (is_type_call_target(func_ctx->context, expr)) {
                emit_x86_64_imm(output, 0);
                return 0;
            }
            if (expr->ident && strncmp(expr->ident, "__cheng_", 8) == 0) {
                emit_x86_64_imm(output, 0);
                return 0;
            }
            if (expr->ident && isupper((unsigned char)expr->ident[0])) {
                emit_x86_64_imm(output, 0);
                return 0;
            }
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown identifier '%s' in asm backend",
                         expr->ident ? expr->ident : "?");
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr, msg);
            }
            return -1;
        }
        case NK_CAST: {
            const ChengNode *value = NULL;
            if (expr->len > 1) {
                value = expr->kids[1];
            } else if (expr->len > 0) {
                value = expr->kids[0];
            }
            if (!value) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid cast expression");
                return -1;
            }
            return emit_expr_x86_64(func_ctx, value);
        }
        case NK_POSTFIX: {
            const ChengNode *value = NULL;
            const ChengNode *op = NULL;
            const char *op_name = NULL;
            if (expr->len > 0) {
                op = expr->kids[0];
            }
            if (op && (op->kind == NK_IDENT || op->kind == NK_SYMBOL)) {
                op_name = op->ident;
            }
            if (expr->len > 1) {
                value = expr->kids[1];
            } else if (expr->len > 0) {
                value = expr->kids[0];
            }
            if (!value) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid postfix expression");
                return -1;
            }
            if (op_name && strcmp(op_name, "?") == 0) {
                return emit_result_unwrap_x86_64(func_ctx, value);
            }
            return emit_expr_x86_64(func_ctx, value);
        }
        case NK_BRACKET_EXPR: {
            if (expr->len > 0 && expr->kids[0] &&
                (expr->kids[0]->kind == NK_IDENT || expr->kids[0]->kind == NK_SYMBOL) &&
                expr->kids[0]->ident && strcmp(expr->kids[0]->ident, "default") == 0) {
                if (expr->len != 2) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "default requires one type argument");
                    return -1;
                }
                const ChengNode *arg = expr->kids[1];
                if (!arg || !is_type_call_target(func_ctx->context, arg)) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "default requires a type argument");
                    return -1;
                }
                emit_x86_64_imm(output, 0);
                return 0;
            }
            if (is_type_call_target(func_ctx->context, expr)) {
                emit_x86_64_imm(output, 0);
                return 0;
            }
            if (expr->len >= 2 && expr->kids[0] && expr->kids[1]) {
                AsmIndexKind kind = asm_index_kind_for_expr(func_ctx, expr->kids[0], NULL);
                if (kind != ASM_INDEX_NONE) {
                    int is_byte = 0;
                    AsmAccessKind access = ASM_ACCESS_QWORD;
                    if (emit_index_addr_x86_64(func_ctx, expr, &access, &is_byte) != 0) {
                        return -1;
                    }
                    if (is_byte) {
                        access = ASM_ACCESS_BYTE;
                    }
                    switch (access) {
                        case ASM_ACCESS_BYTE:
                            fprintf(output, "  movzbl (%%rax), %%eax\n");
                            break;
                        case ASM_ACCESS_WORD:
                            fprintf(output, "  movzwl (%%rax), %%eax\n");
                            break;
                        case ASM_ACCESS_DWORD:
                            fprintf(output, "  movl (%%rax), %%eax\n");
                            break;
                        case ASM_ACCESS_QWORD:
                        default:
                            fprintf(output, "  movq (%%rax), %%rax\n");
                            break;
                    }
                    return 0;
                }
            }
            if (expr->len > 0 && expr->kids[0]) {
                return emit_expr_x86_64(func_ctx, expr->kids[0]);
            }
            add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                     "invalid bracket expression in asm backend");
            return -1;
        }
        case NK_PTR_TY:
        case NK_REF_TY:
        case NK_VAR_TY:
        case NK_FN_TY:
        case NK_SET_TY:
        case NK_TUPLE_TY:
            emit_x86_64_imm(output, 0);
            return 0;
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0] || !expr->kids[0]->ident) {
                return -1;
            }
            const char *op = expr->kids[0]->ident;
            const ChengNode *rhs = expr->kids[1];
            if (strcmp(op, "&") == 0) {
                return emit_addr_x86_64(func_ctx, rhs, NULL);
            }
            if (emit_expr_x86_64(func_ctx, rhs) != 0) {
                return -1;
            }
            if (strcmp(op, "+") == 0) {
                return 0;
            }
            if (strcmp(op, "-") == 0) {
                fprintf(output, "  negq %%rax\n");
                return 0;
            }
            if (strcmp(op, "~") == 0) {
                fprintf(output, "  notq %%rax\n");
                return 0;
            }
            if (strcmp(op, "!") == 0) {
                fprintf(output, "  cmpq $0, %%rax\n");
                fprintf(output, "  sete %%al\n");
                fprintf(output, "  movzbl %%al, %%eax\n");
                return 0;
            }
            if (strcmp(op, "*") == 0) {
                fprintf(output, "  movq (%%rax), %%rax\n");
                return 0;
            }
            add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                     "unsupported prefix operator in asm backend");
            return -1;
        }
        case NK_DOT_EXPR: {
            if (expr->len < 2 || !expr->kids[1] || expr->kids[1]->kind != NK_IDENT ||
                !expr->kids[1]->ident) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "unsupported dot expression in asm backend");
                return -1;
            }
            const char *field = expr->kids[1]->ident;
            const ChengNode *base = expr->kids[0];
            if (field && strcmp(field, "len") == 0 && base && expr_is_string_like(func_ctx, base)) {
                if (emit_expr_x86_64(func_ctx, base) != 0) {
                    return -1;
                }
                fprintf(output, "  movq %%rax, %%rdi\n");
                const char *call_sym = "c_strlen";
                const char *runtime_sym = NULL;
                if (asm_runtime_lookup_symbol(call_sym, &runtime_sym)) {
                    call_sym = runtime_sym;
                }
                char sym_buf[256];
                call_sym = asm_mangle_symbol(func_ctx->context, call_sym, sym_buf, sizeof(sym_buf));
                fprintf(output, "  call %s\n", call_sym);
                return 0;
            }
            AsmAccessKind access = ASM_ACCESS_QWORD;
            if (emit_dot_addr_x86_64(func_ctx, expr, &access) != 0) {
                return -1;
            }
            switch (access) {
                case ASM_ACCESS_BYTE:
                    fprintf(output, "  movzbl (%%rax), %%eax\n");
                    break;
                case ASM_ACCESS_WORD:
                    fprintf(output, "  movzwl (%%rax), %%eax\n");
                    break;
                case ASM_ACCESS_DWORD:
                    fprintf(output, "  movl (%%rax), %%eax\n");
                    break;
                case ASM_ACCESS_QWORD:
                default:
                    fprintf(output, "  movq (%%rax), %%rax\n");
                    break;
            }
            return 0;
        }
        case NK_ASGN: {
            if (expr->len < 2) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid assignment expression");
                return -1;
            }
            const ChengNode *lhs = expr->kids[0];
            const ChengNode *rhs = expr->kids[1];
            if (expr_contains_call(func_ctx, lhs)) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "call in assignment target is not supported in asm backend");
                return -1;
            }
            if (orc_mm_enabled()) {
                const ChengNode *lhs_base = strip_paren_expr(lhs);
                if (lhs_base && (lhs_base->kind == NK_IDENT || lhs_base->kind == NK_SYMBOL)) {
                    const ChengNode *type_expr = var_type_expr_from_ident(func_ctx, lhs_base);
                    if (orc_type_is_managed(type_expr)) {
                        if (emit_expr_x86_64(func_ctx, rhs) != 0) {
                            return -1;
                        }
                        if (orc_expr_is_borrowed(rhs)) {
                            emit_orc_call_x86_64(func_ctx, "memRetain", "%rax", 1);
                        }
                        fprintf(output, "  movq %%rax, %%r10\n");
                        if (emit_addr_x86_64(func_ctx, lhs_base, NULL) != 0) {
                            return -1;
                        }
                        fprintf(output, "  movq (%%rax), %%r11\n");
                        fprintf(output, "  movq %%r10, (%%rax)\n");
                        fprintf(output, "  movq %%r10, %%rax\n");
                        emit_orc_call_x86_64(func_ctx, "memRelease", "%r11", 1);
                        return 0;
                    }
                }
            }
            if (emit_expr_x86_64(func_ctx, rhs) != 0) {
                return -1;
            }
            fprintf(output, "  pushq %%rax\n");
            AsmAccessKind access = ASM_ACCESS_QWORD;
            if (emit_addr_x86_64(func_ctx, lhs, &access) != 0) {
                fprintf(output, "  addq $8, %%rsp\n");
                return -1;
            }
            fprintf(output, "  popq %%r11\n");
            switch (access) {
                case ASM_ACCESS_BYTE:
                    fprintf(output, "  movb %%r11b, (%%rax)\n");
                    fprintf(output, "  movzbl %%r11b, %%eax\n");
                    break;
                case ASM_ACCESS_WORD:
                    fprintf(output, "  movw %%r11w, (%%rax)\n");
                    fprintf(output, "  movzwl %%r11w, %%eax\n");
                    break;
                case ASM_ACCESS_DWORD:
                    fprintf(output, "  movl %%r11d, (%%rax)\n");
                    fprintf(output, "  movl %%r11d, %%eax\n");
                    break;
                case ASM_ACCESS_QWORD:
                default:
                    fprintf(output, "  movq %%r11, (%%rax)\n");
                    fprintf(output, "  movq %%r11, %%rax\n");
                    break;
            }
            return 0;
        }
        case NK_INFIX:
            return emit_expr_infix_x86_64(func_ctx, expr);
        case NK_CALL: {
            if (expr->len < 1 || !expr->kids[0]) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid call expression");
                return -1;
            }
            const ChengNode *callee = expr->kids[0];
            const ChengNode *call_target = call_target_node(callee);
            size_t arg_count = expr->len - 1;
            int has_named = 0;
            if (is_type_call_target(func_ctx->context, callee)) {
                if (arg_count == 0) {
                    emit_x86_64_imm(output, 0);
                    return 0;
                }
                if (arg_count == 1) {
                    const ChengNode *arg = call_arg_value(expr->kids[1], &has_named);
                    if (arg && !has_named) {
                        return emit_expr_x86_64(func_ctx, arg);
                    }
                }
            }
            if (!call_target || !(call_target->kind == NK_IDENT || call_target->kind == NK_SYMBOL) ||
                !call_target->ident) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "unsupported call target in asm backend");
                return -1;
            }
            if (strcmp(call_target->ident, "default") == 0) {
                if (arg_count != 1) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "default requires one type argument");
                    return -1;
                }
                {
                    int named = 0;
                    const ChengNode *arg = call_arg_value(expr->kids[1], &named);
                    if (!arg || named) {
                        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                                 "default does not support named arguments");
                        return -1;
                    }
                }
                emit_x86_64_imm(output, 0);
                return 0;
            }
            if (strcmp(call_target->ident, "sizeof") == 0 || strcmp(call_target->ident, "alignof") == 0) {
                if (arg_count != 1) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "sizeof/alignof requires one type argument");
                    return -1;
                }
                int named = 0;
                const ChengNode *arg = call_arg_value(expr->kids[1], &named);
                if (!arg || named) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "sizeof/alignof does not support named arguments");
                    return -1;
                }
                if (!func_ctx->context->layout) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "asm backend missing layout for sizeof/alignof");
                    return -1;
                }
                size_t size = 0;
                size_t align = 0;
                if (!asm_layout_type_info(func_ctx->context->layout, arg, &size, &align)) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "asm backend failed to resolve sizeof/alignof type");
                    return -1;
                }
                int64_t value = strcmp(call_target->ident, "sizeof") == 0 ? (int64_t)size : (int64_t)align;
                emit_x86_64_imm(output, value);
                return 0;
            }
            if (arg_count > 6) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "asm backend only supports up to 6 call arguments");
                return -1;
            }
            for (size_t arg_index = 0; arg_index < arg_count; arg_index++) {
                const ChengNode *arg = call_arg_value(expr->kids[arg_index + 1], &has_named);
                if (!arg) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "invalid call argument");
                    return -1;
                }
                if (emit_expr_x86_64(func_ctx, arg) != 0) {
                    return -1;
                }
                fprintf(output, "  pushq %%rax\n");
            }
            for (size_t arg_index = arg_count; arg_index-- > 0;) {
                const char *arg_reg = x86_64_arg_reg(arg_index);
                if (!arg_reg) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "unsupported call argument register");
                    return -1;
                }
                fprintf(output, "  popq %%rax\n");
                fprintf(output, "  movq %%rax, %s\n", arg_reg);
            }
            {
                const char *call_sym = call_target->ident;
                const char *runtime_sym = NULL;
                if (asm_runtime_lookup_symbol(call_target->ident, &runtime_sym)) {
                    call_sym = runtime_sym;
                }
                char sym_buf[256];
                call_sym = asm_mangle_symbol(func_ctx->context, call_sym, sym_buf, sizeof(sym_buf));
                fprintf(output, "  call %s\n", call_sym);
            }
            return 0;
        }
        default:
            {
                char msg[96];
                if (expr->kind == NK_DOT_EXPR && expr->len >= 2 &&
                    expr->kids[0] && expr->kids[1]) {
                    const char *lhs = expr->kids[0]->ident ? expr->kids[0]->ident : "?";
                    const char *rhs = expr->kids[1]->ident ? expr->kids[1]->ident : "?";
                    snprintf(msg, sizeof(msg), "unsupported dot expression %s.%s in asm backend",
                             lhs, rhs);
                } else {
                    snprintf(msg, sizeof(msg), "unsupported expression kind %d in x86_64 asm backend",
                             (int)expr->kind);
                }
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr, msg);
            }
            return -1;
    }
}

static int emit_stmt_x86_64(AsmFuncContext *func_ctx, const ChengNode *stmt);

static int emit_stmt_list_x86_64(AsmFuncContext *func_ctx, const ChengNode *list) {
    if (!list) {
        return 0;
    }
    if (list->kind != NK_STMT_LIST) {
        return emit_stmt_x86_64(func_ctx, list);
    }
    for (size_t index = 0; index < list->len; index++) {
        if (emit_stmt_x86_64(func_ctx, list->kids[index]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int emit_if_x86_64(AsmFuncContext *func_ctx, const ChengNode *if_node, int end_label) {
    if (!func_ctx || !if_node || if_node->len < 2) {
        return -1;
    }
    int else_label = asm_next_label(func_ctx->context);
    if (emit_expr_x86_64(func_ctx, if_node->kids[0]) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  cmpq $0, %%rax\n");
    fprintf(func_ctx->context->output, "  je .L%d\n", else_label);
    if (emit_stmt_list_x86_64(func_ctx, if_node->kids[1]) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  jmp .L%d\n", end_label);
    asm_emit_label(func_ctx->context->output, else_label);
    if (if_node->len > 2 && if_node->kids[2] && if_node->kids[2]->kind == NK_ELSE &&
        if_node->kids[2]->len > 0) {
        const ChengNode *else_body = if_node->kids[2]->kids[0];
        if (else_body && else_body->kind == NK_IF) {
            if (emit_if_x86_64(func_ctx, else_body, end_label) != 0) {
                return -1;
            }
        } else if (emit_stmt_list_x86_64(func_ctx, else_body) != 0) {
            return -1;
        }
    }
    return 0;
}

static int emit_while_x86_64(AsmFuncContext *func_ctx, const ChengNode *while_node) {
    if (!func_ctx || !while_node || while_node->len < 2) {
        return -1;
    }
    int start_label = asm_next_label(func_ctx->context);
    int end_label = asm_next_label(func_ctx->context);
    if (func_ctx->loop_depth >= 64) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, while_node,
                 "loop nesting too deep in asm backend");
        return -1;
    }
    func_ctx->loop_start_labels[func_ctx->loop_depth] = start_label;
    func_ctx->loop_end_labels[func_ctx->loop_depth] = end_label;
    func_ctx->loop_depth++;
    asm_emit_label(func_ctx->context->output, start_label);
    if (emit_expr_x86_64(func_ctx, while_node->kids[0]) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  cmpq $0, %%rax\n");
    fprintf(func_ctx->context->output, "  je .L%d\n", end_label);
    if (emit_stmt_list_x86_64(func_ctx, while_node->kids[1]) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  jmp .L%d\n", start_label);
    asm_emit_label(func_ctx->context->output, end_label);
    func_ctx->loop_depth--;
    return 0;
}

static int emit_for_x86_64(AsmFuncContext *func_ctx, const ChengNode *for_node) {
    if (!func_ctx || !for_node || for_node->len < 3) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "invalid for statement");
        return -1;
    }
    const ChengNode *pattern = for_node->kids[0];
    const ChengNode *iter = for_node->kids[1];
    const ChengNode *body = for_node->kids[2];
    const ChengNode *ident = pattern_ident_node(pattern);
    if (!ident || !ident->ident) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "for requires identifier pattern in asm backend");
        return -1;
    }
    const ChengNode *start = NULL;
    const ChengNode *end = NULL;
    int inclusive = 0;
    if (iter && iter->kind == NK_INFIX && iter->len >= 3 && iter->kids[0] && iter->kids[0]->ident) {
        const char *op = iter->kids[0]->ident;
        if (strcmp(op, "..") == 0 || strcmp(op, "..<") == 0) {
            start = iter->kids[1];
            end = iter->kids[2];
            inclusive = strcmp(op, "..") == 0;
        }
    }
    if (!start || !end) {
        int is_seq = expr_is_seq_like(func_ctx, iter);
        int is_str = expr_is_string_like(func_ctx, iter);
        if (!is_seq && !is_str) {
            add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                     "for only supports numeric ranges or seq/str iterables");
            return -1;
        }
        int offset = 0;
        if (!asm_var_table_find(&func_ctx->locals, ident->ident, &offset)) {
            add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                     "unknown loop variable in asm backend");
            return -1;
        }
        char idx_name[64];
        format_for_index_name(for_node, idx_name, sizeof(idx_name));
        int idx_offset = 0;
        if (!asm_var_table_find(&func_ctx->locals, idx_name, &idx_offset)) {
            add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                     "missing loop index slot in asm backend");
            return -1;
        }
        emit_x86_64_imm(func_ctx->context->output, 0);
        FILE *output = func_ctx->context->output;
        fprintf(output, "  movq %%rax, %d(%%rbp)\n", idx_offset);
        int cond_label = asm_next_label(func_ctx->context);
        int step_label = asm_next_label(func_ctx->context);
        int end_label = asm_next_label(func_ctx->context);
        if (func_ctx->loop_depth >= 64) {
            add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                     "loop nesting too deep in asm backend");
            return -1;
        }
        func_ctx->loop_start_labels[func_ctx->loop_depth] = step_label;
        func_ctx->loop_end_labels[func_ctx->loop_depth] = end_label;
        func_ctx->loop_depth++;
        size_t elem_size = 8;
        if (is_seq) {
            const ChengNode *iter_probe = strip_paren_expr(iter);
            const ChengNode *iter_type = NULL;
            if (iter_probe && iter_probe->kind == NK_IDENT) {
                iter_type = var_type_expr_from_ident(func_ctx, iter_probe);
            }
            const ChengNode *elem_type = type_expr_bracket_arg(iter_type, 1);
            if (elem_type) {
                elem_size = asm_elem_size_for_type(func_ctx->context, elem_type);
                if (elem_size == 0) {
                    elem_size = 8;
                }
            }
        }
        asm_emit_label(output, cond_label);
        if (is_seq) {
            if (emit_addr_x86_64(func_ctx, iter, NULL) != 0) {
                return -1;
            }
            fprintf(output, "  movl (%%rax), %%ecx\n");
        } else {
            if (emit_expr_x86_64(func_ctx, iter) != 0) {
                return -1;
            }
            fprintf(output, "  movq %%rax, %%rdi\n");
            const char *call_sym = "c_strlen";
            const char *runtime_sym = NULL;
            if (asm_runtime_lookup_symbol(call_sym, &runtime_sym)) {
                call_sym = runtime_sym;
            }
            char sym_buf[256];
            call_sym = asm_mangle_symbol(func_ctx->context, call_sym, sym_buf, sizeof(sym_buf));
            fprintf(output, "  call %s\n", call_sym);
            fprintf(output, "  movq %%rax, %%rcx\n");
        }
        fprintf(output, "  movq %d(%%rbp), %%rax\n", idx_offset);
        fprintf(output, "  cmpq %%rcx, %%rax\n");
        fprintf(output, "  jge .L%d\n", end_label);
        if (is_seq) {
            if (emit_addr_x86_64(func_ctx, iter, NULL) != 0) {
                return -1;
            }
            fprintf(output, "  movq 8(%%rax), %%rax\n");
            fprintf(output, "  movq %d(%%rbp), %%rcx\n", idx_offset);
            if (elem_size != 1) {
                fprintf(output, "  movq $%zu, %%r11\n", elem_size);
                fprintf(output, "  imulq %%r11, %%rcx\n");
            }
            fprintf(output, "  addq %%rcx, %%rax\n");
            switch (asm_access_kind_from_size(elem_size)) {
                case ASM_ACCESS_BYTE:
                    fprintf(output, "  movzbl (%%rax), %%eax\n");
                    break;
                case ASM_ACCESS_WORD:
                    fprintf(output, "  movzwl (%%rax), %%eax\n");
                    break;
                case ASM_ACCESS_DWORD:
                    fprintf(output, "  movl (%%rax), %%eax\n");
                    break;
                case ASM_ACCESS_QWORD:
                default:
                    fprintf(output, "  movq (%%rax), %%rax\n");
                    break;
            }
        } else {
            if (emit_expr_x86_64(func_ctx, iter) != 0) {
                return -1;
            }
            fprintf(output, "  movq %d(%%rbp), %%rcx\n", idx_offset);
            fprintf(output, "  addq %%rcx, %%rax\n");
            fprintf(output, "  movzbl (%%rax), %%eax\n");
        }
        fprintf(output, "  movq %%rax, %d(%%rbp)\n", offset);
        if (emit_stmt_list_x86_64(func_ctx, body) != 0) {
            return -1;
        }
        asm_emit_label(output, step_label);
        fprintf(output, "  movq %d(%%rbp), %%rax\n", idx_offset);
        fprintf(output, "  addq $1, %%rax\n");
        fprintf(output, "  movq %%rax, %d(%%rbp)\n", idx_offset);
        fprintf(output, "  jmp .L%d\n", cond_label);
        asm_emit_label(output, end_label);
        func_ctx->loop_depth--;
        return 0;
    }
    int offset = 0;
    if (!asm_var_table_find(&func_ctx->locals, ident->ident, &offset)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "unknown loop variable in asm backend");
        return -1;
    }
    if (emit_expr_x86_64(func_ctx, start) != 0) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    fprintf(output, "  movq %%rax, %d(%%rbp)\n", offset);
    int cond_label = asm_next_label(func_ctx->context);
    int step_label = asm_next_label(func_ctx->context);
    int end_label = asm_next_label(func_ctx->context);
    if (func_ctx->loop_depth >= 64) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "loop nesting too deep in asm backend");
        return -1;
    }
    func_ctx->loop_start_labels[func_ctx->loop_depth] = step_label;
    func_ctx->loop_end_labels[func_ctx->loop_depth] = end_label;
    func_ctx->loop_depth++;
    asm_emit_label(output, cond_label);
    if (emit_expr_x86_64(func_ctx, end) != 0) {
        return -1;
    }
    fprintf(output, "  movq %%rax, %%rcx\n");
    fprintf(output, "  movq %d(%%rbp), %%rax\n", offset);
    fprintf(output, "  cmpq %%rcx, %%rax\n");
    if (inclusive) {
        fprintf(output, "  jg .L%d\n", end_label);
    } else {
        fprintf(output, "  jge .L%d\n", end_label);
    }
    if (emit_stmt_list_x86_64(func_ctx, body) != 0) {
        return -1;
    }
    asm_emit_label(output, step_label);
    fprintf(output, "  movq %d(%%rbp), %%rax\n", offset);
    fprintf(output, "  addq $1, %%rax\n");
    fprintf(output, "  movq %%rax, %d(%%rbp)\n", offset);
    fprintf(output, "  jmp .L%d\n", cond_label);
    asm_emit_label(output, end_label);
    func_ctx->loop_depth--;
    return 0;
}

static int emit_case_x86_64(AsmFuncContext *func_ctx, const ChengNode *stmt) {
    if (!func_ctx || !stmt || stmt->len < 1) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                 "invalid case statement in asm backend");
        return -1;
    }
    const ChengNode *selector = stmt->kids[0];
    char tmp_name[64];
    format_case_tmp_name(stmt, tmp_name, sizeof(tmp_name));
    int tmp_offset = 0;
    if (!asm_var_table_find(&func_ctx->locals, tmp_name, &tmp_offset)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                 "missing case temp in asm backend");
        return -1;
    }
    if (emit_expr_x86_64(func_ctx, selector) != 0) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    fprintf(output, "  movq %%rax, %d(%%rbp)\n", tmp_offset);
    int end_label = asm_next_label(func_ctx->context);
    for (size_t i = 1; i < stmt->len; i++) {
        const ChengNode *branch = stmt->kids[i];
        if (!branch) {
            continue;
        }
        if (branch->kind == NK_OF_BRANCH) {
            const ChengNode *pattern = branch->len > 0 ? branch->kids[0] : NULL;
            const ChengNode *body = branch->len > 1 ? branch->kids[1] : NULL;
            int match_label = asm_next_label(func_ctx->context);
            const char *bind_name = NULL;
            int unconditional = 0;
            int saw_pattern = 0;
            if (pattern && pattern->kind == NK_PATTERN) {
                for (size_t p = 0; p < pattern->len; p++) {
                    const ChengNode *pat = pattern->kids[p];
                    if (!pat) {
                        continue;
                    }
                    const char *name = case_pattern_ident(pat);
                    if (name) {
                        saw_pattern = 1;
                        if (!case_pattern_is_wildcard(name)) {
                            bind_name = name;
                        }
                        unconditional = 1;
                        break;
                    }
                    if (!hard_rt_case_pattern_const(pat, NULL)) {
                        add_diag(func_ctx->context->diags, func_ctx->context->filename, pat,
                                 hard_rt_case_pattern_error(func_ctx));
                        return -1;
                    }
                    saw_pattern = 1;
                    if (emit_expr_x86_64(func_ctx, pat) != 0) {
                        return -1;
                    }
                    fprintf(output, "  cmpq %d(%%rbp), %%rax\n", tmp_offset);
                    fprintf(output, "  je .L%d\n", match_label);
                }
            } else if (pattern) {
                const char *name = case_pattern_ident(pattern);
                if (name) {
                    saw_pattern = 1;
                    if (!case_pattern_is_wildcard(name)) {
                        bind_name = name;
                    }
                    unconditional = 1;
                } else if (hard_rt_case_pattern_const(pattern, NULL)) {
                    saw_pattern = 1;
                    if (emit_expr_x86_64(func_ctx, pattern) != 0) {
                        return -1;
                    }
                    fprintf(output, "  cmpq %d(%%rbp), %%rax\n", tmp_offset);
                    fprintf(output, "  je .L%d\n", match_label);
                }
            }
            if (!saw_pattern) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, branch,
                         "invalid case pattern in asm backend");
                return -1;
            }
            int next_label = -1;
            if (unconditional) {
                fprintf(output, "  jmp .L%d\n", match_label);
            } else {
                next_label = asm_next_label(func_ctx->context);
                fprintf(output, "  jmp .L%d\n", next_label);
            }
            asm_emit_label(output, match_label);
            if (bind_name) {
                int bind_offset = 0;
                if (!asm_var_table_find(&func_ctx->locals, bind_name, &bind_offset)) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, branch,
                             "unknown case binding in asm backend");
                    return -1;
                }
                fprintf(output, "  movq %d(%%rbp), %%rax\n", tmp_offset);
                fprintf(output, "  movq %%rax, %d(%%rbp)\n", bind_offset);
            }
            if (emit_stmt_list_x86_64(func_ctx, body) != 0) {
                return -1;
            }
            fprintf(output, "  jmp .L%d\n", end_label);
            if (!unconditional && next_label >= 0) {
                asm_emit_label(output, next_label);
            }
            continue;
        }
        if (branch->kind == NK_ELSE) {
            const ChengNode *body = branch->len > 0 ? branch->kids[0] : NULL;
            if (emit_stmt_list_x86_64(func_ctx, body) != 0) {
                return -1;
            }
            fprintf(output, "  jmp .L%d\n", end_label);
        }
    }
    asm_emit_label(output, end_label);
    return 0;
}

static int emit_stmt_x86_64(AsmFuncContext *func_ctx, const ChengNode *stmt) {
    if (!func_ctx || !stmt) {
        return 0;
    }
    FILE *output = func_ctx->context->output;
    switch (stmt->kind) {
        case NK_RETURN: {
            if (stmt->len > 0 && stmt->kids[0] && stmt->kids[0]->kind != NK_EMPTY) {
                if (emit_expr_x86_64(func_ctx, stmt->kids[0]) != 0) {
                    return -1;
                }
                if (orc_mm_enabled() && func_ctx->ret_type_expr &&
                    orc_type_is_managed(func_ctx->ret_type_expr) &&
                    orc_expr_is_borrowed(stmt->kids[0])) {
                    emit_orc_call_x86_64(func_ctx, "memRetain", "%rax", 1);
                }
            } else {
                emit_x86_64_imm(output, 0);
            }
            fprintf(output, "  jmp .L%d\n", func_ctx->end_label);
            return 0;
        }
        case NK_IF:
            return emit_if_x86_64(func_ctx, stmt, func_ctx->end_label);
        case NK_WHILE:
            return emit_while_x86_64(func_ctx, stmt);
        case NK_FOR:
            return emit_for_x86_64(func_ctx, stmt);
        case NK_CASE:
            return emit_case_x86_64(func_ctx, stmt);
        case NK_VAR:
        case NK_LET:
        case NK_CONST: {
            if (stmt->len < 1) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "invalid local declaration");
                return -1;
            }
            const ChengNode *pattern = stmt->kids[0];
            const ChengNode *ident = pattern_ident_node(pattern);
            if (!ident || !ident->ident) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "unsupported local pattern in asm backend");
                return -1;
            }
            int offset = 0;
            if (!asm_var_table_find(&func_ctx->locals, ident->ident, &offset)) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "unknown local in asm backend");
                return -1;
            }
            const ChengNode *value = NULL;
            if (stmt->len > 2) {
                value = stmt->kids[2];
            }
            if (!value || value->kind == NK_EMPTY) {
                emit_x86_64_imm(output, 0);
            } else if (emit_expr_x86_64(func_ctx, value) != 0) {
                return -1;
            }
            if (orc_mm_enabled() && value && value->kind != NK_EMPTY) {
                const ChengNode *type_expr = var_type_expr_from_ident(func_ctx, ident);
                if (orc_type_is_managed(type_expr) && orc_expr_is_borrowed(value)) {
                    emit_orc_call_x86_64(func_ctx, "memRetain", "%rax", 1);
                }
            }
            fprintf(output, "  movq %%rax, %d(%%rbp)\n", offset);
            return 0;
        }
        case NK_ASGN: {
            if (stmt->len < 2) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "invalid assignment");
                return -1;
            }
            return emit_store_x86_64(func_ctx, stmt->kids[0], stmt->kids[1]);
        }
        case NK_BREAK: {
            if (func_ctx->loop_depth == 0) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "break outside loop in asm backend");
                return -1;
            }
            size_t depth_index = func_ctx->loop_depth - 1;
            fprintf(output, "  jmp .L%d\n", func_ctx->loop_end_labels[depth_index]);
            return 0;
        }
        case NK_CONTINUE: {
            if (func_ctx->loop_depth == 0) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "continue outside loop in asm backend");
                return -1;
            }
            size_t depth_index = func_ctx->loop_depth - 1;
            fprintf(output, "  jmp .L%d\n", func_ctx->loop_start_labels[depth_index]);
            return 0;
        }
        case NK_DEFER: {
            const ChengNode *body = stmt->len > 0 ? stmt->kids[0] : NULL;
            if (!body) {
                return 0;
            }
            return emit_stmt_list_x86_64(func_ctx, body);
        }
        case NK_CALL:
            return emit_expr_x86_64(func_ctx, stmt);
        case NK_STMT_LIST:
            return emit_stmt_list_x86_64(func_ctx, stmt);
        case NK_EMPTY:
            return 0;
        case NK_PRAGMA:
            return 0;
        default:
            if (is_simple_expr_stmt_kind(stmt->kind)) {
                return emit_expr_x86_64(func_ctx, stmt);
            }
            add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                     "unsupported statement in asm backend");
            return -1;
    }
}

static int emit_function_x86_64_elf(AsmEmitContext *context, const ChengNode *fn_node) {
    if (!context || !fn_node || fn_node->kind != NK_FN_DECL || fn_node->len < 4) {
        return -1;
    }
    const ChengNode *name_node = fn_node->kids[0];
    if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
        add_diag(context->diags, context->filename, fn_node, "invalid function name");
        return -1;
    }
    const char *name = name_node->ident;
    const ChengNode *params = fn_node->kids[1];
    const ChengNode *ret_type = fn_node->kids[2];
    const ChengNode *body = fn_node->kids[3];
    AsmFuncContext func_ctx = {0};
    func_ctx.context = context;
    func_ctx.ret_kind = ret_kind_from_type(ret_type);
    func_ctx.ret_type_expr = ret_type;
    func_ctx.ret_is_result = type_expr_is_result(ret_type);
    asm_var_table_init(&func_ctx.locals);
    if (params && params->kind == NK_FORMAL_PARAMS) {
        if (params->len > 0) {
            for (size_t param_index = 0; param_index < params->len; param_index++) {
                const ChengNode *param = params->kids[param_index];
                if (!param || param->len < 1 || !param->kids[0] ||
                    param->kids[0]->kind != NK_IDENT || !param->kids[0]->ident) {
                    add_diag(context->diags, context->filename, param,
                             "unsupported parameter in asm backend");
                    asm_var_table_free(&func_ctx.locals);
                    return -1;
                }
                const ChengNode *param_type = param->len > 1 ? param->kids[1] : NULL;
                if (asm_var_table_add(&func_ctx.locals, param->kids[0]->ident, param_type) != 0) {
                    add_diag(context->diags, context->filename, param,
                             "failed to allocate parameter");
                    asm_var_table_free(&func_ctx.locals);
                    return -1;
                }
            }
        }
    }
    if (collect_locals_from_list(body, &func_ctx.locals, context->filename, context->diags) != 0) {
        asm_var_table_free(&func_ctx.locals);
        return -1;
    }
    func_ctx.stack_size = assign_stack_offsets(&func_ctx.locals);
    char sym_buf[256];
    const char *sym = asm_mangle_symbol(context, name, sym_buf, sizeof(sym_buf));
    fprintf(context->output, ".globl %s\n", sym);
    fprintf(context->output, ".type %s, @function\n", sym);
    fprintf(context->output, "%s:\n", sym);
    fprintf(context->output, "  pushq %%rbp\n");
    fprintf(context->output, "  movq %%rsp, %%rbp\n");
    if (func_ctx.stack_size > 0) {
        fprintf(context->output, "  subq $%d, %%rsp\n", func_ctx.stack_size);
    }
    for (size_t param_index = 0; param_index < func_ctx.locals.len; param_index++) {
        if (!params || params->kind != NK_FORMAL_PARAMS || param_index >= params->len) {
            break;
        }
        int offset = func_ctx.locals.items[param_index].offset;
        if (param_index < 6) {
            const char *arg_reg = x86_64_arg_reg(param_index);
            if (!arg_reg) {
                add_diag(context->diags, context->filename, params->kids[param_index],
                         "unsupported argument register");
                asm_var_table_free(&func_ctx.locals);
                return -1;
            }
            fprintf(context->output, "  movq %s, %d(%%rbp)\n", arg_reg, offset);
        } else {
            int stack_arg_offset = 16 + (int)((param_index - 6) * 8);
            fprintf(context->output, "  movq %d(%%rbp), %%rax\n", stack_arg_offset);
            fprintf(context->output, "  movq %%rax, %d(%%rbp)\n", offset);
        }
    }
    func_ctx.end_label = asm_next_label(context);
    func_ctx.loop_depth = 0;
    if (emit_stmt_list_x86_64(&func_ctx, body) != 0) {
        asm_var_table_free(&func_ctx.locals);
        return -1;
    }
    emit_x86_64_imm(context->output, 0);
    fprintf(context->output, "  jmp .L%d\n", func_ctx.end_label);
    asm_emit_label(context->output, func_ctx.end_label);
    fprintf(context->output, "  leave\n");
    fprintf(context->output, "  ret\n");
    fprintf(context->output, ".size %s, .-%s\n", sym, sym);
    asm_var_table_free(&func_ctx.locals);
    return 0;
}

static int emit_x86_64_elf_file(FILE *output,
                                const ChengNode *root,
                                const char *filename,
                                ChengDiagList *diags,
                                AsmLayout *layout,
                                const AsmModuleFilter *module_filter) {
    if (!output || !root) {
        return -1;
    }
    const ChengNode *list = get_top_level_list(root);
    if (!list || list->kind != NK_STMT_LIST) {
        add_diag(diags, filename, root, "asm backend expects a top-level statement list");
        return -1;
    }
    int rc = -1;
    AsmFuncTable table;
    asm_func_table_init(&table);
    ChengStrList importc;
    cheng_strlist_init(&importc);
    AsmEmitContext context = {0};
    context.output = output;
    context.filename = filename;
    context.diags = diags;
    context.label_counter = 1;
    context.root = root;
    context.hard_rt = asm_env_is_hard_rt();
    context.layout = layout;
    context.module_filter = module_filter;
    asm_global_table_init(&context.globals);
    context.module_prefix = asm_module_prefix_from_env(module_filter);
    if (collect_global_types(list, &context.globals, filename, diags) != 0) {
        goto cleanup;
    }
    collect_importc_names(list, &importc);
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_FN_DECL) {
            if (asm_func_table_add(&table, stmt) != 0) {
                add_diag(diags, filename, stmt, "failed to collect function declarations");
                goto cleanup;
            }
        }
    }
    if (module_filter) {
        for (size_t index = 0; index < table.len; index++) {
            AsmFuncEntry *entry = &table.items[index];
            entry->emit = asm_module_filter_has_func(module_filter, entry->name);
        }
    } else {
        AsmFuncEntry *main_entry = asm_func_table_find(&table, "main");
        if (!main_entry) {
            add_diag(diags, filename, root, "asm backend requires a main function");
            goto cleanup;
        }
        main_entry->emit = 1;
        int changed = 1;
        while (changed) {
            changed = 0;
            for (size_t index = 0; index < table.len; index++) {
                AsmFuncEntry *entry = &table.items[index];
                if (entry->emit && !entry->scanned) {
                    entry->scanned = 1;
                    if (asm_func_is_runtime_name(entry->name) ||
                        asm_func_is_importc_name(&importc, entry->name) ||
                        !asm_func_decl_has_body(entry->decl)) {
                        continue;
                    }
                    if (entry->decl && entry->decl->len > 3) {
                        const ChengNode *body = entry->decl->kids[3];
                        asm_func_table_mark_calls(body, &table, &importc, &changed);
                    }
                }
            }
        }
    }
    fprintf(output, ".text\n");
    for (size_t index = 0; index < table.len; index++) {
        AsmFuncEntry *entry = &table.items[index];
        if (!asm_func_entry_should_emit(entry, &importc)) {
            continue;
        }
        if (emit_function_x86_64_elf(&context, entry->decl) != 0) {
            goto cleanup;
        }
    }
    rc = 0;
cleanup:
    if (context.module_prefix) {
        free(context.module_prefix);
    }
    cheng_strlist_free(&importc);
    asm_global_table_free(&context.globals);
    asm_func_table_free(&table);
    return rc;
}

static int emit_function_x86_64_coff(AsmEmitContext *context, const ChengNode *fn_node) {
    if (!context || !fn_node || fn_node->kind != NK_FN_DECL || fn_node->len < 4) {
        return -1;
    }
    const ChengNode *name_node = fn_node->kids[0];
    if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
        add_diag(context->diags, context->filename, fn_node, "invalid function name");
        return -1;
    }
    const char *name = name_node->ident;
    const ChengNode *params = fn_node->kids[1];
    const ChengNode *ret_type = fn_node->kids[2];
    const ChengNode *body = fn_node->kids[3];
    AsmFuncContext func_ctx = {0};
    func_ctx.context = context;
    func_ctx.ret_kind = ret_kind_from_type(ret_type);
    func_ctx.ret_type_expr = ret_type;
    func_ctx.ret_is_result = type_expr_is_result(ret_type);
    asm_var_table_init(&func_ctx.locals);
    if (params && params->kind == NK_FORMAL_PARAMS) {
        if (params->len > 0) {
            for (size_t param_index = 0; param_index < params->len; param_index++) {
                const ChengNode *param = params->kids[param_index];
                if (!param || param->len < 1 || !param->kids[0] ||
                    param->kids[0]->kind != NK_IDENT || !param->kids[0]->ident) {
                    add_diag(context->diags, context->filename, param,
                             "unsupported parameter in asm backend");
                    asm_var_table_free(&func_ctx.locals);
                    return -1;
                }
                const ChengNode *param_type = param->len > 1 ? param->kids[1] : NULL;
                if (asm_var_table_add(&func_ctx.locals, param->kids[0]->ident, param_type) != 0) {
                    add_diag(context->diags, context->filename, param,
                             "failed to allocate parameter");
                    asm_var_table_free(&func_ctx.locals);
                    return -1;
                }
            }
        }
    }
    if (collect_locals_from_list(body, &func_ctx.locals, context->filename, context->diags) != 0) {
        asm_var_table_free(&func_ctx.locals);
        return -1;
    }
    func_ctx.stack_size = assign_stack_offsets(&func_ctx.locals);
    char sym_buf[256];
    const char *sym = asm_mangle_symbol(context, name, sym_buf, sizeof(sym_buf));
    fprintf(context->output, ".globl %s\n", sym);
    fprintf(context->output, "%s:\n", sym);
    fprintf(context->output, "  pushq %%rbp\n");
    fprintf(context->output, "  movq %%rsp, %%rbp\n");
    if (func_ctx.stack_size > 0) {
        fprintf(context->output, "  subq $%d, %%rsp\n", func_ctx.stack_size);
    }
    for (size_t param_index = 0; param_index < func_ctx.locals.len; param_index++) {
        if (!params || params->kind != NK_FORMAL_PARAMS || param_index >= params->len) {
            break;
        }
        int offset = func_ctx.locals.items[param_index].offset;
        if (param_index < 6) {
            const char *arg_reg = x86_64_arg_reg(param_index);
            if (!arg_reg) {
                add_diag(context->diags, context->filename, params->kids[param_index],
                         "unsupported argument register");
                asm_var_table_free(&func_ctx.locals);
                return -1;
            }
            fprintf(context->output, "  movq %s, %d(%%rbp)\n", arg_reg, offset);
        } else {
            int stack_arg_offset = 16 + (int)((param_index - 6) * 8);
            fprintf(context->output, "  movq %d(%%rbp), %%rax\n", stack_arg_offset);
            fprintf(context->output, "  movq %%rax, %d(%%rbp)\n", offset);
        }
    }
    func_ctx.end_label = asm_next_label(context);
    func_ctx.loop_depth = 0;
    if (emit_stmt_list_x86_64(&func_ctx, body) != 0) {
        asm_var_table_free(&func_ctx.locals);
        return -1;
    }
    emit_x86_64_imm(context->output, 0);
    fprintf(context->output, "  jmp .L%d\n", func_ctx.end_label);
    asm_emit_label(context->output, func_ctx.end_label);
    fprintf(context->output, "  leave\n");
    fprintf(context->output, "  ret\n");
    asm_var_table_free(&func_ctx.locals);
    return 0;
}

static int emit_x86_64_coff_file(FILE *output,
                                 const ChengNode *root,
                                 const char *filename,
                                 ChengDiagList *diags,
                                 AsmLayout *layout,
                                 const AsmModuleFilter *module_filter) {
    if (!output || !root) {
        return -1;
    }
    const ChengNode *list = get_top_level_list(root);
    if (!list || list->kind != NK_STMT_LIST) {
        add_diag(diags, filename, root, "asm backend expects a top-level statement list");
        return -1;
    }
    int rc = -1;
    AsmFuncTable table;
    asm_func_table_init(&table);
    ChengStrList importc;
    cheng_strlist_init(&importc);
    AsmEmitContext context = {0};
    context.output = output;
    context.filename = filename;
    context.diags = diags;
    context.label_counter = 1;
    context.root = root;
    context.hard_rt = asm_env_is_hard_rt();
    context.layout = layout;
    context.module_filter = module_filter;
    asm_global_table_init(&context.globals);
    context.module_prefix = asm_module_prefix_from_env(module_filter);
    if (collect_global_types(list, &context.globals, filename, diags) != 0) {
        goto cleanup;
    }
    collect_importc_names(list, &importc);
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_FN_DECL) {
            if (asm_func_table_add(&table, stmt) != 0) {
                add_diag(diags, filename, stmt, "failed to collect function declarations");
                goto cleanup;
            }
        }
    }
    if (module_filter) {
        for (size_t index = 0; index < table.len; index++) {
            AsmFuncEntry *entry = &table.items[index];
            entry->emit = asm_module_filter_has_func(module_filter, entry->name);
        }
    } else {
        AsmFuncEntry *main_entry = asm_func_table_find(&table, "main");
        if (!main_entry) {
            add_diag(diags, filename, root, "asm backend requires a main function");
            goto cleanup;
        }
        main_entry->emit = 1;
        int changed = 1;
        while (changed) {
            changed = 0;
            for (size_t index = 0; index < table.len; index++) {
                AsmFuncEntry *entry = &table.items[index];
                if (entry->emit && !entry->scanned) {
                    entry->scanned = 1;
                    if (asm_func_is_runtime_name(entry->name) ||
                        asm_func_is_importc_name(&importc, entry->name) ||
                        !asm_func_decl_has_body(entry->decl)) {
                        continue;
                    }
                    if (entry->decl && entry->decl->len > 3) {
                        const ChengNode *body = entry->decl->kids[3];
                        asm_func_table_mark_calls(body, &table, &importc, &changed);
                    }
                }
            }
        }
    }
    fprintf(output, ".text\n");
    for (size_t index = 0; index < table.len; index++) {
        AsmFuncEntry *entry = &table.items[index];
        if (!asm_func_entry_should_emit(entry, &importc)) {
            continue;
        }
        if (emit_function_x86_64_coff(&context, entry->decl) != 0) {
            goto cleanup;
        }
    }
    rc = 0;
cleanup:
    if (context.module_prefix) {
        free(context.module_prefix);
    }
    cheng_strlist_free(&importc);
    asm_global_table_free(&context.globals);
    asm_func_table_free(&table);
    return rc;
}

static int emit_function_x86_64_darwin(AsmEmitContext *context, const ChengNode *fn_node) {
    if (!context || !fn_node || fn_node->kind != NK_FN_DECL || fn_node->len < 4) {
        return -1;
    }
    const ChengNode *name_node = fn_node->kids[0];
    if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
        add_diag(context->diags, context->filename, fn_node, "invalid function name");
        return -1;
    }
    const char *name = name_node->ident;
    const ChengNode *params = fn_node->kids[1];
    const ChengNode *ret_type = fn_node->kids[2];
    const ChengNode *body = fn_node->kids[3];
    AsmFuncContext func_ctx = {0};
    func_ctx.context = context;
    func_ctx.ret_kind = ret_kind_from_type(ret_type);
    func_ctx.ret_type_expr = ret_type;
    func_ctx.ret_is_result = type_expr_is_result(ret_type);
    asm_var_table_init(&func_ctx.locals);
    if (params && params->kind == NK_FORMAL_PARAMS) {
        if (params->len > 0) {
            for (size_t param_index = 0; param_index < params->len; param_index++) {
                const ChengNode *param = params->kids[param_index];
                if (!param || param->len < 1 || !param->kids[0] ||
                    param->kids[0]->kind != NK_IDENT || !param->kids[0]->ident) {
                    add_diag(context->diags, context->filename, param,
                             "unsupported parameter in asm backend");
                    asm_var_table_free(&func_ctx.locals);
                    return -1;
                }
                const ChengNode *param_type = param->len > 1 ? param->kids[1] : NULL;
                if (asm_var_table_add(&func_ctx.locals, param->kids[0]->ident, param_type) != 0) {
                    add_diag(context->diags, context->filename, param,
                             "failed to allocate parameter");
                    asm_var_table_free(&func_ctx.locals);
                    return -1;
                }
            }
        }
    }
    if (collect_locals_from_list(body, &func_ctx.locals, context->filename, context->diags) != 0) {
        asm_var_table_free(&func_ctx.locals);
        return -1;
    }
    func_ctx.stack_size = assign_stack_offsets(&func_ctx.locals);
    char sym_buf[256];
    const char *sym = asm_mangle_symbol(context, name, sym_buf, sizeof(sym_buf));
    fprintf(context->output, ".globl %s\n", sym);
    fprintf(context->output, "%s:\n", sym);
    fprintf(context->output, "  pushq %%rbp\n");
    fprintf(context->output, "  movq %%rsp, %%rbp\n");
    if (func_ctx.stack_size > 0) {
        fprintf(context->output, "  subq $%d, %%rsp\n", func_ctx.stack_size);
    }
    for (size_t param_index = 0; param_index < func_ctx.locals.len; param_index++) {
        if (!params || params->kind != NK_FORMAL_PARAMS || param_index >= params->len) {
            break;
        }
        int offset = func_ctx.locals.items[param_index].offset;
        if (param_index < 6) {
            const char *arg_reg = x86_64_arg_reg(param_index);
            if (!arg_reg) {
                add_diag(context->diags, context->filename, params->kids[param_index],
                         "unsupported argument register");
                asm_var_table_free(&func_ctx.locals);
                return -1;
            }
            fprintf(context->output, "  movq %s, %d(%%rbp)\n", arg_reg, offset);
        } else {
            int stack_arg_offset = 16 + (int)((param_index - 6) * 8);
            fprintf(context->output, "  movq %d(%%rbp), %%rax\n", stack_arg_offset);
            fprintf(context->output, "  movq %%rax, %d(%%rbp)\n", offset);
        }
    }
    func_ctx.end_label = asm_next_label(context);
    func_ctx.loop_depth = 0;
    if (emit_stmt_list_x86_64(&func_ctx, body) != 0) {
        asm_var_table_free(&func_ctx.locals);
        return -1;
    }
    emit_x86_64_imm(context->output, 0);
    fprintf(context->output, "  jmp .L%d\n", func_ctx.end_label);
    asm_emit_label(context->output, func_ctx.end_label);
    fprintf(context->output, "  leave\n");
    fprintf(context->output, "  ret\n");
    asm_var_table_free(&func_ctx.locals);
    return 0;
}

static int emit_x86_64_darwin_file(FILE *output,
                                   const ChengNode *root,
                                   const char *filename,
                                   ChengDiagList *diags,
                                   AsmLayout *layout,
                                   const AsmModuleFilter *module_filter) {
    if (!output || !root) {
        return -1;
    }
    const ChengNode *list = get_top_level_list(root);
    if (!list || list->kind != NK_STMT_LIST) {
        add_diag(diags, filename, root, "asm backend expects a top-level statement list");
        return -1;
    }
    int rc = -1;
    AsmFuncTable table;
    asm_func_table_init(&table);
    ChengStrList importc;
    cheng_strlist_init(&importc);
    AsmEmitContext context = {0};
    context.output = output;
    context.filename = filename;
    context.diags = diags;
    context.label_counter = 1;
    context.root = root;
    context.hard_rt = asm_env_is_hard_rt();
    context.mangle_underscore = 1;
    context.layout = layout;
    context.module_filter = module_filter;
    asm_global_table_init(&context.globals);
    context.module_prefix = asm_module_prefix_from_env(module_filter);
    if (collect_global_types(list, &context.globals, filename, diags) != 0) {
        goto cleanup;
    }
    collect_importc_names(list, &importc);
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_FN_DECL) {
            if (asm_func_table_add(&table, stmt) != 0) {
                add_diag(diags, filename, stmt, "failed to collect function declarations");
                goto cleanup;
            }
        }
    }
    if (module_filter) {
        for (size_t index = 0; index < table.len; index++) {
            AsmFuncEntry *entry = &table.items[index];
            entry->emit = asm_module_filter_has_func(module_filter, entry->name);
        }
    } else {
        AsmFuncEntry *main_entry = asm_func_table_find(&table, "main");
        if (!main_entry) {
            add_diag(diags, filename, root, "asm backend requires a main function");
            goto cleanup;
        }
        main_entry->emit = 1;
        int changed = 1;
        while (changed) {
            changed = 0;
            for (size_t index = 0; index < table.len; index++) {
                AsmFuncEntry *entry = &table.items[index];
                if (entry->emit && !entry->scanned) {
                    entry->scanned = 1;
                    if (asm_func_is_runtime_name(entry->name) ||
                        asm_func_is_importc_name(&importc, entry->name) ||
                        !asm_func_decl_has_body(entry->decl)) {
                        continue;
                    }
                    if (entry->decl && entry->decl->len > 3) {
                        const ChengNode *body = entry->decl->kids[3];
                        asm_func_table_mark_calls(body, &table, &importc, &changed);
                    }
                }
            }
        }
    }
    fprintf(output, ".section __TEXT,__text,regular,pure_instructions\n");
    for (size_t index = 0; index < table.len; index++) {
        AsmFuncEntry *entry = &table.items[index];
        if (!asm_func_entry_should_emit(entry, &importc)) {
            continue;
        }
        if (emit_function_x86_64_darwin(&context, entry->decl) != 0) {
            goto cleanup;
        }
    }
    rc = 0;
cleanup:
    if (context.module_prefix) {
        free(context.module_prefix);
    }
    cheng_strlist_free(&importc);
    asm_global_table_free(&context.globals);
    asm_func_table_free(&table);
    return rc;
}

static void emit_rv64_imm(FILE *output, int64_t value) {
    fprintf(output, "  li a0, %lld\n", (long long)value);
}

static int emit_expr_rv64(AsmFuncContext *func_ctx, const ChengNode *expr);

static void emit_orc_call_rv64(AsmFuncContext *func_ctx,
                               const char *name,
                               const char *value_reg,
                               int preserve_a0) {
    if (!func_ctx || !name || !value_reg) {
        return;
    }
    FILE *output = func_ctx->context->output;
    if (preserve_a0) {
        fprintf(output, "  addi sp, sp, -16\n");
        fprintf(output, "  sd a0, 8(sp)\n");
    }
    if (strcmp(value_reg, "a0") != 0) {
        fprintf(output, "  mv a0, %s\n", value_reg);
    }
    const char *call_sym = name;
    const char *runtime_sym = NULL;
    if (asm_runtime_lookup_symbol(name, &runtime_sym)) {
        call_sym = runtime_sym;
    }
    char sym_buf[256];
    call_sym = asm_mangle_symbol(func_ctx->context, call_sym, sym_buf, sizeof(sym_buf));
    fprintf(output, "  call %s\n", call_sym);
    if (preserve_a0) {
        fprintf(output, "  ld a0, 8(sp)\n");
        fprintf(output, "  addi sp, sp, 16\n");
    }
}

static int emit_result_unwrap_rv64(AsmFuncContext *func_ctx, const ChengNode *value) {
    if (!func_ctx || !value) {
        return -1;
    }
    const char *type_name = result_type_name_from_expr(func_ctx, value);
    if (!type_name || !type_name_is_result(type_name)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, value,
                 "Result/? expects a Result value in asm backend");
        return -1;
    }
    if (func_ctx->ret_is_result && func_ctx->ret_type_expr) {
        const char *ret_name = type_name_from_type_expr(func_ctx->ret_type_expr);
        if (ret_name && strcmp(ret_name, type_name) != 0) {
            add_diag(func_ctx->context->diags, func_ctx->context->filename, value,
                     "Result/? type mismatch in asm backend");
            return -1;
        }
    }
    ResultFieldInfo info;
    if (!result_field_info(func_ctx->context, type_name, &info)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, value,
                 "Result/? missing layout info in asm backend");
        return -1;
    }
    if (emit_expr_rv64(func_ctx, value) != 0) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    fprintf(output, "  mv t1, a0\n");
    switch (asm_access_kind_from_size(info.ok_size)) {
        case ASM_ACCESS_BYTE:
            fprintf(output, "  lbu t0, %zu(t1)\n", info.ok_offset);
            break;
        case ASM_ACCESS_WORD:
            fprintf(output, "  lhu t0, %zu(t1)\n", info.ok_offset);
            break;
        case ASM_ACCESS_DWORD:
            fprintf(output, "  lw t0, %zu(t1)\n", info.ok_offset);
            break;
        case ASM_ACCESS_QWORD:
        default:
            fprintf(output, "  ld t0, %zu(t1)\n", info.ok_offset);
            break;
    }
    int err_label = asm_next_label(func_ctx->context);
    int done_label = asm_next_label(func_ctx->context);
    fprintf(output, "  beqz t0, .L%d\n", err_label);
    switch (asm_access_kind_from_size(info.value_size)) {
        case ASM_ACCESS_BYTE:
            fprintf(output, "  lbu a0, %zu(t1)\n", info.value_offset);
            break;
        case ASM_ACCESS_WORD:
            fprintf(output, "  lhu a0, %zu(t1)\n", info.value_offset);
            break;
        case ASM_ACCESS_DWORD:
            fprintf(output, "  lw a0, %zu(t1)\n", info.value_offset);
            break;
        case ASM_ACCESS_QWORD:
        default:
            fprintf(output, "  ld a0, %zu(t1)\n", info.value_offset);
            break;
    }
    fprintf(output, "  j .L%d\n", done_label);
    asm_emit_label(output, err_label);
    if (func_ctx->ret_is_result) {
        fprintf(output, "  mv a0, t1\n");
        fprintf(output, "  j .L%d\n", func_ctx->end_label);
    } else {
        switch (asm_access_kind_from_size(info.err_size)) {
            case ASM_ACCESS_BYTE:
                fprintf(output, "  lbu a0, %zu(t1)\n", info.err_offset);
                break;
            case ASM_ACCESS_WORD:
                fprintf(output, "  lhu a0, %zu(t1)\n", info.err_offset);
                break;
            case ASM_ACCESS_DWORD:
                fprintf(output, "  lw a0, %zu(t1)\n", info.err_offset);
                break;
            case ASM_ACCESS_QWORD:
            default:
                fprintf(output, "  ld a0, %zu(t1)\n", info.err_offset);
                break;
        }
        emit_orc_call_rv64(func_ctx, "panic", "a0", 0);
        emit_rv64_imm(output, 0);
        fprintf(output, "  j .L%d\n", func_ctx->end_label);
    }
    asm_emit_label(output, done_label);
    return 0;
}

static int emit_expr_infix_rv64(AsmFuncContext *func_ctx, const ChengNode *expr) {
    if (!func_ctx || !expr || expr->len < 3 || !expr->kids[0] || !expr->kids[0]->ident) {
        return -1;
    }
    const char *op = expr->kids[0]->ident;
    const ChengNode *lhs = expr->kids[1];
    const ChengNode *rhs = expr->kids[2];
    if (expr_contains_call(func_ctx, lhs) || expr_contains_call(func_ctx, rhs)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                 "call inside infix expression is not supported in asm backend");
        return -1;
    }
    if (emit_expr_rv64(func_ctx, lhs) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  addi sp, sp, -8\n");
    fprintf(func_ctx->context->output, "  sd a0, 0(sp)\n");
    if (emit_expr_rv64(func_ctx, rhs) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  ld t0, 0(sp)\n");
    fprintf(func_ctx->context->output, "  addi sp, sp, 8\n");
    if (strcmp(op, "+") == 0) {
        fprintf(func_ctx->context->output, "  add a0, t0, a0\n");
        return 0;
    }
    if (strcmp(op, "-") == 0) {
        fprintf(func_ctx->context->output, "  sub a0, t0, a0\n");
        return 0;
    }
    if (strcmp(op, "*") == 0) {
        fprintf(func_ctx->context->output, "  mul a0, t0, a0\n");
        return 0;
    }
    if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
        fprintf(func_ctx->context->output, "  div a0, t0, a0\n");
        return 0;
    }
    if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
        fprintf(func_ctx->context->output, "  rem a0, t0, a0\n");
        return 0;
    }
    if (strcmp(op, "&") == 0) {
        fprintf(func_ctx->context->output, "  and a0, t0, a0\n");
        return 0;
    }
    if (strcmp(op, "|") == 0) {
        fprintf(func_ctx->context->output, "  or a0, t0, a0\n");
        return 0;
    }
    if (strcmp(op, "^") == 0) {
        fprintf(func_ctx->context->output, "  xor a0, t0, a0\n");
        return 0;
    }
    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
        if (strcmp(op, "<<") == 0) {
            fprintf(func_ctx->context->output, "  sll a0, t0, a0\n");
        } else {
            fprintf(func_ctx->context->output, "  sra a0, t0, a0\n");
        }
        return 0;
    }
    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        fprintf(func_ctx->context->output, "  snez t0, t0\n");
        fprintf(func_ctx->context->output, "  snez a0, a0\n");
        if (strcmp(op, "&&") == 0) {
            fprintf(func_ctx->context->output, "  and a0, t0, a0\n");
        } else {
            fprintf(func_ctx->context->output, "  or a0, t0, a0\n");
        }
        return 0;
    }
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
        if (strcmp(op, "==") == 0) {
            fprintf(func_ctx->context->output, "  sub t0, t0, a0\n");
            fprintf(func_ctx->context->output, "  seqz a0, t0\n");
        } else if (strcmp(op, "!=") == 0) {
            fprintf(func_ctx->context->output, "  sub t0, t0, a0\n");
            fprintf(func_ctx->context->output, "  snez a0, t0\n");
        } else if (strcmp(op, "<") == 0) {
            fprintf(func_ctx->context->output, "  slt a0, t0, a0\n");
        } else if (strcmp(op, "<=") == 0) {
            fprintf(func_ctx->context->output, "  slt a0, a0, t0\n");
            fprintf(func_ctx->context->output, "  xori a0, a0, 1\n");
        } else if (strcmp(op, ">") == 0) {
            fprintf(func_ctx->context->output, "  slt a0, a0, t0\n");
        } else {
            fprintf(func_ctx->context->output, "  slt a0, t0, a0\n");
            fprintf(func_ctx->context->output, "  xori a0, a0, 1\n");
        }
        return 0;
    }
    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
             "unsupported infix operator in asm backend");
    return -1;
}

static int emit_expr_rv64(AsmFuncContext *func_ctx, const ChengNode *expr) {
    if (!func_ctx || !expr) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    switch (expr->kind) {
        case NK_PAR:
            if (expr->len > 0) {
                return emit_expr_rv64(func_ctx, expr->kids[0]);
            }
            return -1;
        case NK_INT_LIT:
            emit_rv64_imm(output, expr->int_val);
            return 0;
        case NK_CHAR_LIT: {
            int ch = 0;
            if (!decode_char_lit(expr->str_val, &ch)) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid char literal");
                return -1;
            }
            emit_rv64_imm(output, (int64_t)ch);
            return 0;
        }
        case NK_BOOL_LIT: {
            int val = 0;
            if (expr->ident && strcmp(expr->ident, "true") == 0) {
                val = 1;
            }
            emit_rv64_imm(output, (int64_t)val);
            return 0;
        }
        case NK_IDENT: {
            int offset = 0;
            if (!asm_var_table_find(&func_ctx->locals, expr->ident, &offset)) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "unknown identifier in asm backend");
                return -1;
            }
            fprintf(output, "  ld a0, %d(s0)\n", offset);
            return 0;
        }
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0] || !expr->kids[0]->ident) {
                return -1;
            }
            const char *op = expr->kids[0]->ident;
            const ChengNode *rhs = expr->kids[1];
            if (expr_contains_call(func_ctx, rhs)) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "call inside prefix expression is not supported in asm backend");
                return -1;
            }
            if (emit_expr_rv64(func_ctx, rhs) != 0) {
                return -1;
            }
            if (strcmp(op, "+") == 0) {
                return 0;
            }
            if (strcmp(op, "-") == 0) {
                fprintf(output, "  neg a0, a0\n");
                return 0;
            }
            if (strcmp(op, "~") == 0) {
                fprintf(output, "  not a0, a0\n");
                return 0;
            }
            if (strcmp(op, "!") == 0) {
                fprintf(output, "  seqz a0, a0\n");
                return 0;
            }
            add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                     "unsupported prefix operator in asm backend");
            return -1;
        }
        case NK_INFIX:
            return emit_expr_infix_rv64(func_ctx, expr);
        case NK_POSTFIX: {
            const ChengNode *value = NULL;
            const ChengNode *op = NULL;
            const char *op_name = NULL;
            if (expr->len > 0) {
                op = expr->kids[0];
            }
            if (op && (op->kind == NK_IDENT || op->kind == NK_SYMBOL)) {
                op_name = op->ident;
            }
            if (expr->len > 1) {
                value = expr->kids[1];
            } else if (expr->len > 0) {
                value = expr->kids[0];
            }
            if (!value) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid postfix expression in asm backend");
                return -1;
            }
            if (op_name && strcmp(op_name, "?") == 0) {
                return emit_result_unwrap_rv64(func_ctx, value);
            }
            return emit_expr_rv64(func_ctx, value);
        }
        case NK_CALL: {
            if (expr->len < 1 || !expr->kids[0]) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "invalid call expression");
                return -1;
            }
            const ChengNode *callee = expr->kids[0];
            if (callee->kind != NK_IDENT || !callee->ident) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                         "unsupported call target in asm backend");
                return -1;
            }
            size_t arg_count = expr->len - 1;
            if (strcmp(callee->ident, "default") == 0) {
                if (arg_count != 1) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "default requires one type argument");
                    return -1;
                }
                const ChengNode *arg = expr->kids[1];
                if (arg && arg->kind == NK_CALL_ARG) {
                    if (arg->len > 1) {
                        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                                 "default does not support named arguments");
                        return -1;
                    }
                    arg = arg->len > 0 ? arg->kids[0] : NULL;
                }
                if (!arg) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "default requires one type argument");
                    return -1;
                }
                emit_rv64_imm(output, 0);
                return 0;
            }
            if (strcmp(callee->ident, "sizeof") == 0 || strcmp(callee->ident, "alignof") == 0) {
                if (arg_count != 1) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "sizeof/alignof requires one type argument");
                    return -1;
                }
                const ChengNode *arg = expr->kids[1];
                if (arg && arg->kind == NK_CALL_ARG) {
                    if (arg->len > 1) {
                        add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                                 "sizeof/alignof does not support named arguments");
                        return -1;
                    }
                    arg = arg->len > 0 ? arg->kids[0] : NULL;
                }
                if (!arg) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "sizeof/alignof requires one type argument");
                    return -1;
                }
                if (!func_ctx->context->layout) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "asm backend missing layout for sizeof/alignof");
                    return -1;
                }
                size_t size = 0;
                size_t align = 0;
                if (!asm_layout_type_info(func_ctx->context->layout, arg, &size, &align)) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "asm backend failed to resolve sizeof/alignof type");
                    return -1;
                }
                int64_t value = strcmp(callee->ident, "sizeof") == 0 ? (int64_t)size : (int64_t)align;
                emit_rv64_imm(output, value);
                return 0;
            }
            int reg_args = arg_count < 8 ? (int)arg_count : 8;
            int stack_args = arg_count > 8 ? (int)(arg_count - 8) : 0;
            int64_t stack_bytes = (int64_t)stack_args * 8;
            int64_t temp_bytes = (int64_t)reg_args * 8;
            int64_t arg_bytes = stack_bytes + temp_bytes;
            if (arg_bytes % 16 != 0) {
                arg_bytes += 8;
            }
            if (arg_bytes > 0) {
                fprintf(output, "  addi sp, sp, -%lld\n", (long long)arg_bytes);
            }
            for (size_t arg_index = 0; arg_index < arg_count; arg_index++) {
                const ChengNode *arg = expr->kids[arg_index + 1];
                if (arg && arg->kind == NK_CALL_ARG) {
                    if (arg->len > 1) {
                        arg = arg->kids[1];
                    } else {
                        arg = NULL;
                    }
                }
                if (!arg) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "invalid call argument");
                    return -1;
                }
                if (expr_contains_call(func_ctx, arg)) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, expr,
                             "nested call in argument is not supported in asm backend");
                    return -1;
                }
                if (emit_expr_rv64(func_ctx, arg) != 0) {
                    return -1;
                }
                int64_t off = 0;
                if (arg_index >= 8) {
                    off = (int64_t)(arg_index - 8) * 8;
                } else {
                    off = stack_bytes + (int64_t)arg_index * 8;
                }
                fprintf(output, "  sd a0, %lld(sp)\n", (long long)off);
            }
            for (int arg_index = 0; arg_index < reg_args; arg_index++) {
                int64_t off = stack_bytes + (int64_t)arg_index * 8;
                fprintf(output, "  ld a%d, %lld(sp)\n", arg_index, (long long)off);
            }
            {
                const char *call_sym = callee->ident;
                const char *runtime_sym = NULL;
                if (asm_runtime_lookup_symbol(callee->ident, &runtime_sym)) {
                    call_sym = runtime_sym;
                }
                char sym_buf[256];
                call_sym = asm_mangle_symbol(func_ctx->context, call_sym, sym_buf, sizeof(sym_buf));
                fprintf(output, "  call %s\n", call_sym);
            }
            if (arg_bytes > 0) {
                fprintf(output, "  addi sp, sp, %lld\n", (long long)arg_bytes);
            }
            return 0;
        }
        default:
            {
                char msg[96];
                snprintf(msg, sizeof(msg), "unsupported expression kind %d in asm backend",
                         (int)expr->kind);
                add_diag(func_ctx->context->diags, func_ctx->context->filename, expr, msg);
            }
            return -1;
    }
}

static int emit_stmt_rv64(AsmFuncContext *func_ctx, const ChengNode *stmt);

static int emit_stmt_list_rv64(AsmFuncContext *func_ctx, const ChengNode *list) {
    if (!list) {
        return 0;
    }
    if (list->kind != NK_STMT_LIST) {
        return emit_stmt_rv64(func_ctx, list);
    }
    for (size_t index = 0; index < list->len; index++) {
        if (emit_stmt_rv64(func_ctx, list->kids[index]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int emit_if_rv64(AsmFuncContext *func_ctx, const ChengNode *if_node, int end_label) {
    if (!func_ctx || !if_node || if_node->len < 2) {
        return -1;
    }
    int else_label = asm_next_label(func_ctx->context);
    if (emit_expr_rv64(func_ctx, if_node->kids[0]) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  beqz a0, .L%d\n", else_label);
    if (emit_stmt_list_rv64(func_ctx, if_node->kids[1]) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  j .L%d\n", end_label);
    asm_emit_label(func_ctx->context->output, else_label);
    if (if_node->len > 2 && if_node->kids[2] && if_node->kids[2]->kind == NK_ELSE &&
        if_node->kids[2]->len > 0) {
        const ChengNode *else_body = if_node->kids[2]->kids[0];
        if (else_body && else_body->kind == NK_IF) {
            if (emit_if_rv64(func_ctx, else_body, end_label) != 0) {
                return -1;
            }
        } else if (emit_stmt_list_rv64(func_ctx, else_body) != 0) {
            return -1;
        }
    }
    return 0;
}

static int emit_while_rv64(AsmFuncContext *func_ctx, const ChengNode *while_node) {
    if (!func_ctx || !while_node || while_node->len < 2) {
        return -1;
    }
    int start_label = asm_next_label(func_ctx->context);
    int end_label = asm_next_label(func_ctx->context);
    if (func_ctx->loop_depth >= 64) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, while_node,
                 "loop nesting too deep in asm backend");
        return -1;
    }
    func_ctx->loop_start_labels[func_ctx->loop_depth] = start_label;
    func_ctx->loop_end_labels[func_ctx->loop_depth] = end_label;
    func_ctx->loop_depth++;
    asm_emit_label(func_ctx->context->output, start_label);
    if (emit_expr_rv64(func_ctx, while_node->kids[0]) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  beqz a0, .L%d\n", end_label);
    if (emit_stmt_list_rv64(func_ctx, while_node->kids[1]) != 0) {
        return -1;
    }
    fprintf(func_ctx->context->output, "  j .L%d\n", start_label);
    asm_emit_label(func_ctx->context->output, end_label);
    func_ctx->loop_depth--;
    return 0;
}

static int emit_for_rv64(AsmFuncContext *func_ctx, const ChengNode *for_node) {
    if (!func_ctx || !for_node || for_node->len < 3) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "invalid for statement");
        return -1;
    }
    const ChengNode *pattern = for_node->kids[0];
    const ChengNode *iter = for_node->kids[1];
    const ChengNode *body = for_node->kids[2];
    const ChengNode *ident = pattern_ident_node(pattern);
    if (!ident || !ident->ident) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "for requires identifier pattern in asm backend");
        return -1;
    }
    const ChengNode *start = NULL;
    const ChengNode *end = NULL;
    int inclusive = 0;
    if (iter && iter->kind == NK_INFIX && iter->len >= 3 && iter->kids[0] && iter->kids[0]->ident) {
        const char *op = iter->kids[0]->ident;
        if (strcmp(op, "..") == 0 || strcmp(op, "..<") == 0) {
            start = iter->kids[1];
            end = iter->kids[2];
            inclusive = strcmp(op, "..") == 0;
        }
    }
    if (!start || !end) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "for only supports numeric ranges (a ..< b)");
        return -1;
    }
    int offset = 0;
    if (!asm_var_table_find(&func_ctx->locals, ident->ident, &offset)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "unknown loop variable in asm backend");
        return -1;
    }
    if (emit_expr_rv64(func_ctx, start) != 0) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    fprintf(output, "  sd a0, %d(s0)\n", offset);
    int cond_label = asm_next_label(func_ctx->context);
    int step_label = asm_next_label(func_ctx->context);
    int end_label = asm_next_label(func_ctx->context);
    if (func_ctx->loop_depth >= 64) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, for_node,
                 "loop nesting too deep in asm backend");
        return -1;
    }
    func_ctx->loop_start_labels[func_ctx->loop_depth] = step_label;
    func_ctx->loop_end_labels[func_ctx->loop_depth] = end_label;
    func_ctx->loop_depth++;
    asm_emit_label(output, cond_label);
    if (emit_expr_rv64(func_ctx, end) != 0) {
        return -1;
    }
    fprintf(output, "  mv t1, a0\n");
    fprintf(output, "  ld a0, %d(s0)\n", offset);
    if (inclusive) {
        fprintf(output, "  blt t1, a0, .L%d\n", end_label);
    } else {
        fprintf(output, "  bge a0, t1, .L%d\n", end_label);
    }
    if (emit_stmt_list_rv64(func_ctx, body) != 0) {
        return -1;
    }
    asm_emit_label(output, step_label);
    fprintf(output, "  ld a0, %d(s0)\n", offset);
    fprintf(output, "  addi a0, a0, 1\n");
    fprintf(output, "  sd a0, %d(s0)\n", offset);
    fprintf(output, "  j .L%d\n", cond_label);
    asm_emit_label(output, end_label);
    func_ctx->loop_depth--;
    return 0;
}

static int emit_case_rv64(AsmFuncContext *func_ctx, const ChengNode *stmt) {
    if (!func_ctx || !stmt || stmt->len < 1) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                 "invalid case statement in asm backend");
        return -1;
    }
    const ChengNode *selector = stmt->kids[0];
    char tmp_name[64];
    format_case_tmp_name(stmt, tmp_name, sizeof(tmp_name));
    int tmp_offset = 0;
    if (!asm_var_table_find(&func_ctx->locals, tmp_name, &tmp_offset)) {
        add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                 "missing case temp in asm backend");
        return -1;
    }
    if (emit_expr_rv64(func_ctx, selector) != 0) {
        return -1;
    }
    FILE *output = func_ctx->context->output;
    fprintf(output, "  sd a0, %d(s0)\n", tmp_offset);
    int end_label = asm_next_label(func_ctx->context);
    for (size_t i = 1; i < stmt->len; i++) {
        const ChengNode *branch = stmt->kids[i];
        if (!branch) {
            continue;
        }
        if (branch->kind == NK_OF_BRANCH) {
            const ChengNode *pattern = branch->len > 0 ? branch->kids[0] : NULL;
            const ChengNode *body = branch->len > 1 ? branch->kids[1] : NULL;
            int match_label = asm_next_label(func_ctx->context);
            const char *bind_name = NULL;
            int unconditional = 0;
            int saw_pattern = 0;
            if (pattern && pattern->kind == NK_PATTERN) {
                for (size_t p = 0; p < pattern->len; p++) {
                    const ChengNode *pat = pattern->kids[p];
                    if (!pat) {
                        continue;
                    }
                    const char *name = case_pattern_ident(pat);
                    if (name) {
                        saw_pattern = 1;
                        if (!case_pattern_is_wildcard(name)) {
                            bind_name = name;
                        }
                        unconditional = 1;
                        break;
                    }
                    if (!hard_rt_case_pattern_const(pat, NULL)) {
                        add_diag(func_ctx->context->diags, func_ctx->context->filename, pat,
                                 hard_rt_case_pattern_error(func_ctx));
                        return -1;
                    }
                    saw_pattern = 1;
                    if (emit_expr_rv64(func_ctx, pat) != 0) {
                        return -1;
                    }
                    fprintf(output, "  ld t1, %d(s0)\n", tmp_offset);
                    fprintf(output, "  beq a0, t1, .L%d\n", match_label);
                }
            } else if (pattern) {
                const char *name = case_pattern_ident(pattern);
                if (name) {
                    saw_pattern = 1;
                    if (!case_pattern_is_wildcard(name)) {
                        bind_name = name;
                    }
                    unconditional = 1;
                } else if (hard_rt_case_pattern_const(pattern, NULL)) {
                    saw_pattern = 1;
                    if (emit_expr_rv64(func_ctx, pattern) != 0) {
                        return -1;
                    }
                    fprintf(output, "  ld t1, %d(s0)\n", tmp_offset);
                    fprintf(output, "  beq a0, t1, .L%d\n", match_label);
                }
            }
            if (!saw_pattern) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, branch,
                         "invalid case pattern in asm backend");
                return -1;
            }
            int next_label = -1;
            if (unconditional) {
                fprintf(output, "  j .L%d\n", match_label);
            } else {
                next_label = asm_next_label(func_ctx->context);
                fprintf(output, "  j .L%d\n", next_label);
            }
            asm_emit_label(output, match_label);
            if (bind_name) {
                int bind_offset = 0;
                if (!asm_var_table_find(&func_ctx->locals, bind_name, &bind_offset)) {
                    add_diag(func_ctx->context->diags, func_ctx->context->filename, branch,
                             "unknown case binding in asm backend");
                    return -1;
                }
                fprintf(output, "  ld a0, %d(s0)\n", tmp_offset);
                fprintf(output, "  sd a0, %d(s0)\n", bind_offset);
            }
            if (emit_stmt_list_rv64(func_ctx, body) != 0) {
                return -1;
            }
            fprintf(output, "  j .L%d\n", end_label);
            if (!unconditional && next_label >= 0) {
                asm_emit_label(output, next_label);
            }
            continue;
        }
        if (branch->kind == NK_ELSE) {
            const ChengNode *body = branch->len > 0 ? branch->kids[0] : NULL;
            if (emit_stmt_list_rv64(func_ctx, body) != 0) {
                return -1;
            }
            fprintf(output, "  j .L%d\n", end_label);
        }
    }
    asm_emit_label(output, end_label);
    return 0;
}

static int emit_stmt_rv64(AsmFuncContext *func_ctx, const ChengNode *stmt) {
    if (!func_ctx || !stmt) {
        return 0;
    }
    FILE *output = func_ctx->context->output;
    switch (stmt->kind) {
        case NK_RETURN: {
            if (stmt->len > 0 && stmt->kids[0] && stmt->kids[0]->kind != NK_EMPTY) {
                if (emit_expr_rv64(func_ctx, stmt->kids[0]) != 0) {
                    return -1;
                }
                if (orc_mm_enabled() && func_ctx->ret_type_expr &&
                    orc_type_is_managed(func_ctx->ret_type_expr) &&
                    orc_expr_is_borrowed(stmt->kids[0])) {
                    emit_orc_call_rv64(func_ctx, "memRetain", "a0", 1);
                }
            } else {
                emit_rv64_imm(output, 0);
            }
            fprintf(output, "  j .L%d\n", func_ctx->end_label);
            return 0;
        }
        case NK_IF:
            return emit_if_rv64(func_ctx, stmt, func_ctx->end_label);
        case NK_WHILE:
            return emit_while_rv64(func_ctx, stmt);
        case NK_FOR:
            return emit_for_rv64(func_ctx, stmt);
        case NK_CASE:
            return emit_case_rv64(func_ctx, stmt);
        case NK_VAR:
        case NK_LET:
        case NK_CONST: {
            if (stmt->len < 1) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "invalid local declaration");
                return -1;
            }
            const ChengNode *pattern = stmt->kids[0];
            const ChengNode *ident = pattern_ident_node(pattern);
            if (!ident || !ident->ident) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "unsupported local pattern in asm backend");
                return -1;
            }
            int offset = 0;
            if (!asm_var_table_find(&func_ctx->locals, ident->ident, &offset)) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "unknown local in asm backend");
                return -1;
            }
            const ChengNode *value = NULL;
            if (stmt->len > 2) {
                value = stmt->kids[2];
            }
            if (!value || value->kind == NK_EMPTY) {
                emit_rv64_imm(output, 0);
            } else {
                if (emit_expr_rv64(func_ctx, value) != 0) {
                    return -1;
                }
            }
            if (orc_mm_enabled() && value && value->kind != NK_EMPTY) {
                const ChengNode *type_expr = var_type_expr_from_ident(func_ctx, ident);
                if (orc_type_is_managed(type_expr) && orc_expr_is_borrowed(value)) {
                    emit_orc_call_rv64(func_ctx, "memRetain", "a0", 1);
                }
            }
            fprintf(output, "  sd a0, %d(s0)\n", offset);
            return 0;
        }
        case NK_ASGN: {
            if (stmt->len < 2) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "invalid assignment in asm backend");
                return -1;
            }
            const ChengNode *lhs = stmt->kids[0];
            if (!lhs || lhs->kind != NK_IDENT || !lhs->ident) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "unsupported assignment target in asm backend");
                return -1;
            }
            int offset = 0;
            if (!asm_var_table_find(&func_ctx->locals, lhs->ident, &offset)) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "unknown local in asm backend");
                return -1;
            }
            if (emit_expr_rv64(func_ctx, stmt->kids[1]) != 0) {
                return -1;
            }
            if (orc_mm_enabled()) {
                const ChengNode *type_expr = var_type_expr_from_ident(func_ctx, lhs);
                if (orc_type_is_managed(type_expr)) {
                    if (orc_expr_is_borrowed(stmt->kids[1])) {
                        emit_orc_call_rv64(func_ctx, "memRetain", "a0", 1);
                    }
                    fprintf(output, "  ld t0, %d(s0)\n", offset);
                    fprintf(output, "  sd a0, %d(s0)\n", offset);
                    emit_orc_call_rv64(func_ctx, "memRelease", "t0", 0);
                    return 0;
                }
            }
            fprintf(output, "  sd a0, %d(s0)\n", offset);
            return 0;
        }
        case NK_BREAK: {
            if (func_ctx->loop_depth == 0) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "break outside loop in asm backend");
                return -1;
            }
            size_t depth_index = func_ctx->loop_depth - 1;
            fprintf(output, "  j .L%d\n", func_ctx->loop_end_labels[depth_index]);
            return 0;
        }
        case NK_CONTINUE: {
            if (func_ctx->loop_depth == 0) {
                add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                         "continue outside loop in asm backend");
                return -1;
            }
            size_t depth_index = func_ctx->loop_depth - 1;
            fprintf(output, "  j .L%d\n", func_ctx->loop_start_labels[depth_index]);
            return 0;
        }
        case NK_CALL:
            return emit_expr_rv64(func_ctx, stmt);
        case NK_STMT_LIST:
            return emit_stmt_list_rv64(func_ctx, stmt);
        case NK_EMPTY:
            return 0;
        case NK_PRAGMA:
            return 0;
        default:
            if (is_simple_expr_stmt_kind(stmt->kind)) {
                return emit_expr_rv64(func_ctx, stmt);
            }
            add_diag(func_ctx->context->diags, func_ctx->context->filename, stmt,
                     "unsupported statement in asm backend");
            return -1;
    }
}

static int emit_function_rv64_elf(AsmEmitContext *context, const ChengNode *fn_node) {
    if (!context || !fn_node || fn_node->kind != NK_FN_DECL || fn_node->len < 4) {
        return -1;
    }
    const ChengNode *name_node = fn_node->kids[0];
    if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
        add_diag(context->diags, context->filename, fn_node, "invalid function name");
        return -1;
    }
    const char *name = name_node->ident;
    const ChengNode *params = fn_node->kids[1];
    const ChengNode *ret_type = fn_node->kids[2];
    const ChengNode *body = fn_node->kids[3];
    AsmFuncContext func_ctx = {0};
    func_ctx.context = context;
    func_ctx.ret_kind = ret_kind_from_type(ret_type);
    func_ctx.ret_type_expr = ret_type;
    func_ctx.ret_is_result = type_expr_is_result(ret_type);
    asm_var_table_init(&func_ctx.locals);
    if (params && params->kind == NK_FORMAL_PARAMS) {
        if (params->len > 0) {
            for (size_t param_index = 0; param_index < params->len; param_index++) {
                const ChengNode *param = params->kids[param_index];
                if (!param || param->len < 1 || !param->kids[0] ||
                    param->kids[0]->kind != NK_IDENT || !param->kids[0]->ident) {
                    add_diag(context->diags, context->filename, param,
                             "unsupported parameter in asm backend");
                    asm_var_table_free(&func_ctx.locals);
                    return -1;
                }
                const ChengNode *param_type = param->len > 1 ? param->kids[1] : NULL;
                if (asm_var_table_add(&func_ctx.locals, param->kids[0]->ident, param_type) != 0) {
                    add_diag(context->diags, context->filename, param,
                             "failed to allocate parameter");
                    asm_var_table_free(&func_ctx.locals);
                    return -1;
                }
            }
        }
    }
    if (collect_locals_from_list(body, &func_ctx.locals, context->filename, context->diags) != 0) {
        asm_var_table_free(&func_ctx.locals);
        return -1;
    }
    func_ctx.stack_size = assign_stack_offsets(&func_ctx.locals);
    int frame_size = func_ctx.stack_size + 16;
    char sym_buf[256];
    const char *sym = asm_mangle_symbol(context, name, sym_buf, sizeof(sym_buf));
    fprintf(context->output, ".globl %s\n", sym);
    fprintf(context->output, ".type %s, @function\n", sym);
    fprintf(context->output, "%s:\n", sym);
    fprintf(context->output, "  addi sp, sp, -%d\n", frame_size);
    fprintf(context->output, "  sd ra, %d(sp)\n", frame_size - 8);
    fprintf(context->output, "  sd s0, %d(sp)\n", frame_size - 16);
    fprintf(context->output, "  addi s0, sp, %d\n", frame_size);
    for (size_t param_index = 0; param_index < func_ctx.locals.len; param_index++) {
        if (!params || params->kind != NK_FORMAL_PARAMS || param_index >= params->len) {
            break;
        }
        int offset = func_ctx.locals.items[param_index].offset;
        if (param_index < 8) {
            fprintf(context->output, "  sd a%zu, %d(s0)\n", param_index, offset);
        } else {
            int stack_arg_offset = (int)((param_index - 8) * 8);
            fprintf(context->output, "  ld t0, %d(s0)\n", stack_arg_offset);
            fprintf(context->output, "  sd t0, %d(s0)\n", offset);
        }
    }
    func_ctx.end_label = asm_next_label(context);
    func_ctx.loop_depth = 0;
    int handled_expr = 0;
    if (body && body->kind == NK_STMT_LIST && body->len == 1 &&
        body->kids[0] && is_simple_expr_stmt_kind(body->kids[0]->kind)) {
        if (emit_expr_rv64(&func_ctx, body->kids[0]) != 0) {
            asm_var_table_free(&func_ctx.locals);
            return -1;
        }
        fprintf(context->output, "  j .L%d\n", func_ctx.end_label);
        handled_expr = 1;
    } else {
        if (emit_stmt_list_rv64(&func_ctx, body) != 0) {
            asm_var_table_free(&func_ctx.locals);
            return -1;
        }
    }
    if (!handled_expr) {
        emit_rv64_imm(context->output, 0);
        fprintf(context->output, "  j .L%d\n", func_ctx.end_label);
    }
    asm_emit_label(context->output, func_ctx.end_label);
    fprintf(context->output, "  ld ra, %d(sp)\n", frame_size - 8);
    fprintf(context->output, "  ld s0, %d(sp)\n", frame_size - 16);
    fprintf(context->output, "  addi sp, sp, %d\n", frame_size);
    fprintf(context->output, "  ret\n");
    fprintf(context->output, ".size %s, .-%s\n", sym, sym);
    asm_var_table_free(&func_ctx.locals);
    return 0;
}

static int emit_rv64_elf_file(FILE *output,
                              const ChengNode *root,
                              const char *filename,
                              ChengDiagList *diags,
                              AsmLayout *layout,
                              const AsmModuleFilter *module_filter) {
    if (!output || !root) {
        return -1;
    }
    const ChengNode *list = get_top_level_list(root);
    if (!list || list->kind != NK_STMT_LIST) {
        add_diag(diags, filename, root, "asm backend expects a top-level statement list");
        return -1;
    }
    int rc = -1;
    AsmFuncTable table;
    asm_func_table_init(&table);
    ChengStrList importc;
    cheng_strlist_init(&importc);
    AsmEmitContext context = {0};
    context.output = output;
    context.filename = filename;
    context.diags = diags;
    context.label_counter = 1;
    context.root = root;
    context.hard_rt = asm_env_is_hard_rt();
    context.layout = layout;
    context.module_filter = module_filter;
    asm_global_table_init(&context.globals);
    context.module_prefix = asm_module_prefix_from_env(module_filter);
    if (collect_global_types(list, &context.globals, filename, diags) != 0) {
        goto cleanup;
    }
    collect_importc_names(list, &importc);
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_FN_DECL) {
            if (asm_func_table_add(&table, stmt) != 0) {
                add_diag(diags, filename, stmt, "failed to collect function declarations");
                goto cleanup;
            }
        }
    }
    if (module_filter) {
        for (size_t index = 0; index < table.len; index++) {
            AsmFuncEntry *entry = &table.items[index];
            entry->emit = asm_module_filter_has_func(module_filter, entry->name);
        }
    } else {
        AsmFuncEntry *main_entry = asm_func_table_find(&table, "main");
        if (!main_entry) {
            add_diag(diags, filename, root, "asm backend requires a main function");
            goto cleanup;
        }
        main_entry->emit = 1;
        int changed = 1;
        while (changed) {
            changed = 0;
            for (size_t index = 0; index < table.len; index++) {
                AsmFuncEntry *entry = &table.items[index];
                if (entry->emit && !entry->scanned) {
                    entry->scanned = 1;
                    if (asm_func_is_runtime_name(entry->name) ||
                        asm_func_is_importc_name(&importc, entry->name) ||
                        !asm_func_decl_has_body(entry->decl)) {
                        continue;
                    }
                    if (entry->decl && entry->decl->len > 3) {
                        const ChengNode *body = entry->decl->kids[3];
                        asm_func_table_mark_calls(body, &table, &importc, &changed);
                    }
                }
            }
        }
    }
    fprintf(output, ".text\n");
    for (size_t index = 0; index < table.len; index++) {
        AsmFuncEntry *entry = &table.items[index];
        if (!asm_func_entry_should_emit(entry, &importc)) {
            continue;
        }
        if (emit_function_rv64_elf(&context, entry->decl) != 0) {
            goto cleanup;
        }
    }
    rc = 0;
cleanup:
    if (context.module_prefix) {
        free(context.module_prefix);
    }
    cheng_strlist_free(&importc);
    asm_global_table_free(&context.globals);
    asm_func_table_free(&table);
    return rc;
}

static int emit_main_asm(FILE *f,
                         AsmTarget target,
                         AsmAbi abi,
                         AsmRetKind kind,
                         int64_t value) {
    const char *label = (abi == ASM_ABI_DARWIN) ? "_main" : "main";
    if (abi == ASM_ABI_DARWIN) {
        fprintf(f, ".section __TEXT,__text,regular,pure_instructions\n");
    } else {
        fprintf(f, ".text\n");
    }
    fprintf(f, ".globl %s\n", label);
    if (abi == ASM_ABI_ELF) {
        fprintf(f, ".type %s, @function\n", label);
    }
    fprintf(f, "%s:\n", label);
    if (target == ASM_TARGET_X86_64) {
        if (kind == ASM_RET_I64) {
            fprintf(f, "  movabs $%lld, %%rax\n", (long long)value);
        } else {
            fprintf(f, "  movl $%lld, %%eax\n", (long long)(int32_t)value);
        }
        fprintf(f, "  ret\n");
    } else if (target == ASM_TARGET_AARCH64) {
        if (kind == ASM_RET_I64) {
            emit_aarch64_imm(f, "x0", (uint64_t)value, 0);
        } else {
            emit_aarch64_imm(f, "w0", (uint32_t)value, 1);
        }
        fprintf(f, "  ret\n");
    } else if (target == ASM_TARGET_RISCV64) {
        fprintf(f, "  li a0, %lld\n", (long long)value);
        fprintf(f, "  ret\n");
    } else {
        return -1;
    }
    if (abi == ASM_ABI_ELF) {
        fprintf(f, ".size %s, .-%s\n", label, label);
    }
    return 0;
}

static const char *data_section_directive(AsmAbi abi, AsmDataSection section) {
    switch (abi) {
        case ASM_ABI_DARWIN:
            switch (section) {
                case ASM_DATA_RODATA: return ".section __TEXT,__const";
                case ASM_DATA_DATA: return ".section __DATA,__data";
                case ASM_DATA_BSS: return ".section __DATA,__bss";
            }
            break;
        case ASM_ABI_COFF:
            switch (section) {
                case ASM_DATA_RODATA: return ".section .rdata";
                case ASM_DATA_DATA: return ".data";
                case ASM_DATA_BSS: return ".bss";
            }
            break;
        case ASM_ABI_ELF:
        default:
            switch (section) {
                case ASM_DATA_RODATA: return ".section .rodata";
                case ASM_DATA_DATA: return ".data";
                case ASM_DATA_BSS: return ".bss";
            }
            break;
    }
    return ".data";
}

static int abi_needs_prefix(AsmAbi abi) {
    return abi == ASM_ABI_DARWIN;
}

static void emit_align_directive(FILE *f, AsmAbi abi, size_t align) {
    if (align <= 1) {
        return;
    }
    if (abi == ASM_ABI_DARWIN) {
        size_t pow2 = 1;
        unsigned shift = 0;
        while (pow2 < align) {
            pow2 <<= 1;
            shift++;
        }
        if (pow2 == align) {
            fprintf(f, "  .p2align %u\n", shift);
        } else {
            fprintf(f, "  .balign %zu\n", align);
        }
    } else {
        fprintf(f, "  .balign %zu\n", align);
    }
}

static void emit_symbol_name(FILE *f, AsmAbi abi, const char *name, int needs_prefix) {
    if (!name) {
        return;
    }
    if (needs_prefix && abi_needs_prefix(abi)) {
        fprintf(f, "_%s", name);
    } else {
        fprintf(f, "%s", name);
    }
}

static void emit_bytes(FILE *f, const uint8_t *bytes, size_t len) {
    if (!bytes || len == 0) {
        return;
    }
    size_t i = 0;
    while (i < len) {
        size_t chunk = len - i;
        if (chunk > 16) {
            chunk = 16;
        }
        fprintf(f, "  .byte ");
        for (size_t j = 0; j < chunk; j++) {
            if (j > 0) {
                fprintf(f, ", ");
            }
            fprintf(f, "0x%02x", (unsigned)bytes[i + j]);
        }
        fprintf(f, "\n");
        i += chunk;
    }
}

static void emit_int_bytes(FILE *f, int64_t value, size_t size) {
    uint8_t bytes[16];
    if (size > sizeof(bytes)) {
        size = sizeof(bytes);
    }
    uint64_t uval = (uint64_t)value;
    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)((uval >> (i * 8)) & 0xff);
    }
    emit_bytes(f, bytes, size);
}

static void emit_int_value(FILE *f, int64_t value, size_t size) {
    switch (size) {
        case 1:
            fprintf(f, "  .byte %lld\n", (long long)(value & 0xff));
            break;
        case 2:
            fprintf(f, "  .short %lld\n", (long long)(value & 0xffff));
            break;
        case 4:
            fprintf(f, "  .long %lld\n", (long long)(value & 0xffffffff));
            break;
        case 8:
            fprintf(f, "  .quad %lld\n", (long long)value);
            break;
        default:
            emit_int_bytes(f, value, size);
            break;
    }
}

static void emit_label_value(FILE *f, AsmAbi abi, const char *label, int needs_prefix, size_t size) {
    const char *dir = NULL;
    if (size == 4) {
        dir = "  .long ";
    } else {
        dir = "  .quad ";
    }
    fprintf(f, "%s", dir);
    emit_symbol_name(f, abi, label, needs_prefix);
    fprintf(f, "\n");
}

static void emit_data_section(FILE *f,
                              AsmAbi abi,
                              AsmDataSection section,
                              const AsmLayout *layout,
                              const AsmModuleFilter *module_filter) {
    size_t len = 0;
    const AsmDataItem *items = asm_layout_data_items(layout, &len);
    if (!items || len == 0) {
        return;
    }
    int has_section = 0;
    for (size_t i = 0; i < len; i++) {
        if (items[i].section == section) {
            has_section = 1;
            break;
        }
    }
    if (!has_section) {
        return;
    }
    fprintf(f, "%s\n", data_section_directive(abi, section));
    for (size_t i = 0; i < len; i++) {
        const AsmDataItem *item = &items[i];
        if (item->section != section || !item->name || item->size == 0) {
            continue;
        }
        if (module_filter && item->is_global &&
            !asm_module_filter_has_global(module_filter, item->name)) {
            continue;
        }
        if (item->is_global) {
            fprintf(f, ".globl ");
            emit_symbol_name(f, abi, item->name, 1);
            fprintf(f, "\n");
        }
        emit_align_directive(f, abi, item->align);
        emit_symbol_name(f, abi, item->name, item->is_global);
        fprintf(f, ":\n");
        if (section == ASM_DATA_BSS || item->init_kind == ASM_DATA_INIT_NONE) {
            fprintf(f, "  .zero %zu\n", item->size);
            continue;
        }
        switch (item->init_kind) {
            case ASM_DATA_INIT_INT:
                emit_int_value(f, item->int_value, item->int_size ? item->int_size : item->size);
                break;
            case ASM_DATA_INIT_LABEL:
                emit_label_value(f, abi, item->label, item->label_needs_prefix, item->size);
                break;
            case ASM_DATA_INIT_BYTES:
                emit_bytes(f, item->bytes, item->bytes_len);
                break;
            default:
                fprintf(f, "  .zero %zu\n", item->size);
                break;
        }
    }
}

static void emit_data_sections(FILE *f,
                               AsmAbi abi,
                               const AsmLayout *layout,
                               const AsmModuleFilter *module_filter) {
    emit_data_section(f, abi, ASM_DATA_RODATA, layout, module_filter);
    emit_data_section(f, abi, ASM_DATA_DATA, layout, module_filter);
    emit_data_section(f, abi, ASM_DATA_BSS, layout, module_filter);
}

int cheng_emit_asm(const ChengNode *root,
                   const char *filename,
                   const char *out_path,
                   ChengDiagList *diags,
                   const ChengNode *module_stmts) {
    if (!root || !diags) {
        return -1;
    }
    if (!out_path || out_path[0] == '\0') {
        add_diag(diags, filename, root, "missing asm output path");
        return -1;
    }
    int hard_rt = asm_env_is_hard_rt();
    if (hard_rt) {
        if (hard_rt_check(root, filename, diags) != 0) {
            return -1;
        }
    }
    const char *target_env = getenv("CHENG_ASM_TARGET");
    if (!target_env || target_env[0] == '\0') {
        target_env = getenv("CHENG_TARGET");
    }
    const char *abi_env = getenv("CHENG_ASM_ABI");
    AsmTarget target = detect_target(target_env);
    if (target == ASM_TARGET_UNKNOWN) {
        add_diag(diags, filename, root, "unsupported asm target (set CHENG_ASM_TARGET)");
        return -1;
    }
    AsmAbi abi = detect_abi(target_env, abi_env);
    if (abi == ASM_ABI_UNKNOWN) {
        add_diag(diags, filename, root, "unsupported asm ABI (set CHENG_ASM_ABI)");
        return -1;
    }
    AsmLayoutConfig layout_cfg;
    asm_layout_config_default(&layout_cfg, 8);
    AsmLayout *layout = asm_layout_create(&layout_cfg);
    if (!layout) {
        add_diag(diags, filename, root, "failed to allocate asm layout");
        return -1;
    }
    if (asm_layout_collect(layout, root, filename, diags) != 0) {
        asm_layout_destroy(layout);
        return -1;
    }
    AsmModuleFilter *module_filter = NULL;
    if (module_stmts) {
        module_filter = asm_module_filter_create(module_stmts);
        if (!module_filter) {
            add_diag(diags, filename, root, "failed to allocate module filter");
            asm_layout_destroy(layout);
            return -1;
        }
    }
    const char *modsym_out = getenv("CHENG_ASM_MODSYM_OUT");
    if (modsym_out && modsym_out[0] != '\0') {
        const char *module_path = getenv("CHENG_ASM_MODULE");
        if (!module_filter) {
            add_diag(diags, filename, root, "module symbol output requires CHENG_ASM_MODULE");
            asm_module_filter_free(module_filter);
            asm_layout_destroy(layout);
            return -1;
        }
        if (asm_module_filter_write(module_filter, module_path, modsym_out, diags, filename, root) != 0) {
            asm_module_filter_free(module_filter);
            asm_layout_destroy(layout);
            return -1;
        }
    }
    const char *layout_out = getenv("CHENG_ASM_LAYOUT_OUT");
    if (layout_out && layout_out[0] != '\0') {
        FILE *layout_file = fopen(layout_out, "w");
        if (!layout_file) {
            add_diag(diags, filename, root, "failed to write asm layout output");
            asm_module_filter_free(module_filter);
            asm_layout_destroy(layout);
            return -1;
        }
        if (asm_layout_dump(layout, layout_file) != 0) {
            fclose(layout_file);
            add_diag(diags, filename, root, "failed to dump asm layout");
            asm_module_filter_free(module_filter);
            asm_layout_destroy(layout);
            return -1;
        }
        fclose(layout_file);
    }
    FILE *f = fopen(out_path, "w");
    if (!f) {
        add_diag(diags, filename, root, "failed to write asm output");
        asm_module_filter_free(module_filter);
        asm_layout_destroy(layout);
        return -1;
    }
    emit_data_sections(f, abi, layout, module_filter);
    int rc = 0;
    int report_emit_failure = 0;
    int opt_failed = 0;
    if (target == ASM_TARGET_X86_64 && abi == ASM_ABI_ELF) {
        rc = emit_x86_64_elf_file(f, root, filename, diags, layout, module_filter);
        report_emit_failure = 1;
    } else if (target == ASM_TARGET_X86_64 && abi == ASM_ABI_DARWIN) {
        rc = emit_x86_64_darwin_file(f, root, filename, diags, layout, module_filter);
        report_emit_failure = 1;
    } else if (target == ASM_TARGET_X86_64 && abi == ASM_ABI_COFF) {
        rc = emit_x86_64_coff_file(f, root, filename, diags, layout, module_filter);
        report_emit_failure = 1;
    } else if (target == ASM_TARGET_AARCH64 && abi == ASM_ABI_ELF) {
        rc = emit_a64_elf_file(f, root, filename, diags, layout, module_filter);
        report_emit_failure = 1;
    } else if (target == ASM_TARGET_AARCH64 && abi == ASM_ABI_DARWIN) {
        rc = emit_a64_darwin_file(f, root, filename, diags, layout, module_filter);
        report_emit_failure = 1;
    } else if (target == ASM_TARGET_RISCV64 && abi == ASM_ABI_ELF) {
        rc = emit_rv64_elf_file(f, root, filename, diags, layout, module_filter);
        report_emit_failure = 1;
    } else {
        const ChengNode *main_fn = find_main_fn(root);
        if (!main_fn) {
            add_diag(diags, filename, root, "asm backend requires a main function");
            rc = -1;
            goto cleanup;
        }
        if (main_fn->len < 4) {
            add_diag(diags, filename, main_fn, "invalid main declaration");
            rc = -1;
            goto cleanup;
        }
        const ChengNode *params = main_fn->kids[1];
        if (params && params->kind == NK_FORMAL_PARAMS && params->len > 0) {
            add_diag(diags, filename, params, "asm backend v0 only supports main without parameters");
            rc = -1;
            goto cleanup;
        }
        const ChengNode *ret_type = main_fn->kids[2];
        const ChengNode *body = main_fn->kids[3];
        const ChengNode *expr = extract_return_expr(body);
        if (!expr) {
            add_diag(diags, filename, body, "asm backend v0 only supports constant return expressions");
            rc = -1;
            goto cleanup;
        }
        ConstVal cval = {0};
        if (!eval_const_expr(expr, &cval, filename, diags)) {
            rc = -1;
            goto cleanup;
        }
        AsmRetKind kind = ret_kind_from_type(ret_type);
        if (kind == ASM_RET_UNKNOWN) {
            if (cval.is_bool) {
                kind = ASM_RET_BOOL;
            } else if (cval.value >= INT32_MIN && cval.value <= INT32_MAX) {
                kind = ASM_RET_I32;
            } else {
                kind = ASM_RET_I64;
            }
        }
        if (cval.is_bool && kind != ASM_RET_BOOL) {
            add_diag(diags, filename, expr, "return type expects int but expression is bool");
            rc = -1;
            goto cleanup;
        }
        if (!cval.is_bool && kind == ASM_RET_BOOL) {
            add_diag(diags, filename, expr, "return type expects bool but expression is int");
            rc = -1;
            goto cleanup;
        }
        rc = emit_main_asm(f, target, abi, kind, cval.value);
        report_emit_failure = 1;
    }
cleanup:
    fclose(f);
    if (rc == 0 && asm_opt_enabled()) {
        AsmOptStats opt_stats;
        asm_opt_stats_init(&opt_stats);
        if (asm_optimize_file(out_path, target, filename, diags, &opt_stats) != 0) {
            rc = -1;
            opt_failed = 1;
        }
    }
    asm_module_filter_free(module_filter);
    asm_layout_destroy(layout);
    if (rc != 0 && report_emit_failure && !opt_failed) {
        add_diag(diags, filename, root, "failed to emit asm for target");
        return -1;
    }
    return rc == 0 ? 0 : -1;
}
