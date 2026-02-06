#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm_emit.h"
#include "asm_emit_layout.h"
#include "asm_emit_runtime.h"
#include "semantics.h"

typedef enum {
    A64_TYPE_UNKNOWN = 0,
    A64_TYPE_I32,
    A64_TYPE_I64,
    A64_TYPE_BOOL
} A64TypeKind;

typedef enum {
    A64_INDEX_NONE = 0,
    A64_INDEX_ARRAY,
    A64_INDEX_SEQ,
    A64_INDEX_STRING
} A64IndexKind;

typedef struct {
    const char *name;
    A64TypeKind type;
    const ChengNode *type_expr;
    size_t index;
} A64Var;

typedef struct {
    A64Var *items;
    size_t len;
    size_t cap;
} A64VarTable;

typedef struct {
    char *name;
    const ChengNode *type_expr;
} A64GlobalType;

typedef struct {
    A64GlobalType *items;
    size_t len;
    size_t cap;
} A64GlobalTypeTable;

typedef struct {
    int call_area_size;
    int temp_area_size;
    int arg_slots_size;
    int locals_size;
    int frame_size;
    int call_area_offset;
    int temp_area_offset;
    int arg_slots_offset;
    int locals_offset;
} A64FrameLayout;

typedef struct {
    FILE *out;
    const char *filename;
    ChengDiagList *diags;
    const char *fn_name;
    const ChengNode *ret_type_expr;
    const ChengNode *root;
    const AsmModuleFilter *module_filter;
    char *module_prefix;
    int ret_is_result;
    int mangle_underscore;
    int is_darwin;
    AsmLayout *layout;
    const A64GlobalTypeTable *globals;
    A64VarTable vars;
    A64FrameLayout frame;
    int temp_top;
    int temp_slots;
    int arg_slots;
    int label_id;
    int ret_label;
    int has_error;
    int loop_depth;
    int loop_cap;
    int *loop_break;
    int *loop_continue;
} A64EmitContext;

typedef struct {
    const ChengNode *decl;
    const char *name;
    int emit;
    int scanned;
} A64FuncEntry;

typedef struct {
    A64FuncEntry *items;
    size_t len;
    size_t cap;
} A64FuncTable;

static const ChengNode *a64_var_type_expr_from_ident(const A64EmitContext *ctx, const ChengNode *ident);

static void a64_add_diag(ChengDiagList *diags, const char *filename, const ChengNode *node, const char *msg) {
    int line = 1;
    int col = 1;
    if (node) {
        line = node->line;
        col = node->col;
    }
    cheng_diag_add(diags, CHENG_SV_ERROR, filename, line, col, msg);
}

