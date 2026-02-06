#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "c_emit.h"
#include "semantics.h"
#include "strlist.h"

static char *cheng_strdup(const char *s);

#define MODULE_INDEX_INVALID ((size_t)-1)

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_init(StrBuf *sb) {
    if (!sb) {
        return;
    }
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_free(StrBuf *sb) {
    if (!sb) {
        return;
    }
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int sb_reserve(StrBuf *sb, size_t extra) {
    if (!sb) {
        return -1;
    }
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) {
        return 0;
    }
    size_t next = sb->cap == 0 ? 32 : sb->cap * 2;
    while (next < need) {
        next *= 2;
    }
    char *data = (char *)realloc(sb->data, next);
    if (!data) {
        return -1;
    }
    sb->data = data;
    sb->cap = next;
    return 0;
}

static int sb_append(StrBuf *sb, const char *text) {
    if (!sb || !text) {
        return -1;
    }
    size_t n = strlen(text);
    if (sb_reserve(sb, n) != 0) {
        return -1;
    }
    memcpy(sb->data + sb->len, text, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 0;
}

static int sb_append_char(StrBuf *sb, char c) {
    if (!sb) {
        return -1;
    }
    if (sb_reserve(sb, 1) != 0) {
        return -1;
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 0;
}

static char *sb_build(StrBuf *sb) {
    if (!sb) {
        return NULL;
    }
    if (!sb->data) {
        return cheng_strdup("");
    }
    char *out = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

typedef struct {
    char *name;
    char *c_name;
} ImportMapEntry;

typedef struct {
    const ChengNode *node;
    char *name;
    char *c_name;
    const ChengNode *params;
    const ChengNode *ret_type;
    int is_importc;
    size_t module_index;
    size_t overload_index;
} FnSig;

typedef struct {
    const ChengNode *stmts;
    char *prefix;
    char *name;
} CEmitModule;

typedef struct {
    const ChengNode *stmt;
    size_t module_index;
} StmtModuleMap;

typedef struct {
    char *name;
    char *c_name;
    size_t module_index;
} TypeMapEntry;

typedef struct {
    char *name;
    const ChengNode *value;
    size_t module_index;
} GlobalInit;

typedef struct {
    char *name;
    char *c_name;
    size_t module_index;
    const ChengNode *type_node;
} GlobalMapEntry;

typedef struct {
    char *name;
    char *c_name;
    size_t module_index;
} EnumFieldMapEntry;

typedef struct {
    char *name;
    int is_str;
    const ChengNode *type_node;
} LocalTypeEntry;

typedef struct {
    FILE *out;
    const char *filename;
    ChengDiagList *diags;
    int indent;
    ImportMapEntry *imports;
    size_t imports_len;
    size_t imports_cap;
    FnSig *fns;
    size_t fns_len;
    size_t fns_cap;
    TypeMapEntry *type_map;
    size_t type_map_len;
    size_t type_map_cap;
    char **type_names;
    size_t type_names_len;
    size_t type_names_cap;
    char **ref_obj_names;
    size_t ref_obj_names_len;
    size_t ref_obj_names_cap;
    char **fwd_names;
    size_t fwd_names_len;
    size_t fwd_names_cap;
    CEmitModule *modules;
    size_t modules_len;
    StmtModuleMap *stmt_modules;
    size_t stmt_modules_len;
    size_t stmt_modules_cap;
    size_t current_module;
    GlobalInit *global_inits;
    size_t global_inits_len;
    size_t global_inits_cap;
    GlobalMapEntry *global_map;
    size_t global_map_len;
    size_t global_map_cap;
    EnumFieldMapEntry *enum_fields;
    size_t enum_fields_len;
    size_t enum_fields_cap;
    LocalTypeEntry *locals;
    size_t locals_len;
    size_t locals_cap;
    char **generic_params;
    size_t generic_params_len;
    size_t generic_params_cap;
    const ChengNode *current_ret_type;
    int current_ret_is_void;
    int current_ret_is_async;
    int current_async_value_kind;
    int emit_module_only;
    int allow_generic_erasure;
} CEmit;

static void add_diag(CEmit *ctx, const ChengNode *node, const char *msg) {
    int line = 1;
    int col = 1;
    if (node) {
        line = node->line;
        col = node->col;
    }
    cheng_diag_add(ctx->diags, CHENG_SV_ERROR, ctx->filename, line, col, msg);
}

static int is_empty_node(const ChengNode *node) {
    return !node || node->kind == NK_EMPTY;
}

static const ChengNode *unwrap_parens(const ChengNode *node);
static size_t call_arg_count(const ChengNode *expr);
static const FnSig *fn_sig_resolve_call(const CEmit *ctx, const char *name, size_t arity);
static int fn_params_match(const ChengNode *a_params, const ChengNode *b_params);
static int type_is_str_node(const ChengNode *type_node);
static int expr_is_str(CEmit *ctx, const ChengNode *expr);
static char *infer_type_key_from_expr(CEmit *ctx, const ChengNode *expr);
static const char *default_value_for_type(const char *ctype);

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

static char *sanitize_ident(const char *name) {
    if (!name) {
        return cheng_strdup("anon");
    }
    size_t n = strlen(name);
    if (n == 0) {
        return cheng_strdup("anon");
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

static char *format_int(long long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", value);
    return cheng_strdup(buf);
}

static char *sanitize_key(const char *s) {
    if (!s) {
        return cheng_strdup("anon");
    }
    StrBuf sb;
    sb_init(&sb);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '_') {
            if (sb_append_char(&sb, (char)c) != 0) {
                sb_free(&sb);
                return NULL;
            }
        } else {
            if (sb_append_char(&sb, '_') != 0) {
                sb_free(&sb);
                return NULL;
            }
        }
    }
    if (sb.len == 0) {
        if (sb_append(&sb, "anon") != 0) {
            sb_free(&sb);
            return NULL;
        }
    }
    return sb_build(&sb);
}

static uint64_t fnv1a64(const char *s) {
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

static const char *path_basename(const char *path) {
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

static char *module_prefix_from_path(const char *path) {
    if (!path) {
        return cheng_strdup("mod");
    }
    const char *base = path_basename(path);
    char *base_key = sanitize_key(base);
    if (!base_key) {
        return cheng_strdup("mod");
    }
    uint64_t h = fnv1a64(path);
    char hash_buf[32];
    snprintf(hash_buf, sizeof(hash_buf), "m%08llx", (unsigned long long)(h & 0xffffffffULL));
    StrBuf sb;
    sb_init(&sb);
    if (sb_append(&sb, base_key) != 0 || sb_append_char(&sb, '_') != 0 || sb_append(&sb, hash_buf) != 0) {
        free(base_key);
        sb_free(&sb);
        return NULL;
    }
    free(base_key);
    char *raw = sb_build(&sb);
    if (!raw) {
        return NULL;
    }
    char *san = sanitize_ident(raw);
    free(raw);
    return san ? san : NULL;
}

static char *module_name_from_path(const char *path) {
    if (!path) {
        return NULL;
    }
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = strlen(base);
    if (len >= 6 && strcmp(base + len - 6, ".cheng") == 0) {
        len -= 6;
    }
    if (len == 0) {
        return cheng_strdup(base);
    }
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

static const char *import_path_value(const ChengNode *node) {
    if (!node) {
        return NULL;
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        return node->ident;
    }
    if (node->kind == NK_STR_LIT && node->str_val) {
        return node->str_val;
    }
    return NULL;
}

static char *import_item_module_name(const ChengNode *item, const char **alias_out) {
    const ChengNode *path_node = item;
    const char *alias = NULL;
    if (item && item->kind == NK_IMPORT_AS && item->len >= 2) {
        path_node = item->kids[0];
        const ChengNode *alias_node = item->kids[1];
        if (alias_node && (alias_node->kind == NK_IDENT || alias_node->kind == NK_SYMBOL) && alias_node->ident) {
            alias = alias_node->ident;
        }
    }
    if (alias_out) {
        *alias_out = alias;
    }
    const char *path_raw = import_path_value(path_node);
    if (!path_raw) {
        return NULL;
    }
    return module_name_from_path(path_raw);
}

static size_t module_index_from_name(const CEmit *ctx, const char *name) {
    if (!ctx || !name || name[0] == '\0') {
        return MODULE_INDEX_INVALID;
    }
    size_t found = MODULE_INDEX_INVALID;
    for (size_t i = 0; i < ctx->modules_len; i++) {
        const char *mod_name = ctx->modules[i].name;
        if (!mod_name || strcmp(mod_name, name) != 0) {
            continue;
        }
        if (found != MODULE_INDEX_INVALID) {
            return MODULE_INDEX_INVALID;
        }
        found = i;
    }
    if (found == MODULE_INDEX_INVALID && strcmp(name, "system") == 0) {
        for (size_t i = 0; i < ctx->modules_len; i++) {
            const char *mod_name = ctx->modules[i].name;
            if (mod_name && strcmp(mod_name, "system_c") == 0) {
                if (found != MODULE_INDEX_INVALID) {
                    return MODULE_INDEX_INVALID;
                }
                found = i;
            }
        }
    }
    return found;
}

static size_t module_index_from_imports(const CEmit *ctx, const ChengNode *node, const char *alias) {
    if (!ctx || !node || !alias || alias[0] == '\0') {
        return MODULE_INDEX_INVALID;
    }
    if (node->kind == NK_IMPORT_STMT || node->kind == NK_IMPORT_GROUP) {
        for (size_t i = 0; i < node->len; i++) {
            const ChengNode *item = node->kids[i];
            if (!item) {
                continue;
            }
            const char *alias_name = NULL;
            char *base = import_item_module_name(item, &alias_name);
            if (!base) {
                continue;
            }
            const char *use_alias = alias_name && alias_name[0] != '\0' ? alias_name : base;
            size_t idx = MODULE_INDEX_INVALID;
            if (use_alias && strcmp(use_alias, alias) == 0) {
                idx = module_index_from_name(ctx, base);
            }
            free(base);
            if (idx != MODULE_INDEX_INVALID) {
                return idx;
            }
        }
        return MODULE_INDEX_INVALID;
    }
    for (size_t i = 0; i < node->len; i++) {
        size_t idx = module_index_from_imports(ctx, node->kids[i], alias);
        if (idx != MODULE_INDEX_INVALID) {
            return idx;
        }
    }
    return MODULE_INDEX_INVALID;
}

static size_t module_index_from_alias(const CEmit *ctx, const char *alias) {
    if (!ctx || !alias || alias[0] == '\0') {
        return MODULE_INDEX_INVALID;
    }
    size_t idx = module_index_from_name(ctx, alias);
    if (idx != MODULE_INDEX_INVALID) {
        return idx;
    }
    if (ctx->current_module == MODULE_INDEX_INVALID || ctx->current_module >= ctx->modules_len) {
        return MODULE_INDEX_INVALID;
    }
    const ChengNode *stmts = ctx->modules[ctx->current_module].stmts;
    if (!stmts) {
        return MODULE_INDEX_INVALID;
    }
    return module_index_from_imports(ctx, stmts, alias);
}

static int stmt_module_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->stmt_modules_len + extra;
    if (need <= ctx->stmt_modules_cap) {
        return 0;
    }
    size_t next = ctx->stmt_modules_cap == 0 ? 64 : ctx->stmt_modules_cap * 2;
    while (next < need) {
        next *= 2;
    }
    StmtModuleMap *items = (StmtModuleMap *)realloc(ctx->stmt_modules, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    ctx->stmt_modules = items;
    ctx->stmt_modules_cap = next;
    return 0;
}

static void stmt_module_add(CEmit *ctx, const ChengNode *stmt, size_t module_index) {
    if (!ctx || !stmt) {
        return;
    }
    if (stmt_module_reserve(ctx, 1) != 0) {
        return;
    }
    ctx->stmt_modules[ctx->stmt_modules_len].stmt = stmt;
    ctx->stmt_modules[ctx->stmt_modules_len].module_index = module_index;
    ctx->stmt_modules_len++;
}

static size_t module_index_for_stmt(const CEmit *ctx, const ChengNode *stmt) {
    if (!ctx || !stmt) {
        return MODULE_INDEX_INVALID;
    }
    for (size_t i = 0; i < ctx->stmt_modules_len; i++) {
        if (ctx->stmt_modules[i].stmt == stmt) {
            return ctx->stmt_modules[i].module_index;
        }
    }
    return MODULE_INDEX_INVALID;
}

static void build_stmt_module_map(CEmit *ctx) {
    if (!ctx || ctx->modules_len == 0) {
        return;
    }
    for (size_t i = 0; i < ctx->modules_len; i++) {
        const ChengNode *stmts = ctx->modules[i].stmts;
        if (!stmts || stmts->kind != NK_STMT_LIST) {
            continue;
        }
        for (size_t j = 0; j < stmts->len; j++) {
            const ChengNode *stmt = stmts->kids[j];
            if (!stmt) {
                continue;
            }
            stmt_module_add(ctx, stmt, i);
        }
    }
}

static const char *module_prefix(const CEmit *ctx, size_t module_index) {
    if (!ctx || module_index == MODULE_INDEX_INVALID || module_index >= ctx->modules_len) {
        return NULL;
    }
    return ctx->modules[module_index].prefix;
}

static const char *normalize_mangle_base(const char *name) {
    if (!name) {
        return "empty";
    }
    if (strcmp(name, "[]") == 0) {
        return "bracket";
    }
    if (strcmp(name, "[]=") == 0) {
        return "bracketEq";
    }
    if (strcmp(name, "+") == 0) {
        return "add";
    }
    if (strcmp(name, "-") == 0) {
        return "sub";
    }
    if (strcmp(name, "*") == 0) {
        return "mul";
    }
    if (strcmp(name, "/") == 0) {
        return "div";
    }
    if (strcmp(name, "%") == 0) {
        return "mod";
    }
    if (strcmp(name, "==") == 0) {
        return "eq";
    }
    if (strcmp(name, "!=") == 0) {
        return "neq";
    }
    if (strcmp(name, "<") == 0) {
        return "lt";
    }
    if (strcmp(name, "<=") == 0) {
        return "le";
    }
    if (strcmp(name, ">") == 0) {
        return "gt";
    }
    if (strcmp(name, ">=") == 0) {
        return "ge";
    }
    if (strcmp(name, "&&") == 0) {
        return "and";
    }
    if (strcmp(name, "||") == 0) {
        return "or";
    }
    if (strcmp(name, "!") == 0) {
        return "not";
    }
    if (strcmp(name, "&") == 0) {
        return "bitand";
    }
    if (strcmp(name, "|") == 0) {
        return "bitor";
    }
    if (strcmp(name, "^") == 0) {
        return "xor";
    }
    if (strcmp(name, "<<") == 0) {
        return "shl";
    }
    if (strcmp(name, ">>") == 0) {
        return "shr";
    }
    if (strcmp(name, "~") == 0) {
        return "bitnot";
    }
    if (strcmp(name, "$") == 0) {
        return "dollar";
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

typedef struct {
    ChengStrList funcs;
    ChengStrList globals;
} CModuleSymbols;

static const ChengNode *c_pattern_ident_node(const ChengNode *pattern) {
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

static int c_is_fn_decl_kind(ChengNodeKind kind) {
    return kind == NK_FN_DECL || kind == NK_ITERATOR_DECL;
}

static CModuleSymbols *c_module_symbols_create(const ChengNode *module_stmts) {
    CModuleSymbols *symbols = (CModuleSymbols *)calloc(1, sizeof(*symbols));
    if (!symbols) {
        return NULL;
    }
    cheng_strlist_init(&symbols->funcs);
    cheng_strlist_init(&symbols->globals);
    if (!module_stmts || module_stmts->kind != NK_STMT_LIST) {
        return symbols;
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
                if (c_is_fn_decl_kind(child->kind) && child->len > 0 &&
                    child->kids[0] && child->kids[0]->kind == NK_IDENT &&
                    child->kids[0]->ident) {
                    cheng_strlist_push_unique(&symbols->funcs, child->kids[0]->ident);
                } else if (child->kind == NK_VAR || child->kind == NK_LET || child->kind == NK_CONST) {
                    const ChengNode *ident = c_pattern_ident_node(child->len > 0 ? child->kids[0] : NULL);
                    if (ident && ident->ident) {
                        cheng_strlist_push_unique(&symbols->globals, ident->ident);
                    }
                }
            }
            continue;
        }
        if (c_is_fn_decl_kind(stmt->kind) && stmt->len > 0 &&
            stmt->kids[0] && stmt->kids[0]->kind == NK_IDENT &&
            stmt->kids[0]->ident) {
            cheng_strlist_push_unique(&symbols->funcs, stmt->kids[0]->ident);
        } else if (stmt->kind == NK_VAR || stmt->kind == NK_LET || stmt->kind == NK_CONST) {
            const ChengNode *ident = c_pattern_ident_node(stmt->len > 0 ? stmt->kids[0] : NULL);
            if (ident && ident->ident) {
                cheng_strlist_push_unique(&symbols->globals, ident->ident);
            }
        }
    }
    return symbols;
}

static void c_module_symbols_free(CModuleSymbols *symbols) {
    if (!symbols) {
        return;
    }
    cheng_strlist_free(&symbols->funcs);
    cheng_strlist_free(&symbols->globals);
    free(symbols);
}

static int c_module_symbols_write(const CModuleSymbols *symbols,
                                  const char *module_path,
                                  const char *out_path,
                                  ChengDiagList *diags,
                                  const char *filename,
                                  const ChengNode *node) {
    if (!symbols || !out_path || out_path[0] == '\0') {
        return 0;
    }
    FILE *out = fopen(out_path, "w");
    if (!out) {
        cheng_diag_add(diags, CHENG_SV_ERROR, filename, node ? node->line : 1, node ? node->col : 1,
                       "failed to write module symbol output");
        return -1;
    }
    fprintf(out, "# cheng modsym v1\n");
    if (module_path && module_path[0] != '\0') {
        fprintf(out, "module=%s\n", module_path);
    }
    for (size_t i = 0; i < symbols->funcs.len; i++) {
        const char *name = symbols->funcs.items[i];
        if (name && name[0] != '\0') {
            fprintf(out, "func %s\n", name);
        }
    }
    for (size_t i = 0; i < symbols->globals.len; i++) {
        const char *name = symbols->globals.items[i];
        if (name && name[0] != '\0') {
            fprintf(out, "global %s\n", name);
        }
    }
    fclose(out);
    return 0;
}

static char *type_key(const ChengNode *node) {
    if (!node) {
        return cheng_strdup("");
    }
    switch (node->kind) {
        case NK_IDENT:
        case NK_SYMBOL:
            return sanitize_key(node->ident);
        case NK_DOT_EXPR:
            if (node->len > 1) {
                return type_key(node->kids[1]);
            }
            return cheng_strdup("");
        case NK_IDENT_DEFS:
            if (node->len > 1) {
                return type_key(node->kids[1]);
            }
            return cheng_strdup("");
        case NK_CALL_ARG:
            if (node->len > 1) {
                return type_key(node->kids[1]);
            }
            if (node->len > 0) {
                return type_key(node->kids[0]);
            }
            return cheng_strdup("");
        case NK_INT_LIT:
            return format_int(node->int_val);
        case NK_PTR_TY:
            if (node->len > 0) {
                char *inner = type_key(node->kids[0]);
                if (!inner) {
                    return cheng_strdup("ptr");
                }
                StrBuf sb;
                sb_init(&sb);
                if (sb_append(&sb, "ptr_") != 0 || sb_append(&sb, inner) != 0) {
                    free(inner);
                    sb_free(&sb);
                    return NULL;
                }
                free(inner);
                return sb_build(&sb);
            }
            return cheng_strdup("ptr");
        case NK_REF_TY:
            if (node->len > 0) {
                char *inner = type_key(node->kids[0]);
                if (!inner) {
                    return cheng_strdup("ref");
                }
                StrBuf sb;
                sb_init(&sb);
                if (sb_append(&sb, "ref_") != 0 || sb_append(&sb, inner) != 0) {
                    free(inner);
                    sb_free(&sb);
                    return NULL;
                }
                free(inner);
                return sb_build(&sb);
            }
            return cheng_strdup("ref");
        case NK_VAR_TY:
            if (node->len > 0) {
                char *inner = type_key(node->kids[0]);
                if (!inner) {
                    return cheng_strdup("var");
                }
                StrBuf sb;
                sb_init(&sb);
                if (sb_append(&sb, "var_") != 0 || sb_append(&sb, inner) != 0) {
                    free(inner);
                    sb_free(&sb);
                    return NULL;
                }
                free(inner);
                return sb_build(&sb);
            }
            return cheng_strdup("var");
        case NK_SET_TY:
            if (node->len > 0) {
                char *inner = type_key(node->kids[0]);
                if (!inner) {
                    return cheng_strdup("set");
                }
                StrBuf sb;
                sb_init(&sb);
                if (sb_append(&sb, "set_") != 0 || sb_append(&sb, inner) != 0) {
                    free(inner);
                    sb_free(&sb);
                    return NULL;
                }
                free(inner);
                return sb_build(&sb);
            }
            return cheng_strdup("set");
        case NK_TUPLE_TY:
        case NK_TUPLE_LIT: {
            StrBuf sb;
            sb_init(&sb);
            if (sb_append(&sb, "tuple") != 0) {
                sb_free(&sb);
                return NULL;
            }
            for (size_t i = 0; i < node->len; i++) {
                char *part = type_key(node->kids[i]);
                if (!part) {
                    continue;
                }
                if (part[0] != '\0') {
                    if (sb_append_char(&sb, '_') != 0 || sb_append(&sb, part) != 0) {
                        free(part);
                        sb_free(&sb);
                        return NULL;
                    }
                }
                free(part);
            }
            return sb_build(&sb);
        }
        case NK_BRACKET_EXPR:
            if (node->len > 0 && node->kids[0]) {
                char *base = type_key(node->kids[0]);
                StrBuf sb;
                sb_init(&sb);
                if (base && base[0] != '\0') {
                    if (sb_append(&sb, base) != 0) {
                        free(base);
                        sb_free(&sb);
                        return NULL;
                    }
                }
                free(base);
                for (size_t i = 1; i < node->len; i++) {
                    char *arg = type_key(node->kids[i]);
                    if (!arg) {
                        continue;
                    }
                    if (arg[0] != '\0') {
                        if (sb_append_char(&sb, '_') != 0 || sb_append(&sb, arg) != 0) {
                            free(arg);
                            sb_free(&sb);
                            return NULL;
                        }
                    }
                    free(arg);
                }
                return sb_build(&sb);
            }
            return cheng_strdup("");
        case NK_FN_TY: {
            StrBuf sb;
            sb_init(&sb);
            if (sb_append(&sb, "fn") != 0) {
                sb_free(&sb);
                return NULL;
            }
            if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_FORMAL_PARAMS) {
                ChengNode *params = node->kids[0];
                for (size_t i = 0; i < params->len; i++) {
                    ChengNode *defs = params->kids[i];
                    if (!defs || defs->kind != NK_IDENT_DEFS) {
                        continue;
                    }
                    const ChengNode *param_type = defs->len > 1 ? defs->kids[1] : NULL;
                    char *key = type_key(param_type);
                    if (!key) {
                        continue;
                    }
                    if (key[0] != '\0') {
                        if (sb_append_char(&sb, '_') != 0 || sb_append(&sb, key) != 0) {
                            free(key);
                            sb_free(&sb);
                            return NULL;
                        }
                    }
                    free(key);
                }
            }
            if (node->len > 1 && node->kids[1]) {
                char *ret_key = type_key(node->kids[1]);
                if (ret_key && ret_key[0] != '\0') {
                    if (sb_append(&sb, "_ret_") != 0 || sb_append(&sb, ret_key) != 0) {
                        free(ret_key);
                        sb_free(&sb);
                        return NULL;
                    }
                }
                free(ret_key);
            }
            return sb_build(&sb);
        }
        default:
            if (node->ident && node->ident[0] != '\0') {
                return sanitize_key(node->ident);
            }
            return cheng_strdup("node");
    }
}

static char *mangle_instance_name(const char *name, const ChengNode *node) {
    const char *base = normalize_mangle_base(name);
    char *base_key = sanitize_key(base);
    if (!base_key) {
        return NULL;
    }
    if (!node || node->len <= 1) {
        return base_key;
    }
    StrBuf sb;
    sb_init(&sb);
    if (sb_append(&sb, base_key) != 0) {
        free(base_key);
        sb_free(&sb);
        return NULL;
    }
    free(base_key);
    for (size_t i = 1; i < node->len; i++) {
        char *arg = type_key(node->kids[i]);
        if (!arg) {
            continue;
        }
        if (arg[0] != '\0') {
            if (sb_append_char(&sb, '_') != 0 || sb_append(&sb, arg) != 0) {
                free(arg);
                sb_free(&sb);
                return NULL;
            }
        }
        free(arg);
    }
    return sb_build(&sb);
}

static void emit_indent(CEmit *ctx) {
    for (int i = 0; i < ctx->indent; i++) {
        fputs("    ", ctx->out);
    }
}

static int import_map_reserve(CEmit *ctx, size_t extra) {
    size_t need = ctx->imports_len + extra;
    if (need <= ctx->imports_cap) {
        return 0;
    }
    size_t next = ctx->imports_cap == 0 ? 8 : ctx->imports_cap * 2;
    while (next < need) {
        next *= 2;
    }
    ImportMapEntry *next_items = (ImportMapEntry *)realloc(ctx->imports, next * sizeof(ImportMapEntry));
    if (!next_items) {
        return -1;
    }
    ctx->imports = next_items;
    ctx->imports_cap = next;
    return 0;
}

static void import_map_set(CEmit *ctx, const char *name, const char *c_name) {
    if (!ctx || !name || !c_name) {
        return;
    }
    for (size_t i = 0; i < ctx->imports_len; i++) {
        if (ctx->imports[i].name && strcmp(ctx->imports[i].name, name) == 0) {
            free(ctx->imports[i].c_name);
            ctx->imports[i].c_name = cheng_strdup(c_name);
            return;
        }
    }
    if (import_map_reserve(ctx, 1) != 0) {
        return;
    }
    ctx->imports[ctx->imports_len].name = cheng_strdup(name);
    ctx->imports[ctx->imports_len].c_name = cheng_strdup(c_name);
    ctx->imports_len++;
}

static const char *import_map_get(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->imports_len; i++) {
        if (ctx->imports[i].name && strcmp(ctx->imports[i].name, name) == 0) {
            return ctx->imports[i].c_name;
        }
    }
    return NULL;
}

static void import_map_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->imports_len; i++) {
        free(ctx->imports[i].name);
        free(ctx->imports[i].c_name);
    }
    free(ctx->imports);
    ctx->imports = NULL;
    ctx->imports_len = 0;
    ctx->imports_cap = 0;
}

static void modules_init(CEmit *ctx, const ChengCModuleInfo *modules, size_t len) {
    if (!ctx || !modules || len == 0) {
        return;
    }
    ctx->modules = (CEmitModule *)calloc(len, sizeof(*ctx->modules));
    if (!ctx->modules) {
        return;
    }
    ctx->modules_len = len;
    for (size_t i = 0; i < len; i++) {
        ctx->modules[i].stmts = modules[i].stmts;
        ctx->modules[i].prefix = module_prefix_from_path(modules[i].path);
        ctx->modules[i].name = module_name_from_path(modules[i].path);
    }
    build_stmt_module_map(ctx);
}

static void modules_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->modules_len; i++) {
        free(ctx->modules[i].prefix);
        free(ctx->modules[i].name);
    }
    free(ctx->modules);
    ctx->modules = NULL;
    ctx->modules_len = 0;
    free(ctx->stmt_modules);
    ctx->stmt_modules = NULL;
    ctx->stmt_modules_len = 0;
    ctx->stmt_modules_cap = 0;
}

static int fn_sig_reserve(CEmit *ctx, size_t extra) {
    size_t need = ctx->fns_len + extra;
    if (need <= ctx->fns_cap) {
        return 0;
    }
    size_t next = ctx->fns_cap == 0 ? 16 : ctx->fns_cap * 2;
    while (next < need) {
        next *= 2;
    }
    FnSig *next_items = (FnSig *)realloc(ctx->fns, next * sizeof(FnSig));
    if (!next_items) {
        return -1;
    }
    ctx->fns = next_items;
    ctx->fns_cap = next;
    return 0;
}

static int fn_has_generics(const ChengNode *node) {
    if (!node || node->kind != NK_FN_DECL) {
        return 0;
    }
    if (node->len > 4 && node->kids[4] &&
        node->kids[4]->kind == NK_GENERIC_PARAMS && node->kids[4]->len > 0) {
        return 1;
    }
    return 0;
}

static const ChengNode *node_generics(const ChengNode *node) {
    if (!node) {
        return NULL;
    }
    for (size_t i = 0; i < node->len; i++) {
        const ChengNode *kid = node->kids[i];
        if (kid && kid->kind == NK_GENERIC_PARAMS && kid->len > 0) {
            return kid;
        }
    }
    return NULL;
}

static const char *type_decl_base_name(const ChengNode *name_node) {
    if (!name_node) {
        return NULL;
    }
    if ((name_node->kind == NK_IDENT || name_node->kind == NK_SYMBOL) && name_node->ident) {
        return name_node->ident;
    }
    if (name_node->kind == NK_BRACKET_EXPR && name_node->len > 0 && name_node->kids[0]) {
        const ChengNode *base = name_node->kids[0];
        if ((base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident) {
            return base->ident;
        }
    }
    return NULL;
}

static char *generic_key_from_params(const char *base, const ChengNode *generics) {
    if (!base || !generics || generics->kind != NK_GENERIC_PARAMS) {
        return NULL;
    }
    char *base_key = sanitize_key(base);
    if (!base_key) {
        return NULL;
    }
    StrBuf sb;
    sb_init(&sb);
    if (sb_append(&sb, base_key) != 0) {
        free(base_key);
        sb_free(&sb);
        return NULL;
    }
    free(base_key);
    for (size_t i = 0; i < generics->len; i++) {
        const ChengNode *defs = generics->kids[i];
        if (!defs || defs->kind != NK_IDENT_DEFS || defs->len == 0) {
            continue;
        }
        const ChengNode *name_node = defs->kids[0];
        if (!name_node || (name_node->kind != NK_IDENT && name_node->kind != NK_SYMBOL) || !name_node->ident) {
            continue;
        }
        char *param_key = sanitize_key(name_node->ident);
        if (!param_key) {
            continue;
        }
        if (param_key[0] != '\0') {
            if (sb_append_char(&sb, '_') != 0 || sb_append(&sb, param_key) != 0) {
                free(param_key);
                sb_free(&sb);
                return NULL;
            }
        }
        free(param_key);
    }
    return sb_build(&sb);
}

static char *type_decl_generic_key(const ChengNode *name_node, const ChengNode *generics, const char *base_name) {
    if (name_node && name_node->kind == NK_BRACKET_EXPR) {
        return type_key(name_node);
    }
    return generic_key_from_params(base_name, generics);
}

static int fn_is_async(const ChengNode *node) {
    if (!node || node->len < 6) {
        return 0;
    }
    const ChengNode *pragma = node->kids[5];
    if (!pragma || pragma->kind != NK_PRAGMA) {
        return 0;
    }
    for (size_t i = 0; i < pragma->len; i++) {
        const ChengNode *ann = pragma->kids[i];
        if (!ann) {
            continue;
        }
        if ((ann->kind == NK_IDENT || ann->kind == NK_SYMBOL) && ann->ident &&
            strcmp(ann->ident, "async") == 0) {
            return 1;
        }
        if (ann->kind == NK_CALL && ann->len > 0 && ann->kids[0] &&
            ann->kids[0]->kind == NK_IDENT && ann->kids[0]->ident &&
            strcmp(ann->kids[0]->ident, "async") == 0) {
            return 1;
        }
        if (ann->kind == NK_ANNOTATION && ann->len > 0 && ann->kids[0] &&
            ann->kids[0]->kind == NK_IDENT && ann->kids[0]->ident &&
            strcmp(ann->kids[0]->ident, "async") == 0) {
            return 1;
        }
    }
    return 0;
}

static ChengNode *async_await_i32_node(void) {
    static ChengNode *node = NULL;
    if (!node) {
        node = cheng_node_new(NK_IDENT, 0, 0);
        cheng_node_set_ident(node, "await_i32");
    }
    return node;
}

static ChengNode *async_await_void_node(void) {
    static ChengNode *node = NULL;
    if (!node) {
        node = cheng_node_new(NK_IDENT, 0, 0);
        cheng_node_set_ident(node, "await_void");
    }
    return node;
}

static int async_value_kind_from_type(const ChengNode *ret_type) {
    if (!ret_type || ret_type->kind == NK_EMPTY) {
        return 2;
    }
    if (ret_type->kind == NK_IDENT && ret_type->ident && strcmp(ret_type->ident, "void") == 0) {
        return 2;
    }
    char *key = type_key(ret_type);
    if (!key) {
        return 0;
    }
    int kind = 0;
    if (strcmp(key, "int32") == 0) {
        kind = 1;
    }
    free(key);
    return kind;
}

static const ChengNode *async_ret_type_node(CEmit *ctx, const ChengNode *ret_type, int *out_kind) {
    int kind = async_value_kind_from_type(ret_type);
    if (out_kind) {
        *out_kind = kind;
    }
    if (kind == 2) {
        return async_await_void_node();
    }
    if (kind == 1) {
        return async_await_i32_node();
    }
    if (ctx) {
        add_diag(ctx, ret_type, "[C-Profile] not-mapped: async return type (FULLC.R03)");
    }
    return ret_type;
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

static void collect_importc_list(CEmit *ctx, const ChengNode *list) {
    if (!ctx || !list || list->kind != NK_STMT_LIST) {
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
                        import_map_set(ctx, last_fn->kids[0]->ident, imp);
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
                import_map_set(ctx, stmt->kids[0]->ident, pending);
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

static void collect_importc(CEmit *ctx, const ChengNode *root) {
    if (!root) {
        return;
    }
    const ChengNode *list = root;
    if (root->kind == NK_MODULE && root->len > 0) {
        list = root->kids[0];
    }
    collect_importc_list(ctx, list);
}

static char *resolve_c_name(CEmit *ctx,
                            const char *name,
                            int is_importc,
                            size_t module_index,
                            size_t overload_index) {
    if (!name) {
        return cheng_strdup("anon");
    }
    const char *base = normalize_mangle_base(name);
    if (strcmp(base, "main") == 0) {
        return cheng_strdup("main");
    }
    if (is_importc) {
        const char *imp = import_map_get(ctx, name);
        if (!imp && strcmp(base, name) != 0) {
            imp = import_map_get(ctx, base);
        }
        if (imp && imp[0] != '\0') {
            return cheng_strdup(imp);
        }
        return sanitize_ident(base);
    }
    char *san = sanitize_ident(base);
    if (!san) {
        return cheng_strdup("anon");
    }
    const char *prefix = module_prefix(ctx, module_index);
    StrBuf sb;
    sb_init(&sb);
    if (prefix && prefix[0] != '\0') {
        if (sb_append(&sb, prefix) != 0 || sb_append(&sb, "__") != 0) {
            free(san);
            sb_free(&sb);
            return cheng_strdup("anon");
        }
    }
    if (sb_append(&sb, san) != 0) {
        free(san);
        sb_free(&sb);
        return cheng_strdup("anon");
    }
    free(san);
    if (overload_index > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "__ovl%zu", overload_index);
        if (sb_append(&sb, buf) != 0) {
            sb_free(&sb);
            return cheng_strdup("anon");
        }
    }
    return sb_build(&sb);
}

static size_t fn_sig_overload_index(const CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name) {
            continue;
        }
        if (sig->module_index == module_index && strcmp(sig->name, name) == 0) {
            count++;
        }
    }
    return count;
}

static const FnSig *fn_sig_find_duplicate(const CEmit *ctx,
                                              const char *name,
                                              const ChengNode *params,
                                              size_t module_index) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name || strcmp(sig->name, name) != 0) {
            continue;
        }
        if (sig->module_index != module_index) {
            continue;
        }
        if (fn_params_match(params, sig->params)) {
            return sig;
        }
    }
    return NULL;
}

