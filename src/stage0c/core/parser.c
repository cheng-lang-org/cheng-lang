#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

static ChengNode *new_node_from_token_ident(ChengNodeKind kind, const ChengToken *tok, const char *text) {
    int line = tok ? tok->line : 0;
    int col = tok ? tok->col : 0;
    ChengNode *node = cheng_node_new(kind, line, col);
    if (node && text) {
        cheng_node_set_ident(node, text);
    }
    return node;
}

static ChengNode *new_node_from_token_str(ChengNodeKind kind, const ChengToken *tok, const char *text) {
    int line = tok ? tok->line : 0;
    int col = tok ? tok->col : 0;
    ChengNode *node = cheng_node_new(kind, line, col);
    if (node && text) {
        cheng_node_set_str(node, text);
    }
    return node;
}

static ChengNode *parse_primary(ChengParser *p);
static ChengNode *parse_object_type(ChengParser *p, const ChengToken *start);
static ChengNode *parse_type_expr(ChengParser *p);
static ChengNode *parse_formal_params(ChengParser *p);
static ChengNode *parse_type_params(ChengParser *p);
static int is_type_keyword_token(const ChengToken *tok);

static ChengToken *current(ChengParser *p) {
    if (p->pos < p->tokens->len) {
        return &p->tokens->data[p->pos];
    }
    return NULL;
}

static ChengToken *peek(ChengParser *p, size_t offset) {
    size_t idx = p->pos + offset;
    if (idx < p->tokens->len) {
        return &p->tokens->data[idx];
    }
    return NULL;
}

static void advance(ChengParser *p) {
    if (p->pos < p->tokens->len) {
        p->pos += 1;
    }
}

static int token_is_keyword(const ChengToken *tok, const char *kw) {
    size_t n = strlen(kw);
    return tok && tok->kind == TK_KEYWORD && tok->len == n && memcmp(tok->start, kw, n) == 0;
}

static int token_is_symbol(const ChengToken *tok, const char *sym) {
    size_t n = strlen(sym);
    return tok && tok->kind == TK_SYMBOL && tok->len == n && memcmp(tok->start, sym, n) == 0;
}

static int token_is_ident(const ChengToken *tok) {
    return tok && tok->kind == TK_IDENT;
}

static int is_line_end(const ChengToken *tok) {
    if (!tok) {
        return 1;
    }
    return tok->kind == TK_NEWLINE || tok->kind == TK_DEDENT || tok->kind == TK_EOF;
}

static ChengToken *prev_token(ChengParser *p) {
    if (!p || p->pos == 0) {
        return NULL;
    }
    return &p->tokens->data[p->pos - 1];
}

static char *copy_token(const ChengToken *tok);
static char *decode_string_lit(ChengParser *p, const ChengToken *tok);

static int token_span(const ChengToken *tok) {
    if (!tok) {
        return 0;
    }
    if (tok->kind == TK_STRING) {
        size_t span = tok->len + 2;
        if (tok->str_prefix) {
            span += 1;
        }
        return (int)span;
    }
    if (tok->kind == TK_CHAR) {
        return (int)(tok->len + 2);
    }
    return (int)tok->len;
}

static int token_attached(ChengParser *p) {
    ChengToken *prev = prev_token(p);
    ChengToken *cur = current(p);
    if (!prev || !cur) {
        return 0;
    }
    if (prev->line == 0 || cur->line == 0) {
        return 0;
    }
    if (prev->kind == TK_NEWLINE || prev->kind == TK_INDENT || prev->kind == TK_DEDENT) {
        return 0;
    }
    if (prev->line != cur->line) {
        return 0;
    }
    return prev->col + token_span(prev) == cur->col;
}

static int space_call_same_line(ChengParser *p) {
    ChengToken *prev = prev_token(p);
    ChengToken *cur = current(p);
    if (!prev || !cur) {
        return 0;
    }
    if (prev->line == 0 || cur->line == 0) {
        return 0;
    }
    if (prev->kind == TK_NEWLINE || prev->kind == TK_INDENT || prev->kind == TK_DEDENT) {
        return 0;
    }
    return prev->line == cur->line;
}

static int is_c_style_cast_follow_token(const ChengToken *tok) {
    if (!tok) {
        return 0;
    }
    if (tok->kind == TK_NEWLINE || tok->kind == TK_DEDENT || tok->kind == TK_EOF) {
        return 0;
    }
    if (tok->kind == TK_NUMBER || tok->kind == TK_STRING || tok->kind == TK_CHAR) {
        return 1;
    }
    if (tok->kind == TK_IDENT) {
        return 1;
    }
    if (tok->kind == TK_KEYWORD) {
        if (token_is_keyword(tok, "true") || token_is_keyword(tok, "false") ||
            token_is_keyword(tok, "nil")) {
            return 1;
        }
        if (token_is_keyword(tok, "fn") || token_is_keyword(tok, "iterator")) {
            return 1;
        }
        if (token_is_keyword(tok, "await")) {
            return 1;
        }
        if (token_is_keyword(tok, "ref") || token_is_keyword(tok, "var")) {
            return 1;
        }
        return 0;
    }
    if (tok->kind == TK_SYMBOL && tok->len == 1) {
        char c = tok->start[0];
        if (c == '(' || c == '[' || c == '{' || c == '@') {
            return 1;
        }
        if (c == '+' || c == '-' || c == '!' || c == '~' || c == '*' || c == '&') {
            return 1;
        }
    }
    return 0;
}

static int is_builtin_type_name(const char *name) {
    if (!name || !name[0]) {
        return 0;
    }
    return strcmp(name, "int") == 0 ||
           strcmp(name, "int8") == 0 ||
           strcmp(name, "int16") == 0 ||
           strcmp(name, "int32") == 0 ||
           strcmp(name, "int64") == 0 ||
           strcmp(name, "uint") == 0 ||
           strcmp(name, "uint8") == 0 ||
           strcmp(name, "uint16") == 0 ||
           strcmp(name, "uint32") == 0 ||
           strcmp(name, "uint64") == 0 ||
           strcmp(name, "i8") == 0 ||
           strcmp(name, "u8") == 0 ||
           strcmp(name, "float") == 0 ||
           strcmp(name, "float32") == 0 ||
           strcmp(name, "float64") == 0 ||
           strcmp(name, "bool") == 0 ||
           strcmp(name, "char") == 0 ||
           strcmp(name, "str") == 0 ||
           strcmp(name, "cstring") == 0 ||
           strcmp(name, "void") == 0;
}

static int is_likely_type_ident(const char *name) {
    if (!name || !name[0]) {
        return 0;
    }
    if (is_builtin_type_name(name)) {
        return 1;
    }
    if (name[0] >= 'A' && name[0] <= 'Z') {
        return 1;
    }
    for (const char *p = name + 1; *p; p++) {
        if (p[-1] == '_' && *p >= 'A' && *p <= 'Z') {
            return 1;
        }
    }
    return 0;
}

static int is_likely_type_expr(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_REF_TY:
        case NK_PTR_TY:
        case NK_VAR_TY:
        case NK_TUPLE_TY:
        case NK_SET_TY:
        case NK_FN_TY:
        case NK_DOT_EXPR: {
            if (node->len > 1) {
                ChengNode *member = node->kids[1];
                if (member && (member->kind == NK_IDENT || member->kind == NK_SYMBOL)) {
                    return is_likely_type_ident(member->ident);
                }
            }
            return 0;
        }
        case NK_BRACKET_EXPR: {
            if (node->len < 1) {
                return 0;
            }
            if (!is_likely_type_expr(node->kids[0])) {
                return 0;
            }
            for (size_t i = 1; i < node->len; i++) {
                ChengNode *arg = node->kids[i];
                if (!arg) {
                    return 0;
                }
                if (arg->kind == NK_INT_LIT) {
                    continue;
                }
                if (!is_likely_type_expr(arg)) {
                    return 0;
                }
            }
            return 1;
        }
        case NK_IDENT:
        case NK_SYMBOL:
            return is_likely_type_ident(node->ident);
        default:
            return 0;
    }
}

static int is_definite_type_expr(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_REF_TY:
        case NK_PTR_TY:
        case NK_VAR_TY:
        case NK_TUPLE_TY:
        case NK_SET_TY:
        case NK_FN_TY:
            return 1;
        case NK_BRACKET_EXPR: {
            if (node->len < 1) {
                return 0;
            }
            ChengNode *base = node->kids[0];
            if (!base || (base->kind != NK_IDENT && base->kind != NK_SYMBOL) || !base->ident) {
                return 0;
            }
            if (!is_builtin_type_name(base->ident)) {
                return 0;
            }
            return 1;
        }
        case NK_IDENT:
        case NK_SYMBOL:
            return node->ident && is_builtin_type_name(node->ident);
        case NK_PAR:
            return node->len == 1 && is_definite_type_expr(node->kids[0]);
        default:
            return 0;
    }
}

static int is_likely_type_token_start(const ChengToken *tok) {
    if (!tok) {
        return 0;
    }
    if (tok->kind == TK_KEYWORD) {
        return is_type_keyword_token(tok);
    }
    if (tok->kind == TK_IDENT) {
        char *name = copy_token(tok);
        int ok = is_likely_type_ident(name);
        free(name);
        return ok;
    }
    return 0;
}

static int is_type_call_start(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!tok) {
        return 0;
    }
    if (is_likely_type_token_start(tok)) {
        return 1;
    }
    if (tok->kind == TK_IDENT || tok->kind == TK_KEYWORD) {
        ChengToken *next = peek(p, 1);
        if (next && token_is_symbol(next, "[")) {
            return 1;
        }
    }
    return 0;
}

static int is_space_call_arg_start(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!tok) {
        return 0;
    }
    if (tok->kind == TK_NUMBER || tok->kind == TK_STRING || tok->kind == TK_CHAR) {
        return 1;
    }
    if (tok->kind == TK_IDENT) {
        return 1;
    }
    if (tok->kind == TK_KEYWORD) {
        if (token_is_keyword(tok, "true") || token_is_keyword(tok, "false") ||
            token_is_keyword(tok, "nil")) {
            return 1;
        }
        if (token_is_keyword(tok, "fn") || token_is_keyword(tok, "iterator")) {
            return 1;
        }
        return 0;
    }
    if (tok->kind == TK_SYMBOL && tok->len == 1) {
        char c = tok->start[0];
        if (c == '(' || c == '[' || c == '{' || c == '@') {
            return 1;
        }
    }
    return 0;
}

static int is_type_keyword_token(const ChengToken *tok) {
    if (!tok || tok->kind != TK_KEYWORD) {
        return 0;
    }
    char *name = copy_token(tok);
    if (!name) {
        return 0;
    }
    int ok = is_builtin_type_name(name) ||
        strcmp(name, "ref") == 0 || strcmp(name, "var") == 0 ||
        strcmp(name, "enum") == 0 || strcmp(name, "tuple") == 0 ||
        strcmp(name, "set") == 0 || strcmp(name, "fn") == 0 ||
        strcmp(name, "ptr") == 0;
    free(name);
    return ok;
}

static int is_intrinsic_type_callee(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind != NK_IDENT && node->kind != NK_SYMBOL) {
        return 0;
    }
    if (!node->ident) {
        return 0;
    }
    return strcmp(node->ident, "sizeof") == 0 ||
        strcmp(node->ident, "default") == 0 ||
        strcmp(node->ident, "alignof") == 0;
}

static int is_space_call_arg_start_for(ChengParser *p, const ChengNode *lhs) {
    if (is_space_call_arg_start(p)) {
        return 1;
    }
    if (is_intrinsic_type_callee(lhs)) {
        ChengToken *tok = current(p);
        if (is_type_keyword_token(tok) || is_likely_type_token_start(tok)) {
            return 1;
        }
    }
    return 0;
}