static char *a64_sanitize_ident(const char *name) {
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

static char *a64_sanitize_key(const char *s) {
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

static uint64_t a64_fnv1a64(const char *s) {
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

static const char *a64_path_basename(const char *path) {
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

static char *a64_module_prefix_from_path(const char *path) {
    if (!path) {
        return strdup("mod");
    }
    const char *base = a64_path_basename(path);
    char *base_key = a64_sanitize_key(base);
    if (!base_key) {
        return strdup("mod");
    }
    uint64_t h = a64_fnv1a64(path);
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
    char *san = a64_sanitize_ident(raw);
    free(raw);
    return san ? san : NULL;
}

static char *a64_module_prefix_from_env(const AsmModuleFilter *module_filter) {
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
    return a64_module_prefix_from_path(module_path);
}

static int a64_should_prefix_symbol(const A64EmitContext *ctx, const char *name) {
    if (!ctx || !name || !ctx->module_prefix || !ctx->module_filter) {
        return 0;
    }
    if (asm_module_filter_has_func(ctx->module_filter, name)) {
        return 1;
    }
    if (asm_module_filter_has_global(ctx->module_filter, name)) {
        return 1;
    }
    return 0;
}

static const char *a64_mangle_symbol(const A64EmitContext *ctx,
                                     const char *name,
                                     char *buf,
                                     size_t buf_len) {
    if (!name) {
        name = "";
    }
    const char *base = name;
    if (strcmp(base, "[]") == 0) {
        base = "bracket";
    } else if (strcmp(base, "[]=") == 0) {
        base = "bracketEq";
    } else if (strcmp(base, "..") == 0) {
        base = "dotdot";
    } else if (strcmp(base, "..<") == 0) {
        base = "dotdotless";
    } else if (strcmp(base, "=>") == 0) {
        base = "arrow";
    } else if (strcmp(base, ".") == 0) {
        base = "dot";
    } else if (base[0] == '\0') {
        base = "empty";
    }
    int need_prefix = a64_should_prefix_symbol(ctx, name);
    int need_underscore = ctx && ctx->mangle_underscore;
    if (!need_prefix && !need_underscore) {
        return base;
    }
    if (!buf || buf_len == 0) {
        return base;
    }
    if (need_prefix) {
        int n;
        if (need_underscore) {
            n = snprintf(buf, buf_len, "_%s__%s", ctx->module_prefix, base);
        } else {
            n = snprintf(buf, buf_len, "%s__%s", ctx->module_prefix, base);
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

static const ChengNode *a64_strip_paren_expr(const ChengNode *node) {
    const ChengNode *cur = node;
    while (cur && cur->kind == NK_PAR && cur->len > 0) {
        cur = cur->kids[0];
    }
    return cur;
}

static const ChengNode *a64_get_top_level_list(const ChengNode *root) {
    if (!root) {
        return NULL;
    }
    if (root->kind == NK_MODULE && root->len > 0) {
        return root->kids[0];
    }
    return root;
}

static const ChengNode *a64_find_fn_decl(const ChengNode *root, const char *name) {
    if (!root || !name || name[0] == '\0') {
        return NULL;
    }
    const ChengNode *list = a64_get_top_level_list(root);
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

static void a64_format_for_index_name(const ChengNode *for_node, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return;
    }
    int line = for_node ? for_node->line : 0;
    int col = for_node ? for_node->col : 0;
    snprintf(buf, buf_len, "__for_idx_%d_%d", line, col);
}

static void a64_format_case_tmp_name(const ChengNode *case_node, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return;
    }
    int line = case_node ? case_node->line : 0;
    int col = case_node ? case_node->col : 0;
    snprintf(buf, buf_len, "__case_sel_%d_%d", line, col);
}

static const char *a64_case_pattern_ident(const ChengNode *pattern) {
    const ChengNode *node = a64_strip_paren_expr(pattern);
    if (node && node->kind == NK_IDENT && node->ident) {
        return node->ident;
    }
    return NULL;
}

static int a64_case_pattern_is_wildcard(const char *name) {
    return name && strcmp(name, "_") == 0;
}

static int a64_case_pattern_const(const ChengNode *pattern) {
    const ChengNode *node = a64_strip_paren_expr(pattern);
    if (!node) {
        return 0;
    }
    if (node->kind == NK_INT_LIT || node->kind == NK_BOOL_LIT ||
        node->kind == NK_NIL_LIT || node->kind == NK_CHAR_LIT) {
        return 1;
    }
    if (node->kind == NK_PREFIX && node->len > 1 && node->kids[0] && node->kids[0]->ident &&
        node->kids[1] && node->kids[1]->kind == NK_INT_LIT) {
        if (strcmp(node->kids[0]->ident, "-") == 0 || strcmp(node->kids[0]->ident, "+") == 0) {
            return 1;
        }
    }
    return 0;
}

static const ChengNode *a64_strip_type_wrappers(const ChengNode *type_expr, int *out_is_ptr) {
    const ChengNode *cur = a64_strip_paren_expr(type_expr);
    int is_ptr = 0;
    while (cur && (cur->kind == NK_PTR_TY || cur->kind == NK_REF_TY || cur->kind == NK_VAR_TY)) {
        is_ptr = 1;
        if (cur->len == 0) {
            break;
        }
        cur = a64_strip_paren_expr(cur->kids[0]);
    }
    if (out_is_ptr) {
        *out_is_ptr = is_ptr;
    }
    return cur;
}

static const char *a64_type_expr_bracket_base_ident(const ChengNode *type_expr) {
    const ChengNode *cur = a64_strip_type_wrappers(type_expr, NULL);
    if (!cur || cur->kind != NK_BRACKET_EXPR || cur->len == 0) {
        return NULL;
    }
    const ChengNode *base = cur->kids[0];
    if (base && base->kind == NK_IDENT && base->ident) {
        return base->ident;
    }
    return NULL;
}

static const ChengNode *a64_type_expr_bracket_arg(const ChengNode *type_expr, size_t index) {
    const ChengNode *cur = a64_strip_type_wrappers(type_expr, NULL);
    if (!cur || cur->kind != NK_BRACKET_EXPR || cur->len <= index) {
        return NULL;
    }
    return cur->kids[index];
}

static int a64_type_expr_is_seq(const ChengNode *type_expr) {
    const ChengNode *cur = a64_strip_type_wrappers(type_expr, NULL);
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

static int a64_type_expr_is_string(const ChengNode *type_expr, int *out_is_cstring) {
    const ChengNode *cur = a64_strip_type_wrappers(type_expr, NULL);
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

static int a64_type_name_is_result(const char *name) {
    if (!name) {
        return 0;
    }
    if (strcmp(name, "Result") == 0) {
        return 1;
    }
    return strncmp(name, "Result_", 7) == 0;
}

static int a64_type_expr_is_result(const ChengNode *type_expr) {
    const ChengNode *cur = a64_strip_type_wrappers(type_expr, NULL);
    if (!cur) {
        return 0;
    }
    if (cur->kind == NK_IDENT && cur->ident) {
        return a64_type_name_is_result(cur->ident);
    }
    if (cur->kind == NK_BRACKET_EXPR && cur->len > 0) {
        const ChengNode *base = cur->kids[0];
        if (base && base->kind == NK_IDENT && base->ident) {
            return strcmp(base->ident, "Result") == 0;
        }
    }
    return 0;
}

static const char *a64_type_name_from_type_expr(const ChengNode *type_expr) {
    const ChengNode *cur = a64_strip_type_wrappers(type_expr, NULL);
    while (cur && cur->kind == NK_DOT_EXPR && cur->len > 1) {
        cur = cur->kids[1];
    }
    if (!cur || cur->kind != NK_IDENT || !cur->ident) {
        return NULL;
    }
    return cur->ident;
}

static int a64_type_expr_is_indirect(const A64EmitContext *ctx, const ChengNode *type_expr) {
    if (!ctx || !ctx->layout || !type_expr) {
        return 0;
    }
    size_t size = 0;
    size_t align = 0;
    if (!asm_layout_type_info(ctx->layout, type_expr, &size, &align)) {
        return 0;
    }
    return size > 8;
}

static int a64_str_eq_ci(const char *a, const char *b) {
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

static int a64_orc_enabled(void) {
    const char *mm = getenv("CHENG_MM");
    if (!mm || mm[0] == '\0') {
        const char *arc = getenv("CHENG_ARC");
        if (!arc || arc[0] == '\0') {
            return 1;
        }
        return !a64_str_eq_ci(arc, "0");
    }
    if (a64_str_eq_ci(mm, "off") || a64_str_eq_ci(mm, "none") || strcmp(mm, "0") == 0) {
        return 0;
    }
    return 1;
}

static int a64_orc_expr_borrowed(const ChengNode *expr) {
    const ChengNode *cur = a64_strip_paren_expr(expr);
    if (!cur) {
        return 0;
    }
    if (cur->kind == NK_CALL_ARG) {
        if (cur->len > 1) {
            return a64_orc_expr_borrowed(cur->kids[1]);
        }
        if (cur->len > 0) {
            return a64_orc_expr_borrowed(cur->kids[0]);
        }
        return 0;
    }
    if (cur->kind == NK_PAR && cur->len == 1) {
        return a64_orc_expr_borrowed(cur->kids[0]);
    }
    if (cur->kind == NK_CAST) {
        if (cur->len > 1) {
            return a64_orc_expr_borrowed(cur->kids[1]);
        }
        if (cur->len > 0) {
            return a64_orc_expr_borrowed(cur->kids[0]);
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

static int a64_orc_name_has_prefix(const char *name, const char *prefix) {
    if (!name || !prefix) {
        return 0;
    }
    size_t plen = strlen(prefix);
    return strncmp(name, prefix, plen) == 0;
}

static int a64_orc_type_is_managed(const ChengNode *type_expr) {
    if (!type_expr) {
        return 0;
    }
    int is_cstring = 0;
    if (a64_type_expr_is_string(type_expr, &is_cstring)) {
        return !is_cstring;
    }
    const char *name = a64_type_name_from_type_expr(type_expr);
    if (name) {
        if (a64_orc_name_has_prefix(name, "str_fixed") ||
            a64_orc_name_has_prefix(name, "seq_fixed") ||
            a64_orc_name_has_prefix(name, "Table_fixed") ||
            a64_orc_name_has_prefix(name, "table_fixed")) {
            return 0;
        }
        if (a64_orc_name_has_prefix(name, "seq_") ||
            a64_orc_name_has_prefix(name, "Table_") ||
            a64_orc_name_has_prefix(name, "table_")) {
            return 1;
        }
    }
    const char *base = a64_type_expr_bracket_base_ident(type_expr);
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

static int a64_hex_digit_val(int c) {
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

static int a64_decode_char_lit(const char *text, int *out) {
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
        int high = a64_hex_digit_val(text[2]);
        int low = a64_hex_digit_val(text[3]);
        if (high >= 0 && low >= 0) {
            *out = (high * 16) + low;
            return 1;
        }
    }
    if (len > 0) {
        *out = (unsigned char)text[0];
    }
    return 0;
}

static const ChengNode *a64_call_target_node(const ChengNode *callee) {
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

static const ChengNode *a64_call_arg_value(const ChengNode *arg, int *out_named) {
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

static const ChengNode *a64_call_return_type_expr(A64EmitContext *ctx, const ChengNode *call) {
    if (!ctx || !call || call->kind != NK_CALL) {
        return NULL;
    }
    if (!ctx->root) {
        return NULL;
    }
    if (call->len == 0 || !call->kids[0]) {
        return NULL;
    }
    const ChengNode *callee = a64_call_target_node(call->kids[0]);
    if (!callee || !(callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) || !callee->ident) {
        return NULL;
    }
    const ChengNode *decl = a64_find_fn_decl(ctx->root, callee->ident);
    if (!decl || decl->kind != NK_FN_DECL || decl->len < 3) {
        return NULL;
    }
    return decl->kids[2];
}

static const ChengNode *a64_expr_type_expr(A64EmitContext *ctx, const ChengNode *expr) {
    const ChengNode *base = a64_strip_paren_expr(expr);
    if (!base) {
        return NULL;
    }
    if (base->kind == NK_IDENT) {
        return a64_var_type_expr_from_ident(ctx, base);
    }
    if (base->kind == NK_CALL) {
        return a64_call_return_type_expr(ctx, base);
    }
    return NULL;
}

static const char *a64_result_type_name_from_expr(A64EmitContext *ctx, const ChengNode *expr) {
    const ChengNode *type_expr = a64_expr_type_expr(ctx, expr);
    if (!type_expr) {
        return NULL;
    }
    return a64_type_name_from_type_expr(type_expr);
}

static A64TypeKind a64_type_from_node(const ChengNode *node) {
    if (!node) {
        return A64_TYPE_UNKNOWN;
    }
    if (node->kind == NK_IDENT && node->ident) {
        if (strcmp(node->ident, "int32") == 0) {
            return A64_TYPE_I32;
        }
        if (strcmp(node->ident, "int64") == 0) {
            return A64_TYPE_I64;
        }
        if (strcmp(node->ident, "bool") == 0) {
            return A64_TYPE_BOOL;
        }
        if (strcmp(node->ident, "char") == 0) {
            return A64_TYPE_I32;
        }
    }
    return A64_TYPE_UNKNOWN;
}

static int a64_is_type_call_target(const A64EmitContext *ctx, const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_PTR_TY || node->kind == NK_REF_TY || node->kind == NK_VAR_TY ||
        node->kind == NK_FN_TY || node->kind == NK_SET_TY || node->kind == NK_TUPLE_TY) {
        return 1;
    }
    if (!ctx || !ctx->layout) {
        return 0;
    }
    size_t size = 0;
    size_t align = 0;
    if (asm_layout_type_info(ctx->layout, node, &size, &align)) {
        return 1;
    }
    return 0;
}

static int a64_is_expr_kind(int kind) {
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
        case NK_POSTFIX:
        case NK_ASGN:
        case NK_PAR:
            return 1;
        default:
            return 0;
    }
}

static void a64_vars_init(A64VarTable *vars) {
    vars->items = NULL;
    vars->len = 0;
    vars->cap = 0;
}

static void a64_global_table_init(A64GlobalTypeTable *table) {
    if (!table) {
        return;
    }
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static void a64_global_table_free(A64GlobalTypeTable *table) {
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

static int a64_global_table_reserve(A64GlobalTypeTable *table, size_t extra) {
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
    A64GlobalType *next_items = (A64GlobalType *)realloc(table->items, next_cap * sizeof(A64GlobalType));
    if (!next_items) {
        return -1;
    }
    table->items = next_items;
    table->cap = next_cap;
    return 0;
}

static A64GlobalType *a64_global_table_find(A64GlobalTypeTable *table, const char *name) {
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

static int a64_global_table_add(A64GlobalTypeTable *table, const char *name, const ChengNode *type_expr) {
    if (!table || !name) {
        return -1;
    }
    A64GlobalType *existing = a64_global_table_find(table, name);
    if (existing) {
        if (!existing->type_expr && type_expr && type_expr->kind != NK_EMPTY) {
            existing->type_expr = type_expr;
        }
        return 0;
    }
    if (a64_global_table_reserve(table, 1) != 0) {
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

static void a64_func_table_init(A64FuncTable *table) {
    if (!table) {
        return;
    }
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static void a64_func_table_free(A64FuncTable *table) {
    if (!table) {
        return;
    }
    free(table->items);
    table->items = NULL;
    table->len = 0;
    table->cap = 0;
}

static int a64_func_table_reserve(A64FuncTable *table, size_t extra) {
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
    A64FuncEntry *next_items = (A64FuncEntry *)realloc(table->items, next_cap * sizeof(A64FuncEntry));
    if (!next_items) {
        return -1;
    }
    table->items = next_items;
    table->cap = next_cap;
    return 0;
}

static A64FuncEntry *a64_func_table_find(A64FuncTable *table, const char *name) {
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

static int a64_func_table_add(A64FuncTable *table, const ChengNode *decl) {
    if (!table || !decl || decl->kind != NK_FN_DECL || decl->len < 1) {
        return -1;
    }
    const ChengNode *name_node = decl->kids[0];
    if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
        return -1;
    }
    A64FuncEntry *entry = a64_func_table_find(table, name_node->ident);
    if (entry) {
        entry->decl = decl;
        entry->name = name_node->ident;
        entry->emit = 0;
        entry->scanned = 0;
        return 0;
    }
    if (a64_func_table_reserve(table, 1) != 0) {
        return -1;
    }
    table->items[table->len].decl = decl;
    table->items[table->len].name = name_node->ident;
    table->items[table->len].emit = 0;
    table->items[table->len].scanned = 0;
    table->len++;
    return 0;
}

static void a64_vars_free(A64VarTable *vars) {
    free(vars->items);
    vars->items = NULL;
    vars->len = 0;
    vars->cap = 0;
}

static A64Var *a64_vars_find(A64VarTable *vars, const char *name) {
    if (!vars || !name) {
        return NULL;
    }
    for (size_t index = 0; index < vars->len; index++) {
        if (vars->items[index].name && strcmp(vars->items[index].name, name) == 0) {
            return &vars->items[index];
        }
    }
    return NULL;
}

static const ChengNode *a64_pattern_ident_node(const ChengNode *pattern) {
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

static int a64_collect_global_types_from_stmt(const ChengNode *stmt,
                                              A64GlobalTypeTable *globals,
                                              const char *filename,
                                              ChengDiagList *diags);

static int a64_collect_global_types(const ChengNode *list,
                                    A64GlobalTypeTable *globals,
                                    const char *filename,
                                    ChengDiagList *diags) {
    if (!list || !globals) {
        return 0;
    }
    if (list->kind != NK_STMT_LIST) {
        return a64_collect_global_types_from_stmt(list, globals, filename, diags);
    }
    for (size_t index = 0; index < list->len; index++) {
        if (a64_collect_global_types_from_stmt(list->kids[index], globals, filename, diags) != 0) {
            return -1;
        }
    }
    return 0;
}

static int a64_collect_global_types_from_stmt(const ChengNode *stmt,
                                              A64GlobalTypeTable *globals,
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
            const ChengNode *ident = a64_pattern_ident_node(pattern);
            if (!ident || !ident->ident) {
                return 0;
            }
            const ChengNode *type_expr = stmt->len > 1 ? stmt->kids[1] : NULL;
            if (a64_global_table_add(globals, ident->ident, type_expr) != 0) {
                a64_add_diag(diags, filename, stmt, "failed to collect global type");
                return -1;
            }
            return 0;
        }
        default:
            return 0;
    }
}

static const ChengNode *a64_var_type_expr_from_ident(const A64EmitContext *ctx, const ChengNode *ident) {
    if (!ctx || !ident || ident->kind != NK_IDENT || !ident->ident) {
        return NULL;
    }
    A64Var *entry = a64_vars_find((A64VarTable *)&ctx->vars, ident->ident);
    if (!entry) {
        if (ctx->globals) {
            A64GlobalType *global = a64_global_table_find((A64GlobalTypeTable *)ctx->globals, ident->ident);
            if (global) {
                return global->type_expr;
            }
        }
        return NULL;
    }
    return entry->type_expr;
}

static int a64_expr_is_string_like(A64EmitContext *ctx, const ChengNode *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->type_id == CHENG_TYPE_STR || expr->type_id == CHENG_TYPE_CSTRING) {
        return 1;
    }
    const ChengNode *base = a64_strip_paren_expr(expr);
    if (base && base->kind == NK_IDENT) {
        return a64_type_expr_is_string(a64_var_type_expr_from_ident(ctx, base), NULL);
    }
    return 0;
}

static int a64_expr_is_seq_like(A64EmitContext *ctx, const ChengNode *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->type_id == CHENG_TYPE_SEQ) {
        return 1;
    }
    const ChengNode *base = a64_strip_paren_expr(expr);
    if (base && base->kind == NK_IDENT) {
        const ChengNode *type_expr = a64_var_type_expr_from_ident(ctx, base);
        if (a64_type_expr_is_seq(type_expr)) {
            return 1;
        }
        const char *type_name = a64_type_name_from_type_expr(type_expr);
        if (type_name && strncmp(type_name, "seq_", 4) == 0) {
            return 1;
        }
    }
    return 0;
}

static A64IndexKind a64_index_kind_for_expr(A64EmitContext *ctx,
                                            const ChengNode *base,
                                            const ChengNode **out_elem_type) {
    if (out_elem_type) {
        *out_elem_type = NULL;
    }
    if (!ctx || !base) {
        return A64_INDEX_NONE;
    }
    if (a64_expr_is_string_like(ctx, base)) {
        return A64_INDEX_STRING;
    }
    const ChengNode *probe = a64_strip_paren_expr(base);
    const ChengNode *type_expr = NULL;
    if (probe && probe->kind == NK_IDENT) {
        type_expr = a64_var_type_expr_from_ident(ctx, probe);
    }
    const char *base_name = a64_type_expr_bracket_base_ident(type_expr);
    if (base_name) {
        if (strcmp(base_name, "array") == 0) {
            if (out_elem_type) {
                *out_elem_type = a64_type_expr_bracket_arg(type_expr, 1);
            }
            return A64_INDEX_ARRAY;
        }
        if (strcmp(base_name, "seq") == 0 ||
            strcmp(base_name, "seqT") == 0 ||
            strcmp(base_name, "seq_fixed") == 0) {
            if (out_elem_type) {
                *out_elem_type = a64_type_expr_bracket_arg(type_expr, 1);
            }
            return A64_INDEX_SEQ;
        }
    }
    if (a64_expr_is_seq_like(ctx, base)) {
        return A64_INDEX_SEQ;
    }
    return A64_INDEX_NONE;
}

static size_t a64_elem_size_for_type(A64EmitContext *ctx, const ChengNode *type_expr) {
    size_t size = 8;
    size_t align = 8;
    if (!ctx || !ctx->layout || !type_expr) {
        return size;
    }
    if (asm_layout_type_info(ctx->layout, type_expr, &size, &align) && size > 0) {
        return size;
    }
    return 8;
}

static const char *a64_expr_type_name(A64EmitContext *ctx, const ChengNode *expr) {
    const ChengNode *base = a64_strip_paren_expr(expr);
    if (base && base->kind == NK_IDENT) {
        return a64_type_name_from_type_expr(a64_var_type_expr_from_ident(ctx, base));
    }
    return NULL;
}

static int a64_vars_add(A64VarTable *vars,
                        const char *name,
                        A64TypeKind type,
                        const ChengNode *type_expr) {
    if (!vars || !name) {
        return 0;
    }
    if (vars->len >= vars->cap) {
        size_t next_cap = vars->cap == 0 ? 8 : vars->cap * 2;
        A64Var *next_items = (A64Var *)realloc(vars->items, next_cap * sizeof(A64Var));
        if (!next_items) {
            return 0;
        }
        vars->items = next_items;
        vars->cap = next_cap;
    }
    vars->items[vars->len].name = name;
    vars->items[vars->len].type = type;
    vars->items[vars->len].type_expr = type_expr;
    vars->items[vars->len].index = vars->len;
    vars->len++;
    return 1;
}

static int a64_align16(int value) {
    return (value + 15) & ~15;
}

static A64FrameLayout a64_make_layout(int var_count, int temp_slots, int max_call_args) {
    A64FrameLayout layout;
    int call_area_size = 0;
    if (max_call_args > 8) {
        call_area_size = (max_call_args - 8) * 8;
    }
    int temp_area_size = temp_slots * 8;
    int arg_slots_size = max_call_args * 8;
    int locals_size = var_count * 8;
    int call_area_offset = 0;
    int temp_area_offset = call_area_offset + call_area_size;
    int arg_slots_offset = temp_area_offset + temp_area_size;
    int locals_offset = arg_slots_offset + arg_slots_size;
    int frame_size = a64_align16(locals_offset + locals_size);
    layout.call_area_size = call_area_size;
    layout.temp_area_size = temp_area_size;
    layout.arg_slots_size = arg_slots_size;
    layout.locals_size = locals_size;
    layout.frame_size = frame_size;
    layout.call_area_offset = call_area_offset;
    layout.temp_area_offset = temp_area_offset;
    layout.arg_slots_offset = arg_slots_offset;
    layout.locals_offset = locals_offset;
    return layout;
}

static int a64_expr_temp_depth(const ChengNode *expr) {
    if (!expr) {
        return 0;
    }
    if (expr->kind == NK_INFIX) {
        if (expr->len < 3) {
            return 0;
        }
        int left_depth = a64_expr_temp_depth(expr->kids[1]);
        int right_depth = a64_expr_temp_depth(expr->kids[2]);
        int use_depth = left_depth + 1;
        return use_depth > right_depth ? use_depth : right_depth;
    }
    if (expr->kind == NK_BRACKET_EXPR) {
        if (expr->len < 2) {
            return 0;
        }
        int base_depth = a64_expr_temp_depth(expr->kids[0]);
        int index_depth = a64_expr_temp_depth(expr->kids[1]);
        int use_depth = base_depth + 1;
        return use_depth > index_depth ? use_depth : index_depth;
    }
    if (expr->kind == NK_PREFIX) {
        if (expr->len > 1) {
            return a64_expr_temp_depth(expr->kids[1]);
        }
        return 0;
    }
    if (expr->kind == NK_PAR) {
        if (expr->len > 0) {
            return a64_expr_temp_depth(expr->kids[0]);
        }
        return 0;
    }
    if (expr->kind == NK_CAST) {
        if (expr->len > 1) {
            return a64_expr_temp_depth(expr->kids[1]);
        }
        if (expr->len > 0) {
            return a64_expr_temp_depth(expr->kids[0]);
        }
        return 0;
    }
    if (expr->kind == NK_CALL) {
        int max_depth = 0;
        for (size_t index = 1; index < expr->len; index++) {
            int depth = a64_expr_temp_depth(expr->kids[index]);
            if (depth > max_depth) {
                max_depth = depth;
            }
        }
        return max_depth;
    }
    if (expr->kind == NK_ASGN) {
        if (expr->len < 2) {
            return 0;
        }
        int lhs_depth = a64_expr_temp_depth(expr->kids[0]);
        int rhs_depth = a64_expr_temp_depth(expr->kids[1]);
        int depth = rhs_depth;
        int lhs_total = lhs_depth + 1;
        return lhs_total > depth ? lhs_total : depth;
    }
    int max_depth = 0;
    for (size_t index = 0; index < expr->len; index++) {
        int depth = a64_expr_temp_depth(expr->kids[index]);
        if (depth > max_depth) {
            max_depth = depth;
        }
    }
    return max_depth;
}

static int a64_stmt_max_temp(const ChengNode *stmt) {
    if (!stmt) {
        return 0;
    }
    switch (stmt->kind) {
        case NK_STMT_LIST: {
            int max_depth = 0;
            for (size_t index = 0; index < stmt->len; index++) {
                int depth = a64_stmt_max_temp(stmt->kids[index]);
                if (depth > max_depth) {
                    max_depth = depth;
                }
            }
            return max_depth;
        }
        case NK_RETURN:
            if (stmt->len > 0) {
                return a64_expr_temp_depth(stmt->kids[0]);
            }
            return 0;
        case NK_LET:
        case NK_VAR:
        case NK_CONST:
            if (stmt->len > 2) {
                return a64_expr_temp_depth(stmt->kids[2]);
            }
            return 0;
        case NK_ASGN:
            if (stmt->len > 1) {
                int rhs_depth = a64_expr_temp_depth(stmt->kids[1]);
                int lhs_depth = 0;
                if (stmt->len > 0) {
                    lhs_depth = a64_expr_temp_depth(stmt->kids[0]);
                }
                int depth = rhs_depth;
                int lhs_total = lhs_depth + 1;
                return lhs_total > depth ? lhs_total : depth;
            }
            return 0;
        case NK_IF: {
            int max_depth = 0;
            if (stmt->len > 0) {
                max_depth = a64_expr_temp_depth(stmt->kids[0]);
            }
            if (stmt->len > 1) {
                int body_depth = a64_stmt_max_temp(stmt->kids[1]);
                if (body_depth > max_depth) {
                    max_depth = body_depth;
                }
            }
            if (stmt->len > 2 && stmt->kids[2]) {
                const ChengNode *else_node = stmt->kids[2];
                if (else_node->kind == NK_ELSE && else_node->len > 0) {
                    int else_depth = a64_stmt_max_temp(else_node->kids[0]);
                    if (else_depth > max_depth) {
                        max_depth = else_depth;
                    }
                } else {
                    int else_depth = a64_stmt_max_temp(else_node);
                    if (else_depth > max_depth) {
                        max_depth = else_depth;
                    }
                }
            }
            return max_depth;
        }
        case NK_WHILE: {
            int max_depth = 0;
            if (stmt->len > 0) {
                max_depth = a64_expr_temp_depth(stmt->kids[0]);
            }
            if (stmt->len > 1) {
                int body_depth = a64_stmt_max_temp(stmt->kids[1]);
                if (body_depth > max_depth) {
                    max_depth = body_depth;
                }
            }
            return max_depth;
        }
        case NK_FOR: {
            int max_depth = 0;
            if (stmt->len > 1) {
                int iter_depth = a64_expr_temp_depth(stmt->kids[1]);
                if (iter_depth > max_depth) {
                    max_depth = iter_depth;
                }
            }
            if (stmt->len > 2) {
                int body_depth = a64_stmt_max_temp(stmt->kids[2]);
                if (body_depth > max_depth) {
                    max_depth = body_depth;
                }
            }
            return max_depth;
        }
        case NK_CASE: {
            int max_depth = 0;
            if (stmt->len > 0) {
                max_depth = a64_expr_temp_depth(stmt->kids[0]);
            }
            for (size_t index = 1; index < stmt->len; index++) {
                const ChengNode *branch = stmt->kids[index];
                if (!branch) {
                    continue;
                }
                if (branch->kind == NK_OF_BRANCH) {
                    if (branch->len > 0) {
                        const ChengNode *pattern = branch->kids[0];
                        if (pattern) {
                            if (pattern->kind == NK_PATTERN) {
                                for (size_t p = 0; p < pattern->len; p++) {
                                    int depth = a64_expr_temp_depth(pattern->kids[p]);
                                    if (depth > max_depth) {
                                        max_depth = depth;
                                    }
                                }
                            } else {
                                int depth = a64_expr_temp_depth(pattern);
                                if (depth > max_depth) {
                                    max_depth = depth;
                                }
                            }
                        }
                    }
                    if (branch->len > 1) {
                        int body_depth = a64_stmt_max_temp(branch->kids[1]);
                        if (body_depth > max_depth) {
                            max_depth = body_depth;
                        }
                    }
                    continue;
                }
                if (branch->kind == NK_ELSE) {
                    if (branch->len > 0) {
                        int body_depth = a64_stmt_max_temp(branch->kids[0]);
                        if (body_depth > max_depth) {
                            max_depth = body_depth;
                        }
                    }
                    continue;
                }
                {
                    int branch_depth = a64_stmt_max_temp(branch);
                    if (branch_depth > max_depth) {
                        max_depth = branch_depth;
                    }
                }
            }
            return max_depth;
        }
        default:
            if (a64_is_expr_kind(stmt->kind)) {
                return a64_expr_temp_depth(stmt);
            }
            return 0;
    }
}

static int a64_max_call_args(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    int max_count = 0;
    if (node->kind == NK_CALL) {
        int count = 0;
        if (node->len > 0) {
            count = (int)node->len - 1;
        }
        if (count > max_count) {
            max_count = count;
        }
    }
    for (size_t index = 0; index < node->len; index++) {
        int child_max = a64_max_call_args(node->kids[index]);
        if (child_max > max_count) {
            max_count = child_max;
        }
    }
    return max_count;
}

static void a64_collect_case_bindings(const ChengNode *pattern,
                                      A64VarTable *vars,
                                      const char *filename,
                                      ChengDiagList *diags) {
    if (!pattern || !vars) {
        return;
    }
    if (pattern->kind == NK_PATTERN) {
        for (size_t i = 0; i < pattern->len; i++) {
            a64_collect_case_bindings(pattern->kids[i], vars, filename, diags);
        }
        return;
    }
    const char *name = a64_case_pattern_ident(pattern);
    if (!name || a64_case_pattern_is_wildcard(name)) {
        return;
    }
    if (a64_vars_find(vars, name)) {
        return;
    }
    if (!a64_vars_add(vars, name, A64_TYPE_I64, NULL)) {
        a64_add_diag(diags, filename, pattern, "failed to allocate case binding");
    }
}

static void a64_collect_locals_stmt(const ChengNode *stmt,
                                    A64VarTable *vars,
                                    const char *filename,
                                    ChengDiagList *diags) {
    if (!stmt) {
        return;
    }
    switch (stmt->kind) {
        case NK_STMT_LIST:
            for (size_t index = 0; index < stmt->len; index++) {
                a64_collect_locals_stmt(stmt->kids[index], vars, filename, diags);
            }
            return;
        case NK_LET:
        case NK_VAR:
        case NK_CONST: {
            const ChengNode *pattern = stmt->len > 0 ? stmt->kids[0] : NULL;
            const ChengNode *type_node = stmt->len > 1 ? stmt->kids[1] : NULL;
            if (!pattern || pattern->kind != NK_PATTERN || pattern->len == 0) {
                a64_add_diag(diags, filename, stmt, "asm a64 backend expects simple binding");
                return;
            }
            if (pattern->len != 1 || !pattern->kids[0] || pattern->kids[0]->kind != NK_IDENT ||
                !pattern->kids[0]->ident) {
                a64_add_diag(diags, filename, stmt, "asm a64 backend expects a single identifier binding");
                return;
            }
            const char *name = pattern->kids[0]->ident;
            if (a64_vars_find(vars, name)) {
                a64_add_diag(diags, filename, stmt, "duplicate variable in asm a64 backend");
                return;
            }
            A64TypeKind type = a64_type_from_node(type_node);
            if (!a64_vars_add(vars, name, type, type_node)) {
                a64_add_diag(diags, filename, stmt, "failed to allocate variable slot");
            }
            return;
        }
        case NK_IF:
            if (stmt->len > 1) {
                a64_collect_locals_stmt(stmt->kids[1], vars, filename, diags);
            }
            if (stmt->len > 2 && stmt->kids[2]) {
                const ChengNode *else_node = stmt->kids[2];
                if (else_node->kind == NK_ELSE && else_node->len > 0) {
                    a64_collect_locals_stmt(else_node->kids[0], vars, filename, diags);
                } else {
                    a64_collect_locals_stmt(else_node, vars, filename, diags);
                }
            }
            return;
        case NK_WHILE:
            if (stmt->len > 1) {
                a64_collect_locals_stmt(stmt->kids[1], vars, filename, diags);
            }
            return;
        case NK_FOR: {
            if (stmt->len < 3) {
                a64_add_diag(diags, filename, stmt, "invalid for statement");
                return;
            }
            const ChengNode *pattern = stmt->kids[0];
            if (!pattern || pattern->kind != NK_PATTERN || pattern->len == 0) {
                a64_add_diag(diags, filename, stmt, "asm a64 backend expects simple for binding");
                return;
            }
            if (pattern->len != 1 || !pattern->kids[0] || pattern->kids[0]->kind != NK_IDENT ||
                !pattern->kids[0]->ident) {
                a64_add_diag(diags, filename, stmt, "asm a64 backend expects identifier for binding");
                return;
            }
            const char *name = pattern->kids[0]->ident;
            if (a64_vars_find(vars, name)) {
                a64_add_diag(diags, filename, stmt, "duplicate variable in asm a64 backend");
                return;
            }
            if (!a64_vars_add(vars, name, A64_TYPE_I64, NULL)) {
                a64_add_diag(diags, filename, stmt, "failed to allocate loop variable");
                return;
            }
            {
                char idx_name[64];
                a64_format_for_index_name(stmt, idx_name, sizeof(idx_name));
                if (!a64_vars_find(vars, idx_name)) {
                    char *idx_dup = strdup(idx_name);
                    if (!idx_dup) {
                        a64_add_diag(diags, filename, stmt, "failed to allocate loop index");
                        return;
                    }
                    if (!a64_vars_add(vars, idx_dup, A64_TYPE_I64, NULL)) {
                        free(idx_dup);
                        a64_add_diag(diags, filename, stmt, "failed to allocate loop index");
                        return;
                    }
                }
            }
            a64_collect_locals_stmt(stmt->kids[2], vars, filename, diags);
            return;
        }
        case NK_CASE: {
            char tmp_name[64];
            a64_format_case_tmp_name(stmt, tmp_name, sizeof(tmp_name));
            if (!a64_vars_find(vars, tmp_name)) {
                char *tmp_dup = strdup(tmp_name);
                if (!tmp_dup) {
                    a64_add_diag(diags, filename, stmt, "failed to allocate case temp");
                    return;
                }
                if (!a64_vars_add(vars, tmp_dup, A64_TYPE_I64, NULL)) {
                    a64_add_diag(diags, filename, stmt, "failed to allocate case temp");
                    return;
                }
            }
            for (size_t i = 1; i < stmt->len; i++) {
                const ChengNode *branch = stmt->kids[i];
                if (!branch) {
                    continue;
                }
                if (branch->kind == NK_OF_BRANCH) {
                    if (branch->len > 0) {
                        a64_collect_case_bindings(branch->kids[0], vars, filename, diags);
                    }
                    if (branch->len > 1) {
                        a64_collect_locals_stmt(branch->kids[1], vars, filename, diags);
                    }
                } else if (branch->kind == NK_ELSE) {
                    if (branch->len > 0) {
                        a64_collect_locals_stmt(branch->kids[0], vars, filename, diags);
                    }
                }
            }
            return;
        }
        default:
            return;
    }
}

static int a64_collect_params(const ChengNode *params,
                              A64VarTable *vars,
                              const char *filename,
                              ChengDiagList *diags) {
    if (!params || params->kind != NK_FORMAL_PARAMS) {
        return 0;
    }
    int count = 0;
    for (size_t index = 0; index < params->len; index++) {
        const ChengNode *entry = params->kids[index];
        if (!entry || entry->kind != NK_IDENT_DEFS || entry->len == 0) {
            a64_add_diag(diags, filename, entry, "asm a64 backend expects simple parameter list");
            continue;
        }
        const ChengNode *name_node = entry->kids[0];
        const ChengNode *type_node = entry->len > 1 ? entry->kids[1] : NULL;
        if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
            a64_add_diag(diags, filename, entry, "asm a64 backend expects parameter identifier");
            continue;
        }
        const char *name = name_node->ident;
        if (a64_vars_find(vars, name)) {
            a64_add_diag(diags, filename, entry, "duplicate parameter in asm a64 backend");
            continue;
        }
        A64TypeKind type = a64_type_from_node(type_node);
        if (!a64_vars_add(vars, name, type, type_node)) {
            a64_add_diag(diags, filename, entry, "failed to allocate parameter slot");
            continue;
        }
        count++;
    }
    return count;
}

static void a64_emit_imm(FILE *out, const char *reg, uint64_t value, int is32) {
    uint16_t part0 = (uint16_t)(value & 0xffffu);
    uint16_t part1 = (uint16_t)((value >> 16) & 0xffffu);
    uint16_t part2 = (uint16_t)((value >> 32) & 0xffffu);
    uint16_t part3 = (uint16_t)((value >> 48) & 0xffffu);
    fprintf(out, "  movz %s, #%u\n", reg, (unsigned)part0);
    if (part1 != 0) {
        fprintf(out, "  movk %s, #%u, lsl #16\n", reg, (unsigned)part1);
    }
    if (!is32) {
        if (part2 != 0) {
            fprintf(out, "  movk %s, #%u, lsl #32\n", reg, (unsigned)part2);
        }
        if (part3 != 0) {
            fprintf(out, "  movk %s, #%u, lsl #48\n", reg, (unsigned)part3);
        }
    }
}

static int a64_new_label(A64EmitContext *ctx) {
    int label = ctx->label_id;
    ctx->label_id++;
    return label;
}

static void a64_emit_label(A64EmitContext *ctx, int label) {
    fprintf(ctx->out, ".L%d:\n", label);
}

static int a64_temp_offset(const A64EmitContext *ctx, int index) {
    return ctx->frame.temp_area_offset + (index * 8);
}

static int a64_arg_slot_offset(const A64EmitContext *ctx, int index) {
    return ctx->frame.arg_slots_offset + (index * 8);
}

static int a64_local_offset(const A64EmitContext *ctx, const A64Var *var) {
    return ctx->frame.locals_offset + (int)(var->index * 8);
}

typedef enum {
    A64_ACCESS_BYTE = 0,
    A64_ACCESS_WORD,
    A64_ACCESS_DWORD,
    A64_ACCESS_QWORD
} A64AccessKind;

static A64AccessKind a64_access_from_size(size_t size) {
    if (size <= 1) {
        return A64_ACCESS_BYTE;
    }
    if (size <= 2) {
        return A64_ACCESS_WORD;
    }
    if (size <= 4) {
        return A64_ACCESS_DWORD;
    }
    return A64_ACCESS_QWORD;
}

static const AsmDataItem *a64_find_global_item(const A64EmitContext *ctx, const char *name) {
    if (!ctx || !ctx->layout || !name) {
        return NULL;
    }
    return asm_layout_find_data_item(ctx->layout, name);
}

static const char *a64_data_symbol_name(const A64EmitContext *ctx,
                                        const AsmDataItem *item,
                                        char *buf,
                                        size_t buf_len) {
    if (!item || !item->name) {
        return "";
    }
    if (item->is_global) {
        return a64_mangle_symbol(ctx, item->name, buf, buf_len);
    }
    return item->name;
}

static void a64_emit_symbol_addr(A64EmitContext *ctx,
                                 const char *name,
                                 int is_global,
                                 const char *reg) {
    char sym_buf[256];
    const char *sym = name;
    if (is_global) {
        sym = a64_mangle_symbol(ctx, name, sym_buf, sizeof(sym_buf));
    }
    if (ctx->is_darwin) {
        fprintf(ctx->out, "  adrp %s, %s@PAGE\n", reg, sym);
        fprintf(ctx->out, "  add %s, %s, %s@PAGEOFF\n", reg, reg, sym);
    } else {
        fprintf(ctx->out, "  adrp %s, %s\n", reg, sym);
        fprintf(ctx->out, "  add %s, %s, :lo12:%s\n", reg, reg, sym);
    }
}

static int a64_emit_expr(A64EmitContext *ctx, const ChengNode *expr);

static int a64_layout_field_info(A64EmitContext *ctx,
                                 const char *type_name,
                                 const char *field_name,
                                 size_t *out_offset,
                                 size_t *out_size) {
    if (!ctx || !ctx->layout || !type_name || !field_name || !out_offset || !out_size) {
        return 0;
    }
    const AsmTypeLayout *type_layout = asm_layout_find_type(ctx->layout, type_name);
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
} A64ResultFieldInfo;

static int a64_result_field_info(A64EmitContext *ctx, const char *type_name, A64ResultFieldInfo *out) {
    if (!ctx || !ctx->layout || !type_name || !out) {
        return 0;
    }
    if (!a64_layout_field_info(ctx, type_name, "ok", &out->ok_offset, &out->ok_size)) {
        return 0;
    }
    if (!a64_layout_field_info(ctx, type_name, "value", &out->value_offset, &out->value_size)) {
        return 0;
    }
    if (!a64_layout_field_info(ctx, type_name, "err", &out->err_offset, &out->err_size)) {
        return 0;
    }
    return 1;
}

static int a64_emit_dot_addr(A64EmitContext *ctx,
                             const ChengNode *expr,
                             A64AccessKind *out_kind);
static int a64_emit_index_addr(A64EmitContext *ctx,
                               const ChengNode *expr,
                               A64AccessKind *out_kind,
                               int *out_is_byte);
static void a64_push_reg(A64EmitContext *ctx, const char *reg);
static void a64_pop_reg(A64EmitContext *ctx, const char *reg);

static int a64_emit_addr(A64EmitContext *ctx,
                         const ChengNode *expr,
                         A64AccessKind *out_kind) {
    if (!ctx || !expr) {
        return 0;
    }
    switch (expr->kind) {
        case NK_PAR:
            if (expr->len > 0) {
                return a64_emit_addr(ctx, expr->kids[0], out_kind);
            }
            return 0;
        case NK_IDENT: {
            if (!expr->ident) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "asm a64 backend expects identifier");
                ctx->has_error = 1;
                return 0;
            }
            A64Var *var = a64_vars_find(&ctx->vars, expr->ident);
            if (var) {
                int offset = a64_local_offset(ctx, var);
                fprintf(ctx->out, "  add x0, sp, #%d\n", offset);
                if (out_kind) {
                    *out_kind = A64_ACCESS_QWORD;
                }
                return 1;
            }
            const AsmDataItem *global = a64_find_global_item(ctx, expr->ident);
            if (global) {
                char sym_buf[256];
                const char *sym = a64_data_symbol_name(ctx, global, sym_buf, sizeof(sym_buf));
                a64_emit_symbol_addr(ctx, sym, 0, "x0");
                if (out_kind) {
                    *out_kind = a64_access_from_size(global->size);
                }
                return 1;
            }
            a64_add_diag(ctx->diags, ctx->filename, expr, "asm a64 backend unknown address");
            ctx->has_error = 1;
            return 0;
        }
        case NK_DOT_EXPR:
            return a64_emit_dot_addr(ctx, expr, out_kind);
        case NK_BRACKET_EXPR: {
            int is_byte = 0;
            A64AccessKind access = A64_ACCESS_QWORD;
            if (!a64_emit_index_addr(ctx, expr, &access, &is_byte)) {
                return 0;
            }
            if (is_byte) {
                a64_add_diag(ctx->diags, ctx->filename, expr,
                             "string indexing is not assignable in asm a64 backend");
                ctx->has_error = 1;
                return 0;
            }
            if (out_kind) {
                *out_kind = access;
            }
            return 1;
        }
        case NK_HIDDEN_DEREF:
        case NK_DEREF_EXPR: {
            if (expr->len < 1 || !expr->kids[0]) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "asm a64 backend invalid deref");
                ctx->has_error = 1;
                return 0;
            }
            if (!a64_emit_expr(ctx, expr->kids[0])) {
                return 0;
            }
            if (out_kind) {
                *out_kind = A64_ACCESS_QWORD;
            }
            return 1;
        }
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0] || !expr->kids[0]->ident) {
                return 0;
            }
            const char *op = expr->kids[0]->ident;
            if (strcmp(op, "*") == 0) {
                if (!a64_emit_expr(ctx, expr->kids[1])) {
                    return 0;
                }
                if (out_kind) {
                    *out_kind = A64_ACCESS_QWORD;
                }
                return 1;
            }
            if (strcmp(op, "&") == 0) {
                return a64_emit_addr(ctx, expr->kids[1], out_kind);
            }
            break;
        }
        default:
            break;
    }
    a64_add_diag(ctx->diags, ctx->filename, expr, "unsupported address expression in asm a64 backend");
    ctx->has_error = 1;
    return 0;
}

static int a64_emit_index_addr(A64EmitContext *ctx,
                               const ChengNode *expr,
                               A64AccessKind *out_kind,
                               int *out_is_byte) {
    if (!ctx || !expr || expr->kind != NK_BRACKET_EXPR || expr->len < 2) {
        a64_add_diag(ctx ? ctx->diags : NULL, ctx ? ctx->filename : NULL, expr,
                     "invalid bracket expression in asm a64 backend");
        if (ctx) {
            ctx->has_error = 1;
        }
        return 0;
    }
    if (expr->len > 2) {
        a64_add_diag(ctx->diags, ctx->filename, expr,
                     "asm a64 backend does not support multi-index bracket expressions");
        ctx->has_error = 1;
        return 0;
    }
    const ChengNode *base = expr->kids[0];
    const ChengNode *index = expr->kids[1];
    const ChengNode *elem_type = NULL;
    A64IndexKind kind = a64_index_kind_for_expr(ctx, base, &elem_type);
    if (kind == A64_INDEX_NONE) {
        a64_add_diag(ctx->diags, ctx->filename, expr, "unsupported bracket expression in asm a64 backend");
        ctx->has_error = 1;
        return 0;
    }
    size_t elem_size = 8;
    int is_byte = 0;
    if (kind == A64_INDEX_STRING) {
        elem_size = 1;
        is_byte = 1;
    } else {
        elem_size = a64_elem_size_for_type(ctx, elem_type);
        if (elem_size == 0) {
            elem_size = 8;
        }
    }
    if (out_kind) {
        *out_kind = a64_access_from_size(elem_size);
    }
    if (out_is_byte) {
        *out_is_byte = is_byte;
    }
    if (kind == A64_INDEX_STRING) {
        if (!a64_emit_expr(ctx, base)) {
            return 0;
        }
        a64_push_reg(ctx, "x0");
        if (!a64_emit_expr(ctx, index)) {
            a64_pop_reg(ctx, "x1");
            return 0;
        }
        a64_pop_reg(ctx, "x1");
        fprintf(ctx->out, "  add x0, x1, x0\n");
        return 1;
    }
    if (!a64_emit_addr(ctx, base, NULL)) {
        return 0;
    }
    if (kind == A64_INDEX_SEQ) {
        fprintf(ctx->out, "  ldr x0, [x0, #8]\n");
    }
    a64_push_reg(ctx, "x0");
    if (!a64_emit_expr(ctx, index)) {
        a64_pop_reg(ctx, "x1");
        return 0;
    }
    a64_pop_reg(ctx, "x1");
    if (elem_size != 1) {
        fprintf(ctx->out, "  mov x2, #%zu\n", elem_size);
        fprintf(ctx->out, "  mul x0, x0, x2\n");
    }
    fprintf(ctx->out, "  add x0, x1, x0\n");
    return 1;
}

static int a64_emit_dot_addr(A64EmitContext *ctx,
                             const ChengNode *expr,
                             A64AccessKind *out_kind) {
    if (!ctx || !expr || expr->kind != NK_DOT_EXPR || expr->len < 2) {
        return 0;
    }
    const ChengNode *base = expr->kids[0];
    const ChengNode *member = expr->kids[1];
    if (!member || member->kind != NK_IDENT || !member->ident) {
        a64_add_diag(ctx->diags, ctx->filename, expr, "asm a64 backend expects identifier field");
        ctx->has_error = 1;
        return 0;
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
    base_for_type = a64_strip_paren_expr(base_for_type);
    const char *type_name = a64_expr_type_name(ctx, base_for_type);
    size_t offset = 0;
    size_t size = 8;
    int have_field = 0;
    if (type_name && ctx->layout) {
        have_field = a64_layout_field_info(ctx, type_name, member->ident, &offset, &size);
    }
    if (!have_field) {
        if (a64_expr_is_seq_like(ctx, base_for_type ? base_for_type : base)) {
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
        snprintf(msg, sizeof(msg), "unsupported dot field %s.%s in asm a64 backend",
                 type_name ? type_name : "<?>",
                 member->ident ? member->ident : "?");
        a64_add_diag(ctx->diags, ctx->filename, expr, msg);
        ctx->has_error = 1;
        return 0;
    }
    const ChengNode *base_addr = a64_strip_paren_expr(base);
    int base_is_indirect = 0;
    if (base_addr && base_addr->kind == NK_IDENT) {
        const ChengNode *type_expr = a64_var_type_expr_from_ident(ctx, base_addr);
        if (a64_type_expr_is_result(type_expr) || a64_type_expr_is_indirect(ctx, type_expr)) {
            base_is_indirect = 1;
        }
    }
    if (base_addr && base_addr->kind == NK_DOT_EXPR) {
        if (!a64_emit_dot_addr(ctx, base_addr, NULL)) {
            return 0;
        }
    } else if (base_addr && (base_addr->kind == NK_HIDDEN_DEREF || base_addr->kind == NK_DEREF_EXPR)) {
        if (base_addr->len < 1 || !base_addr->kids[0]) {
            a64_add_diag(ctx->diags, ctx->filename, expr, "invalid deref base in dot expression");
            ctx->has_error = 1;
            return 0;
        }
        if (!a64_emit_expr(ctx, base_addr->kids[0])) {
            return 0;
        }
    } else if (base_addr && base_addr->kind == NK_PREFIX && base_addr->len > 1 &&
               base_addr->kids[0] && base_addr->kids[0]->ident &&
               strcmp(base_addr->kids[0]->ident, "*") == 0) {
        if (!a64_emit_expr(ctx, base_addr->kids[1])) {
            return 0;
        }
    } else if (base_addr && base_addr->kind == NK_IDENT) {
        if (base_is_indirect) {
            if (!a64_emit_expr(ctx, base_addr)) {
                return 0;
            }
        } else {
            if (!a64_emit_addr(ctx, base_addr, NULL)) {
                return 0;
            }
        }
    } else {
        a64_add_diag(ctx->diags, ctx->filename, expr, "unsupported dot base expression in asm a64 backend");
        ctx->has_error = 1;
        return 0;
    }
    if (offset != 0) {
        fprintf(ctx->out, "  add x0, x0, #%zu\n", offset);
    }
    if (out_kind) {
        *out_kind = a64_access_from_size(size);
    }
    return 1;
}

static void a64_push_reg(A64EmitContext *ctx, const char *reg) {
    if (ctx->temp_top >= ctx->temp_slots) {
        ctx->has_error = 1;
        a64_add_diag(ctx->diags, ctx->filename, NULL, "asm a64 backend temp stack overflow");
        return;
    }
    int offset = a64_temp_offset(ctx, ctx->temp_top);
    fprintf(ctx->out, "  str %s, [sp, #%d]\n", reg, offset);
    ctx->temp_top++;
}

static void a64_pop_reg(A64EmitContext *ctx, const char *reg) {
    if (ctx->temp_top <= 0) {
        ctx->has_error = 1;
        a64_add_diag(ctx->diags, ctx->filename, NULL, "asm a64 backend temp stack underflow");
        return;
    }
    ctx->temp_top--;
    int offset = a64_temp_offset(ctx, ctx->temp_top);
    fprintf(ctx->out, "  ldr %s, [sp, #%d]\n", reg, offset);
}

static void a64_emit_orc_call(A64EmitContext *ctx,
                              const char *name,
                              const char *value_reg,
                              int preserve_x0) {
    if (!ctx || !name || !value_reg) {
        return;
    }
    if (preserve_x0) {
        a64_push_reg(ctx, "x0");
        if (ctx->has_error) {
            return;
        }
    }
    if (strcmp(value_reg, "x0") != 0) {
        fprintf(ctx->out, "  mov x0, %s\n", value_reg);
    }
    const char *call_sym = name;
    const char *runtime_sym = NULL;
    if (asm_runtime_lookup_symbol(name, &runtime_sym)) {
        call_sym = runtime_sym;
    }
    char sym_buf[256];
    call_sym = a64_mangle_symbol(ctx, call_sym, sym_buf, sizeof(sym_buf));
    fprintf(ctx->out, "  bl %s\n", call_sym);
    if (preserve_x0) {
        a64_pop_reg(ctx, "x0");
    }
}

static int a64_emit_result_unwrap(A64EmitContext *ctx, const ChengNode *value) {
    if (!ctx || !value) {
        return 0;
    }
    const char *type_name = a64_result_type_name_from_expr(ctx, value);
    if (!type_name || !a64_type_name_is_result(type_name)) {
        a64_add_diag(ctx->diags, ctx->filename, value, "Result/? expects a Result value in asm a64 backend");
        ctx->has_error = 1;
        return 0;
    }
    if (ctx->ret_is_result && ctx->ret_type_expr) {
        const char *ret_name = a64_type_name_from_type_expr(ctx->ret_type_expr);
        if (ret_name && strcmp(ret_name, type_name) != 0) {
            a64_add_diag(ctx->diags, ctx->filename, value, "Result/? type mismatch in asm a64 backend");
            ctx->has_error = 1;
            return 0;
        }
    }
    A64ResultFieldInfo info;
    if (!a64_result_field_info(ctx, type_name, &info)) {
        a64_add_diag(ctx->diags, ctx->filename, value, "Result/? missing layout info in asm a64 backend");
        ctx->has_error = 1;
        return 0;
    }
    if (!a64_emit_expr(ctx, value)) {
        return 0;
    }
    fprintf(ctx->out, "  mov x9, x0\n");
    switch (a64_access_from_size(info.ok_size)) {
        case A64_ACCESS_BYTE:
            fprintf(ctx->out, "  ldrb w10, [x9, #%zu]\n", info.ok_offset);
            break;
        case A64_ACCESS_WORD:
            fprintf(ctx->out, "  ldrh w10, [x9, #%zu]\n", info.ok_offset);
            break;
        case A64_ACCESS_DWORD:
            fprintf(ctx->out, "  ldr w10, [x9, #%zu]\n", info.ok_offset);
            break;
        case A64_ACCESS_QWORD:
        default:
            fprintf(ctx->out, "  ldr x10, [x9, #%zu]\n", info.ok_offset);
            break;
    }
    int err_label = a64_new_label(ctx);
    int done_label = a64_new_label(ctx);
    fprintf(ctx->out, "  cbz x10, .L%d\n", err_label);
    switch (a64_access_from_size(info.value_size)) {
        case A64_ACCESS_BYTE:
            fprintf(ctx->out, "  ldrb w0, [x9, #%zu]\n", info.value_offset);
            break;
        case A64_ACCESS_WORD:
            fprintf(ctx->out, "  ldrh w0, [x9, #%zu]\n", info.value_offset);
            break;
        case A64_ACCESS_DWORD:
            fprintf(ctx->out, "  ldr w0, [x9, #%zu]\n", info.value_offset);
            break;
        case A64_ACCESS_QWORD:
        default:
            fprintf(ctx->out, "  ldr x0, [x9, #%zu]\n", info.value_offset);
            break;
    }
    fprintf(ctx->out, "  b .L%d\n", done_label);
    a64_emit_label(ctx, err_label);
    if (ctx->ret_is_result) {
        fprintf(ctx->out, "  mov x0, x9\n");
        fprintf(ctx->out, "  b .L%d\n", ctx->ret_label);
    } else {
        switch (a64_access_from_size(info.err_size)) {
            case A64_ACCESS_BYTE:
                fprintf(ctx->out, "  ldrb w0, [x9, #%zu]\n", info.err_offset);
                break;
            case A64_ACCESS_WORD:
                fprintf(ctx->out, "  ldrh w0, [x9, #%zu]\n", info.err_offset);
                break;
            case A64_ACCESS_DWORD:
                fprintf(ctx->out, "  ldr w0, [x9, #%zu]\n", info.err_offset);
                break;
            case A64_ACCESS_QWORD:
            default:
                fprintf(ctx->out, "  ldr x0, [x9, #%zu]\n", info.err_offset);
                break;
        }
        a64_emit_orc_call(ctx, "panic", "x0", 0);
        fprintf(ctx->out, "  mov w0, #0\n");
        fprintf(ctx->out, "  b .L%d\n", ctx->ret_label);
    }
    a64_emit_label(ctx, done_label);
    return 1;
}

static void a64_loop_push(A64EmitContext *ctx, int break_label, int continue_label) {
    if (ctx->loop_depth >= ctx->loop_cap) {
        int next_cap = ctx->loop_cap == 0 ? 4 : ctx->loop_cap * 2;
        int *next_break = (int *)realloc(ctx->loop_break, sizeof(int) * (size_t)next_cap);
        int *next_continue = (int *)realloc(ctx->loop_continue, sizeof(int) * (size_t)next_cap);
        if (!next_break || !next_continue) {
            free(next_break);
            free(next_continue);
            ctx->has_error = 1;
            a64_add_diag(ctx->diags, ctx->filename, NULL, "asm a64 backend loop stack alloc failed");
            return;
        }
        ctx->loop_break = next_break;
        ctx->loop_continue = next_continue;
        ctx->loop_cap = next_cap;
    }
    ctx->loop_break[ctx->loop_depth] = break_label;
    ctx->loop_continue[ctx->loop_depth] = continue_label;
    ctx->loop_depth++;
}

static void a64_loop_pop(A64EmitContext *ctx) {
    if (ctx->loop_depth > 0) {
        ctx->loop_depth--;
    }
}

static int a64_loop_break_label(const A64EmitContext *ctx) {
    if (ctx->loop_depth == 0) {
        return -1;
    }
    return ctx->loop_break[ctx->loop_depth - 1];
}

static int a64_loop_continue_label(const A64EmitContext *ctx) {
    if (ctx->loop_depth == 0) {
        return -1;
    }
    return ctx->loop_continue[ctx->loop_depth - 1];
}

static int a64_is_compare_op(const char *op) {
    if (!op) {
        return 0;
    }
    return strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
           strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
           strcmp(op, ">") == 0 || strcmp(op, ">=") == 0;
}

static const char *a64_branch_cond(const char *op) {
    if (!op) {
        return "ne";
    }
    if (strcmp(op, "==") == 0) {
        return "eq";
    }
    if (strcmp(op, "!=") == 0) {
        return "ne";
    }
    if (strcmp(op, "<") == 0) {
        return "lt";
    }
    if (strcmp(op, "<=") == 0) {
        return "le";
    }
    if (strcmp(op, ">") == 0) {
        return "gt";
    }
    if (strcmp(op, ">=") == 0) {
        return "ge";
    }
    return "ne";
}

static int a64_emit_expr(A64EmitContext *ctx, const ChengNode *expr);
static void a64_emit_cond_branch(A64EmitContext *ctx,
                                 const ChengNode *expr,
                                 int true_label,
                                 int false_label);

static void a64_emit_bool_value(A64EmitContext *ctx, const ChengNode *expr) {
    int label_true = a64_new_label(ctx);
    int label_false = a64_new_label(ctx);
    int label_end = a64_new_label(ctx);
    a64_emit_cond_branch(ctx, expr, label_true, label_false);
    a64_emit_label(ctx, label_true);
    fprintf(ctx->out, "  mov w0, #1\n");
    fprintf(ctx->out, "  b .L%d\n", label_end);
    a64_emit_label(ctx, label_false);
    fprintf(ctx->out, "  mov w0, #0\n");
    a64_emit_label(ctx, label_end);
}

static int a64_emit_call(A64EmitContext *ctx, const ChengNode *call) {
    if (!call || call->kind != NK_CALL) {
        return 0;
    }
    if (call->len == 0 || !call->kids[0]) {
        a64_add_diag(ctx->diags, ctx->filename, call, "asm a64 backend expects call callee");
        ctx->has_error = 1;
        return 0;
    }
    const ChengNode *callee = call->kids[0];
    const ChengNode *call_target = a64_call_target_node(callee);
    int arg_count = 0;
    if (call->len > 0) {
        arg_count = (int)call->len - 1;
    }
    if (a64_is_type_call_target(ctx, callee)) {
        if (arg_count == 0) {
            fprintf(ctx->out, "  mov w0, #0\n");
            return 1;
        }
        if (arg_count == 1) {
            int has_named = 0;
            const ChengNode *arg = a64_call_arg_value(call->kids[1], &has_named);
            if (arg && !has_named) {
                return a64_emit_expr(ctx, arg);
            }
        }
    }
    if (!call_target ||
        !((call_target->kind == NK_IDENT || call_target->kind == NK_SYMBOL) && call_target->ident)) {
        a64_add_diag(ctx->diags, ctx->filename, call, "asm a64 backend only supports direct calls");
        ctx->has_error = 1;
        return 0;
    }
    if (strcmp(call_target->ident, "default") == 0) {
        if (arg_count != 1) {
            a64_add_diag(ctx->diags, ctx->filename, call, "default requires one type argument");
            ctx->has_error = 1;
            return 0;
        }
        int has_named = 0;
        const ChengNode *arg = a64_call_arg_value(call->kids[1], &has_named);
        if (!arg || has_named) {
            a64_add_diag(ctx->diags, ctx->filename, call, "default does not support named arguments");
            ctx->has_error = 1;
            return 0;
        }
        fprintf(ctx->out, "  mov w0, #0\n");
        return 1;
    }
    if (strcmp(call_target->ident, "sizeof") == 0 || strcmp(call_target->ident, "alignof") == 0) {
        if (arg_count != 1) {
            a64_add_diag(ctx->diags, ctx->filename, call, "sizeof/alignof requires one type argument");
            ctx->has_error = 1;
            return 0;
        }
        int has_named = 0;
        const ChengNode *arg = a64_call_arg_value(call->kids[1], &has_named);
        if (!arg || has_named) {
            a64_add_diag(ctx->diags, ctx->filename, call, "sizeof/alignof does not support named arguments");
            ctx->has_error = 1;
            return 0;
        }
        if (!ctx->layout) {
            a64_add_diag(ctx->diags, ctx->filename, call, "asm a64 backend missing layout for sizeof/alignof");
            ctx->has_error = 1;
            return 0;
        }
        size_t size = 0;
        size_t align = 0;
        if (!asm_layout_type_info(ctx->layout, arg, &size, &align)) {
            a64_add_diag(ctx->diags, ctx->filename, call, "asm a64 backend failed to resolve sizeof/alignof type");
            ctx->has_error = 1;
            return 0;
        }
        uint64_t value = strcmp(call_target->ident, "sizeof") == 0 ? (uint64_t)size : (uint64_t)align;
        a64_emit_imm(ctx->out, "x0", value, 0);
        return 1;
    }
    if (arg_count > ctx->arg_slots) {
        a64_add_diag(ctx->diags, ctx->filename, call, "asm a64 backend call arg overflow");
        ctx->has_error = 1;
        return 0;
    }
    for (int arg_index = 0; arg_index < arg_count; arg_index++) {
        int has_named = 0;
        const ChengNode *arg = a64_call_arg_value(call->kids[arg_index + 1], &has_named);
        if (has_named) {
            a64_add_diag(ctx->diags, ctx->filename, call, "asm a64 backend does not support named args");
            ctx->has_error = 1;
            return 0;
        }
        if (!arg) {
            a64_add_diag(ctx->diags, ctx->filename, call, "asm a64 backend invalid call argument");
            ctx->has_error = 1;
            return 0;
        }
        if (!a64_emit_expr(ctx, arg)) {
            return 0;
        }
        int slot_offset = a64_arg_slot_offset(ctx, arg_index);
        fprintf(ctx->out, "  str x0, [sp, #%d]\n", slot_offset);
    }
    for (int arg_index = 0; arg_index < arg_count; arg_index++) {
        int slot_offset = a64_arg_slot_offset(ctx, arg_index);
        if (arg_index < 8) {
            fprintf(ctx->out, "  ldr x%d, [sp, #%d]\n", arg_index, slot_offset);
        } else {
            int stack_offset = ctx->frame.call_area_offset + (arg_index - 8) * 8;
            fprintf(ctx->out, "  ldr x9, [sp, #%d]\n", slot_offset);
            fprintf(ctx->out, "  str x9, [sp, #%d]\n", stack_offset);
        }
    }
    const char *call_sym = call_target->ident;
    const char *runtime_sym = NULL;
    if (asm_runtime_lookup_symbol(callee->ident, &runtime_sym)) {
        call_sym = runtime_sym;
    }
    {
        char sym_buf[256];
        call_sym = a64_mangle_symbol(ctx, call_sym, sym_buf, sizeof(sym_buf));
        fprintf(ctx->out, "  bl %s\n", call_sym);
    }
    return 1;
}

static int a64_emit_expr(A64EmitContext *ctx, const ChengNode *expr) {
    if (ctx->has_error) {
        return 0;
    }
    if (!expr) {
        fprintf(ctx->out, "  mov w0, #0\n");
        return 1;
    }
    switch (expr->kind) {
        case NK_PAR:
            if (expr->len > 0) {
                return a64_emit_expr(ctx, expr->kids[0]);
            }
            fprintf(ctx->out, "  mov w0, #0\n");
            return 1;
        case NK_INT_LIT:
            a64_emit_imm(ctx->out, "x0", (uint64_t)expr->int_val, 0);
            return 1;
        case NK_CHAR_LIT: {
            int ch = 0;
            if (!a64_decode_char_lit(expr->str_val, &ch)) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "invalid char literal");
                ctx->has_error = 1;
                return 0;
            }
            a64_emit_imm(ctx->out, "x0", (uint64_t)(int64_t)ch, 0);
            return 1;
        }
        case NK_BOOL_LIT: {
            int value = 0;
            if (expr->ident && strcmp(expr->ident, "true") == 0) {
                value = 1;
            }
            fprintf(ctx->out, "  mov w0, #%d\n", value);
            return 1;
        }
        case NK_STR_LIT: {
            const char *label = NULL;
            if (ctx->layout) {
                label = asm_layout_string_label(ctx->layout, expr->str_val ? expr->str_val : "");
            }
            if (!label) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "missing string literal in asm layout");
                ctx->has_error = 1;
                return 0;
            }
            a64_emit_symbol_addr(ctx, label, 0, "x0");
            return 1;
        }
        case NK_NIL_LIT:
            fprintf(ctx->out, "  mov w0, #0\n");
            return 1;
        case NK_IDENT: {
            if (!expr->ident) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "asm a64 backend expects identifier");
                ctx->has_error = 1;
                return 0;
            }
            A64Var *var = a64_vars_find(&ctx->vars, expr->ident);
            if (!var) {
                const AsmDataItem *global = a64_find_global_item(ctx, expr->ident);
                if (global && global->is_global) {
                    char sym_buf[256];
                    const char *sym = a64_data_symbol_name(ctx, global, sym_buf, sizeof(sym_buf));
                    a64_emit_symbol_addr(ctx, sym, 0, "x0");
                    switch (a64_access_from_size(global->size)) {
                        case A64_ACCESS_BYTE:
                            fprintf(ctx->out, "  ldrb w0, [x0]\n");
                            break;
                        case A64_ACCESS_WORD:
                            fprintf(ctx->out, "  ldrh w0, [x0]\n");
                            break;
                        case A64_ACCESS_DWORD:
                            fprintf(ctx->out, "  ldr w0, [x0]\n");
                            break;
                        case A64_ACCESS_QWORD:
                        default:
                            fprintf(ctx->out, "  ldr x0, [x0]\n");
                            break;
                    }
                    return 1;
                }
                if (a64_is_type_call_target(ctx, expr)) {
                    fprintf(ctx->out, "  mov w0, #0\n");
                    return 1;
                }
                if (expr->ident && strncmp(expr->ident, "__cheng_", 8) == 0) {
                    fprintf(ctx->out, "  mov w0, #0\n");
                    return 1;
                }
                if (expr->ident && isupper((unsigned char)expr->ident[0])) {
                    fprintf(ctx->out, "  mov w0, #0\n");
                    return 1;
                }
                {
                    char msg[160];
                    snprintf(msg, sizeof(msg), "asm a64 backend unknown variable: %s",
                             expr->ident ? expr->ident : "?");
                    a64_add_diag(ctx->diags, ctx->filename, expr, msg);
                }
                ctx->has_error = 1;
                return 0;
            }
            int offset = a64_local_offset(ctx, var);
            fprintf(ctx->out, "  ldr x0, [sp, #%d]\n", offset);
            return 1;
        }
        case NK_CALL:
            return a64_emit_call(ctx, expr);
        case NK_CAST:
            if (expr->len > 1) {
                return a64_emit_expr(ctx, expr->kids[1]);
            }
            if (expr->len > 0) {
                return a64_emit_expr(ctx, expr->kids[0]);
            }
            fprintf(ctx->out, "  mov w0, #0\n");
            return 1;
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
                a64_add_diag(ctx->diags, ctx->filename, expr, "invalid postfix expression");
                ctx->has_error = 1;
                return 0;
            }
            if (op_name && strcmp(op_name, "?") == 0) {
                return a64_emit_result_unwrap(ctx, value);
            }
            return a64_emit_expr(ctx, value);
        }
        case NK_BRACKET_EXPR:
            if (expr->len > 0 && expr->kids[0] &&
                (expr->kids[0]->kind == NK_IDENT || expr->kids[0]->kind == NK_SYMBOL) &&
                expr->kids[0]->ident && strcmp(expr->kids[0]->ident, "default") == 0) {
                if (expr->len != 2) {
                    a64_add_diag(ctx->diags, ctx->filename, expr,
                                 "default requires one type argument");
                    ctx->has_error = 1;
                    return 0;
                }
                const ChengNode *arg = expr->kids[1];
                if (!arg || !a64_is_type_call_target(ctx, arg)) {
                    a64_add_diag(ctx->diags, ctx->filename, expr,
                                 "default requires a type argument");
                    ctx->has_error = 1;
                    return 0;
                }
                fprintf(ctx->out, "  mov w0, #0\n");
                return 1;
            }
            if (a64_is_type_call_target(ctx, expr)) {
                fprintf(ctx->out, "  mov w0, #0\n");
                return 1;
            }
            if (expr->len >= 2 && expr->kids[0] && expr->kids[1]) {
                A64IndexKind kind = a64_index_kind_for_expr(ctx, expr->kids[0], NULL);
                if (kind != A64_INDEX_NONE) {
                    int is_byte = 0;
                    A64AccessKind access = A64_ACCESS_QWORD;
                    if (!a64_emit_index_addr(ctx, expr, &access, &is_byte)) {
                        return 0;
                    }
                    if (is_byte) {
                        access = A64_ACCESS_BYTE;
                    }
                    switch (access) {
                        case A64_ACCESS_BYTE:
                            fprintf(ctx->out, "  ldrb w0, [x0]\n");
                            break;
                        case A64_ACCESS_WORD:
                            fprintf(ctx->out, "  ldrh w0, [x0]\n");
                            break;
                        case A64_ACCESS_DWORD:
                            fprintf(ctx->out, "  ldr w0, [x0]\n");
                            break;
                        case A64_ACCESS_QWORD:
                        default:
                            fprintf(ctx->out, "  ldr x0, [x0]\n");
                            break;
                    }
                    return 1;
                }
            }
            if (expr->len > 0 && expr->kids[0]) {
                return a64_emit_expr(ctx, expr->kids[0]);
            }
            a64_add_diag(ctx->diags, ctx->filename, expr, "invalid bracket expression in asm a64 backend");
            ctx->has_error = 1;
            return 0;
        case NK_PTR_TY:
        case NK_REF_TY:
        case NK_VAR_TY:
        case NK_FN_TY:
        case NK_SET_TY:
        case NK_TUPLE_TY:
            fprintf(ctx->out, "  mov w0, #0\n");
            return 1;
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0] || !expr->kids[0]->ident) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "invalid prefix expression");
                ctx->has_error = 1;
                return 0;
            }
            const char *op = expr->kids[0]->ident;
            const ChengNode *rhs = expr->kids[1];
            if (strcmp(op, "!") == 0) {
                a64_emit_bool_value(ctx, rhs);
                fprintf(ctx->out, "  eor w0, w0, #1\n");
                return 1;
            }
            if (strcmp(op, "&") == 0) {
                return a64_emit_addr(ctx, rhs, NULL);
            }
            if (!a64_emit_expr(ctx, rhs)) {
                return 0;
            }
            if (strcmp(op, "+") == 0) {
                return 1;
            }
            if (strcmp(op, "-") == 0) {
                fprintf(ctx->out, "  neg x0, x0\n");
                return 1;
            }
            if (strcmp(op, "~") == 0) {
                fprintf(ctx->out, "  mvn x0, x0\n");
                return 1;
            }
            if (strcmp(op, "*") == 0) {
                fprintf(ctx->out, "  ldr x0, [x0]\n");
                return 1;
            }
            a64_add_diag(ctx->diags, ctx->filename, expr, "unsupported prefix operator in asm a64 backend");
            ctx->has_error = 1;
            return 0;
        }
        case NK_DOT_EXPR: {
            if (expr->len < 2 || !expr->kids[1] || expr->kids[1]->kind != NK_IDENT ||
                !expr->kids[1]->ident) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "unsupported dot expression in asm a64 backend");
                ctx->has_error = 1;
                return 0;
            }
            const char *field = expr->kids[1]->ident;
            const ChengNode *base = expr->kids[0];
            if (field && strcmp(field, "len") == 0 && base && a64_expr_is_string_like(ctx, base)) {
                if (!a64_emit_expr(ctx, base)) {
                    return 0;
                }
                const char *call_sym = "c_strlen";
                const char *runtime_sym = NULL;
                if (asm_runtime_lookup_symbol(call_sym, &runtime_sym)) {
                    call_sym = runtime_sym;
                }
                char sym_buf[256];
                call_sym = a64_mangle_symbol(ctx, call_sym, sym_buf, sizeof(sym_buf));
                fprintf(ctx->out, "  bl %s\n", call_sym);
                return 1;
            }
            A64AccessKind access = A64_ACCESS_QWORD;
            if (!a64_emit_dot_addr(ctx, expr, &access)) {
                return 0;
            }
            switch (access) {
                case A64_ACCESS_BYTE:
                    fprintf(ctx->out, "  ldrb w0, [x0]\n");
                    break;
                case A64_ACCESS_WORD:
                    fprintf(ctx->out, "  ldrh w0, [x0]\n");
                    break;
                case A64_ACCESS_DWORD:
                    fprintf(ctx->out, "  ldr w0, [x0]\n");
                    break;
                case A64_ACCESS_QWORD:
                default:
                    fprintf(ctx->out, "  ldr x0, [x0]\n");
                    break;
            }
            return 1;
        }
        case NK_ASGN: {
            if (expr->len < 2) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "invalid assignment expression");
                ctx->has_error = 1;
                return 0;
            }
            const ChengNode *lhs = expr->kids[0];
            const ChengNode *rhs = expr->kids[1];
            if (a64_orc_enabled()) {
                const ChengNode *lhs_base = a64_strip_paren_expr(lhs);
                if (lhs_base && (lhs_base->kind == NK_IDENT || lhs_base->kind == NK_SYMBOL)) {
                    const ChengNode *type_expr = a64_var_type_expr_from_ident(ctx, lhs_base);
                    if (a64_orc_type_is_managed(type_expr)) {
                        if (!a64_emit_expr(ctx, rhs)) {
                            return 0;
                        }
                        if (a64_orc_expr_borrowed(rhs)) {
                            a64_emit_orc_call(ctx, "memRetain", "x0", 1);
                        }
                        a64_push_reg(ctx, "x0");
                        if (ctx->has_error) {
                            return 0;
                        }
                        if (!a64_emit_addr(ctx, lhs, NULL)) {
                            a64_pop_reg(ctx, "x1");
                            return 0;
                        }
                        fprintf(ctx->out, "  ldr x2, [x0]\n");
                        a64_pop_reg(ctx, "x1");
                        fprintf(ctx->out, "  str x1, [x0]\n");
                        fprintf(ctx->out, "  mov x0, x1\n");
                        a64_emit_orc_call(ctx, "memRelease", "x2", 1);
                        return 1;
                    }
                }
            }
            if (!a64_emit_expr(ctx, rhs)) {
                return 0;
            }
            a64_push_reg(ctx, "x0");
            A64AccessKind access = A64_ACCESS_QWORD;
            if (!a64_emit_addr(ctx, lhs, &access)) {
                a64_pop_reg(ctx, "x1");
                return 0;
            }
            a64_pop_reg(ctx, "x1");
            switch (access) {
                case A64_ACCESS_BYTE:
                    fprintf(ctx->out, "  strb w1, [x0]\n");
                    fprintf(ctx->out, "  uxtb w0, w1\n");
                    break;
                case A64_ACCESS_WORD:
                    fprintf(ctx->out, "  strh w1, [x0]\n");
                    fprintf(ctx->out, "  uxth w0, w1\n");
                    break;
                case A64_ACCESS_DWORD:
                    fprintf(ctx->out, "  str w1, [x0]\n");
                    fprintf(ctx->out, "  mov w0, w1\n");
                    break;
                case A64_ACCESS_QWORD:
                default:
                    fprintf(ctx->out, "  str x1, [x0]\n");
                    fprintf(ctx->out, "  mov x0, x1\n");
                    break;
            }
            return 1;
        }
        case NK_INFIX: {
            if (expr->len < 3 || !expr->kids[0] || !expr->kids[0]->ident) {
                a64_add_diag(ctx->diags, ctx->filename, expr, "invalid infix expression");
                ctx->has_error = 1;
                return 0;
            }
            const char *op = expr->kids[0]->ident;
            const ChengNode *lhs = expr->kids[1];
            const ChengNode *rhs = expr->kids[2];
            if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
                a64_emit_bool_value(ctx, expr);
                return 1;
            }
            if (a64_is_compare_op(op)) {
                if (!a64_emit_expr(ctx, lhs)) {
                    return 0;
                }
                a64_push_reg(ctx, "x0");
                if (!a64_emit_expr(ctx, rhs)) {
                    return 0;
                }
                a64_pop_reg(ctx, "x1");
                fprintf(ctx->out, "  cmp x1, x0\n");
                fprintf(ctx->out, "  cset w0, %s\n", a64_branch_cond(op));
                return 1;
            }
            if (!a64_emit_expr(ctx, lhs)) {
                return 0;
            }
            a64_push_reg(ctx, "x0");
            if (!a64_emit_expr(ctx, rhs)) {
                return 0;
            }
            a64_pop_reg(ctx, "x1");
            if (strcmp(op, "+") == 0) {
                fprintf(ctx->out, "  add x0, x1, x0\n");
                return 1;
            }
            if (strcmp(op, "-") == 0) {
                fprintf(ctx->out, "  sub x0, x1, x0\n");
                return 1;
            }
            if (strcmp(op, "*") == 0) {
                fprintf(ctx->out, "  mul x0, x1, x0\n");
                return 1;
            }
            if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
                fprintf(ctx->out, "  sdiv x0, x1, x0\n");
                return 1;
            }
            if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
                fprintf(ctx->out, "  sdiv x9, x1, x0\n");
                fprintf(ctx->out, "  msub x0, x9, x0, x1\n");
                return 1;
            }
            if (strcmp(op, "&") == 0) {
                fprintf(ctx->out, "  and x0, x1, x0\n");
                return 1;
            }
            if (strcmp(op, "|") == 0) {
                fprintf(ctx->out, "  orr x0, x1, x0\n");
                return 1;
            }
            if (strcmp(op, "^") == 0) {
                fprintf(ctx->out, "  eor x0, x1, x0\n");
                return 1;
            }
            if (strcmp(op, "<<") == 0) {
                fprintf(ctx->out, "  lsl x0, x1, x0\n");
                return 1;
            }
            if (strcmp(op, ">>") == 0) {
                fprintf(ctx->out, "  asr x0, x1, x0\n");
                return 1;
            }
            a64_add_diag(ctx->diags, ctx->filename, expr, "unsupported infix operator in asm a64 backend");
            ctx->has_error = 1;
            return 0;
        }
        default:
            {
                char msg[96];
                snprintf(msg, sizeof(msg), "unsupported expression kind %d in asm a64 backend",
                         expr->kind);
                a64_add_diag(ctx->diags, ctx->filename, expr, msg);
            }
            ctx->has_error = 1;
            return 0;
    }
}