static void fn_sig_add(CEmit *ctx,
                         const ChengNode *node,
                         const char *name,
                         const ChengNode *params,
                         const ChengNode *ret_type,
                         int is_importc,
                         size_t module_index) {
    if (!ctx || !name) {
        return;
    }
    const FnSig *dup = fn_sig_find_duplicate(ctx, name, params, module_index);
    if (dup) {
        FnSig *slot = (FnSig *)dup;
        slot->node = node;
        slot->params = params;
        slot->ret_type = ret_type;
        slot->is_importc = is_importc;
        return;
    }
    size_t overload_index = fn_sig_overload_index(ctx, name, module_index);
    if (fn_sig_reserve(ctx, 1) != 0) {
        return;
    }
    FnSig *sig = &ctx->fns[ctx->fns_len++];
    sig->node = node;
    sig->name = cheng_strdup(name);
    sig->is_importc = is_importc;
    sig->params = params;
    sig->ret_type = ret_type;
    sig->module_index = module_index;
    sig->overload_index = overload_index;
    sig->c_name = resolve_c_name(ctx, name, is_importc, module_index, overload_index);
}

static const FnSig *fn_sig_find_in_module(const CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name || strcmp(sig->name, name) != 0) {
            continue;
        }
        if (sig->module_index == module_index) {
            return sig;
        }
    }
    return NULL;
}

static const FnSig *fn_sig_find_by_node(const CEmit *ctx, const ChengNode *node) {
    if (!ctx || !node) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->fns_len; i++) {
        if (ctx->fns[i].node == node) {
            return &ctx->fns[i];
        }
    }
    return NULL;
}

static int fn_params_match(const ChengNode *a_params, const ChengNode *b_params) {
    if (!a_params || !b_params) {
        return 0;
    }
    if (a_params == b_params) {
        return 1;
    }
    if (a_params->kind != NK_FORMAL_PARAMS || b_params->kind != NK_FORMAL_PARAMS) {
        return 0;
    }
    if (a_params->len != b_params->len) {
        return 0;
    }
    for (size_t i = 0; i < a_params->len; i++) {
        const ChengNode *a_defs = a_params->kids[i];
        const ChengNode *b_defs = b_params->kids[i];
        if (!a_defs || !b_defs || a_defs->kind != NK_IDENT_DEFS || b_defs->kind != NK_IDENT_DEFS) {
            return 0;
        }
        const ChengNode *a_type = a_defs->len > 1 ? a_defs->kids[1] : NULL;
        const ChengNode *b_type = b_defs->len > 1 ? b_defs->kids[1] : NULL;
        if (!a_type && !b_type) {
            continue;
        }
        if (!a_type || !b_type) {
            return 0;
        }
        char *a_key = type_key(a_type);
        char *b_key = type_key(b_type);
        int match = a_key && b_key && strcmp(a_key, b_key) == 0;
        free(a_key);
        free(b_key);
        if (!match) {
            return 0;
        }
    }
    return 1;
}

static const FnSig *fn_sig_find_by_decl(const CEmit *ctx, const ChengNode *fn, const char *name) {
    if (!ctx || !fn || !name) {
        return NULL;
    }
    const FnSig *sig = fn_sig_find_by_node(ctx, fn);
    if (sig && sig->name && strcmp(sig->name, name) == 0) {
        return sig;
    }
    const ChengNode *params = fn->len > 1 ? fn->kids[1] : NULL;
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *cur = &ctx->fns[i];
        if (!cur->node || !cur->name) {
            continue;
        }
        if (strcmp(cur->name, name) != 0) {
            continue;
        }
        if (ctx->current_module != MODULE_INDEX_INVALID && cur->module_index != ctx->current_module) {
            continue;
        }
        if (cur->node->line == fn->line && cur->node->col == fn->col) {
            return cur;
        }
        if (params && cur->params && fn_params_match(params, cur->params)) {
            return cur;
        }
    }
    return NULL;
}

static const FnSig *fn_sig_find_unique(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    const FnSig *found = NULL;
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name || strcmp(sig->name, name) != 0) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = sig;
    }
    return found;
}

static const FnSig *fn_sig_find_any(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->fns_len; i++) {
        if (ctx->fns[i].name && strcmp(ctx->fns[i].name, name) == 0) {
            return &ctx->fns[i];
        }
    }
    return NULL;
}

static void fn_sig_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->fns_len; i++) {
        free(ctx->fns[i].name);
        free(ctx->fns[i].c_name);
    }
    free(ctx->fns);
    ctx->fns = NULL;
    ctx->fns_len = 0;
    ctx->fns_cap = 0;
}

static int type_name_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->type_names_len + extra;
    if (need <= ctx->type_names_cap) {
        return 0;
    }
    size_t next = ctx->type_names_cap == 0 ? 16 : ctx->type_names_cap * 2;
    while (next < need) {
        next *= 2;
    }
    char **names = (char **)realloc(ctx->type_names, next * sizeof(*names));
    if (!names) {
        return -1;
    }
    ctx->type_names = names;
    ctx->type_names_cap = next;
    return 0;
}

static int type_name_has(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return 0;
    }
    for (size_t i = 0; i < ctx->type_names_len; i++) {
        if (ctx->type_names[i] && strcmp(ctx->type_names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void type_name_add(CEmit *ctx, const char *name) {
    if (!ctx || !name || name[0] == '\0') {
        return;
    }
    if (type_name_has(ctx, name)) {
        return;
    }
    if (type_name_reserve(ctx, 1) != 0) {
        return;
    }
    ctx->type_names[ctx->type_names_len++] = cheng_strdup(name);
}

static int ref_obj_name_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->ref_obj_names_len + extra;
    if (need <= ctx->ref_obj_names_cap) {
        return 0;
    }
    size_t next = ctx->ref_obj_names_cap == 0 ? 8 : ctx->ref_obj_names_cap * 2;
    while (next < need) {
        next *= 2;
    }
    char **items = (char **)realloc(ctx->ref_obj_names, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    ctx->ref_obj_names = items;
    ctx->ref_obj_names_cap = next;
    return 0;
}

static int ref_obj_name_has(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return 0;
    }
    for (size_t i = 0; i < ctx->ref_obj_names_len; i++) {
        if (ctx->ref_obj_names[i] && strcmp(ctx->ref_obj_names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void ref_obj_name_add(CEmit *ctx, const char *name) {
    if (!ctx || !name || name[0] == '\0') {
        return;
    }
    if (ref_obj_name_has(ctx, name)) {
        return;
    }
    if (ref_obj_name_reserve(ctx, 1) != 0) {
        return;
    }
    ctx->ref_obj_names[ctx->ref_obj_names_len++] = cheng_strdup(name);
}

static void type_name_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->type_names_len; i++) {
        free(ctx->type_names[i]);
    }
    free(ctx->type_names);
    ctx->type_names = NULL;
    ctx->type_names_len = 0;
    ctx->type_names_cap = 0;
}

static void ref_obj_name_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->ref_obj_names_len; i++) {
        free(ctx->ref_obj_names[i]);
    }
    free(ctx->ref_obj_names);
    ctx->ref_obj_names = NULL;
    ctx->ref_obj_names_len = 0;
    ctx->ref_obj_names_cap = 0;
}

static int type_map_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->type_map_len + extra;
    if (need <= ctx->type_map_cap) {
        return 0;
    }
    size_t next = ctx->type_map_cap == 0 ? 16 : ctx->type_map_cap * 2;
    while (next < need) {
        next *= 2;
    }
    TypeMapEntry *items = (TypeMapEntry *)realloc(ctx->type_map, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    ctx->type_map = items;
    ctx->type_map_cap = next;
    return 0;
}

static const TypeMapEntry *type_map_find(const CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->type_map_len; i++) {
        const TypeMapEntry *entry = &ctx->type_map[i];
        if (!entry->name || entry->module_index != module_index) {
            continue;
        }
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

static const TypeMapEntry *type_map_find_unique(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    const TypeMapEntry *found = NULL;
    for (size_t i = 0; i < ctx->type_map_len; i++) {
        const TypeMapEntry *entry = &ctx->type_map[i];
        if (!entry->name || strcmp(entry->name, name) != 0) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = entry;
    }
    return found;
}

static const TypeMapEntry *type_map_find_any_name(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->type_map_len; i++) {
        const TypeMapEntry *entry = &ctx->type_map[i];
        if (!entry->name || strcmp(entry->name, name) != 0) {
            continue;
        }
        return entry;
    }
    return NULL;
}

static const TypeMapEntry *type_map_find_any_name_or_suffix(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    const TypeMapEntry *entry = type_map_find_any_name(ctx, name);
    if (entry) {
        return entry;
    }
    size_t name_len = strlen(name);
    if (name_len == 0) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->type_map_len; i++) {
        const TypeMapEntry *cur = &ctx->type_map[i];
        if (!cur->name) {
            continue;
        }
        size_t cur_len = strlen(cur->name);
        if (cur_len < name_len + 2) {
            continue;
        }
        if (strcmp(cur->name + (cur_len - name_len), name) != 0) {
            continue;
        }
        if (cur->name[cur_len - name_len - 1] != '_' || cur->name[cur_len - name_len - 2] != '_') {
            continue;
        }
        return cur;
    }
    return NULL;
}

static size_t type_map_dup_index(const CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < ctx->type_map_len; i++) {
        const TypeMapEntry *entry = &ctx->type_map[i];
        if (!entry->name || entry->module_index != module_index) {
            continue;
        }
        if (strcmp(entry->name, name) == 0) {
            count++;
        }
    }
    return count;
}

static char *type_map_make_c_name(const CEmit *ctx, const char *name, size_t module_index, size_t dup_index) {
    if (!name) {
        return cheng_strdup("anon");
    }
    char *san = sanitize_ident(name);
    if (!san) {
        return cheng_strdup("anon");
    }
    const char *prefix = module_prefix(ctx, module_index);
    StrBuf sb;
    sb_init(&sb);
    if (prefix && prefix[0] != '\0') {
        if (sb_append(&sb, prefix) != 0 || sb_append(&sb, "__") != 0) {
            free(san);
            sb_free(&sb);
            return cheng_strdup("anon");
        }
    }
    if (sb_append(&sb, san) != 0) {
        free(san);
        sb_free(&sb);
        return cheng_strdup("anon");
    }
    free(san);
    if (dup_index > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "__t%zu", dup_index);
        if (sb_append(&sb, buf) != 0) {
            sb_free(&sb);
            return cheng_strdup("anon");
        }
    }
    return sb_build(&sb);
}

static const char *module_prefix_for_name(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->modules_len; i++) {
        if (!ctx->modules[i].name) {
            continue;
        }
        if (strcmp(ctx->modules[i].name, name) == 0) {
            return ctx->modules[i].prefix;
        }
    }
    return NULL;
}

static void emit_system_helpers_type_aliases(CEmit *ctx) {
    if (!ctx || !ctx->out) {
        return;
    }
    const char *names[] = {"ChengAwaitI32", "ChengAwaitVoid", "ChengChanI32"};
    const char *prefix = module_prefix_for_name(ctx, "async_rt");
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const TypeMapEntry *entry = type_map_find_any_name_or_suffix(ctx, names[i]);
        const char *c_name = entry ? entry->c_name : NULL;
        char *owned = NULL;
        if (!c_name || c_name[0] == '\0') {
            if (prefix && prefix[0] != '\0') {
                StrBuf sb;
                sb_init(&sb);
                if (sb_append(&sb, prefix) != 0 || sb_append(&sb, "__") != 0 ||
                    sb_append(&sb, names[i]) != 0) {
                    sb_free(&sb);
                } else {
                    owned = sb_build(&sb);
                    c_name = owned;
                }
            }
        }
        if (c_name && c_name[0] != '\0') {
            fprintf(ctx->out, "#define %s %s\n", names[i], c_name);
        }
        free(owned);
    }
}

static const char *type_map_add(CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return NULL;
    }
    size_t dup_index = type_map_dup_index(ctx, name, module_index);
    if (type_map_reserve(ctx, 1) != 0) {
        return NULL;
    }
    char *c_name = type_map_make_c_name(ctx, name, module_index, dup_index);
    if (!c_name) {
        return NULL;
    }
    ctx->type_map[ctx->type_map_len].name = cheng_strdup(name);
    ctx->type_map[ctx->type_map_len].c_name = c_name;
    ctx->type_map[ctx->type_map_len].module_index = module_index;
    ctx->type_map_len++;
    return c_name;
}

static const char *type_map_add_alias(CEmit *ctx, const char *name, size_t module_index, const char *c_name) {
    if (!ctx || !name || !c_name) {
        return NULL;
    }
    const TypeMapEntry *existing = type_map_find(ctx, name, module_index);
    if (existing && existing->c_name) {
        return existing->c_name;
    }
    if (type_map_reserve(ctx, 1) != 0) {
        return NULL;
    }
    ctx->type_map[ctx->type_map_len].name = cheng_strdup(name);
    ctx->type_map[ctx->type_map_len].c_name = cheng_strdup(c_name);
    ctx->type_map[ctx->type_map_len].module_index = module_index;
    ctx->type_map_len++;
    return ctx->type_map[ctx->type_map_len - 1].c_name;
}

static void type_map_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->type_map_len; i++) {
        free(ctx->type_map[i].name);
        free(ctx->type_map[i].c_name);
    }
    free(ctx->type_map);
    ctx->type_map = NULL;
    ctx->type_map_len = 0;
    ctx->type_map_cap = 0;
}

static const char *resolve_type_c_name(const CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return NULL;
    }
    const TypeMapEntry *entry = NULL;
    if (module_index != MODULE_INDEX_INVALID) {
        entry = type_map_find(ctx, name, module_index);
    }
    if (!entry) {
        entry = type_map_find_unique(ctx, name);
    }
    return entry ? entry->c_name : NULL;
}

static int global_init_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->global_inits_len + extra;
    if (need <= ctx->global_inits_cap) {
        return 0;
    }
    size_t next = ctx->global_inits_cap == 0 ? 8 : ctx->global_inits_cap * 2;
    while (next < need) {
        next *= 2;
    }
    GlobalInit *items = (GlobalInit *)realloc(ctx->global_inits, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    ctx->global_inits = items;
    ctx->global_inits_cap = next;
    return 0;
}

static void global_init_add(CEmit *ctx, const char *name, const ChengNode *value, size_t module_index) {
    if (!ctx || !name || !value) {
        return;
    }
    if (global_init_reserve(ctx, 1) != 0) {
        return;
    }
    ctx->global_inits[ctx->global_inits_len].name = cheng_strdup(name);
    ctx->global_inits[ctx->global_inits_len].value = value;
    ctx->global_inits[ctx->global_inits_len].module_index = module_index;
    ctx->global_inits_len++;
}

static void global_init_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->global_inits_len; i++) {
        free(ctx->global_inits[i].name);
    }
    free(ctx->global_inits);
    ctx->global_inits = NULL;
    ctx->global_inits_len = 0;
    ctx->global_inits_cap = 0;
}

static int global_map_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->global_map_len + extra;
    if (need <= ctx->global_map_cap) {
        return 0;
    }
    size_t next = ctx->global_map_cap == 0 ? 16 : ctx->global_map_cap * 2;
    while (next < need) {
        next *= 2;
    }
    GlobalMapEntry *items = (GlobalMapEntry *)realloc(ctx->global_map, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    ctx->global_map = items;
    ctx->global_map_cap = next;
    return 0;
}

static const GlobalMapEntry *global_map_find(const CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->global_map_len; i++) {
        const GlobalMapEntry *entry = &ctx->global_map[i];
        if (!entry->name || entry->module_index != module_index) {
            continue;
        }
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

static const GlobalMapEntry *global_map_find_unique(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    const GlobalMapEntry *found = NULL;
    for (size_t i = 0; i < ctx->global_map_len; i++) {
        const GlobalMapEntry *entry = &ctx->global_map[i];
        if (!entry->name || strcmp(entry->name, name) != 0) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = entry;
    }
    return found;
}

static size_t global_map_dup_index(const CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < ctx->global_map_len; i++) {
        const GlobalMapEntry *entry = &ctx->global_map[i];
        if (!entry->name || entry->module_index != module_index) {
            continue;
        }
        if (strcmp(entry->name, name) == 0) {
            count++;
        }
    }
    return count;
}

static char *global_map_make_c_name(const CEmit *ctx, const char *name, size_t module_index, size_t dup_index) {
    if (!name) {
        return cheng_strdup("anon");
    }
    char *san = sanitize_ident(name);
    if (!san) {
        return cheng_strdup("anon");
    }
    const char *prefix = module_prefix(ctx, module_index);
    StrBuf sb;
    sb_init(&sb);
    if (prefix && prefix[0] != '\0') {
        if (sb_append(&sb, prefix) != 0 || sb_append(&sb, "__") != 0) {
            free(san);
            sb_free(&sb);
            return cheng_strdup("anon");
        }
    }
    if (sb_append(&sb, san) != 0) {
        free(san);
        sb_free(&sb);
        return cheng_strdup("anon");
    }
    free(san);
    if (dup_index > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "__g%zu", dup_index);
        if (sb_append(&sb, buf) != 0) {
            sb_free(&sb);
            return cheng_strdup("anon");
        }
    }
    return sb_build(&sb);
}

static const char *global_map_add(CEmit *ctx,
                                  const char *name,
                                  const ChengNode *type_node,
                                  size_t module_index) {
    if (!ctx || !name) {
        return NULL;
    }
    size_t dup_index = global_map_dup_index(ctx, name, module_index);
    if (global_map_reserve(ctx, 1) != 0) {
        return NULL;
    }
    char *c_name = global_map_make_c_name(ctx, name, module_index, dup_index);
    if (!c_name) {
        return NULL;
    }
    ctx->global_map[ctx->global_map_len].name = cheng_strdup(name);
    ctx->global_map[ctx->global_map_len].c_name = c_name;
    ctx->global_map[ctx->global_map_len].module_index = module_index;
    ctx->global_map[ctx->global_map_len].type_node = type_node;
    ctx->global_map_len++;
    return c_name;
}

static void global_map_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->global_map_len; i++) {
        free(ctx->global_map[i].name);
        free(ctx->global_map[i].c_name);
    }
    free(ctx->global_map);
    ctx->global_map = NULL;
    ctx->global_map_len = 0;
    ctx->global_map_cap = 0;
}

static int enum_field_map_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->enum_fields_len + extra;
    if (need <= ctx->enum_fields_cap) {
        return 0;
    }
    size_t next = ctx->enum_fields_cap == 0 ? 16 : ctx->enum_fields_cap * 2;
    while (next < need) {
        next *= 2;
    }
    EnumFieldMapEntry *items = (EnumFieldMapEntry *)realloc(ctx->enum_fields, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    ctx->enum_fields = items;
    ctx->enum_fields_cap = next;
    return 0;
}