static void skip_space_call_extras(ChengParser *p) {
    for (;;) {
        ChengToken *tok = current(p);
        if (is_line_end(tok)) {
            break;
        }
        if (tok->kind == TK_SYMBOL && tok->len == 1) {
            char c = tok->start[0];
            if (c == ';' || c == '}' || c == ')' || c == ']' ||
                c == ',' || c == ':' || c == '=') {
                break;
            }
        }
        advance(p);
    }
}

static int match_symbol(ChengParser *p, const char *sym) {
    ChengToken *tok = current(p);
    if (token_is_symbol(tok, sym)) {
        advance(p);
        return 1;
    }
    return 0;
}

static int match_keyword(ChengParser *p, const char *kw) {
    ChengToken *tok = current(p);
    if (token_is_keyword(tok, kw)) {
        advance(p);
        return 1;
    }
    return 0;
}

static void expect_symbol(ChengParser *p, const char *sym, const char *msg) {
    if (match_symbol(p, sym)) {
        return;
    }
    ChengToken *tok = current(p);
    if (p->diags && tok) {
        cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col, msg);
    }
}

static void skip_newlines(ChengParser *p) {
    ChengToken *tok = current(p);
    while (tok && tok->kind == TK_NEWLINE) {
        advance(p);
        tok = current(p);
    }
}

static void skip_stmt_seps(ChengParser *p) {
    for (;;) {
        skip_newlines(p);
        ChengToken *tok = current(p);
        if (tok && tok->kind == TK_SYMBOL && tok->len == 1 && tok->start[0] == ';') {
            advance(p);
            continue;
        }
        break;
    }
}

static char *copy_token(const ChengToken *tok) {
    if (!tok) {
        return NULL;
    }
    char *out = (char *)malloc(tok->len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, tok->start, tok->len);
    out[tok->len] = '\0';
    return out;
}

static int hex_digit_val(char c) {
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

static char *decode_string_lit(ChengParser *p, const ChengToken *tok) {
    if (!tok) {
        return NULL;
    }
    char *raw = copy_token(tok);
    if (!raw) {
        return NULL;
    }
    if (tok->str_prefix == 'r') {
        return raw;
    }
    size_t len = strlen(raw);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        free(raw);
        return NULL;
    }
    size_t out_len = 0;
    size_t i = 0;
    while (i < len) {
        char c = raw[i];
        if (c != '\\') {
            out[out_len++] = c;
            i++;
            continue;
        }
        i++;
        if (i >= len) {
            if (p && p->diags) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename,
                               tok->line, tok->col, "Unterminated string escape sequence");
            }
            break;
        }
        char esc = raw[i++];
        switch (esc) {
            case 'n': out[out_len++] = '\n'; break;
            case 'r': out[out_len++] = '\r'; break;
            case 't': out[out_len++] = '\t'; break;
            case '0': out[out_len++] = '\0'; break;
            case '\\': out[out_len++] = '\\'; break;
            case '"': out[out_len++] = '"'; break;
            case '\'': out[out_len++] = '\''; break;
            case 'x': {
                if (i + 1 >= len) {
                    if (p && p->diags) {
                        cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename,
                                       tok->line, tok->col, "String escape \\x requires two hex digits");
                    }
                    break;
                }
                int a = hex_digit_val(raw[i]);
                int b = hex_digit_val(raw[i + 1]);
                if (a < 0 || b < 0) {
                    if (p && p->diags) {
                        cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename,
                                       tok->line, tok->col, "String escape \\x requires two hex digits");
                    }
                    break;
                }
                out[out_len++] = (char)((a << 4) | b);
                i += 2;
                break;
            }
            default: {
                if (p && p->diags) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Invalid string escape: \\%c", esc);
                    cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename,
                                   tok->line, tok->col, msg);
                }
                out[out_len++] = esc;
                break;
            }
        }
    }
    out[out_len] = '\0';
    free(raw);
    return out;
}

static ChengNode *empty_node(const ChengToken *tok) {
    int line = tok ? tok->line : 0;
    int col = tok ? tok->col : 0;
    return cheng_node_new(NK_EMPTY, line, col);
}

static ChengNode *clone_tree(const ChengNode *node) {
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
        ChengNode *child = clone_tree(node->kids[i]);
        if (child) {
            cheng_node_add(out, child);
        }
    }
    return out;
}

static ChengNode *parse_ident(ChengParser *p, int allow_keyword) {
    ChengToken *tok = current(p);
    if (!tok) {
        return NULL;
    }
    if (tok->kind != TK_IDENT && !(allow_keyword && tok->kind == TK_KEYWORD)) {
        return NULL;
    }
    char *name = copy_token(tok);
    ChengNode *node = new_node_from_token_ident(NK_IDENT, tok, name);
    free(name);
    advance(p);
    return node;
}

static ChengNode *parse_type_arg(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!tok) {
        return NULL;
    }
    if (tok->kind == TK_NUMBER) {
        return parse_primary(p);
    }
    return parse_type_expr(p);
}

static ChengNode *parse_type_params(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!token_is_symbol(tok, "[")) {
        return NULL;
    }
    advance(p);
    ChengNode *params = cheng_node_new(NK_GENERIC_PARAMS, tok ? tok->line : 0, tok ? tok->col : 0);
    if (!params) {
        return NULL;
    }
    while (current(p)) {
        if (token_is_symbol(current(p), "]")) {
            advance(p);
            break;
        }
        ChengNode *name = parse_ident(p, 1);
        if (!name) {
            if (current(p) && !token_is_symbol(current(p), "]")) {
                advance(p);
                continue;
            }
            break;
        }
        ChengNode *constraint = empty_node(tok);
        ChengNode *default_val = empty_node(tok);
        if (match_symbol(p, ":")) {
            constraint = parse_type_expr(p);
            if (!constraint) {
                constraint = empty_node(tok);
            }
        }
        if (match_symbol(p, "=")) {
            default_val = parse_type_expr(p);
            if (!default_val) {
                default_val = empty_node(tok);
            }
        }
        ChengNode *defn = cheng_node_new(NK_IDENT_DEFS, name->line, name->col);
        if (defn) {
            cheng_node_add(defn, name);
            cheng_node_add(defn, constraint);
            cheng_node_add(defn, default_val);
            cheng_node_add(params, defn);
        } else {
            cheng_node_free(name);
            cheng_node_free(constraint);
            cheng_node_free(default_val);
        }
        if (match_symbol(p, ",") || match_symbol(p, ";")) {
            continue;
        }
        if (current(p) && token_is_symbol(current(p), "]")) {
            continue;
        }
        break;
    }
    if (current(p) && token_is_symbol(current(p), "]")) {
        advance(p);
    }
    return params;
}

static ChengNode *parse_type_expr(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!tok) {
        return NULL;
    }
    ChengNode *base = NULL;
    if (tok->kind == TK_KEYWORD && token_is_keyword(tok, "ref")) {
        advance(p);
        ChengNode *inner = NULL;
        if (current(p) && (current(p)->kind == TK_NEWLINE ||
                           (current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "of")) ||
                           token_is_symbol(current(p), ":"))) {
            inner = parse_object_type(p, tok);
        } else {
            inner = parse_type_expr(p);
        }
        ChengNode *ty = cheng_node_new(NK_REF_TY, tok->line, tok->col);
        if (inner) {
            cheng_node_add(ty, inner);
        }
        base = ty;
    }
    if (!base && tok->kind == TK_KEYWORD && token_is_keyword(tok, "var")) {
        advance(p);
        ChengNode *inner = parse_type_expr(p);
        ChengNode *ty = cheng_node_new(NK_VAR_TY, tok->line, tok->col);
        if (inner) {
            cheng_node_add(ty, inner);
        }
        base = ty;
    }
    if (!base && tok->kind == TK_KEYWORD && token_is_keyword(tok, "ptr")) {
        advance(p);
        ChengNode *inner = parse_type_expr(p);
        ChengNode *ty = cheng_node_new(NK_PTR_TY, tok->line, tok->col);
        if (inner) {
            cheng_node_add(ty, inner);
        }
        base = ty;
    }
    if (!base && tok->kind == TK_KEYWORD && token_is_keyword(tok, "fn")) {
        advance(p);
        ChengNode *params = parse_formal_params(p);
        ChengNode *ret_type = empty_node(tok);
        if (match_symbol(p, ":")) {
            ret_type = parse_type_expr(p);
        }
        ChengNode *ty = cheng_node_new(NK_FN_TY, tok->line, tok->col);
        if (params) {
            cheng_node_add(ty, params);
        } else {
            cheng_node_add(ty, empty_node(tok));
        }
        if (ret_type) {
            cheng_node_add(ty, ret_type);
        }
        base = ty;
    }
    if (!base) {
        base = parse_ident(p, 1);
        if (!base) {
            return NULL;
        }
        for (;;) {
            if (token_is_symbol(current(p), ".")) {
                ChengToken *dot = current(p);
                advance(p);
                ChengNode *rhs = parse_ident(p, 1);
                if (!rhs) {
                    if (p->diags && dot) {
                        cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, dot->line, dot->col,
                                       "Expected identifier after '.'");
                    }
                    break;
                }
                ChengNode *node = cheng_node_new(NK_DOT_EXPR, dot ? dot->line : 0, dot ? dot->col : 0);
                cheng_node_add(node, base);
                cheng_node_add(node, rhs);
                base = node;
                continue;
            }
            if (token_is_symbol(current(p), "[")) {
                ChengToken *br = current(p);
                advance(p);
                ChengNode *node = cheng_node_new(NK_BRACKET_EXPR, br ? br->line : 0, br ? br->col : 0);
                cheng_node_add(node, base);
                if (!token_is_symbol(current(p), "]")) {
                    while (1) {
                        ChengNode *arg = parse_type_arg(p);
                        if (arg) {
                            cheng_node_add(node, arg);
                        } else if (current(p) && !token_is_symbol(current(p), "]")) {
                            advance(p);
                            break;
                        }
                        if (!match_symbol(p, ",") && !match_symbol(p, ";")) {
                            break;
                        }
                    }
                }
                expect_symbol(p, "]", "Expected ']'");
                base = node;
                continue;
            }
            break;
        }
    }
    while (token_is_symbol(current(p), "*")) {
        ChengToken *star = current(p);
        advance(p);
        ChengNode *node = cheng_node_new(NK_PTR_TY, star ? star->line : 0, star ? star->col : 0);
        if (base) {
            cheng_node_add(node, base);
        }
        base = node;
    }
    return base;
}

static ChengNode *parse_primary(ChengParser *p);
static ChengNode *parse_expression(ChengParser *p, int min_prec);

static void skip_bracketed(ChengParser *p) {
    if (!token_is_symbol(current(p), "[")) {
        return;
    }
    int depth = 0;
    while (current(p)) {
        ChengToken *tok = current(p);
        if (token_is_symbol(tok, "[")) {
            depth += 1;
        } else if (token_is_symbol(tok, "]")) {
            depth -= 1;
            if (depth == 0) {
                advance(p);
                break;
            }
        }
        advance(p);
    }
}

static void skip_indent_block(ChengParser *p) {
    if (!current(p) || current(p)->kind != TK_INDENT) {
        return;
    }
    int depth = 0;
    while (current(p)) {
        ChengToken *tok = current(p);
        if (tok->kind == TK_INDENT) {
            depth += 1;
        } else if (tok->kind == TK_DEDENT) {
            depth -= 1;
            if (depth == 0) {
                advance(p);
                break;
            }
        }
        advance(p);
    }
}

enum {
    PREC_TERNARY = 0,
    PREC_OR = 1,
    PREC_AND = 2,
    PREC_BITOR = 3,
    PREC_BITXOR = 4,
    PREC_BITAND = 5,
    PREC_EQUALITY = 6,
    PREC_COMPARISON = 7,
    PREC_MEMBERSHIP = 8,
    PREC_RANGE = 9,
    PREC_SHIFT = 10,
    PREC_ADDITIVE = 11,
    PREC_MULTIPLICATIVE = 12,
};

