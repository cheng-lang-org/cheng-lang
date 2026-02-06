#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "semantics.h"
#include "strlist.h"

static int sem_trace_enabled(void) {
    const char *env = getenv("CHENG_STAGE0C_SEM_TRACE");
    return env && env[0] != '\0' && env[0] != '0';
}

typedef enum {
    SYM_VAR = 0,
    SYM_CONST,
    SYM_LET,
    SYM_FN,
    SYM_TYPE
} ChengSymbolKind;

typedef struct {
    char *name;
    ChengSymbolKind kind;
    ChengTypeId type_id;
    int sendable;
    int line;
    int col;
} ChengSymbol;

typedef struct {
    ChengSymbol *data;
    size_t len;
    size_t cap;
    size_t *scopes;
    size_t scope_len;
    size_t scope_cap;
} ChengSymbolTable;

typedef struct {
    ChengDiagList *diags;
    const char *filename;
    ChengSymbolTable symbols;
    ChengStrList thread_boundaries;
    ChengStrList generic_value_params;
    size_t *generic_value_scopes;
    size_t generic_value_scope_len;
    size_t generic_value_scope_cap;
} ChengSem;

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

const char *cheng_type_name(ChengTypeId type) {
    switch (type) {
        case CHENG_TYPE_VOID: return "void";
        case CHENG_TYPE_BOOL: return "bool";
        case CHENG_TYPE_INT: return "int";
        case CHENG_TYPE_FLOAT: return "float";
        case CHENG_TYPE_STR: return "str";
        case CHENG_TYPE_CSTRING: return "cstring";
        case CHENG_TYPE_PTR: return "ptr";
        case CHENG_TYPE_REF: return "ref";
        case CHENG_TYPE_SEQ: return "seq";
        case CHENG_TYPE_TABLE: return "Table";
        case CHENG_TYPE_UNKNOWN:
        default: return "unknown";
    }
}

static void symbols_init(ChengSymbolTable *table) {
    memset(table, 0, sizeof(*table));
}

static void symbols_free(ChengSymbolTable *table) {
    for (size_t i = 0; i < table->len; i++) {
        free(table->data[i].name);
    }
    free(table->data);
    free(table->scopes);
    memset(table, 0, sizeof(*table));
}

static int symbols_push_scope(ChengSymbolTable *table) {
    if (table->scope_len + 1 > table->scope_cap) {
        size_t next = table->scope_cap == 0 ? 8 : table->scope_cap * 2;
        size_t *scopes = (size_t *)realloc(table->scopes, next * sizeof(size_t));
        if (!scopes) {
            return -1;
        }
        table->scopes = scopes;
        table->scope_cap = next;
    }
    table->scopes[table->scope_len++] = table->len;
    return 0;
}

static void symbols_pop_scope(ChengSymbolTable *table) {
    if (table->scope_len == 0) {
        return;
    }
    size_t start = table->scopes[table->scope_len - 1];
    for (size_t i = start; i < table->len; i++) {
        free(table->data[i].name);
    }
    table->len = start;
    table->scope_len--;
}