static const EnumFieldMapEntry *enum_field_map_find(const CEmit *ctx, const char *name, size_t module_index) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->enum_fields_len; i++) {
        const EnumFieldMapEntry *entry = &ctx->enum_fields[i];
        if (entry->module_index == module_index && entry->name && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

static const EnumFieldMapEntry *enum_field_map_find_unique(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    const EnumFieldMapEntry *found = NULL;
    for (size_t i = 0; i < ctx->enum_fields_len; i++) {
        const EnumFieldMapEntry *entry = &ctx->enum_fields[i];
        if (!entry->name || strcmp(entry->name, name) != 0) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = entry;
    }
    return found;
}

static void enum_field_map_add(CEmit *ctx, const char *name, const char *c_name, size_t module_index) {
    if (!ctx || !name || !c_name || name[0] == '\0' || c_name[0] == '\0') {
        return;
    }
    if (enum_field_map_find(ctx, name, module_index)) {
        return;
    }
    if (enum_field_map_reserve(ctx, 1) != 0) {
        return;
    }
    ctx->enum_fields[ctx->enum_fields_len].name = cheng_strdup(name);
    ctx->enum_fields[ctx->enum_fields_len].c_name = cheng_strdup(c_name);
    ctx->enum_fields[ctx->enum_fields_len].module_index = module_index;
    ctx->enum_fields_len++;
}

static void enum_field_map_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->enum_fields_len; i++) {
        free(ctx->enum_fields[i].name);
        free(ctx->enum_fields[i].c_name);
    }
    free(ctx->enum_fields);
    ctx->enum_fields = NULL;
    ctx->enum_fields_len = 0;
    ctx->enum_fields_cap = 0;
}

static int locals_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->locals_len + extra;
    if (need <= ctx->locals_cap) {
        return 0;
    }
    size_t next = ctx->locals_cap == 0 ? 16 : ctx->locals_cap * 2;
    while (next < need) {
        next *= 2;
    }
    LocalTypeEntry *items = (LocalTypeEntry *)realloc(ctx->locals, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    ctx->locals = items;
    ctx->locals_cap = next;
    return 0;
}

static void locals_set(CEmit *ctx, const char *name, int is_str, const ChengNode *type_node) {
    if (!ctx || !name || name[0] == '\0') {
        return;
    }
    for (size_t i = 0; i < ctx->locals_len; i++) {
        if (ctx->locals[i].name && strcmp(ctx->locals[i].name, name) == 0) {
            ctx->locals[i].is_str = is_str;
            ctx->locals[i].type_node = type_node;
            return;
        }
    }
    if (locals_reserve(ctx, 1) != 0) {
        return;
    }
    ctx->locals[ctx->locals_len].name = cheng_strdup(name);
    ctx->locals[ctx->locals_len].is_str = is_str;
    ctx->locals[ctx->locals_len].type_node = type_node;
    ctx->locals_len++;
}

static int locals_get_is_str(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return 0;
    }
    for (size_t i = 0; i < ctx->locals_len; i++) {
        if (ctx->locals[i].name && strcmp(ctx->locals[i].name, name) == 0) {
            return ctx->locals[i].is_str;
        }
    }
    return 0;
}

static const ChengNode *locals_get_type(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->locals_len; i++) {
        if (ctx->locals[i].name && strcmp(ctx->locals[i].name, name) == 0) {
            return ctx->locals[i].type_node;
        }
    }
    return NULL;
}

static void locals_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->locals_len; i++) {
        free(ctx->locals[i].name);
    }
    free(ctx->locals);
    ctx->locals = NULL;
    ctx->locals_len = 0;
    ctx->locals_cap = 0;
}

static int generic_params_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->generic_params_len + extra;
    if (need <= ctx->generic_params_cap) {
        return 0;
    }
    size_t next = ctx->generic_params_cap == 0 ? 8 : ctx->generic_params_cap * 2;
    while (next < need) {
        next *= 2;
    }
    char **names = (char **)realloc(ctx->generic_params, next * sizeof(*names));
    if (!names) {
        return -1;
    }
    ctx->generic_params = names;
    ctx->generic_params_cap = next;
    return 0;
}

static size_t generic_params_push(CEmit *ctx, const ChengNode *generics) {
    if (!ctx) {
        return 0;
    }
    size_t prev_len = ctx->generic_params_len;
    if (!generics || generics->kind != NK_GENERIC_PARAMS) {
        return prev_len;
    }
    for (size_t i = 0; i < generics->len; i++) {
        const ChengNode *defs = generics->kids[i];
        if (!defs || defs->kind != NK_IDENT_DEFS || defs->len == 0) {
            continue;
        }
        const ChengNode *name_node = defs->kids[0];
        if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
            continue;
        }
        if (generic_params_reserve(ctx, 1) != 0) {
            break;
        }
        ctx->generic_params[ctx->generic_params_len++] = cheng_strdup(name_node->ident);
    }
    return prev_len;
}

static void generic_params_pop(CEmit *ctx, size_t prev_len) {
    if (!ctx) {
        return;
    }
    if (prev_len > ctx->generic_params_len) {
        prev_len = 0;
    }
    for (size_t i = prev_len; i < ctx->generic_params_len; i++) {
        free(ctx->generic_params[i]);
    }
    ctx->generic_params_len = prev_len;
}

static void generic_params_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    generic_params_pop(ctx, 0);
    free(ctx->generic_params);
    ctx->generic_params = NULL;
    ctx->generic_params_cap = 0;
}

static int generic_param_has(const CEmit *ctx, const char *name) {
    if (!ctx || !name || name[0] == '\0') {
        return 0;
    }
    for (size_t i = 0; i < ctx->generic_params_len; i++) {
        if (ctx->generic_params[i] && strcmp(ctx->generic_params[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int fwd_name_reserve(CEmit *ctx, size_t extra) {
    if (!ctx) {
        return -1;
    }
    size_t need = ctx->fwd_names_len + extra;
    if (need <= ctx->fwd_names_cap) {
        return 0;
    }
    size_t next = ctx->fwd_names_cap == 0 ? 16 : ctx->fwd_names_cap * 2;
    while (next < need) {
        next *= 2;
    }
    char **names = (char **)realloc(ctx->fwd_names, next * sizeof(*names));
    if (!names) {
        return -1;
    }
    ctx->fwd_names = names;
    ctx->fwd_names_cap = next;
    return 0;
}

static int fwd_name_has(const CEmit *ctx, const char *name) {
    if (!ctx || !name) {
        return 0;
    }
    for (size_t i = 0; i < ctx->fwd_names_len; i++) {
        if (ctx->fwd_names[i] && strcmp(ctx->fwd_names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void fwd_name_add(CEmit *ctx, const char *name) {
    if (!ctx || !name || name[0] == '\0') {
        return;
    }
    if (fwd_name_has(ctx, name)) {
        return;
    }
    if (fwd_name_reserve(ctx, 1) != 0) {
        return;
    }
    ctx->fwd_names[ctx->fwd_names_len++] = cheng_strdup(name);
}

static void fwd_name_free(CEmit *ctx) {
    if (!ctx) {
        return;
    }
    for (size_t i = 0; i < ctx->fwd_names_len; i++) {
        free(ctx->fwd_names[i]);
    }
    free(ctx->fwd_names);
    ctx->fwd_names = NULL;
    ctx->fwd_names_len = 0;
    ctx->fwd_names_cap = 0;
}

static void collect_fn_sigs(CEmit *ctx, const ChengNode *node, size_t module_index, int report_generics) {
    if (!ctx || !node) {
        return;
    }
    if (node->kind == NK_FN_DECL && node->len >= 3) {
        if (fn_has_generics(node)) {
            if (report_generics) {
                add_diag(ctx, node, "[C-Profile] unsupported: generic fn");
            }
            return;
        }
        const ChengNode *name_node = node->kids[0];
        if (name_node && name_node->kind == NK_IDENT && name_node->ident) {
            int is_importc = import_map_get(ctx, name_node->ident) != NULL;
            const ChengNode *ret_type = node->kids[2];
            if (fn_is_async(node)) {
                ret_type = async_ret_type_node(ctx, ret_type, NULL);
            }
            fn_sig_add(ctx, node, name_node->ident, node->kids[1], ret_type, is_importc, module_index);
        }
    }
    for (size_t i = 0; i < node->len; i++) {
        collect_fn_sigs(ctx, node->kids[i], module_index, report_generics);
    }
}

static void collect_fn_sigs_for_list(CEmit *ctx, const ChengNode *list, int report_generics) {
    if (!ctx || !list || list->kind != NK_STMT_LIST) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        size_t module_index = module_index_for_stmt(ctx, stmt);
        collect_fn_sigs(ctx, stmt, module_index, report_generics);
    }
}

static const ChengNode *ref_object_inner(const ChengNode *type_node);
static char *make_ref_obj_name(const char *c_name);

static void collect_type_entry_names(CEmit *ctx, const ChengNode *entry, size_t module_index) {
    if (!ctx || !entry) {
        return;
    }
    if (entry->kind == NK_TYPE_DECL) {
        if (entry->len == 0) {
            return;
        }
        const ChengNode *name_node = entry->kids[0];
        const char *base_name = type_decl_base_name(name_node);
        if (base_name) {
            const char *c_name = type_map_add(ctx, base_name, module_index);
            if (c_name) {
                type_name_add(ctx, c_name);
                if (ctx->allow_generic_erasure) {
                    const ChengNode *generics = node_generics(entry);
                    if (generics || (name_node && name_node->kind == NK_BRACKET_EXPR)) {
                        char *key = type_decl_generic_key(name_node, generics, base_name);
                        if (key && key[0] != '\0') {
                            type_map_add_alias(ctx, key, module_index, c_name);
                        }
                        free(key);
                    }
                }
                if (entry->len > 1 && entry->kids[1]) {
                    const ChengNode *type_node = entry->kids[1];
                    const ChengNode *alias_node = type_node;
                    if (alias_node && alias_node->kind == NK_PAR && alias_node->len == 1) {
                        alias_node = alias_node->kids[0];
                    }
                    if (type_node->kind == NK_OBJECT_DECL || type_node->kind == NK_TUPLE_TY) {
                        fwd_name_add(ctx, c_name);
                    } else if (ref_object_inner(type_node)) {
                        ref_obj_name_add(ctx, c_name);
                    } else if (alias_node &&
                               (alias_node->kind == NK_PTR_TY ||
                                alias_node->kind == NK_REF_TY ||
                                alias_node->kind == NK_VAR_TY)) {
                        ref_obj_name_add(ctx, c_name);
                    }
                }
            }
        }
        return;
    }
    if (entry->kind == NK_OBJECT_DECL || entry->kind == NK_ENUM_DECL) {
        if (entry->len == 0) {
            return;
        }
        const ChengNode *name_node = entry->kids[0];
        const char *base_name = type_decl_base_name(name_node);
        if (base_name) {
            const char *c_name = type_map_add(ctx, base_name, module_index);
            if (c_name) {
                type_name_add(ctx, c_name);
                if (ctx->allow_generic_erasure) {
                    const ChengNode *generics = node_generics(entry);
                    if (generics || (name_node && name_node->kind == NK_BRACKET_EXPR)) {
                        char *key = type_decl_generic_key(name_node, generics, base_name);
                        if (key && key[0] != '\0') {
                            type_map_add_alias(ctx, key, module_index, c_name);
                        }
                        free(key);
                    }
                }
                if (entry->kind == NK_OBJECT_DECL) {
                    fwd_name_add(ctx, c_name);
                }
            }
        }
        return;
    }
}

static void collect_type_names(CEmit *ctx, const ChengNode *node, size_t module_index) {
    if (!ctx || !node) {
        return;
    }
    if (node->kind == NK_TYPE_DECL) {
        if (node->len > 0 && node->kids[0] &&
            (node->kids[0]->kind == NK_OBJECT_DECL || node->kids[0]->kind == NK_TYPE_DECL)) {
            for (size_t i = 0; i < node->len; i++) {
                collect_type_entry_names(ctx, node->kids[i], module_index);
            }
        } else {
            collect_type_entry_names(ctx, node, module_index);
        }
        return;
    } else if (node->kind == NK_OBJECT_DECL || node->kind == NK_ENUM_DECL) {
        collect_type_entry_names(ctx, node, module_index);
        return;
    }
    for (size_t i = 0; i < node->len; i++) {
        collect_type_names(ctx, node->kids[i], module_index);
    }
}

static void collect_type_names_for_list(CEmit *ctx, const ChengNode *list) {
    if (!ctx || !list || list->kind != NK_STMT_LIST) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        size_t module_index = module_index_for_stmt(ctx, stmt);
        collect_type_names(ctx, stmt, module_index);
    }
}

static void collect_global_names_for_list(CEmit *ctx, const ChengNode *list) {
    if (!ctx || !list || list->kind != NK_STMT_LIST) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        if (stmt->kind != NK_LET && stmt->kind != NK_VAR && stmt->kind != NK_CONST) {
            continue;
        }
        const ChengNode *pattern = stmt->len > 0 ? stmt->kids[0] : NULL;
        if (!pattern || pattern->len == 0 || !pattern->kids[0] || pattern->kids[0]->kind != NK_IDENT) {
            continue;
        }
        const char *name = pattern->kids[0]->ident;
        if (!name) {
            continue;
        }
        size_t module_index = module_index_for_stmt(ctx, stmt);
        if (!global_map_find(ctx, name, module_index)) {
            const ChengNode *type_node = stmt->len > 1 ? stmt->kids[1] : NULL;
            global_map_add(ctx, name, type_node, module_index);
        }
    }
}

static int is_builtin_type_name(const char *name) {
    if (!name) {
        return 0;
    }
    return strcmp(name, "void") == 0 ||
           strcmp(name, "bool") == 0 ||
           strcmp(name, "char") == 0 ||
           strcmp(name, "byte") == 0 ||
           strcmp(name, "int8") == 0 ||
           strcmp(name, "int16") == 0 ||
           strcmp(name, "int32") == 0 ||
           strcmp(name, "int") == 0 ||
           strcmp(name, "int64") == 0 ||
           strcmp(name, "uint8") == 0 ||
           strcmp(name, "uint16") == 0 ||
           strcmp(name, "uint32") == 0 ||
           strcmp(name, "uint") == 0 ||
           strcmp(name, "uint64") == 0 ||
           strcmp(name, "float32") == 0 ||
           strcmp(name, "float") == 0 ||
           strcmp(name, "float64") == 0 ||
           strcmp(name, "cstring") == 0 ||
           strcmp(name, "str") == 0;
}

static int type_is_str_node(const ChengNode *type_node) {
    if (!type_node || is_empty_node(type_node)) {
        return 0;
    }
    const ChengNode *node = type_node;
    if (node->kind == NK_PAR && node->len == 1) {
        node = node->kids[0];
    }
    if (!node) {
        return 0;
    }
    if (node->kind == NK_PTR_TY && node->len > 0) {
        const ChengNode *inner = node->kids[0];
        if (inner && (inner->kind == NK_IDENT || inner->kind == NK_SYMBOL) && inner->ident) {
            return strcmp(inner->ident, "char") == 0;
        }
        return 0;
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        return strcmp(node->ident, "str") == 0 || strcmp(node->ident, "cstring") == 0;
    }
    return 0;
}

static const ChengNode *strip_type_wrappers(const ChengNode *type_node, int *is_ptr) {
    const ChengNode *node = type_node;
    if (is_ptr) {
        *is_ptr = 0;
    }
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_PAR && node->len == 1) {
        node = node->kids[0];
    }
    while (node && (node->kind == NK_VAR_TY || node->kind == NK_REF_TY || node->kind == NK_PTR_TY)) {
        if (is_ptr) {
            *is_ptr = 1;
        }
        node = node->len > 0 ? node->kids[0] : NULL;
        if (node && node->kind == NK_PAR && node->len == 1) {
            node = node->kids[0];
        }
    }
    return node;
}

static const ChengNode *ref_object_inner(const ChengNode *type_node) {
    const ChengNode *node = type_node;
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_PAR && node->len == 1) {
        node = node->kids[0];
    }
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_REF_TY || node->kind == NK_VAR_TY) {
        node = node->len > 0 ? node->kids[0] : NULL;
        if (node && node->kind == NK_PAR && node->len == 1) {
            node = node->kids[0];
        }
    }
    if (node && node->kind == NK_OBJECT_DECL) {
        return node;
    }
    return NULL;
}

static char *make_ref_obj_name(const char *c_name) {
    if (!c_name || c_name[0] == '\0') {
        return NULL;
    }
    size_t n = strlen(c_name) + 5;
    char *out = (char *)malloc(n);
    if (!out) {
        return NULL;
    }
    snprintf(out, n, "%s_Obj", c_name);
    return out;
}

static char *seq_elem_key_from_type(const ChengNode *type_node, int *is_ptr) {
    const ChengNode *node = strip_type_wrappers(type_node, is_ptr);
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_BRACKET_EXPR && node->len > 1) {
        const ChengNode *base = node->kids[0];
        if (base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident &&
            strcmp(base->ident, "seq") == 0) {
            return type_key(node->kids[1]);
        }
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        const char *name = node->ident;
        if (strncmp(name, "seq_", 4) == 0 && name[4] != '\0') {
            return cheng_strdup(name + 4);
        }
    }
    return NULL;
}

static char *seq_elem_key_from_expr(CEmit *ctx, const ChengNode *expr, int *is_ptr) {
    if (!ctx || !expr) {
        return NULL;
    }
    const ChengNode *node = unwrap_parens(expr);
    if (!node) {
        return NULL;
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        const ChengNode *type_node = locals_get_type(ctx, node->ident);
        if (type_node) {
            return seq_elem_key_from_type(type_node, is_ptr);
        }
        const GlobalMapEntry *g = NULL;
        if (ctx->current_module != MODULE_INDEX_INVALID) {
            g = global_map_find(ctx, node->ident, ctx->current_module);
        }
        if (!g) {
            g = global_map_find_unique(ctx, node->ident);
        }
        if (g && g->type_node) {
            return seq_elem_key_from_type(g->type_node, is_ptr);
        }
    }
    return NULL;
}

static const ChengNode *seq_elem_type_from_type(const ChengNode *type_node, int *is_ptr) {
    const ChengNode *node = strip_type_wrappers(type_node, is_ptr);
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_BRACKET_EXPR && node->len > 1) {
        const ChengNode *base = node->kids[0];
        if (base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident) {
            if (strcmp(base->ident, "seq") == 0 || strcmp(base->ident, "seq_fixed") == 0) {
                return node->kids[1];
            }
        }
    }
    return NULL;
}

static const ChengNode *seq_elem_type_from_expr(CEmit *ctx, const ChengNode *expr, int *is_ptr) {
    if (!ctx || !expr) {
        return NULL;
    }
    const ChengNode *node = unwrap_parens(expr);
    if (!node) {
        return NULL;
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        const ChengNode *type_node = locals_get_type(ctx, node->ident);
        if (!type_node) {
            const GlobalMapEntry *g = NULL;
            if (ctx->current_module != MODULE_INDEX_INVALID) {
                g = global_map_find(ctx, node->ident, ctx->current_module);
            }
            if (!g) {
                g = global_map_find_unique(ctx, node->ident);
            }
            if (g) {
                type_node = g->type_node;
            }
        }
        if (type_node) {
            return seq_elem_type_from_type(type_node, is_ptr);
        }
    }
    return NULL;
}

static char *seq_func_name(const char *prefix, const char *elem_key) {
    if (!prefix || !elem_key) {
        return NULL;
    }
    size_t n = strlen(prefix) + 1 + strlen(elem_key) + 1;
    char *out = (char *)malloc(n);
    if (!out) {
        return NULL;
    }
    snprintf(out, n, "%s_%s", prefix, elem_key);
    return out;
}

static void emit_forward_decls(CEmit *ctx) {
    if (!ctx || !ctx->out) {
        return;
    }
    for (size_t i = 0; i < ctx->fwd_names_len; i++) {
        const char *name = ctx->fwd_names[i];
        if (!name || name[0] == '\0') {
            continue;
        }
        char *san = sanitize_ident(name);
        const char *use = san ? san : name;
        fprintf(ctx->out, "typedef struct %s %s;\n", use, use);
        free(san);
    }
    if (ctx->fwd_names_len > 0) {
        fputs("\n", ctx->out);
    }
}

static char *type_to_c(CEmit *ctx, const ChengNode *type_node) {
    if (is_empty_node(type_node)) {
        return cheng_strdup("void");
    }
    if (!type_node) {
        return cheng_strdup("void");
    }
    const ChengNode *node = type_node;
    if (node->kind == NK_PAR && node->len == 1) {
        node = node->kids[0];
    }
    if (!node) {
        return cheng_strdup("NI32");
    }
    if (node->kind == NK_VAR_TY || node->kind == NK_REF_TY || node->kind == NK_PTR_TY) {
        const ChengNode *inner = node->len > 0 ? node->kids[0] : NULL;
        char *base = type_to_c(ctx, inner);
        if (!base) {
            return cheng_strdup("void *");
        }
        size_t n = strlen(base);
        char *out = (char *)malloc(n + 3);
        if (!out) {
            free(base);
            return cheng_strdup("void *");
        }
        snprintf(out, n + 3, "%s*", base);
        free(base);
        return out;
    }
    if (node->kind == NK_IDENT || node->kind == NK_SYMBOL) {
        const char *name = node->ident;
        if (!name) {
            return cheng_strdup("NI32");
        }
        if (strcmp(name, "void") == 0) return cheng_strdup("void");
        if (strcmp(name, "bool") == 0) return cheng_strdup("NB8");
        if (strcmp(name, "char") == 0) return cheng_strdup("NC8");
        if (strcmp(name, "byte") == 0) return cheng_strdup("NU8");
        if (strcmp(name, "int8") == 0) return cheng_strdup("NI8");
        if (strcmp(name, "int16") == 0) return cheng_strdup("NI16");
        if (strcmp(name, "int32") == 0) return cheng_strdup("NI32");
        if (strcmp(name, "int") == 0) return cheng_strdup("NI");
        if (strcmp(name, "int64") == 0) return cheng_strdup("NI64");
        if (strcmp(name, "uint8") == 0) return cheng_strdup("NU8");
        if (strcmp(name, "uint16") == 0) return cheng_strdup("NU16");
        if (strcmp(name, "uint32") == 0) return cheng_strdup("NU32");
        if (strcmp(name, "uint") == 0) return cheng_strdup("NU");
        if (strcmp(name, "uint64") == 0) return cheng_strdup("NU64");
        if (strcmp(name, "float32") == 0 || strcmp(name, "float") == 0) return cheng_strdup("NF32");
        if (strcmp(name, "float64") == 0) return cheng_strdup("NF64");
        if (strcmp(name, "cstring") == 0) return cheng_strdup("const char *");
        if (strcmp(name, "str") == 0) return cheng_strdup("char *");
        const char *mapped = resolve_type_c_name(ctx, name, ctx->current_module);
        if (mapped) {
            return cheng_strdup(mapped);
        }
        if (ctx->allow_generic_erasure && generic_param_has(ctx, name)) {
            return cheng_strdup("void *");
        }
        return sanitize_ident(name);
    }
    if (node->kind == NK_DOT_EXPR && node->len >= 2) {
        return type_to_c(ctx, node->kids[1]);
    }
    if (node->kind == NK_TUPLE_TY) {
        add_diag(ctx, node, "[C-Profile] unsupported: tuple type");
        return cheng_strdup("void *");
    }
    if (node->kind == NK_OBJECT_DECL) {
        if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT && node->kids[0]->ident) {
            const char *mapped = resolve_type_c_name(ctx, node->kids[0]->ident, ctx->current_module);
            if (mapped) {
                return cheng_strdup(mapped);
            }
            return sanitize_ident(node->kids[0]->ident);
        }
        if (getenv("CHENG_C_TRACE_UNSUPPORTED")) {
            fprintf(stderr, "[stage0c-c] inline object type fallback line=%d col=%d\n",
                    node->line, node->col);
        }
        return cheng_strdup("void *");
    }
    if (node->kind == NK_ENUM_DECL) {
        if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT && node->kids[0]->ident) {
            const char *mapped = resolve_type_c_name(ctx, node->kids[0]->ident, ctx->current_module);
            if (mapped) {
                return cheng_strdup(mapped);
            }
            return sanitize_ident(node->kids[0]->ident);
        }
        if (getenv("CHENG_C_TRACE_UNSUPPORTED")) {
            fprintf(stderr, "[stage0c-c] inline enum type fallback line=%d col=%d\n",
                    node->line, node->col);
        }
        return cheng_strdup("void *");
    }
    if (node->kind == NK_SET_TY) {
        return cheng_strdup("void *");
    }
    if (node->kind == NK_FN_TY) {
        return cheng_strdup("void *");
    }
    if (node->kind == NK_BRACKET_EXPR) {
        char *key = type_key(node);
        if (key && key[0] != '\0') {
            const char *mapped = resolve_type_c_name(ctx, key, ctx->current_module);
            if (mapped) {
                free(key);
                return cheng_strdup(mapped);
            }
            if (ctx->allow_generic_erasure && node->len > 0) {
                const ChengNode *base = node->kids[0];
                if (base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident) {
                    const char *base_mapped = resolve_type_c_name(ctx, base->ident, ctx->current_module);
                    if (base_mapped) {
                        free(key);
                        return cheng_strdup(base_mapped);
                    }
                }
            }
            return key;
        }
        free(key);
        if (getenv("CHENG_C_TRACE_UNSUPPORTED")) {
            fprintf(stderr, "[stage0c-c] unsupported type expr kind=%d line=%d col=%d\n",
                    node->kind, node->line, node->col);
        }
        if (!ctx->allow_generic_erasure) {
            add_diag(ctx, node, "[C-Profile] unsupported: generic type");
        }
        return cheng_strdup("void *");
    }
    if (getenv("CHENG_C_TRACE_UNSUPPORTED")) {
        fprintf(stderr, "[stage0c-c] unsupported type expr kind=%d line=%d col=%d\n",
                node->kind, node->line, node->col);
    }
    add_diag(ctx, node, "[C-Profile] unsupported: type expr");
    return cheng_strdup("void *");
}

static char *ctype_from_type_key(CEmit *ctx, const char *key) {
    if (!key || key[0] == '\0') {
        return cheng_strdup("void");
    }
    if (strncmp(key, "ptr_", 4) == 0) {
        char *inner = ctype_from_type_key(ctx, key + 4);
        if (!inner) {
            return cheng_strdup("void *");
        }
        size_t n = strlen(inner);
        char *out = (char *)malloc(n + 3);
        if (!out) {
            free(inner);
            return cheng_strdup("void *");
        }
        snprintf(out, n + 3, "%s*", inner);
        free(inner);
        return out;
    }
    if (strncmp(key, "ref_", 4) == 0) {
        char *inner = ctype_from_type_key(ctx, key + 4);
        if (!inner) {
            return cheng_strdup("void *");
        }
        size_t n = strlen(inner);
        char *out = (char *)malloc(n + 3);
        if (!out) {
            free(inner);
            return cheng_strdup("void *");
        }
        snprintf(out, n + 3, "%s*", inner);
        free(inner);
        return out;
    }
    if (strncmp(key, "var_", 4) == 0) {
        char *inner = ctype_from_type_key(ctx, key + 4);
        if (!inner) {
            return cheng_strdup("void *");
        }
        size_t n = strlen(inner);
        char *out = (char *)malloc(n + 3);
        if (!out) {
            free(inner);
            return cheng_strdup("void *");
        }
        snprintf(out, n + 3, "%s*", inner);
        free(inner);
        return out;
    }
    if (strcmp(key, "void") == 0) return cheng_strdup("void");
    if (strcmp(key, "bool") == 0) return cheng_strdup("NB8");
    if (strcmp(key, "char") == 0) return cheng_strdup("NC8");
    if (strcmp(key, "byte") == 0) return cheng_strdup("NU8");
    if (strcmp(key, "int8") == 0) return cheng_strdup("NI8");
    if (strcmp(key, "int16") == 0) return cheng_strdup("NI16");
    if (strcmp(key, "int32") == 0) return cheng_strdup("NI32");
    if (strcmp(key, "int") == 0) return cheng_strdup("NI");
    if (strcmp(key, "int64") == 0) return cheng_strdup("NI64");
    if (strcmp(key, "uint8") == 0) return cheng_strdup("NU8");
    if (strcmp(key, "uint16") == 0) return cheng_strdup("NU16");
    if (strcmp(key, "uint32") == 0) return cheng_strdup("NU32");
    if (strcmp(key, "uint") == 0) return cheng_strdup("NU");
    if (strcmp(key, "uint64") == 0) return cheng_strdup("NU64");
    if (strcmp(key, "float32") == 0 || strcmp(key, "float") == 0) return cheng_strdup("NF32");
    if (strcmp(key, "float64") == 0) return cheng_strdup("NF64");
    if (strcmp(key, "cstring") == 0) return cheng_strdup("const char *");
    if (strcmp(key, "str") == 0) return cheng_strdup("char *");
    const char *mapped = resolve_type_c_name(ctx, key, MODULE_INDEX_INVALID);
    if (mapped) {
        return cheng_strdup(mapped);
    }
    return sanitize_ident(key);
}