static void a64_emit_cond_branch(A64EmitContext *ctx,
                                 const ChengNode *expr,
                                 int true_label,
                                 int false_label) {
    if (ctx->has_error) {
        return;
    }
    if (!expr) {
        fprintf(ctx->out, "  b .L%d\n", false_label);
        return;
    }
    if (expr->kind == NK_BOOL_LIT) {
        int value = 0;
        if (expr->ident && strcmp(expr->ident, "true") == 0) {
            value = 1;
        }
        fprintf(ctx->out, "  b .L%d\n", value ? true_label : false_label);
        return;
    }
    if (expr->kind == NK_PREFIX && expr->len > 1 && expr->kids[0] && expr->kids[0]->ident &&
        strcmp(expr->kids[0]->ident, "!") == 0) {
        a64_emit_cond_branch(ctx, expr->kids[1], false_label, true_label);
        return;
    }
    if (expr->kind == NK_INFIX && expr->len >= 3 && expr->kids[0] && expr->kids[0]->ident) {
        const char *op = expr->kids[0]->ident;
        if (strcmp(op, "&&") == 0) {
            int mid_label = a64_new_label(ctx);
            a64_emit_cond_branch(ctx, expr->kids[1], mid_label, false_label);
            a64_emit_label(ctx, mid_label);
            a64_emit_cond_branch(ctx, expr->kids[2], true_label, false_label);
            return;
        }
        if (strcmp(op, "||") == 0) {
            int mid_label = a64_new_label(ctx);
            a64_emit_cond_branch(ctx, expr->kids[1], true_label, mid_label);
            a64_emit_label(ctx, mid_label);
            a64_emit_cond_branch(ctx, expr->kids[2], true_label, false_label);
            return;
        }
        if (a64_is_compare_op(op)) {
            if (!a64_emit_expr(ctx, expr->kids[1])) {
                return;
            }
            a64_push_reg(ctx, "x0");
            if (!a64_emit_expr(ctx, expr->kids[2])) {
                return;
            }
            a64_pop_reg(ctx, "x1");
            fprintf(ctx->out, "  cmp x1, x0\n");
            fprintf(ctx->out, "  b.%s .L%d\n", a64_branch_cond(op), true_label);
            fprintf(ctx->out, "  b .L%d\n", false_label);
            return;
        }
    }
    if (!a64_emit_expr(ctx, expr)) {
        return;
    }
    fprintf(ctx->out, "  cmp x0, #0\n");
    fprintf(ctx->out, "  b.ne .L%d\n", true_label);
    fprintf(ctx->out, "  b .L%d\n", false_label);
}