static int is_ternary_question(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!token_is_symbol(tok, "?")) {
        return 0;
    }
    int depth = 0;
    for (int i = 1;; i++) {
        ChengToken *next = peek(p, i);
        if (!next || next->kind == TK_EOF || next->kind == TK_DEDENT || next->kind == TK_NEWLINE) {
            return 0;
        }
        if (next->kind == TK_SYMBOL) {
            if (token_is_symbol(next, "(") || token_is_symbol(next, "[") || token_is_symbol(next, "{")) {
                depth += 1;
                continue;
            }
            if (token_is_symbol(next, ")") || token_is_symbol(next, "]") || token_is_symbol(next, "}")) {
                if (depth == 0) {
                    return 0;
                }
                depth -= 1;
                continue;
            }
            if (token_is_symbol(next, ":")) {
                if (depth == 0) {
                    return 1;
                }
            }
            if ((token_is_symbol(next, ",") || token_is_symbol(next, ";") || token_is_symbol(next, "=")) &&
                depth == 0) {
                return 0;
            }
        }
    }
    return 0;
}

static int op_precedence(const ChengToken *tok, char *out_op, size_t cap) {
    if (!tok) {
        return -1;
    }
    if (tok->kind == TK_SYMBOL) {
        const char *sym = tok->start;
        size_t len = tok->len;
        if (len + 1 > cap) {
            return -1;
        }
        memcpy(out_op, sym, len);
        out_op[len] = '\0';
        if (len == 3 && memcmp(sym, "..<", 3) == 0) return PREC_RANGE;
        if (len == 2 && memcmp(sym, "..", 2) == 0) return PREC_RANGE;
        if (len == 2 && memcmp(sym, "==", 2) == 0) return PREC_EQUALITY;
        if (len == 2 && memcmp(sym, "!=", 2) == 0) return PREC_EQUALITY;
        if (len == 1 && (*sym == '<' || *sym == '>')) return PREC_COMPARISON;
        if (len == 2 && (memcmp(sym, "<=", 2) == 0 || memcmp(sym, ">=", 2) == 0)) return PREC_COMPARISON;
        if (len == 2 && (memcmp(sym, "<<", 2) == 0 || memcmp(sym, ">>", 2) == 0)) return PREC_SHIFT;
        if (len == 2 && memcmp(sym, "&&", 2) == 0) return PREC_AND;
        if (len == 2 && memcmp(sym, "||", 2) == 0) return PREC_OR;
        if (len == 1 && (*sym == '+' || *sym == '-')) return PREC_ADDITIVE;
        if (len == 1 && (*sym == '*' || *sym == '/' || *sym == '%')) return PREC_MULTIPLICATIVE;
        if (len == 1 && *sym == '&') return PREC_BITAND;
        if (len == 1 && *sym == '^') return PREC_BITXOR;
        if (len == 1 && *sym == '|') return PREC_BITOR;
    }
    if (tok->kind == TK_KEYWORD) {
        if (tok->len == 2 && memcmp(tok->start, "in", 2) == 0) {
            memcpy(out_op, "in", 3);
            return PREC_MEMBERSHIP;
        }
        if (tok->len == 5 && memcmp(tok->start, "notin", 5) == 0) {
            memcpy(out_op, "notin", 6);
            return PREC_MEMBERSHIP;
        }
        if (tok->len == 2 && memcmp(tok->start, "is", 2) == 0) {
            memcpy(out_op, "is", 3);
            return PREC_MEMBERSHIP;
        }
        if (tok->len == 3 && memcmp(tok->start, "div", 3) == 0) {
            memcpy(out_op, "div", 4);
            return PREC_MULTIPLICATIVE;
        }
        if (tok->len == 3 && memcmp(tok->start, "mod", 3) == 0) {
            memcpy(out_op, "mod", 4);
            return PREC_MULTIPLICATIVE;
        }
    }
    return -1;
}

static ChengNode *parse_postfix(ChengParser *p, ChengNode *lhs) {
    ChengNode *node = lhs;
    for (;;) {
        ChengToken *tok = current(p);
        if (token_is_symbol(tok, ".")) {
            advance(p);
            ChengNode *member = parse_ident(p, 1);
            ChengNode *dn = cheng_node_new(NK_DOT_EXPR, tok->line, tok->col);
            if (node) {
                cheng_node_add(dn, node);
            }
            if (member) {
                cheng_node_add(dn, member);
            }
            node = dn;
            continue;
        }
        if (token_is_symbol(tok, "->")) {
            advance(p);
            ChengNode *member = parse_ident(p, 1);
            ChengNode *dr = cheng_node_new(NK_HIDDEN_DEREF, tok->line, tok->col);
            if (node) {
                cheng_node_add(dr, node);
            }
            ChengNode *dn = cheng_node_new(NK_DOT_EXPR, tok->line, tok->col);
            cheng_node_add(dn, dr);
            if (member) {
                cheng_node_add(dn, member);
            }
            node = dn;
            continue;
        }
        if (token_is_symbol(tok, "(")) {
            if (!token_attached(p)) {
                break;
            }
            int is_discard = 0;
            int arg_count = 0;
            int has_named = 0;
            if (node && node->kind == NK_IDENT && node->ident && strcmp(node->ident, "discard") == 0) {
                is_discard = 1;
            }
            advance(p);
            ChengNode *call = cheng_node_new(NK_CALL, tok->line, tok->col);
            if (node) {
                cheng_node_add(call, node);
            }
            if (call) {
                call->call_style = CHENG_CALL_STYLE_PAREN;
            }
            if (!token_is_symbol(current(p), ")")) {
                while (1) {
                    ChengNode *arg = NULL;
                    ChengToken *arg_tok = current(p);
                    if (arg_tok && (token_is_ident(arg_tok) || arg_tok->kind == TK_KEYWORD) &&
                        token_is_symbol(peek(p, 1), ":")) {
                        ChengNode *name = parse_ident(p, 1);
                        match_symbol(p, ":");
                        ChengNode *value = parse_expression(p, 0);
                        ChengNode *call_arg = cheng_node_new(NK_CALL_ARG,
                                                             arg_tok ? arg_tok->line : 0,
                                                             arg_tok ? arg_tok->col : 0);
                        if (name) {
                            cheng_node_add(call_arg, name);
                        }
                        if (value) {
                            cheng_node_add(call_arg, value);
                        }
                        arg = call_arg;
                        has_named = 1;
                    } else {
                        arg = parse_expression(p, 0);
                    }
                    if (arg) {
                        cheng_node_add(call, arg);
                        arg_count++;
                    }
                    if (!match_symbol(p, ",") && !match_symbol(p, ";")) {
                        break;
                    }
                }
            }
            expect_symbol(p, ")", "Expected ')'");
            if (is_discard && p->diags) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                               "discard syntax is removed; use a plain expression statement");
            }
            if (arg_count == 0 && node && is_definite_type_expr(node) && p->diags) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                               "TypeExpr() is removed; use implicit default initialization or an explicit value");
            }
            node = call;
            continue;
        }
        if (token_is_symbol(tok, "[")) {
            advance(p);
            int parse_type_args = 0;
            if (is_likely_type_token_start(current(p))) {
                parse_type_args = 1;
            }
            ChengNode *br = cheng_node_new(NK_BRACKET_EXPR, tok->line, tok->col);
            if (node) {
                cheng_node_add(br, node);
            }
            if (!token_is_symbol(current(p), "]")) {
                while (1) {
                    ChengNode *arg = NULL;
                    if (parse_type_args) {
                        arg = parse_type_arg(p);
                    } else {
                        arg = parse_expression(p, 0);
                    }
                    if (arg) {
                        cheng_node_add(br, arg);
                    }
                    int matched = match_symbol(p, ",");
                    if (!matched && parse_type_args) {
                        matched = match_symbol(p, ";");
                    }
                    if (!matched) {
                        break;
                    }
                }
            }
            expect_symbol(p, "]", "Expected ']'");
            node = br;
            continue;
        }
        if (token_is_symbol(tok, "?") && !is_ternary_question(p)) {
            advance(p);
            ChengNode *pn = cheng_node_new(NK_POSTFIX, tok->line, tok->col);
            ChengNode *op = new_node_from_token_ident(NK_SYMBOL, tok, "?");
            if (op) {
                cheng_node_add(pn, op);
            }
            if (node) {
                cheng_node_add(pn, node);
            }
            node = pn;
            continue;
        }
        break;
    }
    return node;
}

static ChengNode *parse_type_call_callee(ChengParser *p);

static ChengNode *parse_space_atom(ChengParser *p) {
    ChengNode *type_callee = parse_type_call_callee(p);
    if (type_callee) {
        return parse_postfix(p, type_callee);
    }
    return parse_postfix(p, parse_primary(p));
}

static ChengNode *maybe_parse_space_type_ptr_call(ChengParser *p, ChengNode *arg) {
    if (!arg || (arg->kind != NK_IDENT && arg->kind != NK_SYMBOL) || !arg->ident) {
        return arg;
    }
    if (!is_likely_type_ident(arg->ident)) {
        return arg;
    }
    size_t offset = 0;
    for (;;) {
        ChengToken *tok = peek(p, (int)offset);
        if (!token_is_symbol(tok, "*")) {
            break;
        }
        offset += 1;
    }
    if (offset == 0) {
        return arg;
    }
    ChengToken *after = peek(p, (int)offset);
    if (!token_is_symbol(after, "(")) {
        return arg;
    }
    ChengNode *base = arg;
    for (size_t i = 0; i < offset; i++) {
        ChengToken *star = peek(p, (int)i);
        ChengNode *node = cheng_node_new(NK_PTR_TY, star ? star->line : 0, star ? star->col : 0);
        if (base) {
            cheng_node_add(node, base);
        }
        base = node;
    }
    for (size_t i = 0; i < offset; i++) {
        advance(p);
    }
    return parse_postfix(p, base);
}

static ChengNode *chain_space_call_args(ChengParser *p, ChengNode *arg) {
    ChengNode *out = arg;
    while (out && is_space_call_arg_start(p) && space_call_same_line(p)) {
        ChengToken *tok = current(p);
        if (token_is_symbol(tok, "(")) {
            if (p->diags && tok) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                               "Space call does not allow parenthesized argument; use 'f x' or 'f(x)' for one argument, and 'f(x, y)' for multiple arguments");
            }
        }
        ChengNode *nested = cheng_node_new(NK_CALL, out->line, out->col);
        if (nested) {
            nested->call_style = CHENG_CALL_STYLE_SPACE;
        }
        cheng_node_add(nested, out);
        ChengNode *next_arg = parse_space_atom(p);
        if (next_arg) {
            cheng_node_add(nested, next_arg);
        }
        out = nested;
    }
    return out;
}

static ChengNode *parse_type_call_callee(ChengParser *p) {
    if (!is_type_call_start(p)) {
        return NULL;
    }
    size_t save = p->pos;
    size_t diag_len = p->diags ? p->diags->len : 0;
    ChengNode *type_node = parse_type_expr(p);
    if (type_node) {
        if (token_is_symbol(current(p), "*")) {
            while (token_is_symbol(current(p), "*")) {
                ChengToken *star = current(p);
                advance(p);
                ChengNode *node = cheng_node_new(NK_PTR_TY, star ? star->line : 0, star ? star->col : 0);
                if (type_node) {
                    cheng_node_add(node, type_node);
                }
                type_node = node;
            }
        }
        if (token_is_symbol(current(p), "(") && is_likely_type_expr(type_node)) {
            return type_node;
        }
    }
    if (type_node) {
        cheng_node_free(type_node);
    }
    p->pos = save;
    if (p->diags) {
        p->diags->len = diag_len;
    }
    return NULL;
}