static char *resolve_callee_name(const ChengNode *callee) {
    const ChengNode *node = unwrap_parens(callee);
    if (!node) {
        return NULL;
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        return cheng_strdup(node->ident);
    }
    if (node->kind == NK_DOT_EXPR && node->len >= 2) {
        const ChengNode *member = node->kids[1];
        if (member && (member->kind == NK_IDENT || member->kind == NK_SYMBOL) && member->ident) {
            return cheng_strdup(member->ident);
        }
    }
    if (node->kind == NK_BRACKET_EXPR && node->len > 0) {
        const ChengNode *base = unwrap_parens(node->kids[0]);
        const char *base_name = NULL;
        if (base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident) {
            base_name = base->ident;
        } else if (base && base->kind == NK_DOT_EXPR && base->len >= 2) {
            const ChengNode *member = base->kids[1];
            if (member && (member->kind == NK_IDENT || member->kind == NK_SYMBOL) && member->ident) {
                base_name = member->ident;
            }
        }
        if (base_name) {
            return mangle_instance_name(base_name, node);
        }
    }
    return NULL;
}

static int expr_is_str(CEmit *ctx, const ChengNode *expr) {
    if (!ctx || !expr) {
        return 0;
    }
    const ChengNode *node = unwrap_parens(expr);
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_STR_LIT:
            return 1;
        case NK_IDENT:
        case NK_SYMBOL:
            return node->ident ? locals_get_is_str(ctx, node->ident) : 0;
        case NK_CAST:
            if (node->len > 0 && type_is_str_node(node->kids[0])) {
                return 1;
            }
            if (node->len > 1) {
                return expr_is_str(ctx, node->kids[1]);
            }
            return 0;
        case NK_CALL: {
            if (node->len == 0) {
                return 0;
            }
            char *callee_name = resolve_callee_name(node->kids[0]);
            size_t arity = call_arg_count(node);
            const FnSig *sig = NULL;
            if (callee_name) {
                if (strcmp(callee_name, "load_str") == 0) {
                    free(callee_name);
                    return 1;
                }
                sig = fn_sig_resolve_call(ctx, callee_name, arity);
                if (sig && type_is_str_node(sig->ret_type)) {
                    free(callee_name);
                    return 1;
                }
            }
            free(callee_name);
            return 0;
        }
        case NK_INFIX:
            if (node->len >= 3 && node->kids[0] && node->kids[0]->ident &&
                strcmp(node->kids[0]->ident, "+") == 0) {
                return expr_is_str(ctx, node->kids[1]) || expr_is_str(ctx, node->kids[2]);
            }
            return 0;
        default:
            return 0;
    }
}

static int is_type_call_callee(const CEmit *ctx, const ChengNode *callee) {
    if (!ctx || !callee) {
        return 0;
    }
    const ChengNode *node = unwrap_parens(callee);
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_PTR_TY:
        case NK_REF_TY:
        case NK_VAR_TY:
        case NK_FN_TY:
        case NK_SET_TY:
        case NK_TUPLE_TY:
        case NK_BRACKET_EXPR:
        case NK_OBJECT_DECL:
        case NK_ENUM_DECL:
            return 1;
        case NK_IDENT:
        case NK_SYMBOL:
            if (node->ident && (is_builtin_type_name(node->ident) ||
                                resolve_type_c_name(ctx, node->ident, ctx->current_module))) {
                return 1;
            }
            return 0;
        default:
            return 0;
    }
}

static char *infer_type_from_expr(CEmit *ctx, const ChengNode *expr) {
    if (!expr) {
        return cheng_strdup("NI32");
    }
    if (expr_is_str(ctx, expr)) {
        return cheng_strdup("char *");
    }
    switch (expr->kind) {
        case NK_INT_LIT:
            return cheng_strdup("NI");
        case NK_FLOAT_LIT:
            return cheng_strdup("NF64");
        case NK_BOOL_LIT:
            return cheng_strdup("NB8");
        case NK_CHAR_LIT:
            return cheng_strdup("NC8");
        case NK_STR_LIT:
            return cheng_strdup("char *");
        case NK_NIL_LIT:
            return cheng_strdup("__auto_type");
        case NK_CAST:
            if (expr->len > 0) {
                char *ctype = type_to_c(ctx, expr->kids[0]);
                if (ctype) {
                    return ctype;
                }
            }
            return cheng_strdup("__auto_type");
        case NK_CALL: {
            if (expr->len > 0) {
                const ChengNode *callee = unwrap_parens(expr->kids[0]);
                if (callee && is_type_call_callee(ctx, callee) && call_arg_count(expr) == 1) {
                    char *ctype = type_to_c(ctx, callee);
                    if (ctype) {
                        return ctype;
                    }
                }
                const ChengNode *name_callee = expr->kids[0] ? expr->kids[0] : callee;
                char *name = resolve_callee_name(name_callee);
                if (name) {
                    size_t arity = call_arg_count(expr);
                    const FnSig *sig = fn_sig_resolve_call(ctx, name, arity);
                    if (sig && sig->ret_type && sig->ret_type->kind != NK_EMPTY) {
                        char *ctype = type_to_c(ctx, sig->ret_type);
                        free(name);
                        if (ctype) {
                            return ctype;
                        }
                        return cheng_strdup("__auto_type");
                    }
                    free(name);
                }
            }
            return cheng_strdup("__auto_type");
        }
        default:
            return cheng_strdup("__auto_type");
    }
}

static char *infer_type_key_from_expr(CEmit *ctx, const ChengNode *expr) {
    if (!ctx || !expr) {
        return NULL;
    }
    const ChengNode *node = unwrap_parens(expr);
    if (!node) {
        return NULL;
    }
    switch (node->kind) {
        case NK_INT_LIT:
            return cheng_strdup("int");
        case NK_FLOAT_LIT:
            return cheng_strdup("float64");
        case NK_BOOL_LIT:
            return cheng_strdup("bool");
        case NK_CHAR_LIT:
            return cheng_strdup("char");
        case NK_STR_LIT:
            return cheng_strdup("str");
        case NK_NIL_LIT:
            return NULL;
        case NK_CAST:
            if (node->len > 0) {
                return type_key(node->kids[0]);
            }
            return NULL;
        case NK_CALL: {
            if (node->len > 0) {
                const ChengNode *callee = unwrap_parens(node->kids[0]);
                if (callee && is_type_call_callee(ctx, callee) && call_arg_count(node) == 1) {
                    return type_key(callee);
                }
                const ChengNode *name_callee = node->kids[0] ? node->kids[0] : callee;
                char *name = resolve_callee_name(name_callee);
                if (name) {
                    size_t arity = call_arg_count(node);
                    const FnSig *sig = fn_sig_resolve_call(ctx, name, arity);
                    if (sig && sig->ret_type && sig->ret_type->kind != NK_EMPTY) {
                        char *key = type_key(sig->ret_type);
                        free(name);
                        return key;
                    }
                    free(name);
                }
            }
            return NULL;
        }
        case NK_IDENT:
        case NK_SYMBOL:
            if (node->ident) {
                const ChengNode *type_node = locals_get_type(ctx, node->ident);
                if (!type_node) {
                    const GlobalMapEntry *g = NULL;
                    if (ctx->current_module != MODULE_INDEX_INVALID) {
                        g = global_map_find(ctx, node->ident, ctx->current_module);
                    }
                    if (!g) {
                        g = global_map_find_unique(ctx, node->ident);
                    }
                    if (g) {
                        type_node = g->type_node;
                    }
                }
                if (type_node) {
                    return type_key(type_node);
                }
            }
            return NULL;
        case NK_PREFIX: {
            const char *op = (node->len > 0 && node->kids[0] && node->kids[0]->ident) ? node->kids[0]->ident : "";
            if (strcmp(op, "!") == 0 || strcmp(op, "not") == 0) {
                return cheng_strdup("bool");
            }
            if (strcmp(op, "await") == 0) {
                if (node->len > 1) {
                    char *rhs = infer_type_key_from_expr(ctx, node->kids[1]);
                    if (rhs) {
                        if (strcmp(rhs, "await_i32") == 0) {
                            free(rhs);
                            return cheng_strdup("int32");
                        }
                        if (strcmp(rhs, "await_void") == 0) {
                            free(rhs);
                            return cheng_strdup("void");
                        }
                        free(rhs);
                    }
                }
                return NULL;
            }
            if (node->len > 1) {
                return infer_type_key_from_expr(ctx, node->kids[1]);
            }
            return NULL;
        }
        case NK_INFIX: {
            const char *op = (node->len > 0 && node->kids[0] && node->kids[0]->ident) ? node->kids[0]->ident : "";
            if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 ||
                strcmp(op, "and") == 0 || strcmp(op, "or") == 0 ||
                strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
                strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
                return cheng_strdup("bool");
            }
            if (strcmp(op, "+") == 0 && expr_is_str(ctx, node)) {
                return cheng_strdup("str");
            }
            char *lhs = NULL;
            char *rhs = NULL;
            if (node->len > 1) {
                lhs = infer_type_key_from_expr(ctx, node->kids[1]);
            }
            if (lhs) {
                return lhs;
            }
            if (node->len > 2) {
                rhs = infer_type_key_from_expr(ctx, node->kids[2]);
            }
            if (rhs) {
                return rhs;
            }
            return cheng_strdup("int");
        }
        default:
            return NULL;
    }
}

static void emit_c_string(FILE *out, const char *text) {
    fputc('"', out);
    if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
            unsigned char c = *p;
            switch (c) {
                case '\\': fputs("\\\\", out); break;
                case '"': fputs("\\\"", out); break;
                case '\n': fputs("\\n", out); break;
                case '\r': fputs("\\r", out); break;
                case '\t': fputs("\\t", out); break;
                case '\0': fputs("\\0", out); break;
                default:
                    if (c < 32 || c >= 127) {
                        fprintf(out, "\\x%02x", c);
                    } else {
                        fputc((char)c, out);
                    }
                    break;
            }
        }
    }
    fputc('"', out);
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
    if (len > 0) {
        *out = (unsigned char)text[0];
    }
    return 0;
}

static int type_is_ref_like(const ChengNode *type_node) {
    if (!type_node) {
        return 0;
    }
    if (type_node->kind == NK_VAR_TY || type_node->kind == NK_REF_TY) {
        return 1;
    }
    if (type_node->kind == NK_PTR_TY) {
        return 0;
    }
    if (type_node->kind == NK_PAR && type_node->len == 1) {
        return type_is_ref_like(type_node->kids[0]);
    }
    return 0;
}

static int type_is_ref_alias(const CEmit *ctx, const ChengNode *type_node) {
    if (!ctx || !type_node) {
        return 0;
    }
    const ChengNode *node = type_node;
    if (node->kind == NK_PAR && node->len == 1) {
        node = node->kids[0];
    }
    if (!node) {
        return 0;
    }
    if (node->kind == NK_IDENT || node->kind == NK_SYMBOL) {
        if (!node->ident) {
            return 0;
        }
        const char *mapped = resolve_type_c_name(ctx, node->ident, ctx->current_module);
        if (mapped && ref_obj_name_has(ctx, mapped)) {
            return 1;
        }
        if (ref_obj_name_has(ctx, node->ident)) {
            return 1;
        }
    }
    return 0;
}

static int expr_returns_ref_like(CEmit *ctx, const ChengNode *expr) {
    if (!ctx || !expr) {
        return 0;
    }
    const ChengNode *node = unwrap_parens(expr);
    if (!node) {
        return 0;
    }
    if (node->kind == NK_CALL && node->len > 0) {
        char *name = resolve_callee_name(node->kids[0]);
        if (name) {
            size_t arity = call_arg_count(node);
            const FnSig *sig = fn_sig_resolve_call(ctx, name, arity);
            free(name);
            if (sig && sig->ret_type) {
                int is_ptr = 0;
                strip_type_wrappers(sig->ret_type, &is_ptr);
                if (is_ptr || type_is_ref_alias(ctx, sig->ret_type)) {
                    return 1;
                }
            }
        }
    }
    if (node->kind == NK_CAST && node->len > 0) {
        int is_ptr = 0;
        strip_type_wrappers(node->kids[0], &is_ptr);
        if (is_ptr || type_is_ref_alias(ctx, node->kids[0])) {
            return 1;
        }
    }
    return 0;
}

static int emit_expr(CEmit *ctx, const ChengNode *expr);
static void emit_case_cond(CEmit *ctx, const char *selector, const ChengNode *pat);

static const ChengNode *unwrap_call_arg(const ChengNode *arg) {
    if (!arg) {
        return NULL;
    }
    if (arg->kind != NK_CALL_ARG) {
        return arg;
    }
    if (arg->len > 1) {
        return arg->kids[1];
    }
    if (arg->len > 0) {
        return arg->kids[0];
    }
    return NULL;
}

static const ChengNode *unwrap_parens(const ChengNode *node) {
    const ChengNode *cur = node;
    while (cur && cur->kind == NK_PAR && cur->len == 1) {
        cur = cur->kids[0];
    }
    return cur;
}

static size_t fn_sig_param_count(const FnSig *sig) {
    if (!sig || !sig->params || sig->params->kind != NK_FORMAL_PARAMS) {
        return 0;
    }
    return sig->params->len;
}

static const FnSig *fn_sig_find_in_module_arity(const CEmit *ctx,
                                                    const char *name,
                                                    size_t module_index,
                                                    size_t arity) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name || strcmp(sig->name, name) != 0) {
            continue;
        }
        if (sig->module_index != module_index) {
            continue;
        }
        if (fn_sig_param_count(sig) == arity) {
            return sig;
        }
    }
    return NULL;
}

static const FnSig *fn_sig_find_unique_arity(const CEmit *ctx, const char *name, size_t arity) {
    if (!ctx || !name) {
        return NULL;
    }
    const FnSig *found = NULL;
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name || strcmp(sig->name, name) != 0) {
            continue;
        }
        if (fn_sig_param_count(sig) != arity) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = sig;
    }
    return found;
}

static const FnSig *fn_sig_find_any_arity(const CEmit *ctx, const char *name, size_t arity) {
    if (!ctx || !name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name || strcmp(sig->name, name) != 0) {
            continue;
        }
        if (fn_sig_param_count(sig) == arity) {
            return sig;
        }
    }
    return NULL;
}

static const FnSig *fn_sig_find_c_name_arity(const CEmit *ctx, const char *c_name, size_t arity) {
    if (!ctx || !c_name) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->c_name || strcmp(sig->c_name, c_name) != 0) {
            continue;
        }
        if (fn_sig_param_count(sig) == arity) {
            return sig;
        }
    }
    return NULL;
}

static const FnSig *fn_sig_find_in_module_min_arity(const CEmit *ctx,
                                                        const char *name,
                                                        size_t module_index,
                                                        size_t arity) {
    if (!ctx || !name) {
        return NULL;
    }
    const FnSig *found = NULL;
    size_t best = (size_t)-1;
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name || strcmp(sig->name, name) != 0) {
            continue;
        }
        if (sig->module_index != module_index) {
            continue;
        }
        size_t count = fn_sig_param_count(sig);
        if (count < arity) {
            continue;
        }
        if (count < best) {
            best = count;
            found = sig;
        } else if (count == best) {
            found = NULL;
        }
    }
    return found;
}

static const FnSig *fn_sig_find_min_arity(const CEmit *ctx, const char *name, size_t arity) {
    if (!ctx || !name) {
        return NULL;
    }
    const FnSig *found = NULL;
    size_t best = (size_t)-1;
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->name || strcmp(sig->name, name) != 0) {
            continue;
        }
        size_t count = fn_sig_param_count(sig);
        if (count < arity) {
            continue;
        }
        if (count < best) {
            best = count;
            found = sig;
        } else if (count == best) {
            found = NULL;
        }
    }
    return found;
}

static const FnSig *fn_sig_find_c_name_min_arity(const CEmit *ctx, const char *c_name, size_t arity) {
    if (!ctx || !c_name) {
        return NULL;
    }
    const FnSig *found = NULL;
    size_t best = (size_t)-1;
    for (size_t i = 0; i < ctx->fns_len; i++) {
        const FnSig *sig = &ctx->fns[i];
        if (!sig->c_name || strcmp(sig->c_name, c_name) != 0) {
            continue;
        }
        size_t count = fn_sig_param_count(sig);
        if (count < arity) {
            continue;
        }
        if (count < best) {
            best = count;
            found = sig;
        } else if (count == best) {
            found = NULL;
        }
    }
    return found;
}

static const FnSig *fn_sig_resolve_call(const CEmit *ctx, const char *name, size_t arity) {
    if (!ctx || !name) {
        return NULL;
    }
    const FnSig *sig = NULL;
    if (!sig && ctx->current_module != MODULE_INDEX_INVALID) {
        sig = fn_sig_find_in_module_arity(ctx, name, ctx->current_module, arity);
    }
    if (!sig) {
        sig = fn_sig_find_unique_arity(ctx, name, arity);
    }
    if (!sig) {
        sig = fn_sig_find_any_arity(ctx, name, arity);
    }
    if (!sig) {
        sig = fn_sig_find_c_name_arity(ctx, name, arity);
    }
    if (!sig && ctx->current_module != MODULE_INDEX_INVALID) {
        sig = fn_sig_find_in_module_min_arity(ctx, name, ctx->current_module, arity);
    }
    if (!sig) {
        sig = fn_sig_find_min_arity(ctx, name, arity);
    }
    if (!sig) {
        sig = fn_sig_find_c_name_min_arity(ctx, name, arity);
    }
    return sig;
}

static size_t call_arg_count(const ChengNode *expr) {
    if (!expr || expr->len <= 1) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 1; i < expr->len; i++) {
        const ChengNode *arg = unwrap_call_arg(expr->kids[i]);
        if (arg) {
            count++;
        }
    }
    return count;
}

static void emit_call_arg(CEmit *ctx, const FnSig *sig, size_t arg_index, const ChengNode *arg) {
    int param_is_ref = 0;
    int param_is_ptr = 0;
    int needs_addr = 0;
    int needs_deref = 0;
    const ChengNode *arg_type = NULL;
    int arg_is_ptr = 0;
    int arg_is_ref = 0;
    if (sig && sig->params && sig->params->kind == NK_FORMAL_PARAMS && arg_index < sig->params->len) {
        const ChengNode *defs = sig->params->kids[arg_index];
        if (defs && defs->kind == NK_IDENT_DEFS && defs->len > 1) {
            const ChengNode *type_node = defs->kids[1];
            param_is_ref = type_is_ref_like(type_node);
            strip_type_wrappers(type_node, &param_is_ptr);
            needs_addr = param_is_ref;
        }
    }
    if (arg && arg->kind == NK_IDENT && arg->ident) {
        arg_type = locals_get_type(ctx, arg->ident);
        if (!arg_type) {
            const GlobalMapEntry *g = NULL;
            if (ctx->current_module != MODULE_INDEX_INVALID) {
                g = global_map_find(ctx, arg->ident, ctx->current_module);
            }
            if (!g) {
                g = global_map_find_unique(ctx, arg->ident);
            }
            if (g) {
                arg_type = g->type_node;
            }
        }
        arg_is_ref = type_is_ref_like(arg_type);
        strip_type_wrappers(arg_type, &arg_is_ptr);
        if (!arg_is_ptr && type_is_ref_alias(ctx, arg_type)) {
            arg_is_ptr = 1;
        }
        if (needs_addr && arg_is_ptr) {
            needs_addr = 0;
        }
        if (sig && !param_is_ref && arg_is_ref && !param_is_ptr) {
            needs_deref = 1;
        }
    }
    if (needs_addr && arg && arg->kind == NK_IDENT && arg->ident) {
        fputc('&', ctx->out);
        char *arg_name = sanitize_ident(arg->ident);
        fputs(arg_name ? arg_name : arg->ident, ctx->out);
        free(arg_name);
    } else if (needs_deref && arg && arg->kind == NK_IDENT && arg->ident) {
        fputs("(*", ctx->out);
        char *arg_name = sanitize_ident(arg->ident);
        fputs(arg_name ? arg_name : arg->ident, ctx->out);
        free(arg_name);
        fputc(')', ctx->out);
    } else if (arg) {
        emit_expr(ctx, arg);
    } else {
        fputs("0", ctx->out);
    }
}

static int bracket_name_info(const char *name, int *is_set, const char **suffix) {
    if (!name) {
        return 0;
    }
    if (strcmp(name, "bracket") == 0) {
        if (is_set) {
            *is_set = 0;
        }
        if (suffix) {
            *suffix = NULL;
        }
        return 1;
    }
    if (strcmp(name, "bracketEq") == 0) {
        if (is_set) {
            *is_set = 1;
        }
        if (suffix) {
            *suffix = NULL;
        }
        return 1;
    }
    if (strncmp(name, "bracketEq_", 10) == 0 && name[10] != '\0') {
        if (is_set) {
            *is_set = 1;
        }
        if (suffix) {
            *suffix = name + 10;
        }
        return 1;
    }
    if (strncmp(name, "bracket_", 8) == 0 && name[8] != '\0') {
        if (is_set) {
            *is_set = 0;
        }
        if (suffix) {
            *suffix = name + 8;
        }
        return 1;
    }
    return 0;
}

static int emit_seq_index_expr(CEmit *ctx,
                               const ChengNode *base_arg,
                               const ChengNode *index_arg,
                               const ChengNode *val_arg,
                               const char *elem_ctype,
                               int base_is_ptr,
                               int is_set) {
    if (!ctx || !elem_ctype) {
        return -1;
    }
    if (!base_arg || !index_arg) {
        add_diag(ctx, base_arg ? base_arg : index_arg, "[C-Profile] [] missing arguments");
        return -1;
    }
    fputs("(*(", ctx->out);
    fputs("(", ctx->out);
    fputs(elem_ctype, ctx->out);
    fputs("*)ptr_add(", ctx->out);
    fputc('(', ctx->out);
    emit_expr(ctx, base_arg);
    fputc(')', ctx->out);
    fputs(base_is_ptr ? "->buffer" : ".buffer", ctx->out);
    fputs(", (NI32)((NI64)(", ctx->out);
    emit_expr(ctx, index_arg);
    fputs(") * (NI64)sizeof(", ctx->out);
    fputs(elem_ctype, ctx->out);
    fputs(")))", ctx->out);
    fputs("))", ctx->out);
    if (is_set) {
        if (!val_arg) {
            add_diag(ctx, base_arg, "[C-Profile] []= missing value");
            return -1;
        }
        fputs(" = ", ctx->out);
        emit_expr(ctx, val_arg);
    }
    return 0;
}

static int emit_call_with_name(CEmit *ctx,
                               const char *name,
                               const ChengNode *expr,
                               const ChengNode *prepend_arg,
                               const FnSig *preferred_sig) {
    if (!ctx || !name || !expr) {
        add_diag(ctx, expr, "[C-Profile] unsupported: call target");
        return -1;
    }
    size_t arity = call_arg_count(expr) + (prepend_arg ? 1 : 0);
    int inject_default_zero = 0;
    if (arity == 0 && name && (strncmp(name, "newSeq", 6) == 0 || strstr(name, "__newSeq") != NULL)) {
        inject_default_zero = 1;
        arity = 1;
    }
    const FnSig *sig = preferred_sig ? preferred_sig : fn_sig_resolve_call(ctx, name, arity);
    const char *c_name = name;
    const char *imp = NULL;
    char *owned_name = NULL;
    if (sig && sig->c_name) {
        c_name = sig->c_name;
    } else if ((imp = import_map_get(ctx, name)) != NULL) {
        c_name = imp;
    } else {
        owned_name = resolve_c_name(ctx, name, 0, ctx->current_module, 0);
        if (owned_name) {
            c_name = owned_name;
        } else {
            c_name = normalize_mangle_base(name);
        }
    }
    if (!sig) {
        sig = fn_sig_find_c_name_min_arity(ctx, c_name, arity);
    }
    fprintf(ctx->out, "%s(", c_name);
    if (owned_name) {
        free(owned_name);
        owned_name = NULL;
    }
    size_t arg_index = 0;
    if (prepend_arg) {
        emit_call_arg(ctx, sig, arg_index, prepend_arg);
        arg_index++;
    }
    if (inject_default_zero) {
        if (arg_index > 0) {
            fputs(", ", ctx->out);
        }
        fputs("0", ctx->out);
        arg_index++;
    }
    for (size_t i = 1; i < expr->len; i++) {
        const ChengNode *arg = unwrap_call_arg(expr->kids[i]);
        if (!arg) {
            continue;
        }
        if (arg_index > 0) {
            fputs(", ", ctx->out);
        }
        emit_call_arg(ctx, sig, arg_index, arg);
        arg_index++;
    }
    if (sig && sig->params && sig->params->kind == NK_FORMAL_PARAMS) {
        while (arg_index < sig->params->len) {
            const ChengNode *defs = sig->params->kids[arg_index];
            const ChengNode *ptype = NULL;
            if (defs && defs->kind == NK_IDENT_DEFS && defs->len > 1) {
                ptype = defs->kids[1];
            }
            char *ctype = type_to_c(ctx, ptype);
            if (arg_index > 0) {
                fputs(", ", ctx->out);
            }
            fputs(default_value_for_type(ctype), ctx->out);
            free(ctype);
            arg_index++;
        }
    }
    fputc(')', ctx->out);
    return 0;
}

static int emit_call_intrinsic(CEmit *ctx, const char *c_name, const ChengNode *expr) {
    if (!ctx || !c_name || !expr) {
        return -1;
    }
    fputs(c_name, ctx->out);
    fputc('(', ctx->out);
    size_t arg_index = 0;
    for (size_t i = 1; i < expr->len; i++) {
        const ChengNode *arg = unwrap_call_arg(expr->kids[i]);
        if (!arg) {
            continue;
        }
        if (arg_index > 0) {
            fputs(", ", ctx->out);
        }
        emit_expr(ctx, arg);
        arg_index++;
    }
    fputc(')', ctx->out);
    return 0;
}

static int emit_infix_call(CEmit *ctx, const char *name, const ChengNode *lhs, const ChengNode *rhs) {
    if (!ctx || !name) {
        return -1;
    }
    const FnSig *sig = fn_sig_resolve_call(ctx, name, 2);
    const char *c_name = name;
    const char *imp = NULL;
    char *owned_name = NULL;
    if (sig && sig->c_name) {
        c_name = sig->c_name;
    } else if ((imp = import_map_get(ctx, name)) != NULL) {
        c_name = imp;
    } else {
        owned_name = resolve_c_name(ctx, name, 0, ctx->current_module, 0);
        if (owned_name) {
            c_name = owned_name;
        } else {
            c_name = normalize_mangle_base(name);
        }
    }
    fprintf(ctx->out, "%s(", c_name);
    if (owned_name) {
        free(owned_name);
    }
    emit_call_arg(ctx, sig, 0, lhs);
    fputs(", ", ctx->out);
    emit_call_arg(ctx, sig, 1, rhs);
    fputc(')', ctx->out);
    return 0;
}