static void a64_emit_stmt(A64EmitContext *ctx, const ChengNode *stmt);

static void a64_emit_stmt_list(A64EmitContext *ctx, const ChengNode *list) {
    if (!list || ctx->has_error) {
        return;
    }
    if (list->kind != NK_STMT_LIST) {
        a64_emit_stmt(ctx, list);
        return;
    }
    for (size_t index = 0; index < list->len; index++) {
        a64_emit_stmt(ctx, list->kids[index]);
        if (ctx->has_error) {
            return;
        }
    }
}

static void a64_emit_for(A64EmitContext *ctx, const ChengNode *stmt) {
    if (!ctx || !stmt || stmt->len < 3) {
        a64_add_diag(ctx->diags, ctx->filename, stmt, "invalid for statement");
        ctx->has_error = 1;
        return;
    }
    const ChengNode *pattern = stmt->kids[0];
    const ChengNode *iter = stmt->kids[1];
    const ChengNode *body = stmt->kids[2];
    if (!pattern || pattern->kind != NK_PATTERN || pattern->len == 0 ||
        !pattern->kids[0] || pattern->kids[0]->kind != NK_IDENT || !pattern->kids[0]->ident) {
        a64_add_diag(ctx->diags, ctx->filename, stmt, "for requires identifier pattern in asm a64 backend");
        ctx->has_error = 1;
        return;
    }
    const char *name = pattern->kids[0]->ident;
    A64Var *var = a64_vars_find(&ctx->vars, name);
    if (!var) {
        a64_add_diag(ctx->diags, ctx->filename, stmt, "asm a64 backend unknown loop variable");
        ctx->has_error = 1;
        return;
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
        int is_seq = a64_expr_is_seq_like(ctx, iter);
        int is_str = a64_expr_is_string_like(ctx, iter);
        if (!is_seq && !is_str) {
            a64_add_diag(ctx->diags, ctx->filename, stmt,
                         "for only supports numeric ranges or seq/str iterables");
            ctx->has_error = 1;
            return;
        }
        char idx_name[64];
        a64_format_for_index_name(stmt, idx_name, sizeof(idx_name));
        A64Var *idx_var = a64_vars_find(&ctx->vars, idx_name);
        if (!idx_var) {
            a64_add_diag(ctx->diags, ctx->filename, stmt, "asm a64 backend missing loop index slot");
            ctx->has_error = 1;
            return;
        }
        int idx_offset = a64_local_offset(ctx, idx_var);
        fprintf(ctx->out, "  mov x0, #0\n");
        fprintf(ctx->out, "  str x0, [sp, #%d]\n", idx_offset);
        int label_cond = a64_new_label(ctx);
        int label_step = a64_new_label(ctx);
        int label_end = a64_new_label(ctx);
        size_t elem_size = 8;
        if (is_seq) {
            const ChengNode *iter_probe = a64_strip_paren_expr(iter);
            const ChengNode *iter_type = NULL;
            if (iter_probe && iter_probe->kind == NK_IDENT) {
                iter_type = a64_var_type_expr_from_ident(ctx, iter_probe);
            }
            const ChengNode *elem_type = a64_type_expr_bracket_arg(iter_type, 1);
            if (elem_type) {
                elem_size = a64_elem_size_for_type(ctx, elem_type);
                if (elem_size == 0) {
                    elem_size = 8;
                }
            }
        }
        a64_emit_label(ctx, label_cond);
        if (is_seq) {
            if (!a64_emit_addr(ctx, iter, NULL)) {
                return;
            }
            fprintf(ctx->out, "  ldr w1, [x0]\n");
        } else {
            if (!a64_emit_expr(ctx, iter)) {
                return;
            }
            const char *call_sym = "c_strlen";
            const char *runtime_sym = NULL;
            if (asm_runtime_lookup_symbol(call_sym, &runtime_sym)) {
                call_sym = runtime_sym;
            }
            char sym_buf[256];
            call_sym = a64_mangle_symbol(ctx, call_sym, sym_buf, sizeof(sym_buf));
            fprintf(ctx->out, "  bl %s\n", call_sym);
            fprintf(ctx->out, "  mov x1, x0\n");
        }
        fprintf(ctx->out, "  ldr x0, [sp, #%d]\n", idx_offset);
        fprintf(ctx->out, "  cmp x0, x1\n");
        fprintf(ctx->out, "  b.ge .L%d\n", label_end);
        if (is_seq) {
            if (!a64_emit_addr(ctx, iter, NULL)) {
                return;
            }
            fprintf(ctx->out, "  ldr x0, [x0, #8]\n");
            fprintf(ctx->out, "  ldr x1, [sp, #%d]\n", idx_offset);
            if (elem_size != 1) {
                fprintf(ctx->out, "  mov x2, #%zu\n", elem_size);
                fprintf(ctx->out, "  mul x1, x1, x2\n");
            }
            fprintf(ctx->out, "  add x0, x0, x1\n");
            switch (a64_access_from_size(elem_size)) {
                case A64_ACCESS_BYTE:
                    fprintf(ctx->out, "  ldrb w0, [x0]\n");
                    break;
                case A64_ACCESS_WORD:
                    fprintf(ctx->out, "  ldrh w0, [x0]\n");
                    break;
                case A64_ACCESS_DWORD:
                    fprintf(ctx->out, "  ldr w0, [x0]\n");
                    break;
                case A64_ACCESS_QWORD:
                default:
                    fprintf(ctx->out, "  ldr x0, [x0]\n");
                    break;
            }
        } else {
            if (!a64_emit_expr(ctx, iter)) {
                return;
            }
            fprintf(ctx->out, "  ldr x1, [sp, #%d]\n", idx_offset);
            fprintf(ctx->out, "  add x0, x0, x1\n");
            fprintf(ctx->out, "  ldrb w0, [x0]\n");
        }
        int offset = a64_local_offset(ctx, var);
        fprintf(ctx->out, "  str x0, [sp, #%d]\n", offset);
        a64_loop_push(ctx, label_end, label_step);
        a64_emit_stmt_list(ctx, body);
        a64_loop_pop(ctx);
        a64_emit_label(ctx, label_step);
        fprintf(ctx->out, "  ldr x0, [sp, #%d]\n", idx_offset);
        fprintf(ctx->out, "  add x0, x0, #1\n");
        fprintf(ctx->out, "  str x0, [sp, #%d]\n", idx_offset);
        fprintf(ctx->out, "  b .L%d\n", label_cond);
        a64_emit_label(ctx, label_end);
        return;
    }
    if (!a64_emit_expr(ctx, start)) {
        return;
    }
    int offset = a64_local_offset(ctx, var);
    fprintf(ctx->out, "  str x0, [sp, #%d]\n", offset);
    int label_cond = a64_new_label(ctx);
    int label_step = a64_new_label(ctx);
    int label_end = a64_new_label(ctx);
    a64_emit_label(ctx, label_cond);
    if (!a64_emit_expr(ctx, end)) {
        return;
    }
    fprintf(ctx->out, "  mov x1, x0\n");
    fprintf(ctx->out, "  ldr x0, [sp, #%d]\n", offset);
    fprintf(ctx->out, "  cmp x0, x1\n");
    if (inclusive) {
        fprintf(ctx->out, "  b.gt .L%d\n", label_end);
    } else {
        fprintf(ctx->out, "  b.ge .L%d\n", label_end);
    }
    a64_loop_push(ctx, label_end, label_step);
    a64_emit_stmt_list(ctx, body);
    a64_loop_pop(ctx);
    a64_emit_label(ctx, label_step);
    fprintf(ctx->out, "  ldr x0, [sp, #%d]\n", offset);
    fprintf(ctx->out, "  add x0, x0, #1\n");
    fprintf(ctx->out, "  str x0, [sp, #%d]\n", offset);
    fprintf(ctx->out, "  b .L%d\n", label_cond);
    a64_emit_label(ctx, label_end);
}