static ChengNode *maybe_parse_type_ptr_base(ChengParser *p, ChengNode *base) {
    if (!base || !is_likely_type_expr(base)) {
        return base;
    }
    size_t offset = 0;
    for (;;) {
        ChengToken *tok = peek(p, (int)offset);
        if (!token_is_symbol(tok, "*")) {
            break;
        }
        offset += 1;
    }
    if (offset == 0) {
        return base;
    }
    ChengToken *after = peek(p, (int)offset);
    if (!token_is_symbol(after, "(")) {
        return base;
    }
    for (size_t i = 0; i < offset; i++) {
        ChengToken *star = current(p);
        ChengNode *node = cheng_node_new(NK_PTR_TY, star ? star->line : 0, star ? star->col : 0);
        if (base) {
            cheng_node_add(node, base);
        }
        base = node;
        advance(p);
    }
    return base;
}

static ChengNode *parse_unary(ChengParser *p) {
    ChengNode *type_callee = parse_type_call_callee(p);
    if (type_callee) {
        return parse_postfix(p, type_callee);
    }
    ChengToken *tok = current(p);
    if (!tok) {
        return NULL;
    }
    if (tok->kind == TK_SYMBOL && tok->len == 1 &&
        (tok->start[0] == '-' || tok->start[0] == '+' || tok->start[0] == '*' ||
         tok->start[0] == '&' || tok->start[0] == '!' || tok->start[0] == '~' ||
         tok->start[0] == '$')) {
        advance(p);
        ChengNode *rhs = parse_unary(p);
        char op_buf[2];
        op_buf[0] = tok->start[0];
        op_buf[1] = '\0';
        ChengNode *op = new_node_from_token_ident(NK_SYMBOL, tok, op_buf);
        ChengNode *node = cheng_node_new(NK_PREFIX, tok->line, tok->col);
        cheng_node_add(node, op);
        if (rhs) {
            cheng_node_add(node, rhs);
        }
        return node;
    }
    if (tok->kind == TK_KEYWORD && token_is_keyword(tok, "await")) {
        advance(p);
        ChengNode *rhs = parse_unary(p);
        ChengNode *op = new_node_from_token_ident(NK_IDENT, tok, "await");
        ChengNode *node = cheng_node_new(NK_PREFIX, tok->line, tok->col);
        cheng_node_add(node, op);
        if (rhs) {
            cheng_node_add(node, rhs);
        }
        return node;
    }
    ChengNode *node = parse_postfix(p, parse_primary(p));
    node = maybe_parse_type_ptr_base(p, node);
    return parse_postfix(p, node);
}

static ChengNode *parse_expression(ChengParser *p, int min_prec) {
    ChengNode *lhs = parse_unary(p);
    if (lhs && is_space_call_arg_start_for(p, lhs) && space_call_same_line(p)) {
        if (is_definite_type_expr(lhs) && p->diags) {
            ChengToken *tok = current(p);
            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename,
                           tok ? tok->line : lhs->line,
                           tok ? tok->col : lhs->col,
                           "Type conversion does not allow space call; use T(x)");
        }
        ChengToken *tok = current(p);
        if (token_is_symbol(tok, "(")) {
            if (p->diags && tok) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                               "Space call does not allow parenthesized argument; use 'f x' or 'f(x)' for one argument, and 'f(x, y)' for multiple arguments");
            }
        }
        if (lhs->kind == NK_IDENT && lhs->ident && strcmp(lhs->ident, "discard") == 0) {
            if (p->diags) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, lhs->line, lhs->col,
                               "discard syntax is removed; use a plain expression statement");
            }
            ChengNode *arg = parse_space_atom(p);
            arg = maybe_parse_space_type_ptr_call(p, arg);
            if (arg) {
                if (is_space_call_arg_start(p) && space_call_same_line(p)) {
                    if (p->diags && current(p)) {
                        cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, current(p)->line, current(p)->col,
                                       "Space call allows only one argument; wrap inner call in parentheses");
                    }
                    arg = chain_space_call_args(p, arg);
                }
                lhs = arg;
            }
        } else {
            ChengNode *call = cheng_node_new(NK_CALL, lhs->line, lhs->col);
            if (call) {
                call->call_style = CHENG_CALL_STYLE_SPACE;
            }
            cheng_node_add(call, lhs);
            ChengNode *arg = parse_space_atom(p);
            arg = maybe_parse_space_type_ptr_call(p, arg);
            if (arg) {
                if (is_space_call_arg_start(p) && space_call_same_line(p)) {
                    if (p->diags && current(p)) {
                        cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, current(p)->line, current(p)->col,
                                       "Space call allows only one argument; wrap inner call in parentheses");
                    }
                    arg = chain_space_call_args(p, arg);
                }
                cheng_node_add(call, arg);
            }
            lhs = call;
        }
    }
    for (;;) {
        ChengToken *tok = current(p);
        if (token_is_symbol(tok, "*") && is_likely_type_expr(lhs)) {
            ChengToken *nxt = peek(p, 1);
            int is_delim = 0;
            if (token_is_symbol(nxt, ")") || token_is_symbol(nxt, "]") || token_is_symbol(nxt, ",") ||
                token_is_symbol(nxt, ";") || token_is_symbol(nxt, ":") || token_is_symbol(nxt, "=")) {
                is_delim = 1;
            } else if (nxt && (nxt->kind == TK_NEWLINE || nxt->kind == TK_DEDENT || nxt->kind == TK_EOF)) {
                is_delim = 1;
            }
            if (is_delim) {
                advance(p);
                ChengNode *node = cheng_node_new(NK_PTR_TY, tok ? tok->line : 0, tok ? tok->col : 0);
                if (node) {
                    cheng_node_add(node, lhs);
                }
                lhs = node;
                continue;
            }
        }
        char op_buf[8];
        int prec = op_precedence(tok, op_buf, sizeof(op_buf));
        if (prec < min_prec) {
            break;
        }
        advance(p);
        ChengNode *rhs = parse_expression(p, prec + 1);
        ChengNode *op = new_node_from_token_ident(NK_SYMBOL, tok, op_buf);
        ChengNode *node = cheng_node_new(NK_INFIX, tok->line, tok->col);
        if (op) {
            cheng_node_add(node, op);
        }
        if (lhs) {
            cheng_node_add(node, lhs);
        }
        if (rhs) {
            cheng_node_add(node, rhs);
        }
        lhs = node;
    }
    if (lhs && min_prec <= PREC_TERNARY && is_ternary_question(p)) {
        ChengToken *qt = current(p);
        advance(p);
        ChengNode *then_expr = parse_expression(p, 0);
        expect_symbol(p, ":", "Expected ':'");
        ChengNode *else_expr = parse_expression(p, PREC_TERNARY);
        ChengNode *node = cheng_node_new(NK_IF_EXPR, qt ? qt->line : 0, qt ? qt->col : 0);
        cheng_node_add(node, lhs ? lhs : empty_node(qt));
        cheng_node_add(node, then_expr ? then_expr : empty_node(qt));
        cheng_node_add(node, else_expr ? else_expr : empty_node(qt));
        return node;
    }
    return lhs;
}

static ChengNode *parse_primary(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!tok) {
        return NULL;
    }
    if (tok->kind == TK_IDENT) {
        return parse_ident(p, 0);
    }
    if (tok->kind == TK_KEYWORD) {
        if (token_is_keyword(tok, "if")) {
            ChengToken *kw = tok;
            advance(p);
            ChengNode *cond = parse_expression(p, 0);
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *then_expr = parse_expression(p, 0);
            if (!(current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "else"))) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, kw->line, kw->col, "if expression requires else");
            } else {
                advance(p);
            }
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *else_expr = parse_expression(p, 0);
            ChengNode *node = cheng_node_new(NK_IF_EXPR, kw->line, kw->col);
            cheng_node_add(node, cond ? cond : empty_node(kw));
            cheng_node_add(node, then_expr ? then_expr : empty_node(kw));
            cheng_node_add(node, else_expr ? else_expr : empty_node(kw));
            return node;
        }
        if (token_is_keyword(tok, "when")) {
            ChengToken *kw = tok;
            advance(p);
            ChengNode *cond = parse_expression(p, 0);
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *then_expr = parse_expression(p, 0);
            if (!(current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "else"))) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, kw->line, kw->col, "when expression requires else");
            } else {
                advance(p);
            }
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *else_expr = parse_expression(p, 0);
            ChengNode *node = cheng_node_new(NK_WHEN_EXPR, kw->line, kw->col);
            cheng_node_add(node, cond ? cond : empty_node(kw));
            cheng_node_add(node, then_expr ? then_expr : empty_node(kw));
            cheng_node_add(node, else_expr ? else_expr : empty_node(kw));
            return node;
        }
        if (token_is_keyword(tok, "case")) {
            ChengToken *kw = tok;
            advance(p);
            ChengNode *selector = parse_expression(p, 0);
            if (token_is_symbol(current(p), ":")) {
                advance(p);
            }
            ChengNode *node = cheng_node_new(NK_CASE_EXPR, kw->line, kw->col);
            cheng_node_add(node, selector ? selector : empty_node(kw));
            if (current(p) && current(p)->kind == TK_NEWLINE) {
                skip_newlines(p);
                if (current(p) && current(p)->kind == TK_INDENT) {
                    advance(p);
                }
            }
            while (current(p) && current(p)->kind != TK_DEDENT && current(p)->kind != TK_EOF) {
                if (current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "of")) {
                    ChengToken *of_kw = current(p);
                    advance(p);
                    ChengNode *pat = parse_expression(p, 0);
                    expect_symbol(p, ":", "Expected ':'");
                    ChengNode *expr = parse_expression(p, 0);
                    ChengNode *branch = cheng_node_new(NK_OF_BRANCH, of_kw->line, of_kw->col);
                    cheng_node_add(branch, pat ? pat : empty_node(of_kw));
                    cheng_node_add(branch, expr ? expr : empty_node(of_kw));
                    cheng_node_add(node, branch);
                } else if (current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "else")) {
                    ChengToken *else_kw = current(p);
                    advance(p);
                    expect_symbol(p, ":", "Expected ':'");
                    ChengNode *expr = parse_expression(p, 0);
                    ChengNode *else_node = cheng_node_new(NK_ELSE, else_kw->line, else_kw->col);
                    cheng_node_add(else_node, expr ? expr : empty_node(else_kw));
                    cheng_node_add(node, else_node);
                } else {
                    advance(p);
                }
                skip_stmt_seps(p);
            }
            if (current(p) && current(p)->kind == TK_DEDENT) {
                advance(p);
            }
            return node;
        }
        if (token_is_keyword(tok, "for")) {
            ChengToken *kw = tok;
            advance(p);
            ChengNode *name = parse_ident(p, 1);
            ChengNode *pattern = cheng_node_new(NK_PATTERN, name ? name->line : 0, name ? name->col : 0);
            if (name) {
                cheng_node_add(pattern, name);
            }
            if (!(current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "in"))) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, kw->line, kw->col, "for requires in");
            } else {
                advance(p);
            }
            ChengNode *iter = parse_expression(p, 0);
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *expr = parse_expression(p, 0);
            ChengNode *node = cheng_node_new(NK_COMPREHENSION, kw->line, kw->col);
            cheng_node_add(node, pattern);
            cheng_node_add(node, iter ? iter : empty_node(kw));
            cheng_node_add(node, expr ? expr : empty_node(kw));
            return node;
        }
        if (token_is_keyword(tok, "true") || token_is_keyword(tok, "false")) {
            int is_true = token_is_keyword(tok, "true");
            ChengNode *node = new_node_from_token_ident(NK_BOOL_LIT, tok, is_true ? "true" : "false");
            if (node) {
                node->int_val = is_true ? 1 : 0;
            }
            advance(p);
            return node;
        }
        if (token_is_keyword(tok, "nil")) {
            ChengNode *node = cheng_node_new(NK_NIL_LIT, tok->line, tok->col);
            advance(p);
            return node;
        }
        if (token_is_keyword(tok, "elif") || token_is_keyword(tok, "else") ||
            token_is_keyword(tok, "of")) {
            return NULL;
        }
        return parse_ident(p, 1);
    }
    if (tok->kind == TK_NUMBER) {
        char *num = copy_token(tok);
        ChengNode *node = NULL;
        if (num && (strchr(num, '.') || strchr(num, 'e') || strchr(num, 'E'))) {
            node = cheng_node_new(NK_FLOAT_LIT, tok->line, tok->col);
            if (node) {
                node->float_val = strtod(num, NULL);
            }
        } else {
            node = cheng_node_new(NK_INT_LIT, tok->line, tok->col);
            if (node) {
                node->int_val = num ? strtoll(num, NULL, 10) : 0;
            }
        }
        free(num);
        advance(p);
        return node;
    }
    if (tok->kind == TK_STRING) {
        char *text = decode_string_lit(p, tok);
        ChengNode *node = new_node_from_token_str(NK_STR_LIT, tok, text);
        free(text);
        advance(p);
        return node;
    }
    if (tok->kind == TK_SYMBOL && tok->len == 1 && tok->start[0] == '@' &&
        token_is_symbol(peek(p, 1), "[")) {
        ChengToken *at = tok;
        advance(p);
        advance(p);
        ChengNode *node = cheng_node_new(NK_SEQ_LIT, at->line, at->col);
        if (!token_is_symbol(current(p), "]")) {
            while (1) {
                ChengNode *elem = parse_expression(p, 0);
                if (elem) {
                    cheng_node_add(node, elem);
                }
                if (!match_symbol(p, ",")) {
                    break;
                }
            }
        }
        expect_symbol(p, "]", "Expected ']'");
        return node;
    }
    if (token_is_symbol(tok, "[")) {
        ChengToken *br = tok;
        advance(p);
        ChengNode *node = cheng_node_new(NK_BRACKET, br->line, br->col);
        if (!token_is_symbol(current(p), "]")) {
            while (1) {
                ChengNode *elem = parse_expression(p, 0);
                if (elem) {
                    cheng_node_add(node, elem);
                }
                if (!match_symbol(p, ",")) {
                    break;
                }
            }
        }
        expect_symbol(p, "]", "Expected ']'");
        return node;
    }
    if (tok->kind == TK_CHAR) {
        char *text = copy_token(tok);
        ChengNode *node = new_node_from_token_str(NK_CHAR_LIT, tok, text);
        free(text);
        advance(p);
        return node;
    }
    if (token_is_symbol(tok, "(")) {
        size_t save = p->pos;
        int line = tok->line;
        int col = tok->col;
        if (is_likely_type_token_start(peek(p, 1))) {
            advance(p);
            ChengNode *type_node = parse_type_expr(p);
            if (type_node && token_is_symbol(current(p), ")") && is_likely_type_expr(type_node)) {
                ChengToken *after = peek(p, 1);
                if (after && is_c_style_cast_follow_token(after)) {
                    if (p->diags) {
                        cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, line, col,
                                       "C-style cast is removed; use T(x)");
                    }
                    advance(p);
                    ChengNode *expr = parse_unary(p);
                    if (type_node) {
                        cheng_node_free(type_node);
                    }
                    if (expr) {
                        cheng_node_free(expr);
                    }
                    return cheng_node_new(NK_ERROR, line, col);
                }
            }
            if (type_node) {
                cheng_node_free(type_node);
            }
            p->pos = save;
        }
        advance(p);
        ChengNode *expr = parse_expression(p, 0);
        expect_symbol(p, ")", "Expected ')'");
        ChengNode *par = cheng_node_new(NK_PAR, tok->line, tok->col);
        if (expr) {
            cheng_node_add(par, expr);
        }
        return par;
    }
    return NULL;
}