static int emit_intrinsic_chr(CEmit *ctx, const ChengNode *expr) {
    if (!ctx || !expr) {
        return -1;
    }
    if (call_arg_count(expr) != 1) {
        add_diag(ctx, expr, "[C-Profile] chr requires one argument");
        return -1;
    }
    const ChengNode *arg = unwrap_call_arg(expr->kids[1]);
    if (!arg) {
        add_diag(ctx, expr, "[C-Profile] chr missing argument");
        return -1;
    }
    fputs("(NC8)(", ctx->out);
    emit_expr(ctx, arg);
    fputc(')', ctx->out);
    return 0;
}

static const char *intrinsic_callee_name(const char *name) {
    if (!name) {
        return NULL;
    }
    if (strcmp(name, "alloc") == 0) return "cheng_malloc";
    if (strcmp(name, "realloc") == 0) return "cheng_realloc";
    if (strcmp(name, "free") == 0) return "cheng_free";
    if (strcmp(name, "dealloc") == 0) return "cheng_free";
    if (strcmp(name, "copyMem") == 0) return "cheng_memcpy";
    if (strcmp(name, "setMem") == 0) return "cheng_memset";
    if (strcmp(name, "cmpMem") == 0) return "cheng_memcmp";
    if (strcmp(name, "c_memcpy") == 0) return "cheng_memcpy";
    if (strcmp(name, "c_memset") == 0) return "cheng_memset";
    if (strcmp(name, "c_strlen") == 0) return "cheng_strlen";
    if (strcmp(name, "c_strcmp") == 0) return "cheng_strcmp";
    if (strcmp(name, "ptr_add") == 0) return "ptr_add";
    if (strcmp(name, "load_ptr") == 0) return "load_ptr";
    if (strcmp(name, "store_ptr") == 0) return "store_ptr";
    if (strcmp(name, "load_str") == 0) return "load_ptr";
    if (strcmp(name, "store_str") == 0) return "store_ptr";
    return NULL;
}

static int emit_intrinsic_zero_mem(CEmit *ctx, const ChengNode *expr) {
    if (!ctx || !expr) {
        return -1;
    }
    if (call_arg_count(expr) != 2) {
        add_diag(ctx, expr, "[C-Profile] zeroMem requires two arguments");
        return -1;
    }
    const ChengNode *dest = unwrap_call_arg(expr->kids[1]);
    const ChengNode *size = unwrap_call_arg(expr->kids[2]);
    if (!dest || !size) {
        add_diag(ctx, expr, "[C-Profile] zeroMem missing arguments");
        return -1;
    }
    fputs("cheng_memset(", ctx->out);
    emit_expr(ctx, dest);
    fputs(", 0, ", ctx->out);
    emit_expr(ctx, size);
    fputc(')', ctx->out);
    return 0;
}

static int emit_intrinsic_load(CEmit *ctx, const ChengNode *callee, const ChengNode *expr) {
    if (!ctx || !callee || !expr) {
        return -1;
    }
    if (call_arg_count(expr) != 1) {
        add_diag(ctx, expr, "[C-Profile] load requires one argument");
        return -1;
    }
    const ChengNode *type_arg = NULL;
    if (callee->kind == NK_BRACKET_EXPR && callee->len > 1) {
        type_arg = callee->kids[1];
    }
    if (!type_arg) {
        add_diag(ctx, expr, "[C-Profile] load requires a type argument");
        return -1;
    }
    const ChengNode *arg = unwrap_call_arg(expr->kids[1]);
    if (!arg) {
        add_diag(ctx, expr, "[C-Profile] load missing argument");
        return -1;
    }
    char *ctype = type_to_c(ctx, type_arg);
    fputs("*((", ctx->out);
    fputs(ctype ? ctype : "void *", ctx->out);
    fputs("*)", ctx->out);
    emit_expr(ctx, arg);
    fputc(')', ctx->out);
    free(ctype);
    return 0;
}

static int emit_intrinsic_store(CEmit *ctx, const ChengNode *callee, const ChengNode *expr) {
    if (!ctx || !callee || !expr) {
        return -1;
    }
    if (call_arg_count(expr) != 2) {
        add_diag(ctx, expr, "[C-Profile] store requires two arguments");
        return -1;
    }
    const ChengNode *type_arg = NULL;
    if (callee->kind == NK_BRACKET_EXPR && callee->len > 1) {
        type_arg = callee->kids[1];
    }
    if (!type_arg) {
        add_diag(ctx, expr, "[C-Profile] store requires a type argument");
        return -1;
    }
    const ChengNode *ptr_arg = unwrap_call_arg(expr->kids[1]);
    const ChengNode *val_arg = unwrap_call_arg(expr->kids[2]);
    if (!ptr_arg || !val_arg) {
        add_diag(ctx, expr, "[C-Profile] store missing arguments");
        return -1;
    }
    char *ctype = type_to_c(ctx, type_arg);
    fputs("(*(", ctx->out);
    fputs(ctype ? ctype : "void *", ctx->out);
    fputs("*)", ctx->out);
    emit_expr(ctx, ptr_arg);
    fputs(") = ", ctx->out);
    emit_expr(ctx, val_arg);
    free(ctype);
    return 0;
}

static int emit_call_by_expr(CEmit *ctx, const ChengNode *callee, const ChengNode *expr) {
    fputc('(', ctx->out);
    emit_expr(ctx, callee);
    fputs(")(", ctx->out);
    size_t arg_index = 0;
    for (size_t i = 1; i < expr->len; i++) {
        const ChengNode *arg = unwrap_call_arg(expr->kids[i]);
        if (!arg) {
            continue;
        }
        if (arg_index > 0) {
            fputs(", ", ctx->out);
        }
        emit_expr(ctx, arg);
        arg_index++;
    }
    fputc(')', ctx->out);
    return 0;
}

static int emit_cast_call(CEmit *ctx, const ChengNode *callee, const ChengNode *expr) {
    if (!expr || expr->len < 2) {
        add_diag(ctx, expr, "[C-Profile] unsupported: type cast");
        return -1;
    }
    const ChengNode *arg = unwrap_call_arg(expr->kids[1]);
    if (!arg) {
        add_diag(ctx, expr, "[C-Profile] unsupported: type cast arg");
        return -1;
    }
    char *ctype = type_to_c(ctx, callee);
    fputc('(', ctx->out);
    fputs(ctype ? ctype : "void *", ctx->out);
    fputc(')', ctx->out);
    fputc('(', ctx->out);
    emit_expr(ctx, arg);
    fputc(')', ctx->out);
    free(ctype);
    return 0;
}

static int emit_intrinsic_type_call(CEmit *ctx, const char *name, const ChengNode *expr) {
    if (!ctx || !name || !expr) {
        add_diag(ctx, expr, "[C-Profile] unsupported: intrinsic type call");
        return -1;
    }
    if (call_arg_count(expr) != 1) {
        if (strcmp(name, "default") == 0) {
            add_diag(ctx, expr, "[C-Profile] default requires one type argument");
        } else {
            add_diag(ctx, expr, "[C-Profile] sizeof/alignof requires one type argument");
        }
        return -1;
    }
    const ChengNode *arg = expr->kids[1];
    if (arg && arg->kind == NK_CALL_ARG) {
        if (arg->len > 1) {
            add_diag(ctx, expr, "[C-Profile] intrinsic type call does not support named arguments");
            return -1;
        }
        arg = arg->len > 0 ? arg->kids[0] : NULL;
    }
    if (!arg) {
        add_diag(ctx, expr, "[C-Profile] intrinsic type call missing type");
        return -1;
    }
    int is_type_arg = is_type_call_callee(ctx, arg);
    if (strcmp(name, "default") == 0) {
        if (!is_type_arg) {
            add_diag(ctx, expr, "[C-Profile] default requires a type argument");
            return -1;
        }
        char *ctype = type_to_c(ctx, arg);
        fputs("((", ctx->out);
        fputs(ctype ? ctype : "void *", ctx->out);
        fputs("){0})", ctx->out);
        free(ctype);
        return 0;
    }
    if (strcmp(name, "alignof") == 0) {
        fputs("__alignof__(", ctx->out);
        if (is_type_arg) {
            char *ctype = type_to_c(ctx, arg);
            fputs(ctype ? ctype : "void *", ctx->out);
            free(ctype);
        } else {
            emit_expr(ctx, arg);
        }
        fputc(')', ctx->out);
        return 0;
    }
    fputs("sizeof(", ctx->out);
    if (is_type_arg) {
        char *ctype = type_to_c(ctx, arg);
        fputs(ctype ? ctype : "void *", ctx->out);
        free(ctype);
    } else {
        emit_expr(ctx, arg);
    }
    fputc(')', ctx->out);
    return 0;
}

static int emit_call(CEmit *ctx, const ChengNode *expr) {
    const ChengNode *raw_callee = expr->kids[0];
    const ChengNode *callee = unwrap_parens(raw_callee);
    if (!callee) {
        add_diag(ctx, expr, "[C-Profile] unsupported: call target");
        return -1;
    }
    if (callee->kind == NK_PREFIX && callee->len > 1 && callee->kids[0]) {
        const ChengNode *op = callee->kids[0];
        const char *op_name = op->ident ? op->ident : "";
        if (strcmp(op_name, "!") == 0 || strcmp(op_name, "not") == 0) {
            int rc = 0;
            fputs("!(", ctx->out);
            const ChengNode *inner = callee->kids[1];
            if (inner && (inner->kind == NK_IDENT || inner->kind == NK_SYMBOL) && inner->ident) {
                rc = emit_call_with_name(ctx, inner->ident, expr, NULL, NULL);
            } else if (inner) {
                rc = emit_call_by_expr(ctx, inner, expr);
            } else {
                add_diag(ctx, expr, "[C-Profile] unsupported: call target");
                rc = -1;
            }
            fputc(')', ctx->out);
            return rc;
        }
    }
    if ((callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) && callee->ident) {
        if (strcmp(callee->ident, "sizeof") == 0 ||
            strcmp(callee->ident, "alignof") == 0 ||
            strcmp(callee->ident, "default") == 0) {
            return emit_intrinsic_type_call(ctx, callee->ident, expr);
        }
        if (strcmp(callee->ident, "chr") == 0) {
            return emit_intrinsic_chr(ctx, expr);
        }
        if (strcmp(callee->ident, "[]") == 0 || strcmp(callee->ident, "[]=") == 0) {
            const ChengNode *base_arg = expr->len > 1 ? unwrap_call_arg(expr->kids[1]) : NULL;
            const ChengNode *index_arg = expr->len > 2 ? unwrap_call_arg(expr->kids[2]) : NULL;
            const ChengNode *val_arg = expr->len > 3 ? unwrap_call_arg(expr->kids[3]) : NULL;
            int base_is_ptr = 0;
            const ChengNode *elem_type = seq_elem_type_from_expr(ctx, base_arg, &base_is_ptr);
            char *elem_ctype = NULL;
            if (elem_type) {
                elem_ctype = type_to_c(ctx, elem_type);
            } else {
                char *elem_key = seq_elem_key_from_expr(ctx, base_arg, &base_is_ptr);
                if (elem_key) {
                    elem_ctype = ctype_from_type_key(ctx, elem_key);
                    free(elem_key);
                }
            }
            if (elem_ctype) {
                int rc = emit_seq_index_expr(ctx,
                                             base_arg,
                                             index_arg,
                                             val_arg,
                                             elem_ctype,
                                             base_is_ptr,
                                             strcmp(callee->ident, "[]=") == 0);
                free(elem_ctype);
                return rc;
            }
        }
        int bracket_is_set = 0;
        const char *bracket_suffix = NULL;
        if (bracket_name_info(callee->ident, &bracket_is_set, &bracket_suffix)) {
            const ChengNode *base_arg = expr->len > 1 ? unwrap_call_arg(expr->kids[1]) : NULL;
            const ChengNode *index_arg = expr->len > 2 ? unwrap_call_arg(expr->kids[2]) : NULL;
            const ChengNode *val_arg = expr->len > 3 ? unwrap_call_arg(expr->kids[3]) : NULL;
            int base_is_ptr = 0;
            const ChengNode *elem_type = seq_elem_type_from_expr(ctx, base_arg, &base_is_ptr);
            char *elem_ctype = NULL;
            if (elem_type) {
                elem_ctype = type_to_c(ctx, elem_type);
            } else if (bracket_suffix) {
                elem_ctype = ctype_from_type_key(ctx, bracket_suffix);
            } else {
                char *elem_key = seq_elem_key_from_expr(ctx, base_arg, &base_is_ptr);
                if (elem_key) {
                    elem_ctype = ctype_from_type_key(ctx, elem_key);
                    free(elem_key);
                }
            }
            if (elem_ctype) {
                int rc = emit_seq_index_expr(ctx,
                                             base_arg,
                                             index_arg,
                                             val_arg,
                                             elem_ctype,
                                             base_is_ptr,
                                             bracket_is_set);
                free(elem_ctype);
                return rc;
            }
        }
        if (strcmp(callee->ident, "zeroMem") == 0) {
            return emit_intrinsic_zero_mem(ctx, expr);
        }
        const char *intrinsic = intrinsic_callee_name(callee->ident);
        if (intrinsic) {
            return emit_call_intrinsic(ctx, intrinsic, expr);
        }
    }
    if (callee->kind == NK_BRACKET_EXPR && callee->len > 0) {
        const ChengNode *base = unwrap_parens(callee->kids[0]);
        const char *base_name = NULL;
        if (base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident) {
            base_name = base->ident;
        } else if (base && base->kind == NK_DOT_EXPR && base->len >= 2) {
            const ChengNode *member = base->kids[1];
            if (member && (member->kind == NK_IDENT || member->kind == NK_SYMBOL) && member->ident) {
                base_name = member->ident;
            }
        }
        if (base_name) {
            if (strcmp(base_name, "load") == 0) {
                return emit_intrinsic_load(ctx, callee, expr);
            }
            if (strcmp(base_name, "store") == 0) {
                return emit_intrinsic_store(ctx, callee, expr);
            }
        }
    }
    if (call_arg_count(expr) == 1 && is_type_call_callee(ctx, callee)) {
        return emit_cast_call(ctx, callee, expr);
    }
    if (callee->kind == NK_DOT_EXPR && callee->len >= 2) {
        const ChengNode *base = callee->kids[0];
        const ChengNode *member = callee->kids[1];
        if (member && (member->kind == NK_IDENT || member->kind == NK_SYMBOL) && member->ident) {
            if (base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident) {
                size_t mod_index = module_index_from_alias(ctx, base->ident);
                if (mod_index != MODULE_INDEX_INVALID) {
                    size_t argc = call_arg_count(expr);
                    const FnSig *sig = fn_sig_find_in_module_arity(ctx, member->ident, mod_index, argc);
                    if (!sig) {
                        sig = fn_sig_find_in_module_min_arity(ctx, member->ident, mod_index, argc);
                    }
                    size_t saved_module = ctx->current_module;
                    ctx->current_module = mod_index;
                    int rc = emit_call_with_name(ctx, member->ident, expr, NULL, sig);
                    ctx->current_module = saved_module;
                    return rc;
                }
            }
            if (strcmp(member->ident, "add") == 0) {
                int base_is_ptr = 0;
                char *elem_key = seq_elem_key_from_expr(ctx, base, &base_is_ptr);
                if (elem_key) {
                    char *fname = seq_func_name("addPtr", elem_key);
                    free(elem_key);
                    if (fname) {
                        fputs(fname, ctx->out);
                        fputc('(', ctx->out);
                        if (!base_is_ptr) {
                            fputc('&', ctx->out);
                        }
                        emit_expr(ctx, base);
                        size_t arg_index = 0;
                        for (size_t i = 1; i < expr->len; i++) {
                            const ChengNode *arg = unwrap_call_arg(expr->kids[i]);
                            if (!arg) {
                                continue;
                            }
                            if (arg_index >= 0) {
                                fputs(", ", ctx->out);
                            }
                            emit_expr(ctx, arg);
                            arg_index++;
                        }
                        fputc(')', ctx->out);
                        free(fname);
                        return 0;
                    }
                }
            }
            if (strcmp(member->ident, "zeroMem") == 0) {
                return emit_intrinsic_zero_mem(ctx, expr);
            }
            const char *intrinsic = intrinsic_callee_name(member->ident);
            if (intrinsic) {
                return emit_call_intrinsic(ctx, intrinsic, expr);
            }
            size_t argc = call_arg_count(expr);
            const FnSig *sig_method = fn_sig_resolve_call(ctx, member->ident, argc + 1);
            if (sig_method) {
                return emit_call_with_name(ctx, member->ident, expr, base, sig_method);
            }
            const FnSig *sig_plain = fn_sig_resolve_call(ctx, member->ident, argc);
            if (sig_plain) {
                return emit_call_with_name(ctx, member->ident, expr, NULL, sig_plain);
            }
            return emit_call_with_name(ctx, member->ident, expr, base, NULL);
        }
        return emit_call_by_expr(ctx, raw_callee, expr);
    }
    if (callee->kind == NK_BRACKET_EXPR && callee->len > 0) {
        const ChengNode *base = unwrap_parens(callee->kids[0]);
        const char *base_name = NULL;
        const ChengNode *prepend = NULL;
        if (base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident) {
            base_name = base->ident;
        } else if (base && base->kind == NK_DOT_EXPR && base->len >= 2) {
            const ChengNode *member = base->kids[1];
            if (member && (member->kind == NK_IDENT || member->kind == NK_SYMBOL) && member->ident) {
                base_name = member->ident;
                prepend = base->kids[0];
            }
        }
        if (base_name) {
            char *name = mangle_instance_name(base_name, callee);
            int rc = emit_call_with_name(ctx, name ? name : base_name, expr, prepend, NULL);
            free(name);
            return rc;
        }
        return emit_call_by_expr(ctx, raw_callee, expr);
    }
    if ((callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) && callee->ident) {
        return emit_call_with_name(ctx, callee->ident, expr, NULL, NULL);
    }
    return emit_call_by_expr(ctx, raw_callee, expr);
}

static int emit_array_literal(CEmit *ctx, const ChengNode *expr) {
    if (!ctx || !expr) {
        fputs("0", ctx->out);
        return 0;
    }
    char *elem_key = NULL;
    for (size_t i = 0; i < expr->len; i++) {
        elem_key = infer_type_key_from_expr(ctx, expr->kids[i]);
        if (elem_key && elem_key[0] != '\0') {
            break;
        }
        free(elem_key);
        elem_key = NULL;
    }
    if (!elem_key || elem_key[0] == '\0') {
        free(elem_key);
        add_diag(ctx, expr, "[C-Profile] unsupported: array literal element type");
        fputs("0", ctx->out);
        return -1;
    }
    char *elem_ctype = ctype_from_type_key(ctx, elem_key);
    size_t slice_n = strlen(elem_key) + 7;
    char *slice_name = (char *)malloc(slice_n);
    if (!slice_name) {
        free(elem_key);
        free(elem_ctype);
        add_diag(ctx, expr, "[C-Profile] out of memory");
        fputs("0", ctx->out);
        return -1;
    }
    snprintf(slice_name, slice_n, "slice_%s", elem_key);
    const char *slice_c = resolve_type_c_name(ctx, slice_name, ctx->current_module);
    const char *slice_use = slice_c ? slice_c : slice_name;
    char *slice_san = sanitize_ident(slice_use);
    fputs("((", ctx->out);
    fputs(slice_san ? slice_san : slice_use, ctx->out);
    fputs("){ .ptr = ", ctx->out);
    if (expr->len > 0) {
        fputc('(', ctx->out);
        fputs(elem_ctype ? elem_ctype : "NI32", ctx->out);
        fputs("[]){", ctx->out);
        for (size_t i = 0; i < expr->len; i++) {
            emit_expr(ctx, expr->kids[i]);
            if (i + 1 < expr->len) {
                fputs(", ", ctx->out);
            }
        }
        fputc('}', ctx->out);
    } else {
        fputs("CHENG_NIL", ctx->out);
    }
    fputs(", .len = ", ctx->out);
    fprintf(ctx->out, "%lld", (long long)expr->len);
    fputs(" })", ctx->out);
    free(elem_key);
    free(elem_ctype);
    free(slice_name);
    free(slice_san);
    return 0;
}