static void a64_emit_case(A64EmitContext *ctx, const ChengNode *stmt) {
    if (!ctx || !stmt || stmt->len < 1) {
        a64_add_diag(ctx->diags, ctx->filename, stmt, "invalid case statement in asm a64 backend");
        ctx->has_error = 1;
        return;
    }
    const ChengNode *selector = stmt->kids[0];
    char tmp_name[64];
    a64_format_case_tmp_name(stmt, tmp_name, sizeof(tmp_name));
    A64Var *tmp_var = a64_vars_find(&ctx->vars, tmp_name);
    if (!tmp_var) {
        a64_add_diag(ctx->diags, ctx->filename, stmt, "asm a64 backend missing case temp");
        ctx->has_error = 1;
        return;
    }
    if (!a64_emit_expr(ctx, selector)) {
        return;
    }
    int tmp_offset = a64_local_offset(ctx, tmp_var);
    fprintf(ctx->out, "  str x0, [sp, #%d]\n", tmp_offset);
    int end_label = a64_new_label(ctx);
    for (size_t i = 1; i < stmt->len; i++) {
        const ChengNode *branch = stmt->kids[i];
        if (!branch) {
            continue;
        }
        if (branch->kind == NK_OF_BRANCH) {
            const ChengNode *pattern = branch->len > 0 ? branch->kids[0] : NULL;
            const ChengNode *body = branch->len > 1 ? branch->kids[1] : NULL;
            int match_label = a64_new_label(ctx);
            const char *bind_name = NULL;
            int unconditional = 0;
            int saw_pattern = 0;
            if (pattern && pattern->kind == NK_PATTERN) {
                for (size_t p = 0; p < pattern->len; p++) {
                    const ChengNode *pat = pattern->kids[p];
                    if (!pat) {
                        continue;
                    }
                    const char *name = a64_case_pattern_ident(pat);
                    if (name) {
                        saw_pattern = 1;
                        if (!a64_case_pattern_is_wildcard(name)) {
                            bind_name = name;
                        }
                        unconditional = 1;
                        break;
                    }
                    if (!a64_case_pattern_const(pat)) {
                        a64_add_diag(ctx->diags, ctx->filename, pat,
                                     "unsupported case pattern in asm a64 backend");
                        ctx->has_error = 1;
                        return;
                    }
                    saw_pattern = 1;
                    if (!a64_emit_expr(ctx, pat)) {
                        return;
                    }
                    fprintf(ctx->out, "  ldr x1, [sp, #%d]\n", tmp_offset);
                    fprintf(ctx->out, "  cmp x0, x1\n");
                    fprintf(ctx->out, "  b.eq .L%d\n", match_label);
                }
            } else if (pattern) {
                const char *name = a64_case_pattern_ident(pattern);
                if (name) {
                    saw_pattern = 1;
                    if (!a64_case_pattern_is_wildcard(name)) {
                        bind_name = name;
                    }
                    unconditional = 1;
                } else if (a64_case_pattern_const(pattern)) {
                    saw_pattern = 1;
                    if (!a64_emit_expr(ctx, pattern)) {
                        return;
                    }
                    fprintf(ctx->out, "  ldr x1, [sp, #%d]\n", tmp_offset);
                    fprintf(ctx->out, "  cmp x0, x1\n");
                    fprintf(ctx->out, "  b.eq .L%d\n", match_label);
                }
            }
            if (!saw_pattern) {
                a64_add_diag(ctx->diags, ctx->filename, branch, "invalid case pattern in asm a64 backend");
                ctx->has_error = 1;
                return;
            }
            int next_label = -1;
            if (unconditional) {
                fprintf(ctx->out, "  b .L%d\n", match_label);
            } else {
                next_label = a64_new_label(ctx);
                fprintf(ctx->out, "  b .L%d\n", next_label);
            }
            a64_emit_label(ctx, match_label);
            if (bind_name) {
                A64Var *bind_var = a64_vars_find(&ctx->vars, bind_name);
                if (!bind_var) {
                    a64_add_diag(ctx->diags, ctx->filename, branch,
                                 "asm a64 backend missing case binding");
                    ctx->has_error = 1;
                    return;
                }
                int bind_offset = a64_local_offset(ctx, bind_var);
                fprintf(ctx->out, "  ldr x0, [sp, #%d]\n", tmp_offset);
                fprintf(ctx->out, "  str x0, [sp, #%d]\n", bind_offset);
            }
            a64_emit_stmt_list(ctx, body);
            fprintf(ctx->out, "  b .L%d\n", end_label);
            if (!unconditional && next_label >= 0) {
                a64_emit_label(ctx, next_label);
            }
            continue;
        }
        if (branch->kind == NK_ELSE) {
            const ChengNode *body = branch->len > 0 ? branch->kids[0] : NULL;
            a64_emit_stmt_list(ctx, body);
            fprintf(ctx->out, "  b .L%d\n", end_label);
        }
    }
    a64_emit_label(ctx, end_label);
}