static ChengSymbol *symbols_lookup(ChengSymbolTable *table, const char *name) {
    if (!name) {
        return NULL;
    }
    for (size_t i = table->len; i > 0; i--) {
        ChengSymbol *sym = &table->data[i - 1];
        if (sym->name && strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

static int symbols_define(ChengSem *sem,
                          const char *name,
                          ChengSymbolKind kind,
                          ChengTypeId type_id,
                          int sendable,
                          ChengNode *node) {
    if (!name || !name[0]) {
        return -1;
    }
    size_t start = 0;
    if (sem->symbols.scope_len > 0) {
        start = sem->symbols.scopes[sem->symbols.scope_len - 1];
    }
    for (size_t i = sem->symbols.len; i > start; i--) {
        ChengSymbol *sym = &sem->symbols.data[i - 1];
        if (sym->name && strcmp(sym->name, name) == 0) {
            if (sem_trace_enabled()) {
                fprintf(stderr, "[stage0c-sem] dup name=%s scope=%zu\n",
                        name, sem->symbols.scope_len);
            }
            return 0;
        }
    }
    if (sem->symbols.len + 1 > sem->symbols.cap) {
        size_t next = sem->symbols.cap == 0 ? 16 : sem->symbols.cap * 2;
        ChengSymbol *data = (ChengSymbol *)realloc(sem->symbols.data, next * sizeof(ChengSymbol));
        if (!data) {
            return -1;
        }
        sem->symbols.data = data;
        sem->symbols.cap = next;
    }
    ChengSymbol *sym = &sem->symbols.data[sem->symbols.len++];
    sym->name = cheng_strdup(name);
    sym->kind = kind;
    sym->type_id = type_id;
    sym->sendable = sendable;
    sym->line = node ? node->line : 0;
    sym->col = node ? node->col : 0;
    return 0;
}

static int generic_value_scope_push(ChengSem *sem) {
    if (!sem) {
        return -1;
    }
    if (sem->generic_value_scope_len + 1 > sem->generic_value_scope_cap) {
        size_t next = sem->generic_value_scope_cap == 0 ? 8 : sem->generic_value_scope_cap * 2;
        size_t *scopes = (size_t *)realloc(sem->generic_value_scopes, next * sizeof(size_t));
        if (!scopes) {
            return -1;
        }
        sem->generic_value_scopes = scopes;
        sem->generic_value_scope_cap = next;
    }
    sem->generic_value_scopes[sem->generic_value_scope_len++] = sem->generic_value_params.len;
    return 0;
}

static void generic_value_scope_pop(ChengSem *sem) {
    if (!sem || sem->generic_value_scope_len == 0) {
        return;
    }
    size_t start = sem->generic_value_scopes[sem->generic_value_scope_len - 1];
    sem->generic_value_scope_len--;
    while (sem->generic_value_params.len > start) {
        sem->generic_value_params.len--;
        free(sem->generic_value_params.items[sem->generic_value_params.len]);
        sem->generic_value_params.items[sem->generic_value_params.len] = NULL;
    }
}

static void generic_value_param_add(ChengSem *sem, const char *name) {
    if (!sem || !name || !name[0]) {
        return;
    }
    cheng_strlist_push_unique(&sem->generic_value_params, name);
}

static int sem_is_generic_value_param(ChengSem *sem, ChengNode *node) {
    if (!sem || !node) {
        return 0;
    }
    if (node->kind != NK_IDENT && node->kind != NK_SYMBOL) {
        return 0;
    }
    if (!node->ident || !node->ident[0]) {
        return 0;
    }
    return cheng_strlist_has(&sem->generic_value_params, node->ident);
}

static int type_is_numeric(ChengTypeId type_id) {
    return type_id == CHENG_TYPE_INT || type_id == CHENG_TYPE_FLOAT;
}

static ChengTypeId type_from_ident(const char *name) {
    if (!name) {
        return CHENG_TYPE_UNKNOWN;
    }
    if (strcmp(name, "void") == 0) {
        return CHENG_TYPE_VOID;
    }
    if (strcmp(name, "bool") == 0) {
        return CHENG_TYPE_BOOL;
    }
    if (strcmp(name, "int") == 0 || strcmp(name, "int32") == 0 || strcmp(name, "int64") == 0) {
        return CHENG_TYPE_INT;
    }
    if (strcmp(name, "float") == 0 || strcmp(name, "float32") == 0 || strcmp(name, "float64") == 0) {
        return CHENG_TYPE_FLOAT;
    }
    if (strcmp(name, "str") == 0) {
        return CHENG_TYPE_STR;
    }
    if (strcmp(name, "cstring") == 0) {
        return CHENG_TYPE_CSTRING;
    }
    return CHENG_TYPE_UNKNOWN;
}

static int sem_type_is_send(ChengNode *node) {
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_VAR_TY:
            return 0;
        case NK_PTR_TY:
        case NK_REF_TY:
            return 0;
        case NK_TUPLE_TY:
            for (size_t i = 0; i < node->len; i++) {
                ChengNode *elem = node->kids[i];
                if (elem && elem->kind == NK_IDENT_DEFS && elem->len > 1) {
                    elem = elem->kids[1];
                }
                if (!sem_type_is_send(elem)) {
                    return 0;
                }
            }
            return 1;
        case NK_BRACKET_EXPR:
            if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT) {
                const char *name = node->kids[0]->ident;
                if (name && (strcmp(name, "Arc") == 0 || strcmp(name, "Mutex") == 0 ||
                             strcmp(name, "RwLock") == 0 || strcmp(name, "Atomic") == 0)) {
                    if (node->len > 1) {
                        return sem_type_is_send(node->kids[1]);
                    }
                    return 1;
                }
            }
            return 0;
        case NK_IDENT:
            if (!node->ident) {
                return 0;
            }
            if (strcmp(node->ident, "int") == 0 || strcmp(node->ident, "int32") == 0 ||
                strcmp(node->ident, "int64") == 0 || strcmp(node->ident, "uint") == 0 ||
                strcmp(node->ident, "uint32") == 0 || strcmp(node->ident, "uint64") == 0 ||
                strcmp(node->ident, "float") == 0 || strcmp(node->ident, "float32") == 0 ||
                strcmp(node->ident, "float64") == 0 || strcmp(node->ident, "bool") == 0 ||
                strcmp(node->ident, "char") == 0) {
                return 1;
            }
            return 0;
        default:
            return 0;
    }
}

static ChengNode *sem_call_arg_value(ChengNode *node) {
    if (node && node->kind == NK_CALL_ARG && node->len > 1) {
        return node->kids[1];
    }
    return node;
}

static int sem_is_empty_seq_literal(ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_CALL_ARG) {
        if (node->len > 1) {
            return sem_is_empty_seq_literal(node->kids[1]);
        }
        if (node->len > 0) {
            return sem_is_empty_seq_literal(node->kids[0]);
        }
        return 0;
    }
    if (node->kind == NK_PAR && node->len == 1) {
        return sem_is_empty_seq_literal(node->kids[0]);
    }
    return node->kind == NK_SEQ_LIT && node->len == 0;
}