static int emit_expr(CEmit *ctx, const ChengNode *expr) {
    if (!expr) {
        fputs("0", ctx->out);
        return 0;
    }
    switch (expr->kind) {
        case NK_IDENT:
        case NK_SYMBOL: {
            if (expr->ident) {
                const char *use = expr->ident;
                const GlobalMapEntry *g = NULL;
                const EnumFieldMapEntry *ef = NULL;
                if (ctx->current_module != MODULE_INDEX_INVALID) {
                    g = global_map_find(ctx, expr->ident, ctx->current_module);
                }
                if (!g) {
                    g = global_map_find_unique(ctx, expr->ident);
                }
                if (!g) {
                    if (ctx->current_module != MODULE_INDEX_INVALID) {
                        ef = enum_field_map_find(ctx, expr->ident, ctx->current_module);
                    }
                    if (!ef) {
                        ef = enum_field_map_find_unique(ctx, expr->ident);
                    }
                }
                if (g && g->c_name) {
                    use = g->c_name;
                } else if (ef && ef->c_name) {
                    use = ef->c_name;
                } else if (!locals_get_type(ctx, expr->ident)) {
                    const FnSig *sig = NULL;
                    if (ctx->current_module != MODULE_INDEX_INVALID) {
                        sig = fn_sig_find_in_module(ctx, expr->ident, ctx->current_module);
                    }
                    if (!sig) {
                        sig = fn_sig_find_unique(ctx, expr->ident);
                    }
                    if (!sig) {
                        sig = fn_sig_find_any(ctx, expr->ident);
                    }
                    if (sig && sig->c_name) {
                        use = sig->c_name;
                    }
                }
                char *name = sanitize_ident(use);
                fputs(name ? name : use, ctx->out);
                free(name);
            } else {
                fputs("0", ctx->out);
            }
            return 0;
        }
        case NK_INT_LIT:
            fprintf(ctx->out, "%lld", (long long)expr->int_val);
            return 0;
        case NK_FLOAT_LIT:
            fprintf(ctx->out, "%0.17g", expr->float_val);
            return 0;
        case NK_BOOL_LIT:
            fputs(expr->int_val ? "1" : "0", ctx->out);
            return 0;
        case NK_CHAR_LIT: {
            int val = 0;
            decode_char_lit(expr->str_val, &val);
            fprintf(ctx->out, "%d", val);
            return 0;
        }
        case NK_STR_LIT:
            emit_c_string(ctx->out, expr->str_val);
            return 0;
        case NK_NIL_LIT:
            fputs("CHENG_NIL", ctx->out);
            return 0;
        case NK_INFIX: {
            if (expr->len < 3 || !expr->kids[0]) {
                add_diag(ctx, expr, "[C-Profile] unsupported: infix");
                return -1;
            }
            const char *op = expr->kids[0]->ident ? expr->kids[0]->ident : "";
            if (strcmp(op, "&") == 0) {
                const ChengNode *lhs = expr->kids[1];
                const ChengNode *rhs = expr->kids[2];
                if (lhs && (lhs->kind == NK_IDENT || lhs->kind == NK_SYMBOL) && lhs->ident) {
                    const FnSig *sig = fn_sig_resolve_call(ctx, lhs->ident, 1);
                    if (sig) {
                        const char *c_name = lhs->ident;
                        const char *imp = NULL;
                        char *owned_name = NULL;
                        if (sig->c_name) {
                            c_name = sig->c_name;
                        } else if ((imp = import_map_get(ctx, lhs->ident)) != NULL) {
                            c_name = imp;
                        } else {
                            owned_name = resolve_c_name(ctx, lhs->ident, 0, ctx->current_module, 0);
                            if (owned_name) {
                                c_name = owned_name;
                            } else {
                                c_name = normalize_mangle_base(lhs->ident);
                            }
                        }
                        fprintf(ctx->out, "%s(", c_name);
                        if (rhs && rhs->kind == NK_PREFIX && rhs->len > 1 && rhs->kids[0] &&
                            rhs->kids[0]->ident && strcmp(rhs->kids[0]->ident, "&") == 0) {
                            emit_expr(ctx, rhs);
                        } else {
                            fputc('&', ctx->out);
                            emit_expr(ctx, rhs);
                        }
                        fputc(')', ctx->out);
                        free(owned_name);
                        return 0;
                    }
                }
            }
            if ((strcmp(op, "+") == 0 || strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) &&
                (expr_is_str(ctx, expr->kids[1]) || expr_is_str(ctx, expr->kids[2]))) {
                if (strcmp(op, "!=") == 0) {
                    if (fn_sig_resolve_call(ctx, "!=", 2)) {
                        return emit_infix_call(ctx, "!=", expr->kids[1], expr->kids[2]);
                    }
                    fputs("!(", ctx->out);
                    emit_infix_call(ctx, "==", expr->kids[1], expr->kids[2]);
                    fputc(')', ctx->out);
                    return 0;
                }
                return emit_infix_call(ctx, op, expr->kids[1], expr->kids[2]);
            }
            const char *use = op;
            if (strcmp(op, "and") == 0) use = "&&";
            else if (strcmp(op, "or") == 0) use = "||";
            else if (strcmp(op, "xor") == 0) use = "^";
            else if (strcmp(op, "mod") == 0) use = "%";
            if (strcmp(op, "..") == 0 || strcmp(op, "..<") == 0) {
                add_diag(ctx, expr, "[C-Profile] unsupported: range expr");
                return -1;
            }
            fputc('(', ctx->out);
            emit_expr(ctx, expr->kids[1]);
            fprintf(ctx->out, " %s ", use);
            emit_expr(ctx, expr->kids[2]);
            fputc(')', ctx->out);
            return 0;
        }
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0]) {
                add_diag(ctx, expr, "[C-Profile] unsupported: prefix");
                return -1;
            }
            const char *op = expr->kids[0]->ident ? expr->kids[0]->ident : "";
            if (strcmp(op, "&") == 0 && expr->len > 1) {
                const ChengNode *rhs = expr->kids[1];
                if (rhs && (rhs->kind == NK_IDENT || rhs->kind == NK_SYMBOL) && rhs->ident) {
                    const char *use = rhs->ident;
                    const GlobalMapEntry *g = NULL;
                    const EnumFieldMapEntry *ef = NULL;
                    const FnSig *sig = NULL;
                    if (!locals_get_type(ctx, rhs->ident)) {
                        if (ctx->current_module != MODULE_INDEX_INVALID) {
                            g = global_map_find(ctx, rhs->ident, ctx->current_module);
                        }
                        if (!g) {
                            g = global_map_find_unique(ctx, rhs->ident);
                        }
                        if (!g) {
                            if (ctx->current_module != MODULE_INDEX_INVALID) {
                                ef = enum_field_map_find(ctx, rhs->ident, ctx->current_module);
                            }
                            if (!ef) {
                                ef = enum_field_map_find_unique(ctx, rhs->ident);
                            }
                        }
                        if (!g && !ef) {
                            if (ctx->current_module != MODULE_INDEX_INVALID) {
                                sig = fn_sig_find_in_module(ctx, rhs->ident, ctx->current_module);
                            }
                            if (!sig) {
                                sig = fn_sig_find_unique(ctx, rhs->ident);
                            }
                            if (!sig) {
                                sig = fn_sig_find_any(ctx, rhs->ident);
                            }
                        }
                    }
                    if (g && g->c_name) {
                        use = g->c_name;
                    } else if (ef && ef->c_name) {
                        use = ef->c_name;
                    } else if (sig && sig->c_name) {
                        use = sig->c_name;
                    }
                    char *name = sanitize_ident(use);
                    fprintf(ctx->out, "(&%s)", name ? name : use);
                    free(name);
                    return 0;
                }
            }
            if (strcmp(op, "await") == 0) {
                const char *fn_name = NULL;
                if (expr->len > 1) {
                    char *rhs = infer_type_key_from_expr(ctx, expr->kids[1]);
                    if (rhs) {
                        if (strcmp(rhs, "await_i32") == 0) {
                            fn_name = "cheng_await_i32";
                        } else if (strcmp(rhs, "await_void") == 0) {
                            fn_name = "cheng_await_void";
                        }
                        free(rhs);
                    }
                }
                if (!fn_name) {
                    add_diag(ctx, expr, "[C-Profile] not-mapped: await type (FULLC.R03)");
                    fputs("0", ctx->out);
                    return -1;
                }
                fputs(fn_name, ctx->out);
                fputc('(', ctx->out);
                emit_expr(ctx, expr->kids[1]);
                fputc(')', ctx->out);
                return 0;
            }
            const char *use = op;
            if (strcmp(op, "not") == 0) {
                use = "!";
            }
            fputc('(', ctx->out);
            fputs(use, ctx->out);
            emit_expr(ctx, expr->kids[1]);
            fputc(')', ctx->out);
            return 0;
        }
        case NK_CALL:
            return emit_call(ctx, expr);
        case NK_DOT_EXPR: {
            if (expr->len < 2) {
                add_diag(ctx, expr, "[C-Profile] unsupported: dot expr");
                return -1;
            }
            const ChengNode *base = expr->kids[0];
            const ChengNode *member = expr->kids[1];
            int use_arrow = 0;
            if (base && (base->kind == NK_HIDDEN_DEREF || base->kind == NK_DEREF_EXPR) && base->len > 0) {
                use_arrow = 1;
                base = base->kids[0];
            }
            if (!member || member->kind != NK_IDENT || !member->ident) {
                add_diag(ctx, expr, "[C-Profile] unsupported: dot expr");
                return -1;
            }
            fputc('(', ctx->out);
            emit_expr(ctx, base);
            if (!use_arrow && base && (base->type_id == CHENG_TYPE_PTR || base->type_id == CHENG_TYPE_REF)) {
                use_arrow = 1;
            }
            if (!use_arrow && base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident) {
                int base_is_ptr = 0;
                const ChengNode *type_node = locals_get_type(ctx, base->ident);
                if (!type_node) {
                    const GlobalMapEntry *g = NULL;
                    if (ctx->current_module != MODULE_INDEX_INVALID) {
                        g = global_map_find(ctx, base->ident, ctx->current_module);
                    }
                    if (!g) {
                        g = global_map_find_unique(ctx, base->ident);
                    }
                    if (g) {
                        type_node = g->type_node;
                    }
                }
                strip_type_wrappers(type_node, &base_is_ptr);
                if (base_is_ptr || type_is_ref_alias(ctx, type_node)) {
                    use_arrow = 1;
                }
            }
            if (!use_arrow && base && expr_returns_ref_like(ctx, base)) {
                use_arrow = 1;
            }
            if (use_arrow) {
                fputs("->", ctx->out);
            } else {
                fputc('.', ctx->out);
            }
            char *field = sanitize_ident(member->ident);
            fputs(field ? field : member->ident, ctx->out);
            free(field);
            fputc(')', ctx->out);
            return 0;
        }
        case NK_HIDDEN_DEREF:
        case NK_DEREF_EXPR:
            if (expr->len > 0) {
                fputs("(*", ctx->out);
                emit_expr(ctx, expr->kids[0]);
                fputc(')', ctx->out);
                return 0;
            }
            fputs("0", ctx->out);
            return 0;
        case NK_CAST: {
            if (expr->len < 2) {
                add_diag(ctx, expr, "[C-Profile] unsupported: cast");
                return -1;
            }
            char *ctype = type_to_c(ctx, expr->kids[0]);
            fputc('(', ctx->out);
            fputc('(', ctx->out);
            fputs(ctype ? ctype : "void*", ctx->out);
            fputc(')', ctx->out);
            fputc('(', ctx->out);
            emit_expr(ctx, expr->kids[1]);
            fputc(')', ctx->out);
            fputc(')', ctx->out);
            free(ctype);
            return 0;
        }
        case NK_PAR:
            if (expr->len > 0) {
                fputc('(', ctx->out);
                emit_expr(ctx, expr->kids[0]);
                fputc(')', ctx->out);
                return 0;
            }
            fputs("0", ctx->out);
            return 0;
        case NK_BRACKET:
            return emit_array_literal(ctx, expr);
        case NK_BRACKET_EXPR:
            if (expr->len > 0 && expr->kids[0] &&
                (expr->kids[0]->kind == NK_IDENT || expr->kids[0]->kind == NK_SYMBOL) &&
                expr->kids[0]->ident && strcmp(expr->kids[0]->ident, "default") == 0) {
                if (expr->len != 2) {
                    add_diag(ctx, expr, "[C-Profile] default requires one type argument");
                    return -1;
                }
                const ChengNode *arg = expr->kids[1];
                if (!arg || !is_type_call_callee(ctx, arg)) {
                    add_diag(ctx, expr, "[C-Profile] default requires a type argument");
                    return -1;
                }
                char *ctype = type_to_c(ctx, arg);
                fputs("((", ctx->out);
                fputs(ctype ? ctype : "void *", ctx->out);
                fputs("){0})", ctx->out);
                free(ctype);
                return 0;
            }
            if (expr->len < 2) {
                add_diag(ctx, expr, "[C-Profile] unsupported: index expr");
                return -1;
            }
            fputc('(', ctx->out);
            emit_expr(ctx, expr->kids[0]);
            fputc(')', ctx->out);
            for (size_t i = 1; i < expr->len; i++) {
                fputc('[', ctx->out);
                emit_expr(ctx, expr->kids[i]);
                fputc(']', ctx->out);
            }
            return 0;
        case NK_PTR_TY:
        case NK_REF_TY:
        case NK_VAR_TY:
        case NK_FN_TY:
        case NK_SET_TY:
        case NK_TUPLE_TY:
            fputs("0", ctx->out);
            return 0;
        case NK_TUPLE_LIT:
        case NK_SEQ_LIT:
        case NK_TABLE_LIT:
        case NK_COMPREHENSION:
            add_diag(ctx, expr, "[C-Profile] unsupported: literal");
            return -1;
        case NK_IF_EXPR:
        case NK_WHEN_EXPR: {
            if (expr->len < 3) {
                add_diag(ctx, expr, "[C-Profile] unsupported: expr");
                return -1;
            }
            fputc('(', ctx->out);
            emit_expr(ctx, expr->kids[0]);
            fputs(" ? ", ctx->out);
            emit_expr(ctx, expr->kids[1]);
            fputs(" : ", ctx->out);
            emit_expr(ctx, expr->kids[2]);
            fputc(')', ctx->out);
            return 0;
        }
        case NK_CASE_EXPR: {
            if (expr->len < 2) {
                add_diag(ctx, expr, "[C-Profile] unsupported: expr");
                return -1;
            }
            const ChengNode *selector = expr->kids[0];
            const ChengNode *else_expr = NULL;
            fputs("({ __auto_type __case = ", ctx->out);
            emit_expr(ctx, selector);
            fputs("; ", ctx->out);
            for (size_t i = 1; i < expr->len; i++) {
                const ChengNode *branch = expr->kids[i];
                if (!branch) {
                    continue;
                }
                if (branch->kind == NK_ELSE && branch->len > 0) {
                    else_expr = branch->kids[0];
                    continue;
                }
                if (branch->kind != NK_OF_BRANCH || branch->len < 2) {
                    continue;
                }
                emit_case_cond(ctx, "__case", branch->kids[0]);
                fputs(" ? ", ctx->out);
                emit_expr(ctx, branch->kids[1]);
                fputs(" : ", ctx->out);
            }
            if (else_expr) {
                emit_expr(ctx, else_expr);
            } else {
                fputs("0", ctx->out);
            }
            fputs("; })", ctx->out);
            return 0;
        }
        default:
            if (getenv("CHENG_C_TRACE_UNSUPPORTED")) {
                fprintf(stderr, "[stage0c-c] unsupported expr kind=%d line=%d col=%d\n",
                        expr->kind, expr->line, expr->col);
            }
            add_diag(ctx, expr, "[C-Profile] unsupported: expr");
            return -1;
    }
}

static const char *default_value_for_type(const char *ctype) {
    if (!ctype) {
        return "0";
    }
    if (strstr(ctype, "*") != NULL) {
        return "CHENG_NIL";
    }
    if (strcmp(ctype, "NB8") == 0 ||
        strcmp(ctype, "NC8") == 0 ||
        strcmp(ctype, "NI8") == 0 ||
        strcmp(ctype, "NI16") == 0 ||
        strcmp(ctype, "NI32") == 0 ||
        strcmp(ctype, "NI") == 0 ||
        strcmp(ctype, "NI64") == 0 ||
        strcmp(ctype, "NU8") == 0 ||
        strcmp(ctype, "NU16") == 0 ||
        strcmp(ctype, "NU32") == 0 ||
        strcmp(ctype, "NU") == 0 ||
        strcmp(ctype, "NU64") == 0) {
        return "0";
    }
    if (strcmp(ctype, "NF32") == 0 || strcmp(ctype, "NF64") == 0) {
        return "0.0";
    }
    return "{0}";
}

static int is_const_expr(const ChengNode *expr) {
    if (!expr || expr->kind == NK_EMPTY) {
        return 1;
    }
    switch (expr->kind) {
        case NK_INT_LIT:
        case NK_FLOAT_LIT:
        case NK_STR_LIT:
        case NK_CHAR_LIT:
        case NK_BOOL_LIT:
        case NK_NIL_LIT:
            return 1;
        case NK_PAR:
            if (expr->len == 1) {
                return is_const_expr(expr->kids[0]);
            }
            return 0;
        case NK_CAST:
            if (expr->len > 1) {
                return is_const_expr(expr->kids[1]);
            }
            if (expr->len > 0) {
                return is_const_expr(expr->kids[0]);
            }
            return 0;
        default:
            return 0;
    }
}

static int emit_stmt_list(CEmit *ctx, const ChengNode *list);
static int emit_if_stmt(CEmit *ctx, const ChengNode *stmt, int is_else_if);
static int emit_if_return(CEmit *ctx, const ChengNode *stmt, int is_else_if);
static int emit_case_stmt(CEmit *ctx, const ChengNode *stmt);
static int emit_case_return(CEmit *ctx, const ChengNode *stmt);
static int emit_return_body(CEmit *ctx, const ChengNode *body);
static int emit_expr_cond(CEmit *ctx, const ChengNode *expr);
static int is_noop_expr_stmt(const ChengNode *stmt);
static void emit_return_fallback(CEmit *ctx);

static int emit_if_stmt(CEmit *ctx, const ChengNode *stmt, int is_else_if) {
    if (!stmt || stmt->len < 2) {
        add_diag(ctx, stmt, "[C-Profile] unsupported: if");
        return -1;
    }
    const ChengNode *cond = stmt->kids[0];
    const ChengNode *then_body = stmt->kids[1];
    const ChengNode *else_node = stmt->len > 2 ? stmt->kids[2] : NULL;
    emit_indent(ctx);
    if (is_else_if) {
        fputs("else if (", ctx->out);
    } else {
        fputs("if (", ctx->out);
    }
    emit_expr_cond(ctx, cond);
    fputs(") {\n", ctx->out);
    ctx->indent++;
    emit_stmt_list(ctx, then_body);
    ctx->indent--;
    emit_indent(ctx);
    fputs("}\n", ctx->out);
    if (else_node && else_node->kind == NK_ELSE && else_node->len > 0) {
        const ChengNode *else_body = else_node->kids[0];
        if (else_body && else_body->kind == NK_IF) {
            return emit_if_stmt(ctx, else_body, 1);
        }
        emit_indent(ctx);
        fputs("else {\n", ctx->out);
        ctx->indent++;
        emit_stmt_list(ctx, else_body);
        ctx->indent--;
        emit_indent(ctx);
        fputs("}\n", ctx->out);
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
        case NK_SYMBOL:
        case NK_STR_LIT:
        case NK_INT_LIT:
        case NK_BOOL_LIT:
        case NK_NIL_LIT:
        case NK_CHAR_LIT:
        case NK_INFIX:
        case NK_PREFIX:
        case NK_PAR:
            return 1;
        default:
            return 0;
    }
}

static int is_noop_expr_stmt(const ChengNode *stmt) {
    if (!stmt) {
        return 1;
    }
    switch (stmt->kind) {
        case NK_INT_LIT:
        case NK_FLOAT_LIT:
        case NK_BOOL_LIT:
        case NK_NIL_LIT:
        case NK_CHAR_LIT:
        case NK_STR_LIT:
            return 1;
        case NK_PAR:
            if (stmt->len > 0) {
                return is_noop_expr_stmt(stmt->kids[0]);
            }
            return 1;
        default:
            return 0;
    }
}

static void emit_async_return_expr(CEmit *ctx, const ChengNode *expr) {
    if (!ctx || !ctx->current_ret_is_async) {
        return;
    }
    const char *fn_name = NULL;
    if (ctx->current_async_value_kind == 2) {
        fn_name = "cheng_async_ready_void";
    } else if (ctx->current_async_value_kind == 1) {
        fn_name = "cheng_async_ready_i32";
    }
    if (!fn_name) {
        add_diag(ctx, expr, "[C-Profile] not-mapped: async return type (FULLC.R03)");
        emit_indent(ctx);
        fputs("return 0;\n", ctx->out);
        return;
    }
    if (ctx->current_async_value_kind == 2) {
        if (expr && !is_empty_node(expr)) {
            emit_indent(ctx);
            emit_expr(ctx, expr);
            fputs(";\n", ctx->out);
        }
        emit_indent(ctx);
        fprintf(ctx->out, "return %s();\n", fn_name);
        return;
    }
    emit_indent(ctx);
    fprintf(ctx->out, "return %s(", fn_name);
    if (expr && !is_empty_node(expr)) {
        emit_expr(ctx, expr);
    } else {
        fputs("0", ctx->out);
    }
    fputs(");\n", ctx->out);
}

static void emit_return_fallback(CEmit *ctx) {
    if (!ctx || ctx->current_ret_is_void) {
        return;
    }
    if (ctx->current_ret_is_async) {
        emit_async_return_expr(ctx, NULL);
        return;
    }
    emit_indent(ctx);
    if (!ctx->current_ret_type || ctx->current_ret_type->kind == NK_EMPTY) {
        fputs("return 0;\n", ctx->out);
        return;
    }
    char *ctype = type_to_c(ctx, ctx->current_ret_type);
    const char *def = default_value_for_type(ctype);
    if (ctype && strcmp(def, "{0}") == 0) {
        fprintf(ctx->out, "return (%s){0};\n", ctype);
    } else {
        fprintf(ctx->out, "return %s;\n", def);
    }
    free(ctype);
}

static int emit_return_body(CEmit *ctx, const ChengNode *body) {
    const ChengNode *stmt = body;
    if (body && body->kind == NK_STMT_LIST && body->len == 1) {
        stmt = body->kids[0];
    }
    if (stmt && is_simple_expr_stmt_kind(stmt->kind)) {
        if (ctx->current_ret_is_async) {
            emit_async_return_expr(ctx, stmt);
            return 0;
        }
        emit_indent(ctx);
        fputs("return ", ctx->out);
        emit_expr(ctx, stmt);
        fputs(";\n", ctx->out);
        return 0;
    }
    return emit_stmt_list(ctx, body);
}

static int emit_expr_cond(CEmit *ctx, const ChengNode *expr) {
    if (!expr) {
        fputs("0", ctx->out);
        return 0;
    }
    switch (expr->kind) {
        case NK_INFIX: {
            if (expr->len < 3 || !expr->kids[0]) {
                add_diag(ctx, expr, "[C-Profile] unsupported: infix");
                return -1;
            }
            const char *op = expr->kids[0]->ident ? expr->kids[0]->ident : "";
            if (strcmp(op, "&") == 0) {
                const ChengNode *lhs = expr->kids[1];
                const ChengNode *rhs = expr->kids[2];
                if (lhs && (lhs->kind == NK_IDENT || lhs->kind == NK_SYMBOL) && lhs->ident) {
                    const FnSig *sig = fn_sig_resolve_call(ctx, lhs->ident, 1);
                    if (sig) {
                        const char *c_name = lhs->ident;
                        const char *imp = NULL;
                        char *owned_name = NULL;
                        if (sig->c_name) {
                            c_name = sig->c_name;
                        } else if ((imp = import_map_get(ctx, lhs->ident)) != NULL) {
                            c_name = imp;
                        } else {
                            owned_name = resolve_c_name(ctx, lhs->ident, 0, ctx->current_module, 0);
                            if (owned_name) {
                                c_name = owned_name;
                            } else {
                                c_name = normalize_mangle_base(lhs->ident);
                            }
                        }
                        fprintf(ctx->out, "%s(", c_name);
                        if (rhs && rhs->kind == NK_PREFIX && rhs->len > 1 && rhs->kids[0] &&
                            rhs->kids[0]->ident && strcmp(rhs->kids[0]->ident, "&") == 0) {
                            emit_expr(ctx, rhs);
                        } else {
                            fputc('&', ctx->out);
                            emit_expr(ctx, rhs);
                        }
                        fputc(')', ctx->out);
                        free(owned_name);
                        return 0;
                    }
                }
            }
            if ((strcmp(op, "+") == 0 || strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) &&
                (expr_is_str(ctx, expr->kids[1]) || expr_is_str(ctx, expr->kids[2]))) {
                if (strcmp(op, "!=") == 0) {
                    if (fn_sig_resolve_call(ctx, "!=", 2)) {
                        return emit_infix_call(ctx, "!=", expr->kids[1], expr->kids[2]);
                    }
                    fputs("!(", ctx->out);
                    emit_infix_call(ctx, "==", expr->kids[1], expr->kids[2]);
                    fputc(')', ctx->out);
                    return 0;
                }
                return emit_infix_call(ctx, op, expr->kids[1], expr->kids[2]);
            }
            const char *use = op;
            if (strcmp(op, "and") == 0) use = "&&";
            else if (strcmp(op, "or") == 0) use = "||";
            else if (strcmp(op, "xor") == 0) use = "^";
            else if (strcmp(op, "mod") == 0) use = "%";
            if (strcmp(op, "..") == 0 || strcmp(op, "..<") == 0) {
                add_diag(ctx, expr, "[C-Profile] unsupported: range expr");
                return -1;
            }
            emit_expr(ctx, expr->kids[1]);
            fprintf(ctx->out, " %s ", use);
            emit_expr(ctx, expr->kids[2]);
            return 0;
        }
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0]) {
                add_diag(ctx, expr, "[C-Profile] unsupported: prefix");
                return -1;
            }
            const char *op = expr->kids[0]->ident ? expr->kids[0]->ident : "";
            if (strcmp(op, "await") == 0) {
                const char *fn_name = NULL;
                if (expr->len > 1) {
                    char *rhs = infer_type_key_from_expr(ctx, expr->kids[1]);
                    if (rhs) {
                        if (strcmp(rhs, "await_i32") == 0) {
                            fn_name = "cheng_await_i32";
                        } else if (strcmp(rhs, "await_void") == 0) {
                            fn_name = "cheng_await_void";
                        }
                        free(rhs);
                    }
                }
                if (!fn_name) {
                    add_diag(ctx, expr, "[C-Profile] not-mapped: await type (FULLC.R03)");
                    fputs("0", ctx->out);
                    return -1;
                }
                fputs(fn_name, ctx->out);
                fputc('(', ctx->out);
                emit_expr(ctx, expr->kids[1]);
                fputc(')', ctx->out);
                return 0;
            }
            const char *use = op;
            if (strcmp(op, "not") == 0) {
                use = "!";
            }
            fputs(use, ctx->out);
            emit_expr(ctx, expr->kids[1]);
            return 0;
        }
        case NK_PAR:
            if (expr->len > 0) {
                return emit_expr_cond(ctx, expr->kids[0]);
            }
            fputs("0", ctx->out);
            return 0;
        default:
            return emit_expr(ctx, expr);
    }
}

static int emit_if_return(CEmit *ctx, const ChengNode *stmt, int is_else_if) {
    if (!stmt || stmt->len < 2) {
        add_diag(ctx, stmt, "[C-Profile] unsupported: if");
        return -1;
    }
    const ChengNode *cond = stmt->kids[0];
    const ChengNode *then_body = stmt->kids[1];
    const ChengNode *else_node = stmt->len > 2 ? stmt->kids[2] : NULL;
    emit_indent(ctx);
    if (is_else_if) {
        fputs("else if (", ctx->out);
    } else {
        fputs("if (", ctx->out);
    }
    emit_expr_cond(ctx, cond);
    fputs(") {\n", ctx->out);
    ctx->indent++;
    if (emit_return_body(ctx, then_body) != 0) {
        return -1;
    }
    ctx->indent--;
    emit_indent(ctx);
    fputs("}\n", ctx->out);
    if (else_node && else_node->kind == NK_ELSE && else_node->len > 0) {
        const ChengNode *else_body = else_node->kids[0];
        if (else_body && else_body->kind == NK_IF) {
            return emit_if_return(ctx, else_body, 1);
        }
        emit_indent(ctx);
        fputs("else {\n", ctx->out);
        ctx->indent++;
        if (emit_return_body(ctx, else_body) != 0) {
            return -1;
        }
        ctx->indent--;
        emit_indent(ctx);
        fputs("}\n", ctx->out);
    }
    if (!is_else_if) {
        emit_return_fallback(ctx);
    }
    return 0;
}

static void emit_case_eq(CEmit *ctx, const char *selector, const ChengNode *pat) {
    fputs(selector, ctx->out);
    fputs(" == ", ctx->out);
    emit_expr(ctx, pat);
}

static void emit_case_cond(CEmit *ctx, const char *selector, const ChengNode *pat) {
    if (!pat) {
        fputs("0", ctx->out);
        return;
    }
    if (pat->kind == NK_PATTERN && pat->len > 0) {
        fputc('(', ctx->out);
        for (size_t i = 0; i < pat->len; i++) {
            if (i > 0) {
                fputs(" || ", ctx->out);
            }
            emit_case_eq(ctx, selector, pat->kids[i]);
        }
        fputc(')', ctx->out);
        return;
    }
    emit_case_eq(ctx, selector, pat);
}

static int emit_case_stmt(CEmit *ctx, const ChengNode *stmt) {
    if (!stmt || stmt->len < 1) {
        add_diag(ctx, stmt, "[C-Profile] unsupported: case");
        return -1;
    }
    const ChengNode *selector = stmt->kids[0];
    emit_indent(ctx);
    fputs("{\n", ctx->out);
    ctx->indent++;
    emit_indent(ctx);
    fputs("__auto_type __case = ", ctx->out);
    emit_expr(ctx, selector);
    fputs(";\n", ctx->out);
    int first = 1;
    for (size_t i = 1; i < stmt->len; i++) {
        const ChengNode *branch = stmt->kids[i];
        if (!branch) {
            continue;
        }
        if (branch->kind == NK_OF_BRANCH && branch->len >= 2) {
            emit_indent(ctx);
            if (first) {
                fputs("if (", ctx->out);
                first = 0;
            } else {
                fputs("else if (", ctx->out);
            }
            emit_case_cond(ctx, "__case", branch->kids[0]);
            fputs(") {\n", ctx->out);
            ctx->indent++;
            emit_stmt_list(ctx, branch->kids[1]);
            ctx->indent--;
            emit_indent(ctx);
            fputs("}\n", ctx->out);
        } else if (branch->kind == NK_ELSE && branch->len > 0) {
            emit_indent(ctx);
            fputs("else {\n", ctx->out);
            ctx->indent++;
            emit_stmt_list(ctx, branch->kids[0]);
            ctx->indent--;
            emit_indent(ctx);
            fputs("}\n", ctx->out);
        }
    }
    ctx->indent--;
    emit_indent(ctx);
    fputs("}\n", ctx->out);
    emit_return_fallback(ctx);
    return 0;
}

static int emit_case_return(CEmit *ctx, const ChengNode *stmt) {
    if (!stmt || stmt->len < 1) {
        add_diag(ctx, stmt, "[C-Profile] unsupported: case");
        return -1;
    }
    const ChengNode *selector = stmt->kids[0];
    emit_indent(ctx);
    fputs("{\n", ctx->out);
    ctx->indent++;
    emit_indent(ctx);
    fputs("__auto_type __case = ", ctx->out);
    emit_expr(ctx, selector);
    fputs(";\n", ctx->out);
    int first = 1;
    for (size_t i = 1; i < stmt->len; i++) {
        const ChengNode *branch = stmt->kids[i];
        if (!branch) {
            continue;
        }
        if (branch->kind == NK_OF_BRANCH && branch->len >= 2) {
            emit_indent(ctx);
            if (first) {
                fputs("if (", ctx->out);
                first = 0;
            } else {
                fputs("else if (", ctx->out);
            }
            emit_case_cond(ctx, "__case", branch->kids[0]);
            fputs(") {\n", ctx->out);
            ctx->indent++;
            if (emit_return_body(ctx, branch->kids[1]) != 0) {
                return -1;
            }
            ctx->indent--;
            emit_indent(ctx);
            fputs("}\n", ctx->out);
        } else if (branch->kind == NK_ELSE && branch->len > 0) {
            emit_indent(ctx);
            fputs("else {\n", ctx->out);
            ctx->indent++;
            if (emit_return_body(ctx, branch->kids[0]) != 0) {
                return -1;
            }
            ctx->indent--;
            emit_indent(ctx);
            fputs("}\n", ctx->out);
        }
    }
    ctx->indent--;
    emit_indent(ctx);
    fputs("}\n", ctx->out);
    return 0;
}

static char *ref_object_name_from_type(CEmit *ctx, const ChengNode *type_node) {
    if (!ctx || !type_node) {
        return NULL;
    }
    const ChengNode *node = type_node;
    if (node->kind == NK_PAR && node->len == 1) {
        node = node->kids[0];
    }
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_REF_TY || node->kind == NK_VAR_TY || node->kind == NK_PTR_TY) {
        node = node->len > 0 ? node->kids[0] : NULL;
        if (node && node->kind == NK_PAR && node->len == 1) {
            node = node->kids[0];
        }
    }
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_IDENT || node->kind == NK_SYMBOL) {
        if (!node->ident) {
            return NULL;
        }
        const char *mapped = resolve_type_c_name(ctx, node->ident, ctx->current_module);
        if (mapped) {
            return make_ref_obj_name(mapped);
        }
        char *san = sanitize_ident(node->ident);
        char *obj = make_ref_obj_name(san ? san : node->ident);
        free(san);
        return obj;
    }
    if (node->kind == NK_OBJECT_DECL) {
        if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT && node->kids[0]->ident) {
            const char *mapped = resolve_type_c_name(ctx, node->kids[0]->ident, ctx->current_module);
            if (mapped) {
                return make_ref_obj_name(mapped);
            }
            char *san = sanitize_ident(node->kids[0]->ident);
            char *obj = make_ref_obj_name(san ? san : node->kids[0]->ident);
            free(san);
            return obj;
        }
    }
    return NULL;
}