static void a64_emit_stmt(A64EmitContext *ctx, const ChengNode *stmt) {
    if (!stmt || ctx->has_error) {
        return;
    }
    switch (stmt->kind) {
        case NK_STMT_LIST:
            a64_emit_stmt_list(ctx, stmt);
            return;
        case NK_EMPTY:
            return;
        case NK_PRAGMA:
            return;
        case NK_RETURN: {
            const ChengNode *expr = stmt->len > 0 ? stmt->kids[0] : NULL;
            if (!expr || expr->kind == NK_EMPTY) {
                fprintf(ctx->out, "  mov w0, #0\n");
            } else {
                if (!a64_emit_expr(ctx, expr)) {
                    return;
                }
                if (a64_orc_enabled() && ctx->ret_type_expr &&
                    a64_orc_type_is_managed(ctx->ret_type_expr) &&
                    a64_orc_expr_borrowed(expr)) {
                    a64_emit_orc_call(ctx, "memRetain", "x0", 1);
                }
            }
            fprintf(ctx->out, "  b .L%d\n", ctx->ret_label);
            return;
        }
        case NK_LET:
        case NK_VAR:
        case NK_CONST: {
            const ChengNode *pattern = stmt->len > 0 ? stmt->kids[0] : NULL;
            const ChengNode *value = stmt->len > 2 ? stmt->kids[2] : NULL;
            if (!pattern || pattern->kind != NK_PATTERN || pattern->len == 0 ||
                !pattern->kids[0] || pattern->kids[0]->kind != NK_IDENT) {
                a64_add_diag(ctx->diags, ctx->filename, stmt, "asm a64 backend expects simple binding");
                ctx->has_error = 1;
                return;
            }
            const char *name = pattern->kids[0]->ident;
            if (!name) {
                a64_add_diag(ctx->diags, ctx->filename, stmt, "asm a64 backend expects identifier");
                ctx->has_error = 1;
                return;
            }
            A64Var *var = a64_vars_find(&ctx->vars, name);
            if (!var) {
                a64_add_diag(ctx->diags, ctx->filename, stmt, "asm a64 backend missing binding");
                ctx->has_error = 1;
                return;
            }
            if (value && value->kind != NK_EMPTY) {
                if (!a64_emit_expr(ctx, value)) {
                    return;
                }
            } else {
                fprintf(ctx->out, "  mov w0, #0\n");
            }
            if (a64_orc_enabled() && value && value->kind != NK_EMPTY) {
                const ChengNode *type_expr = a64_var_type_expr_from_ident(ctx, pattern->kids[0]);
                if (a64_orc_type_is_managed(type_expr) && a64_orc_expr_borrowed(value)) {
                    a64_emit_orc_call(ctx, "memRetain", "x0", 1);
                }
            }
            int offset = a64_local_offset(ctx, var);
            fprintf(ctx->out, "  str x0, [sp, #%d]\n", offset);
            return;
        }
        case NK_ASGN: {
            if (stmt->len < 2) {
                a64_add_diag(ctx->diags, ctx->filename, stmt, "asm a64 backend invalid assignment");
                ctx->has_error = 1;
                return;
            }
            const ChengNode *lhs = stmt->kids[0];
            const ChengNode *rhs = stmt->kids[1];
            if (!a64_emit_expr(ctx, rhs)) {
                return;
            }
            if (a64_orc_enabled()) {
                const ChengNode *lhs_base = a64_strip_paren_expr(lhs);
                if (lhs_base && (lhs_base->kind == NK_IDENT || lhs_base->kind == NK_SYMBOL)) {
                    const ChengNode *type_expr = a64_var_type_expr_from_ident(ctx, lhs_base);
                    if (a64_orc_type_is_managed(type_expr)) {
                        if (a64_orc_expr_borrowed(rhs)) {
                            a64_emit_orc_call(ctx, "memRetain", "x0", 1);
                        }
                        a64_push_reg(ctx, "x0");
                        if (ctx->has_error) {
                            return;
                        }
                        if (!a64_emit_addr(ctx, lhs, NULL)) {
                            a64_pop_reg(ctx, "x1");
                            return;
                        }
                        fprintf(ctx->out, "  ldr x2, [x0]\n");
                        a64_pop_reg(ctx, "x1");
                        fprintf(ctx->out, "  str x1, [x0]\n");
                        a64_emit_orc_call(ctx, "memRelease", "x2", 0);
                        return;
                    }
                }
            }
            a64_push_reg(ctx, "x0");
            A64AccessKind access = A64_ACCESS_QWORD;
            if (!a64_emit_addr(ctx, lhs, &access)) {
                a64_pop_reg(ctx, "x1");
                return;
            }
            a64_pop_reg(ctx, "x1");
            switch (access) {
                case A64_ACCESS_BYTE:
                    fprintf(ctx->out, "  strb w1, [x0]\n");
                    fprintf(ctx->out, "  uxtb w0, w1\n");
                    break;
                case A64_ACCESS_WORD:
                    fprintf(ctx->out, "  strh w1, [x0]\n");
                    fprintf(ctx->out, "  uxth w0, w1\n");
                    break;
                case A64_ACCESS_DWORD:
                    fprintf(ctx->out, "  str w1, [x0]\n");
                    fprintf(ctx->out, "  mov w0, w1\n");
                    break;
                case A64_ACCESS_QWORD:
                default:
                    fprintf(ctx->out, "  str x1, [x0]\n");
                    fprintf(ctx->out, "  mov x0, x1\n");
                    break;
            }
            return;
        }
        case NK_IF: {
            int label_true = a64_new_label(ctx);
            int label_false = a64_new_label(ctx);
            int label_end = a64_new_label(ctx);
            const ChengNode *cond = stmt->len > 0 ? stmt->kids[0] : NULL;
            const ChengNode *body = stmt->len > 1 ? stmt->kids[1] : NULL;
            const ChengNode *else_node = stmt->len > 2 ? stmt->kids[2] : NULL;
            a64_emit_cond_branch(ctx, cond, label_true, label_false);
            a64_emit_label(ctx, label_true);
            a64_emit_stmt_list(ctx, body);
            fprintf(ctx->out, "  b .L%d\n", label_end);
            a64_emit_label(ctx, label_false);
            if (else_node) {
                if (else_node->kind == NK_ELSE && else_node->len > 0) {
                    a64_emit_stmt(ctx, else_node->kids[0]);
                } else {
                    a64_emit_stmt(ctx, else_node);
                }
            }
            a64_emit_label(ctx, label_end);
            return;
        }
        case NK_WHILE: {
            int label_cond = a64_new_label(ctx);
            int label_body = a64_new_label(ctx);
            int label_end = a64_new_label(ctx);
            const ChengNode *cond = stmt->len > 0 ? stmt->kids[0] : NULL;
            const ChengNode *body = stmt->len > 1 ? stmt->kids[1] : NULL;
            a64_emit_label(ctx, label_cond);
            a64_emit_cond_branch(ctx, cond, label_body, label_end);
            a64_emit_label(ctx, label_body);
            a64_loop_push(ctx, label_end, label_cond);
            a64_emit_stmt_list(ctx, body);
            a64_loop_pop(ctx);
            fprintf(ctx->out, "  b .L%d\n", label_cond);
            a64_emit_label(ctx, label_end);
            return;
        }
        case NK_FOR:
            a64_emit_for(ctx, stmt);
            return;
        case NK_CASE:
            a64_emit_case(ctx, stmt);
            return;
        case NK_BLOCK: {
            const ChengNode *body = stmt->len > 0 ? stmt->kids[0] : NULL;
            a64_emit_stmt_list(ctx, body);
            return;
        }
        case NK_BREAK: {
            int label = a64_loop_break_label(ctx);
            if (label < 0) {
                a64_add_diag(ctx->diags, ctx->filename, stmt, "break outside loop in asm a64 backend");
                ctx->has_error = 1;
                return;
            }
            fprintf(ctx->out, "  b .L%d\n", label);
            return;
        }
        case NK_CONTINUE: {
            int label = a64_loop_continue_label(ctx);
            if (label < 0) {
                a64_add_diag(ctx->diags, ctx->filename, stmt, "continue outside loop in asm a64 backend");
                ctx->has_error = 1;
                return;
            }
            fprintf(ctx->out, "  b .L%d\n", label);
            return;
        }
        default:
            if (a64_is_expr_kind(stmt->kind)) {
                a64_emit_expr(ctx, stmt);
                return;
            }
            a64_add_diag(ctx->diags, ctx->filename, stmt, "unsupported statement in asm a64 backend");
            ctx->has_error = 1;
            return;
    }
}