static int parse_import_part(ChengParser *p, char **out) {
    ChengToken *tok = current(p);
    if (!tok || (!token_is_ident(tok) && tok->kind != TK_KEYWORD)) {
        return 0;
    }
    char *part = copy_token(tok);
    if (!part) {
        return 0;
    }
    advance(p);
    for (;;) {
        tok = current(p);
        if (!token_is_symbol(tok, "-")) {
            break;
        }
        ChengToken *next_tok = peek(p, 1);
        if (!next_tok || (!token_is_ident(next_tok) && next_tok->kind != TK_KEYWORD)) {
            break;
        }
        advance(p);
        tok = current(p);
        char *tail = copy_token(tok);
        if (!tail) {
            free(part);
            return 0;
        }
        size_t part_len = strlen(part);
        size_t tail_len = strlen(tail);
        char *next_part = (char *)realloc(part, part_len + tail_len + 2);
        if (!next_part) {
            free(tail);
            free(part);
            return 0;
        }
        part = next_part;
        part[part_len] = '-';
        memcpy(part + part_len + 1, tail, tail_len);
        part[part_len + tail_len + 1] = '\0';
        free(tail);
        advance(p);
    }
    *out = part;
    return 1;
}

static ChengNode *parse_import_path(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!tok || (!token_is_ident(tok) && tok->kind != TK_KEYWORD)) {
        return NULL;
    }
    ChengToken *start_tok = tok;
    char *path = NULL;
    if (!parse_import_part(p, &path) || !path) {
        return NULL;
    }
    for (;;) {
        tok = current(p);
        if (!token_is_symbol(tok, ".") && !token_is_symbol(tok, "/")) {
            break;
        }
        if (token_is_symbol(tok, ".") && p->diags) {
            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                           "dot-separated imports are not supported; use '/'");
        }
        ChengToken *next_tok = peek(p, 1);
        if (!next_tok || (!token_is_ident(next_tok) && next_tok->kind != TK_KEYWORD)) {
            break;
        }
        advance(p);
        char *part = NULL;
        if (!parse_import_part(p, &part) || !part) {
            break;
        }
        size_t path_len = strlen(path);
        size_t part_len = strlen(part);
        char *next_path = (char *)realloc(path, path_len + part_len + 2);
        if (!next_path) {
            free(part);
            free(path);
            return NULL;
        }
        path = next_path;
        path[path_len] = '/';
        memcpy(path + path_len + 1, part, part_len);
        path[path_len + part_len + 1] = '\0';
        free(part);
    }
    ChengNode *node = new_node_from_token_ident(NK_IDENT, start_tok, path);
    free(path);
    return node;
}

static ChengNode *parse_import_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *stmt = new_node_from_token_ident(NK_IMPORT_STMT, kw, NULL);
    if (!stmt) {
        return NULL;
    }
    ChengToken *tok = current(p);
    ChengNode *item = NULL;
    if (tok && (token_is_symbol(tok, ".") || token_is_symbol(tok, "/"))) {
        if (p->diags) {
            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                           "relative/absolute imports are not supported; use normalized module paths like 'cheng/libp2p/...'");
        }
        item = empty_node(tok);
        advance(p);
        while (!is_line_end(current(p))) {
            tok = current(p);
            if (token_is_symbol(tok, ";") || token_is_symbol(tok, "}")) {
                break;
            }
            advance(p);
        }
    } else if (tok && tok->kind == TK_STRING) {
        if (p->diags) {
            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                           "string/path imports are not supported; use normalized module paths like 'cheng/libp2p/...'");
        }
        item = empty_node(tok);
        advance(p);
    } else {
        item = parse_import_path(p);
        if (item) {
            tok = current(p);
            if (token_is_symbol(tok, "/")) {
                ChengToken *next = peek(p, 1);
                if (next && token_is_symbol(next, "[")) {
                    advance(p);
                    advance(p);
                    ChengNode *group = cheng_node_new(NK_IMPORT_GROUP, item->line, item->col);
                    if (!group || cheng_node_add(group, item) != 0) {
                        cheng_node_free(group);
                        cheng_node_free(item);
                        cheng_node_free(stmt);
                        return NULL;
                    }
                    item = group;
                    tok = current(p);
                    if (token_is_symbol(tok, "]")) {
                        if (p->diags) {
                            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                                           "import group cannot be empty");
                        }
                        advance(p);
                    } else {
                        for (;;) {
                            ChengNode *entry = parse_import_path(p);
                            if (!entry) {
                                if (p->diags && tok) {
                                    cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                                                   "invalid import group entry");
                                }
                                break;
                            }
                            if (cheng_node_add(item, entry) != 0) {
                                cheng_node_free(entry);
                                cheng_node_free(stmt);
                                return NULL;
                            }
                            tok = current(p);
                            if (token_is_symbol(tok, ",")) {
                                advance(p);
                                tok = current(p);
                                if (token_is_symbol(tok, "]")) {
                                    advance(p);
                                    break;
                                }
                                continue;
                            }
                            if (token_is_symbol(tok, "]")) {
                                advance(p);
                                break;
                            }
                            if (p->diags && tok) {
                                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                                               "expected ',' or ']' in import group");
                            }
                            while (!is_line_end(current(p))) {
                                tok = current(p);
                                if (token_is_symbol(tok, "]")) {
                                    advance(p);
                                    break;
                                }
                                if (token_is_symbol(tok, ";") || token_is_symbol(tok, "}")) {
                                    break;
                                }
                                advance(p);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    if (!item) {
        cheng_node_free(stmt);
        if (p->diags && kw) {
            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, kw->line, kw->col, "invalid import statement");
        }
        return NULL;
    }
    tok = current(p);
    if (item && item->kind == NK_IMPORT_GROUP && token_is_keyword(tok, "as")) {
        if (p->diags && tok) {
            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                           "import group does not support alias; split into multiple imports");
        }
        advance(p);
        if (token_is_ident(current(p)) || (current(p) && current(p)->kind == TK_KEYWORD)) {
            advance(p);
        }
        return stmt;
    }
    if (token_is_keyword(tok, "as")) {
        advance(p);
        tok = current(p);
        if (token_is_ident(tok) || (tok && tok->kind == TK_KEYWORD)) {
            char *alias = copy_token(tok);
            ChengNode *alias_ident = new_node_from_token_ident(NK_IDENT, tok, alias);
            free(alias);
            ChengNode *as_node = cheng_node_new(NK_IMPORT_AS, item ? item->line : 0, item ? item->col : 0);
            if (!alias_ident || !as_node ||
                cheng_node_add(as_node, item) != 0 ||
                cheng_node_add(as_node, alias_ident) != 0 ||
                cheng_node_add(stmt, as_node) != 0) {
                cheng_node_free(item);
                cheng_node_free(alias_ident);
                cheng_node_free(as_node);
                cheng_node_free(stmt);
                return NULL;
            }
            advance(p);
            return stmt;
        }
        if (p->diags && tok) {
            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col, "invalid import alias");
        }
    }
    if (cheng_node_add(stmt, item) != 0) {
        cheng_node_free(item);
        cheng_node_free(stmt);
        return NULL;
    }
    tok = current(p);
    if (token_is_symbol(tok, ",")) {
        if (p->diags) {
            cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, tok->line, tok->col,
                           "import supports only a single path; split into multiple lines");
        }
        advance(p);
        while (!is_line_end(current(p))) {
            tok = current(p);
            if (token_is_symbol(tok, ";") || token_is_symbol(tok, "}")) {
                break;
            }
            advance(p);
        }
    }
    return stmt;
}

static void skip_type_entry(ChengParser *p) {
    size_t start_pos = p->pos;
    ChengNode *name = parse_ident(p, 1);
    if (name) {
        cheng_node_free(name);
    }
    if (token_is_symbol(current(p), "[")) {
        skip_bracketed(p);
    }
    if (match_symbol(p, "=")) {
        if (current(p) && current(p)->kind == TK_NEWLINE) {
            skip_newlines(p);
            if (current(p) && current(p)->kind == TK_INDENT) {
                skip_indent_block(p);
            }
            return;
        }
        ChengNode *ty = parse_type_expr(p);
        if (ty) {
            cheng_node_free(ty);
        }
        if (current(p) && current(p)->kind == TK_NEWLINE) {
            skip_newlines(p);
            if (current(p) && current(p)->kind == TK_INDENT) {
                skip_indent_block(p);
            }
        }
    }
    if (p->pos == start_pos) {
        advance(p);
    }
}

static ChengNode *parse_type_entry(ChengParser *p);
static ChengNode *parse_record_body(ChengParser *p, const ChengToken *start);