static int emit_new_stmt(CEmit *ctx, const ChengNode *expr) {
    if (!ctx || !expr || expr->kind != NK_CALL) {
        return 1;
    }
    const ChengNode *raw_callee = expr->kids[0];
    const ChengNode *callee = unwrap_parens(raw_callee);
    if (!callee || (callee->kind != NK_IDENT && callee->kind != NK_SYMBOL) || !callee->ident) {
        return 1;
    }
    if (strcmp(callee->ident, "new") != 0) {
        return 1;
    }
    if (call_arg_count(expr) != 1) {
        add_diag(ctx, expr, "[C-Profile] new requires one argument");
        return -1;
    }
    const ChengNode *arg = unwrap_call_arg(expr->kids[1]);
    if (!arg || arg->kind != NK_IDENT || !arg->ident) {
        add_diag(ctx, expr, "[C-Profile] new requires an identifier target");
        return -1;
    }
    const ChengNode *type_node = locals_get_type(ctx, arg->ident);
    if (!type_node) {
        add_diag(ctx, expr, "[C-Profile] new target missing type");
        return -1;
    }
    char *obj_name = ref_object_name_from_type(ctx, type_node);
    if (!obj_name) {
        add_diag(ctx, expr, "[C-Profile] new requires ref object type");
        return -1;
    }
    char *ctype = type_to_c(ctx, type_node);
    char *arg_name = sanitize_ident(arg->ident);
    emit_indent(ctx);
    fputs("{\n", ctx->out);
    ctx->indent++;
    char *san_obj = sanitize_ident(obj_name);
    emit_indent(ctx);
    fprintf(ctx->out, "NI32 __newSize = (NI32)sizeof(struct %s);\n", san_obj ? san_obj : obj_name);
    emit_indent(ctx);
    fputs("void* __newPtr = cheng_malloc(__newSize);\n", ctx->out);
    emit_indent(ctx);
    fputs("if (__newPtr != CHENG_NIL) {\n", ctx->out);
    ctx->indent++;
    emit_indent(ctx);
    fputs("cheng_memset(__newPtr, 0, (NI64)__newSize);\n", ctx->out);
    emit_indent(ctx);
    fprintf(ctx->out, "%s = (%s)__newPtr;\n",
            arg_name ? arg_name : arg->ident,
            ctype ? ctype : "void*");
    ctx->indent--;
    emit_indent(ctx);
    fputs("}\n", ctx->out);
    ctx->indent--;
    emit_indent(ctx);
    fputs("}\n", ctx->out);
    free(san_obj);
    free(arg_name);
    free(ctype);
    free(obj_name);
    return 0;
}

static int emit_stmt(CEmit *ctx, const ChengNode *stmt) {
    if (!stmt) {
        return 0;
    }
    switch (stmt->kind) {
        case NK_STMT_LIST:
            return emit_stmt_list(ctx, stmt);
        case NK_LET:
        case NK_VAR:
        case NK_CONST: {
            const ChengNode *pattern = stmt->len > 0 ? stmt->kids[0] : NULL;
            const ChengNode *type_node = stmt->len > 1 ? stmt->kids[1] : NULL;
            const ChengNode *value = stmt->len > 2 ? stmt->kids[2] : NULL;
            if (!pattern || pattern->len == 0 || !pattern->kids[0] || pattern->kids[0]->kind != NK_IDENT) {
                add_diag(ctx, stmt, "[C-Profile] unsupported: binding pattern");
                return -1;
            }
            const char *name = pattern->kids[0]->ident;
            if (!name) {
                add_diag(ctx, stmt, "[C-Profile] unsupported: binding name");
                return -1;
            }
            char *ctype = NULL;
            if (!is_empty_node(type_node)) {
                ctype = type_to_c(ctx, type_node);
            } else {
                if (!is_empty_node(value)) {
                    ctype = infer_type_from_expr(ctx, value);
                } else {
                    add_diag(ctx, stmt, "[C-Profile] unsupported: binding requires type or init");
                    return -1;
                }
            }
            int is_str = 0;
            if (!is_empty_node(type_node)) {
                is_str = type_is_str_node(type_node);
            } else if (!is_empty_node(value)) {
                is_str = expr_is_str(ctx, value);
            }
            emit_indent(ctx);
            if (stmt->kind == NK_CONST) {
                fputs("const ", ctx->out);
            }
            fputs(ctype ? ctype : "NI32", ctx->out);
            fputc(' ', ctx->out);
            char *san = sanitize_ident(name);
            fputs(san ? san : name, ctx->out);
            free(san);
            if (!is_empty_node(value)) {
                fputs(" = ", ctx->out);
                emit_expr(ctx, value);
            } else {
                fprintf(ctx->out, " = %s", default_value_for_type(ctype));
            }
            fputs(";\n", ctx->out);
            locals_set(ctx, name, is_str, is_empty_node(type_node) ? NULL : type_node);
            free(ctype);
            return 0;
        }
        case NK_ASGN:
        case NK_FAST_ASGN: {
            if (stmt->len < 2) {
                add_diag(ctx, stmt, "[C-Profile] unsupported: assignment");
                return -1;
            }
            emit_indent(ctx);
            emit_expr(ctx, stmt->kids[0]);
            fputs(" = ", ctx->out);
            emit_expr(ctx, stmt->kids[1]);
            fputs(";\n", ctx->out);
            return 0;
        }
        case NK_RETURN: {
            if (ctx->current_ret_is_async) {
                const ChengNode *ret_expr = NULL;
                if (stmt->len > 0 && !is_empty_node(stmt->kids[0])) {
                    ret_expr = stmt->kids[0];
                }
                emit_async_return_expr(ctx, ret_expr);
                return 0;
            }
            emit_indent(ctx);
            if (stmt->len > 0 && !is_empty_node(stmt->kids[0])) {
                fputs("return ", ctx->out);
                emit_expr(ctx, stmt->kids[0]);
                fputs(";\n", ctx->out);
            } else {
                fputs("return;\n", ctx->out);
            }
            return 0;
        }
        case NK_IF:
        case NK_WHEN:
            return emit_if_stmt(ctx, stmt, 0);
        case NK_WHILE: {
            const ChengNode *cond = stmt->len > 0 ? stmt->kids[0] : NULL;
            const ChengNode *body = stmt->len > 1 ? stmt->kids[1] : NULL;
            emit_indent(ctx);
            fputs("while (", ctx->out);
            emit_expr_cond(ctx, cond);
            fputs(") {\n", ctx->out);
            ctx->indent++;
            emit_stmt_list(ctx, body);
            ctx->indent--;
            emit_indent(ctx);
            fputs("}\n", ctx->out);
            return 0;
        }
        case NK_FOR: {
            if (stmt->len < 3) {
                add_diag(ctx, stmt, "[C-Profile] unsupported: for");
                return -1;
            }
            const ChengNode *pattern = stmt->kids[0];
            const ChengNode *iter = stmt->kids[1];
            const ChengNode *body = stmt->kids[2];
            if (!pattern || pattern->len == 0 || !pattern->kids[0] ||
                pattern->kids[0]->kind != NK_IDENT) {
                add_diag(ctx, stmt, "[C-Profile] unsupported: for pattern");
                return -1;
            }
            const char *name = pattern->kids[0]->ident;
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
                add_diag(ctx, stmt, "[C-Profile] unsupported: for-range");
                return -1;
            }
            char *san = sanitize_ident(name);
            emit_indent(ctx);
            fputs("for (NI32 ", ctx->out);
            fputs(san ? san : name, ctx->out);
            fputs(" = ", ctx->out);
            emit_expr(ctx, start);
            fputs("; ", ctx->out);
            fputs(san ? san : name, ctx->out);
            fputs(inclusive ? " <= " : " < ", ctx->out);
            emit_expr(ctx, end);
            fputs("; ", ctx->out);
            fputs(san ? san : name, ctx->out);
            fputs(" += 1) {\n", ctx->out);
            free(san);
            ctx->indent++;
            emit_stmt_list(ctx, body);
            ctx->indent--;
            emit_indent(ctx);
            fputs("}\n", ctx->out);
            return 0;
        }
        case NK_BREAK:
            emit_indent(ctx);
            fputs("break;\n", ctx->out);
            return 0;
        case NK_CONTINUE:
            emit_indent(ctx);
            fputs("continue;\n", ctx->out);
            return 0;
        case NK_PRAGMA:
        case NK_IMPORT_STMT:
        case NK_INCLUDE_STMT:
        case NK_TYPE_DECL:
        case NK_OBJECT_DECL:
        case NK_ENUM_DECL:
            return 0;
        case NK_DEFER:
            return 0;
        case NK_CASE:
            return emit_case_stmt(ctx, stmt);
        case NK_YIELD:
            add_diag(ctx, stmt, "[C-Profile] unsupported: stmt");
            return -1;
        case NK_CALL: {
            int rc = emit_new_stmt(ctx, stmt);
            if (rc != 1) {
                return rc;
            }
            emit_indent(ctx);
            emit_expr(ctx, stmt);
            fputs(";\n", ctx->out);
            return 0;
        }
        default:
            if (is_noop_expr_stmt(stmt)) {
                return 0;
            }
            emit_indent(ctx);
            emit_expr(ctx, stmt);
            fputs(";\n", ctx->out);
            return 0;
    }
}

static int emit_defers_reverse(CEmit *ctx, const ChengNode **defers, size_t defer_count) {
    for (size_t i = defer_count; i-- > 0;) {
        if (emit_stmt_list(ctx, defers[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int emit_stmt_list(CEmit *ctx, const ChengNode *list) {
    if (!list) {
        return 0;
    }
    if (list->kind != NK_STMT_LIST) {
        return emit_stmt(ctx, list);
    }
    const ChengNode **defers = NULL;
    size_t defer_count = 0;
    size_t defer_cap = 0;
    int terminated = 0;
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_DEFER) {
            if (stmt->len > 0 && stmt->kids[0]) {
                if (defer_count == defer_cap) {
                    size_t next = defer_cap == 0 ? 4 : defer_cap * 2;
                    const ChengNode **items = (const ChengNode **)realloc(defers, next * sizeof(*defers));
                    if (!items) {
                        free(defers);
                        return -1;
                    }
                    defers = items;
                    defer_cap = next;
                }
                defers[defer_count++] = stmt->kids[0];
            }
            continue;
        }
        if (stmt->kind == NK_RETURN || stmt->kind == NK_BREAK || stmt->kind == NK_CONTINUE) {
            if (emit_defers_reverse(ctx, defers, defer_count) != 0) {
                free(defers);
                return -1;
            }
            defer_count = 0;
            terminated = 1;
        }
        if (emit_stmt(ctx, stmt) != 0) {
            free(defers);
            return -1;
        }
        if (terminated) {
            break;
        }
    }
    if (!terminated) {
        if (emit_defers_reverse(ctx, defers, defer_count) != 0) {
            free(defers);
            return -1;
        }
    }
    free(defers);
    return 0;
}

static int rec_list_has_fields(const ChengNode *rec_list) {
    if (!rec_list || rec_list->kind != NK_REC_LIST) {
        return 0;
    }
    for (size_t i = 0; i < rec_list->len; i++) {
        const ChengNode *field = rec_list->kids[i];
        if (!field) {
            continue;
        }
        if (field->kind == NK_IDENT_DEFS || field->kind == NK_REC_CASE) {
            return 1;
        }
    }
    return 0;
}

static int emit_rec_field_decl(CEmit *ctx, const ChengNode *field) {
    if (!ctx || !field || field->kind != NK_IDENT_DEFS || field->len == 0) {
        return 0;
    }
    const ChengNode *fname = field->kids[0];
    const ChengNode *ftype = field->len > 1 ? field->kids[1] : NULL;
    if (!fname || fname->kind != NK_IDENT || !fname->ident) {
        add_diag(ctx, field, "[C-Profile] unsupported: field name");
        return -1;
    }
    char *ctype = type_to_c(ctx, ftype);
    emit_indent(ctx);
    fputs(ctype ? ctype : "NI32", ctx->out);
    fputc(' ', ctx->out);
    char *fsan = sanitize_ident(fname->ident);
    fputs(fsan ? fsan : fname->ident, ctx->out);
    free(fsan);
    fputs(";\n", ctx->out);
    free(ctype);
    return 0;
}

static int emit_rec_list_fields(CEmit *ctx, const ChengNode *rec_list);

static int emit_rec_case_fields(CEmit *ctx, const ChengNode *rec_case) {
    if (!ctx || !rec_case || rec_case->kind != NK_REC_CASE) {
        return 0;
    }
    if (rec_case->len > 0) {
        const ChengNode *defs = rec_case->kids[0];
        if (defs && defs->kind == NK_IDENT_DEFS) {
            if (emit_rec_field_decl(ctx, defs) != 0) {
                return -1;
            }
        }
    }
    int has_union = 0;
    for (size_t i = 1; i < rec_case->len; i++) {
        const ChengNode *branch = rec_case->kids[i];
        const ChengNode *body = NULL;
        if (!branch) {
            continue;
        }
        if (branch->kind == NK_OF_BRANCH && branch->len > 1) {
            body = branch->kids[1];
        } else if (branch->kind == NK_ELSE && branch->len > 0) {
            body = branch->kids[0];
        }
        if (rec_list_has_fields(body)) {
            has_union = 1;
            break;
        }
    }
    if (!has_union) {
        return 0;
    }
    emit_indent(ctx);
    fputs("union {\n", ctx->out);
    ctx->indent++;
    for (size_t i = 1; i < rec_case->len; i++) {
        const ChengNode *branch = rec_case->kids[i];
        const ChengNode *body = NULL;
        if (!branch) {
            continue;
        }
        if (branch->kind == NK_OF_BRANCH && branch->len > 1) {
            body = branch->kids[1];
        } else if (branch->kind == NK_ELSE && branch->len > 0) {
            body = branch->kids[0];
        }
        if (!rec_list_has_fields(body)) {
            continue;
        }
        emit_indent(ctx);
        fputs("struct {\n", ctx->out);
        ctx->indent++;
        if (emit_rec_list_fields(ctx, body) != 0) {
            return -1;
        }
        ctx->indent--;
        emit_indent(ctx);
        fputs("};\n", ctx->out);
    }
    ctx->indent--;
    emit_indent(ctx);
    fputs("};\n", ctx->out);
    return 0;
}

static int emit_rec_list_fields(CEmit *ctx, const ChengNode *rec_list) {
    if (!ctx || !rec_list || rec_list->kind != NK_REC_LIST) {
        return 0;
    }
    for (size_t i = 0; i < rec_list->len; i++) {
        const ChengNode *field = rec_list->kids[i];
        if (!field) {
            continue;
        }
        if (field->kind == NK_IDENT_DEFS) {
            if (emit_rec_field_decl(ctx, field) != 0) {
                return -1;
            }
        } else if (field->kind == NK_REC_CASE) {
            if (emit_rec_case_fields(ctx, field) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int emit_object_struct(CEmit *ctx, const char *c_name, const ChengNode *obj) {
    if (!c_name || !obj) {
        return -1;
    }
    const ChengNode *rec = NULL;
    const ChengNode *base = NULL;
    if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind == NK_IDENT) {
        if (obj->len > 1 && obj->kids[1] && obj->kids[1]->kind == NK_REC_LIST) {
            rec = obj->kids[1];
        }
        if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind != NK_IDENT) {
            base = obj->kids[0];
        }
    } else {
        if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind != NK_REC_LIST) {
            base = obj->kids[0];
        }
        if (obj->len > 1 && obj->kids[1] && obj->kids[1]->kind == NK_REC_LIST) {
            rec = obj->kids[1];
        } else if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind == NK_REC_LIST) {
            rec = obj->kids[0];
        }
    }
    if (base && !is_empty_node(base)) {
        add_diag(ctx, obj, "[C-Profile] unsupported: object inheritance");
        return -1;
    }
    char *san = sanitize_ident(c_name);
    fprintf(ctx->out, "struct %s {\n", san ? san : c_name);
    ctx->indent++;
    if (rec && rec->kind == NK_REC_LIST) {
        if (emit_rec_list_fields(ctx, rec) != 0) {
            free(san);
            return -1;
        }
    }
    ctx->indent--;
    fprintf(ctx->out, "};\n\n");
    free(san);
    return 0;
}

static int emit_object_decl(CEmit *ctx, const char *c_name, const ChengNode *obj) {
    if (!c_name || !obj) {
        return -1;
    }
    const ChengNode *rec = NULL;
    const ChengNode *base = NULL;
    if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind == NK_IDENT) {
        if (obj->len > 1 && obj->kids[1] && obj->kids[1]->kind == NK_REC_LIST) {
            rec = obj->kids[1];
        }
        if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind != NK_IDENT) {
            base = obj->kids[0];
        }
    } else {
        if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind != NK_REC_LIST) {
            base = obj->kids[0];
        }
        if (obj->len > 1 && obj->kids[1] && obj->kids[1]->kind == NK_REC_LIST) {
            rec = obj->kids[1];
        } else if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind == NK_REC_LIST) {
            rec = obj->kids[0];
        }
    }
    if (base && !is_empty_node(base)) {
        add_diag(ctx, obj, "[C-Profile] unsupported: object inheritance");
        return -1;
    }
    char *san = sanitize_ident(c_name);
    fprintf(ctx->out, "typedef struct %s {\n", san ? san : c_name);
    ctx->indent++;
    if (rec && rec->kind == NK_REC_LIST) {
        if (emit_rec_list_fields(ctx, rec) != 0) {
            free(san);
            return -1;
        }
    }
    ctx->indent--;
    fprintf(ctx->out, "} %s;\n\n", san ? san : c_name);
    free(san);
    return 0;
}

static int emit_enum_decl(CEmit *ctx, const char *c_name, const ChengNode *node) {
    if (!c_name || !node) {
        return -1;
    }
    const ChengNode *base = node->len > 0 ? node->kids[0] : NULL;
    if (base && !is_empty_node(base)) {
        add_diag(ctx, node, "[C-Profile] unsupported: enum base type");
    }
    char *san = sanitize_ident(c_name);
    fprintf(ctx->out, "typedef enum %s {\n", san ? san : c_name);
    ctx->indent++;
    for (size_t i = 1; i < node->len; i++) {
        const ChengNode *field = node->kids[i];
        if (!field || field->kind != NK_ENUM_FIELD_DECL || field->len == 0) {
            continue;
        }
        const ChengNode *fname = field->kids[0];
        if (!fname || fname->kind != NK_IDENT || !fname->ident) {
            continue;
        }
        emit_indent(ctx);
        char *field_c_name = NULL;
        if (san) {
            size_t n = strlen(san) + 1 + strlen(fname->ident) + 1;
            field_c_name = (char *)malloc(n);
            if (field_c_name) {
                snprintf(field_c_name, n, "%s_%s", san, fname->ident);
            }
        } else {
            size_t n = strlen(c_name) + 1 + strlen(fname->ident) + 1;
            field_c_name = (char *)malloc(n);
            if (field_c_name) {
                snprintf(field_c_name, n, "%s_%s", c_name, fname->ident);
            }
        }
        if (field_c_name) {
            fputs(field_c_name, ctx->out);
            enum_field_map_add(ctx, fname->ident, field_c_name, ctx->current_module);
            free(field_c_name);
        } else {
            if (san) {
                fprintf(ctx->out, "%s_%s", san, fname->ident);
            } else {
                fprintf(ctx->out, "%s_%s", c_name, fname->ident);
            }
        }
        if (field->len > 1 && field->kids[1] && field->kids[1]->kind == NK_INT_LIT) {
            fprintf(ctx->out, " = %lld", (long long)field->kids[1]->int_val);
        } else if (field->len > 1 && !is_empty_node(field->kids[1])) {
            add_diag(ctx, field, "[C-Profile] unsupported: enum value");
        }
        if (i + 1 < node->len) {
            fputs(",", ctx->out);
        }
        fputs("\n", ctx->out);
    }
    ctx->indent--;
    fprintf(ctx->out, "} %s;\n\n", san ? san : c_name);
    free(san);
    return 0;
}

static int emit_tuple_decl(CEmit *ctx, const char *c_name, const ChengNode *tuple) {
    if (!c_name || !tuple) {
        return -1;
    }
    char *san = sanitize_ident(c_name);
    fprintf(ctx->out, "typedef struct %s {\n", san ? san : c_name);
    ctx->indent++;
    for (size_t i = 0; i < tuple->len; i++) {
        const ChengNode *field_type = tuple->kids[i];
        char *ctype = type_to_c(ctx, field_type);
        emit_indent(ctx);
        fputs(ctype ? ctype : "NI32", ctx->out);
        fprintf(ctx->out, " f%zu;\n", i);
        free(ctype);
    }
    ctx->indent--;
    fprintf(ctx->out, "} %s;\n\n", san ? san : c_name);
    free(san);
    return 0;
}

typedef enum {
    TYPE_ENTRY_ALIAS,
    TYPE_ENTRY_OBJECT,
    TYPE_ENTRY_TUPLE,
    TYPE_ENTRY_ENUM
} TypeEntryKind;

typedef struct {
    const ChengNode *node;
    const char *name;
    char *c_name;
    TypeEntryKind kind;
    size_t module_index;
} TypeEntry;

static int type_entries_reserve(TypeEntry **items, size_t *cap, size_t len, size_t extra) {
    if (!items || !cap) {
        return -1;
    }
    size_t need = len + extra;
    if (*cap >= need) {
        return 0;
    }
    size_t next = *cap == 0 ? 16 : *cap * 2;
    while (next < need) {
        next *= 2;
    }
    TypeEntry *out = (TypeEntry *)realloc(*items, next * sizeof(*out));
    if (!out) {
        return -1;
    }
    *items = out;
    *cap = next;
    return 0;
}

static void type_entries_add(TypeEntry **items,
                             size_t *len,
                             size_t *cap,
                             const ChengNode *node,
                             const char *name,
                             const char *c_name,
                             TypeEntryKind kind,
                             size_t module_index) {
    if (!items || !len || !cap || !node || !name || name[0] == '\0' || !c_name) {
        return;
    }
    if (type_entries_reserve(items, cap, *len, 1) != 0) {
        return;
    }
    (*items)[*len].node = node;
    (*items)[*len].name = name;
    (*items)[*len].c_name = cheng_strdup(c_name);
    (*items)[*len].kind = kind;
    (*items)[*len].module_index = module_index;
    (*len)++;
}

static int type_entry_emitted(const TypeEntry *entries, const int *emitted, size_t len, const char *c_name) {
    if (!entries || !emitted || !c_name) {
        return 1;
    }
    for (size_t i = 0; i < len; i++) {
        if (entries[i].c_name && strcmp(entries[i].c_name, c_name) == 0) {
            return emitted[i] != 0;
        }
    }
    return 1;
}