static void a64_emit_param_spills(A64EmitContext *ctx, int param_count) {
    for (int param_index = 0; param_index < param_count; param_index++) {
        A64Var *var = &ctx->vars.items[(size_t)param_index];
        int offset = a64_local_offset(ctx, var);
        if (param_index < 8) {
            if (var->type == A64_TYPE_I32) {
                fprintf(ctx->out, "  sxtw x9, w%d\n", param_index);
                fprintf(ctx->out, "  str x9, [sp, #%d]\n", offset);
            } else if (var->type == A64_TYPE_BOOL) {
                fprintf(ctx->out, "  and w9, w%d, #1\n", param_index);
                fprintf(ctx->out, "  str x9, [sp, #%d]\n", offset);
            } else {
                fprintf(ctx->out, "  str x%d, [sp, #%d]\n", param_index, offset);
            }
        } else {
            int stack_offset = 16 + (param_index - 8) * 8;
            if (var->type == A64_TYPE_I32) {
                fprintf(ctx->out, "  ldr w9, [x29, #%d]\n", stack_offset);
                fprintf(ctx->out, "  sxtw x9, w9\n");
            } else if (var->type == A64_TYPE_BOOL) {
                fprintf(ctx->out, "  ldr w9, [x29, #%d]\n", stack_offset);
                fprintf(ctx->out, "  and w9, w9, #1\n");
            } else {
                fprintf(ctx->out, "  ldr x9, [x29, #%d]\n", stack_offset);
            }
            fprintf(ctx->out, "  str x9, [sp, #%d]\n", offset);
        }
    }
}