static ChengNode *parse_record_field(ChengParser *p) {
    ChengToken *tok = current(p);
    if (!tok || tok->kind == TK_DEDENT) {
        return NULL;
    }
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        return NULL;
    }
    ChengNode *typ = empty_node(tok);
    if (match_symbol(p, ":")) {
        typ = parse_type_expr(p);
    }
    if (match_symbol(p, "=")) {
        ChengNode *defval = parse_expression(p, 0);
        if (defval) {
            cheng_node_free(defval);
        }
    }
    ChengNode *defs = cheng_node_new(NK_IDENT_DEFS, name->line, name->col);
    cheng_node_add(defs, name);
    if (typ) {
        cheng_node_add(defs, typ);
    }
    return defs;
}

static ChengNode *parse_record_case(ChengParser *p, const ChengToken *start) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        return NULL;
    }
    ChengNode *typ = empty_node(kw);
    if (match_symbol(p, ":")) {
        typ = parse_type_expr(p);
    }
    ChengNode *defval = empty_node(kw);
    if (match_symbol(p, "=")) {
        defval = parse_expression(p, 0);
    }
    ChengNode *defs = cheng_node_new(NK_IDENT_DEFS, name->line, name->col);
    cheng_node_add(defs, name);
    if (typ) {
        cheng_node_add(defs, typ);
    }
    if (defval) {
        cheng_node_add(defs, defval);
    }
    ChengNode *node = cheng_node_new(NK_REC_CASE, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(node, defs);
    int has_indent = 0;
    if (current(p) && current(p)->kind == TK_NEWLINE) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            has_indent = 1;
            advance(p);
        }
    }
    for (;;) {
        ChengToken *tok = current(p);
        if (!tok || tok->kind == TK_EOF) {
            break;
        }
        if (tok->kind == TK_DEDENT) {
            if (has_indent) {
                advance(p);
            }
            break;
        }
        if (tok->kind == TK_KEYWORD && token_is_keyword(tok, "of")) {
            ChengToken *of_kw = current(p);
            advance(p);
            ChengNode *pat = parse_expression(p, 0);
            if (match_symbol(p, ",")) {
                ChengNode *pat_list = cheng_node_new(NK_PATTERN, of_kw ? of_kw->line : 0, of_kw ? of_kw->col : 0);
                if (pat) {
                    cheng_node_add(pat_list, pat);
                }
                while (1) {
                    ChengNode *next = parse_expression(p, 0);
                    if (next) {
                        cheng_node_add(pat_list, next);
                    }
                    if (!match_symbol(p, ",")) {
                        break;
                    }
                }
                pat = pat_list;
            }
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *body = NULL;
            if (current(p) && current(p)->kind == TK_NEWLINE) {
                skip_newlines(p);
            }
            if (current(p) && current(p)->kind == TK_INDENT) {
                body = parse_record_body(p, of_kw);
            }
            if (!body) {
                body = cheng_node_new(NK_REC_LIST, of_kw ? of_kw->line : 0, of_kw ? of_kw->col : 0);
            }
            ChengNode *branch = cheng_node_new(NK_OF_BRANCH, of_kw ? of_kw->line : 0, of_kw ? of_kw->col : 0);
            cheng_node_add(branch, pat ? pat : empty_node(of_kw));
            cheng_node_add(branch, body);
            cheng_node_add(node, branch);
        } else if (current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "else")) {
            ChengToken *else_kw = current(p);
            advance(p);
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *body = NULL;
            if (current(p) && current(p)->kind == TK_NEWLINE) {
                skip_newlines(p);
            }
            if (current(p) && current(p)->kind == TK_INDENT) {
                body = parse_record_body(p, else_kw);
            }
            if (!body) {
                body = cheng_node_new(NK_REC_LIST, else_kw ? else_kw->line : 0, else_kw ? else_kw->col : 0);
            }
            ChengNode *else_node = cheng_node_new(NK_ELSE, else_kw ? else_kw->line : 0, else_kw ? else_kw->col : 0);
            cheng_node_add(else_node, body);
            cheng_node_add(node, else_node);
        } else {
            if (!has_indent) {
                break;
            }
            advance(p);
        }
        skip_stmt_seps(p);
    }
    return node;
}

static ChengNode *parse_record_entry(ChengParser *p, const ChengToken *start) {
    if (current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "case")) {
        return parse_record_case(p, start);
    }
    return parse_record_field(p);
}

static ChengNode *parse_record_body(ChengParser *p, const ChengToken *start) {
    if (!current(p) || current(p)->kind != TK_INDENT) {
        return NULL;
    }
    ChengNode *rec = cheng_node_new(NK_REC_LIST, start ? start->line : 0, start ? start->col : 0);
    advance(p);
    while (current(p) && current(p)->kind != TK_DEDENT && current(p)->kind != TK_EOF) {
        if (current(p)->kind == TK_NEWLINE) {
            skip_stmt_seps(p);
            continue;
        }
        ChengNode *entry = parse_record_entry(p, start);
        if (entry) {
            cheng_node_add(rec, entry);
        } else {
            advance(p);
        }
        skip_stmt_seps(p);
    }
    if (current(p) && current(p)->kind == TK_DEDENT) {
        advance(p);
    }
    return rec;
}

static ChengNode *parse_object_type(ChengParser *p, const ChengToken *start) {
    ChengNode *base = empty_node(start);
    if (current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "of")) {
        advance(p);
        base = parse_type_expr(p);
        if (!base) {
            base = empty_node(start);
        }
    }
    int has_fields = 0;
    if (match_symbol(p, ":")) {
        has_fields = 1;
    } else if (current(p) && current(p)->kind == TK_NEWLINE) {
        has_fields = 1;
    }
    ChengNode *rec = NULL;
    if (has_fields) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            rec = parse_record_body(p, start);
        }
    }
    ChengNode *obj = cheng_node_new(NK_OBJECT_DECL, start ? start->line : 0, start ? start->col : 0);
    if (!obj) {
        if (base) {
            cheng_node_free(base);
        }
        if (rec) {
            cheng_node_free(rec);
        }
        return NULL;
    }
    if (base) {
        cheng_node_add(obj, base);
    }
    if (rec) {
        cheng_node_add(obj, rec);
    }
    return obj;
}

static void skip_enum_body(ChengParser *p) {
    if (current(p) && current(p)->kind == TK_NEWLINE) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            skip_indent_block(p);
        }
        return;
    }
    while (current(p) && current(p)->kind != TK_NEWLINE && current(p)->kind != TK_DEDENT &&
           current(p)->kind != TK_EOF) {
        advance(p);
    }
}

static ChengNode *parse_enum_field(ChengParser *p, const ChengToken *tok) {
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        return NULL;
    }
    ChengNode *field = cheng_node_new(NK_ENUM_FIELD_DECL, name->line, name->col);
    if (!field) {
        cheng_node_free(name);
        return NULL;
    }
    cheng_node_add(field, name);
    if (match_symbol(p, "=")) {
        ChengNode *value = parse_expression(p, 0);
        if (value) {
            cheng_node_add(field, value);
        } else {
            cheng_node_add(field, empty_node(tok));
        }
    }
    return field;
}

static ChengNode *parse_enum_body(ChengParser *p, const ChengToken *tok) {
    ChengNode *node = cheng_node_new(NK_ENUM_DECL, tok ? tok->line : 0, tok ? tok->col : 0);
    if (!node) {
        return NULL;
    }
    ChengNode *base = empty_node(tok);
    if (match_symbol(p, ":")) {
        ChengNode *parsed = parse_type_expr(p);
        if (parsed) {
            base = parsed;
        } else {
            base = empty_node(tok);
        }
    }
    cheng_node_add(node, base);
    if (current(p) && current(p)->kind == TK_NEWLINE) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            advance(p);
            while (current(p) && current(p)->kind != TK_DEDENT && current(p)->kind != TK_EOF) {
                ChengNode *field = parse_enum_field(p, tok);
                if (field) {
                    cheng_node_add(node, field);
                } else {
                    advance(p);
                }
                if (match_symbol(p, ",")) {
                    continue;
                }
                skip_stmt_seps(p);
            }
            if (current(p) && current(p)->kind == TK_DEDENT) {
                advance(p);
            }
            return node;
        }
    }
    while (current(p) && current(p)->kind != TK_NEWLINE && current(p)->kind != TK_DEDENT &&
           current(p)->kind != TK_EOF) {
        ChengNode *field = parse_enum_field(p, tok);
        if (field) {
            cheng_node_add(node, field);
        } else {
            advance(p);
        }
        if (!match_symbol(p, ",")) {
            break;
        }
    }
    return node;
}

static ChengNode *parse_type_entry(ChengParser *p) {
    ChengToken *tok = current(p);
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        return NULL;
    }
    ChengNode *generics = empty_node(tok);
    if (token_is_symbol(current(p), "[")) {
        ChengNode *parsed = parse_type_params(p);
        if (parsed) {
            generics = parsed;
        }
    }
    if (!match_symbol(p, "=")) {
        cheng_node_free(name);
        cheng_node_free(generics);
        return NULL;
    }
    if (current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "enum")) {
        ChengToken *kw = current(p);
        advance(p);
        ChengNode *enum_body = parse_enum_body(p, kw);
        ChengNode *alias = cheng_node_new(NK_TYPE_DECL, kw ? kw->line : 0, kw ? kw->col : 0);
        if (!alias) {
            cheng_node_free(enum_body);
            cheng_node_free(name);
            cheng_node_free(generics);
            return NULL;
        }
        cheng_node_add(alias, name);
        if (enum_body) {
            cheng_node_add(alias, enum_body);
        } else {
            cheng_node_add(alias, empty_node(tok));
        }
        cheng_node_add(alias, generics);
        cheng_node_add(alias, empty_node(tok));
        return alias;
    }
    if (current(p) && current(p)->kind == TK_NEWLINE) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            ChengNode *rec = parse_record_body(p, tok);
            ChengNode *obj = cheng_node_new(NK_OBJECT_DECL, tok ? tok->line : 0, tok ? tok->col : 0);
            cheng_node_add(obj, name);
            if (rec) {
                cheng_node_add(obj, rec);
            }
            cheng_node_add(obj, generics);
            cheng_node_add(obj, empty_node(tok));
            return obj;
        }
    }
    ChengNode *alias = cheng_node_new(NK_TYPE_DECL, tok ? tok->line : 0, tok ? tok->col : 0);
    ChengNode *ty = parse_type_expr(p);
    if (!ty) {
        ty = empty_node(tok);
    }
    cheng_node_add(alias, name);
    cheng_node_add(alias, ty);
    cheng_node_add(alias, generics);
    cheng_node_add(alias, empty_node(tok));
    return alias;
}

static ChengNode *parse_type_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *node = cheng_node_new(NK_TYPE_DECL, kw ? kw->line : 0, kw ? kw->col : 0);
    if (current(p) && current(p)->kind == TK_NEWLINE) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            advance(p);
            while (current(p) && current(p)->kind != TK_DEDENT && current(p)->kind != TK_EOF) {
                ChengNode *entry = parse_type_entry(p);
                if (entry) {
                    cheng_node_add(node, entry);
                } else {
                    skip_type_entry(p);
                }
                skip_stmt_seps(p);
            }
            if (current(p) && current(p)->kind == TK_DEDENT) {
                advance(p);
            }
            return node;
        }
    }
    {
        ChengNode *entry = parse_type_entry(p);
        if (entry) {
            cheng_node_add(node, entry);
        } else {
            skip_type_entry(p);
        }
    }
    return node;
}