static int type_node_unresolved_dep(const CEmit *ctx,
                                    const ChengNode *node,
                                    const char *self_c_name,
                                    size_t module_index,
                                    const TypeEntry *entries,
                                    const int *emitted,
                                    size_t len) {
    if (!node) {
        return 0;
    }
    const ChengNode *cur = node;
    if (cur->kind == NK_PAR && cur->len == 1) {
        cur = cur->kids[0];
    }
    if (!cur) {
        return 0;
    }
    if (cur->kind == NK_PTR_TY || cur->kind == NK_REF_TY || cur->kind == NK_VAR_TY) {
        return 0;
    }
    if (cur->kind == NK_IDENT || cur->kind == NK_SYMBOL) {
        const char *name = cur->ident;
        if (!name) {
            return 0;
        }
        const char *c_name = resolve_type_c_name(ctx, name, module_index);
        if (!c_name || (self_c_name && strcmp(c_name, self_c_name) == 0)) {
            return 0;
        }
        if (type_name_has(ctx, c_name) && !type_entry_emitted(entries, emitted, len, c_name)) {
            return 1;
        }
        return 0;
    }
    if (cur->kind == NK_BRACKET_EXPR) {
        char *key = type_key(cur);
        if (key && key[0] != '\0') {
            const char *c_name = resolve_type_c_name(ctx, key, module_index);
            int unresolved = c_name && type_name_has(ctx, c_name) &&
                             !type_entry_emitted(entries, emitted, len, c_name);
            free(key);
            return unresolved;
        }
        free(key);
        return 0;
    }
    if (cur->kind == NK_TUPLE_TY) {
        for (size_t i = 0; i < cur->len; i++) {
            if (type_node_unresolved_dep(ctx, cur->kids[i], self_c_name, module_index, entries, emitted, len)) {
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

static int type_rec_list_unresolved_dep(const CEmit *ctx,
                                        const ChengNode *rec_list,
                                        const char *self_c_name,
                                        size_t module_index,
                                        const TypeEntry *entries,
                                        const int *emitted,
                                        size_t len) {
    if (!rec_list || rec_list->kind != NK_REC_LIST) {
        return 0;
    }
    for (size_t i = 0; i < rec_list->len; i++) {
        const ChengNode *field = rec_list->kids[i];
        if (!field) {
            continue;
        }
        if (field->kind == NK_IDENT_DEFS) {
            if (field->len > 1) {
                const ChengNode *ftype = field->kids[1];
                if (type_node_unresolved_dep(ctx, ftype, self_c_name, module_index, entries, emitted, len)) {
                    return 1;
                }
            }
            continue;
        }
        if (field->kind == NK_REC_CASE) {
            if (field->len > 0) {
                const ChengNode *defs = field->kids[0];
                if (defs && defs->kind == NK_IDENT_DEFS && defs->len > 1) {
                    const ChengNode *tag_type = defs->kids[1];
                    if (type_node_unresolved_dep(ctx, tag_type, self_c_name, module_index, entries, emitted, len)) {
                        return 1;
                    }
                }
            }
            for (size_t bi = 1; bi < field->len; bi++) {
                const ChengNode *branch = field->kids[bi];
                const ChengNode *body = NULL;
                if (!branch) {
                    continue;
                }
                if (branch->kind == NK_OF_BRANCH && branch->len > 1) {
                    body = branch->kids[1];
                } else if (branch->kind == NK_ELSE && branch->len > 0) {
                    body = branch->kids[0];
                }
                if (type_rec_list_unresolved_dep(ctx, body, self_c_name, module_index, entries, emitted, len)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int type_entry_unresolved_dep(const CEmit *ctx,
                                     const TypeEntry *entry,
                                     const TypeEntry *entries,
                                     const int *emitted,
                                     size_t len) {
    if (!ctx || !entry || !entry->node || !entry->name) {
        return 0;
    }
    const ChengNode *node = entry->node;
    if (entry->kind == TYPE_ENTRY_ALIAS) {
        if (node->kind == NK_TYPE_DECL && node->len > 1) {
            const ChengNode *ref_obj = ref_object_inner(node->kids[1]);
            if (ref_obj) {
                const ChengNode *rec = NULL;
                if (ref_obj->len > 1 && ref_obj->kids[1] && ref_obj->kids[1]->kind == NK_REC_LIST) {
                    rec = ref_obj->kids[1];
                } else if (ref_obj->len > 0 && ref_obj->kids[0] && ref_obj->kids[0]->kind == NK_REC_LIST) {
                    rec = ref_obj->kids[0];
                }
                if (rec && rec->kind == NK_REC_LIST) {
                    if (type_rec_list_unresolved_dep(ctx,
                                                     rec,
                                                     entry->c_name,
                                                     entry->module_index,
                                                     entries,
                                                     emitted,
                                                     len)) {
                        return 1;
                    }
                }
            }
            return type_node_unresolved_dep(ctx,
                                            node->kids[1],
                                            entry->c_name,
                                            entry->module_index,
                                            entries,
                                            emitted,
                                            len);
        }
        return 0;
    }
    if (entry->kind == TYPE_ENTRY_TUPLE) {
        const ChengNode *tuple = NULL;
        if (node->kind == NK_TYPE_DECL && node->len > 1) {
            tuple = node->kids[1];
        } else if (node->kind == NK_TUPLE_TY) {
            tuple = node;
        }
        if (!tuple) {
            return 0;
        }
        for (size_t i = 0; i < tuple->len; i++) {
                if (type_node_unresolved_dep(ctx,
                                             tuple->kids[i],
                                             entry->c_name,
                                             entry->module_index,
                                             entries,
                                             emitted,
                                             len)) {
                    return 1;
                }
            }
        return 0;
    }
    if (entry->kind == TYPE_ENTRY_OBJECT) {
        const ChengNode *obj = NULL;
        if (node->kind == NK_TYPE_DECL && node->len > 1) {
            obj = node->kids[1];
        } else if (node->kind == NK_OBJECT_DECL) {
            obj = node;
        }
        if (!obj) {
            return 0;
        }
        const ChengNode *rec = NULL;
        if (obj->len > 1 && obj->kids[1] && obj->kids[1]->kind == NK_REC_LIST) {
            rec = obj->kids[1];
        } else if (obj->len > 0 && obj->kids[0] && obj->kids[0]->kind == NK_REC_LIST) {
            rec = obj->kids[0];
        }
        if (rec && rec->kind == NK_REC_LIST) {
            if (type_rec_list_unresolved_dep(ctx,
                                             rec,
                                             entry->c_name,
                                             entry->module_index,
                                             entries,
                                             emitted,
                                             len)) {
                return 1;
            }
        }
    }
    return 0;
}

static int emit_type_entry(CEmit *ctx, const TypeEntry *entry) {
    if (!entry || !entry->node || !entry->c_name) {
        return 0;
    }
    const ChengNode *node = entry->node;
    const ChengNode *generics = NULL;
    size_t gen_base = 0;
    if (ctx->allow_generic_erasure) {
        generics = node_generics(node);
        if (generics) {
            gen_base = generic_params_push(ctx, generics);
        }
    }
    int rc = 0;
    if (node->kind == NK_TYPE_DECL) {
        if (node->len < 2) {
            rc = 0;
            goto done;
        }
        const ChengNode *name_node = node->kids[0];
        const ChengNode *type_node = node->kids[1];
        if (!type_decl_base_name(name_node)) {
            rc = 0;
            goto done;
        }
        if (type_node && type_node->kind == NK_ENUM_DECL) {
            rc = emit_enum_decl(ctx, entry->c_name, type_node);
            goto done;
        }
        const ChengNode *ref_obj = ref_object_inner(type_node);
        if (ref_obj) {
            char *obj_name = make_ref_obj_name(entry->c_name);
            if (!obj_name) {
                rc = -1;
                goto done;
            }
            char *san_obj = sanitize_ident(obj_name);
            char *san = sanitize_ident(entry->c_name);
            fprintf(ctx->out, "struct %s;\n", san_obj ? san_obj : obj_name);
            fprintf(ctx->out, "typedef struct %s* %s;\n",
                    san_obj ? san_obj : obj_name,
                    san ? san : entry->c_name);
            rc = emit_object_struct(ctx, obj_name, ref_obj);
            free(san_obj);
            free(san);
            free(obj_name);
            goto done;
        }
        if (type_node && type_node->kind == NK_OBJECT_DECL) {
            rc = emit_object_decl(ctx, entry->c_name, type_node);
            goto done;
        }
        if (type_node && type_node->kind == NK_TUPLE_TY) {
            rc = emit_tuple_decl(ctx, entry->c_name, type_node);
            goto done;
        }
        if (type_node && !is_empty_node(type_node)) {
            char *ctype = type_to_c(ctx, type_node);
            char *san = sanitize_ident(entry->c_name);
            fprintf(ctx->out, "typedef %s %s;\n", ctype ? ctype : "NI32", san ? san : entry->c_name);
            free(ctype);
            free(san);
            rc = 0;
            goto done;
        }
        rc = 0;
        goto done;
    }
    if (node->kind == NK_OBJECT_DECL) {
        if (node->len == 0) {
            rc = 0;
            goto done;
        }
        const ChengNode *name_node = node->kids[0];
        if (!type_decl_base_name(name_node)) {
            add_diag(ctx, node, "[C-Profile] unsupported: object name");
            rc = -1;
            goto done;
        }
        rc = emit_object_decl(ctx, entry->c_name, node);
        goto done;
    }
    if (node->kind == NK_ENUM_DECL) {
        rc = emit_enum_decl(ctx, entry->c_name, node);
        goto done;
    }
    if (node->kind == NK_TUPLE_TY) {
        rc = emit_tuple_decl(ctx, entry->c_name, node);
        goto done;
    }
    rc = 0;
done:
    if (generics) {
        generic_params_pop(ctx, gen_base);
    }
    return rc;
}

static void collect_type_entry(CEmit *ctx,
                               TypeEntry **entries,
                               size_t *len,
                               size_t *cap,
                               const ChengNode *entry,
                               size_t module_index) {
    if (!ctx || !entries || !len || !cap || !entry) {
        return;
    }
    if (entry->kind == NK_TYPE_DECL) {
        if (entry->len < 2) {
            return;
        }
        const ChengNode *name_node = entry->kids[0];
        const ChengNode *type_node = entry->kids[1];
        const char *base_name = type_decl_base_name(name_node);
        if (!base_name) {
            return;
        }
        const char *c_name = resolve_type_c_name(ctx, base_name, module_index);
        if (!c_name) {
            c_name = type_map_add(ctx, base_name, module_index);
        }
        if (!c_name) {
            return;
        }
        if (ctx->allow_generic_erasure) {
            const ChengNode *generics = node_generics(entry);
            if (generics || (name_node && name_node->kind == NK_BRACKET_EXPR)) {
                char *key = type_decl_generic_key(name_node, generics, base_name);
                if (key && key[0] != '\0') {
                    type_map_add_alias(ctx, key, module_index, c_name);
                }
                free(key);
            }
        }
        TypeEntryKind kind = TYPE_ENTRY_ALIAS;
        if (type_node) {
            if (type_node->kind == NK_OBJECT_DECL) {
                kind = TYPE_ENTRY_OBJECT;
            } else if (type_node->kind == NK_TUPLE_TY) {
                kind = TYPE_ENTRY_TUPLE;
            } else if (type_node->kind == NK_ENUM_DECL) {
                kind = TYPE_ENTRY_ENUM;
            }
        }
        type_entries_add(entries, len, cap, entry, base_name, c_name, kind, module_index);
        return;
    }
    if (entry->kind == NK_OBJECT_DECL || entry->kind == NK_ENUM_DECL) {
        if (entry->len == 0) {
            return;
        }
        const ChengNode *name_node = entry->kids[0];
        const char *base_name = type_decl_base_name(name_node);
        if (!base_name) {
            return;
        }
        const char *c_name = resolve_type_c_name(ctx, base_name, module_index);
        if (!c_name) {
            c_name = type_map_add(ctx, base_name, module_index);
        }
        if (!c_name) {
            return;
        }
        if (ctx->allow_generic_erasure) {
            const ChengNode *generics = node_generics(entry);
            if (generics || (name_node && name_node->kind == NK_BRACKET_EXPR)) {
                char *key = type_decl_generic_key(name_node, generics, base_name);
                if (key && key[0] != '\0') {
                    type_map_add_alias(ctx, key, module_index, c_name);
                }
                free(key);
            }
        }
        TypeEntryKind kind = entry->kind == NK_ENUM_DECL ? TYPE_ENTRY_ENUM : TYPE_ENTRY_OBJECT;
        type_entries_add(entries, len, cap, entry, base_name, c_name, kind, module_index);
        return;
    }
}

static int emit_type_decls(CEmit *ctx, const ChengNode *list) {
    if (!list || list->kind != NK_STMT_LIST) {
        return 0;
    }
    TypeEntry *entries = NULL;
    size_t entry_len = 0;
    size_t entry_cap = 0;
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        size_t module_index = module_index_for_stmt(ctx, stmt);
        if (stmt->kind == NK_TYPE_DECL && stmt->len > 0 &&
            (stmt->kids[0]->kind == NK_OBJECT_DECL || stmt->kids[0]->kind == NK_TYPE_DECL)) {
            for (size_t j = 0; j < stmt->len; j++) {
                collect_type_entry(ctx, &entries, &entry_len, &entry_cap, stmt->kids[j], module_index);
            }
            continue;
        }
        collect_type_entry(ctx, &entries, &entry_len, &entry_cap, stmt, module_index);
    }
    if (entry_len == 0) {
        free(entries);
        return 0;
    }
    int *emitted = (int *)calloc(entry_len, sizeof(int));
    if (!emitted) {
        for (size_t i = 0; i < entry_len; i++) {
            ctx->current_module = entries[i].module_index;
            emit_type_entry(ctx, &entries[i]);
        }
        ctx->current_module = MODULE_INDEX_INVALID;
        for (size_t i = 0; i < entry_len; i++) {
            free(entries[i].c_name);
        }
        free(entries);
        return 0;
    }
    for (size_t i = 0; i < entry_len; i++) {
        if (entries[i].kind == TYPE_ENTRY_ENUM) {
            ctx->current_module = entries[i].module_index;
            emit_type_entry(ctx, &entries[i]);
            emitted[i] = 1;
        }
    }
    int progress = 1;
    while (progress) {
        progress = 0;
        for (size_t i = 0; i < entry_len; i++) {
            if (emitted[i]) {
                continue;
            }
            if (type_entry_unresolved_dep(ctx, &entries[i], entries, emitted, entry_len)) {
                continue;
            }
            ctx->current_module = entries[i].module_index;
            emit_type_entry(ctx, &entries[i]);
            emitted[i] = 1;
            progress = 1;
        }
    }
    for (size_t i = 0; i < entry_len; i++) {
        if (!emitted[i]) {
            ctx->current_module = entries[i].module_index;
            emit_type_entry(ctx, &entries[i]);
        }
    }
    ctx->current_module = MODULE_INDEX_INVALID;
    free(emitted);
    for (size_t i = 0; i < entry_len; i++) {
        free(entries[i].c_name);
    }
    free(entries);
    return 0;
}

static int emit_global_decls(CEmit *ctx, const ChengNode *list) {
    if (!list || list->kind != NK_STMT_LIST) {
        return 0;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        ctx->current_module = module_index_for_stmt(ctx, stmt);
        if (stmt->kind == NK_LET || stmt->kind == NK_VAR || stmt->kind == NK_CONST) {
            const ChengNode *pattern = stmt->len > 0 ? stmt->kids[0] : NULL;
            const ChengNode *type_node = stmt->len > 1 ? stmt->kids[1] : NULL;
            const ChengNode *value = stmt->len > 2 ? stmt->kids[2] : NULL;
            if (!pattern || pattern->len == 0 || !pattern->kids[0] || pattern->kids[0]->kind != NK_IDENT) {
                add_diag(ctx, stmt, "[C-Profile] unsupported: global pattern");
                continue;
            }
            const char *name = pattern->kids[0]->ident;
            if (!name) {
                continue;
            }
            const GlobalMapEntry *g = NULL;
            if (ctx->current_module != MODULE_INDEX_INVALID) {
                g = global_map_find(ctx, name, ctx->current_module);
            }
            if (!g) {
                g = global_map_find_unique(ctx, name);
            }
            const char *c_name = g && g->c_name ? g->c_name : name;
            char *ctype = NULL;
            if (!is_empty_node(type_node)) {
                ctype = type_to_c(ctx, type_node);
            } else if (!is_empty_node(value)) {
                ctype = infer_type_from_expr(ctx, value);
            } else {
                add_diag(ctx, stmt, "[C-Profile] unsupported: global requires init");
                continue;
            }
            int has_value = !is_empty_node(value);
            int const_init = has_value && is_const_expr(value);
            if (stmt->kind == NK_CONST && has_value && !const_init) {
                add_diag(ctx, stmt, "[C-Profile] unsupported: const requires constant init");
                free(ctype);
                continue;
            }
            if (stmt->kind == NK_CONST && ctx->emit_module_only) {
                fputs("static ", ctx->out);
            }
            if (stmt->kind == NK_CONST) {
                fputs("const ", ctx->out);
            }
            fputs(ctype ? ctype : "NI32", ctx->out);
            fputc(' ', ctx->out);
            char *san = sanitize_ident(c_name);
            fputs(san ? san : c_name, ctx->out);
            if (has_value && const_init) {
                fputs(" = ", ctx->out);
                emit_expr(ctx, value);
            } else {
                fprintf(ctx->out, " = %s", default_value_for_type(ctype));
            }
            fputs(";\n", ctx->out);
            if (has_value && !const_init && stmt->kind != NK_CONST) {
                global_init_add(ctx, san ? san : c_name, value, ctx->current_module);
            }
            free(san);
            free(ctype);
        }
    }
    ctx->current_module = MODULE_INDEX_INVALID;
    if (list->len > 0) {
        fputs("\n", ctx->out);
    }
    return 0;
}

static void emit_global_inits(CEmit *ctx) {
    if (!ctx || ctx->global_inits_len == 0) {
        return;
    }
    fputs("static void __cheng_init_globals(void) {\n", ctx->out);
    ctx->indent++;
    for (size_t i = 0; i < ctx->global_inits_len; i++) {
        const GlobalInit *init = &ctx->global_inits[i];
        if (!init->name || !init->value) {
            continue;
        }
        ctx->current_module = init->module_index;
        emit_indent(ctx);
        fputs(init->name, ctx->out);
        fputs(" = ", ctx->out);
        emit_expr(ctx, init->value);
        fputs(";\n", ctx->out);
    }
    ctx->current_module = MODULE_INDEX_INVALID;
    ctx->indent--;
    fputs("}\n\n", ctx->out);
}

static int emit_fn_prototype(CEmit *ctx, const FnSig *sig) {
    if (!ctx || !sig) {
        return 0;
    }
    if (sig->is_importc && sig->c_name && strcmp(sig->c_name, "getenv") == 0) {
        return 0;
    }
    const ChengNode *generics = NULL;
    size_t gen_base = 0;
    if (ctx->allow_generic_erasure && sig->node && fn_has_generics(sig->node)) {
        generics = node_generics(sig->node);
        if (generics) {
            gen_base = generic_params_push(ctx, generics);
        }
    }
    char *ret = type_to_c(ctx, sig->ret_type);
    int is_main = 0;
    if (sig->name && strcmp(sig->name, "main") == 0) {
        is_main = 1;
    } else if (sig->c_name && strcmp(sig->c_name, "main") == 0) {
        is_main = 1;
    }
    if (sig->is_importc && (!sig->ret_type || sig->ret_type->kind == NK_EMPTY)) {
        free(ret);
        ret = cheng_strdup("void");
    }
    if (is_main) {
        fputs("int main(", ctx->out);
    } else if (sig->is_importc) {
        fprintf(ctx->out, "N_CDECL(%s, %s)(", ret ? ret : "void", sig->c_name);
    } else {
        fprintf(ctx->out, "C_CHENGCALL(%s, %s)(", ret ? ret : "void", sig->c_name);
    }
    free(ret);
    if (!sig->params || sig->params->kind != NK_FORMAL_PARAMS || sig->params->len == 0) {
        fputs("void", ctx->out);
    } else {
        for (size_t i = 0; i < sig->params->len; i++) {
            const ChengNode *defs = sig->params->kids[i];
            if (!defs || defs->kind != NK_IDENT_DEFS || defs->len == 0) {
                continue;
            }
            const ChengNode *name_node = defs->kids[0];
            const ChengNode *type_node = defs->len > 1 ? defs->kids[1] : NULL;
            char *ctype = type_to_c(ctx, type_node);
            if (i > 0) {
                fputs(", ", ctx->out);
            }
            fputs(ctype ? ctype : "NI32", ctx->out);
            fputc(' ', ctx->out);
            if (name_node && name_node->kind == NK_IDENT && name_node->ident) {
                char *san = sanitize_ident(name_node->ident);
                fputs(san ? san : name_node->ident, ctx->out);
                free(san);
            } else {
                fputs("arg", ctx->out);
            }
            free(ctype);
        }
    }
    fputs(");\n", ctx->out);
    if (generics) {
        generic_params_pop(ctx, gen_base);
    }
    return 0;
}

static int emit_fn_def(CEmit *ctx, const ChengNode *fn) {
    if (!fn || fn->kind != NK_FN_DECL || fn->len < 4) {
        return 0;
    }
    if (fn_has_generics(fn)) {
        if (!ctx->allow_generic_erasure) {
            add_diag(ctx, fn, "[C-Profile] unsupported: generic fn");
            return -1;
        }
        return 0;
    }
    const ChengNode *name_node = fn->kids[0];
    if (!name_node || name_node->kind != NK_IDENT || !name_node->ident) {
        return 0;
    }
    const char *name = name_node->ident;
    locals_free(ctx);
    const ChengNode *params = fn->kids[1];
    size_t arity = 0;
    if (params && params->kind == NK_FORMAL_PARAMS) {
        arity = params->len;
    }
    const FnSig *sig = fn_sig_find_by_decl(ctx, fn, name);
    if (!sig && ctx->current_module != MODULE_INDEX_INVALID) {
        sig = fn_sig_find_in_module_arity(ctx, name, ctx->current_module, arity);
    }
    if (!sig) {
        sig = fn_sig_find_unique_arity(ctx, name, arity);
    }
    if (!sig) {
        sig = fn_sig_find_any_arity(ctx, name, arity);
    }
    if (params && params->kind == NK_FORMAL_PARAMS) {
        for (size_t i = 0; i < params->len; i++) {
            const ChengNode *defs = params->kids[i];
            if (!defs || defs->kind != NK_IDENT_DEFS || defs->len == 0) {
                continue;
            }
            const ChengNode *pname = defs->kids[0];
            const ChengNode *ptype = defs->len > 1 ? defs->kids[1] : NULL;
            if (pname && pname->kind == NK_IDENT && pname->ident) {
                locals_set(ctx, pname->ident, type_is_str_node(ptype), ptype);
            }
        }
    }
    const ChengNode *ret_type = fn->kids[2];
    const ChengNode *body = fn->kids[3];
    if (sig && sig->node != fn && params && sig->params && fn_params_match(params, sig->params)) {
        return 0;
    }
    if (sig && sig->is_importc) {
        return 0;
    }
    if (body && body->kind == NK_EMPTY) {
        return 0;
    }
    int is_async = fn_is_async(fn);
    int async_kind = 0;
    const ChengNode *emit_ret_type = ret_type;
    if (is_async) {
        emit_ret_type = async_ret_type_node(ctx, ret_type, &async_kind);
    }
    char *ret = type_to_c(ctx, emit_ret_type);
    int ret_is_void = ret && strcmp(ret, "void") == 0;
    int is_main = strcmp(name, "main") == 0;
    int main_default_ret = is_main && (!emit_ret_type || emit_ret_type->kind == NK_EMPTY || ret_is_void);
    ctx->current_ret_type = emit_ret_type;
    ctx->current_ret_is_void = ret_is_void;
    ctx->current_ret_is_async = is_async;
    ctx->current_async_value_kind = async_kind;
    const char *emit_name = name;
    char *owned_name = NULL;
    if (sig && sig->c_name) {
        emit_name = sig->c_name;
    } else {
        owned_name = resolve_c_name(ctx, name, 0, ctx->current_module, 0);
        if (owned_name) {
            emit_name = owned_name;
        }
    }
    if (is_main) {
        fprintf(ctx->out, "int %s(", emit_name);
    } else {
        fprintf(ctx->out, "C_CHENGCALL(%s, %s)(", ret ? ret : "void", emit_name);
    }
    free(ret);
    if (!params || params->kind != NK_FORMAL_PARAMS || params->len == 0) {
        fputs("void", ctx->out);
    } else {
        for (size_t i = 0; i < params->len; i++) {
            const ChengNode *defs = params->kids[i];
            if (!defs || defs->kind != NK_IDENT_DEFS || defs->len == 0) {
                continue;
            }
            const ChengNode *pname = defs->kids[0];
            const ChengNode *ptype = defs->len > 1 ? defs->kids[1] : NULL;
            char *ctype = type_to_c(ctx, ptype);
            if (i > 0) {
                fputs(", ", ctx->out);
            }
            fputs(ctype ? ctype : "NI32", ctx->out);
            fputc(' ', ctx->out);
            if (pname && pname->kind == NK_IDENT && pname->ident) {
                char *san = sanitize_ident(pname->ident);
                fputs(san ? san : pname->ident, ctx->out);
                free(san);
            } else {
                fputs("arg", ctx->out);
            }
            free(ctype);
        }
    }
    fputs(") {\n", ctx->out);
    ctx->indent++;
    if (ctx->global_inits_len > 0 && is_main) {
        emit_indent(ctx);
        fputs("__cheng_init_globals();\n", ctx->out);
    }
    int emitted_body = 0;
    if (main_default_ret) {
        emit_stmt_list(ctx, body);
        emit_indent(ctx);
        fputs("return 0;\n", ctx->out);
    } else {
        if (!ret_is_void && body && body->kind == NK_STMT_LIST && body->len == 1) {
            const ChengNode *stmt = body->kids[0];
            if (stmt && is_simple_expr_stmt_kind(stmt->kind)) {
                emit_return_body(ctx, stmt);
                emitted_body = 1;
            } else if (stmt && stmt->kind == NK_CASE) {
                emit_case_return(ctx, stmt);
                emitted_body = 1;
            } else if (stmt && (stmt->kind == NK_IF || stmt->kind == NK_WHEN)) {
                emit_if_return(ctx, stmt, 0);
                emitted_body = 1;
            }
        }
        if (!emitted_body) {
            emit_stmt_list(ctx, body);
        }
    }
    ctx->indent--;
    ctx->current_ret_type = NULL;
    ctx->current_ret_is_void = 0;
    ctx->current_ret_is_async = 0;
    ctx->current_async_value_kind = 0;
    fputs("}\n\n", ctx->out);
    if (owned_name) {
        free(owned_name);
    }
    return 0;
}

int cheng_emit_c(const ChengNode *root,
                 const char *filename,
                 const char *out_path,
                 ChengDiagList *diags,
                 const ChengNode *module_stmts,
                 const ChengCModuleInfo *modules,
                 size_t modules_len) {
    if (!root || !out_path || !diags) {
        return -1;
    }
    CEmit ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.filename = filename;
    ctx.diags = diags;
    ctx.emit_module_only = module_stmts != NULL;
    {
        const char *generic_erase = getenv("CHENG_C_GENERIC_ERASE");
        ctx.allow_generic_erasure = (generic_erase && generic_erase[0] != '\0');
    }
    const ChengNode *list = root;
    if (root->kind == NK_MODULE && root->len > 0) {
        list = root->kids[0];
    }
    const ChengNode *emit_list = module_stmts ? module_stmts : list;

    ctx.current_module = MODULE_INDEX_INVALID;
    modules_init(&ctx, modules, modules_len);
    collect_importc(&ctx, root);
    collect_fn_sigs_for_list(&ctx, list, ctx.allow_generic_erasure ? 0 : 1);
    collect_type_names_for_list(&ctx, list);
    collect_global_names_for_list(&ctx, list);

    const char *modsym_out = getenv("CHENG_C_MODSYM_OUT");
    if (modsym_out && modsym_out[0] != '\0') {
        const char *module_path = getenv("CHENG_C_MODULE");
        if (!module_stmts) {
            add_diag(&ctx, root, "module symbol output requires CHENG_C_MODULE");
            fn_sig_free(&ctx);
            type_map_free(&ctx);
            type_name_free(&ctx);
            ref_obj_name_free(&ctx);
            fwd_name_free(&ctx);
            import_map_free(&ctx);
            global_init_free(&ctx);
            global_map_free(&ctx);
            enum_field_map_free(&ctx);
            generic_params_free(&ctx);
            locals_free(&ctx);
            modules_free(&ctx);
            return -1;
        }
        CModuleSymbols *symbols = c_module_symbols_create(module_stmts);
        if (!symbols) {
            add_diag(&ctx, root, "failed to allocate module symbols");
            fn_sig_free(&ctx);
            type_map_free(&ctx);
            type_name_free(&ctx);
            ref_obj_name_free(&ctx);
            fwd_name_free(&ctx);
            import_map_free(&ctx);
            global_init_free(&ctx);
            global_map_free(&ctx);
            enum_field_map_free(&ctx);
            generic_params_free(&ctx);
            locals_free(&ctx);
            modules_free(&ctx);
            return -1;
        }
        if (c_module_symbols_write(symbols, module_path, modsym_out, ctx.diags, ctx.filename, root) != 0) {
            c_module_symbols_free(symbols);
            fn_sig_free(&ctx);
            type_map_free(&ctx);
            type_name_free(&ctx);
            ref_obj_name_free(&ctx);
            fwd_name_free(&ctx);
            import_map_free(&ctx);
            global_init_free(&ctx);
            global_map_free(&ctx);
            enum_field_map_free(&ctx);
            generic_params_free(&ctx);
            locals_free(&ctx);
            modules_free(&ctx);
            return -1;
        }
        c_module_symbols_free(symbols);
    }

    ctx.out = fopen(out_path, "w");
    if (!ctx.out) {
        add_diag(&ctx, root, "failed to open output");
        fn_sig_free(&ctx);
        type_map_free(&ctx);
        type_name_free(&ctx);
        ref_obj_name_free(&ctx);
        fwd_name_free(&ctx);
        import_map_free(&ctx);
        global_init_free(&ctx);
        global_map_free(&ctx);
        enum_field_map_free(&ctx);
        generic_params_free(&ctx);
        locals_free(&ctx);
        modules_free(&ctx);
        return -1;
    }
    fputs("#ifndef CHENG_INTBITS\n#define CHENG_INTBITS 32\n#endif\n", ctx.out);
    fputs("#include <stddef.h>\n", ctx.out);
    fputs("#include <stdint.h>\n", ctx.out);
    fputs("#include <stdlib.h>\n", ctx.out);
    fputs("#include <string.h>\n", ctx.out);
    fputs("#include \"chengbase.h\"\n", ctx.out);
    emit_system_helpers_type_aliases(&ctx);
    fputs("#include \"system_helpers.h\"\n", ctx.out);
    fputs("#undef ChengAwaitI32\n", ctx.out);
    fputs("#undef ChengAwaitVoid\n", ctx.out);
    fputs("#undef ChengChanI32\n", ctx.out);
    fputs("\n", ctx.out);

    emit_forward_decls(&ctx);
    emit_type_decls(&ctx, list);

    for (size_t i = 0; i < ctx.fns_len; i++) {
        ctx.current_module = ctx.fns[i].module_index;
        emit_fn_prototype(&ctx, &ctx.fns[i]);
    }
    ctx.current_module = MODULE_INDEX_INVALID;
    if (ctx.fns_len > 0) {
        fputs("\n", ctx.out);
    }

    emit_global_decls(&ctx, emit_list);
    emit_global_inits(&ctx);

    if (emit_list && emit_list->kind == NK_STMT_LIST) {
        for (size_t i = 0; i < emit_list->len; i++) {
            const ChengNode *stmt = emit_list->kids[i];
            if (!stmt || stmt->kind != NK_FN_DECL) {
                continue;
            }
            ctx.current_module = module_index_for_stmt(&ctx, stmt);
            emit_fn_def(&ctx, stmt);
        }
    }
    ctx.current_module = MODULE_INDEX_INVALID;

    fclose(ctx.out);
    fn_sig_free(&ctx);
    type_map_free(&ctx);
    type_name_free(&ctx);
    ref_obj_name_free(&ctx);
    fwd_name_free(&ctx);
    import_map_free(&ctx);
    global_init_free(&ctx);
    global_map_free(&ctx);
    enum_field_map_free(&ctx);
    generic_params_free(&ctx);
    locals_free(&ctx);
    modules_free(&ctx);
    return cheng_diags_has_error(diags) ? -1 : 0;
}
