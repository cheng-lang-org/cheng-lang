#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diagnostics.h"
#include "lexer.h"
#include "token.h"

typedef struct {
    int *data;
    size_t len;
    size_t cap;
} IntStack;

typedef struct {
    const char *start;
    size_t len;
} ChengLine;

static void stack_init(IntStack *s) {
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

static void stack_free(IntStack *s) {
    free(s->data);
    s->data = NULL;
    s->len = 0;
    s->cap = 0;
}

static int stack_push(IntStack *s, int v) {
    if (s->len + 1 > s->cap) {
        size_t next = s->cap == 0 ? 8 : s->cap * 2;
        int *data = (int *)realloc(s->data, next * sizeof(int));
        if (!data) {
            return -1;
        }
        s->data = data;
        s->cap = next;
    }
    s->data[s->len++] = v;
    return 0;
}

static int stack_peek(const IntStack *s) {
    if (s->len == 0) {
        return 0;
    }
    return s->data[s->len - 1];
}

static void stack_pop(IntStack *s) {
    if (s->len > 0) {
        s->len -= 1;
    }
}

static int is_keyword(const char *start, size_t len) {
    static const char *keywords[] = {
        "module", "const", "let", "var", "type", "concept", "trait", "fn",
        "iterator", "macro", "template", "async", "mut",
        "if", "elif", "else", "for", "while", "break", "continue", "return",
        "yield", "defer", "await", "import", "as", "in", "when",
        "case", "of", "where", "true", "false", "nil", "block",
        "enum", "ref", "tuple", "set", "str",
        "div", "mod", "is", "notin",
        NULL
    };
    for (size_t i = 0; keywords[i] != NULL; i++) {
        if (strlen(keywords[i]) == len && memcmp(start, keywords[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_block_start_keyword(const char *start, size_t len) {
    static const char *words[] = {
        "if", "elif", "else", "for", "while", "case", "of", "when", "block", NULL
    };
    for (size_t i = 0; words[i] != NULL; i++) {
        if (strlen(words[i]) == len && memcmp(start, words[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_block_continue_keyword(const char *start, size_t len) {
    static const char *words[] = { "elif", "else", "of", NULL };
    for (size_t i = 0; words[i] != NULL; i++) {
        if (strlen(words[i]) == len && memcmp(start, words[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_block_expr_keyword(const char *start, size_t len) {
    static const char *words[] = { "if", "fn", "iterator", NULL };
    for (size_t i = 0; words[i] != NULL; i++) {
        if (strlen(words[i]) == len && memcmp(start, words[i], len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_def_keyword(const ChengToken *tok) {
    if (tok->kind != TK_KEYWORD) {
        return 0;
    }
    static const char *words[] = {
        "fn", "iterator", "macro", "template", "type", "concept", "trait", NULL
    };
    for (size_t i = 0; words[i] != NULL; i++) {
        if (strlen(words[i]) == tok->len && memcmp(tok->start, words[i], tok->len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_continuation_token(const ChengToken *tok) {
    if (tok->kind == TK_SYMBOL) {
        static const char *symbols[] = {
            "=", "+", "-", "/", "%", "&", "|", "^", ",", ".", "->", "..", "..<",
            "==", "!=", "<", ">", "<=", ">=", "<<", ">>", "&&", "||", NULL
        };
        for (size_t i = 0; symbols[i] != NULL; i++) {
            if (strlen(symbols[i]) == tok->len && memcmp(tok->start, symbols[i], tok->len) == 0) {
                return 1;
            }
        }
    }
    if (tok->kind == TK_KEYWORD) {
        static const char *words[] = { "in", "notin", "is", "div", "mod", NULL };
        for (size_t i = 0; words[i] != NULL; i++) {
            if (strlen(words[i]) == tok->len && memcmp(tok->start, words[i], tok->len) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int line_starts_continue_keyword(const ChengLine *line) {
    size_t pos = 0;
    while (pos < line->len && line->start[pos] == ' ') {
        pos++;
    }
    if (pos >= line->len || line->start[pos] == '#') {
        return 0;
    }
    if (!isalpha((unsigned char)line->start[pos]) && line->start[pos] != '_') {
        return 0;
    }
    size_t start = pos;
    while (pos < line->len && (isalnum((unsigned char)line->start[pos]) || line->start[pos] == '_')) {
        pos++;
    }
    return is_block_continue_keyword(line->start + start, pos - start);
}

static int next_line_starts_block_expr(const ChengLine *lines, size_t line_count, size_t start_idx) {
    size_t idx = start_idx;
    while (idx < line_count) {
        const ChengLine *line = &lines[idx];
        size_t pos = 0;
        while (pos < line->len && line->start[pos] == ' ') {
            pos++;
        }
        if (pos >= line->len || line->start[pos] == '#') {
            idx++;
            continue;
        }
        if (!isalpha((unsigned char)line->start[pos]) && line->start[pos] != '_') {
            return 0;
        }
        size_t start = pos;
        while (pos < line->len && (isalnum((unsigned char)line->start[pos]) || line->start[pos] == '_')) {
            pos++;
        }
        return is_block_expr_keyword(line->start + start, pos - start);
    }
    return 0;
}

static int emit_indent_tokens(ChengTokenList *tokens,
                              IntStack *stack,
                              int new_indent,
                              int line,
                              const char *filename,
                              ChengDiagList *diags) {
    int current = stack_peek(stack);
    if (new_indent > current) {
        if (stack_push(stack, new_indent) != 0) {
            return -1;
        }
        ChengToken tok = { TK_INDENT, "", 0, '\0', line, 1 };
        if (cheng_tokens_push(tokens, tok) != 0) {
            return -1;
        }
    } else if (new_indent < current) {
        while (stack->len > 0 && stack_peek(stack) > new_indent) {
            stack_pop(stack);
            ChengToken tok = { TK_DEDENT, "", 0, '\0', line, 1 };
            if (cheng_tokens_push(tokens, tok) != 0) {
                return -1;
            }
        }
    }
    if (stack->len == 0 || stack_peek(stack) != new_indent) {
        if (cheng_diag_add(diags, CHENG_SV_ERROR, filename, line, 1, "indentation level mismatch") != 0) {
            return -1;
        }
        if (stack_push(stack, new_indent) != 0) {
            return -1;
        }
        ChengToken tok = { TK_INDENT, "", 0, '\0', line, 1 };
        if (cheng_tokens_push(tokens, tok) != 0) {
            return -1;
        }
    }
    return 0;
}

static int push_token(ChengTokenList *tokens,
                      ChengTokenKind kind,
                      const char *start,
                      size_t len,
                      char prefix,
                      int line,
                      int col) {
    ChengToken tok;
    tok.kind = kind;
    tok.start = start;
    tok.len = len;
    tok.str_prefix = prefix;
    tok.line = line;
    tok.col = col;
    return cheng_tokens_push(tokens, tok);
}

static int lex_number(const ChengLine *line,
                      size_t start,
                      int line_num,
                      ChengTokenList *tokens) {
    size_t i = start;
    if (i + 1 < line->len && line->start[i] == '0' &&
        (line->start[i + 1] == 'x' || line->start[i + 1] == 'X' ||
         line->start[i + 1] == 'b' || line->start[i + 1] == 'B' ||
         line->start[i + 1] == 'o' || line->start[i + 1] == 'O')) {
        i += 2;
        while (i < line->len && (isalnum((unsigned char)line->start[i]) || line->start[i] == '_')) {
            i++;
        }
    } else {
        int has_dot = 0;
        while (i < line->len) {
            if (isdigit((unsigned char)line->start[i]) || line->start[i] == '_') {
                i++;
            } else if (line->start[i] == '.') {
                if (has_dot) {
                    break;
                }
                if (i + 1 < line->len && line->start[i + 1] == '.') {
                    break;
                }
                has_dot = 1;
                i++;
            } else {
                break;
            }
        }
    }
    if (push_token(tokens, TK_NUMBER, line->start + start, i - start, '\0', line_num, (int)start + 1) != 0) {
        return -1;
    }
    return (int)i;
}

static int lex_ident(const ChengLine *line, size_t start, int line_num, ChengTokenList *tokens) {
    size_t i = start;
    while (i < line->len &&
           (isalnum((unsigned char)line->start[i]) || line->start[i] == '_')) {
        i++;
    }
    ChengTokenKind kind = is_keyword(line->start + start, i - start) ? TK_KEYWORD : TK_IDENT;
    if (push_token(tokens, kind, line->start + start, i - start, '\0', line_num, (int)start + 1) != 0) {
        return -1;
    }
    return (int)i;
}

static int lex_char(const ChengLine *line,
                    size_t start,
                    int line_num,
                    const char *filename,
                    ChengTokenList *tokens,
                    ChengDiagList *diags) {
    size_t i = start + 1;
    while (i < line->len && line->start[i] != '\'') {
        if (line->start[i] == '\\' && i + 1 < line->len) {
            i++;
        }
        i++;
    }
    if (i >= line->len) {
        cheng_diag_add(diags, CHENG_SV_ERROR, filename, line_num, (int)start + 1, "unterminated character literal");
        if (push_token(tokens, TK_CHAR, line->start + start + 1, line->len - (start + 1), '\0', line_num, (int)start + 1) != 0) {
            return -1;
        }
        return (int)i;
    }
    size_t lex_len = i - (start + 1);
    if (lex_len != 1) {
        if (!(lex_len == 2 && line->start[start + 1] == '\\')) {
            if (!(lex_len == 4 && line->start[start + 1] == '\\' && line->start[start + 2] == 'x')) {
                cheng_diag_add(diags, CHENG_SV_ERROR, filename, line_num, (int)start + 1, "character literal must contain one character");
            }
        }
    }
    if (push_token(tokens, TK_CHAR, line->start + start + 1, lex_len, '\0', line_num, (int)start + 1) != 0) {
        return -1;
    }
    return (int)(i + 1);
}

static int lex_string(const ChengLine *line,
                      size_t start,
                      int line_num,
                      const char *filename,
                      ChengTokenList *tokens,
                      ChengDiagList *diags) {
    size_t i = start;
    char prefix = '\0';
    if ((line->start[i] == 'r' || line->start[i] == 'f') &&
        i + 1 < line->len && line->start[i + 1] == '"') {
        prefix = line->start[i];
        i++;
    }
    if (i + 2 < line->len &&
        line->start[i] == '"' && line->start[i + 1] == '"' && line->start[i + 2] == '"') {
        size_t content_start = i + 3;
        i += 3;
        while (i + 2 < line->len &&
               !(line->start[i] == '"' && line->start[i + 1] == '"' && line->start[i + 2] == '"')) {
            i++;
        }
        if (i + 2 >= line->len) {
            cheng_diag_add(diags, CHENG_SV_ERROR, filename, line_num, (int)start + 1, "unterminated string literal");
            if (push_token(tokens, TK_STRING, line->start + content_start, line->len - content_start, prefix, line_num, (int)start + 1) != 0) {
                return -1;
            }
            return (int)line->len;
        }
        if (push_token(tokens, TK_STRING, line->start + content_start, i - content_start, prefix, line_num, (int)start + 1) != 0) {
            return -1;
        }
        return (int)(i + 3);
    }
    if (line->start[i] == '"') {
        i++;
    }
    size_t content_start = i;
    while (i < line->len && line->start[i] != '"') {
        if (line->start[i] == '\\' && i + 1 < line->len) {
            i += 2;
            continue;
        }
        i++;
    }
    if (i >= line->len) {
        cheng_diag_add(diags, CHENG_SV_ERROR, filename, line_num, (int)start + 1, "unterminated string literal");
        if (push_token(tokens, TK_STRING, line->start + content_start, line->len - content_start, prefix, line_num, (int)start + 1) != 0) {
            return -1;
        }
        return (int)i;
    }
    if (push_token(tokens, TK_STRING, line->start + content_start, i - content_start, prefix, line_num, (int)start + 1) != 0) {
        return -1;
    }
    return (int)(i + 1);
}

static int lex_line(const ChengLine *line,
                    size_t line_idx,
                    size_t line_count,
                    const ChengLine *lines,
                    const char *filename,
                    ChengTokenList *tokens,
                    ChengDiagList *diags,
                    IntStack *indent_stack,
                    IntStack *paren_indent_stack,
                    IntStack *expr_indent_stack,
                    IntStack *type_indent_stack,
                    int *paren_depth,
                    int *line_continues,
                    int *def_continues,
                    int *paren_indent_active,
                    int *paren_indent_base,
                    int *paren_indent_paren_depth,
                    int *expr_indent_pending,
                    int *expr_indent_active,
                    int *expr_indent_base,
                    int *type_indent_pending,
                    int *type_indent_base) {
    int line_num = (int)line_idx + 1;
    size_t pos = 0;
    int space_count = 0;
    while (pos < line->len && line->start[pos] == ' ') {
        space_count++;
        pos++;
    }
    if (pos >= line->len) {
        return 0;
    }
    if (pos < line->len && line->start[pos] == '#') {
        return 0;
    }
    if (*expr_indent_active && space_count <= *expr_indent_base && !line_starts_continue_keyword(line)) {
        while (expr_indent_stack->len > 1) {
            stack_pop(expr_indent_stack);
            ChengToken tok = { TK_DEDENT, "", 0, '\0', line_num, 1 };
            if (cheng_tokens_push(tokens, tok) != 0) {
                return -1;
            }
        }
        *expr_indent_active = 0;
        *expr_indent_base = 0;
        expr_indent_stack->len = 1;
    }

    int paren_continuation =
        *paren_indent_active ? (*paren_depth > *paren_indent_paren_depth) : (*paren_depth > 0);
    int continuation = *line_continues || paren_continuation;
    int in_type_block = 0;
    if (!continuation) {
        while (type_indent_stack->len > 0 && space_count < stack_peek(type_indent_stack)) {
            stack_pop(type_indent_stack);
        }
        if (*type_indent_pending) {
            if (space_count > *type_indent_base) {
                if (stack_push(type_indent_stack, space_count) != 0) {
                    return -1;
                }
            }
            *type_indent_pending = 0;
        }
        if (type_indent_stack->len > 0 && space_count == stack_peek(type_indent_stack)) {
            in_type_block = 1;
        }
    }
    if (pos < line->len && line->start[pos] == '\t') {
        cheng_diag_add(diags, CHENG_SV_ERROR, filename, line_num, (int)pos + 1, "tab indentation is not allowed");
    }
    if (!continuation) {
        if (*expr_indent_pending) {
            *expr_indent_active = 1;
            *expr_indent_base = space_count;
            expr_indent_stack->len = 1;
            *expr_indent_pending = 0;
        } else if (*expr_indent_active) {
            int rel = space_count - *expr_indent_base;
            if (rel < 0) {
                rel = 0;
            }
            if (emit_indent_tokens(tokens, expr_indent_stack, rel, line_num, filename, diags) != 0) {
                return -1;
            }
        } else if (*paren_indent_active) {
            int rel = space_count - *paren_indent_base;
            if (rel < 0) {
                rel = 0;
            }
            if (emit_indent_tokens(tokens, paren_indent_stack, rel, line_num, filename, diags) != 0) {
                return -1;
            }
        } else {
            if (emit_indent_tokens(tokens, indent_stack, space_count, line_num, filename, diags) != 0) {
                return -1;
            }
        }
    }

    int line_has_tokens = 0;
    int line_has_def = 0;
    int line_starts_block = 0;
    int line_starts_continuation = 0;
    int line_token_count = 0;
    ChengToken line_first_tok;
    ChengToken last_tok;
    line_first_tok.kind = TK_EOF;
    line_first_tok.start = "";
    line_first_tok.len = 0;
    line_first_tok.line = line_num;
    line_first_tok.col = 0;
    last_tok = line_first_tok;

    while (pos < line->len) {
        char ch = line->start[pos];
        if (ch == ' ') {
            pos++;
            continue;
        }
        if (ch == '#') {
            break;
        }
        if (ch == '"' || ((ch == 'r' || ch == 'f') && pos + 1 < line->len && line->start[pos + 1] == '"')) {
            int next = lex_string(line, pos, line_num, filename, tokens, diags);
            if (next < 0) {
                return -1;
            }
            ChengToken tok = tokens->data[tokens->len - 1];
            if (line_token_count == 0) {
                line_first_tok = tok;
            }
            line_token_count++;
            pos = (size_t)next;
            if (!line_has_tokens && tok.kind == TK_KEYWORD) {
                if (is_block_start_keyword(tok.start, tok.len)) {
                    line_starts_block = 1;
                }
                if (is_block_continue_keyword(tok.start, tok.len)) {
                    line_starts_continuation = 1;
                }
            }
            last_tok = tok;
            line_has_tokens = 1;
            continue;
        }
        if (isalpha((unsigned char)ch) || ch == '_') {
            int next = lex_ident(line, pos, line_num, tokens);
            if (next < 0) {
                return -1;
            }
            ChengToken tok = tokens->data[tokens->len - 1];
            if (line_token_count == 0) {
                line_first_tok = tok;
            }
            line_token_count++;
            pos = (size_t)next;
            if (!line_has_tokens && tok.kind == TK_KEYWORD) {
                if (is_block_start_keyword(tok.start, tok.len)) {
                    line_starts_block = 1;
                }
                if (is_block_continue_keyword(tok.start, tok.len)) {
                    line_starts_continuation = 1;
                }
            }
            if (is_def_keyword(&tok)) {
                line_has_def = 1;
            }
            last_tok = tok;
            line_has_tokens = 1;
            continue;
        }
        if (isdigit((unsigned char)ch)) {
            int next = lex_number(line, pos, line_num, tokens);
            if (next < 0) {
                return -1;
            }
            ChengToken tok = tokens->data[tokens->len - 1];
            if (line_token_count == 0) {
                line_first_tok = tok;
            }
            line_token_count++;
            pos = (size_t)next;
            last_tok = tok;
            line_has_tokens = 1;
            continue;
        }
        if (ch == '\'') {
            int next = lex_char(line, pos, line_num, filename, tokens, diags);
            if (next < 0) {
                return -1;
            }
            ChengToken tok = tokens->data[tokens->len - 1];
            if (line_token_count == 0) {
                line_first_tok = tok;
            }
            line_token_count++;
            pos = (size_t)next;
            last_tok = tok;
            line_has_tokens = 1;
            continue;
        }
        if (ch == '`') {
            size_t i = pos + 1;
            while (i < line->len && line->start[i] != '`') {
                i++;
            }
            if (i < line->len) {
                if (push_token(tokens, TK_IDENT, line->start + pos + 1, i - (pos + 1), '\0', line_num, (int)pos + 1) != 0) {
                    return -1;
                }
                ChengToken tok = tokens->data[tokens->len - 1];
                if (line_token_count == 0) {
                    line_first_tok = tok;
                }
                line_token_count++;
                pos = i + 1;
                last_tok = tok;
                line_has_tokens = 1;
                continue;
            }
            cheng_diag_add(diags, CHENG_SV_ERROR, filename, line_num, (int)pos + 1, "unterminated backtick identifier");
            pos = i;
            continue;
        }

        static const char *symbols[] = {
        "=>", "->", "..<", "..", "==", "!=", "<=", ">=", "<<", ">>", "&&", "||",
            "(", ")", "[", "]", "{", "}", ",", ":", ";",
            ".", "=", "+", "-", "*", "/", "%", "<", ">", "&", "|", "^", "~", "!", "$", "?", "@",
            NULL
        };
        int matched = 0;
        const char *matched_sym = NULL;
        for (size_t i = 0; symbols[i] != NULL; i++) {
            size_t sym_len = strlen(symbols[i]);
            if (pos + sym_len <= line->len &&
                memcmp(line->start + pos, symbols[i], sym_len) == 0) {
                if (push_token(tokens, TK_SYMBOL, line->start + pos, sym_len, '\0', line_num, (int)pos + 1) != 0) {
                    return -1;
                }
                matched = 1;
                matched_sym = symbols[i];
                pos += sym_len;
                break;
            }
        }
        if (matched) {
            if (strcmp(matched_sym, "(") == 0 || strcmp(matched_sym, "[") == 0 || strcmp(matched_sym, "{") == 0) {
                *paren_depth += 1;
            } else if (strcmp(matched_sym, ")") == 0 || strcmp(matched_sym, "]") == 0 || strcmp(matched_sym, "}") == 0) {
                if (*paren_depth > 0) {
                    *paren_depth -= 1;
                }
            }
            last_tok = tokens->data[tokens->len - 1];
            line_has_tokens = 1;
            line_token_count++;
            continue;
        }
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "unrecognized character '%c'", ch);
            cheng_diag_add(diags, CHENG_SV_ERROR, filename, line_num, (int)pos + 1, msg);
        }
        pos++;
    }

    if (line_has_tokens) {
        if (in_type_block) {
            line_has_def = 1;
        }
        int def_active = line_has_def || *def_continues;
        int starts_block_expr =
            last_tok.kind == TK_SYMBOL &&
            last_tok.len == 1 && last_tok.start[0] == '=' &&
            !def_active &&
            next_line_starts_block_expr(lines, line_count, line_idx + 1);
        int opens_block =
            (last_tok.kind == TK_SYMBOL && last_tok.len == 1 && last_tok.start[0] == ':' && line_starts_block) ||
            (last_tok.kind == TK_SYMBOL && last_tok.len == 1 && last_tok.start[0] == '=' && def_active) ||
            starts_block_expr;
        int paren_continuation_now =
            *paren_indent_active ? (*paren_depth > *paren_indent_paren_depth) : (*paren_depth > 0);
        int line_continues_now = paren_continuation_now || is_continuation_token(&last_tok);
        if (def_active && last_tok.kind == TK_SYMBOL && last_tok.len == 1 && last_tok.start[0] == '=') {
            line_continues_now = 0;
        }
        if (opens_block) {
            line_continues_now = 0;
        }
        if (!line_continues_now) {
            ChengToken tok = { TK_NEWLINE, "", 0, '\0', line_num, (int)line->len + 1 };
            if (cheng_tokens_push(tokens, tok) != 0) {
                return -1;
            }
        }
        *line_continues = line_continues_now;
        if (line_token_count == 1 &&
            line_first_tok.kind == TK_KEYWORD &&
            line_first_tok.len == 4 &&
            memcmp(line_first_tok.start, "type", 4) == 0 &&
            !line_continues_now) {
            *type_indent_pending = 1;
            *type_indent_base = space_count;
        }
        *def_continues = def_active && line_continues_now;
        if (!*paren_indent_active && *paren_depth > 0 && opens_block) {
            *paren_indent_active = 1;
            *paren_indent_base = space_count;
            *paren_indent_paren_depth = *paren_depth;
            paren_indent_stack->len = 1;
        }
        if (starts_block_expr) {
            *expr_indent_pending = 1;
        }
        if (*paren_indent_active && *paren_depth <= *paren_indent_paren_depth) {
            int rel_indent = space_count - *paren_indent_base;
            if (rel_indent <= 0 && !line_starts_continuation && !opens_block) {
                *paren_indent_active = 0;
                *paren_indent_base = 0;
                *paren_indent_paren_depth = 0;
                paren_indent_stack->len = 1;
            }
        }
    }
    return 0;
}

static int build_lines(const char *content, size_t len, ChengLine **out_lines, size_t *out_count) {
    size_t cap = 64;
    size_t count = 0;
    ChengLine *lines = (ChengLine *)malloc(cap * sizeof(ChengLine));
    if (!lines) {
        return -1;
    }
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || content[i] == '\n') {
            size_t line_len = i - start;
            if (line_len > 0 && content[start + line_len - 1] == '\r') {
                line_len -= 1;
            }
            if (count + 1 > cap) {
                cap *= 2;
                ChengLine *next = (ChengLine *)realloc(lines, cap * sizeof(ChengLine));
                if (!next) {
                    free(lines);
                    return -1;
                }
                lines = next;
            }
            lines[count].start = content + start;
            lines[count].len = line_len;
            count++;
            start = i + 1;
        }
    }
    *out_lines = lines;
    *out_count = count;
    return 0;
}

static int cheng_lex_lines(const ChengLine *lines,
                           size_t line_count,
                           const char *filename,
                           ChengTokenList *tokens,
                           ChengDiagList *diags) {
    IntStack indent_stack;
    IntStack paren_indent_stack;
    IntStack expr_indent_stack;
    IntStack type_indent_stack;
    stack_init(&indent_stack);
    stack_init(&paren_indent_stack);
    stack_init(&expr_indent_stack);
    stack_init(&type_indent_stack);
    stack_push(&indent_stack, 0);
    stack_push(&paren_indent_stack, 0);
    stack_push(&expr_indent_stack, 0);
    int paren_depth = 0;
    int line_continues = 0;
    int def_continues = 0;
    int paren_indent_active = 0;
    int paren_indent_base = 0;
    int paren_indent_paren_depth = 0;
    int expr_indent_pending = 0;
    int expr_indent_active = 0;
    int expr_indent_base = 0;
    int type_indent_pending = 0;
    int type_indent_base = 0;

    for (size_t i = 0; i < line_count; i++) {
        if (lex_line(&lines[i],
                     i,
                     line_count,
                     lines,
                     filename,
                     tokens,
                     diags,
                     &indent_stack,
                     &paren_indent_stack,
                     &expr_indent_stack,
                     &type_indent_stack,
                     &paren_depth,
                     &line_continues,
                     &def_continues,
                     &paren_indent_active,
                     &paren_indent_base,
                     &paren_indent_paren_depth,
                     &expr_indent_pending,
                     &expr_indent_active,
                     &expr_indent_base,
                     &type_indent_pending,
                     &type_indent_base) != 0) {
            stack_free(&indent_stack);
            stack_free(&paren_indent_stack);
            stack_free(&expr_indent_stack);
            stack_free(&type_indent_stack);
            return -1;
        }
    }
    while (expr_indent_stack.len > 1) {
        stack_pop(&expr_indent_stack);
        ChengToken tok = { TK_DEDENT, "", 0, '\0', (int)line_count + 1, 1 };
        cheng_tokens_push(tokens, tok);
    }
    while (paren_indent_stack.len > 1) {
        stack_pop(&paren_indent_stack);
        ChengToken tok = { TK_DEDENT, "", 0, '\0', (int)line_count + 1, 1 };
        cheng_tokens_push(tokens, tok);
    }
    while (indent_stack.len > 1) {
        stack_pop(&indent_stack);
        ChengToken tok = { TK_DEDENT, "", 0, '\0', (int)line_count + 1, 1 };
        cheng_tokens_push(tokens, tok);
    }
    {
        ChengToken tok = { TK_EOF, "", 0, '\0', (int)line_count + 1, 1 };
        cheng_tokens_push(tokens, tok);
    }
    stack_free(&indent_stack);
    stack_free(&paren_indent_stack);
    stack_free(&expr_indent_stack);
    stack_free(&type_indent_stack);
    return 0;
}

static int read_file(const char *path, char **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    *out_buf = buf;
    *out_len = read;
    return 0;
}

int cheng_lex_file(const char *path, ChengLexed *out) {
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    cheng_tokens_init(&out->tokens);
    cheng_diags_init(&out->diags);
    if (read_file(path, &out->content, &out->len) != 0) {
        cheng_diag_add(&out->diags, CHENG_SV_ERROR, path, 1, 1, "failed to read file");
        return -1;
    }
    ChengLine *lines = NULL;
    size_t line_count = 0;
    if (build_lines(out->content, out->len, &lines, &line_count) != 0) {
        free(out->content);
        out->content = NULL;
        return -1;
    }
    int rc = cheng_lex_lines(lines, line_count, path, &out->tokens, &out->diags);
    free(lines);
    return rc;
}

int cheng_lex_string(const char *content, const char *filename, ChengLexed *out) {
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    cheng_tokens_init(&out->tokens);
    cheng_diags_init(&out->diags);
    size_t len = content ? strlen(content) : 0;
    out->content = (char *)malloc(len + 1);
    if (!out->content) {
        return -1;
    }
    if (content && len > 0) {
        memcpy(out->content, content, len);
    }
    out->content[len] = '\0';
    out->len = len;
    ChengLine *lines = NULL;
    size_t line_count = 0;
    if (build_lines(out->content, out->len, &lines, &line_count) != 0) {
        free(out->content);
        out->content = NULL;
        return -1;
    }
    int rc = cheng_lex_lines(lines, line_count, filename ? filename : "stdin", &out->tokens, &out->diags);
    free(lines);
    return rc;
}

void cheng_lexed_free(ChengLexed *out) {
    if (!out) {
        return;
    }
    free(out->content);
    out->content = NULL;
    out->len = 0;
    cheng_tokens_free(&out->tokens);
    cheng_diags_free(&out->diags);
}