static ChengNode *parse_formal_params(ChengParser *p) {
    ChengToken *tok = current(p);
    ChengNode *params = cheng_node_new(NK_FORMAL_PARAMS, tok ? tok->line : 0, tok ? tok->col : 0);
    expect_symbol(p, "(", "Expected '('");
    if (!token_is_symbol(current(p), ")")) {
        while (1) {
            ChengNode *defs = NULL;
            ChengNode *typ = empty_node(tok);
            int has_type = 0;
            ChengNode *name = parse_ident(p, 1);
            if (!name) {
                break;
            }
            defs = cheng_node_new(NK_IDENT_DEFS, name->line, name->col);
            if (!defs) {
                cheng_node_free(name);
                break;
            }
            cheng_node_add(defs, name);
            while (match_symbol(p, ",")) {
                ChengNode *next_name = parse_ident(p, 1);
                if (!next_name) {
                    break;
                }
                cheng_node_add(defs, next_name);
            }
            if (match_symbol(p, ":")) {
                ChengNode *parsed = parse_type_expr(p);
                if (parsed) {
                    typ = parsed;
                } else {
                    typ = empty_node(tok);
                }
                has_type = 1;
            }
            if (match_symbol(p, "=")) {
                ChengNode *defval = parse_expression(p, 0);
                if (defval) {
                    cheng_node_free(defval);
                }
            }
            for (size_t i = 0; i < defs->len; i++) {
                ChengNode *single = cheng_node_new(NK_IDENT_DEFS,
                                                  defs->kids[i] ? defs->kids[i]->line : 0,
                                                  defs->kids[i] ? defs->kids[i]->col : 0);
                if (!single) {
                    continue;
                }
                cheng_node_add(single, defs->kids[i]);
                if (has_type) {
                    ChengNode *type_copy = typ ? clone_tree(typ) : NULL;
                    if (!type_copy) {
                        type_copy = empty_node(tok);
                    }
                    cheng_node_add(single, type_copy);
                } else {
                    cheng_node_add(single, empty_node(tok));
                }
                cheng_node_add(params, single);
            }
            defs->len = 0;
            cheng_node_free(defs);
            if (typ) {
                cheng_node_free(typ);
            }
            if (!match_symbol(p, ",") && !match_symbol(p, ";")) {
                break;
            }
        }
    }
    expect_symbol(p, ")", "Expected ')'");
    return params;
}

static ChengNode *parse_suite(ChengParser *p);

static ChengNode *parse_fn_decl(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        name = empty_node(kw);
    }
    ChengNode *generics = empty_node(kw);
    if (token_is_symbol(current(p), "[")) {
        ChengNode *parsed = parse_type_params(p);
        if (parsed) {
            generics = parsed;
        }
    }
    ChengNode *params = parse_formal_params(p);
    ChengNode *ret_type = empty_node(kw);
    if (match_symbol(p, ":")) {
        ret_type = parse_type_expr(p);
    }
    ChengNode *body = empty_node(kw);
    if (match_symbol(p, "=")) {
        body = parse_suite(p);
    }
    ChengNode *decl = cheng_node_new(NK_FN_DECL, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(decl, name);
    cheng_node_add(decl, params);
    cheng_node_add(decl, ret_type);
    cheng_node_add(decl, body);
    cheng_node_add(decl, generics);
    cheng_node_add(decl, empty_node(kw));
    return decl;
}

static void attach_async_pragma(ChengNode *decl, const ChengToken *tok) {
    if (!decl || decl->len < 6) {
        return;
    }
    ChengNode *pragma = cheng_node_new(NK_PRAGMA, tok ? tok->line : 0, tok ? tok->col : 0);
    ChengNode *ident = new_node_from_token_ident(NK_IDENT, tok, "async");
    if (ident) {
        cheng_node_add(pragma, ident);
    }
    decl->kids[5] = pragma;
}

static ChengNode *parse_fn_decl_kind(ChengParser *p, ChengNodeKind kind) {
    ChengNode *decl = parse_fn_decl(p);
    if (decl) {
        decl->kind = kind;
    }
    return decl;
}

static ChengNode *parse_block_decl(ChengParser *p, ChengNodeKind kind) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        name = empty_node(kw);
    }
    expect_symbol(p, ":", "Expected ':'");
    ChengNode *body = parse_suite(p);
    ChengNode *decl = cheng_node_new(kind, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(decl, name);
    cheng_node_add(decl, body ? body : empty_node(kw));
    cheng_node_add(decl, empty_node(kw));
    return decl;
}

static ChengNode *parse_template_decl(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        name = empty_node(kw);
    }
    ChengNode *generics = empty_node(kw);
    if (token_is_symbol(current(p), "[")) {
        ChengNode *parsed = parse_type_params(p);
        if (parsed) {
            generics = parsed;
        }
    }
    ChengNode *params = parse_formal_params(p);
    ChengNode *ret_type = empty_node(kw);
    if (match_symbol(p, ":")) {
        ret_type = parse_type_expr(p);
    }
    ChengNode *body = empty_node(kw);
    if (match_symbol(p, "=")) {
        body = parse_suite(p);
    }
    ChengNode *decl = cheng_node_new(NK_TEMPLATE_DECL, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(decl, name);
    cheng_node_add(decl, params);
    cheng_node_add(decl, ret_type);
    cheng_node_add(decl, body);
    cheng_node_add(decl, generics);
    cheng_node_add(decl, empty_node(kw));
    return decl;
}

static ChengNode *parse_macro_decl(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        name = empty_node(kw);
    }
    ChengNode *generics = empty_node(kw);
    if (token_is_symbol(current(p), "[")) {
        ChengNode *parsed = parse_type_params(p);
        if (parsed) {
            generics = parsed;
        }
    }
    ChengNode *params = parse_formal_params(p);
    ChengNode *ret_type = empty_node(kw);
    if (match_symbol(p, ":")) {
        ret_type = parse_type_expr(p);
    }
    ChengNode *body = empty_node(kw);
    if (match_symbol(p, "=")) {
        body = parse_suite(p);
    }
    ChengNode *decl = cheng_node_new(NK_MACRO_DECL, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(decl, name);
    cheng_node_add(decl, params);
    cheng_node_add(decl, ret_type);
    cheng_node_add(decl, body);
    cheng_node_add(decl, generics);
    cheng_node_add(decl, empty_node(kw));
    return decl;
}

static ChengNode *parse_return_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *node = cheng_node_new(NK_RETURN, kw ? kw->line : 0, kw ? kw->col : 0);
    if (!is_line_end(current(p))) {
        ChengNode *expr = parse_expression(p, 0);
        if (expr) {
            cheng_node_add(node, expr);
        } else {
            cheng_node_add(node, empty_node(kw));
        }
    } else {
        cheng_node_add(node, empty_node(kw));
    }
    return node;
}

static ChengNode *parse_if_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *cond = parse_expression(p, 0);
    expect_symbol(p, ":", "Expected ':'");
    ChengNode *body = parse_suite(p);
    ChengNode *ifn = cheng_node_new(NK_IF, kw ? kw->line : 0, kw ? kw->col : 0);
    if (cond) {
        cheng_node_add(ifn, cond);
    } else {
        cheng_node_add(ifn, empty_node(kw));
    }
    cheng_node_add(ifn, body);
    ChengNode *tail = ifn;
    skip_newlines(p);
    while (current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "elif")) {
        ChengToken *elif_kw = current(p);
        advance(p);
        ChengNode *elif_cond = parse_expression(p, 0);
        expect_symbol(p, ":", "Expected ':'");
        ChengNode *elif_body = parse_suite(p);
        ChengNode *elif_node = cheng_node_new(NK_IF, elif_kw ? elif_kw->line : 0, elif_kw ? elif_kw->col : 0);
        if (elif_cond) {
            cheng_node_add(elif_node, elif_cond);
        } else {
            cheng_node_add(elif_node, empty_node(elif_kw));
        }
        cheng_node_add(elif_node, elif_body);
        ChengNode *else_wrap = cheng_node_new(NK_ELSE, elif_kw ? elif_kw->line : 0, elif_kw ? elif_kw->col : 0);
        cheng_node_add(else_wrap, elif_node);
        cheng_node_add(tail, else_wrap);
        tail = elif_node;
        skip_newlines(p);
    }
    skip_newlines(p);
    if (current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "else")) {
        ChengToken *else_kw = current(p);
        advance(p);
        expect_symbol(p, ":", "Expected ':'");
        ChengNode *else_body = parse_suite(p);
        ChengNode *else_node = cheng_node_new(NK_ELSE, else_kw ? else_kw->line : 0, else_kw ? else_kw->col : 0);
        cheng_node_add(else_node, else_body);
        cheng_node_add(tail, else_node);
    }
    return ifn;
}

static ChengNode *parse_var_decl_entry(ChengParser *p, ChengNodeKind kind, ChengToken *kw) {
    ChengNode *name = parse_ident(p, 1);
    if (!name) {
        return empty_node(kw);
    }
    ChengNode *pattern = cheng_node_new(NK_PATTERN, name ? name->line : 0, name ? name->col : 0);
    if (name) {
        cheng_node_add(pattern, name);
    }
    ChengNode *typ = empty_node(kw);
    ChengNode *val = empty_node(kw);
    if (match_symbol(p, ":")) {
        typ = parse_type_expr(p);
    }
    if (match_symbol(p, "=")) {
        val = parse_expression(p, 0);
    }
    ChengNode *decl = cheng_node_new(kind, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(decl, pattern);
    cheng_node_add(decl, typ);
    cheng_node_add(decl, val);
    cheng_node_add(decl, empty_node(kw));
    return decl;
}

static ChengNode *parse_var_stmt(ChengParser *p, ChengNodeKind kind) {
    ChengToken *kw = current(p);
    advance(p);
    if (current(p) && current(p)->kind == TK_NEWLINE) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            advance(p);
            ChengNode *list = cheng_node_new(NK_STMT_LIST, kw ? kw->line : 0, kw ? kw->col : 0);
            while (current(p) && current(p)->kind != TK_DEDENT && current(p)->kind != TK_EOF) {
                ChengNode *decl = parse_var_decl_entry(p, kind, kw);
                if (decl) {
                    cheng_node_add(list, decl);
                }
                skip_stmt_seps(p);
            }
            if (current(p) && current(p)->kind == TK_DEDENT) {
                advance(p);
            }
            return list;
        }
    }
    return parse_var_decl_entry(p, kind, kw);
}

static ChengNode *parse_for_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *name = parse_ident(p, 1);
    ChengNode *pattern = cheng_node_new(NK_PATTERN, name ? name->line : 0, name ? name->col : 0);
    if (name) {
        cheng_node_add(pattern, name);
    }
    if (!(current(p) && current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "in"))) {
        cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, kw ? kw->line : 0, kw ? kw->col : 0, "for requires in");
    } else {
        advance(p);
    }
    ChengNode *iter = parse_expression(p, 0);
    expect_symbol(p, ":", "Expected ':'");
    ChengNode *body = parse_suite(p);
    ChengNode *node = cheng_node_new(NK_FOR, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(node, pattern);
    if (iter) {
        cheng_node_add(node, iter);
    } else {
        cheng_node_add(node, empty_node(kw));
    }
    cheng_node_add(node, body);
    return node;
}

static ChengNode *parse_continue_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *node = cheng_node_new(NK_CONTINUE, kw ? kw->line : 0, kw ? kw->col : 0);
    return node;
}

static ChengNode *parse_break_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *node = cheng_node_new(NK_BREAK, kw ? kw->line : 0, kw ? kw->col : 0);
    return node;
}

static ChengNode *parse_defer_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    expect_symbol(p, ":", "Expected ':'");
    ChengNode *body = parse_suite(p);
    ChengNode *node = cheng_node_new(NK_DEFER, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(node, body);
    return node;
}

static ChengNode *parse_pragma_stmt(ChengParser *p) {
    ChengToken *tok = current(p);
    advance(p);
    ChengNode *expr = parse_expression(p, 0);
    ChengNode *node = cheng_node_new(NK_PRAGMA, tok ? tok->line : 0, tok ? tok->col : 0);
    if (expr) {
        cheng_node_add(node, expr);
    }
    return node;
}

