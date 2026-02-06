#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "macro_expand.h"

typedef struct {
    const char *name;
    const ChengNode *decl;
} MacroDecl;

typedef struct {
    MacroDecl *items;
    size_t len;
    size_t cap;
} MacroDeclList;

typedef struct {
    const char *name;
    const ChengNode *value;
} MacroArg;

typedef struct {
    MacroArg *items;
    size_t len;
    size_t cap;
} MacroArgList;

typedef struct {
    const char **items;
    size_t len;
    size_t cap;
} MacroStack;

typedef struct {
    const char *filename;
    ChengDiagList *diags;
    const MacroDeclList *decls;
    MacroStack stack;
    int hard_rt;
} MacroCtx;

static void macro_list_init(MacroDeclList *list) {
    if (!list) {
        return;
    }
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static void macro_list_free(MacroDeclList *list) {
    if (!list) {
        return;
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static const MacroDecl *macro_list_find(const MacroDeclList *list, const char *name) {
    if (!list || !name) {
        return NULL;
    }
    for (size_t i = 0; i < list->len; i++) {
        if (list->items[i].name && strcmp(list->items[i].name, name) == 0) {
            return &list->items[i];
        }
    }
    return NULL;
}

static int macro_list_add(MacroDeclList *list, const char *name, const ChengNode *decl) {
    if (!list || !name || !decl) {
        return -1;
    }
    for (size_t i = 0; i < list->len; i++) {
        if (list->items[i].name && strcmp(list->items[i].name, name) == 0) {
            list->items[i].decl = decl;
            return 0;
        }
    }
    if (list->len >= list->cap) {
        size_t next = list->cap == 0 ? 8 : list->cap * 2;
        MacroDecl *items = (MacroDecl *)realloc(list->items, next * sizeof(*items));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->cap = next;
    }
    list->items[list->len].name = name;
    list->items[list->len].decl = decl;
    list->len++;
    return 0;
}

static void macro_args_init(MacroArgList *list) {
    if (!list) {
        return;
    }
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static void macro_args_free(MacroArgList *list) {
    if (!list) {
        return;
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static int macro_args_add(MacroArgList *list, const char *name, const ChengNode *value) {
    if (!list || !name) {
        return -1;
    }
    if (list->len >= list->cap) {
        size_t next = list->cap == 0 ? 8 : list->cap * 2;
        MacroArg *items = (MacroArg *)realloc(list->items, next * sizeof(*items));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->cap = next;
    }
    list->items[list->len].name = name;
    list->items[list->len].value = value;
    list->len++;
    return 0;
}

static const ChengNode *macro_args_find(const MacroArgList *list, const char *name) {
    if (!list || !name) {
        return NULL;
    }
    for (size_t i = 0; i < list->len; i++) {
        if (list->items[i].name && strcmp(list->items[i].name, name) == 0) {
            return list->items[i].value;
        }
    }
    return NULL;
}

static void macro_stack_init(MacroStack *stack) {
    if (!stack) {
        return;
    }
    stack->items = NULL;
    stack->len = 0;
    stack->cap = 0;
}

static void macro_stack_free(MacroStack *stack) {
    if (!stack) {
        return;
    }
    free(stack->items);
    stack->items = NULL;
    stack->len = 0;
    stack->cap = 0;
}

static int macro_stack_has(const MacroStack *stack, const char *name) {
    if (!stack || !name) {
        return 0;
    }
    for (size_t i = 0; i < stack->len; i++) {
        if (stack->items[i] && strcmp(stack->items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int macro_stack_push(MacroStack *stack, const char *name) {
    if (!stack || !name) {
        return -1;
    }
    if (stack->len >= stack->cap) {
        size_t next = stack->cap == 0 ? 8 : stack->cap * 2;
        const char **items = (const char **)realloc(stack->items, next * sizeof(*items));
        if (!items) {
            return -1;
        }
        stack->items = items;
        stack->cap = next;
    }
    stack->items[stack->len++] = name;
    return 0;
}

static void macro_stack_pop(MacroStack *stack) {
    if (!stack || stack->len == 0) {
        return;
    }
    stack->len--;
}

static void macro_add_diag(MacroCtx *ctx, const ChengNode *node, const char *msg) {
    if (!ctx || !ctx->diags || !msg) {
        return;
    }
    int line = node ? node->line : 1;
    int col = node ? node->col : 1;
    cheng_diag_add(ctx->diags, CHENG_SV_ERROR, ctx->filename, line, col, msg);
}

static int macro_str_eq_ci(const char *a, const char *b) {
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

static int macro_env_is_hard_rt(void) {
    const char *value = getenv("CHENG_ASM_RT");
    if (!value || value[0] == '\0') {
        value = getenv("CHENG_HARD_RT");
    }
    if (!value || value[0] == '\0') {
        return 0;
    }
    if (macro_str_eq_ci(value, "hard") || macro_str_eq_ci(value, "hard-rt") || macro_str_eq_ci(value, "hard_rt")) {
        return 1;
    }
    if (macro_str_eq_ci(value, "true") || macro_str_eq_ci(value, "yes") || macro_str_eq_ci(value, "on")) {
        return 1;
    }
    return strcmp(value, "1") == 0;
}

static const ChengNode *macro_decl_name_node(const ChengNode *decl) {
    if (!decl || decl->len == 0) {
        return NULL;
    }
    return decl->kids[0];
}

static const char *macro_decl_name(const ChengNode *decl) {
    const ChengNode *name = macro_decl_name_node(decl);
    if (name && (name->kind == NK_IDENT || name->kind == NK_SYMBOL) && name->ident) {
        return name->ident;
    }
    return NULL;
}

static const ChengNode *macro_decl_params(const ChengNode *decl) {
    if (!decl || decl->len < 2) {
        return NULL;
    }
    return decl->kids[1];
}

static const ChengNode *macro_decl_body(const ChengNode *decl) {
    if (!decl || decl->len < 4) {
        return NULL;
    }
    return decl->kids[3];
}

static const char *macro_call_name(const ChengNode *call) {
    if (!call || call->kind != NK_CALL || call->len == 0) {
        return NULL;
    }
    const ChengNode *callee = call->kids[0];
    if (callee && callee->kind == NK_BRACKET_EXPR && callee->len > 0) {
        callee = callee->kids[0];
    }
    if (callee && callee->kind == NK_DOT_EXPR && callee->len > 1) {
        callee = callee->kids[1];
    }
    if (callee && (callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) && callee->ident) {
        return callee->ident;
    }
    return NULL;
}

static int macro_collect_args(MacroCtx *ctx,
                              const ChengNode *decl,
                              const ChengNode *call,
                              MacroArgList *out) {
    if (!decl || !call || !out) {
        return -1;
    }
    const ChengNode *params = macro_decl_params(decl);
    if (!params || params->kind != NK_FORMAL_PARAMS) {
        return 0;
    }
    const ChengNode **pos_args = NULL;
    size_t pos_len = 0;
    size_t pos_cap = 0;
    const char **named_names = NULL;
    const ChengNode **named_vals = NULL;
    size_t named_len = 0;
    size_t named_cap = 0;

    for (size_t i = 1; i < call->len; i++) {
        const ChengNode *arg = call->kids[i];
        if (!arg) {
            continue;
        }
        if (arg->kind == NK_CALL_ARG && arg->len > 0) {
            const ChengNode *name = arg->kids[0];
            const ChengNode *value = arg->len > 1 ? arg->kids[1] : NULL;
            if (!name || name->kind != NK_IDENT || !name->ident) {
                continue;
            }
            if (named_len >= named_cap) {
                size_t next = named_cap == 0 ? 8 : named_cap * 2;
                const char **next_names = (const char **)realloc(named_names, next * sizeof(*next_names));
                const ChengNode **next_vals = (const ChengNode **)realloc(named_vals, next * sizeof(*next_vals));
                if (!next_names || !next_vals) {
                    free(next_names);
                    free(next_vals);
                    free(pos_args);
                    free(named_names);
                    free(named_vals);
                    return -1;
                }
                named_names = next_names;
                named_vals = next_vals;
                named_cap = next;
            }
            named_names[named_len] = name->ident;
            named_vals[named_len] = value;
            named_len++;
        } else {
            if (pos_len >= pos_cap) {
                size_t next = pos_cap == 0 ? 8 : pos_cap * 2;
                const ChengNode **next_args = (const ChengNode **)realloc(pos_args, next * sizeof(*next_args));
                if (!next_args) {
                    free(pos_args);
                    free(named_names);
                    free(named_vals);
                    return -1;
                }
                pos_args = next_args;
                pos_cap = next;
            }
            pos_args[pos_len++] = arg;
        }
    }

    size_t pos_index = 0;
    int ok = 1;
    int *named_used = NULL;
    if (named_len > 0) {
        named_used = (int *)calloc(named_len, sizeof(int));
        if (!named_used) {
            ok = 0;
        }
    }
    if (ok) {
        for (size_t i = 0; i < params->len; i++) {
            const ChengNode *param = params->kids[i];
            if (!param || param->kind != NK_IDENT_DEFS || param->len == 0) {
                continue;
            }
            const ChengNode *param_name = param->kids[0];
            if (!param_name || param_name->kind != NK_IDENT || !param_name->ident) {
                continue;
            }
            const ChengNode *value = NULL;
            for (size_t j = 0; j < named_len; j++) {
                if (named_names[j] && strcmp(named_names[j], param_name->ident) == 0) {
                    value = named_vals[j];
                    if (named_used) {
                        named_used[j] = 1;
                    }
                    break;
                }
            }
            if (!value && pos_index < pos_len) {
                value = pos_args[pos_index++];
            }
            if (!value) {
                macro_add_diag(ctx, call, "macro/template call missing arguments");
                ok = 0;
                break;
            }
            if (macro_args_add(out, param_name->ident, value) != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        if (pos_index < pos_len) {
            macro_add_diag(ctx, call, "macro/template call has too many arguments");
            ok = 0;
        }
        for (size_t j = 0; j < named_len; j++) {
            if (named_used && !named_used[j]) {
                macro_add_diag(ctx, call, "macro/template call has unknown named argument");
                ok = 0;
                break;
            }
        }
    }
    free(pos_args);
    free(named_names);
    free(named_vals);
    free(named_used);
    return ok ? 0 : -1;
}

static ChengNode *macro_clone_tree(const ChengNode *node) {
    if (!node) {
        return NULL;
    }
    ChengNode *out = cheng_node_new(node->kind, node->line, node->col);
    if (!out) {
        return NULL;
    }
    out->type_id = node->type_id;
    if (node->ident) {
        cheng_node_set_ident(out, node->ident);
    }
    if (node->str_val) {
        cheng_node_set_str(out, node->str_val);
    }
    out->int_val = node->int_val;
    out->float_val = node->float_val;
    out->call_style = node->call_style;
    for (size_t i = 0; i < node->len; i++) {
        ChengNode *child = macro_clone_tree(node->kids[i]);
        if (child) {
            cheng_node_add(out, child);
        }
    }
    return out;
}

static ChengNode *macro_clone_subst(const ChengNode *node, const MacroArgList *args) {
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_IDENT && node->ident) {
        const ChengNode *mapped = macro_args_find(args, node->ident);
        if (mapped) {
            return macro_clone_tree(mapped);
        }
    }
    ChengNode *out = cheng_node_new(node->kind, node->line, node->col);
    if (!out) {
        return NULL;
    }
    out->type_id = node->type_id;
    if (node->ident) {
        cheng_node_set_ident(out, node->ident);
    }
    if (node->str_val) {
        cheng_node_set_str(out, node->str_val);
    }
    out->int_val = node->int_val;
    out->float_val = node->float_val;
    out->call_style = node->call_style;
    for (size_t i = 0; i < node->len; i++) {
        ChengNode *child = macro_clone_subst(node->kids[i], args);
        if (child) {
            cheng_node_add(out, child);
        }
    }
    return out;
}

static ChengNode *macro_expand_expr(MacroCtx *ctx, ChengNode *expr);
static int macro_expand_list(MacroCtx *ctx, ChengNode *list);

static ChengNode *macro_body_as_expr(MacroCtx *ctx, ChengNode *body, const ChengNode *call) {
    if (!body || body->kind != NK_STMT_LIST || body->len == 0) {
        macro_add_diag(ctx, call, "macro/template body cannot be used as expression");
        cheng_node_free(body);
        return NULL;
    }
    if (body->len != 1) {
        macro_add_diag(ctx, call, "macro/template body has multiple statements");
        cheng_node_free(body);
        return NULL;
    }
    ChengNode *stmt = body->kids[0];
    if (!stmt) {
        macro_add_diag(ctx, call, "macro/template body is empty");
        cheng_node_free(body);
        return NULL;
    }
    if (stmt->kind == NK_RETURN) {
        ChengNode *ret_expr = stmt->len > 0 ? stmt->kids[0] : NULL;
        if (!ret_expr || ret_expr->kind == NK_EMPTY) {
            macro_add_diag(ctx, call, "macro/template return lacks expression");
            cheng_node_free(body);
            return NULL;
        }
        stmt->kids[0] = NULL;
        cheng_node_free(stmt);
        body->kids[0] = NULL;
        body->len = 0;
        cheng_node_free(body);
        return ret_expr;
    }
    body->kids[0] = NULL;
    body->len = 0;
    cheng_node_free(body);
    return stmt;
}

static ChengNode *macro_expand_call_expr(MacroCtx *ctx,
                                         const MacroDecl *decl,
                                         ChengNode *call) {
    if (!ctx || !decl || !call) {
        return call;
    }
    if (ctx->hard_rt) {
        macro_add_diag(ctx, call, "hard realtime asm backend does not allow macro declaration");
        return call;
    }
    const char *name = decl->name;
    if (name && macro_stack_has(&ctx->stack, name)) {
        macro_add_diag(ctx, call, "macro/template expansion recursion detected");
        return call;
    }
    MacroArgList args;
    macro_args_init(&args);
    if (macro_collect_args(ctx, decl->decl, call, &args) != 0) {
        macro_args_free(&args);
        return call;
    }
    if (name) {
        if (macro_stack_push(&ctx->stack, name) != 0) {
            macro_args_free(&args);
            return call;
        }
    }
    ChengNode *body = macro_clone_subst(macro_decl_body(decl->decl), &args);
    macro_args_free(&args);
    if (name) {
        macro_stack_pop(&ctx->stack);
    }
    if (!body) {
        return call;
    }
    (void)macro_expand_list(ctx, body);
    ChengNode *expr = macro_body_as_expr(ctx, body, call);
    if (!expr) {
        return call;
    }
    cheng_node_free(call);
    return expr;
}

static ChengNode *macro_expand_call_stmt(MacroCtx *ctx,
                                         const MacroDecl *decl,
                                         ChengNode *call) {
    if (!ctx || !decl || !call) {
        return call;
    }
    if (ctx->hard_rt) {
        macro_add_diag(ctx, call, "hard realtime asm backend does not allow macro declaration");
        return call;
    }
    const char *name = decl->name;
    if (name && macro_stack_has(&ctx->stack, name)) {
        macro_add_diag(ctx, call, "macro/template expansion recursion detected");
        return call;
    }
    MacroArgList args;
    macro_args_init(&args);
    if (macro_collect_args(ctx, decl->decl, call, &args) != 0) {
        macro_args_free(&args);
        return call;
    }
    if (name) {
        if (macro_stack_push(&ctx->stack, name) != 0) {
            macro_args_free(&args);
            return call;
        }
    }
    ChengNode *body = macro_clone_subst(macro_decl_body(decl->decl), &args);
    macro_args_free(&args);
    if (name) {
        macro_stack_pop(&ctx->stack);
    }
    if (!body) {
        return call;
    }
    if (body->kind != NK_STMT_LIST) {
        ChengNode *list = cheng_node_new(NK_STMT_LIST, body->line, body->col);
        if (list) {
            cheng_node_add(list, body);
            body = list;
        }
    }
    (void)macro_expand_list(ctx, body);
    cheng_node_free(call);
    return body;
}

static ChengNode *macro_expand_expr(MacroCtx *ctx, ChengNode *expr) {
    if (!ctx || !expr) {
        return expr;
    }
    if (expr->kind == NK_STMT_LIST) {
        (void)macro_expand_list(ctx, expr);
        return expr;
    }
    if (expr->kind == NK_CALL) {
        const char *name = macro_call_name(expr);
        const MacroDecl *decl = name ? macro_list_find(ctx->decls, name) : NULL;
        if (decl) {
            return macro_expand_call_expr(ctx, decl, expr);
        }
    }
    for (size_t i = 0; i < expr->len; i++) {
        ChengNode *child = expr->kids[i];
        if (!child) {
            continue;
        }
        ChengNode *next = macro_expand_expr(ctx, child);
        if (next != child) {
            expr->kids[i] = next;
        }
    }
    return expr;
}

static ChengNode *macro_expand_stmt(MacroCtx *ctx, ChengNode *stmt) {
    if (!ctx || !stmt) {
        return stmt;
    }
    if (stmt->kind == NK_CALL) {
        const char *name = macro_call_name(stmt);
        const MacroDecl *decl = name ? macro_list_find(ctx->decls, name) : NULL;
        if (decl) {
            return macro_expand_call_stmt(ctx, decl, stmt);
        }
    }
    switch (stmt->kind) {
        case NK_STMT_LIST:
            (void)macro_expand_list(ctx, stmt);
            return stmt;
        case NK_RETURN:
            if (stmt->len > 0) {
                stmt->kids[0] = macro_expand_expr(ctx, stmt->kids[0]);
            }
            return stmt;
        case NK_LET:
        case NK_VAR:
        case NK_CONST:
            if (stmt->len > 2) {
                stmt->kids[2] = macro_expand_expr(ctx, stmt->kids[2]);
            }
            return stmt;
        case NK_ASGN:
            if (stmt->len > 0) {
                stmt->kids[0] = macro_expand_expr(ctx, stmt->kids[0]);
            }
            if (stmt->len > 1) {
                stmt->kids[1] = macro_expand_expr(ctx, stmt->kids[1]);
            }
            return stmt;
        case NK_IF:
            if (stmt->len > 0) {
                stmt->kids[0] = macro_expand_expr(ctx, stmt->kids[0]);
            }
            if (stmt->len > 1) {
                (void)macro_expand_list(ctx, stmt->kids[1]);
            }
            if (stmt->len > 2 && stmt->kids[2] && stmt->kids[2]->kind == NK_ELSE) {
                if (stmt->kids[2]->len > 0) {
                    (void)macro_expand_list(ctx, stmt->kids[2]->kids[0]);
                }
            }
            return stmt;
        case NK_WHILE:
            if (stmt->len > 0) {
                stmt->kids[0] = macro_expand_expr(ctx, stmt->kids[0]);
            }
            if (stmt->len > 1) {
                (void)macro_expand_list(ctx, stmt->kids[1]);
            }
            return stmt;
        case NK_FOR:
            if (stmt->len > 1) {
                stmt->kids[1] = macro_expand_expr(ctx, stmt->kids[1]);
            }
            if (stmt->len > 2) {
                (void)macro_expand_list(ctx, stmt->kids[2]);
            }
            return stmt;
        case NK_CASE:
            if (stmt->len > 0) {
                stmt->kids[0] = macro_expand_expr(ctx, stmt->kids[0]);
            }
            for (size_t i = 1; i < stmt->len; i++) {
                ChengNode *branch = stmt->kids[i];
                if (!branch) {
                    continue;
                }
                if (branch->kind == NK_OF_BRANCH && branch->len > 1) {
                    (void)macro_expand_list(ctx, branch->kids[1]);
                } else if (branch->kind == NK_ELSE && branch->len > 0) {
                    (void)macro_expand_list(ctx, branch->kids[0]);
                }
            }
            return stmt;
        default:
            break;
    }
    for (size_t i = 0; i < stmt->len; i++) {
        ChengNode *child = stmt->kids[i];
        if (!child) {
            continue;
        }
        stmt->kids[i] = macro_expand_expr(ctx, child);
    }
    return stmt;
}

static int macro_expand_list(MacroCtx *ctx, ChengNode *list) {
    if (!ctx || !list) {
        return 0;
    }
    if (list->kind != NK_STMT_LIST) {
        ChengNode *next = macro_expand_stmt(ctx, list);
        if (next != list) {
            list = next;
        }
        return 0;
    }
    for (size_t i = 0; i < list->len; i++) {
        ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        ChengNode *next = macro_expand_stmt(ctx, stmt);
        if (next != stmt) {
            list->kids[i] = next;
        }
    }
    return 0;
}

static void macro_collect_decls(const ChengNode *list, MacroDeclList *out) {
    if (!list || list->kind != NK_STMT_LIST) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_TEMPLATE_DECL || stmt->kind == NK_MACRO_DECL) {
            const char *name = macro_decl_name(stmt);
            if (name) {
                macro_list_add(out, name, stmt);
            }
        }
    }
}

int cheng_expand_macros(ChengNode *root, const char *filename, ChengDiagList *diags) {
    if (!root) {
        return 0;
    }
    ChengNode *list = root;
    if (root->kind == NK_MODULE && root->len > 0) {
        list = root->kids[0];
    }
    if (!list || list->kind != NK_STMT_LIST) {
        return 0;
    }
    MacroDeclList decls;
    macro_list_init(&decls);
    macro_collect_decls(list, &decls);

    MacroCtx ctx = {0};
    ctx.filename = filename ? filename : "";
    ctx.diags = diags;
    ctx.decls = &decls;
    ctx.hard_rt = macro_env_is_hard_rt();
    macro_stack_init(&ctx.stack);

    if (ctx.hard_rt) {
        for (size_t i = 0; i < decls.len; i++) {
            const ChengNode *decl = decls.items[i].decl;
            if (!decl) {
                continue;
            }
            if (decl->kind == NK_MACRO_DECL) {
                macro_add_diag(&ctx, decl, "hard realtime asm backend does not allow macro declaration");
            } else if (decl->kind == NK_TEMPLATE_DECL) {
                macro_add_diag(&ctx, decl, "hard realtime asm backend does not allow template declaration");
            }
        }
    }

    (void)macro_expand_list(&ctx, list);

    macro_stack_free(&ctx.stack);
    macro_list_free(&decls);
    return 0;
}