static const char *sem_call_name(ChengNode *node) {
    if (!node || node->kind != NK_CALL || node->len == 0) {
        return NULL;
    }
    ChengNode *callee = node->kids[0];
    if (callee && callee->kind == NK_BRACKET_EXPR && callee->len > 0) {
        callee = callee->kids[0];
    }
    if (callee && callee->kind == NK_DOT_EXPR && callee->len > 1) {
        callee = callee->kids[1];
    }
    if (callee && (callee->kind == NK_IDENT || callee->kind == NK_SYMBOL)) {
        return callee->ident;
    }
    return NULL;
}

static int sem_expr_is_send(ChengSem *sem, ChengNode *node) {
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_INT_LIT:
        case NK_FLOAT_LIT:
        case NK_BOOL_LIT:
        case NK_CHAR_LIT:
            return 1;
        case NK_NIL_LIT:
            return 1;
        case NK_STR_LIT:
            return 0;
        case NK_IDENT: {
            ChengSymbol *sym = symbols_lookup(&sem->symbols, node->ident);
            return sym ? sym->sendable : 0;
        }
        case NK_CALL_ARG:
            return sem_expr_is_send(sem, sem_call_arg_value(node));
        case NK_PAR:
            if (node->len == 1) {
                return sem_expr_is_send(sem, node->kids[0]);
            }
            return 0;
        case NK_TUPLE_LIT:
            for (size_t i = 0; i < node->len; i++) {
                if (!sem_expr_is_send(sem, node->kids[i])) {
                    return 0;
                }
            }
            return 1;
        case NK_INFIX:
        case NK_PREFIX:
        case NK_POSTFIX:
            for (size_t i = 1; i < node->len; i++) {
                if (!sem_expr_is_send(sem, node->kids[i])) {
                    return 0;
                }
            }
            return 1;
        case NK_CALL: {
            const char *name = sem_call_name(node);
            if (!name) {
                return 0;
            }
            if (strcmp(name, "arcNew") == 0 || strcmp(name, "arcClone") == 0 ||
                strcmp(name, "share_mt") == 0 || strcmp(name, "mutexNew") == 0 ||
                strcmp(name, "rwLockNew") == 0 || strcmp(name, "atomicNew") == 0) {
                return 1;
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int sem_is_thread_boundary_pragma(ChengNode *node) {
    if (!node || node->kind != NK_PRAGMA || node->len == 0) {
        return 0;
    }
    ChengNode *expr = node->kids[0];
    if (!expr) {
        return 0;
    }
    if (expr->kind == NK_IDENT && expr->ident && strcmp(expr->ident, "thread_boundary") == 0) {
        return 1;
    }
    if (expr->kind == NK_CALL) {
        const char *name = sem_call_name(expr);
        if (name && strcmp(name, "thread_boundary") == 0) {
            return 1;
        }
    }
    return 0;
}

static void sem_mark_thread_boundary(ChengSem *sem, const char *name) {
    if (!sem || !name || !name[0]) {
        return;
    }
    cheng_strlist_push_unique(&sem->thread_boundaries, name);
}

static int sem_is_thread_boundary(ChengSem *sem, const char *name) {
    if (!sem || !name || !name[0]) {
        return 0;
    }
    return cheng_strlist_has(&sem->thread_boundaries, name);
}

static void sem_check_thread_boundary_call(ChengSem *sem, ChengNode *node) {
    if (!sem || !node || node->kind != NK_CALL) {
        return;
    }
    const char *name = sem_call_name(node);
    if (!name || !sem_is_thread_boundary(sem, name)) {
        return;
    }
    size_t pos_idx = 0;
    for (size_t i = 1; i < node->len; i++) {
        ChengNode *arg = node->kids[i];
        if (!arg) {
            continue;
        }
        const char *label = NULL;
        char label_buf[16];
        if (arg->kind == NK_CALL_ARG && arg->len > 1 && arg->kids[0] && arg->kids[0]->kind == NK_IDENT) {
            label = arg->kids[0]->ident;
        } else {
            snprintf(label_buf, sizeof(label_buf), "#%zu", pos_idx + 1);
            label = label_buf;
            pos_idx++;
        }
        ChengNode *val = sem_call_arg_value(arg);
        if (val && !sem_expr_is_send(sem, val)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "thread boundary requires Send/Sync: %s arg %s",
                     name, label ? label : "?");
            cheng_diag_add(sem->diags, CHENG_SV_ERROR, sem->filename,
                           val->line, val->col, msg);
        }
    }
}

static void sem_check_send_ctor_call(ChengSem *sem, ChengNode *node) {
    if (!sem || !node || node->kind != NK_CALL) {
        return;
    }
    const char *name = sem_call_name(node);
    if (!name) {
        return;
    }
    if (strcmp(name, "arcNew") != 0 && strcmp(name, "share_mt") != 0 &&
        strcmp(name, "mutexNew") != 0 && strcmp(name, "rwLockNew") != 0 &&
        strcmp(name, "atomicNew") != 0) {
        return;
    }
    size_t pos_idx = 0;
    for (size_t i = 1; i < node->len; i++) {
        ChengNode *arg = node->kids[i];
        if (!arg) {
            continue;
        }
        const char *label = NULL;
        char label_buf[16];
        if (arg->kind == NK_CALL_ARG && arg->len > 1 && arg->kids[0] && arg->kids[0]->kind == NK_IDENT) {
            label = arg->kids[0]->ident;
        } else {
            snprintf(label_buf, sizeof(label_buf), "#%zu", pos_idx + 1);
            label = label_buf;
            pos_idx++;
        }
        ChengNode *val = sem_call_arg_value(arg);
        if (val && !sem_expr_is_send(sem, val)) {
            if (sem_is_generic_value_param(sem, val)) {
                continue;
            }
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "shared ctor requires Send/Sync: %s arg %s",
                     name, label ? label : "?");
            cheng_diag_add(sem->diags, CHENG_SV_ERROR, sem->filename,
                           val->line, val->col, msg);
        }
    }
}

static ChengTypeId sem_type_from_typeexpr(ChengSem *sem, ChengNode *node);

static ChengTypeId sem_type_from_typeexpr(ChengSem *sem, ChengNode *node) {
    if (!node) {
        return CHENG_TYPE_UNKNOWN;
    }
    switch (node->kind) {
        case NK_IDENT: {
            ChengTypeId t = type_from_ident(node->ident);
            node->type_id = t;
            return t;
        }
        case NK_PTR_TY:
            node->type_id = CHENG_TYPE_PTR;
            if (node->len > 0) {
                sem_type_from_typeexpr(sem, node->kids[0]);
            }
            return CHENG_TYPE_PTR;
        case NK_REF_TY:
            node->type_id = CHENG_TYPE_REF;
            if (node->len > 0) {
                sem_type_from_typeexpr(sem, node->kids[0]);
            }
            return CHENG_TYPE_REF;
        case NK_VAR_TY:
            if (node->len > 0) {
                ChengTypeId inner = sem_type_from_typeexpr(sem, node->kids[0]);
                node->type_id = inner;
                return inner;
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        case NK_FN_TY:
            if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_FORMAL_PARAMS) {
                ChengNode *params = node->kids[0];
                for (size_t i = 0; i < params->len; i++) {
                    ChengNode *defs = params->kids[i];
                    if (!defs || defs->kind != NK_IDENT_DEFS || defs->len < 2) {
                        continue;
                    }
                    sem_type_from_typeexpr(sem, defs->kids[1]);
                }
            }
            if (node->len > 1 && node->kids[1]) {
                sem_type_from_typeexpr(sem, node->kids[1]);
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        case NK_BRACKET_EXPR:
            if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT) {
                const char *name = node->kids[0]->ident;
                if (name && (strcmp(name, "seq") == 0 || strcmp(name, "seqT") == 0)) {
                    node->type_id = CHENG_TYPE_SEQ;
                    return CHENG_TYPE_SEQ;
                }
                if (name && (strcmp(name, "Table") == 0 || strcmp(name, "table") == 0)) {
                    node->type_id = CHENG_TYPE_TABLE;
                    return CHENG_TYPE_TABLE;
                }
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        default:
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
    }
}

static ChengTypeId sem_eval_expr_ctx(ChengSem *sem, ChengNode *node, int allow_empty_seq);

static ChengTypeId sem_eval_expr(ChengSem *sem, ChengNode *node) {
    return sem_eval_expr_ctx(sem, node, 0);
}

static ChengTypeId sem_eval_expr_ctx(ChengSem *sem, ChengNode *node, int allow_empty_seq) {
    if (!node) {
        return CHENG_TYPE_UNKNOWN;
    }
    if (node->kind == NK_CALL_ARG) {
        if (node->len > 1) {
            sem_eval_expr_ctx(sem, node->kids[1], allow_empty_seq);
        } else if (node->len > 0) {
            sem_eval_expr_ctx(sem, node->kids[0], allow_empty_seq);
        }
        node->type_id = CHENG_TYPE_UNKNOWN;
        return CHENG_TYPE_UNKNOWN;
    }
    if (node->kind == NK_PAR && node->len == 1) {
        sem_eval_expr_ctx(sem, node->kids[0], allow_empty_seq);
        node->type_id = CHENG_TYPE_UNKNOWN;
        return CHENG_TYPE_UNKNOWN;
    }
    if (node->kind == NK_SEQ_LIT && node->len == 0) {
        if (!allow_empty_seq) {
            cheng_diag_add(sem->diags, CHENG_SV_ERROR, sem->filename,
                           node->line, node->col,
                           "empty seq literal can only be used in assignment/return/call arguments");
        }
        node->type_id = CHENG_TYPE_UNKNOWN;
        return CHENG_TYPE_UNKNOWN;
    }
    switch (node->kind) {
        case NK_INT_LIT:
            node->type_id = CHENG_TYPE_INT;
            return CHENG_TYPE_INT;
        case NK_FLOAT_LIT:
            node->type_id = CHENG_TYPE_FLOAT;
            return CHENG_TYPE_FLOAT;
        case NK_STR_LIT:
        case NK_CHAR_LIT:
            node->type_id = CHENG_TYPE_STR;
            return CHENG_TYPE_STR;
        case NK_BOOL_LIT:
            node->type_id = CHENG_TYPE_BOOL;
            return CHENG_TYPE_BOOL;
        case NK_NIL_LIT:
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        case NK_IDENT: {
            ChengSymbol *sym = symbols_lookup(&sem->symbols, node->ident);
            if (!sym) {
                symbols_define(sem, node->ident, SYM_VAR, CHENG_TYPE_UNKNOWN, 0, node);
                sym = symbols_lookup(&sem->symbols, node->ident);
                if (!sym) {
                    node->type_id = CHENG_TYPE_UNKNOWN;
                    return CHENG_TYPE_UNKNOWN;
                }
            }
            node->type_id = sym->type_id;
            return sym->type_id;
        }
        case NK_CALL: {
            if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT) {
                ChengSymbol *sym = symbols_lookup(&sem->symbols, node->kids[0]->ident);
                if (!sym) {
                    symbols_define(sem, node->kids[0]->ident, SYM_FN, CHENG_TYPE_UNKNOWN, 1, node->kids[0]);
                }
            }
            for (size_t i = 0; i < node->len; i++) {
                if (i == 0) {
                    sem_eval_expr(sem, node->kids[i]);
                } else {
                    sem_eval_expr_ctx(sem, node->kids[i], 1);
                }
            }
            sem_check_send_ctor_call(sem, node);
            sem_check_thread_boundary_call(sem, node);
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        }
        case NK_CAST: {
            ChengTypeId t = CHENG_TYPE_UNKNOWN;
            if (node->len > 0 && node->kids[0]) {
                t = sem_type_from_typeexpr(sem, node->kids[0]);
            }
            if (node->len > 1 && node->kids[1]) {
                sem_eval_expr(sem, node->kids[1]);
            }
            node->type_id = t;
            return t;
        }
        case NK_IF_EXPR:
        case NK_WHEN_EXPR:
            if (node->len > 0) {
                sem_eval_expr(sem, node->kids[0]);
            }
            if (node->len > 1) {
                sem_eval_expr(sem, node->kids[1]);
            }
            if (node->len > 2) {
                sem_eval_expr(sem, node->kids[2]);
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        case NK_CASE_EXPR:
            for (size_t i = 0; i < node->len; i++) {
                sem_eval_expr(sem, node->kids[i]);
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        case NK_SEQ_LIT:
        case NK_BRACKET:
            for (size_t i = 0; i < node->len; i++) {
                sem_eval_expr(sem, node->kids[i]);
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        case NK_COMPREHENSION: {
            ChengNode *pattern = node->len > 0 ? node->kids[0] : NULL;
            ChengNode *iter = node->len > 1 ? node->kids[1] : NULL;
            ChengNode *body = node->len > 2 ? node->kids[2] : NULL;
            if (iter) {
                sem_eval_expr(sem, iter);
            }
            symbols_push_scope(&sem->symbols);
            if (pattern && pattern->len > 0 && pattern->kids[0] && pattern->kids[0]->kind == NK_IDENT) {
                symbols_define(sem, pattern->kids[0]->ident, SYM_VAR, CHENG_TYPE_INT, 1, pattern->kids[0]);
            }
            if (body) {
                sem_eval_expr(sem, body);
            }
            symbols_pop_scope(&sem->symbols);
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        }
        case NK_INFIX: {
            ChengTypeId lhs = CHENG_TYPE_UNKNOWN;
            ChengTypeId rhs = CHENG_TYPE_UNKNOWN;
            const char *op = NULL;
            if (node->len > 0 && node->kids[0]) {
                op = node->kids[0]->ident;
            }
            if (node->len > 1) {
                lhs = sem_eval_expr(sem, node->kids[1]);
            }
            if (node->len > 2) {
                rhs = sem_eval_expr(sem, node->kids[2]);
            }
            if (op) {
                if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 ||
                    strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                    strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
                    strcmp(op, ">") == 0 || strcmp(op, ">=") == 0 ||
                    strcmp(op, "in") == 0 || strcmp(op, "notin") == 0) {
                    node->type_id = CHENG_TYPE_BOOL;
                    return CHENG_TYPE_BOOL;
                }
                if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
                    strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
                    strcmp(op, "mod") == 0) {
                    if (type_is_numeric(lhs)) {
                        node->type_id = lhs;
                        return lhs;
                    }
                    if (type_is_numeric(rhs)) {
                        node->type_id = rhs;
                        return rhs;
                    }
                }
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        }
        case NK_PREFIX:
            if (node->len > 1) {
                sem_eval_expr(sem, node->kids[1]);
            } else if (node->len > 0) {
                sem_eval_expr(sem, node->kids[0]);
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        case NK_POSTFIX:
        case NK_DOT_EXPR:
        case NK_BRACKET_EXPR:
        case NK_PAR:
        case NK_HIDDEN_DEREF:
        case NK_DEREF_EXPR: {
            for (size_t i = 0; i < node->len; i++) {
                sem_eval_expr(sem, node->kids[i]);
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
        }
        default:
            for (size_t i = 0; i < node->len; i++) {
                sem_eval_expr(sem, node->kids[i]);
            }
            node->type_id = CHENG_TYPE_UNKNOWN;
            return CHENG_TYPE_UNKNOWN;
    }
}

static int sem_is_fn_decl_node(ChengNode *node) {
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_FN_DECL:
        case NK_ITERATOR_DECL:
            return 1;
        default:
            return 0;
    }
}

static void sem_visit(ChengSem *sem, ChengNode *node);

static void sem_visit_list(ChengSem *sem, ChengNode *node, int scoped) {
    if (!sem || !node) {
        return;
    }
    if (scoped) {
        symbols_push_scope(&sem->symbols);
    }
    int pending_thread_boundary = 0;
    int pending_pragmas = 0;
    for (size_t i = 0; i < node->len; i++) {
        ChengNode *child = node->kids[i];
        if (!child) {
            continue;
        }
        if (child->kind == NK_PRAGMA) {
            pending_pragmas = 1;
            if (sem_is_thread_boundary_pragma(child)) {
                pending_thread_boundary = 1;
            }
            sem_visit(sem, child);
            continue;
        }
        if (pending_pragmas) {
            if (pending_thread_boundary && sem_is_fn_decl_node(child)) {
                if (child->len > 0 && child->kids[0] &&
                    (child->kids[0]->kind == NK_IDENT || child->kids[0]->kind == NK_SYMBOL)) {
                    sem_mark_thread_boundary(sem, child->kids[0]->ident);
                }
            }
            pending_pragmas = 0;
            pending_thread_boundary = 0;
        }
        sem_visit(sem, child);
    }
    if (scoped) {
        symbols_pop_scope(&sem->symbols);
    }
}

static void sem_visit(ChengSem *sem, ChengNode *node) {
    if (!node) {
        return;
    }
    switch (node->kind) {
        case NK_MODULE:
            sem_visit_list(sem, node, 0);
            break;
        case NK_STMT_LIST:
            sem_visit_list(sem, node, sem->symbols.scope_len > 1);
            break;
        case NK_FN_DECL: {
            if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT) {
                symbols_define(sem, node->kids[0]->ident, SYM_FN, CHENG_TYPE_UNKNOWN, 1, node->kids[0]);
            }
            if (sem_trace_enabled() && node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT) {
                fprintf(stderr, "[stage0c-sem] enter fn=%s scope=%zu\n",
                        node->kids[0]->ident ? node->kids[0]->ident : "",
                        sem->symbols.scope_len);
            }
            symbols_push_scope(&sem->symbols);
            generic_value_scope_push(sem);
            if (node->len > 1 && node->kids[1] && node->kids[1]->kind == NK_FORMAL_PARAMS) {
                ChengNode *params = node->kids[1];
                for (size_t i = 0; i < params->len; i++) {
                    ChengNode *defs = params->kids[i];
                    if (!defs || defs->kind != NK_IDENT_DEFS || defs->len == 0) {
                        continue;
                    }
                    ChengNode *name = defs->kids[0];
                    ChengTypeId t = CHENG_TYPE_UNKNOWN;
                    int sendable = 0;
                    int unknown_param = 0;
                    if (defs->len > 1) {
                        ChengNode *type_node = defs->kids[1];
                        t = sem_type_from_typeexpr(sem, type_node);
                        sendable = sem_type_is_send(type_node);
                        if (type_node && (type_node->kind == NK_IDENT || type_node->kind == NK_SYMBOL)) {
                            if (type_from_ident(type_node->ident) == CHENG_TYPE_UNKNOWN) {
                                unknown_param = 1;
                            }
                        }
                    }
                    if (name && name->kind == NK_IDENT) {
                        symbols_define(sem, name->ident, SYM_VAR, t, sendable, name);
                        if (unknown_param) {
                            generic_value_param_add(sem, name->ident);
                        }
                    }
                }
            }
            if (node->len > 3) {
                sem_visit(sem, node->kids[3]);
            }
            symbols_pop_scope(&sem->symbols);
            generic_value_scope_pop(sem);
            if (sem_trace_enabled() && node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT) {
                fprintf(stderr, "[stage0c-sem] exit fn=%s scope=%zu\n",
                        node->kids[0]->ident ? node->kids[0]->ident : "",
                        sem->symbols.scope_len);
            }
            break;
        }
        case NK_LET:
        case NK_VAR:
        case NK_CONST: {
            ChengNode *pattern = node->len > 0 ? node->kids[0] : NULL;
            ChengNode *type_node = node->len > 1 ? node->kids[1] : NULL;
            ChengNode *value = node->len > 2 ? node->kids[2] : NULL;
            ChengTypeId t = sem_type_from_typeexpr(sem, type_node);
            int sendable = 0;
            int empty_seq = sem_is_empty_seq_literal(value);
            if (empty_seq) {
                cheng_diag_add(sem->diags, CHENG_SV_ERROR, sem->filename,
                               value->line, value->col,
                               "empty seq literal is not allowed for default initialization; omit the initializer");
            }
            if (t == CHENG_TYPE_UNKNOWN && value && !empty_seq) {
                t = sem_eval_expr(sem, value);
            } else if (value && !empty_seq) {
                sem_eval_expr(sem, value);
            }
            if (type_node && type_node->kind != NK_EMPTY) {
                sendable = sem_type_is_send(type_node);
            } else if (value) {
                sendable = sem_expr_is_send(sem, value);
            }
            if (pattern && pattern->len > 0 && pattern->kids[0] && pattern->kids[0]->kind == NK_IDENT) {
                ChengSymbolKind kind = SYM_VAR;
                if (node->kind == NK_LET) {
                    kind = SYM_LET;
                } else if (node->kind == NK_CONST) {
                    kind = SYM_CONST;
                }
                symbols_define(sem, pattern->kids[0]->ident, kind, t, sendable, pattern->kids[0]);
            } else {
                cheng_diag_add(sem->diags, CHENG_SV_ERROR, sem->filename,
                               node->line, node->col,
                               "invalid binding pattern");
            }
            break;
        }
        case NK_RETURN:
            if (node->len > 0) {
                sem_eval_expr_ctx(sem, node->kids[0], 1);
            }
            break;
        case NK_IF:
        case NK_WHEN:
            if (node->len > 0) {
                sem_eval_expr(sem, node->kids[0]);
            }
            if (node->len > 1) {
                sem_visit(sem, node->kids[1]);
            }
            if (node->len > 2) {
                sem_visit(sem, node->kids[2]);
            }
            break;
        case NK_FOR: {
            ChengNode *pattern = node->len > 0 ? node->kids[0] : NULL;
            ChengNode *iter = node->len > 1 ? node->kids[1] : NULL;
            ChengNode *body = node->len > 2 ? node->kids[2] : NULL;
            if (iter) {
                sem_eval_expr(sem, iter);
            }
            symbols_push_scope(&sem->symbols);
            if (pattern && pattern->len > 0 && pattern->kids[0] && pattern->kids[0]->kind == NK_IDENT) {
                symbols_define(sem, pattern->kids[0]->ident, SYM_VAR, CHENG_TYPE_INT, 1, pattern->kids[0]);
            }
            if (body) {
                sem_visit(sem, body);
            }
            symbols_pop_scope(&sem->symbols);
            break;
        }
        case NK_WHILE:
            if (node->len > 0) {
                sem_eval_expr(sem, node->kids[0]);
            }
            if (node->len > 1) {
                sem_visit(sem, node->kids[1]);
            }
            break;
        case NK_CONTINUE:
            break;
        case NK_BREAK:
            break;
        case NK_DEFER:
            if (node->len > 0) {
                sem_visit(sem, node->kids[0]);
            }
            break;
        case NK_ELSE:
            if (node->len > 0) {
                sem_visit(sem, node->kids[0]);
            }
            break;
        case NK_CASE:
            if (node->len > 0) {
                sem_eval_expr(sem, node->kids[0]);
            }
            for (size_t i = 1; i < node->len; i++) {
                ChengNode *branch = node->kids[i];
                if (!branch) {
                    continue;
                }
                if (branch->kind == NK_OF_BRANCH) {
                    if (branch->len > 0) {
                        sem_eval_expr(sem, branch->kids[0]);
                    }
                    if (branch->len > 1) {
                        sem_visit(sem, branch->kids[1]);
                    }
                } else {
                    sem_visit(sem, branch);
                }
            }
            break;
        case NK_PRAGMA:
            if (node->len > 0) {
                sem_eval_expr(sem, node->kids[0]);
            }
            break;
        case NK_ASGN:
        case NK_FAST_ASGN: {
            ChengNode *lhs = node->len > 0 ? node->kids[0] : NULL;
            ChengNode *rhs = node->len > 1 ? node->kids[1] : NULL;
            if (lhs) {
                sem_eval_expr(sem, lhs);
            }
            if (rhs) {
                sem_eval_expr_ctx(sem, rhs, 1);
            }
            if (lhs && rhs && (lhs->kind == NK_IDENT || lhs->kind == NK_SYMBOL)) {
                ChengSymbol *sym = symbols_lookup(&sem->symbols, lhs->ident);
                if (sym) {
                    sym->sendable = sem_expr_is_send(sem, rhs);
                }
            }
            break;
        }
        default:
            sem_eval_expr(sem, node);
            break;
    }
}

int cheng_sem_analyze(ChengNode *root, const char *filename, ChengDiagList *diags) {
    if (!root || !diags) {
        return -1;
    }
    ChengSem sem;
    memset(&sem, 0, sizeof(sem));
    sem.diags = diags;
    sem.filename = filename ? filename : "";
    symbols_init(&sem.symbols);
    cheng_strlist_init(&sem.thread_boundaries);
    cheng_strlist_init(&sem.generic_value_params);
    symbols_push_scope(&sem.symbols);
    sem_visit(&sem, root);
    symbols_pop_scope(&sem.symbols);
    symbols_free(&sem.symbols);
    cheng_strlist_free(&sem.thread_boundaries);
    cheng_strlist_free(&sem.generic_value_params);
    free(sem.generic_value_scopes);
    if (cheng_diags_has_error(diags)) {
        return -1;
    }
    return 0;
}