static ChengNode *parse_while_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *cond = parse_expression(p, 0);
    expect_symbol(p, ":", "Expected ':'");
    ChengNode *body = parse_suite(p);
    ChengNode *node = cheng_node_new(NK_WHILE, kw ? kw->line : 0, kw ? kw->col : 0);
    if (cond) {
        cheng_node_add(node, cond);
    } else {
        cheng_node_add(node, empty_node(kw));
    }
    cheng_node_add(node, body);
    return node;
}

static ChengNode *parse_case_stmt(ChengParser *p) {
    ChengToken *kw = current(p);
    advance(p);
    ChengNode *selector = parse_expression(p, 0);
    if (token_is_symbol(current(p), ":")) {
        advance(p);
    }
    ChengNode *node = cheng_node_new(NK_CASE, kw ? kw->line : 0, kw ? kw->col : 0);
    cheng_node_add(node, selector ? selector : empty_node(kw));
    int has_indent = 0;
    if (current(p) && current(p)->kind == TK_NEWLINE) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            has_indent = 1;
            advance(p);
        }
    }
    for (;;) {
        ChengToken *tok = current(p);
        if (!tok || tok->kind == TK_EOF) {
            break;
        }
        if (tok->kind == TK_DEDENT) {
            if (has_indent) {
                advance(p);
            }
            break;
        }
        if (tok->kind == TK_KEYWORD && token_is_keyword(tok, "of")) {
            ChengToken *of_kw = current(p);
            advance(p);
            ChengNode *pat = parse_expression(p, 0);
            if (match_symbol(p, ",")) {
                ChengNode *pat_list = cheng_node_new(NK_PATTERN, of_kw ? of_kw->line : 0, of_kw ? of_kw->col : 0);
                if (pat) {
                    cheng_node_add(pat_list, pat);
                }
                while (1) {
                    ChengNode *next = parse_expression(p, 0);
                    if (next) {
                        cheng_node_add(pat_list, next);
                    }
                    if (!match_symbol(p, ",")) {
                        break;
                    }
                }
                pat = pat_list;
            }
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *body = parse_suite(p);
            ChengNode *branch = cheng_node_new(NK_OF_BRANCH, of_kw ? of_kw->line : 0, of_kw ? of_kw->col : 0);
            cheng_node_add(branch, pat ? pat : empty_node(of_kw));
            cheng_node_add(branch, body ? body : empty_node(of_kw));
            cheng_node_add(node, branch);
        } else if (current(p)->kind == TK_KEYWORD && token_is_keyword(current(p), "else")) {
            ChengToken *else_kw = current(p);
            advance(p);
            expect_symbol(p, ":", "Expected ':'");
            ChengNode *body = parse_suite(p);
            ChengNode *else_node = cheng_node_new(NK_ELSE, else_kw ? else_kw->line : 0, else_kw ? else_kw->col : 0);
            cheng_node_add(else_node, body ? body : empty_node(else_kw));
            cheng_node_add(node, else_node);
        } else {
            if (!has_indent) {
                break;
            }
            advance(p);
        }
        skip_stmt_seps(p);
    }
    return node;
}

static ChengNode *parse_statement(ChengParser *p) {
    skip_stmt_seps(p);
    ChengToken *tok = current(p);
    if (!tok || tok->kind == TK_EOF) {
        return NULL;
    }
    if (tok->kind == TK_KEYWORD) {
        if (token_is_keyword(tok, "async")) {
            ChengToken *async_kw = current(p);
            advance(p);
            ChengToken *next = current(p);
            if (next && next->kind == TK_KEYWORD && token_is_keyword(next, "fn")) {
                ChengNode *decl = parse_fn_decl(p);
                attach_async_pragma(decl, async_kw);
                return decl;
            }
            if (next && next->kind == TK_KEYWORD && token_is_keyword(next, "iterator")) {
                ChengNode *decl = parse_fn_decl_kind(p, NK_ITERATOR_DECL);
                attach_async_pragma(decl, async_kw);
                return decl;
            }
            if (p->diags && async_kw) {
                cheng_diag_add(p->diags, CHENG_SV_ERROR, p->filename, async_kw->line, async_kw->col,
                               "async can only modify fn/iterator");
            }
            return empty_node(async_kw);
        }
        if (token_is_keyword(tok, "import")) {
            return parse_import_stmt(p);
        }
        if (token_is_keyword(tok, "fn")) {
            return parse_fn_decl(p);
        }
        if (token_is_keyword(tok, "iterator")) {
            return parse_fn_decl_kind(p, NK_ITERATOR_DECL);
        }
        if (token_is_keyword(tok, "trait")) {
            return parse_block_decl(p, NK_TRAIT_DECL);
        }
        if (token_is_keyword(tok, "concept")) {
            return parse_block_decl(p, NK_CONCEPT_DECL);
        }
        if (token_is_keyword(tok, "template")) {
            return parse_template_decl(p);
        }
        if (token_is_keyword(tok, "macro")) {
            return parse_macro_decl(p);
        }
        if (token_is_keyword(tok, "type")) {
            return parse_type_stmt(p);
        }
        if (token_is_keyword(tok, "let")) {
            return parse_var_stmt(p, NK_LET);
        }
        if (token_is_keyword(tok, "var")) {
            return parse_var_stmt(p, NK_VAR);
        }
        if (token_is_keyword(tok, "const")) {
            return parse_var_stmt(p, NK_CONST);
        }
        if (token_is_keyword(tok, "if")) {
            return parse_if_stmt(p);
        }
        if (token_is_keyword(tok, "when")) {
            ChengNode *node = parse_if_stmt(p);
            if (node) {
                node->kind = NK_WHEN;
            }
            return node;
        }
        if (token_is_keyword(tok, "for")) {
            return parse_for_stmt(p);
        }
        if (token_is_keyword(tok, "case")) {
            return parse_case_stmt(p);
        }
        if (token_is_keyword(tok, "while")) {
            return parse_while_stmt(p);
        }
        if (token_is_keyword(tok, "break")) {
            return parse_break_stmt(p);
        }
        if (token_is_keyword(tok, "continue")) {
            return parse_continue_stmt(p);
        }
        if (token_is_keyword(tok, "defer")) {
            return parse_defer_stmt(p);
        }
        if (token_is_keyword(tok, "return")) {
            return parse_return_stmt(p);
        }
    }
    if (token_is_symbol(tok, "@")) {
        return parse_pragma_stmt(p);
    }
    ChengNode *expr = parse_expression(p, 0);
    if (!expr) {
        advance(p);
        return NULL;
    }
    if (match_symbol(p, "=")) {
        ChengNode *rhs = parse_expression(p, 0);
        ChengNode *asn = cheng_node_new(NK_ASGN, expr->line, expr->col);
        cheng_node_add(asn, expr);
        if (rhs) {
            cheng_node_add(asn, rhs);
        } else {
            cheng_node_add(asn, empty_node(tok));
        }
        return asn;
    }
    return expr;
}

static ChengNode *parse_suite(ChengParser *p) {
    ChengToken *tok = current(p);
    if (tok && tok->kind == TK_NEWLINE) {
        skip_newlines(p);
        if (current(p) && current(p)->kind == TK_INDENT) {
            advance(p);
            ChengNode *stmts = cheng_node_new(NK_STMT_LIST, tok->line, tok->col);
            while (current(p) && current(p)->kind != TK_DEDENT && current(p)->kind != TK_EOF) {
                ChengNode *stmt = parse_statement(p);
                if (stmt) {
                    cheng_node_add(stmts, stmt);
                } else {
                    advance(p);
                }
                skip_stmt_seps(p);
            }
            if (current(p) && current(p)->kind == TK_DEDENT) {
                advance(p);
            }
            return stmts;
        }
    }
    ChengNode *single = cheng_node_new(NK_STMT_LIST, tok ? tok->line : 0, tok ? tok->col : 0);
    ChengNode *stmt = parse_statement(p);
    if (stmt) {
        cheng_node_add(single, stmt);
    }
    return single;
}

ChengNode *cheng_parse_module(ChengParser *p) {
    ChengNode *module = cheng_node_new(NK_MODULE, 0, 0);
    ChengNode *stmts = cheng_node_new(NK_STMT_LIST, 0, 0);
    if (!module || !stmts) {
        cheng_node_free(module);
        cheng_node_free(stmts);
        return NULL;
    }
    cheng_node_add(module, stmts);
    while (p->pos < p->tokens->len) {
        ChengToken *tok = current(p);
        if (!tok || tok->kind == TK_EOF) {
            break;
        }
        ChengNode *stmt = parse_statement(p);
        if (stmt) {
            cheng_node_add(stmts, stmt);
        } else {
            advance(p);
        }
        skip_stmt_seps(p);
    }
    return module;
}

void cheng_parser_init(ChengParser *p,
                       ChengTokenList *tokens,
                       const char *filename,
                       ChengDiagList *diags) {
    p->tokens = tokens;
    p->pos = 0;
    p->filename = filename;
    p->diags = diags;
}

int cheng_parse_imports(ChengParser *p, ChengStrList *out) {
    ChengNode *root = cheng_parse_module(p);
    if (!root) {
        return -1;
    }
    cheng_collect_imports_from_ast(root, out);
    cheng_node_free(root);
    return 0;
}

static void collect_imports(const ChengNode *node, ChengStrList *out) {
    if (!node) {
        return;
    }
    if (node->kind == NK_IMPORT_GROUP && node->len > 0) {
        const ChengNode *base_node = node->kids[0];
        const char *base = NULL;
        if (base_node && (base_node->kind == NK_IDENT || base_node->kind == NK_STR_LIT)) {
            base = base_node->kind == NK_STR_LIT ? base_node->str_val : base_node->ident;
        }
        for (size_t i = 1; i < node->len; i++) {
            const ChengNode *entry = node->kids[i];
            if (!entry || (entry->kind != NK_IDENT && entry->kind != NK_STR_LIT)) {
                continue;
            }
            const char *name = entry->kind == NK_STR_LIT ? entry->str_val : entry->ident;
            if (!name || name[0] == '\0') {
                continue;
            }
            if (base && base[0] != '\0') {
                size_t base_len = strlen(base);
                size_t name_len = strlen(name);
                char *full = (char *)malloc(base_len + name_len + 2);
                if (full) {
                    memcpy(full, base, base_len);
                    full[base_len] = '/';
                    memcpy(full + base_len + 1, name, name_len);
                    full[base_len + 1 + name_len] = '\0';
                    cheng_strlist_push_unique(out, full);
                    free(full);
                }
            } else {
                cheng_strlist_push_unique(out, name);
            }
        }
        return;
    }
    if (node->kind == NK_IMPORT_STMT && node->len > 0) {
        const ChengNode *item = node->kids[0];
        const ChengNode *path = item;
        const ChengNode *alias = NULL;
        if (item && item->kind == NK_IMPORT_AS && item->len >= 2) {
            path = item->kids[0];
            alias = item->kids[1];
        }
        if (path && (path->kind == NK_IDENT || path->kind == NK_STR_LIT)) {
            const char *base = path->kind == NK_STR_LIT ? path->str_val : path->ident;
            if (alias && alias->ident && alias->ident[0] != '\0') {
                size_t base_len = base ? strlen(base) : 0;
                size_t alias_len = strlen(alias->ident);
                char *full = (char *)malloc(base_len + alias_len + 5);
                if (full) {
                    if (base) {
                        memcpy(full, base, base_len);
                    }
                    memcpy(full + base_len, " as ", 4);
                    memcpy(full + base_len + 4, alias->ident, alias_len);
                    full[base_len + 4 + alias_len] = '\0';
                    cheng_strlist_push_unique(out, full);
                    free(full);
                }
            } else if (base) {
                cheng_strlist_push_unique(out, base);
            }
        }
    }
    for (size_t i = 0; i < node->len; i++) {
        collect_imports(node->kids[i], out);
    }
}

int cheng_collect_imports_from_ast(const ChengNode *root, ChengStrList *out) {
    collect_imports(root, out);
    return 0;
}