static int a64_emit_function(A64EmitContext *ctx, const ChengNode *fn) {
    if (!fn || fn->kind != NK_FN_DECL || fn->len < 4) {
        a64_add_diag(ctx->diags, ctx->filename, fn, "asm a64 backend expects function declaration");
        return 0;
    }
    const ChengNode *name_node = fn->kids[0];
    const ChengNode *params = fn->kids[1];
    const ChengNode *ret_type = fn->kids[2];
    const ChengNode *body = fn->kids[3];
    if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
        a64_add_diag(ctx->diags, ctx->filename, fn, "asm a64 backend expects function name");
        return 0;
    }
    ctx->fn_name = name_node->ident;
    ctx->ret_type_expr = ret_type;
    ctx->ret_is_result = a64_type_expr_is_result(ret_type);
    a64_vars_init(&ctx->vars);
    size_t diag_start = ctx->diags ? ctx->diags->len : 0;
    int param_count = a64_collect_params(params, &ctx->vars, ctx->filename, ctx->diags);
    a64_collect_locals_stmt(body, &ctx->vars, ctx->filename, ctx->diags);
    if (ctx->diags && ctx->diags->len > diag_start) {
        a64_vars_free(&ctx->vars);
        return 0;
    }
    int max_call_args = a64_max_call_args(body);
    int max_temp = a64_stmt_max_temp(body);
    if (a64_orc_enabled()) {
        max_temp += 1;
    }
    ctx->temp_slots = max_temp;
    ctx->arg_slots = max_call_args;
    ctx->frame = a64_make_layout((int)ctx->vars.len, max_temp, max_call_args);
    ctx->temp_top = 0;
    ctx->ret_label = a64_new_label(ctx);
    {
        char sym_buf[256];
        const char *sym = a64_mangle_symbol(ctx, ctx->fn_name, sym_buf, sizeof(sym_buf));
        fprintf(ctx->out, ".globl %s\n", sym);
        if (!ctx->mangle_underscore) {
            fprintf(ctx->out, ".type %s, @function\n", sym);
        }
        fprintf(ctx->out, "%s:\n", sym);
    }
    fprintf(ctx->out, "  stp x29, x30, [sp, #-16]!\n");
    fprintf(ctx->out, "  mov x29, sp\n");
    if (ctx->frame.frame_size > 0) {
        fprintf(ctx->out, "  sub sp, sp, #%d\n", ctx->frame.frame_size);
    }
    a64_emit_param_spills(ctx, param_count);
    a64_emit_stmt_list(ctx, body);
    if (!ctx->has_error) {
        fprintf(ctx->out, "  mov w0, #0\n");
        fprintf(ctx->out, "  b .L%d\n", ctx->ret_label);
        a64_emit_label(ctx, ctx->ret_label);
        if (ctx->frame.frame_size > 0) {
            fprintf(ctx->out, "  add sp, sp, #%d\n", ctx->frame.frame_size);
        }
        fprintf(ctx->out, "  ldp x29, x30, [sp], #16\n");
        fprintf(ctx->out, "  ret\n");
        if (!ctx->mangle_underscore) {
            char sym_buf[256];
            const char *sym = a64_mangle_symbol(ctx, ctx->fn_name, sym_buf, sizeof(sym_buf));
            fprintf(ctx->out, ".size %s, .-%s\n", sym, sym);
        }
    }
    a64_vars_free(&ctx->vars);
    free(ctx->loop_break);
    free(ctx->loop_continue);
    ctx->loop_break = NULL;
    ctx->loop_continue = NULL;
    ctx->loop_cap = 0;
    ctx->loop_depth = 0;
    return ctx->has_error ? 0 : 1;
}

static int a64_func_decl_has_body(const ChengNode *decl) {
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

static void a64_func_table_mark_calls(const ChengNode *node,
                                      A64FuncTable *table,
                                      int *changed) {
    if (!node || !table) {
        return;
    }
    if (node->kind == NK_CALL && node->len > 0 && node->kids[0] &&
        node->kids[0]->kind == NK_IDENT && node->kids[0]->ident) {
        A64FuncEntry *entry = a64_func_table_find(table, node->kids[0]->ident);
        if (entry && !entry->emit && a64_func_decl_has_body(entry->decl)) {
            entry->emit = 1;
            if (changed) {
                *changed = 1;
            }
        }
    }
    for (size_t index = 0; index < node->len; index++) {
        a64_func_table_mark_calls(node->kids[index], table, changed);
    }
}

static int a64_func_entry_should_emit(const A64FuncEntry *entry) {
    if (!entry || !entry->emit) {
        return 0;
    }
    return a64_func_decl_has_body(entry->decl);
}

int emit_a64_elf_file(FILE *out,
                      const ChengNode *root,
                      const char *filename,
                      ChengDiagList *diags,
                      AsmLayout *layout,
                      const AsmModuleFilter *module_filter) {
    if (!root || !out || !diags) {
        return -1;
    }
    const ChengNode *list = root;
    if (root->kind == NK_MODULE && root->len > 0) {
        list = root->kids[0];
    }
    if (!list || list->kind != NK_STMT_LIST) {
        a64_add_diag(diags, filename, root, "asm a64 backend expects module statements");
        return -1;
    }
    A64GlobalTypeTable globals;
    a64_global_table_init(&globals);
    A64FuncTable table;
    a64_func_table_init(&table);
    char *module_prefix = a64_module_prefix_from_env(module_filter);
    int rc = -1;
    if (a64_collect_global_types(list, &globals, filename, diags) != 0) {
        goto cleanup;
    }
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (stmt && stmt->kind == NK_FN_DECL) {
            if (a64_func_table_add(&table, stmt) != 0) {
                a64_add_diag(diags, filename, stmt, "failed to collect function declarations");
                goto cleanup;
            }
        }
    }
    if (module_filter) {
        for (size_t index = 0; index < table.len; index++) {
            A64FuncEntry *entry = &table.items[index];
            entry->emit = asm_module_filter_has_func(module_filter, entry->name);
        }
    } else {
        A64FuncEntry *main_entry = a64_func_table_find(&table, "main");
        if (!main_entry) {
            a64_func_table_free(&table);
            a64_global_table_free(&globals);
            a64_add_diag(diags, filename, root, "asm backend requires a main function");
            return -1;
        }
        main_entry->emit = 1;
        int changed = 1;
        while (changed) {
            changed = 0;
            for (size_t index = 0; index < table.len; index++) {
                A64FuncEntry *entry = &table.items[index];
                if (entry->emit && !entry->scanned) {
                    entry->scanned = 1;
                    if (!a64_func_decl_has_body(entry->decl)) {
                        continue;
                    }
                    if (entry->decl && entry->decl->len > 3) {
                        const ChengNode *body = entry->decl->kids[3];
                        a64_func_table_mark_calls(body, &table, &changed);
                    }
                }
            }
        }
    }
    fprintf(out, ".text\n");
    int label_seed = 0;
    for (size_t index = 0; index < table.len; index++) {
        A64FuncEntry *entry = &table.items[index];
        if (!a64_func_entry_should_emit(entry)) {
            continue;
        }
        A64EmitContext ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.out = out;
        ctx.filename = filename;
        ctx.diags = diags;
        ctx.root = root;
        ctx.module_filter = module_filter;
        ctx.module_prefix = module_prefix;
        ctx.mangle_underscore = 0;
        ctx.is_darwin = 0;
        ctx.layout = layout;
        ctx.globals = &globals;
        ctx.loop_break = NULL;
        ctx.loop_continue = NULL;
        ctx.loop_cap = 0;
        ctx.loop_depth = 0;
        ctx.label_id = label_seed;
        if (!a64_emit_function(&ctx, entry->decl)) {
            goto cleanup;
        }
        label_seed = ctx.label_id;
    }
    rc = 0;
cleanup:
    if (module_prefix) {
        free(module_prefix);
    }
    a64_func_table_free(&table);
    a64_global_table_free(&globals);
    return rc;
}

int emit_a64_darwin_file(FILE *out,
                         const ChengNode *root,
                         const char *filename,
                         ChengDiagList *diags,
                         AsmLayout *layout,
                         const AsmModuleFilter *module_filter) {
    if (!root || !out || !diags) {
        return -1;
    }
    const ChengNode *list = root;
    if (root->kind == NK_MODULE && root->len > 0) {
        list = root->kids[0];
    }
    if (!list || list->kind != NK_STMT_LIST) {
        a64_add_diag(diags, filename, root, "asm a64 backend expects module statements");
        return -1;
    }
    A64GlobalTypeTable globals;
    a64_global_table_init(&globals);
    A64FuncTable table;
    a64_func_table_init(&table);
    char *module_prefix = a64_module_prefix_from_env(module_filter);
    int rc = -1;
    if (a64_collect_global_types(list, &globals, filename, diags) != 0) {
        goto cleanup;
    }
    for (size_t index = 0; index < list->len; index++) {
        const ChengNode *stmt = list->kids[index];
        if (stmt && stmt->kind == NK_FN_DECL) {
            if (a64_func_table_add(&table, stmt) != 0) {
                a64_add_diag(diags, filename, stmt, "failed to collect function declarations");
                goto cleanup;
            }
        }
    }
    if (module_filter) {
        for (size_t index = 0; index < table.len; index++) {
            A64FuncEntry *entry = &table.items[index];
            entry->emit = asm_module_filter_has_func(module_filter, entry->name);
        }
    } else {
        A64FuncEntry *main_entry = a64_func_table_find(&table, "main");
        if (!main_entry) {
            a64_func_table_free(&table);
            a64_global_table_free(&globals);
            a64_add_diag(diags, filename, root, "asm backend requires a main function");
            return -1;
        }
        main_entry->emit = 1;
        int changed = 1;
        while (changed) {
            changed = 0;
            for (size_t index = 0; index < table.len; index++) {
                A64FuncEntry *entry = &table.items[index];
                if (entry->emit && !entry->scanned) {
                    entry->scanned = 1;
                    if (!a64_func_decl_has_body(entry->decl)) {
                        continue;
                    }
                    if (entry->decl && entry->decl->len > 3) {
                        const ChengNode *body = entry->decl->kids[3];
                        a64_func_table_mark_calls(body, &table, &changed);
                    }
                }
            }
        }
    }
    fprintf(out, ".section __TEXT,__text,regular,pure_instructions\n");
    int label_seed = 0;
    for (size_t index = 0; index < table.len; index++) {
        A64FuncEntry *entry = &table.items[index];
        if (!a64_func_entry_should_emit(entry)) {
            continue;
        }
        A64EmitContext ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.out = out;
        ctx.filename = filename;
        ctx.diags = diags;
        ctx.root = root;
        ctx.module_filter = module_filter;
        ctx.module_prefix = module_prefix;
        ctx.mangle_underscore = 1;
        ctx.is_darwin = 1;
        ctx.layout = layout;
        ctx.globals = &globals;
        ctx.loop_break = NULL;
        ctx.loop_continue = NULL;
        ctx.loop_cap = 0;
        ctx.loop_depth = 0;
        ctx.label_id = label_seed;
        if (!a64_emit_function(&ctx, entry->decl)) {
            goto cleanup;
        }
        label_seed = ctx.label_id;
    }
    rc = 0;
cleanup:
    if (module_prefix) {
        free(module_prefix);
    }
    a64_func_table_free(&table);
    a64_global_table_free(&globals);
    return rc;
}
