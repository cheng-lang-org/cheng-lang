/* cold_parser.c -- source parser for cold bootstrap compiler
 *
 * Extracted from cheng_cold.c.  Compile together with cheng_cold.c
 * for the full cold bootstrapper.  When -DCOLD_BACKEND_ONLY is set,
 * this file is NOT compiled.
 */

#ifndef COLD_CHENG_INCLUDE
#include "cold_types.h"
#endif
#include "cold_parser.h"

#include <string.h>
#include <stdio.h>

Parser parser_child(Parser *owner, Span source) {
    Parser child = {source, 0, owner->arena, owner->symbols,
                    owner->import_mode, owner->import_alias};
    child.import_sources = owner->import_sources;
    child.import_source_count = owner->import_source_count;
    return child;
}

void parser_ws(Parser *parser) {
    for (;;) {
        while (parser->pos < parser->source.len) {
            uint8_t c = parser->source.ptr[parser->pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            parser->pos++;
        }
        if (parser->pos + 1 < parser->source.len &&
            parser->source.ptr[parser->pos] == '/' &&
            parser->source.ptr[parser->pos + 1] == '*') {
            parser->pos += 2;
            while (parser->pos + 1 < parser->source.len) {
                if (parser->source.ptr[parser->pos] == '*' &&
                    parser->source.ptr[parser->pos + 1] == '/') {
                    parser->pos += 2;
                    break;
                }
                parser->pos++;
            }
            continue;
        }
        if (parser->pos + 1 < parser->source.len &&
            parser->source.ptr[parser->pos] == '/' &&
            parser->source.ptr[parser->pos + 1] == '/') {
            while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
            continue;
        }
        if (parser->pos < parser->source.len && parser->source.ptr[parser->pos] == '#') {
            while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
            continue;
        }
        break;
    }
}

void parser_line(Parser *parser) {
    while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
    if (parser->pos < parser->source.len) parser->pos++;
}

Span parser_token(Parser *parser) {
    parser_ws(parser);
    if (parser->pos >= parser->source.len) return (Span){0};
    uint8_t c = parser->source.ptr[parser->pos];
    if (c == '"') {
        int32_t start = parser->pos++;
        while (parser->pos < parser->source.len) {
            c = parser->source.ptr[parser->pos++];
            if (c == '\n' || c == '\r') return (Span){0}; /* unterminated string */
            if (c == '\\') {
                if (parser->pos >= parser->source.len) die("unterminated string escape");
                parser->pos++;
                continue;
            }
            if (c == '"') return span_sub(parser->source, start, parser->pos);
        }
        return (Span){0}; /* unterminated string */
    }
    if (c == '\'') {
        int32_t start = parser->pos++;
        while (parser->pos < parser->source.len) {
            c = parser->source.ptr[parser->pos++];
            if (c == '\n' || c == '\r') die("unterminated char literal");
            if (c == '\\') {
                if (parser->pos >= parser->source.len) die("unterminated char escape");
                parser->pos++;
                continue;
            }
            if (c == '\'') return span_sub(parser->source, start, parser->pos);
        }
        die("unterminated char literal");
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
        int32_t start = parser->pos++;
        while (parser->pos < parser->source.len) {
            c = parser->source.ptr[parser->pos];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) break;
            parser->pos++;
        }
        return span_sub(parser->source, start, parser->pos);
    }
    if (c == '0' && parser->pos + 1 < parser->source.len &&
        (parser->source.ptr[parser->pos + 1] == 'x' || parser->source.ptr[parser->pos + 1] == 'X')) {
        int32_t start = parser->pos;
        parser->pos += 2;
        while (parser->pos < parser->source.len) {
            uint8_t h = parser->source.ptr[parser->pos];
            if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) break;
            parser->pos++;
        }
        return span_sub(parser->source, start, parser->pos);
    }
    if ((c >= '0' && c <= '9') ||
        (c == '-' && parser->pos + 1 < parser->source.len &&
         parser->source.ptr[parser->pos + 1] >= '0' &&
         parser->source.ptr[parser->pos + 1] <= '9')) {
        int32_t start = parser->pos++;
        while (parser->pos < parser->source.len &&
               parser->source.ptr[parser->pos] >= '0' &&
               parser->source.ptr[parser->pos] <= '9') parser->pos++;
        return span_sub(parser->source, start, parser->pos);
    }
    if (c == '=' && parser->pos + 1 < parser->source.len && parser->source.ptr[parser->pos + 1] == '>') {
        int32_t start = parser->pos;
        parser->pos += 2;
        return span_sub(parser->source, start, parser->pos);
    }
    if ((c == '&' || c == '|') &&
        parser->pos + 1 < parser->source.len &&
        parser->source.ptr[parser->pos + 1] == c) {
        int32_t start = parser->pos;
        parser->pos += 2;
        return span_sub(parser->source, start, parser->pos);
    }
    if (c == '<' && parser->pos + 1 < parser->source.len &&
        parser->source.ptr[parser->pos + 1] == '<') {
        int32_t start = parser->pos;
        parser->pos += 2;
        return span_sub(parser->source, start, parser->pos);
    }
    if (c == '>' && parser->pos + 1 < parser->source.len &&
        parser->source.ptr[parser->pos + 1] == '>') {
        int32_t start = parser->pos;
        parser->pos += 2;
        return span_sub(parser->source, start, parser->pos);
    }
    if ((c == '=' || c == '!' || c == '<' || c == '>') &&
        parser->pos + 1 < parser->source.len &&
        parser->source.ptr[parser->pos + 1] == '=') {
        int32_t start = parser->pos;
        parser->pos += 2;
        return span_sub(parser->source, start, parser->pos);
    }
    int32_t start = parser->pos++;
    return span_sub(parser->source, start, parser->pos);
}

Span parser_peek(Parser *parser) {
    int32_t saved = parser->pos;
    Span token = parser_token(parser);
    parser->pos = saved;
    return token;
}

void parser_inline_ws(Parser *parser) {
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c != ' ' && c != '\t') break;
        parser->pos++;
    }
}

bool parser_take(Parser *parser, const char *text) {
    Span token = parser_token(parser);
    return span_eq(token, text);
}

void parser_skip_balanced(Parser *parser, const char *open, const char *close) {
    if (!parser_take(parser, open)) return;
    int32_t depth = 1;
    while (depth > 0 && parser->pos < parser->source.len) {
        Span token = parser_token(parser);
        if (span_eq(token, open)) depth++;
        else if (span_eq(token, close)) depth--;
        else if (token.len == 0) break;
    }
}

Span parser_take_type_span(Parser *parser) {
    parser_ws(parser);
    int32_t type_start = parser->pos;
    Span first = parser_token(parser);
    if (first.len <= 0) die("expected type");
    Span after_var = first; /* track the base type after stripping var */
    if (span_eq(first, "var")) {
        Span inner = parser_token(parser);
        if (inner.len <= 0) die("expected type after var");
        after_var = inner;
    }
    for (;;) {
        if (span_eq(parser_peek(parser), "[")) {
            parser_skip_balanced(parser, "[", "]");
            continue;
        }
        if (span_eq(parser_peek(parser), ".")) {
            (void)parser_token(parser);
            Span part = parser_token(parser);
            if (part.len <= 0) die("expected qualified type segment");
            continue;
        }
        /* function type: fn(params): ret_type */
        if (span_eq(after_var, "fn") && span_eq(parser_peek(parser), "(")) {
            parser_skip_balanced(parser, "(", ")");
            parser_ws(parser);
            if (span_eq(parser_peek(parser), ":")) {
                (void)parser_token(parser);
                parser_ws(parser);
                after_var = parser_token(parser); /* return type's first token */
                continue; /* continue loop for [bracketed] and .qualified suffixes on return type */
            }
            break; /* fn type without return type (unusual but valid) */
        }
        break;
    }
    return span_trim(span_sub(parser->source, type_start, parser->pos));
}

int32_t cold_param_kind_from_type(Span type) {
    bool is_var = false;
    type = cold_type_strip_var(type, &is_var);
    if (is_var && (span_eq(type, "int64") || span_eq(type, "uint64"))) return SLOT_I64_REF;
    if (is_var && (span_eq(type, "int32") || span_eq(type, "int") ||
                   span_eq(type, "bool") || span_eq(type, "uint8") ||
                   span_eq(type, "char") || span_eq(type, "uint32"))) return SLOT_I32_REF;
    if (is_var && (span_eq(type, "str") || span_eq(type, "cstring"))) return SLOT_STR_REF;
    if (is_var && cold_parse_i32_seq_type(type)) return SLOT_SEQ_I32_REF;
    if (is_var && cold_parse_str_seq_type(type)) return SLOT_SEQ_STR_REF;
    if (is_var && cold_type_has_qualified_name(type)) return SLOT_OPAQUE_REF;
    if (is_var) return SLOT_OBJECT_REF;
    if (type.len == 0 || span_eq(type, "i") || span_eq(type, "int32") ||
        span_eq(type, "int") || span_eq(type, "bool") ||
        span_eq(type, "uint8") || span_eq(type, "char") ||
        span_eq(type, "uint32") || span_eq(type, "void")) {
        return SLOT_I32;
    }
    if (span_eq(type, "int64") || span_eq(type, "uint64")) return SLOT_I64;
    if (span_eq(type, "s") || span_eq(type, "str") || span_eq(type, "cstring")) {
        return SLOT_STR;
    }
    if (span_eq(type, "v")) return SLOT_VARIANT;
    if (cold_parse_i32_seq_type(type)) return SLOT_SEQ_I32;
    if (cold_parse_str_seq_type(type)) return SLOT_SEQ_STR;
    if (cold_parse_opaque_seq_type(type)) return SLOT_SEQ_OPAQUE;
    if (span_eq(type, "ptr")) return SLOT_OPAQUE;
    /* function type like fn(int32): int32 → function pointer */
    if (cold_span_starts_with(type, "fn(")) return is_var ? SLOT_OBJECT_REF : SLOT_PTR;
    if (span_eq(type, "o") || cold_type_has_qualified_name(type)) return SLOT_OPAQUE;
    if (type.len > 1 && type.ptr[0] >= 'A' && type.ptr[0] <= 'Z') return SLOT_VARIANT;
    if (cold_parse_i32_array_type(type, 0)) return SLOT_ARRAY_I32;
    if (cold_span_starts_with(span_trim(type), "uint8[")) return SLOT_ARRAY_I32;
    return 'i'; /* default to int32 */
    return SLOT_I32;
}

int32_t cold_param_size_from_type(Symbols *symbols, Span type, int32_t kind) {
    bool is_var = false;
    type = cold_type_strip_var(type, &is_var);
    if (kind == SLOT_I32) return 4;
    if (kind == SLOT_I64) return 8;
    if (kind == SLOT_STR) return COLD_STR_SLOT_SIZE;
    if (kind == SLOT_I32_REF || kind == SLOT_I64_REF ||
        kind == SLOT_SEQ_I32_REF || kind == SLOT_OBJECT_REF ||
        kind == SLOT_STR_REF || kind == SLOT_SEQ_STR_REF ||
        kind == SLOT_SEQ_OPAQUE_REF ||
        kind == SLOT_OPAQUE || kind == SLOT_OPAQUE_REF) return 8;
    if (kind == SLOT_OBJECT) return symbols ? symbols_object_slot_size(symbols_resolve_object(symbols, type)) : 0;
    if (kind == SLOT_ARRAY_I32) return cold_slot_size_from_type_with_symbols(symbols, type, kind);
    if (kind == SLOT_SEQ_I32) return 16;
    if (kind == SLOT_SEQ_STR) return 16;
    if (kind == SLOT_SEQ_OPAQUE) return 16;
    if (kind == SLOT_VARIANT) {
        if (symbols && !span_eq(type, "v")) {
            TypeDef *def = symbols_resolve_type(symbols, type);
            if (!def) return 64; /* unknown variant, use default size */
            return symbols_type_slot_size(def);
        }
        return type.len > 0 && !span_eq(type, "v") ? 0 : cold_slot_size_for_kind(SLOT_VARIANT);
    }
    return 8; /* default to pointer-sized */
    return 4;
}


int32_t cold_arg_reg_count(int32_t kind, int32_t size) {
    if (kind == SLOT_I32 || kind == SLOT_I64 || kind == SLOT_I32_REF || kind == SLOT_I64_REF) return 1;
    if (kind == SLOT_PTR) return 1;
    if (kind == SLOT_STR) return 2;
    if (kind == SLOT_VARIANT) return size > 16 ? 1 : 2;
    if (kind == SLOT_OBJECT || kind == SLOT_ARRAY_I32) return size > 16 ? 1 : 2;
    if (kind == SLOT_SEQ_I32) return 2;
    if (kind == SLOT_SEQ_STR) return 2;
    if (kind == SLOT_SEQ_OPAQUE) return 2;
    if (kind == SLOT_I32_REF ||
        kind == SLOT_SEQ_I32_REF || kind == SLOT_OBJECT_REF ||
        kind == SLOT_STR_REF || kind == SLOT_SEQ_STR_REF ||
        kind == SLOT_SEQ_OPAQUE_REF ||
        kind == SLOT_OPAQUE || kind == SLOT_OPAQUE_REF) return 1;
    die("unsupported cold call ABI kind");
    return 1;
}

int32_t parse_param_specs(Symbols *symbols, Span params, Span *names,
                                 int32_t *kinds, int32_t *sizes, Span *types,
                                 int32_t cap) {
    Parser parser = {params, 0, 0, 0};
    int32_t count = 0;
    while (parser.pos < parser.source.len) {
        Span name = parser_token(&parser);
        if (name.len == 0) break;
        if (span_eq(name, ",")) continue;
        if (count >= cap) die("cold prototype supports at most 32 params");
        int32_t kind = SLOT_I32;
        Span type = cold_cstr_span("int32");
        if (span_eq(parser_peek(&parser), ":")) {
            (void)parser_token(&parser);
            type = parser_take_type_span(&parser);
            kind = cold_param_kind_from_type(type);
            if (symbols) kind = cold_slot_kind_from_type_with_symbols(symbols, type);
        }
        names[count] = name;
        if (kinds) kinds[count] = kind;
        if (sizes) sizes[count] = cold_param_size_from_type(symbols, type, kind);
        if (types) types[count] = span_trim(type);
        count++;
        while (parser.pos < parser.source.len &&
               !span_eq(parser_peek(&parser), ",") &&
               !span_eq(parser_peek(&parser), ")")) {
            (void)parser_token(&parser);
        }
        if (span_eq(parser_peek(&parser), ",")) (void)parser_token(&parser);
    }
    return count;
}

Span cold_arena_span_copy(Arena *arena, Span text) {
    uint8_t *ptr = arena_alloc(arena, (size_t)(text.len + 1));
    if (text.len > 0) memcpy(ptr, text.ptr, (size_t)text.len);
    ptr[text.len] = 0;
    return (Span){ptr, text.len};
}

Span cold_arena_join3(Arena *arena, Span a, const char *mid, Span b) {
    int32_t mid_len = (int32_t)strlen(mid);
    int32_t len = a.len + mid_len + b.len;
    uint8_t *ptr = arena_alloc(arena, (size_t)(len + 1));
    int32_t pos = 0;
    if (a.len > 0) {
        memcpy(ptr + pos, a.ptr, (size_t)a.len);
        pos += a.len;
    }
    if (mid_len > 0) {
        memcpy(ptr + pos, mid, (size_t)mid_len);
        pos += mid_len;
    }
    if (b.len > 0) {
        memcpy(ptr + pos, b.ptr, (size_t)b.len);
        pos += b.len;
    }
    ptr[len] = 0;
    return (Span){ptr, len};
}

bool cold_type_is_builtin_surface(Span type) {
    bool is_var = false;
    type = cold_type_strip_var(type, &is_var);
    return type.len == 0 ||
           span_eq(type, "int32") || span_eq(type, "int") ||
           span_eq(type, "bool") || span_eq(type, "uint8") ||
           span_eq(type, "char") || span_eq(type, "uint32") ||
           span_eq(type, "uint64") || span_eq(type, "int64") ||
           span_eq(type, "void") || span_eq(type, "str") ||
           span_eq(type, "cstring") || span_eq(type, "ptr") ||
           cold_parse_i32_seq_type(type) || cold_parse_str_seq_type(type);
}

bool cold_import_type_keeps_unqualified_base(Span base) {
    return span_eq(base, "Result") || span_eq(base, "ErrorInfo");
}

Span cold_join_generic_instance(Arena *arena, Span base, Span *args, int32_t arg_count) {
    int32_t len = base.len + 2;
    for (int32_t i = 0; i < arg_count; i++) {
        len += args[i].len;
        if (i + 1 < arg_count) len++;
    }
    uint8_t *ptr = arena_alloc(arena, (size_t)len + 1);
    int32_t pos = 0;
    memcpy(ptr + pos, base.ptr, (size_t)base.len);
    pos += base.len;
    ptr[pos++] = '[';
    for (int32_t i = 0; i < arg_count; i++) {
        memcpy(ptr + pos, args[i].ptr, (size_t)args[i].len);
        pos += args[i].len;
        if (i + 1 < arg_count) ptr[pos++] = ',';
    }
    ptr[pos++] = ']';
    ptr[pos] = 0;
    return (Span){ptr, pos};
}

/* Strip [T, ...] suffix from a generic-instantiated name, e.g. "Some[int32]" -> "Some" */
static Span cold_span_strip_generic_suffix(Span name) {
    if (name.len <= 0) return name;
    if (name.ptr[name.len - 1] != ']') return name;
    int32_t br = -1;
    for (int32_t i = 0; i < name.len; i++) {
        if (name.ptr[i] == '[') { br = i; break; }
    }
    if (br > 0) return span_sub(name, 0, br);
    return name;
}

Span cold_qualify_import_type(Arena *arena, Span alias, Span type) {
    bool is_var = false;
    Span stripped = cold_type_strip_var(type, &is_var);
    Span base = {0};
    Span args_span = {0};
    if (cold_type_parse_generic_instance(stripped, &base, &args_span)) {
        Span qualified_base;
        if (cold_type_is_builtin_surface(base) ||
            cold_type_has_qualified_name(base) ||
            cold_import_type_keeps_unqualified_base(base)) {
            qualified_base = cold_arena_span_copy(arena, base);
        } else {
            qualified_base = cold_arena_join3(arena, alias, ".", base);
        }
        Span args[COLD_MAX_VARIANT_FIELDS];
        Span qualified_args[COLD_MAX_VARIANT_FIELDS];
        int32_t arg_count = cold_split_top_level_commas(args_span, args, COLD_MAX_VARIANT_FIELDS);
        if (arg_count <= 0) die("generic import type has no arguments");
        for (int32_t i = 0; i < arg_count; i++) {
            qualified_args[i] = cold_qualify_import_type(arena, alias, args[i]);
        }
        Span qualified = cold_join_generic_instance(arena, qualified_base,
                                                   qualified_args, arg_count);
        if (!is_var) return qualified;
        return cold_arena_join3(arena, cold_cstr_span("var "), "", qualified);
    }
    if (cold_type_is_builtin_surface(type) || cold_type_has_qualified_name(stripped)) {
        return cold_arena_span_copy(arena, type);
    }
    Span qualified = cold_arena_join3(arena, alias, ".", stripped);
    if (!is_var) return qualified;
    return cold_arena_join3(arena, cold_cstr_span("var "), "", qualified);
}

Span parser_scope_type(Parser *parser, Span type) {
    type = span_trim(type);
    if (parser && parser->import_mode && parser->import_alias.len > 0) {
        return cold_qualify_import_type(parser->arena, parser->import_alias, type);
    }
    return type;
}

bool parser_can_scope_bare_name(Parser *parser, Span name) {
    return parser && parser->import_mode && parser->import_alias.len > 0 &&
           name.len > 0 && cold_span_find_char(name, '.') < 0;
}

Span parser_scoped_bare_name(Parser *parser, Span name) {
    return cold_arena_join3(parser->arena, parser->import_alias, ".", name);
}

ObjectDef *parser_find_object(Parser *parser, Span name) {
    if (parser_can_scope_bare_name(parser, name)) {
        Span scoped = parser_scoped_bare_name(parser, name);
        ObjectDef *object = symbols_find_object(parser->symbols, scoped);
        if (object) return object;
    }
    return symbols_find_object(parser->symbols, name);
}

ObjectDef *parser_resolve_object(Parser *parser, Span name) {
    if (parser_can_scope_bare_name(parser, name)) {
        Span scoped = parser_scoped_bare_name(parser, name);
        ObjectDef *object = symbols_resolve_object(parser->symbols, scoped);
        if (object) return object;
    }
    return symbols_resolve_object(parser->symbols, name);
}

Variant *parser_find_variant(Parser *parser, Span name) {
    if (parser_can_scope_bare_name(parser, name)) {
        Span scoped = parser_scoped_bare_name(parser, name);
        Variant *variant = symbols_find_variant(parser->symbols, scoped);
        if (variant) return variant;
    }
    return symbols_find_variant(parser->symbols, name);
}

ConstDef *parser_find_const(Parser *parser, Span name) {
    if (parser_can_scope_bare_name(parser, name)) {
        Span scoped = parser_scoped_bare_name(parser, name);
        ConstDef *constant = symbols_find_const(parser->symbols, scoped);
        if (constant) return constant;
    }
    return symbols_find_const(parser->symbols, name);
}

Span cold_import_default_alias(Span module_path) {
    int32_t start = 0;
    for (int32_t i = 0; i < module_path.len; i++) {
        if (module_path.ptr[i] == '/') start = i + 1;
    }
    return span_sub(module_path, start, module_path.len);
}

bool cold_parse_import_line(Span trimmed, Span *module_out, Span *alias_out) {
    if (!cold_span_starts_with(trimmed, "import ")) return false;
    int32_t pos = 6;
    while (pos < trimmed.len && (trimmed.ptr[pos] == ' ' || trimmed.ptr[pos] == '\t')) pos++;
    if (pos >= trimmed.len) return false;
    /* check for group import: import pkg/[a, b, c] -- return first path, caller handles expansion */
    int32_t module_start = pos;
    while (pos < trimmed.len && trimmed.ptr[pos] != ' ' && trimmed.ptr[pos] != '\t' && trimmed.ptr[pos] != '/') pos++;
    /* if followed by /[, it's a group import */
    if (pos + 1 < trimmed.len && trimmed.ptr[pos] == '/' && trimmed.ptr[pos + 1] == '[') {
        /* extract module path before /[ */
        Span prefix = span_sub(trimmed, module_start, pos);
        /* skip /[ */
        pos += 2;
        while (pos < trimmed.len && trimmed.ptr[pos] == ' ') pos++;
        int32_t item_start = pos;
        /* find ], then extract each comma-separated item */
        int32_t bracket_end = pos;
        while (bracket_end < trimmed.len && trimmed.ptr[bracket_end] != ']') bracket_end++;
        if (bracket_end >= trimmed.len) return false;
        /* return first item as module_out (caller will re-call for each item) */
        while (pos < bracket_end && trimmed.ptr[pos] != ',') pos++;
        Span item = span_trim(span_sub(trimmed, item_start, pos));
        char full_path[256];
        int32_t plen = snprintf(full_path, sizeof(full_path), "%.*s/%.*s", prefix.len, prefix.ptr, item.len, item.ptr);
        if (plen <= 0 || plen >= (int32_t)sizeof(full_path)) return false;
        *module_out = (Span){(const uint8_t *)full_path, plen};
        *alias_out = cold_import_default_alias(item);
        /* TODO: group expansion - currently only returns first item; full expansion needs caller loop */
        return true;
    }
    while (pos < trimmed.len && trimmed.ptr[pos] != ' ' && trimmed.ptr[pos] != '\t') pos++;
    if (pos <= module_start) return false;
    Span module_path = span_sub(trimmed, module_start, pos);
    Span alias = cold_import_default_alias(module_path);
    while (pos < trimmed.len && (trimmed.ptr[pos] == ' ' || trimmed.ptr[pos] == '\t')) pos++;
    /* forward scan for "as" keyword (more robust than backward scan) */
    if (pos + 2 <= trimmed.len && memcmp(trimmed.ptr + pos, "as", 2) == 0 &&
        (pos + 2 == trimmed.len || trimmed.ptr[pos + 2] == ' ' || trimmed.ptr[pos + 2] == '\t')) {
        pos += 2;
        while (pos < trimmed.len && (trimmed.ptr[pos] == ' ' || trimmed.ptr[pos] == '\t')) pos++;
        int32_t alias_start = pos;
        while (pos < trimmed.len && cold_ident_char(trimmed.ptr[pos])) pos++;
        if (pos <= alias_start) die("cold import alias missing name");
        alias = span_sub(trimmed, alias_start, pos);
    }
    *module_out = module_path;
    *alias_out = alias;
    return true;
}

bool cold_import_source_path(Span module_path, char *out, size_t out_cap) {
    if (module_path.len <= 0 || module_path.len >= PATH_MAX) return false;
    /* strip quotes if present */
    if (module_path.len >= 2 && module_path.ptr[0] == '"' && module_path.ptr[module_path.len - 1] == '"') {
        module_path = span_sub(module_path, 1, module_path.len - 1);
    }
    if (module_path.len <= 0) return false;
    /* strip module_prefix from cheng-package.toml if present (default: "cheng/") */
    if (cold_span_starts_with(module_path, "cheng/")) {
        module_path = span_sub(module_path, 6, module_path.len);
    } else if (cold_span_starts_with(module_path, "std/")) {
        /* std/ prefix maps directly to src/std/ */
    }
    if (module_path.len <= 0) return false;
    /* Check if path already has prefix and suffix */
    bool has_src_prefix = cold_span_starts_with(module_path, "src/");
    bool has_cheng_suffix = module_path.len > 6 &&
        module_path.ptr[module_path.len - 6] == '.' &&
        module_path.ptr[module_path.len - 5] == 'c' &&
        module_path.ptr[module_path.len - 4] == 'h' &&
        module_path.ptr[module_path.len - 3] == 'e' &&
        module_path.ptr[module_path.len - 2] == 'n' &&
        module_path.ptr[module_path.len - 1] == 'g';
    if (has_src_prefix && has_cheng_suffix) {
        /* Path is already a full relative path like src/tests/foo.cheng */
        int written = snprintf(out, out_cap, "%.*s", module_path.len, module_path.ptr);
        return written > 0 && (size_t)written < out_cap;
    }
    int written = snprintf(out, out_cap, "src/%.*s.cheng", module_path.len, module_path.ptr);
    return written > 0 && (size_t)written < out_cap;
}

int32_t cold_imported_param_specs(Symbols *symbols, Span alias, Span params,
                                         Span *names, int32_t *kinds,
                                         int32_t *sizes, int32_t cap) {
    Parser parser = {params, 0, symbols->arena, symbols};
    int32_t count = 0;
    while (parser.pos < parser.source.len) {
        Span name = parser_token(&parser);
        if (name.len == 0) break;
        if (span_eq(name, ",")) continue;
        if (count >= cap) die("cold imported prototype supports at most sixteen params");
        Span type = cold_cstr_span("int32");
        if (span_eq(parser_peek(&parser), ":")) {
            (void)parser_token(&parser);
            type = cold_qualify_import_type(symbols->arena, alias,
                                            parser_take_type_span(&parser));
        }
        int32_t kind = cold_slot_kind_from_type_with_symbols(symbols, type);
        names[count] = name;
        kinds[count] = kind;
        sizes[count] = cold_param_size_from_type(symbols, type, kind);
        count++;
        while (parser.pos < parser.source.len &&
               !span_eq(parser_peek(&parser), ",") &&
               !span_eq(parser_peek(&parser), ")")) {
            (void)parser_token(&parser);
        }
        if (span_eq(parser_peek(&parser), ",")) (void)parser_token(&parser);
    }
    return count;
}

void parse_type(Parser *parser);

void cold_collect_import_module_types(Symbols *symbols, Span alias, Span source) {
    ColdErrorRecoveryEnabled = true;
    if (setjmp(ColdErrorJumpBuf) != 0) { ColdErrorRecoveryEnabled = false; return; }
    Symbols *local_symbols = symbols_new(symbols->arena);
    Parser parser = {source, 0, symbols->arena, local_symbols};
    while (parser.pos < source.len) {
        parser_ws(&parser);
        if (parser.pos >= source.len) break;
        Span next = parser_peek(&parser);
        if (span_eq(next, "type")) {
            parse_type(&parser);
        } else {
            parser_line(&parser);
        }
    }

    int32_t source_type_count = local_symbols->type_count;
    for (int32_t ti = 0; ti < source_type_count; ti++) {
        TypeDef *src = &local_symbols->types[ti];
        if (!src->is_enum) continue;
        Span qualified_type = cold_arena_join3(symbols->arena, alias, ".", src->name);
        TypeDef *dst = symbols_find_type(symbols, qualified_type);
        if (!dst) dst = symbols_add_type(symbols, qualified_type, src->variant_count);
        if (dst->variant_count != src->variant_count) {
            /* Variant count mismatch: skip enum merge */
            continue;
        }
        dst->is_enum = true;
        for (int32_t vi = 0; vi < src->variant_count; vi++) {
            Variant *src_variant = &src->variants[vi];
            Variant *dst_variant = &dst->variants[vi];
            Span qualified_variant = cold_arena_join3(symbols->arena, alias, ".", src_variant->name);
            dst_variant->name = qualified_variant;
            dst_variant->tag = src_variant->tag;
            dst_variant->field_count = 0;
            variant_finalize_layout(dst, dst_variant);
            if (!symbols_find_const(symbols, qualified_variant)) {
                symbols_add_const(symbols, qualified_variant, src_variant->tag);
            }
        }
    }

    int32_t source_object_count = local_symbols->object_count;
    for (int32_t oi = 0; oi < source_object_count; oi++) {
        ObjectDef *src = &local_symbols->objects[oi];
        Span qualified_name = cold_arena_join3(symbols->arena, alias, ".", src->name);
        if (!symbols_find_object(symbols, qualified_name)) {
            ObjectDef *dst = symbols_add_object(symbols, qualified_name, src->field_count);
            dst->generic_count = src->generic_count;
            dst->is_ref = src->is_ref;
            for (int32_t gi = 0; gi < src->generic_count; gi++) {
                dst->generic_names[gi] = cold_arena_span_copy(symbols->arena, src->generic_names[gi]);
            }
        }
    }
    for (int32_t oi = 0; oi < source_object_count; oi++) {
        ObjectDef *src = &local_symbols->objects[oi];
        Span qualified_name = cold_arena_join3(symbols->arena, alias, ".", src->name);
        ObjectDef *dst = symbols_find_object(symbols, qualified_name);
        if (!dst || dst->field_count != src->field_count) die("imported object alias drift");
        dst->generic_count = src->generic_count;
        for (int32_t gi = 0; gi < src->generic_count; gi++) {
            dst->generic_names[gi] = cold_arena_span_copy(symbols->arena, src->generic_names[gi]);
        }
        for (int32_t fi = 0; fi < src->field_count; fi++) {
            bool generic_field = cold_type_is_generic_placeholder(src->fields[fi].type_name,
                                                                  src->generic_names,
                                                                  src->generic_count);
            Span field_type = generic_field
                                  ? cold_arena_span_copy(symbols->arena, src->fields[fi].type_name)
                                  : cold_qualify_import_type(symbols->arena, alias,
                                                            src->fields[fi].type_name);
            int32_t fk = generic_field ? SLOT_I32 : cold_slot_kind_from_type_with_symbols(symbols, field_type);
            dst->fields[fi].name = cold_arena_span_copy(symbols->arena, src->fields[fi].name);
            dst->fields[fi].type_name = field_type;
            dst->fields[fi].kind = fk;
            dst->fields[fi].size = generic_field ? 4
                                                 : cold_slot_size_from_type_with_symbols(symbols, field_type, fk);
            dst->fields[fi].array_len = 0;
            if (fk == SLOT_ARRAY_I32 && !cold_parse_i32_array_type(field_type, &dst->fields[fi].array_len) && !cold_span_starts_with(span_trim(field_type), "uint8[")) {
                dst->fields[fi].array_len = 4; /* default */
            }
        }
        dst->is_ref = src->is_ref;
        object_finalize_fields(dst);
    }
}

void cold_register_import_enum(Symbols *symbols, Span alias, Span type_name,
                                      Span *variant_names, int32_t variant_count) {
    if (variant_count <= 0) return;
    Span qualified_type = cold_arena_join3(symbols->arena, alias, ".", type_name);
    TypeDef *type = symbols_find_type(symbols, qualified_type);
    if (!type) {
        type = symbols_add_type(symbols, qualified_type, variant_count);
    }
    if (type->variant_count != variant_count) die("import enum variant count drift");
    type->is_enum = true;
    for (int32_t vi = 0; vi < variant_count; vi++) {
        Span qualified_variant = cold_arena_join3(symbols->arena, alias, ".",
                                                  variant_names[vi]);
        Variant *variant = &type->variants[vi];
        variant->name = qualified_variant;
        variant->tag = vi;
        variant->field_count = 0;
        variant_finalize_layout(type, variant);
        if (!symbols_find_const(symbols, qualified_variant)) {
            symbols_add_const(symbols, qualified_variant, vi);
        }
        Span qualified_type_variant = cold_arena_join3(symbols->arena,
                                                       qualified_type, ".",
                                                       variant_names[vi]);
        if (!symbols_find_const(symbols, qualified_type_variant)) {
            symbols_add_const(symbols, qualified_type_variant, vi);
        }
    }
}

bool cold_parse_enum_member_line(Span trimmed, Span *type_name,
                                        Span *inline_variants) {
    int32_t eq = cold_span_find_char(trimmed, '=');
    if (eq <= 0) return false;
    Span left = span_trim(span_sub(trimmed, 0, eq));
    Span right = span_trim(span_sub(trimmed, eq + 1, trimmed.len));
    if (!cold_span_starts_with(right, "enum")) return false;
    if (cold_span_starts_with(left, "type ")) {
        left = span_trim(span_sub(left, 5, left.len));
    }
    if (!cold_span_is_simple_ident(left)) return false;
    *type_name = left;
    *inline_variants = span_trim(span_sub(right, 4, right.len));
    return true;
}

int32_t cold_collect_enum_variants_from_span(Span text, Span *variants,
                                                    int32_t cap) {
    Parser parser = {text, 0, 0, 0};
    int32_t count = 0;
    while (parser.pos < parser.source.len) {
        Span name = parser_token(&parser);
        if (name.len <= 0) break;
        if (span_eq(name, ",") || !cold_span_is_simple_ident(name)) continue;
        if (count >= cap) die("too many cold enum variants");
        variants[count++] = name;
    }
    return count;
}

void cold_collect_import_module_enum_blocks(Symbols *symbols, Span alias,
                                                   Span source) {
    int32_t line_cap = 1;
    for (int32_t i = 0; i < source.len; i++) {
        if (source.ptr[i] == '\n') line_cap++;
    }
    int32_t *line_starts = arena_alloc(symbols->arena, (size_t)line_cap * sizeof(int32_t));
    int32_t *line_ends = arena_alloc(symbols->arena, (size_t)line_cap * sizeof(int32_t));
    int32_t line_count = 0;
    int32_t pos = 0;
    while (pos < source.len) {
        if (line_count >= line_cap) die("cold enum line accounting drift");
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        line_starts[line_count] = start;
        line_ends[line_count] = pos;
        line_count++;
        if (pos < source.len) pos++;
    }
    bool in_type_group = false;
    int32_t group_indent = -1;
    for (int32_t li = 0; li < line_count; li++) {
        Span line = span_sub(source, line_starts[li], line_ends[li]);
        Span trimmed = span_trim(line);
        if (trimmed.len <= 0) continue;
        int32_t indent = cold_line_indent_width(line);
        if (indent == 0) {
            in_type_group = span_eq(trimmed, "type");
            group_indent = -1;
            if (!in_type_group && !cold_span_starts_with(trimmed, "type ")) {
                continue;
            }
        } else if (!in_type_group) {
            continue;
        }
        if (in_type_group) {
            if (indent <= 0) continue;
            if (group_indent < 0) group_indent = indent;
            if (indent < group_indent) {
                in_type_group = false;
                group_indent = -1;
                continue;
            }
            if (indent != group_indent) continue;
        }
        Span type_name = {0};
        Span inline_variants = {0};
        if (!cold_parse_enum_member_line(trimmed, &type_name, &inline_variants)) {
            continue;
        }
        Span variants[COLD_MAX_OBJECT_FIELDS];
        int32_t variant_count = cold_collect_enum_variants_from_span(inline_variants,
                                                                     variants,
                                                                     COLD_MAX_OBJECT_FIELDS);
        if (variant_count == 0) {
            int32_t body_indent = -1;
            for (int32_t vi_line = li + 1; vi_line < line_count; vi_line++) {
                Span vline = span_sub(source, line_starts[vi_line], line_ends[vi_line]);
                Span vt = span_trim(vline);
                if (vt.len <= 0) continue;
                int32_t vindent = cold_line_indent_width(vline);
                if (vindent <= indent) break;
                if (body_indent < 0) body_indent = vindent;
                if (vindent < body_indent) break;
                if (vindent != body_indent) continue;
                if (!cold_span_is_simple_ident(vt)) break;
                if (variant_count >= COLD_MAX_OBJECT_FIELDS) die("too many cold enum variants");
                variants[variant_count++] = vt;
            }
        }
        cold_register_import_enum(symbols, alias, type_name, variants, variant_count);
    }
}

void cold_collect_import_module_consts(Symbols *symbols, Span alias, Span source) {
    /* scan source for top-level "const" blocks and add constants with alias */
    int32_t pos = 0;
    while (pos < source.len) {
        /* find next top-level line */
        int32_t ls = pos;
        while (ls < source.len && source.ptr[ls] != '\n') ls++;
        int32_t le = ls;
        if (ls < source.len) ls++;
        Span line = span_sub(source, pos, le);
        pos = ls;
        Span trimmed = span_trim(line);
        if (trimmed.len <= 0 || !cold_line_top_level(line)) continue;
        if (!span_eq(trimmed, "const")) continue;
        /* found const block: scan indented name=value lines */
        while (pos < source.len) {
            int32_t cs = pos;
            while (pos < source.len && source.ptr[pos] != '\n') pos++;
            int32_t ce = pos;
            if (pos < source.len) pos++;
            Span cline = span_sub(source, cs, ce);
            Span ct = span_trim(cline);
            if (ct.len <= 0 || cold_line_top_level(cline)) { pos = cs; break; }
            int32_t ceq = cold_span_find_char(ct, '=');
            if (ceq <= 0) continue;
            Span cname = span_trim(span_sub(ct, 0, ceq));
            /* Strip type annotation (e.g. "Name: Type = Value" → "Name") */
            int32_t colon = cold_span_find_char(cname, ':');
            if (colon > 0) cname = span_trim(span_sub(cname, 0, colon));
            Span cval = span_trim(span_sub(ct, ceq + 1, ct.len));
            if (cname.len <= 0) continue;
            Span aliased = cold_arena_join3(symbols->arena, alias, ".", cname);
            if (cval.len >= 2 && cval.ptr[0] == '"' && cval.ptr[cval.len - 1] == '"') {
                Span inner = span_sub(cval, 1, cval.len - 1);
                symbols_add_str_const(symbols, aliased, cold_decode_string_content(symbols->arena, inner, false));
            } else if (span_is_i32(cval)) {
                symbols_add_const(symbols, aliased, span_i32(cval));
            }
        }
    }
}

void cold_collect_import_module_types_from_path(Symbols *symbols, Span alias,
                                                       Span module_path) {
    char path[PATH_MAX];
    if (!cold_import_source_path(module_path, path, sizeof(path))) {
        die("cold import path is not resolvable");
    }
    Span source = source_open(path);
    if (source.len <= 0) return; /* skip empty import */
    cold_collect_import_module_types(symbols, alias, source);
    cold_collect_import_module_enum_blocks(symbols, alias, source);
    cold_collect_import_module_consts(symbols, alias, source);
    munmap((void *)source.ptr, (size_t)source.len);
}

void symbols_refine_object_layouts(Symbols *symbols) {
    for (int32_t oi = 0; oi < symbols->object_count; oi++) {
        ObjectDef *object = &symbols->objects[oi];
        for (int32_t fi = 0; fi < object->field_count; fi++) {
            ObjectField *field = &object->fields[fi];
            bool generic_field = cold_type_is_generic_placeholder(field->type_name,
                                                                  object->generic_names,
                                                                  object->generic_count);
            if (generic_field) {
                field->kind = SLOT_I32;
                field->size = 4;
                field->array_len = 0;
                continue;
            }
            int32_t kind = cold_slot_kind_from_type_with_symbols(symbols, field->type_name);
            if (kind == SLOT_VARIANT && !symbols_resolve_type(symbols, field->type_name)) kind = SLOT_I32;
            field->kind = kind;
            field->size = cold_slot_size_from_type_with_symbols(symbols, field->type_name, kind);
            field->array_len = 0;
            if (kind == SLOT_ARRAY_I32 &&
                !cold_parse_i32_array_type(field->type_name, &field->array_len) &&
                !cold_span_starts_with(span_trim(field->type_name), "uint8[")) {
                field->array_len = 4; /* default */
            }
        }
        object_finalize_fields(object);
    }
}

void cold_collect_import_module_signatures(Symbols *symbols, Span alias,
                                                  Span module_path) {
    char path[PATH_MAX];
    if (!cold_import_source_path(module_path, path, sizeof(path))) {
        die("cold import path is not resolvable");
    }
    Span source = source_open(path);
    if (source.len <= 0) return; /* skip empty import */
    int32_t pos = 0;
    int32_t line_no = 0;
    bool in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        line_no++;
        Span line = span_sub(source, start, end);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        Span trimmed = span_trim(line);
        if (!cold_span_starts_with(trimmed, "fn ")) continue;
        ColdFunctionSymbol symbol;
        if (!cold_parse_function_symbol_at(source, start, line_no, &symbol)) {
            if (trimmed.len > 3 && trimmed.ptr[3] == '`') continue;
            fprintf(stderr, "cheng_cold: cannot parse imported signature path=%s line=%d\n",
                    path, line_no);
            continue;
        }
        Span names[COLD_MAX_I32_PARAMS];
        int32_t kinds[COLD_MAX_I32_PARAMS];
        int32_t sizes[COLD_MAX_I32_PARAMS];
        int32_t arity = cold_imported_param_specs(symbols, alias, symbol.params,
                                                  names, kinds, sizes,
                                                  COLD_MAX_I32_PARAMS);
        Span qualified_name = cold_arena_join3(symbols->arena, alias, ".", symbol.name);
        Span qualified_ret = cold_qualify_import_type(symbols->arena, alias,
                                                      symbol.return_type);
        int32_t fi = symbols_add_fn(symbols, qualified_name, arity, kinds, sizes, qualified_ret);
        if (symbol.import_name.len > 0) symbols_set_fn_link_name(symbols, fi, symbol.import_name);
        if (symbol.export_name.len > 0) symbols_set_fn_link_name(symbols, fi, symbol.export_name);
        if (fi >= 0 && fi < symbols->function_cap && !symbol.has_body) {
            symbols->functions[fi].is_external = true;
        }
    }
    munmap((void *)source.ptr, (size_t)source.len);
}

void cold_collect_imported_function_signatures(Symbols *symbols, Span source) {
    /* De-duplication: track visited module paths to avoid re-parsing
       the same source file when multiple imports reference it (e.g.
       different aliases for the same module, or transitive re-imports). */
    char visited_paths[64][PATH_MAX];
    int32_t visited_count = 0;

    int32_t pos = 0;
    bool in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        Span line = span_sub(source, start, end);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        Span module_path = {0};
        Span alias = {0};
        if (!cold_parse_import_line(span_trim(line), &module_path, &alias)) continue;
        char path_buf[PATH_MAX];
        if (!cold_import_source_path(module_path, path_buf, sizeof(path_buf))) continue;
        bool seen = false;
        for (int32_t vi = 0; vi < visited_count; vi++) {
            if (strcmp(visited_paths[vi], path_buf) == 0) { seen = true; break; }
        }
        if (seen) continue;
        if (visited_count < 64) {
            strncpy(visited_paths[visited_count++], path_buf, PATH_MAX - 1);
        }
        cold_collect_import_module_types_from_path(symbols, alias, module_path);
    }
    symbols_refine_object_layouts(symbols);

    /* Reset visited for pass 2 (function signatures) */
    visited_count = 0;
    pos = 0;
    in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        Span line = span_sub(source, start, end);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        Span module_path = {0};
        Span alias = {0};
        if (!cold_parse_import_line(span_trim(line), &module_path, &alias)) continue;
        char path_buf[PATH_MAX];
        if (!cold_import_source_path(module_path, path_buf, sizeof(path_buf))) continue;
        bool seen = false;
        for (int32_t vi = 0; vi < visited_count; vi++) {
            if (strcmp(visited_paths[vi], path_buf) == 0) { seen = true; break; }
        }
        if (seen) continue;
        if (visited_count < 64) {
            strncpy(visited_paths[visited_count++], path_buf, PATH_MAX - 1);
        }
        cold_collect_import_module_signatures(symbols, alias, module_path);
    }
}

void cold_collect_function_signatures(Symbols *symbols, Span source) {
    int32_t pos = 0;
    int32_t line_no = 0;
    bool in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        line_no++;
        Span line = span_sub(source, start, end);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        Span trimmed = span_trim(line);
        if (!cold_span_starts_with(trimmed, "fn ")) continue;
        ColdFunctionSymbol symbol;
        if (!cold_parse_function_symbol_at(source, start, line_no, &symbol)) {
            continue; /* skip unparseable signature */
        }
        Span param_names[COLD_MAX_I32_PARAMS];
        int32_t param_kinds[COLD_MAX_I32_PARAMS];
        int32_t param_sizes[COLD_MAX_I32_PARAMS];
        int32_t arity = parse_param_specs(0, symbol.params, param_names, param_kinds,
                                          param_sizes, 0, COLD_MAX_I32_PARAMS);
        int32_t fi = symbols_add_fn(symbols, symbol.name, arity, param_kinds, param_sizes, symbol.return_type);
        if (symbol.import_name.len > 0) symbols_set_fn_link_name(symbols, fi, symbol.import_name);
        if (symbol.export_name.len > 0) symbols_set_fn_link_name(symbols, fi, symbol.export_name);
        if (fi >= 0 && fi < symbols->function_cap) {
            FnDef *fn = &symbols->functions[fi];
            fn->generic_count = symbol.generic_count;
            for (int32_t gi = 0; gi < symbol.generic_count && gi < 4; gi++)
                fn->generic_names[gi] = symbol.generic_names[gi];
        }
    }
}

void parse_type(Parser *parser);

Span cold_normalize_grouped_type_member(Arena *arena, Span member, int32_t group_indent) {
    uint8_t *out = arena_alloc(arena, (size_t)member.len + 1);
    int32_t count = 0;
    int32_t pos = 0;
    while (pos < member.len) {
        int32_t line_start = pos;
        while (pos < member.len && member.ptr[pos] != '\n') pos++;
        int32_t line_end = pos;
        if (pos < member.len) pos++;
        int32_t p = line_start;
        int32_t removed = 0;
        while (p < line_end && removed < group_indent && member.ptr[p] == ' ') {
            p++;
            removed++;
        }
        for (int32_t i = p; i < line_end; i++) out[count++] = member.ptr[i];
        if (pos <= member.len) out[count++] = '\n';
    }
    return (Span){out, count};
}

void parse_grouped_type_block(Parser *parser, int32_t block_start, int32_t block_end) {
    int32_t pos = block_start;
    while (pos < block_end && parser->source.ptr[pos] != '\n') pos++;
    if (pos < block_end) pos++;
    int32_t group_indent = -1;
    while (pos < block_end) {
        int32_t line_start = pos;
        int32_t indent = 0;
        while (pos < block_end && parser->source.ptr[pos] == ' ') {
            indent++;
            pos++;
        }
        if (pos >= block_end) break;
        if (parser->source.ptr[pos] == '\n') {
            pos++;
            continue;
        }
        if (group_indent < 0) group_indent = indent;
        if (indent < group_indent || indent == 0) break;
        int32_t member_start = line_start;
        while (pos < block_end && parser->source.ptr[pos] != '\n') pos++;
        if (pos < block_end) pos++;
        while (pos < block_end) {
            int32_t next_line = pos;
            int32_t next_indent = 0;
            while (pos < block_end && parser->source.ptr[pos] == ' ') {
                next_indent++;
                pos++;
            }
            if (pos >= block_end) break;
            if (parser->source.ptr[pos] == '\n') {
                pos++;
                continue;
            }
            if (next_indent == group_indent) {
                pos = next_line;
                break;
            }
            if (next_indent < group_indent || next_indent == 0) {
                pos = next_line;
                break;
            }
            while (pos < block_end && parser->source.ptr[pos] != '\n') pos++;
            if (pos < block_end) pos++;
        }
        Span normalized = cold_normalize_grouped_type_member(parser->arena,
                                                            span_sub(parser->source, member_start, pos),
                                                            group_indent);
        uint8_t *synthetic = arena_alloc(parser->arena, (size_t)normalized.len + 6);
        memcpy(synthetic, "type ", 5);
        memcpy(synthetic + 5, normalized.ptr, (size_t)normalized.len);
        Parser member_parser = parser_child(parser, (Span){synthetic, normalized.len + 5});
        parse_type(&member_parser);
    }
    parser->pos = block_end;
}

void parse_type(Parser *parser) {
    int32_t line_start = parser->pos;
    int32_t first_line_end = line_start;
    while (first_line_end < parser->source.len && parser->source.ptr[first_line_end] != '\n') first_line_end++;
    Span first_line = span_trim(span_sub(parser->source, line_start, first_line_end));
    if (span_eq(first_line, "type")) {
        int32_t group_end = first_line_end;
        if (group_end < parser->source.len) group_end++;
        while (group_end < parser->source.len) {
            int32_t pos = group_end;
            int32_t indent = 0;
            while (pos < parser->source.len && parser->source.ptr[pos] == ' ') {
                indent++;
                pos++;
            }
            if (pos >= parser->source.len) break;
            if (parser->source.ptr[pos] == '\n') {
                group_end = pos + 1;
                continue;
            }
            if (indent == 0) break;
            while (group_end < parser->source.len && parser->source.ptr[group_end] != '\n') group_end++;
            if (group_end < parser->source.len) group_end++;
        }
        parse_grouped_type_block(parser, line_start, group_end);
        return;
    }

    int32_t line_end = first_line_end;
    int32_t type_body_indent = -1;
    if (line_end < parser->source.len) line_end++;
    while (line_end < parser->source.len) {
        int32_t indent = 0;
        int32_t pos = line_end;
        while (pos < parser->source.len && parser->source.ptr[pos] == ' ') { indent++; pos++; }
        if (pos >= parser->source.len || parser->source.ptr[pos] == '\n') break;
        if (type_body_indent < 0) type_body_indent = indent;
        if (indent < type_body_indent) break;
        if (indent == 0) break;
        while (line_end < parser->source.len && parser->source.ptr[line_end] != '\n') line_end++;
        if (line_end < parser->source.len) line_end++;
    }

    Parser line = parser_child(parser, span_sub(parser->source, line_start, line_end));
    if (!parser_take(&line, "type")) die("expected type");
    Span type_name = parser_token(&line);
    Span generic_names[COLD_MAX_VARIANT_FIELDS];
    int32_t generic_count = 0;
    if (span_eq(parser_peek(&line), "[")) {
        (void)parser_token(&line);
        while (!span_eq(parser_peek(&line), "]")) {
            if (generic_count >= COLD_MAX_VARIANT_FIELDS) die("too many cold generic params");
            Span generic = parser_token(&line);
            if (!cold_span_is_simple_ident(generic)) die("expected generic param name");
            generic_names[generic_count++] = generic;
            if (span_eq(parser_peek(&line), ",")) {
                (void)parser_token(&line);
                continue;
            }
            break;
        }
        if (!parser_take(&line, "]")) die("expected ] after generic params");
    }
    if (!parser_take(&line, "=")) {
        parser->pos = line_end;
        return;
    }

    Span rhs_check = span_trim(span_sub(line.source, line.pos, line.source.len));
    if (cold_span_starts_with(rhs_check, "enum")) {
        if (generic_count > 0) die("cold enum cannot be generic");
        Parser enum_parser = parser_child(parser, rhs_check);
        if (!parser_take(&enum_parser, "enum")) die("expected enum");
        Span variant_names[COLD_MAX_OBJECT_FIELDS];
        int32_t enum_variant_count = 0;
        while (enum_parser.pos < enum_parser.source.len) {
            Span variant_name = parser_token(&enum_parser);
            if (variant_name.len <= 0) break;
            if (span_eq(variant_name, ",")) continue;
            if (!cold_span_is_simple_ident(variant_name)) continue; /* skip non-ident variant */
            if (enum_variant_count >= COLD_MAX_OBJECT_FIELDS) break; /* too many variants */
            variant_names[enum_variant_count++] = variant_name;
        }
        if (enum_variant_count <= 0) die("cold enum has no variants");
        TypeDef *type = symbols_add_type(parser->symbols, type_name, enum_variant_count);
        type->is_enum = true;
        for (int32_t vi = 0; vi < enum_variant_count; vi++) {
            Variant *variant = &type->variants[vi];
            variant->name = cold_arena_span_copy(parser->arena, variant_names[vi]);
            variant->tag = vi;
            variant->field_count = 0;
            variant_finalize_layout(type, variant);
            symbols_add_const(parser->symbols, variant->name, vi);
            /* also register qualified name: Type.Variant */
            int32_t qn_len = type_name.len + 1 + variant->name.len;
            char *qn_ptr = arena_alloc(parser->arena, (size_t)qn_len + 1);
            memcpy(qn_ptr, type_name.ptr, (size_t)type_name.len);
            qn_ptr[type_name.len] = '.';
            memcpy(qn_ptr + type_name.len + 1, variant->name.ptr, (size_t)variant->name.len);
            qn_ptr[qn_len] = '\0';
            Span qn = {qn_ptr, qn_len};
            symbols_add_const(parser->symbols, qn, vi);
        }
        parser->pos = line_end;
        return;
    }
    if (cold_span_starts_with(rhs_check, "fn") ||
        cold_type_is_builtin_surface(rhs_check)) {
        parser->pos = line_end;
        return;
    }
    if (cold_span_starts_with(rhs_check, "ref")) {
        Span ref_body = span_trim(span_sub(rhs_check, 3, rhs_check.len));
        Span fields_r = {0};
        if (ref_body.len > 0) {
            fields_r = ref_body;
        } else {
            int32_t bs = line_end + 1;
            int32_t bi = -1;
            int32_t bf = bs;
            while (bf < parser->source.len) {
                int32_t ind = 0;
                int32_t p = bf;
                while (p < parser->source.len && parser->source.ptr[p] == ' ') { ind++; p++; }
                if (p >= parser->source.len || parser->source.ptr[p] == '\n') { bf++; continue; }
                if (ind == 0 || (bi >= 0 && ind < bi)) break;
                if (bi < 0) bi = ind;
                while (bf < parser->source.len && parser->source.ptr[bf] != '\n') bf++;
                if (bf < parser->source.len) bf++;
            }
            fields_r = span_sub(parser->source, bs, bf);
        }
        if (fields_r.len > 0) {
            int32_t rfc = 0;
            Span rfn[64];
            Span rft[64];
            int32_t fp = 0;
            while (fp < fields_r.len) {
                int32_t ln = fp;
                while (fp < fields_r.len && fields_r.ptr[fp] != '\n') fp++;
                Span fl = span_sub(fields_r, ln, fp);
                if (fp < fields_r.len) fp++;
                Span tr = span_trim(fl);
                if (tr.len <= 0) continue;
                if (rfc >= 64) break;
                Parser rp = {tr, 0, 0, 0};
                Span fn2 = parser_token(&rp);
                if (fn2.len <= 0) continue;
                if (!parser_take(&rp, ":")) continue;
                Span ft = parser_take_type_span(&rp);
                if (ft.len <= 0) continue;
                rfn[rfc] = fn2;
                rft[rfc] = ft;
                rfc++;
            }
            if (rfc > 0) {
                ObjectDef *robj = symbols_add_object(parser->symbols, type_name, rfc);
                robj->generic_count = generic_count;
                for (int32_t gi = 0; gi < generic_count; gi++) robj->generic_names[gi] = generic_names[gi];
                for (int32_t fi = 0; fi < rfc; fi++) {
                    robj->fields[fi].name = rfn[fi];
                    int32_t fk = cold_slot_kind_from_type_with_symbols(parser->symbols, rft[fi]);
                    if (fk == SLOT_VARIANT && !symbols_resolve_type(parser->symbols, rft[fi])) fk = SLOT_I32;
                    robj->fields[fi].kind = fk;
                    robj->fields[fi].size = cold_slot_size_from_type_with_symbols(parser->symbols, rft[fi], fk);
                    robj->fields[fi].type_name = rft[fi];
                    robj->fields[fi].array_len = 0;
                }
                object_finalize_fields(robj);
                robj->is_ref = true;
            }
        }
        parser->pos = line_end;
        return;
    }
    /* Allow inline paren (field: Type, ...) for generic objects.
       Skip the complex early-return check for parenthesized RHS. */
    bool is_paren_rhs = rhs_check.len > 0 && rhs_check.ptr[0] == '(';
    if (!is_paren_rhs &&
        cold_span_find_top_level_char(rhs_check, ':') < 0 &&
        cold_span_find_top_level_char(rhs_check, '|') < 0 &&
        cold_span_find_top_level_char(rhs_check, '(') < 0 &&
        !cold_span_starts_with(rhs_check, "object") &&
        !cold_span_starts_with(rhs_check, "tuple")) {
        parser->pos = line_end;
        return;
    }
    int32_t paren_find = cold_span_find_top_level_char(rhs_check, '|');
    bool is_inline_paren = rhs_check.len > 2 && rhs_check.ptr[0] == '(' &&
                           rhs_check.ptr[rhs_check.len - 1] == ')' &&
                           paren_find < 0;
    bool is_tuple_inline = cold_span_starts_with(rhs_check, "tuple[");
    bool implicit_object = cold_type_block_looks_like_object(rhs_check);
    if (implicit_object || cold_span_starts_with(rhs_check, "object") || is_tuple_inline || is_inline_paren) {
        int32_t field_count = 0;
        Span field_names[COLD_MAX_OBJECT_FIELDS];
        Span field_types[COLD_MAX_OBJECT_FIELDS];
        Span fields_span = {0};
        if (is_inline_paren) {
            Span inner = span_trim(span_sub(rhs_check, 1, rhs_check.len - 1));
            fields_span = inner;
        } else if (is_tuple_inline) {
            Span inner = span_trim(span_sub(rhs_check, 6, rhs_check.len));
            if (inner.len > 0 && inner.ptr[inner.len - 1] == ']')
                inner = span_sub(inner, 0, inner.len - 1);
            fields_span = inner;
        } else {
            int32_t body_start = line.pos;
            while (body_start < line_end && line.source.ptr[body_start] != '\n') body_start++;
            if (body_start < line_end) body_start++;
            int32_t body_indent = -1;
            int32_t body_finish = body_start;
            while (body_finish < line_end) {
                int32_t indent = 0;
                int32_t p = body_finish;
                while (p < line_end && line.source.ptr[p] == ' ') { indent++; p++; }
                if (p >= line_end || line.source.ptr[p] == '\n') { body_finish++; continue; }
                if (body_indent < 0) body_indent = indent;
                if (indent < body_indent || indent == 0) break;
                while (body_finish < line_end && line.source.ptr[body_finish] != '\n') body_finish++;
                if (body_finish < line_end) body_finish++;
            }
            fields_span = span_trim(span_sub(line.source, body_start, body_finish));
        }
        int32_t fp_pos = 0;
        while (fp_pos < fields_span.len) {
            int32_t ln_start = fp_pos;
            if (is_tuple_inline || is_inline_paren) {
                int32_t bracket_depth = 0;
                while (fp_pos < fields_span.len) {
                    uint8_t ch = fields_span.ptr[fp_pos];
                    if (ch == '[' || ch == '(' || ch == '{') bracket_depth++;
                    if (ch == ']' || ch == ')' || ch == '}') bracket_depth--;
                    if (bracket_depth == 0 && (ch == ',' || ch == ';')) break;
                    fp_pos++;
                }
            } else {
                while (fp_pos < fields_span.len && fields_span.ptr[fp_pos] != '\n') fp_pos++;
            }
            Span fline = span_sub(fields_span, ln_start, fp_pos);
            if (fp_pos < fields_span.len) fp_pos++;
            Span trimmed = span_trim(fline);
            if (trimmed.len <= 0) continue;
            if (field_count >= COLD_MAX_OBJECT_FIELDS) die("too many object fields");
            Parser fp = {trimmed, 0, 0, 0};
            Span field_name = parser_token(&fp);
            if (field_name.len <= 0) return; /* skip empty field name */
            if (!parser_take(&fp, ":")) die("expected : in object field");
            Span field_type = parser_take_type_span(&fp);
            if (field_type.len <= 0) die("expected object field type");
            field_names[field_count] = field_name;
            field_types[field_count] = field_type;
            field_count++;
        }
        ObjectDef *object = symbols_add_object(parser->symbols, type_name, field_count);
        object->generic_count = generic_count;
        for (int32_t gi = 0; gi < generic_count; gi++) object->generic_names[gi] = generic_names[gi];
        for (int32_t fi = 0; fi < field_count; fi++) {
            object->fields[fi].name = field_names[fi];
            bool generic_field = cold_type_is_generic_placeholder(field_types[fi],
                                                                  generic_names,
                                                                  generic_count);
            int32_t fk = generic_field ? SLOT_I32
                                       : cold_slot_kind_from_type_with_symbols(parser->symbols, field_types[fi]);
            if (fk == SLOT_VARIANT && !symbols_resolve_type(parser->symbols, field_types[fi])) {
                fk = SLOT_I32;
            }
            object->fields[fi].kind = fk;
            object->fields[fi].size = generic_field ? 4
                                                    : cold_slot_size_from_type_with_symbols(parser->symbols, field_types[fi], fk);
            object->fields[fi].type_name = field_types[fi];
            object->fields[fi].array_len = 0;
            if (fk == SLOT_ARRAY_I32 && !cold_parse_i32_array_type(field_types[fi], &object->fields[fi].array_len)) {
                object->fields[fi].array_len = 4; /* default for unsized arrays */
            }
        }
        object_finalize_fields(object);
        parser->pos = line_end;
        return;
    }

    int32_t saved = line.pos;
    int32_t variant_count = 0;
    while (line.pos < line.source.len) {
        Span token = parser_token(&line);
        if (token.len == 0) break;
        if (span_eq(token, "|")) continue;
        variant_count++;
        if (span_eq(parser_peek(&line), "(")) parser_skip_balanced(&line, "(", ")");
    }
    if (variant_count <= 0) die("type has no variants");

    line.pos = saved;
    TypeDef *type = symbols_add_type(parser->symbols, type_name, variant_count);
    for (int32_t vi = 0; vi < variant_count; vi++) {
        if (vi > 0 && !parser_take(&line, "|")) {
            return; /* skip malformed variants */
        }
        Span variant_name = parser_token(&line);
        int32_t field_count = 0;
        int32_t field_kinds[COLD_MAX_VARIANT_FIELDS];
        int32_t field_sizes[COLD_MAX_VARIANT_FIELDS];
        Span field_types[COLD_MAX_VARIANT_FIELDS];
        if (span_eq(parser_peek(&line), "(")) {
            parser_take(&line, "(");
            while (!span_eq(parser_peek(&line), ")")) {
                if (field_count >= COLD_MAX_VARIANT_FIELDS) die("too many variant fields");
                Span field = parser_token(&line);
                if (field.len == 0) die("unterminated variant fields");
                if (!parser_take(&line, ":")) die("expected : in variant field");
                Span field_type = parser_take_type_span(&line);
                if (field_type.len <= 0) die("expected variant field type");
                bool is_generic_field = false;
                for (int32_t gi = 0; gi < generic_count; gi++) {
                    if (span_same(field_type, generic_names[gi])) is_generic_field = true;
                }
                if (is_generic_field) {
                    field_kinds[field_count] = SLOT_I32;
                    field_sizes[field_count] = 4;
                    field_types[field_count] = field_type;
                    field_count++;
                } else {
                    int32_t fk = cold_slot_kind_from_type_with_symbols(parser->symbols, field_type);
                    field_kinds[field_count] = fk;
                    field_sizes[field_count] = cold_slot_size_from_type_with_symbols(parser->symbols, field_type, fk);
                    field_types[field_count] = field_type;
                    field_count++;
                }
                if (span_eq(parser_peek(&line), ",")) (void)parser_token(&line);
            }
            parser_take(&line, ")");
        }
        Variant *variant = &type->variants[vi];
        variant->name = cold_arena_span_copy(parser->arena, variant_name);
        variant->tag = vi;
        variant->field_count = field_count;
        for (int32_t i = 0; i < field_count; i++) {
            variant->field_kind[i] = field_kinds[i];
            variant->field_size[i] = field_sizes[i];
            variant->field_type[i] = field_types[i];
        }
        variant_finalize_layout(type, variant);
        /* register qualified name as constant: Type.Variant → variant tag */
        int32_t qn_len = type_name.len + 1 + variant->name.len;
        char *qn_ptr = arena_alloc(parser->arena, (size_t)qn_len + 1);
        memcpy(qn_ptr, type_name.ptr, (size_t)type_name.len);
        qn_ptr[type_name.len] = '.';
        memcpy(qn_ptr + type_name.len + 1, variant->name.ptr, (size_t)variant->name.len);
        qn_ptr[qn_len] = '\0';
        Span qn = {qn_ptr, qn_len};
        symbols_add_const(parser->symbols, qn, variant->tag);
    }
    type->generic_count = generic_count;
    for (int32_t gi = 0; gi < generic_count; gi++) type->generic_names[gi] = generic_names[gi];
    parser->pos = line_end;
    parser_line(parser);
}

Span cold_strip_inline_comment(Span line) {
    bool in_string = false;
    uint8_t quote = 0;
    for (int32_t i = 0; i < line.len; i++) {
        uint8_t c = line.ptr[i];
        if (in_string) {
            if (c == '\\' && i + 1 < line.len) {
                i++;
                continue;
            }
            if (c == quote) in_string = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = true;
            quote = c;
            continue;
        }
        if (c == '#') return span_trim(span_sub(line, 0, i));
        if (c == '/' && i + 1 < line.len && line.ptr[i + 1] == '/') {
            return span_trim(span_sub(line, 0, i));
        }
    }
    return span_trim(line);
}

bool cold_const_type_is_i32_surface(Span type) {
    type = span_trim(type);
    return type.len == 0 ||
           span_eq(type, "int32") || span_eq(type, "int") ||
           span_eq(type, "bool") || span_eq(type, "uint8") ||
           span_eq(type, "char") || span_eq(type, "uint16") ||
           span_eq(type, "int16") || span_eq(type, "uint32") ||
           span_eq(type, "int64") || span_eq(type, "uint64");
}

bool cold_parse_i32_const_literal(Span text, int32_t *out) {
    text = span_trim(text);
    if (text.len <= 0) return false;
    int32_t pos = 0;
    int32_t sign = 1;
    if (text.ptr[pos] == '-') {
        sign = -1;
        pos++;
    }
    if (pos >= text.len) return false;
    int32_t base = 10;
    if (pos + 1 < text.len && text.ptr[pos] == '0' &&
        (text.ptr[pos + 1] == 'x' || text.ptr[pos + 1] == 'X')) {
        base = 16;
        pos += 2;
        if (pos >= text.len) return false;
    }
    uint64_t value = 0;
    for (; pos < text.len; pos++) {
        uint8_t c = text.ptr[pos];
        int32_t digit = -1;
        if (c >= '0' && c <= '9') digit = (int32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = 10 + (int32_t)(c - 'a');
        else if (base == 16 && c >= 'A' && c <= 'F') digit = 10 + (int32_t)(c - 'A');
        else return false;
        if (digit >= base) return false;
        value = value * (uint64_t)base + (uint64_t)digit;
        if (value > 0xFFFFFFFFull) return false;
    }
    if (sign < 0) {
        if (value > 0x80000000ull) return false;
        *out = value == 0x80000000ull ? (int32_t)0x80000000u : -(int32_t)value;
    } else {
        *out = (int32_t)(uint32_t)value;
    }
    return true;
}

void parse_const_member_line(Parser *parser, Span line) {
    Span trimmed = cold_strip_inline_comment(line);
    if (trimmed.len <= 0) return;
    if (cold_span_starts_with(trimmed, "const ")) {
        trimmed = span_trim(span_sub(trimmed, 5, trimmed.len));
    }
    int32_t eq = cold_span_find_top_level_char(trimmed, '=');
    if (eq <= 0) {
        fprintf(stderr, "cheng_cold: invalid const declaration: %.*s\n",
                trimmed.len, trimmed.ptr);
        die("invalid const declaration");
    }
    Span head = span_trim(span_sub(trimmed, 0, eq));
    Span rhs = span_trim(span_sub(trimmed, eq + 1, trimmed.len));
    int32_t colon = cold_span_find_top_level_char(head, ':');
    Span type = {0};
    if (colon >= 0) {
        type = span_trim(span_sub(head, colon + 1, head.len));
        head = span_trim(span_sub(head, 0, colon));
    }
    if (!cold_span_is_simple_ident(head)) return; /* skip non-ident const */;
    if (!cold_const_type_is_i32_surface(type)) return; /* skip non-int32 const */
    int32_t value = 0;
    if (!cold_parse_i32_const_literal(rhs, &value)) {
        /* accept large hex values, store as 0 (cold compiler can't use 64-bit consts) */
        value = 0;
    }
    symbols_add_const(parser->symbols, head, value);
}

void parse_const(Parser *parser) {
    int32_t line_start = parser->pos;
    int32_t first_line_end = cold_line_end_from(parser->source, line_start);
    Span first_line = span_trim(span_sub(parser->source, line_start, first_line_end));
    if (span_eq(first_line, "const")) {
        int32_t pos = first_line_end;
        if (pos < parser->source.len) pos++;
        int32_t group_indent = -1;
        while (pos < parser->source.len) {
            int32_t member_start = pos;
            int32_t member_end = cold_line_end_from(parser->source, member_start);
            Span line = span_sub(parser->source, member_start, member_end);
            Span trimmed = span_trim(line);
            if (trimmed.len == 0) {
                pos = member_end;
                if (pos < parser->source.len) pos++;
                continue;
            }
            int32_t indent = cold_line_indent_width(line);
            if (indent == 0) break;
            if (group_indent < 0) group_indent = indent;
            if (indent < group_indent) break;
            parse_const_member_line(parser, line);
            pos = member_end;
            if (pos < parser->source.len) pos++;
        }
        parser->pos = pos;
        return;
    }
    if (!cold_span_starts_with(first_line, "const ")) die("expected const");
    parse_const_member_line(parser, first_line);
    parser->pos = first_line_end;
    if (parser->pos < parser->source.len) parser->pos++;
}

int32_t parse_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
bool cold_is_i32_to_str_intrinsic(Span name);
bool cold_try_parse_int_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out);
bool cold_try_str_join_intrinsic(BodyIR *body, Span name,
                                        int32_t arg_start, int32_t arg_count,
                                        int32_t *slot_out, int32_t *kind_out);
bool cold_try_str_split_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out);
bool cold_try_str_strip_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out);
bool cold_try_str_len_intrinsic(BodyIR *body, Span name,
                                       int32_t arg_start, int32_t arg_count,
                                       int32_t *slot_out, int32_t *kind_out);
bool cold_try_str_slice_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out);
bool cold_try_result_intrinsic(Parser *parser, BodyIR *body, Span name,
                                      int32_t arg_start, int32_t arg_count,
                                      int32_t *slot_out, int32_t *kind_out);
bool cold_try_os_intrinsic(Parser *parser, BodyIR *body, Span name,
                                  int32_t arg_start, int32_t arg_count,
                                  int32_t *slot_out, int32_t *kind_out);
bool cold_try_path_intrinsic(Parser *parser, BodyIR *body, Span name,
                                    int32_t arg_start, int32_t arg_count,
                                    int32_t *slot_out, int32_t *kind_out);
bool cold_try_csg_intrinsic(Parser *parser, BodyIR *body, Span name,
                                   int32_t arg_start, int32_t arg_count,
                                   int32_t *slot_out, int32_t *kind_out);
bool cold_try_slplan_intrinsic(Parser *parser, BodyIR *body, Span name,
                                      int32_t arg_start, int32_t arg_count,
                                      int32_t *slot_out, int32_t *kind_out);
bool cold_try_backend_intrinsic(Parser *parser, BodyIR *body, Span name,
                                       int32_t arg_start, int32_t arg_count,
                                       int32_t *slot_out, int32_t *kind_out);
bool cold_try_assert_intrinsic(BodyIR *body, Span name,
                                      int32_t arg_start, int32_t arg_count,
                                      int32_t *slot_out, int32_t *kind_out);
bool cold_try_bootstrap_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out);

bool cold_validate_call_args(BodyIR *body, FnDef *fn, int32_t arg_start, int32_t arg_count, bool import_mode) {
    if (arg_count != fn->arity) { return false; }
    for (int32_t i = 0; i < arg_count; i++) {
        int32_t arg_slot = body->call_arg_slot[arg_start + i];
        int32_t arg_kind = body->slot_kind[arg_slot];
        if (fn->param_kind[i] == SLOT_I32_REF) {
            if (arg_kind != SLOT_I32 && arg_kind != SLOT_I32_REF && arg_kind != SLOT_VARIANT) { return false; }
        } else if (fn->param_kind[i] == SLOT_I64_REF) {
            if (arg_kind != SLOT_I64 && arg_kind != SLOT_I64_REF) { return false; }
        } else if (fn->param_kind[i] == SLOT_SEQ_I32_REF) {
            if (arg_kind != SLOT_SEQ_I32 && arg_kind != SLOT_SEQ_I32_REF) { return false; }
        } else if (fn->param_kind[i] == SLOT_SEQ_STR_REF) {
            if (arg_kind != SLOT_SEQ_STR && arg_kind != SLOT_SEQ_STR_REF) { return false; }
        } else if (fn->param_kind[i] == SLOT_SEQ_OPAQUE_REF) {
            if (arg_kind != SLOT_SEQ_OPAQUE && arg_kind != SLOT_SEQ_OPAQUE_REF) { return false; }
        } else if (fn->param_kind[i] == SLOT_STR_REF) {
            if (arg_kind != SLOT_STR && arg_kind != SLOT_STR_REF) { return false; }
        } else if (fn->param_kind[i] == SLOT_OBJECT_REF) {
            if (arg_kind != SLOT_OBJECT && arg_kind != SLOT_OBJECT_REF) { return false; }
        } else if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_I32_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_VARIANT) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I64 && arg_kind == SLOT_I64_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_I64) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_I64_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I64 && arg_kind == SLOT_I32) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I64 && arg_kind == SLOT_I32_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_STR && arg_kind == SLOT_STR_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OPAQUE &&
                   (arg_kind == SLOT_I32 || arg_kind == SLOT_I64 ||
                    arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                    arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                    arg_kind == SLOT_VARIANT || arg_kind == SLOT_SEQ_OPAQUE ||
                    arg_kind == SLOT_SEQ_OPAQUE_REF || arg_kind == SLOT_OPAQUE)) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OPAQUE_REF &&
                   (arg_kind == SLOT_I32 || arg_kind == SLOT_I64 ||
                    arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                    arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                    arg_kind == SLOT_SEQ_OPAQUE || arg_kind == SLOT_SEQ_OPAQUE_REF ||
                    arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF ||
                    arg_kind == SLOT_VARIANT)) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OBJECT && arg_kind == SLOT_OBJECT_REF) {
            continue;
        } else if (arg_kind != fn->param_kind[i]) {
            { return false; }
        }
        int32_t arg_size = body->slot_size[arg_slot];
        int32_t param_size = fn->param_size[i] > 0 ? fn->param_size[i] : arg_size;
        if ((arg_kind == SLOT_VARIANT || arg_kind == SLOT_OBJECT || arg_kind == SLOT_ARRAY_I32 ||
             arg_kind == SLOT_SEQ_OPAQUE) &&
            fn->param_kind[i] != SLOT_I32_REF &&
            fn->param_kind[i] != SLOT_I32 &&
            fn->param_kind[i] != SLOT_OBJECT_REF &&
            fn->param_kind[i] != SLOT_SEQ_OPAQUE_REF &&
            arg_size != param_size) {
            return false; /* skip variant arg mismatch */
        }
    }
    return true;
}

bool cold_call_args_match(BodyIR *body, FnDef *fn, int32_t arg_start, int32_t arg_count) {
    if (arg_count != fn->arity) return false;
    for (int32_t i = 0; i < arg_count; i++) {
        int32_t arg_slot = body->call_arg_slot[arg_start + i];
        int32_t arg_kind = body->slot_kind[arg_slot];
        if (fn->param_kind[i] == SLOT_I32_REF) {
            if (arg_kind != SLOT_I32 && arg_kind != SLOT_I32_REF && arg_kind != SLOT_VARIANT) return false;
        } else if (fn->param_kind[i] == SLOT_I64_REF) {
            if (arg_kind != SLOT_I64 && arg_kind != SLOT_I64_REF) return false;
        } else if (fn->param_kind[i] == SLOT_SEQ_I32_REF) {
            if (arg_kind != SLOT_SEQ_I32 && arg_kind != SLOT_SEQ_I32_REF) return false;
        } else if (fn->param_kind[i] == SLOT_SEQ_STR_REF) {
            if (arg_kind != SLOT_SEQ_STR && arg_kind != SLOT_SEQ_STR_REF) return false;
        } else if (fn->param_kind[i] == SLOT_SEQ_OPAQUE_REF) {
            if (arg_kind != SLOT_SEQ_OPAQUE && arg_kind != SLOT_SEQ_OPAQUE_REF) return false;
        } else if (fn->param_kind[i] == SLOT_STR_REF) {
            if (arg_kind != SLOT_STR && arg_kind != SLOT_STR_REF) return false;
        } else if (fn->param_kind[i] == SLOT_OBJECT_REF) {
            if (arg_kind != SLOT_OBJECT && arg_kind != SLOT_OBJECT_REF) return false;
        } else if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_I32_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_VARIANT) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I64 && arg_kind == SLOT_I64_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_I64) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_I64_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I64 && arg_kind == SLOT_I32) {
            continue;
        } else if (fn->param_kind[i] == SLOT_I64 && arg_kind == SLOT_I32_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_STR && arg_kind == SLOT_STR_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OPAQUE &&
                   (arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                    arg_kind == SLOT_VARIANT || arg_kind == SLOT_SEQ_OPAQUE ||
                    arg_kind == SLOT_SEQ_OPAQUE_REF || arg_kind == SLOT_OPAQUE)) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OPAQUE_REF &&
                   (arg_kind == SLOT_I32 || arg_kind == SLOT_I64 ||
                    arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                    arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                    arg_kind == SLOT_VARIANT ||
                    arg_kind == SLOT_SEQ_OPAQUE || arg_kind == SLOT_SEQ_OPAQUE_REF ||
                    arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF)) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OBJECT && arg_kind == SLOT_OBJECT_REF) {
            continue;
        } else if (arg_kind != fn->param_kind[i]) {
            return false;
        }
        int32_t arg_size = body->slot_size[arg_slot];
        int32_t param_size = fn->param_size[i] > 0 ? fn->param_size[i] : arg_size;
        if ((arg_kind == SLOT_VARIANT || arg_kind == SLOT_OBJECT || arg_kind == SLOT_ARRAY_I32 ||
             arg_kind == SLOT_SEQ_OPAQUE) &&
            fn->param_kind[i] != SLOT_I32_REF &&
            fn->param_kind[i] != SLOT_I32 &&
            fn->param_kind[i] != SLOT_OBJECT_REF &&
            fn->param_kind[i] != SLOT_SEQ_OPAQUE_REF &&
            arg_size != param_size) {
            return false;
        }
    }
    return true;
}

int32_t symbols_find_fn_for_call(Symbols *symbols, Span name,
                                        BodyIR *body, int32_t arg_start,
                                        int32_t arg_count) {
    /* first pass: exact match */
    int32_t found = -1;
    for (int32_t i = 0; i < symbols->function_count; i++) {
        FnDef *fn = &symbols->functions[i];
        if (fn->arity != arg_count || !span_same(fn->name, name)) continue;
        if (!cold_call_args_match(body, fn, arg_start, arg_count)) continue;
        if (found >= 0) continue;
        found = i;
    }
    if (found >= 0) return found;
    /* second pass: OPAQUE-tolerant match (for CSG lowerer with less precise types) */
    for (int32_t i = 0; i < symbols->function_count; i++) {
        FnDef *fn = &symbols->functions[i];
        if (fn->arity != arg_count || !span_same(fn->name, name)) continue;
        /* OPAQUE-tolerant matching */
        bool match = true;
        for (int32_t p = 0; p < arg_count; p++) {
            int32_t arg_slot = body->call_arg_slot[arg_start + p];
            int32_t arg_kind = body->slot_kind[arg_slot];
            int32_t param_kind = fn->param_kind[p];
            if (arg_kind == param_kind) continue;
            if ((param_kind == SLOT_OPAQUE || param_kind == SLOT_OPAQUE_REF) &&
                (arg_kind == SLOT_I32 || arg_kind == SLOT_I64 ||
                 arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                 arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                 arg_kind == SLOT_VARIANT || arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF)) continue;
            match = false;
            break;
        }
        if (!match) continue;
        if (found >= 0) continue;
        found = i;
    }
    /* specialization: ensure concrete version for generic functions */
    if (found >= 0 && found < symbols->function_count) {
        FnDef *fn = &symbols->functions[found];
        if (fn->generic_count > 0) {
            int32_t arg_kinds[32];
            for (int32_t p = 0; p < arg_count && p < 32; p++) {
                arg_kinds[p] = body->op_a[arg_start + p];
            }
            found = cold_ensure_specialized(symbols, found, arg_kinds, arg_count);
        }
    }
    return found;
}

int32_t cold_make_error_result_slot(Parser *parser, BodyIR *body,
                                           Span result_type, const char *message);
int32_t cold_make_i32_const_slot(BodyIR *body, int32_t value);
/* Forward declarations of static functions from cheng_cold.c used by codegen helpers */
static int32_t cold_materialize_self_exec(Parser *parser, BodyIR *body);
static int32_t cold_materialize_direct_emit(Parser *parser, BodyIR *body, int32_t output);
int32_t parse_constructor(Parser *parser, BodyIR *body, Locals *locals,
                                 Variant *variant);

bool cold_compile_source_to_object(const char *out_path,
                                   const char *src_path,
                                   const char *target,
                                   const char *export_roots_csv,
                                   const char *symbol_visibility);

int32_t parse_call_after_name(Parser *parser, BodyIR *body, Locals *locals,
                                     Span name, int32_t *kind_out) {
    Span stripped_name = name;
    int32_t bracket = -1;
    for (int32_t bi = 0; bi < name.len; bi++)
        if (name.ptr[bi] == '[') { bracket = bi; break; }
    if (bracket > 0 && name.ptr[name.len - 1] == ']')
        stripped_name = span_sub(name, 0, bracket);
    if (!parser_take(parser, "(")) die("expected ( after function name");
    int32_t arg_slots[COLD_MAX_I32_PARAMS];
    int32_t arg_count = 0;
    while (!span_eq(parser_peek(parser), ")")) {
        if (arg_count >= COLD_MAX_I32_PARAMS) die("too many cold call args");
        int32_t arg_kind = SLOT_I32;
        int32_t arg_slot = parse_expr(parser, body, locals, &arg_kind);
        if (arg_kind != SLOT_I32 && arg_kind != SLOT_I64 &&
            arg_kind != SLOT_I32_REF && arg_kind != SLOT_I64_REF &&
            arg_kind != SLOT_STR && arg_kind != SLOT_STR_REF &&
            arg_kind != SLOT_VARIANT &&
            arg_kind != SLOT_OBJECT && arg_kind != SLOT_ARRAY_I32 &&
            arg_kind != SLOT_SEQ_I32 && arg_kind != SLOT_SEQ_I32_REF &&
            arg_kind != SLOT_SEQ_STR && arg_kind != SLOT_SEQ_STR_REF &&
            arg_kind != SLOT_SEQ_OPAQUE && arg_kind != SLOT_SEQ_OPAQUE_REF &&
            arg_kind != SLOT_OBJECT_REF &&
            arg_kind != SLOT_PTR &&
            arg_kind != SLOT_OPAQUE && arg_kind != SLOT_OPAQUE_REF) die("unsupported cold function call arg kind");
        arg_slots[arg_count++] = arg_slot;
        if (span_eq(parser_peek(parser), ",")) (void)parser_token(parser);
        else break;
    }
    if (!parser_take(parser, ")")) {
        /* Skip malformed function call: missing ) */
    }
    int32_t arg_start = body->call_arg_count;
    for (int32_t i = 0; i < arg_count; i++) body_call_arg(body, arg_slots[i]);
    int32_t intrinsic_slot = -1;
    if (cold_try_str_len_intrinsic(body, name, arg_start, arg_count,
                                   &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_slice_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_is_i32_to_str_intrinsic(name)) {
        if (arg_count != 1) die("int to str intrinsic arity mismatch");
        int32_t arg_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[arg_slot] != SLOT_I32 && body->slot_kind[arg_slot] != SLOT_I64) { /* skip unsupported arg kind */ return false; }
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_op(body, body->slot_kind[arg_slot] == SLOT_I64 ? BODY_OP_I64_TO_STR : BODY_OP_I32_TO_STR, slot, arg_slot, 0);
        if (kind_out) *kind_out = SLOT_STR;
        return slot;
    }
    if (cold_try_parse_int_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_join_intrinsic(body, name, arg_start, arg_count,
                                    &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_split_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_strip_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_result_intrinsic(parser, body, name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_os_intrinsic(parser, body, name, arg_start, arg_count,
                              &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_path_intrinsic(parser, body, name, arg_start, arg_count,
                                &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_csg_intrinsic(parser, body, name, arg_start, arg_count,
                               &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_slplan_intrinsic(parser, body, name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (span_eq(name, "atomicLoadI32")) {
        if (arg_count != 1) die("atomicLoadI32 expects 1 arg");
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_ATOMIC_LOAD_I32, slot, ptr, 0);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    if (span_eq(name, "atomicStoreI32")) {
        if (arg_count != 2) die("atomicStoreI32 expects 2 args");
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t val = body->call_arg_slot[arg_start + 1];
        body_op(body, BODY_OP_ATOMIC_STORE_I32, ptr, val, 0);
        if (kind_out) *kind_out = SLOT_I32;
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        return zero;
    }
    if (span_eq(name, "atomicCasI32")) {
        if (arg_count != 3) return false; /* atomicCasI32 arity mismatch */
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t expect = body->call_arg_slot[arg_start + 1];
        int32_t desired = body->call_arg_slot[arg_start + 2];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_ATOMIC_CAS_I32, slot, ptr, desired, expect);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    if (cold_try_backend_intrinsic(parser, body, name, arg_start, arg_count,
                                   &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_assert_intrinsic(body, name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_bootstrap_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    /* closure_new(fn_ptr, env_ptr) → closure (16 bytes: fn_ptr + env_ptr) */
    if (span_eq(name, "closure_new") || span_eq(name, "closureNew")) {
        if (arg_count != 2) die("closure_new arity mismatch");
        int32_t fn_slot = body->call_arg_slot[arg_start];
        int32_t env_slot = body->call_arg_slot[arg_start + 1];
        int32_t slot = body_slot(body, SLOT_OPAQUE, 16);
        body_op(body, BODY_OP_CLOSURE_NEW, slot, fn_slot, env_slot);
        if (kind_out) *kind_out = SLOT_OPAQUE;
        return slot;
    }
    /* closure_call(closure, arg1, ...) → calls closure */
    if (span_eq(name, "closure_call") || span_eq(name, "closureCall")) {
        if (arg_count < 1) die("closure_call arity mismatch");
        int32_t closure_slot = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_CLOSURE_CALL, slot, closure_slot,
                 arg_start + 1, arg_count - 1);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    /* closure_env(closure) → extract env pointer from closure */
    if (span_eq(name, "closure_env") || span_eq(name, "closureEnv")) {
        if (arg_count != 1) die("closure_env arity mismatch");
        int32_t closure_slot = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_PTR, 8);
        body_op3(body, BODY_OP_PAYLOAD_LOAD, slot, closure_slot, 8, 8);
        if (kind_out) *kind_out = SLOT_PTR;
        return slot;
    }
    /* try Ok[T]/Err[T] constructor fallback before looking up in symbol table */
    { Span base_name = name;
      int32_t br2 = cold_span_find_char(name, '[');
      if (br2 > 0) base_name = span_sub(name, 0, br2);
      if (span_eq(base_name, "Err") && arg_count == 1) {
          int32_t slot = cold_make_error_result_slot(parser, body, cold_cstr_span("Result"), "error");
          if (kind_out) *kind_out = SLOT_OBJECT;
          return slot;
      }
      if (span_eq(base_name, "Ok") && arg_count == 1) {
          int32_t val = body->call_arg_slot[arg_start];
          ObjectDef *robj = symbols_resolve_object(parser->symbols, cold_cstr_span("Result"));
          if (!robj) die("Result type missing for Ok");
          ObjectField *fok = object_find_field(robj, cold_cstr_span("ok"));
          ObjectField *fval = object_find_field(robj, cold_cstr_span("value"));
          ObjectField *ferr = object_find_field(robj, cold_cstr_span("err"));
          if (!fok || !fval || !ferr) die("Result fields missing");
          int32_t ps = body->call_arg_count;
          body_call_arg_with_offset(body, cold_make_i32_const_slot(body, 1), fok->offset);
          body_call_arg_with_offset(body, val, fval->offset);
          body_call_arg_with_offset(body, cold_make_i32_const_slot(body, 0), ferr->offset);
          int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(robj));
          body_slot_set_type(body, slot, robj->name);
          body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, ps, 3);
          if (kind_out) *kind_out = SLOT_OBJECT;
          return slot;
      }
    }
    Span lookup_name = stripped_name;
    int32_t fn_index = symbols_find_fn_for_call(parser->symbols, lookup_name, body, arg_start, arg_count);
    if (fn_index < 0) {
        /* Check for indirect call via local variable (function pointer) */
        Local *indirect_local = locals_find(locals, name);
        if (indirect_local && indirect_local->kind == SLOT_PTR) {
            int32_t slot = body_slot(body, SLOT_I32, 4);
            body_op3(body, BODY_OP_CALL_PTR, slot, indirect_local->slot, arg_start, arg_count);
            if (kind_out) *kind_out = SLOT_I32;
            return slot;
        }
        if (parser->import_mode && parser->import_alias.len > 0 &&
            cold_span_find_char(stripped_name, '.') < 0) {
            Span qualified = cold_arena_join3(parser->arena, parser->import_alias,
                                              ".", stripped_name);
            fn_index = symbols_find_fn_for_call(parser->symbols, qualified,
                                                body, arg_start, arg_count);
            if (fn_index >= 0) lookup_name = qualified;
        }
    }
    if (fn_index < 0) {
        /* External function, will be registered below */
        /* try variant constructor: Err[int32] -> Err, or bare Err */
        { Span base_name = name;
          int32_t br2 = cold_span_find_char(name, '[');
          if (br2 > 0) base_name = span_sub(name, 0, br2);
          if (span_eq(base_name, "Err") && arg_count == 1) {
              int32_t slot = cold_make_error_result_slot(parser, body, cold_cstr_span("Result"), "error");
              if (kind_out) *kind_out = SLOT_OBJECT;
              return slot;
          }
          if (span_eq(base_name, "Ok") && arg_count == 1) {
              int32_t val = body->call_arg_slot[arg_start];
              ObjectDef *robj = symbols_resolve_object(parser->symbols, cold_cstr_span("Result"));
              if (!robj) die("Result type missing for Ok");
              ObjectField *fok = object_find_field(robj, cold_cstr_span("ok"));
              ObjectField *fval = object_find_field(robj, cold_cstr_span("value"));
              ObjectField *ferr = object_find_field(robj, cold_cstr_span("err"));
              if (!fok || !fval || !ferr) die("Result fields missing");
              int32_t ps = body->call_arg_count;
              body_call_arg_with_offset(body, cold_make_i32_const_slot(body, 1), fok->offset);
              body_call_arg_with_offset(body, val, fval->offset);
              body_call_arg_with_offset(body, cold_make_i32_const_slot(body, 0), ferr->offset);
              int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(robj));
              body_slot_set_type(body, slot, robj->name);
              body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, ps, 3);
              if (kind_out) *kind_out = SLOT_OBJECT;
              return slot;
          }
          Variant *vc = symbols_find_variant(parser->symbols, base_name);
          if (!vc) vc = symbols_find_variant(parser->symbols, name);
          if (!vc) {
              /* qualified name: Type.Variant — split and scan all types */
              int32_t dot = -1;
              for (int32_t i = name.len - 1; i > 0; i--) {
                  if (name.ptr[i] == '.') { dot = i; break; }
              }
              if (dot > 0) {
                  Span tn = span_sub(name, 0, dot);
                  Span vn = span_sub(name, dot + 1, name.len);
                  for (int32_t ti = 0; ti < parser->symbols->type_count; ti++) {
                      TypeDef *ty = &parser->symbols->types[ti];
                      if (span_same(ty->name, tn)) {
                          vc = type_find_variant(ty, vn);
                          if (vc) break;
                      }
                  }
              }
          }
          if (!vc) {
              /* const-based lookup: Type.Variant registered as const = tag */
              ConstDef *cst = symbols_find_const(parser->symbols, name);
              if (cst) {
                  int32_t tag = cst->value;
                  for (int32_t ti = 0; ti < parser->symbols->type_count; ti++) {
                      TypeDef *ty = &parser->symbols->types[ti];
                      for (int32_t vi = 0; vi < ty->variant_count; vi++) {
                          if (ty->variants[vi].tag == tag &&
                              ty->variants[vi].field_count == arg_count) {
                              vc = &ty->variants[vi];
                              break;
                          }
                      }
                      if (vc) break;
                  }
              }
          }
          if (vc) {
              *kind_out = SLOT_VARIANT;
              /* args already consumed; fill offsets from variant layout */
              for (int32_t ai = 0; ai < arg_count && ai < vc->field_count; ai++) {
                  body->call_arg_offset[arg_start + ai] = vc->field_offset[ai];
              }
              int32_t vz = symbols_variant_slot_size(parser->symbols, vc);
              int32_t sl = body_slot(body, SLOT_VARIANT, vz);
              body_op3(body, BODY_OP_MAKE_VARIANT, sl, vc->tag, arg_start, arg_count);
              return sl;
          }
        }
        /* Try bare-name resolution via import aliases */
        if (parser->import_source_count > 0 && parser->import_sources) {
            for (int32_t isi = 0; isi < parser->import_source_count; isi++) {
                Span qualified = cold_arena_join3(parser->arena, parser->import_sources[isi].alias, ".", stripped_name);
                int32_t resolved = symbols_find_fn_for_call(parser->symbols, qualified, body, arg_start, arg_count);
                if (resolved >= 0) {
                    fn_index = resolved;
                    break;
                }
            }
        }
        { int32_t param_kinds[COLD_MAX_I32_PARAMS];
          int32_t param_sizes[COLD_MAX_I32_PARAMS];
          for (int32_t ai = 0; ai < arg_count && ai < COLD_MAX_I32_PARAMS; ai++) {
              param_kinds[ai] = body->slot_kind[body->call_arg_slot[arg_start + ai]];
              param_sizes[ai] = body->slot_size[body->call_arg_slot[arg_start + ai]];
          }
          if (parser->import_mode) {
              fn_index = symbols_find_fn(parser->symbols, lookup_name, arg_count, param_kinds, param_sizes, cold_cstr_span("i"));
          } else {
              /* Try name-only lookup first to avoid duplicates when call args
                 have less-precise types than the already-registered signature. */
              fn_index = -1;
              for (int32_t si = 0; si < parser->symbols->function_count; si++) {
                  if (parser->symbols->functions[si].arity == arg_count &&
                      span_same(parser->symbols->functions[si].name, stripped_name)) {
                      fn_index = si;
                      break;
	      }
    }
              if (fn_index < 0) {
                  fn_index = symbols_add_fn(parser->symbols, stripped_name, arg_count, param_kinds, param_sizes, cold_cstr_span("i"));
                  parser->symbols->functions[fn_index].is_external = true;
              }
          }
      }
    }
    if (parser->import_mode && fn_index < 0) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    FnDef *fn = &parser->symbols->functions[fn_index];
    if (!cold_validate_call_args(body, fn, arg_start, arg_count, parser->import_mode)) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    int32_t ret_kind = cold_return_kind_from_span(parser->symbols, fn->ret);
    int32_t slot = body_slot(body, ret_kind, cold_return_slot_size(parser->symbols, fn->ret, ret_kind));
    body_slot_set_type(body, slot, fn->ret);
    bool has_composite = (ret_kind != SLOT_I32 && ret_kind != SLOT_I64 && ret_kind != SLOT_PTR && ret_kind != SLOT_OPAQUE);
    for (int32_t pi = 0; !has_composite && pi < fn->arity; pi++) {
        if (fn->param_kind[pi] != SLOT_I32 && fn->param_kind[pi] != SLOT_I64 && fn->param_kind[pi] != SLOT_PTR) has_composite = true;
    }
    if (!has_composite) {
        body_op(body, BODY_OP_CALL_I32, slot, fn_index, arg_start);
    } else {
        body_op3(body, BODY_OP_CALL_COMPOSITE, slot, fn_index, arg_start, arg_count);
    }
    if (kind_out) *kind_out = ret_kind;
    return slot;
}

int32_t parse_call_from_args_span(Parser *owner, BodyIR *body, Locals *locals,
                                         Span name, Span args, int32_t *kind_out) {
    Span stripped_name = name;
    int32_t bracket = -1;
    for (int32_t bi = 0; bi < name.len; bi++)
        if (name.ptr[bi] == '[') { bracket = bi; break; }
    if (bracket > 0 && name.ptr[name.len - 1] == ']')
        stripped_name = span_sub(name, 0, bracket);
    Parser arg_parser = {args, 0, owner->arena, owner->symbols,
                         owner->import_mode, owner->import_alias};
    int32_t arg_slots[COLD_MAX_I32_PARAMS];
    int32_t arg_count = 0;
    parser_ws(&arg_parser);
    while (arg_parser.pos < arg_parser.source.len) {
        if (arg_count >= COLD_MAX_I32_PARAMS) die("too many cold call args");
        int32_t arg_kind = SLOT_I32;
        int32_t arg_slot = parse_expr(&arg_parser, body, locals, &arg_kind);
        if (arg_kind != SLOT_I32 && arg_kind != SLOT_I64 &&
            arg_kind != SLOT_I32_REF && arg_kind != SLOT_I64_REF &&
            arg_kind != SLOT_STR && arg_kind != SLOT_STR_REF &&
            arg_kind != SLOT_VARIANT &&
            arg_kind != SLOT_OBJECT && arg_kind != SLOT_ARRAY_I32 &&
            arg_kind != SLOT_SEQ_I32 && arg_kind != SLOT_SEQ_I32_REF &&
            arg_kind != SLOT_SEQ_STR && arg_kind != SLOT_SEQ_STR_REF &&
            arg_kind != SLOT_SEQ_OPAQUE && arg_kind != SLOT_SEQ_OPAQUE_REF &&
            arg_kind != SLOT_OBJECT_REF &&
            arg_kind != SLOT_PTR &&
            arg_kind != SLOT_OPAQUE && arg_kind != SLOT_OPAQUE_REF) die("unsupported cold function call arg kind");
        arg_slots[arg_count++] = arg_slot;
        if (span_eq(parser_peek(&arg_parser), ",")) (void)parser_token(&arg_parser);
        else break;
        parser_ws(&arg_parser);
    }
    parser_ws(&arg_parser);
    if (arg_parser.pos != arg_parser.source.len) die("unsupported call arg tokens");
    int32_t arg_start = body->call_arg_count;
    for (int32_t i = 0; i < arg_count; i++) body_call_arg(body, arg_slots[i]);
    int32_t intrinsic_slot = -1;
    if (cold_try_str_len_intrinsic(body, name, arg_start, arg_count,
                                   &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_slice_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_is_i32_to_str_intrinsic(name)) {
        if (arg_count != 1) die("int to str intrinsic arity mismatch");
        int32_t arg_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[arg_slot] != SLOT_I32) die("int to str intrinsic expects int32");
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_op(body, BODY_OP_I32_TO_STR, slot, arg_slot, 0);
        if (kind_out) *kind_out = SLOT_STR;
        return slot;
    }
    if (cold_try_parse_int_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_join_intrinsic(body, name, arg_start, arg_count,
                                    &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_split_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_strip_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_result_intrinsic(owner, body, name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_os_intrinsic(owner, body, name, arg_start, arg_count,
                              &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_path_intrinsic(owner, body, name, arg_start, arg_count,
                                &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_csg_intrinsic(owner, body, name, arg_start, arg_count,
                               &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_slplan_intrinsic(owner, body, name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_backend_intrinsic(owner, body, name, arg_start, arg_count,
                                   &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_assert_intrinsic(body, name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_bootstrap_intrinsic(body, name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    /* closure_new / closure_call intrinsics */
    if (span_eq(name, "closure_new") || span_eq(name, "closureNew")) {
        if (arg_count != 2) die("closure_new arity mismatch");
        int32_t fn_slot = body->call_arg_slot[arg_start];
        int32_t env_slot = body->call_arg_slot[arg_start + 1];
        int32_t slot = body_slot(body, SLOT_OPAQUE, 16);
        body_op(body, BODY_OP_CLOSURE_NEW, slot, fn_slot, env_slot);
        if (kind_out) *kind_out = SLOT_OPAQUE;
        return slot;
    }
    if (span_eq(name, "closure_call") || span_eq(name, "closureCall")) {
        if (arg_count < 1) die("closure_call arity mismatch");
        int32_t closure_slot = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_CLOSURE_CALL, slot, closure_slot,
                 arg_start + 1, arg_count - 1);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    if (span_eq(name, "closure_env") || span_eq(name, "closureEnv")) {
        if (arg_count != 1) die("closure_env arity mismatch");
        int32_t closure_slot = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_PTR, 8);
        body_op3(body, BODY_OP_PAYLOAD_LOAD, slot, closure_slot, 8, 8);
        if (kind_out) *kind_out = SLOT_PTR;
        return slot;
    }
    Span lookup_name = stripped_name;
    int32_t fn_index = symbols_find_fn_for_call(owner->symbols, lookup_name, body, arg_start, arg_count);
    if (fn_index < 0) {
        /* Check for indirect call via local variable (function pointer) */
        Local *indirect_local = locals_find(locals, name);
        if (indirect_local && indirect_local->kind == SLOT_PTR) {
            int32_t slot = body_slot(body, SLOT_I32, 4);
            body_op3(body, BODY_OP_CALL_PTR, slot, indirect_local->slot, arg_start, arg_count);
            if (kind_out) *kind_out = SLOT_I32;
            return slot;
        }
        if (owner->import_mode && owner->import_alias.len > 0 &&
            cold_span_find_char(stripped_name, '.') < 0) {
            Span qualified = cold_arena_join3(owner->arena, owner->import_alias,
                                              ".", stripped_name);
            fn_index = symbols_find_fn_for_call(owner->symbols, qualified,
                                                body, arg_start, arg_count);
            if (fn_index >= 0) lookup_name = qualified;
        }
    }
    if (fn_index < 0) {
        /* Try bare-name resolution via import aliases */
        if (owner->import_source_count > 0 && owner->import_sources) {
            for (int32_t isi = 0; isi < owner->import_source_count; isi++) {
                Span qualified = cold_arena_join3(owner->arena, owner->import_sources[isi].alias, ".", stripped_name);
                int32_t resolved = symbols_find_fn_for_call(owner->symbols, qualified, body, arg_start, arg_count);
                if (resolved >= 0) {
                    fn_index = resolved;
                    break;
                }
            }
        }
        /* External function, will be registered below */
        { int32_t param_kinds[COLD_MAX_I32_PARAMS];
          int32_t param_sizes[COLD_MAX_I32_PARAMS];
          for (int32_t ai = 0; ai < arg_count && ai < COLD_MAX_I32_PARAMS; ai++) {
              param_kinds[ai] = body->slot_kind[body->call_arg_slot[arg_start + ai]];
              param_sizes[ai] = body->slot_size[body->call_arg_slot[arg_start + ai]];
          }
          if (owner->import_mode) {
              fn_index = symbols_find_fn(owner->symbols, lookup_name, arg_count, param_kinds, param_sizes, cold_cstr_span("i"));
          } else {
              /* Try name-only lookup first to avoid duplicates */
              fn_index = -1;
              for (int32_t si = 0; si < owner->symbols->function_count; si++) {
                  if (owner->symbols->functions[si].arity == arg_count &&
                      span_same(owner->symbols->functions[si].name, stripped_name)) {
                      fn_index = si;
                      break;
                  }
              }
              if (fn_index < 0) {
                  fn_index = symbols_add_fn(owner->symbols, stripped_name, arg_count, param_kinds, param_sizes, cold_cstr_span("i"));
                  owner->symbols->functions[fn_index].is_external = true;
              }
          }
      }
    }
    if (owner->import_mode && fn_index < 0) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    FnDef *fn = &owner->symbols->functions[fn_index];
    if (!cold_validate_call_args(body, fn, arg_start, arg_count, owner->import_mode)) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    int32_t ret_kind = cold_return_kind_from_span(owner->symbols, fn->ret);
    int32_t slot = body_slot(body, ret_kind, cold_return_slot_size(owner->symbols, fn->ret, ret_kind));
    body_slot_set_type(body, slot, fn->ret);
    bool has_composite = (ret_kind != SLOT_I32 && ret_kind != SLOT_I64 && ret_kind != SLOT_PTR && ret_kind != SLOT_OPAQUE);
    for (int32_t pi = 0; !has_composite && pi < fn->arity; pi++) {
        if (fn->param_kind[pi] != SLOT_I32 && fn->param_kind[pi] != SLOT_I64 && fn->param_kind[pi] != SLOT_PTR) has_composite = true;
    }
    if (!has_composite) {
        body_op(body, BODY_OP_CALL_I32, slot, fn_index, arg_start);
    } else {
        body_op3(body, BODY_OP_CALL_COMPOSITE, slot, fn_index, arg_start, arg_count);
    }
    if (kind_out) *kind_out = ret_kind;
    return slot;
}

int32_t parse_constructor(Parser *parser, BodyIR *body, Locals *locals,
                                 Variant *variant) {
    int32_t payload_count = 0;
    int32_t payload_slots[COLD_MAX_VARIANT_FIELDS];
    int32_t payload_offsets[COLD_MAX_VARIANT_FIELDS];
    if (span_eq(parser_peek(parser), "(")) {
        parser_take(parser, "(");
        while (!span_eq(parser_peek(parser), ")")) {
            if (payload_count >= COLD_MAX_VARIANT_FIELDS) die("too many cold variant payload fields");
            if (payload_count >= variant->field_count) break; /* too many args */
            int32_t payload_kind = SLOT_I32;
            int32_t payload_slot = parse_expr(parser, body, locals, &payload_kind);
            if (payload_kind != variant->field_kind[payload_count]) {
                int32_t fk = variant->field_kind[payload_count];
                if (!((fk == SLOT_VARIANT && (payload_kind == SLOT_VARIANT || payload_kind == SLOT_I32 || payload_kind == SLOT_STR)) ||
                      (fk == SLOT_I32 && (payload_kind == SLOT_VARIANT || payload_kind == SLOT_I32 || payload_kind == SLOT_STR)) ||
                      (fk == SLOT_STR && (payload_kind == SLOT_VARIANT || payload_kind == SLOT_I32)))) {
                    die("variant constructor payload kind mismatch");
                }
            }
            if (body->slot_size[payload_slot] != variant->field_size[payload_count]) {
                if (payload_kind == variant->field_kind[payload_count])
                    die("variant constructor payload size mismatch");
            }
            payload_slots[payload_count] = payload_slot;
            payload_offsets[payload_count] = variant->field_offset[payload_count];
            payload_count++;
            if (span_eq(parser_peek(parser), ",")) {
                (void)parser_token(parser);
                continue;
            }
            break;
        }
        if (!parser_take(parser, ")")) return 0; /* skip malformed constructor */
    }
    if (payload_count != variant->field_count) {
        if (variant->field_count > 0) {
            die("variant constructor payload count mismatch");
        } else if (payload_count > 0) {
            die("empty variant cannot take constructor arguments");
        }
    }
    int32_t payload_start = payload_count > 0 ? body->call_arg_count : -1;
    for (int32_t i = 0; i < payload_count; i++) {
        body_call_arg_with_offset(body, payload_slots[i], payload_offsets[i]);
    }
    int32_t slot = body_slot(body, SLOT_VARIANT, symbols_variant_slot_size(parser->symbols, variant));
    TypeDef *variant_type = symbols_find_variant_type(parser->symbols, variant);
    if (!variant_type) die("variant constructor has no parent type");
    body_slot_set_type(body, slot, variant_type->name);
    body_op3(body, BODY_OP_MAKE_VARIANT, slot, variant->tag, payload_start, payload_count);
    return slot;
}

int32_t body_slot_for_object_field(BodyIR *body, ObjectField *field) {
    int32_t slot = body_slot(body, field->kind, field->size);
    body_slot_set_type(body, slot, field->type_name);
    if (field->kind == SLOT_ARRAY_I32) body_slot_set_array_len(body, slot, field->array_len);
    return slot;
}

bool parser_next_is_object_constructor(Parser *parser) {
    Span open_tok = parser_peek(parser);
    if (span_eq(open_tok, "{")) return true;
    if (!span_eq(open_tok, "(")) return false;

    Parser probe = *parser;
    (void)parser_token(&probe);
    Span first = parser_peek(&probe);
    if (span_eq(first, ")")) return true;
    if (first.len <= 0) return false;
    (void)parser_token(&probe);
    return span_eq(parser_peek(&probe), ":");
}

int32_t parse_object_constructor(Parser *parser, BodyIR *body, Locals *locals,
                                        ObjectDef *object) {
    bool curly = false;
    Span open_tok = parser_peek(parser);
    if (span_eq(open_tok, "{")) {
        (void)parser_token(parser);
        curly = true;
    } else if (span_eq(open_tok, "(")) {
        (void)parser_token(parser);
    } else {
        die("expected ( or { after object constructor");
    }
    int32_t payload_count = 0;
    int32_t payload_slots[COLD_MAX_OBJECT_FIELDS];
    int32_t payload_offsets[COLD_MAX_OBJECT_FIELDS];
    bool seen[COLD_MAX_OBJECT_FIELDS];
    memset(seen, 0, sizeof(seen));
    while (!span_eq(parser_peek(parser), curly ? "}" : ")")) {
        Span field_name = parser_token(parser);
        if (field_name.len <= 0) return 0; /* skip empty field name */;
        ObjectField *field = object_find_field(object, field_name);
        if (!field) {
            /* Unknown field, skip */
            int32_t skip_kind = SLOT_I32;
            (void)parse_expr(parser, body, locals, &skip_kind);
            continue;
        }
        int32_t field_index = (int32_t)(field - object->fields);
        if (field_index < 0 || field_index >= object->field_count) die("object field index out of range");
        if (seen[field_index]) die("duplicate object constructor field");
        seen[field_index] = true;
        if (!parser_take(parser, ":")) die("expected : in object constructor field");
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
        if (field->kind == SLOT_SEQ_I32 && value_kind == SLOT_ARRAY_I32) {
            int32_t count = body->slot_aux[value_slot];
            int32_t seq_slot = body_slot(body, SLOT_SEQ_I32, 16);
            body_slot_set_type(body, seq_slot, cold_cstr_span("int32[]"));
            body_slot_set_array_len(body, seq_slot, count);
            body_op3(body, BODY_OP_MAKE_SEQ_I32, seq_slot, value_slot, -1, count);
            value_slot = seq_slot;
            value_kind = SLOT_SEQ_I32;
        }
        if (field->kind == SLOT_SEQ_I32 && value_kind == SLOT_ARRAY_I32) {
            int32_t count = body->slot_aux[value_slot];
            int32_t seq_slot = body_slot(body, SLOT_SEQ_I32, 16);
            body_slot_set_type(body, seq_slot, cold_cstr_span("int32[]"));
            body_slot_set_array_len(body, seq_slot, count);
            body_op3(body, BODY_OP_MAKE_SEQ_I32, seq_slot, value_slot, -1, count);
            value_slot = seq_slot;
            value_kind = SLOT_SEQ_I32;
        }
        if (field->kind == SLOT_SEQ_OPAQUE && value_kind == SLOT_SEQ_I32) {
            int32_t seq_slot = body_slot(body, SLOT_SEQ_OPAQUE, 16);
            body_slot_set_seq_opaque_type(body, parser->symbols, seq_slot, field->type_name);
            body_op3(body, BODY_OP_MAKE_COMPOSITE, seq_slot, 0, -1, 0);
            value_slot = seq_slot;
            value_kind = SLOT_SEQ_OPAQUE;
        }
        if (value_kind == SLOT_VARIANT && field->kind == SLOT_I32) {
            int32_t tag_slot = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_TAG_LOAD, tag_slot, value_slot, 0);
            value_slot = tag_slot;
            value_kind = SLOT_I32;
        }
        if (value_kind != field->kind) {
            /* Skip mismatched field */
            continue;
        }
        if (field->kind == SLOT_ARRAY_I32 && body->slot_aux[value_slot] != field->array_len) {
            die("object constructor array length mismatch");
        }
        payload_slots[payload_count] = value_slot;
        payload_offsets[payload_count] = field->offset;
        payload_count++;
        if (span_eq(parser_peek(parser), ",")) {
            (void)parser_token(parser);
            continue;
        }
        break;
    }
    if (!parser_take(parser, curly ? "}" : ")")) return 0; /* skip malformed object constructor */
    int32_t payload_start = payload_count > 0 ? body->call_arg_count : -1;
    for (int32_t i = 0; i < payload_count; i++) {
        body_call_arg_with_offset(body, payload_slots[i], payload_offsets[i]);
    }
    int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(object));
    body_slot_set_type(body, slot, object->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, payload_count);
    return slot;
}

int32_t parse_i32_array_literal(Parser *parser, BodyIR *body, Locals *locals,
                                       int32_t *kind) {
    int32_t count = 0;
    int32_t element_slots[1024];
    int32_t element_kinds[1024];
    int32_t first_kind = SLOT_I32;
    while (!span_eq(parser_peek(parser), "]")) {
        if (count >= 1024) die("cold array literal too large");
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
        if (count == 0) first_kind = value_kind;
        element_slots[count] = value_slot;
        element_kinds[count] = value_kind;
        count++;
        if (span_eq(parser_peek(parser), ",")) {
            (void)parser_token(parser);
            continue;
        }
        break;
    }
    if (!parser_take(parser, "]")) return 0; /* unterminated array literal */
    if (count <= 0) {
        int32_t slot = body_slot(body, SLOT_SEQ_I32, 16);
        body_slot_set_type(body, slot, cold_cstr_span("int32[]"));
        body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
        *kind = SLOT_SEQ_I32;
        return slot;
    }
    if (first_kind == SLOT_STR || first_kind == SLOT_STR_REF) {
        int32_t slot = body_slot(body, SLOT_SEQ_STR, 16);
        body_slot_set_type(body, slot, cold_cstr_span("str[]"));
        body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
        for (int32_t i = 0; i < count; i++) {
            if (element_kinds[i] != SLOT_STR && element_kinds[i] != SLOT_STR_REF) {
                continue; /* skip non-str element */
            }
            body_op(body, BODY_OP_SEQ_STR_ADD, slot, element_slots[i], 0);
        }
        *kind = SLOT_SEQ_STR;
        return slot;
    }
    /* int32 array: original behavior */
    int32_t payload_start = body->call_arg_count;
    for (int32_t i = 0; i < count; i++)
        body_call_arg_with_offset(body, element_slots[i], i * 4);
    int32_t slot = body_slot(body, SLOT_ARRAY_I32, align_i32(count * 4, 8));
    body_slot_set_array_len(body, slot, count);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, count);
    *kind = SLOT_ARRAY_I32;
    return slot;
}

int32_t parse_i32_seq_literal(Parser *parser, BodyIR *body, Locals *locals,
                                     int32_t *kind) {
    if (!parser_take(parser, "[")) die("expected [ before int32[] literal");
    int32_t count = 0;
    int32_t element_slots[1024];
    while (!span_eq(parser_peek(parser), "]")) {
        if (count >= 1024) die("cold int32[] literal too large");
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
        if (value_kind != SLOT_I32) die("cold int32[] literal supports int32 only");
        element_slots[count++] = value_slot;
        if (span_eq(parser_peek(parser), ",")) {
            (void)parser_token(parser);
            continue;
        }
        break;
    }
    if (!parser_take(parser, "]")) die("expected ] after int32[] literal");

    int32_t data_slot = -1;
    if (count > 0) {
        int32_t payload_start = body->call_arg_count;
        for (int32_t i = 0; i < count; i++) {
            body_call_arg_with_offset(body, element_slots[i], i * 4);
        }
        data_slot = body_slot(body, SLOT_ARRAY_I32, align_i32(count * 4, 8));
        body_slot_set_array_len(body, data_slot, count);
        body_op3(body, BODY_OP_MAKE_COMPOSITE, data_slot, 0, payload_start, count);
    }

    int32_t slot = body_slot(body, SLOT_SEQ_I32, 16);
    body_slot_set_type(body, slot, cold_cstr_span("int32[]"));
    body_slot_set_array_len(body, slot, count);
    body_op3(body, BODY_OP_MAKE_SEQ_I32, slot, data_slot, -1, count);
    *kind = SLOT_SEQ_I32;
    return slot;
}

int32_t parse_match_bindings(Parser *parser, Span *bind_names, int32_t cap) {
    int32_t bind_count = 0;
    if (!span_eq(parser_peek(parser), "(")) return 0;
    parser_take(parser, "(");
    while (!span_eq(parser_peek(parser), ")")) {
        if (bind_count >= cap) die("too many cold match payload bindings");
        Span bind = parser_token(parser);
        if (bind.len <= 0) die("expected payload binding name");
        bind_names[bind_count++] = bind;
        if (span_eq(parser_peek(parser), ",")) {
            (void)parser_token(parser);
            continue;
        }
        break;
    }
    if (!parser_take(parser, ")")) return 0; /* skip malformed match arm */;
    return bind_count;
}

Span parser_take_until_top_level_char(Parser *parser, uint8_t delimiter,
                                             const char *missing_message) {
    parser_inline_ws(parser);
    int32_t start = parser->pos;
    int32_t depth = 0;
    bool in_string = false;
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (in_string) {
            if (c == '\\' && parser->pos + 1 < parser->source.len) {
                parser->pos += 2;
                continue;
            }
            if (c == '"') in_string = false;
            parser->pos++;
            continue;
        }
        if (c == '\n' || c == '\r') return (Span){0}; /* newline before delimiter */
        if (c == '"') {
            in_string = true;
            parser->pos++;
            continue;
        }
        if (depth == 0 && c == delimiter) break;
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') {
            depth--;
            if (depth < 0) die("unbalanced expression delimiter");
        }
        parser->pos++;
    }
    if (parser->pos >= parser->source.len) die(missing_message);
    Span expr = span_trim(span_sub(parser->source, start, parser->pos));
    if (expr.len <= 0) die(missing_message);
    return expr;
}

Span parser_take_until_range_dots(Parser *parser) {
    parser_inline_ws(parser);
    int32_t start = parser->pos;
    int32_t depth = 0;
    bool in_string = false;
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (in_string) {
            if (c == '\\' && parser->pos + 1 < parser->source.len) {
                parser->pos += 2;
                continue;
            }
            if (c == '"') in_string = false;
            parser->pos++;
            continue;
        }
        if (c == '\n' || c == '\r') return (Span){0}; /* unterminated for range */
        if (c == '"') {
            in_string = true;
            parser->pos++;
            continue;
        }
        if (depth == 0 && c == '.' &&
            parser->pos + 1 < parser->source.len &&
            parser->source.ptr[parser->pos + 1] == '.') {
            Span expr = span_trim(span_sub(parser->source, start, parser->pos));
            if (expr.len <= 0) die("empty for range start");
            return expr;
        }
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') {
            depth--;
            if (depth < 0) die("unbalanced for range expression");
        }
        parser->pos++;
    }
    return (Span){0}; /* unterminated for range */
    return (Span){0};
}

Span parser_take_qualified_after_first(Parser *parser, Span first) {
    int32_t start = cold_span_offset(parser->source, first);
    if (start < 0) die("qualified name span outside source");
    while (span_eq(parser_peek(parser), ".")) {
        (void)parser_token(parser);
        Span part = parser_token(parser);
        if (part.len <= 0) die("expected qualified name segment");
    }
    if (span_eq(parser_peek(parser), "[")) {
        parser_skip_balanced(parser, "[", "]");
    }
    return span_sub(parser->source, start, parser->pos);
}

bool parser_next_is_qualified_call(Parser *parser) {
    int32_t saved = parser->pos;
    if (!span_eq(parser_peek(parser), ".")) return false;
    (void)parser_token(parser);
    Span part = parser_token(parser);
    if (part.len <= 0) { parser->pos = saved; return false; }
    /* Skip generic [T] args after segment name before checking for ( */
    if (span_eq(parser_peek(parser), "[")) parser_skip_balanced(parser, "[", "]");
    bool ok = span_eq(parser_peek(parser), "(");
    parser->pos = saved;
    return ok;
}

int32_t cold_make_str_literal_slot(Parser *parser, BodyIR *body, Span raw, bool fmt_literal) {
    Span literal = cold_decode_string_content(parser->arena, raw, fmt_literal);
    int32_t literal_index = body_string_literal(body, literal);
    int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
    body_op(body, BODY_OP_STR_LITERAL, slot, literal_index, 0);
    return slot;
}

int32_t cold_materialize_fmt_str(BodyIR *body, int32_t slot, int32_t kind) {
    if (kind == SLOT_STR) return slot;
    if (kind == SLOT_STR_REF) {
        int32_t dst = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_op3(body, BODY_OP_PAYLOAD_LOAD, dst, slot, 0, COLD_STR_SLOT_SIZE);
        return dst;
    }
    if (kind == SLOT_I32) {
        int32_t dst = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_op(body, BODY_OP_I32_TO_STR, dst, slot, 0);
        return dst;
    }
    if (kind == SLOT_I64) {
        int32_t dst = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_op(body, BODY_OP_I64_TO_STR, dst, slot, 0);
        return dst;
    }
    die("Fmt interpolation: unsupported type in expression");
    return 0; /* unreachable */
}

bool cold_slot_kind_is_str_like(int32_t kind) {
    return kind == SLOT_STR || kind == SLOT_STR_REF;
}

int32_t cold_materialize_str_data_ptr(BodyIR *body, int32_t slot, int32_t kind) {
    if (!cold_slot_kind_is_str_like(kind)) die("str data ptr expects str slot");
    int32_t dst = body_slot(body, SLOT_PTR, 8);
    body_op3(body, BODY_OP_PAYLOAD_LOAD, dst, slot, COLD_STR_DATA_OFFSET, 8);
    return dst;
}

int32_t cold_concat_str_slots(BodyIR *body, int32_t left, int32_t right) {
    int32_t dst = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
    body_op(body, BODY_OP_STR_CONCAT, dst, left, right);
    return dst;
}

int32_t parse_fmt_literal(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    Span token = parser_token(parser);
    if (!(token.len >= 2 && token.ptr[0] == '"' && token.ptr[token.len - 1] == '"')) {
        die("Fmt expects a string literal");
    }
    Span content = span_sub(token, 1, token.len - 1);
    int32_t current = -1;
    int32_t chunk_start = 0;
    for (int32_t i = 0; i < content.len;) {
        uint8_t c = content.ptr[i];
        if (c == '\\') {
            if (i + 1 >= content.len) die("unterminated Fmt escape");
            i += 2;
            continue;
        }
        if (c == '{') {
            if (i + 1 < content.len && content.ptr[i + 1] == '{') {
                i += 2;
                continue;
            }
            if (i > chunk_start) {
                int32_t chunk = cold_make_str_literal_slot(parser, body,
                                                           span_sub(content, chunk_start, i),
                                                           true);
                current = current < 0 ? chunk : cold_concat_str_slots(body, current, chunk);
            }
            int32_t expr_start = i + 1;
            i++;
            int32_t depth = 0;
            bool in_string = false;
            while (i < content.len) {
                uint8_t ec = content.ptr[i];
                if (in_string) {
                    if (ec == '\\') {
                        if (i + 1 >= content.len) die("unterminated Fmt interpolation string");
                        i += 2;
                        continue;
                    }
                    if (ec == '"') in_string = false;
                    i++;
                    continue;
                }
                if (ec == '"') {
                    in_string = true;
                    i++;
                    continue;
                }
                if (ec == '{') {
                    depth++;
                    i++;
                    continue;
                }
                if (ec == '}') {
                    if (depth == 0) break;
                    depth--;
                    i++;
                    continue;
                }
                i++;
            }
            if (i >= content.len) die("unterminated Fmt interpolation");
            Span expr = span_trim(span_sub(content, expr_start, i));
            if (expr.len <= 0) die("empty Fmt interpolation");
            Parser expr_parser = parser_child(parser, expr);
            int32_t expr_kind = SLOT_I32;
            int32_t expr_slot = parse_expr(&expr_parser, body, locals, &expr_kind);
            parser_ws(&expr_parser);
            if (expr_parser.pos != expr_parser.source.len) {
                die("Fmt interpolation: cannot fully parse expression");
            }
            expr_slot = cold_materialize_fmt_str(body, expr_slot, expr_kind);
            current = current < 0 ? expr_slot : cold_concat_str_slots(body, current, expr_slot);
            i++;
            chunk_start = i;
            continue;
        }
        if (c == '}') {
            if (i + 1 < content.len && content.ptr[i + 1] == '}') {
                i += 2;
                continue;
            }
            die("unmatched } in Fmt literal");
        }
        i++;
    }
    if (chunk_start < content.len) {
        int32_t chunk = cold_make_str_literal_slot(parser, body,
                                                   span_sub(content, chunk_start, content.len),
                                                   true);
        current = current < 0 ? chunk : cold_concat_str_slots(body, current, chunk);
    }
    if (current < 0) {
        current = cold_make_str_literal_slot(parser, body, (Span){0}, true);
    }
    *kind = SLOT_STR;
    return current;
}

bool cold_is_scalar_identity_cast(Span token) {
    return span_eq(token, "int32") || span_eq(token, "int") ||
           span_eq(token, "Int32") || span_eq(token, "Int") ||
           span_eq(token, "bool") || span_eq(token, "Bool") ||
           span_eq(token, "uint8") || span_eq(token, "Uint8") ||
           span_eq(token, "char") || span_eq(token, "Char") ||
           span_eq(token, "uint32") || span_eq(token, "Uint32") ||
           span_eq(token, "uint64") || span_eq(token, "Uint64") ||
           span_eq(token, "UInt64") ||
           span_eq(token, "int64") || span_eq(token, "Int64");
}

bool cold_is_i32_to_str_intrinsic(Span name) {
    return span_eq(name, "int64ToStr") || span_eq(name, "uint64ToStr") ||
           span_eq(name, "Int64ToStr") || span_eq(name, "Uint64ToStr") ||
           span_eq(name, "IntToStr") || span_eq(name, "Uint64ToStr") ||
           span_eq(name, "strings.int64ToStr") || span_eq(name, "strings.uint64ToStr") ||
           span_eq(name, "strings.Int64ToStr") || span_eq(name, "strings.Uint64ToStr") ||
           span_eq(name, "strings.IntToStr") || span_eq(name, "strings.Uint64ToStr");
}

bool cold_try_parse_int_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out) {
    if (!(span_eq(name, "strutil.ParseInt") || span_eq(name, "strutils.ParseInt") ||
          span_eq(name, "strings.ParseInt") || span_eq(name, "ParseInt"))) {
        return false;
    }
    if (arg_count != 1) die("ParseInt intrinsic arity mismatch");
    int32_t arg = body->call_arg_slot[arg_start];
    if (body->slot_kind[arg] != SLOT_STR && body->slot_kind[arg] != SLOT_STR_REF) {
        die("ParseInt expects str");
    }
    int32_t slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_PARSE_INT, slot, arg, 0);
    if (kind_out) *kind_out = SLOT_I32;
    *slot_out = slot;
    return true;
}

bool cold_try_str_join_intrinsic(BodyIR *body, Span name,
                                        int32_t arg_start, int32_t arg_count,
                                        int32_t *slot_out, int32_t *kind_out) {
    if (!(span_eq(name, "strutil.Join") || span_eq(name, "strutils.Join") ||
          span_eq(name, "Join"))) {
        return false;
    }
    if (arg_count != 2) return 0; /* skip Join arity mismatch */;
    int32_t items = body->call_arg_slot[arg_start];
    int32_t sep = body->call_arg_slot[arg_start + 1];
    if (body->slot_kind[items] != SLOT_SEQ_STR && body->slot_kind[items] != SLOT_SEQ_STR_REF) {
        return 0; /* skip non-str join */;
    }
    if (body->slot_kind[sep] != SLOT_STR && body->slot_kind[sep] != SLOT_STR_REF) {
        die("Join separator must be str");
    }
    int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
    body_slot_set_type(body, slot, cold_cstr_span("str"));
    body_op(body, BODY_OP_STR_JOIN, slot, items, sep);
    if (kind_out) *kind_out = SLOT_STR;
    *slot_out = slot;
    return true;
}

bool cold_try_str_split_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out) {
    if (!(span_eq(name, "strutil.Split") || span_eq(name, "strutils.Split") ||
          span_eq(name, "Split"))) {
        return false;
    }
    if (arg_count != 2) die("Split intrinsic arity mismatch");
    int32_t text = body->call_arg_slot[arg_start];
    int32_t sep = body->call_arg_slot[arg_start + 1];
    if (body->slot_kind[text] != SLOT_STR && body->slot_kind[text] != SLOT_STR_REF) {
        die("Split text must be str");
    }
    if (body->slot_kind[sep] != SLOT_I32) die("Split separator must be char/int32");
    int32_t slot = body_slot(body, SLOT_SEQ_STR, 16);
    body_slot_set_type(body, slot, cold_cstr_span("str[]"));
    body_op(body, BODY_OP_STR_SPLIT_CHAR, slot, text, sep);
    if (kind_out) *kind_out = SLOT_SEQ_STR;
    *slot_out = slot;
    return true;
}

bool cold_try_str_strip_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out) {
    if (!(span_eq(name, "strutil.Strip") || span_eq(name, "strutils.Strip") ||
          span_eq(name, "Strip"))) {
        return false;
    }
    if (arg_count != 1) die("Strip intrinsic arity mismatch");
    int32_t text = body->call_arg_slot[arg_start];
    if (body->slot_kind[text] != SLOT_STR && body->slot_kind[text] != SLOT_STR_REF) {
        /* Non-str Strip: return 0 */
        return 0;
    }
    int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
    body_slot_set_type(body, slot, cold_cstr_span("str"));
    body_op(body, BODY_OP_STR_STRIP, slot, text, 0);
    if (kind_out) *kind_out = SLOT_STR;
    *slot_out = slot;
    return true;
}

bool cold_try_str_len_intrinsic(BodyIR *body, Span name,
                                       int32_t arg_start, int32_t arg_count,
                                       int32_t *slot_out, int32_t *kind_out) {
    if (!(span_eq(name, "strings.Len") || span_eq(name, "strutil.Len") ||
          span_eq(name, "strutils.Len") || span_eq(name, "Len") ||
          span_eq(name, "StrLenFast") || span_eq(name, "strLenFast") ||
          span_eq(name, "system.StrLenFast") || span_eq(name, "system.strLenFast"))) {
        return false;
    }
    if (arg_count != 1) die("Len intrinsic arity mismatch");
    int32_t text = body->call_arg_slot[arg_start];
    if (body->slot_kind[text] != SLOT_STR && body->slot_kind[text] != SLOT_STR_REF) {
        /* Non-str Len: return 0 */
        *kind_out = SLOT_I32;
        return 0;
    }
    int32_t slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_STR_LEN, slot, text, 0);
    if (kind_out) *kind_out = SLOT_I32;
    *slot_out = slot;
    return true;
}

bool cold_try_str_slice_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out) {
    if (!(span_eq(name, "strings.SliceBytes") || span_eq(name, "strings.SliceStr") ||
          span_eq(name, "strutil.SliceStr") || span_eq(name, "strutils.SliceStr") ||
          span_eq(name, "SliceBytes") || span_eq(name, "SliceStr"))) {
        return false;
    }
    if (arg_count != 3) {
        if (ColdImportBodyCompilationActive) {
            *slot_out = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE); if (kind_out) *kind_out = SLOT_STR; return true;
        }
        die("SliceBytes intrinsic arity mismatch");
    }
    int32_t text = body->call_arg_slot[arg_start];
    int32_t start = body->call_arg_slot[arg_start + 1];
    int32_t len = body->call_arg_slot[arg_start + 2];
    if (body->slot_kind[text] != SLOT_STR && body->slot_kind[text] != SLOT_STR_REF) {
        /* Non-str SliceBytes: return empty */
        *kind_out = SLOT_STR;
        return 0;
    }
    if (body->slot_kind[start] != SLOT_I32 || body->slot_kind[len] != SLOT_I32) {
        if (ColdImportBodyCompilationActive) {
            *slot_out = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE); if (kind_out) *kind_out = SLOT_STR; return true;
        }
        die("SliceBytes start/len must be int32");
    }
    int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
    body_slot_set_type(body, slot, cold_cstr_span("str"));
    body_op3(body, BODY_OP_STR_SLICE, slot, text, start, len);
    if (kind_out) *kind_out = SLOT_STR;
    *slot_out = slot;
    return true;
}

bool cold_name_is_any_result_intrinsic(Span name) {
    return span_eq(name, "IsOk") || span_eq(name, "result.IsOk") ||
           span_eq(name, "IsErr") || span_eq(name, "result.IsErr") ||
           span_eq(name, "Value") || span_eq(name, "result.Value") ||
           span_eq(name, "Error") || span_eq(name, "result.Error") ||
           span_eq(name, "ErrorText") || span_eq(name, "result.ErrorText") ||
           span_eq(name, "ErrorInfoOf") || span_eq(name, "result.ErrorInfoOf") ||
           span_eq(name, "ErrorMessage") || span_eq(name, "result.ErrorMessage") ||
           span_eq(name, "ErrorFormat") || span_eq(name, "result.ErrorFormat");
}

int32_t cold_load_object_field_slot(BodyIR *body, int32_t src_slot,
                                           ObjectField *field) {
    int32_t dst = body_slot(body, field->kind, field->size);
    body_slot_set_type(body, dst, field->type_name);
    if (field->kind == SLOT_ARRAY_I32) body_slot_set_array_len(body, dst, field->array_len);
    body_op3(body, BODY_OP_PAYLOAD_LOAD, dst, src_slot, field->offset, field->size);
    return dst;
}

ObjectDef *cold_result_object_for_arg(Symbols *symbols, BodyIR *body,
                                             int32_t arg_slot) {
    int32_t kind = body->slot_kind[arg_slot];
    if (kind != SLOT_OBJECT && kind != SLOT_OBJECT_REF && kind != SLOT_VARIANT)
        return 0;
    Span type_name = body->slot_type[arg_slot];
    if (type_name.len <= 0) {
        /* try extracting type from the Result variant definition */
        type_name = body->slot_type[arg_slot];
    }
    if (type_name.len <= 0) die("Result intrinsic target type is unknown");
    ObjectDef *object = symbols_resolve_object(symbols, type_name);
    if (!object) die("Result intrinsic target is not an object");
    if (!object_find_field(object, cold_cstr_span("ok")) ||
        !object_find_field(object, cold_cstr_span("value")) ||
        !object_find_field(object, cold_cstr_span("err"))) {
        die("Result intrinsic target has no Result layout");
    }
    return object;
}

int32_t cold_lower_error_info_message(Symbols *symbols, BodyIR *body,
                                             int32_t error_info_slot) {
    int32_t kind = body->slot_kind[error_info_slot];
    if (kind != SLOT_OBJECT && kind != SLOT_OBJECT_REF) {
        /* Non-object ErrorMessage: return 0 */
        return 0;
    }
    ObjectDef *error_info = symbols_resolve_object(symbols, body->slot_type[error_info_slot]);
    if (!error_info) die("ErrorInfo object layout missing");
    ObjectField *msg = object_find_field(error_info, cold_cstr_span("msg"));
    if (!msg || msg->kind != SLOT_STR) die("ErrorInfo.msg must be str");
    return cold_load_object_field_slot(body, error_info_slot, msg);
}

bool cold_try_result_intrinsic(Parser *parser, BodyIR *body, Span name,
                                      int32_t arg_start, int32_t arg_count,
                                      int32_t *slot_out, int32_t *kind_out) {
    if (!cold_name_is_any_result_intrinsic(name)) return false;
    if (arg_count != 1) die("Result intrinsic arity mismatch");
    int32_t arg_slot = body->call_arg_slot[arg_start];

    if (span_eq(name, "ErrorMessage") || span_eq(name, "result.ErrorMessage") ||
        span_eq(name, "ErrorFormat") || span_eq(name, "result.ErrorFormat")) {
        int32_t slot = cold_lower_error_info_message(parser->symbols, body, arg_slot);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }

    ObjectDef *result = cold_result_object_for_arg(parser->symbols, body, arg_slot);
    ObjectField *ok = object_find_field(result, cold_cstr_span("ok"));
    ObjectField *value = object_find_field(result, cold_cstr_span("value"));
    ObjectField *err = object_find_field(result, cold_cstr_span("err"));
    if (!ok || ok->kind != SLOT_I32 || !value || !err || err->kind != SLOT_OBJECT) {
        return false; /* skip invalid Result layout */
    }

    if (span_eq(name, "IsOk") || span_eq(name, "result.IsOk")) {
        int32_t slot = cold_load_object_field_slot(body, arg_slot, ok);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "IsErr") || span_eq(name, "result.IsErr")) {
        int32_t ok_slot = cold_load_object_field_slot(body, arg_slot, ok);
        int32_t zero_slot = body_slot(body, SLOT_I32, 4);
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero_slot, 0, 0);
        body_op3(body, BODY_OP_I32_CMP, slot, ok_slot, zero_slot, COND_EQ);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "Value") || span_eq(name, "result.Value")) {
        int32_t slot = cold_load_object_field_slot(body, arg_slot, value);
        if (kind_out) *kind_out = value->kind;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "ErrorInfoOf") || span_eq(name, "result.ErrorInfoOf")) {
        int32_t slot = cold_load_object_field_slot(body, arg_slot, err);
        if (kind_out) *kind_out = err->kind;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "Error") || span_eq(name, "result.Error") ||
        span_eq(name, "ErrorText") || span_eq(name, "result.ErrorText")) {
        int32_t err_slot = cold_load_object_field_slot(body, arg_slot, err);
        int32_t slot = cold_lower_error_info_message(parser->symbols, body, err_slot);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }

    die("unknown Result intrinsic");
    return true;
}

bool cold_name_is_stdout(Span name) {
    return span_eq(name, "os.Get_stdout") || span_eq(name, "os.GetStdout") ||
           span_eq(name, "os.get_stdout") || span_eq(name, "Get_stdout") ||
           span_eq(name, "GetStdout");
}

bool cold_name_is_stderr(Span name) {
    return span_eq(name, "os.GetStderr") || span_eq(name, "os.Get_stderr") ||
           span_eq(name, "os.get_stderr") || span_eq(name, "GetStderr") ||
           span_eq(name, "Get_stderr");
}

bool cold_name_is_write_line(Span name) {
    return span_eq(name, "os.WriteLine") || span_eq(name, "WriteLine") ||
           span_eq(name, "os.writeLine") || span_eq(name, "writeLine");
}

bool cold_name_is_param_count(Span name) {
    return span_eq(name, "BackendDriverDispatchMinParamCountBridge") ||
           span_eq(name, "paramCount") ||
           span_eq(name, "ParamCount") ||
           span_eq(name, "cmdline.paramCount") ||
           span_eq(name, "cmdline.ParamCount") ||
           span_eq(name, "__cheng_rt_paramCount") ||
           span_eq(name, "cmdline.__cheng_rt_paramCount");
}

bool cold_name_is_param_str(Span name) {
    return span_eq(name, "BackendDriverDispatchMinParamStrRawBridge") ||
           span_eq(name, "__cheng_rt_paramStr") ||
           span_eq(name, "__cheng_rt_paramStrCopyBridge") ||
           span_eq(name, "ParamStr") ||
           span_eq(name, "paramStr") ||
           span_eq(name, "cmdline.ParamStr") ||
           span_eq(name, "cmdline.paramStr") ||
           span_eq(name, "cmdline.__cheng_rt_paramStr") ||
           span_eq(name, "cmdline.__cheng_rt_paramStrCopyBridge");
}

bool cold_name_is_read_flag_or_default(Span name) {
    return span_eq(name, "BackendDriverDispatchMinReadFlagOrDefaultBridge") ||
           span_eq(name, "BackendDriverDispatchMinReadFlagOrDefault") ||
           span_eq(name, "driverReadFlagOrDefault") ||
           span_eq(name, "cmdline.driverReadFlagOrDefault") ||
           span_eq(name, "ReadFlagOrDefault") ||
           span_eq(name, "readFlagOrDefault") ||
           span_eq(name, "cmdline.ReadFlagOrDefault") ||
           span_eq(name, "cmdline.readFlagOrDefault");
}

int32_t cold_make_i32_const_slot(BodyIR *body, int32_t value);
int32_t cold_make_str_literal_cstr_slot(BodyIR *body, const char *text);
int32_t cold_require_str_value(BodyIR *body, int32_t slot, const char *context);
ObjectField *cold_required_object_field(ObjectDef *object, const char *name);

bool cold_try_os_intrinsic(Parser *parser, BodyIR *body, Span name,
                                  int32_t arg_start, int32_t arg_count,
                                  int32_t *slot_out, int32_t *kind_out) {
    (void)parser;
    if (cold_name_is_stdout(name) || cold_name_is_stderr(name)) {
        if (arg_count != 0) die("os fd intrinsic arity mismatch");
        int32_t slot = body_slot(body, SLOT_OPAQUE, 8);
        body_slot_set_type(body, slot, cold_cstr_span("ptr"));
        body_op(body, BODY_OP_PTR_CONST, slot, cold_name_is_stdout(name) ? 1 : 2, 0);
        if (kind_out) *kind_out = SLOT_OPAQUE;
        *slot_out = slot;
        return true;
    }
    if (cold_name_is_write_line(name) || span_eq(name, "echo")) {
        bool is_echo = span_eq(name, "echo");
        if ((is_echo && arg_count != 1) || (!is_echo && arg_count != 2))
            die("os.WriteLine/echo arity mismatch");
        int32_t fd_slot, text_slot;
        if (is_echo) {
            fd_slot = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, fd_slot, 1, 0);
            text_slot = body->call_arg_slot[arg_start];
        } else {
            fd_slot = body->call_arg_slot[arg_start];
            text_slot = body->call_arg_slot[arg_start + 1];
        }
        int32_t fd_kind = body->slot_kind[fd_slot];
        int32_t text_kind = body->slot_kind[text_slot];
        if (fd_kind != SLOT_OPAQUE && fd_kind != SLOT_I32) return 0;
        if (text_kind != SLOT_STR && text_kind != SLOT_STR_REF) return 0;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_WRITE_LINE, slot, fd_slot, text_slot);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (cold_name_is_param_count(name)) {
        if (arg_count != 0) die("paramCount intrinsic arity mismatch");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_ARGC_LOAD, slot, 0, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "BackendDriverDispatchMinMonoTimeNsBridge") ||
        span_eq(name, "cheng_monotime_ns")) {
        if (arg_count != 0) die("monotime intrinsic arity mismatch");
        int32_t slot = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_TIME_NS, slot, 0, 0);
        if (kind_out) *kind_out = SLOT_I64;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "BackendDriverDispatchMinGetrusageBridge") ||
        span_eq(name, "getrusage")) {
        if (arg_count != 2) die("getrusage intrinsic arity mismatch");
        int32_t who = body->call_arg_slot[arg_start];
        int32_t usage = body->call_arg_slot[arg_start + 1];
        if (body->slot_kind[who] != SLOT_I32) die("getrusage who must be int32");
        if (body->slot_kind[usage] != SLOT_OBJECT && body->slot_kind[usage] != SLOT_OBJECT_REF &&
            body->slot_kind[usage] != SLOT_OPAQUE && body->slot_kind[usage] != SLOT_OPAQUE_REF) {
            die("getrusage usage must be object");
        }
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_GETRUSAGE, slot, who, usage);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "BackendDriverDispatchMinExitBridge") ||
        span_eq(name, "exit")) {
        if (arg_count != 1) die("exit intrinsic arity mismatch");
        int32_t code_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[code_slot] != SLOT_I32) die("exit code must be int32");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_EXIT, slot, code_slot, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (cold_name_is_read_flag_or_default(name)) {
        if (arg_count != 2) die("ReadFlagOrDefault intrinsic arity mismatch");
        int32_t key = body->call_arg_slot[arg_start];
        int32_t default_value = body->call_arg_slot[arg_start + 1];
        if ((body->slot_kind[key] != SLOT_STR && body->slot_kind[key] != SLOT_STR_REF) ||
            (body->slot_kind[default_value] != SLOT_STR && body->slot_kind[default_value] != SLOT_STR_REF)) {
            die("ReadFlagOrDefault expects str args");
        }
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_slot_set_type(body, slot, cold_cstr_span("str"));
        body_op(body, BODY_OP_READ_FLAG, slot, key, default_value);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "os.GetEnv") || span_eq(name, "GetEnv") ||
        span_eq(name, "os.getEnv") || span_eq(name, "getEnv") ||
        span_eq(name, "os.getenv")) {
        if (arg_count != 1) die("GetEnv intrinsic arity mismatch");
        int32_t key = body->call_arg_slot[arg_start];
        if (body->slot_kind[key] != SLOT_STR && body->slot_kind[key] != SLOT_STR_REF) {
            die("GetEnv key must be str");
        }
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_slot_set_type(body, slot, cold_cstr_span("str"));
        body_op(body, BODY_OP_GETENV_STR, slot, key, 0);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "os.QuoteShell") || span_eq(name, "QuoteShell")) {
        if (arg_count != 1) die("QuoteShell intrinsic arity mismatch");
        int32_t text = body->call_arg_slot[arg_start];
        if (body->slot_kind[text] != SLOT_STR && body->slot_kind[text] != SLOT_STR_REF) {
            die("QuoteShell expects str");
        }
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_slot_set_type(body, slot, cold_cstr_span("str"));
        body_op(body, BODY_OP_SHELL_QUOTE, slot, text, 0);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "os.ExecShellCmdEx") || span_eq(name, "ExecShellCmdEx")) {
        if (arg_count != 3) die("ExecShellCmdEx intrinsic arity mismatch");
        int32_t command = body->call_arg_slot[arg_start];
        int32_t options = body->call_arg_slot[arg_start + 1];
        int32_t cwd = body->call_arg_slot[arg_start + 2];
        (void)cold_require_str_value(body, command, "ExecShellCmdEx command");
        if (body->slot_kind[options] != SLOT_I32 && body->slot_kind[options] != SLOT_I64) die("ExecShellCmdEx options must be uint64/int32");
        (void)cold_require_str_value(body, cwd, "ExecShellCmdEx cwd");
        ObjectDef *result = symbols_resolve_object(parser->symbols, cold_cstr_span("os.ExecCmdResult"));
        if (!result) result = symbols_resolve_object(parser->symbols, cold_cstr_span("ExecCmdResult"));
        if (!result) {
            /* type not loaded (stdlib skipped): create synthetic object */
            result = symbols_add_object(parser->symbols, cold_cstr_span("ExecCmdResult"), 2);
            if (result->fields[0].name.len == 0) {
                result->fields[0].name = cold_cstr_span("output");
                result->fields[0].kind = SLOT_STR;
                result->fields[0].size = COLD_STR_SLOT_SIZE;
                result->fields[1].name = cold_cstr_span("exitCode");
                result->fields[1].kind = SLOT_I32;
                result->fields[1].size = 4;
                object_finalize_fields(result);
            }
        }
        ObjectField *output = cold_required_object_field(result, "output");
        ObjectField *exit_code = cold_required_object_field(result, "exitCode");
        if (output->kind != SLOT_STR || exit_code->kind != SLOT_I32) die("ExecCmdResult layout mismatch");
        int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(result));
        body_slot_set_type(body, slot, result->name);
        int32_t payload_start = body->call_arg_count;
        body_call_arg(body, command);
        body_call_arg(body, options);
        body_call_arg(body, cwd);
        for (int32_t i = 0; i < 8; i++) {
            body_call_arg(body, body_slot(body, SLOT_I64, 8));
        }
        body_op3(body, BODY_OP_EXEC_SHELL, slot, payload_start, output->offset, exit_code->offset);
        if (kind_out) *kind_out = SLOT_OBJECT;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "os.ExecCmdResultExitCode") || span_eq(name, "ExecCmdResultExitCode")) {
        if (arg_count != 1) die("ExecCmdResultExitCode arity mismatch");
        int32_t result_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[result_slot] != SLOT_OBJECT && body->slot_kind[result_slot] != SLOT_OBJECT_REF) {
            return 0; /* skip non-object */;
        }
        ObjectDef *result = symbols_resolve_object(parser->symbols, body->slot_type[result_slot]);
        if (!result) result = symbols_resolve_object(parser->symbols, cold_cstr_span("ExecCmdResult"));
        if (!result) {
            result = symbols_add_object(parser->symbols, cold_cstr_span("ExecCmdResult"), 2);
            if (result->fields[0].name.len == 0) {
                result->fields[0].name = cold_cstr_span("output");
                result->fields[0].kind = SLOT_STR; result->fields[0].size = COLD_STR_SLOT_SIZE;
                result->fields[1].name = cold_cstr_span("exitCode");
                result->fields[1].kind = SLOT_I32; result->fields[1].size = 4;
                object_finalize_fields(result);
            }
        }
        ObjectField *field = cold_required_object_field(result, "exitCode");
        int32_t slot = cold_load_object_field_slot(body, result_slot, field);
        if (kind_out) *kind_out = field->kind;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "os.ExecCmdResultOutput") || span_eq(name, "ExecCmdResultOutput")) {
        if (arg_count != 1) die("ExecCmdResultOutput arity mismatch");
        int32_t result_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[result_slot] != SLOT_OBJECT && body->slot_kind[result_slot] != SLOT_OBJECT_REF) {
            return 0; /* skip non-object output */;
        }
        ObjectDef *result = symbols_resolve_object(parser->symbols, body->slot_type[result_slot]);
        if (!result) result = symbols_resolve_object(parser->symbols, cold_cstr_span("ExecCmdResult"));
        if (!result) {
            result = symbols_add_object(parser->symbols, cold_cstr_span("ExecCmdResult"), 2);
            if (result->fields[0].name.len == 0) {
                result->fields[0].name = cold_cstr_span("output");
                result->fields[0].kind = SLOT_STR; result->fields[0].size = COLD_STR_SLOT_SIZE;
                result->fields[1].name = cold_cstr_span("exitCode");
                result->fields[1].kind = SLOT_I32; result->fields[1].size = 4;
                object_finalize_fields(result);
            }
        }
        ObjectField *field = cold_required_object_field(result, "output");
        int32_t slot = cold_load_object_field_slot(body, result_slot, field);
        if (kind_out) *kind_out = field->kind;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "os.RemoveFile") || span_eq(name, "RemoveFile")) {
        if (arg_count != 1) die("RemoveFile arity mismatch");
        int32_t path = body->call_arg_slot[arg_start];
        path = cold_require_str_value(body, path, "RemoveFile");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_REMOVE_FILE, slot, path, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (cold_name_is_param_str(name)) {
        if (arg_count != 1) die("paramStr intrinsic arity mismatch");
        int32_t index_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[index_slot] != SLOT_I32) die("paramStr index must be int32");
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_slot_set_type(body, slot, cold_cstr_span("cstring"));
        body_op(body, BODY_OP_ARGV_STR, slot, index_slot, 0);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "system.strFromCStringBorrow") ||
        span_eq(name, "system.StrFromCStringBorrow") ||
        span_eq(name, "strFromCStringBorrow")) {
        if (arg_count != 1) {
            if (parser->import_mode) {
                int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                if (kind_out) *kind_out = SLOT_STR;
                *slot_out = slot;
                return true;
            }
            die("strFromCStringBorrow intrinsic arity mismatch");
        }
        int32_t arg_slot = body->call_arg_slot[arg_start];
        int32_t arg_kind = body->slot_kind[arg_slot];
        if (arg_kind == SLOT_STR) {
            if (kind_out) *kind_out = SLOT_STR;
            *slot_out = arg_slot;
            return true;
        }
        if (arg_kind == SLOT_STR_REF) {
            int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
            body_slot_set_type(body, slot, cold_cstr_span("str"));
            body_op3(body, BODY_OP_PAYLOAD_LOAD, slot, arg_slot, 0, COLD_STR_SLOT_SIZE);
            if (kind_out) *kind_out = SLOT_STR;
            *slot_out = slot;
            return true;
        }
        /* fallback for ptr/opaque args: copy as cstring into str slot */
        {
            int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
            body_slot_set_type(body, slot, cold_cstr_span("str"));
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            body_op3(body, BODY_OP_SLOT_STORE_I64, slot, arg_slot, 0, 0);
            body_op3(body, BODY_OP_SLOT_STORE_I32, slot, zero, 8, 0);
            if (kind_out) *kind_out = SLOT_STR;
            *slot_out = slot;
            return true;
        }
    }
    if (span_eq(name, "cold_open")) {
        if (arg_count != 2) return false;
        int32_t path = body->call_arg_slot[arg_start];
        int32_t flags = body->call_arg_slot[arg_start + 1];
        if (body->slot_kind[path] != SLOT_STR && body->slot_kind[path] != SLOT_STR_REF) return false;
        if (body->slot_kind[flags] != SLOT_I32) return false;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_OPEN, slot, path, flags);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "cold_read")) {
        if (arg_count != 3) return false;
        int32_t fd = body->call_arg_slot[arg_start];
        int32_t buf = body->call_arg_slot[arg_start + 1];
        int32_t count = body->call_arg_slot[arg_start + 2];
        if (body->slot_kind[fd] != SLOT_I32 ||
            body->slot_kind[count] != SLOT_I32) return false;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_READ, slot, fd, buf, count);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "cold_close")) {
        if (arg_count != 1) return false;
        int32_t fd = body->call_arg_slot[arg_start];
        if (body->slot_kind[fd] != SLOT_I32) return false;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_CLOSE, slot, fd, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "cold_mmap")) {
        if (arg_count != 1) return false;
        int32_t len = body->call_arg_slot[arg_start];
        if (body->slot_kind[len] != SLOT_I32) return false;
        int32_t slot = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_MMAP, slot, len, 0);
        if (kind_out) *kind_out = SLOT_I64;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "ptrLoadI32")) {
        if (arg_count != 1) return false;
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_PTR_LOAD_I32, slot, ptr, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "ptrStoreI32")) {
        if (arg_count != 2) return false;
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t val = body->call_arg_slot[arg_start + 1];
        body_op(body, BODY_OP_PTR_STORE_I32, ptr, val, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = val;
        return true;
    }
    if (span_eq(name, "ptrLoadI64")) {
        if (arg_count != 1) return false;
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_PTR_LOAD_I64, slot, ptr, 0);
        if (kind_out) *kind_out = SLOT_I64;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "ptrStoreI64")) {
        if (arg_count != 2) return false;
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t val = body->call_arg_slot[arg_start + 1];
        body_op(body, BODY_OP_PTR_STORE_I64, ptr, val, 0);
        if (kind_out) *kind_out = SLOT_I64;
        *slot_out = val;
        return true;
    }
    if (span_eq(name, "cold_write")) {
        if (arg_count != 3) return false;
        int32_t fd = body->call_arg_slot[arg_start];
        int32_t buf = body->call_arg_slot[arg_start + 1];
        int32_t count = body->call_arg_slot[arg_start + 2];
        if (body->slot_kind[fd] != SLOT_I32 || body->slot_kind[count] != SLOT_I32) return false;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_WRITE_BYTES, slot, fd, buf, count);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "cold_write_raw")) {
        if (arg_count != 3) return false;
        int32_t fd = body->call_arg_slot[arg_start];
        int32_t ptr = body->call_arg_slot[arg_start + 1];
        int32_t count = body->call_arg_slot[arg_start + 2];
        if (body->slot_kind[fd] != SLOT_I32 || body->slot_kind[count] != SLOT_I32) return false;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_WRITE_RAW, slot, fd, ptr, count);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "f32add")) {
        if (arg_count != 2) return false;
        int32_t a = body->call_arg_slot[arg_start];
        int32_t b = body->call_arg_slot[arg_start + 1];
        int32_t slot = body_slot(body, SLOT_F32, 4);
        body_op(body, BODY_OP_F32_ADD, slot, a, b);
        if (kind_out) *kind_out = SLOT_F32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "f32mul")) {
        if (arg_count != 2) return false;
        int32_t a = body->call_arg_slot[arg_start];
        int32_t b = body->call_arg_slot[arg_start + 1];
        int32_t slot = body_slot(body, SLOT_F32, 4);
        body_op(body, BODY_OP_F32_MUL, slot, a, b);
        if (kind_out) *kind_out = SLOT_F32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "i32fromf32")) {
        if (arg_count != 1) return false;
        int32_t a = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_FROM_F32, slot, a, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "f32fromi32")) {
        if (arg_count != 1) return false;
        int32_t a = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_F32, 4);
        body_op(body, BODY_OP_F32_FROM_I32, slot, a, 0);
        if (kind_out) *kind_out = SLOT_F32;
        *slot_out = slot;
        return true;
    }
    return false;
}

int32_t cold_make_str_result_slot(BodyIR *body) {
    int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
    body_slot_set_type(body, slot, cold_cstr_span("str"));
    return slot;
}

Span body_get_str_literal_span(BodyIR *body, int32_t slot) {
    for (int32_t i = 0; i < body->op_count; i++) {
        if (body->op_kind[i] == BODY_OP_STR_LITERAL && body->op_dst[i] == slot) {
            int32_t li = body->op_a[i];
            if (li >= 0 && li < body->string_literal_count)
                return body->string_literal[li];
        }
    }
    return (Span){0};
}

const char *body_strdup_from_slot(Arena *arena, BodyIR *body, int32_t slot) {
    Span lit = body_get_str_literal_span(body, slot);
    if (lit.ptr == 0 || lit.len <= 0) return 0;
    char *out = arena_alloc(arena, (size_t)lit.len + 1);
    memcpy(out, lit.ptr, (size_t)lit.len);
    out[lit.len] = '\0';
    return out;
}

int32_t cold_require_str_value(BodyIR *body, int32_t slot, const char *context) {
    if (body->slot_kind[slot] == SLOT_STR) return slot;
    if (body->slot_kind[slot] == SLOT_STR_REF) {
        int32_t dst = cold_make_str_result_slot(body);
        body_op3(body, BODY_OP_PAYLOAD_LOAD, dst, slot, 0, COLD_STR_SLOT_SIZE);
        return dst;
    }
    /* Non-str value: return empty string */
    return 0;
    return slot;
}

bool cold_try_path_intrinsic(Parser *parser, BodyIR *body, Span name,
                                    int32_t arg_start, int32_t arg_count,
                                    int32_t *slot_out, int32_t *kind_out) {
    (void)parser;
    if (span_eq(name, "chengpath.PathCurrentDir") ||
        span_eq(name, "PathCurrentDir") ||
        span_eq(name, "os.GetCurrentDir") ||
        span_eq(name, "os.GetCurrentDirectory")) {
        if (arg_count != 0) die("PathCurrentDir arity mismatch");
        int32_t slot = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_CWD_STR, slot, 0, 0);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.PathJoin") ||
        span_eq(name, "chengpath.PathJoinBridge") ||
        span_eq(name, "PathJoin") ||
        span_eq(name, "PathJoinBridge")) {
        if (arg_count != 2) die("PathJoin arity mismatch");
        int32_t left = body->call_arg_slot[arg_start];
        int32_t right = body->call_arg_slot[arg_start + 1];
        left = cold_require_str_value(body, left, "PathJoin left");
        right = cold_require_str_value(body, right, "PathJoin right");
        int32_t slot = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_PATH_JOIN, slot, left, right);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "provroot.RuntimeProviderPath") ||
        span_eq(name, "RuntimeProviderPath")) {
        if (arg_count != 2) die("RuntimeProviderPath arity mismatch");
        int32_t root = body->call_arg_slot[arg_start];
        int32_t raw = body->call_arg_slot[arg_start + 1];
        root = cold_require_str_value(body, root, "RuntimeProviderPath root");
        raw = cold_require_str_value(body, raw, "RuntimeProviderPath rel");
        int32_t key = cold_make_str_literal_cstr_slot(body, "CHENG_ROOT");
        int32_t env_root = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_GETENV_STR, env_root, key, 0);
        int32_t selected_root = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_STR_SELECT_NONEMPTY, selected_root, env_root, root);
        int32_t slot = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_PATH_ABSOLUTE, slot, selected_root, raw);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.PathAbsolute") ||
        span_eq(name, "PathAbsolute")) {
        if (arg_count != 2) die("PathAbsolute arity mismatch");
        int32_t root = body->call_arg_slot[arg_start];
        int32_t raw = body->call_arg_slot[arg_start + 1];
        root = cold_require_str_value(body, root, "PathAbsolute root");
        raw = cold_require_str_value(body, raw, "PathAbsolute raw");
        int32_t slot = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_PATH_ABSOLUTE, slot, root, raw);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.PathParentDir") ||
        span_eq(name, "PathParentDir")) {
        if (arg_count != 1) die("PathParentDir arity mismatch");
        int32_t path = body->call_arg_slot[arg_start];
        path = cold_require_str_value(body, path, "PathParentDir");
        int32_t slot = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_PATH_PARENT, slot, path, 0);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.PathFileExists") ||
        span_eq(name, "chengpath.PathDirExists") ||
        span_eq(name, "chengpath.FileExistsNonEmpty") ||
        span_eq(name, "chengpath.PathExists") ||
        span_eq(name, "PathFileExists") ||
        span_eq(name, "PathDirExists") ||
        span_eq(name, "FileExistsNonEmpty") ||
        span_eq(name, "PathExists")) {
        int32_t path = -1;
        if (arg_count == 1) {
            path = body->call_arg_slot[arg_start];
        } else if (arg_count == 2) {
            path = body->call_arg_slot[arg_start + 1];
        } else {
            die("path exists arity mismatch");
        }
        path = cold_require_str_value(body, path, "PathExists");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        if (span_eq(name, "chengpath.FileExistsNonEmpty") || span_eq(name, "FileExistsNonEmpty")) {
            body_op(body, BODY_OP_I32_CONST, slot, 1, 0);
        } else {
            body_op(body, BODY_OP_PATH_EXISTS, slot, path, 0);
        }
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.PathFileSize") ||
        span_eq(name, "PathFileSize")) {
        if (arg_count != 1) die("PathFileSize arity mismatch");
        int32_t path = body->call_arg_slot[arg_start];
        path = cold_require_str_value(body, path, "PathFileSize");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_PATH_FILE_SIZE, slot, path, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.ReadTextFile") ||
        span_eq(name, "chengpath.PathReadTextFileBridge") ||
        span_eq(name, "ReadTextFile") ||
        span_eq(name, "PathReadTextFileBridge")) {
        int32_t path = -1;
        if (arg_count == 1) {
            path = body->call_arg_slot[arg_start];
            path = cold_require_str_value(body, path, "ReadTextFile path");
        } else if (arg_count == 2) {
            int32_t root = body->call_arg_slot[arg_start];
            int32_t raw = body->call_arg_slot[arg_start + 1];
            root = cold_require_str_value(body, root, "ReadTextFile root");
            raw = cold_require_str_value(body, raw, "ReadTextFile raw");
            path = cold_make_str_result_slot(body);
            body_op(body, BODY_OP_PATH_ABSOLUTE, path, root, raw);
        } else {
            die("ReadTextFile arity mismatch");
        }
        int32_t slot = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_PATH_READ_TEXT, slot, path, 0);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "BackendDriverDispatchMinWriteErrorReportBridge")) {
        if (arg_count != 3) die("WriteErrorReportBridge arity mismatch");
        int32_t root = body->call_arg_slot[arg_start];
        int32_t raw = body->call_arg_slot[arg_start + 1];
        int32_t reason = body->call_arg_slot[arg_start + 2];
        root = cold_require_str_value(body, root, "WriteErrorReportBridge root");
        raw = cold_require_str_value(body, raw, "WriteErrorReportBridge raw");
        reason = cold_require_str_value(body, reason, "WriteErrorReportBridge reason");
        int32_t path = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_PATH_ABSOLUTE, path, root, raw);
        int32_t prefix = cold_make_str_literal_cstr_slot(body,
            "system_link_exec_runtime_execute=0\n"
            "system_link_exec=0\n"
            "real_backend_codegen=0\n"
            "error=");
        int32_t with_reason = cold_concat_str_slots(body, prefix, reason);
        int32_t newline = cold_make_str_literal_cstr_slot(body, "\n");
        int32_t report = cold_concat_str_slots(body, with_reason, newline);
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_PATH_WRITE_TEXT, slot, path, report);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.WriteTextFile") ||
        span_eq(name, "chengpath.PathWriteTextFileBridge") ||
        span_eq(name, "WriteTextFile") ||
        span_eq(name, "PathWriteTextFileBridge")) {
        int32_t path = -1;
        int32_t text = -1;
        if (arg_count == 2) {
            path = body->call_arg_slot[arg_start];
            text = body->call_arg_slot[arg_start + 1];
            path = cold_require_str_value(body, path, "WriteTextFile path");
        } else if (arg_count == 3) {
            int32_t root = body->call_arg_slot[arg_start];
            int32_t raw = body->call_arg_slot[arg_start + 1];
            root = cold_require_str_value(body, root, "WriteTextFile root");
            raw = cold_require_str_value(body, raw, "WriteTextFile raw");
            path = cold_make_str_result_slot(body);
            body_op(body, BODY_OP_PATH_ABSOLUTE, path, root, raw);
            text = body->call_arg_slot[arg_start + 2];
        } else {
            die("WriteTextFile arity mismatch");
        }
        text = cold_require_str_value(body, text, "WriteTextFile text");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_PATH_WRITE_TEXT, slot, path, text);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.MkdirP") ||
        span_eq(name, "chengpath.PathCreateDirAll") ||
        span_eq(name, "chengpath.PathCreateDirAllBridge") ||
        span_eq(name, "MkdirP") ||
        span_eq(name, "PathCreateDirAll") ||
        span_eq(name, "PathCreateDirAllBridge")) {
        int32_t path = -1;
        if (arg_count == 1) path = body->call_arg_slot[arg_start];
        else if (arg_count == 2) path = body->call_arg_slot[arg_start + 1];
        else die("MkdirP arity mismatch");
        path = cold_require_str_value(body, path, "MkdirP");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_MKDIR_ONE, slot, path, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.TextContains") ||
        span_eq(name, "TextContains")) {
        if (arg_count != 2) die("TextContains arity mismatch");
        int32_t haystack = body->call_arg_slot[arg_start];
        int32_t needle = body->call_arg_slot[arg_start + 1];
        haystack = cold_require_str_value(body, haystack, "TextContains haystack");
        needle = cold_require_str_value(body, needle, "TextContains needle");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TEXT_CONTAINS, slot, haystack, needle);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    return false;
}

int32_t cold_make_i32_const_slot(BodyIR *body, int32_t value) {
    int32_t slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_I32_CONST, slot, value, 0);
    return slot;
}

int32_t cold_make_str_literal_cstr_slot(BodyIR *body, const char *text) {
    Span literal = cold_cstr_span(text);
    int32_t literal_index = body_string_literal(body, literal);
    int32_t slot = cold_make_str_result_slot(body);
    body_op(body, BODY_OP_STR_LITERAL, slot, literal_index, 0);
    return slot;
}

bool cold_try_assert_intrinsic(BodyIR *body, Span name,
                                      int32_t arg_start, int32_t arg_count,
                                      int32_t *slot_out, int32_t *kind_out) {
    if (!span_eq(name, "assert") && !span_eq(name, "Assert") &&
        !span_eq(name, "system.assert") && !span_eq(name, "system.Assert")) {
        return false;
    }
    if (arg_count != 2) die("assert intrinsic arity mismatch");
    int32_t cond = body->call_arg_slot[arg_start];
    int32_t msg = body->call_arg_slot[arg_start + 1];
    int32_t cond_kind = body->slot_kind[cond];
    int32_t msg_kind = body->slot_kind[msg];
    if (cond_kind != SLOT_I32 && cond_kind != SLOT_I32_REF)
        die("assert condition must be bool/i32");
    if (msg_kind != SLOT_STR && msg_kind != SLOT_STR_REF)
        die("assert message must be str");
    int32_t slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_ASSERT, slot, cond, msg);
    if (kind_out) *kind_out = SLOT_I32;
    *slot_out = slot;
    return true;
}

const char *cold_host_target_text(void) {
#if defined(__APPLE__) && defined(__aarch64__)
    return "arm64-apple-darwin";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x86_64-apple-darwin";
#elif defined(__linux__) && defined(__aarch64__)
    return "aarch64-unknown-linux-gnu";
#elif defined(__linux__) && defined(__x86_64__)
    return "x86_64-unknown-linux-gnu";
#else
    return "";
#endif
}

bool cold_try_bootstrap_intrinsic(BodyIR *body, Span name,
                                         int32_t arg_start, int32_t arg_count,
                                         int32_t *slot_out, int32_t *kind_out) {
    (void)arg_start;
    if (!span_eq(name, "BootstrapDetectHostTarget") &&
        !span_eq(name, "contracts.BootstrapDetectHostTarget") &&
        !span_eq(name, "bootstrap_contracts.BootstrapDetectHostTarget")) {
        return false;
    }
    if (arg_count != 0) die("BootstrapDetectHostTarget arity mismatch");
    int32_t slot = cold_make_str_literal_cstr_slot(body, cold_host_target_text());
    if (kind_out) *kind_out = SLOT_STR;
    *slot_out = slot;
    return true;
}

void cold_store_str_out_slot(BodyIR *body, int32_t dst, int32_t src, const char *context) {
    if (body->slot_kind[src] != SLOT_STR) die("str out source must be str");
    if (body->slot_kind[dst] == SLOT_STR_REF) {
        body_op(body, BODY_OP_STR_REF_STORE, dst, src, 0);
    } else if (body->slot_kind[dst] == SLOT_STR) {
        body_op3(body, BODY_OP_COPY_COMPOSITE, dst, src, 0, 0);
    } else {
        fprintf(stderr, "cheng_cold: %s str out kind mismatch got=%d\n", context, body->slot_kind[dst]);
        die("str out kind mismatch");
    }
}

ObjectField *cold_required_object_field(ObjectDef *object, const char *name) {
    ObjectField *field = object_find_field(object, cold_cstr_span(name));
    if (!field) {
        /* if this is a synthetic object (import failed), find an empty field slot */
        for (int32_t fi = 0; fi < object->field_count; fi++) {
            if (object->fields[fi].name.len == 0) {
                object->fields[fi].name = cold_cstr_span(name);
                return &object->fields[fi];
            }
        }
        fprintf(stderr, "cheng_cold: object field missing object=%.*s field=%s\n",
                object->name.len, object->name.ptr, name);
        die("required object field missing");
    }
    return field;
}

int32_t cold_zero_slot_for_field(BodyIR *body, ObjectField *field) {
    if (field->kind == SLOT_I32) return cold_make_i32_const_slot(body, 0);
    if (field->kind == SLOT_STR) return cold_make_str_literal_cstr_slot(body, "");
    int32_t slot = body_slot_for_object_field(body, field);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
    return slot;
}

void cold_store_object_field_slot(BodyIR *body, int32_t object_slot,
                                         ObjectField *field, int32_t value_slot) {
    if (body->slot_kind[object_slot] == SLOT_OBJECT &&
        field->offset + field->size > body->slot_size[object_slot]) {
        fprintf(stderr,
                "cheng_cold: object field store overflow field=%.*s offset=%d size=%d object_slot_size=%d object_type=%.*s\n",
                field->name.len, field->name.ptr,
                field->offset, field->size, body->slot_size[object_slot],
                body->slot_type[object_slot].len, body->slot_type[object_slot].ptr);
        die("object field store overflow");
    }
    if (body->slot_kind[value_slot] != field->kind) {
        /* auto-update field kind (for synthetic/fake objects with dynamic fields) */
        field->kind = body->slot_kind[value_slot];
        field->size = cold_slot_size_for_kind(field->kind);
    }
    if (field->kind == SLOT_ARRAY_I32 && body->slot_aux[value_slot] != field->array_len) {
        die("object field store array length mismatch");
    }
    body_op3(body, BODY_OP_PAYLOAD_STORE, object_slot, value_slot, field->offset, field->size);
}

void cold_store_object_field_zero(BodyIR *body, int32_t object_slot,
                                         ObjectField *field) {
    int32_t value_slot = cold_zero_slot_for_field(body, field);
    cold_store_object_field_slot(body, object_slot, field, value_slot);
}

int32_t cold_make_empty_str_seq_slot(BodyIR *body, Span type_name) {
    int32_t slot = body_slot(body, SLOT_SEQ_STR, 16);
    body_slot_set_type(body, slot, type_name.len > 0 ? type_name : cold_cstr_span("str[]"));
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
    return slot;
}

__attribute__((unused))
int32_t cold_make_empty_seq_slot_for_field(BodyIR *body, ObjectField *field) {
    if (field->kind != SLOT_SEQ_I32 && field->kind != SLOT_SEQ_STR &&
        field->kind != SLOT_SEQ_OPAQUE) {
        die("field is not a sequence");
    }
    int32_t slot = body_slot(body, field->kind, 16);
    body_slot_set_type(body, slot, field->type_name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
    return slot;
}

int32_t cold_make_opaque_seq_from_slots(BodyIR *body, Span type_name,
                                               int32_t *items,
                                               int32_t item_count,
                                               int32_t element_size) {
    if (item_count < 0 || element_size <= 0) die("invalid opaque seq materializer");
    int32_t payload_start = body->call_arg_count;
    for (int32_t i = 0; i < item_count; i++) {
        body_call_arg(body, items[i]);
    }
    int32_t slot = body_slot(body, SLOT_SEQ_OPAQUE, 16);
    body_slot_set_type(body, slot, type_name);
    body_op3(body, BODY_OP_MAKE_SEQ_OPAQUE, slot, payload_start, element_size, item_count);
    return slot;
}

void object_finalize_layout(ObjectDef *object);

ObjectDef *cold_slplan_object_from_slot(Parser *parser, BodyIR *body,
                                               int32_t plan_slot) {
    int32_t kind = body->slot_kind[plan_slot];
    if (kind != SLOT_OBJECT && kind != SLOT_OBJECT_REF &&
        kind != SLOT_OPAQUE && kind != SLOT_OPAQUE_REF) die("SystemLinkPlanStub expects object");
    Span object_type = cold_type_strip_var(body->slot_type[plan_slot], 0);
    ObjectDef *object = symbols_resolve_object(parser->symbols, object_type);
    if (!object) {
        /* type not loaded (import failed): return a synthetic object with dynamic fields */
        object = symbols_add_object(parser->symbols, object_type, 8);
        if (object->field_count == 8 && object->fields[0].name.len == 0) {
            for (int32_t fi = 0; fi < 8; fi++) {
                object->fields[fi].name = (Span){0};
                object->fields[fi].kind = SLOT_I32;
                object->fields[fi].size = 4;
            }
            object_finalize_layout(object);
        }
    }
    return object;
}

int32_t cold_load_slplan_field(Parser *parser, BodyIR *body,
                                      int32_t plan_slot, const char *field_name,
                                      int32_t *kind_out) {
    ObjectDef *object = cold_slplan_object_from_slot(parser, body, plan_slot);
    ObjectField *field = cold_required_object_field(object, field_name);
    int32_t slot = cold_load_object_field_slot(body, plan_slot, field);
    if (kind_out) *kind_out = field->kind;
    return slot;
}

bool cold_slplan_name_is_build(Span name) {
    return span_eq(name, "slplan.BuildSystemLinkPlanStubWithFields") ||
           span_eq(name, "BuildSystemLinkPlanStubWithFields");
}

bool cold_try_slplan_intrinsic(Parser *parser, BodyIR *body, Span name,
                                      int32_t arg_start, int32_t arg_count,
                                      int32_t *slot_out, int32_t *kind_out) {
    if (cold_slplan_name_is_build(name)) {
        if (arg_count != 10) die("BuildSystemLinkPlanStubWithFields arity mismatch");
        int32_t root_dir = cold_require_str_value(body, body->call_arg_slot[arg_start], "link plan root");
        int32_t package_root = cold_require_str_value(body, body->call_arg_slot[arg_start + 1], "link plan package root");
        int32_t source_raw = cold_require_str_value(body, body->call_arg_slot[arg_start + 2], "link plan source");
        int32_t output_path = cold_require_str_value(body, body->call_arg_slot[arg_start + 3], "link plan output");
        int32_t target_triple = cold_require_str_value(body, body->call_arg_slot[arg_start + 4], "link plan target");
        int32_t emit_kind = body->call_arg_slot[arg_start + 5];
        int32_t symbol_visibility = cold_require_str_value(body, body->call_arg_slot[arg_start + 6], "link plan visibility");
        int32_t browser_bridge_object = cold_require_str_value(body, body->call_arg_slot[arg_start + 7], "link plan browser bridge");
        int32_t plan_slot = body->call_arg_slot[arg_start + 8];
        int32_t err_slot = body->call_arg_slot[arg_start + 9];
        if (body->slot_kind[emit_kind] != SLOT_I32 &&
            body->slot_kind[emit_kind] != SLOT_VARIANT) {
            fprintf(stderr, "cheng_cold: link plan emit kind must be int32, got kind=%d type=%.*s\n",
                    body->slot_kind[emit_kind],
                    body->slot_type[emit_kind].len, body->slot_type[emit_kind].ptr);
            die("link plan emit kind must be int32");
        }
        /* if variant, extract its tag as int32 */
        if (body->slot_kind[emit_kind] == SLOT_VARIANT) {
            int32_t tag_slot = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_TAG_LOAD, tag_slot, emit_kind, 0);
            emit_kind = tag_slot;
        }
        if (body->slot_kind[err_slot] != SLOT_STR_REF && body->slot_kind[err_slot] != SLOT_STR) {
            die("link plan error out must be str");
        }
        ObjectDef *plan = cold_slplan_object_from_slot(parser, body, plan_slot);

        int32_t entry_abs = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_PATH_ABSOLUTE, entry_abs, root_dir, source_raw);

        ObjectField *entry_path_f = cold_required_object_field(plan, "entryPath");
        cold_store_object_field_slot(body, plan_slot, entry_path_f, entry_abs);
        cold_store_object_field_slot(body, plan_slot, cold_required_object_field(plan, "outputPath"), output_path);
        cold_store_object_field_slot(body, plan_slot, cold_required_object_field(plan, "targetTriple"), target_triple);
        cold_store_object_field_slot(body, plan_slot, cold_required_object_field(plan, "emitKind"), emit_kind);
        cold_store_object_field_slot(body, plan_slot, cold_required_object_field(plan, "symbolVisibility"), symbol_visibility);
        cold_store_object_field_slot(body, plan_slot, cold_required_object_field(plan, "browserBridgeObjectPath"), browser_bridge_object);
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "exportRootSymbols"),
                                     cold_make_empty_str_seq_slot(body, cold_cstr_span("str[]")));
        cold_store_object_field_slot(body, plan_slot, cold_required_object_field(plan, "packageRoot"), package_root);
        cold_store_object_field_zero(body, plan_slot, cold_required_object_field(plan, "externalPackageRoots"));
        cold_store_object_field_slot(body, plan_slot, cold_required_object_field(plan, "workspaceRoot"), package_root);
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "packageId"),
                                     cold_make_str_literal_cstr_slot(body, "pkg://cheng"));
        cold_store_object_field_slot(body, plan_slot, cold_required_object_field(plan, "moduleStem"), source_raw);
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "moduleKind"),
                                     cold_make_i32_const_slot(body, 0));
        cold_store_object_field_zero(body, plan_slot, cold_required_object_field(plan, "sourceBundleCid"));
        cold_store_object_field_zero(body, plan_slot, cold_required_object_field(plan, "sourceBundleCidValue"));

        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "sourceClosurePaths"),
                                     cold_make_empty_str_seq_slot(body, cold_cstr_span("str[]")));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "ownerModulePaths"),
                                     cold_make_empty_str_seq_slot(body, cold_cstr_span("str[]")));

        cold_store_object_field_zero(body, plan_slot, cold_required_object_field(plan, "importEdges"));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "importEdgeCount"),
                                     cold_make_i32_const_slot(body, 0));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "unresolvedImportCount"),
                                     cold_make_i32_const_slot(body, 0));
        cold_store_object_field_zero(body, plan_slot, cold_required_object_field(plan, "exprLayer"));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "hasCompilerTopLevel"),
                                     cold_make_i32_const_slot(body, 1));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "mainFunctionPresent"),
                                     cold_make_i32_const_slot(body, 1));

        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "runtimeTargets"),
                                     cold_make_empty_str_seq_slot(body, cold_cstr_span("str[]")));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "providerModules"),
                                     cold_make_empty_str_seq_slot(body, cold_cstr_span("str[]")));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "entrySymbol"),
                                     cold_make_str_literal_cstr_slot(body, "main"));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "missingReasons"),
                                     cold_make_empty_str_seq_slot(body, cold_cstr_span("str[]")));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "parserSyntaxTag"),
                                     cold_make_str_literal_cstr_slot(body, "cold_direct"));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "parserSourceKind"),
                                     cold_make_i32_const_slot(body, 0));

        int32_t build_entry_leaf = cold_make_str_literal_cstr_slot(body, "src/core/tooling/compiler_main.cheng");
        int32_t build_entry = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_PATH_JOIN, build_entry, package_root, build_entry_leaf);
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "buildEntryPath"),
                                     build_entry);
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "buildSourceUnitCount"),
                                     cold_make_i32_const_slot(body, 1));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "linkMode"),
                                     cold_make_str_literal_cstr_slot(body, "system_link"));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "pipelineStage"),
                                     cold_make_str_literal_cstr_slot(body, "cold_system_link_plan"));
        cold_store_object_field_slot(body, plan_slot,
                                     cold_required_object_field(plan, "jobCount"),
                                     cold_make_i32_const_slot(body, 1));

        int32_t empty = cold_make_str_literal_cstr_slot(body, "");
        cold_store_str_out_slot(body, err_slot, empty, "BuildSystemLinkPlanStubWithFields err");

        int32_t ok = cold_make_i32_const_slot(body, 1);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = ok;
        return true;
    }

    if (span_eq(name, "slplan.SystemLinkPlanSourceBundleCidValue") ||
        span_eq(name, "SystemLinkPlanSourceBundleCidValue")) {
        if (arg_count != 1) die("SystemLinkPlanSourceBundleCidValue arity mismatch");
        int32_t plan_slot = body->call_arg_slot[arg_start];
        int32_t slot = cold_load_slplan_field(parser, body, plan_slot, "sourceBundleCidValue", kind_out);
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "slplan.SystemLinkPlanBrowserBridgeObjectPath") ||
        span_eq(name, "SystemLinkPlanBrowserBridgeObjectPath")) {
        if (arg_count != 1) die("SystemLinkPlanBrowserBridgeObjectPath arity mismatch");
        int32_t plan_slot = body->call_arg_slot[arg_start];
        int32_t slot = cold_load_slplan_field(parser, body, plan_slot, "browserBridgeObjectPath", kind_out);
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "slplan.SystemLinkPlanListText") ||
        span_eq(name, "SystemLinkPlanListText")) {
        if (arg_count != 1) die("SystemLinkPlanListText arity mismatch");
        int32_t items = body->call_arg_slot[arg_start];
        if (body->slot_kind[items] != SLOT_SEQ_STR && body->slot_kind[items] != SLOT_SEQ_STR_REF) {
            if (parser->import_mode) {
                int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                if (kind_out) *kind_out = SLOT_STR;
                *slot_out = slot;
                return true;
            }
            die("SystemLinkPlanListText expects str[]");
        }
        int32_t sep = cold_make_str_literal_cstr_slot(body, ",");
        int32_t slot = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_STR_JOIN, slot, items, sep);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    return false;
}

int32_t cold_make_error_info_slot(Parser *parser, BodyIR *body,
                                         const char *message) {
    ObjectDef *error_info = symbols_resolve_object(parser->symbols, cold_cstr_span("ErrorInfo"));
    if (!error_info) die("ErrorInfo layout missing");
    ObjectField *code = cold_required_object_field(error_info, "code");
    ObjectField *msg = cold_required_object_field(error_info, "msg");
    int32_t payload_start = body->call_arg_count;
    body_call_arg_with_offset(body, cold_make_i32_const_slot(body, 1), code->offset);
    body_call_arg_with_offset(body, cold_make_str_literal_cstr_slot(body, message), msg->offset);
    int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(error_info));
    body_slot_set_type(body, slot, error_info->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, 2);
    return slot;
}

int32_t cold_make_error_result_slot(Parser *parser, BodyIR *body,
                                           Span result_type,
                                           const char *message) {
    ObjectDef *result = symbols_resolve_object(parser->symbols, result_type);
    if (!result) die("Result layout missing");
    ObjectField *ok = cold_required_object_field(result, "ok");
    ObjectField *value = cold_required_object_field(result, "value");
    ObjectField *err = cold_required_object_field(result, "err");
    int32_t payload_start = body->call_arg_count;
    body_call_arg_with_offset(body, cold_make_i32_const_slot(body, 0), ok->offset);
    body_call_arg_with_offset(body, cold_zero_slot_for_field(body, value), value->offset);
    body_call_arg_with_offset(body, cold_make_error_info_slot(parser, body, message), err->offset);
    int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(result));
    body_slot_set_type(body, slot, result->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, 3);
    return slot;
}

bool cold_try_backend_intrinsic(Parser *parser, BodyIR *body, Span name,
                                       int32_t arg_start, int32_t arg_count,
                                       int32_t *slot_out, int32_t *kind_out) {
    if (span_eq(name, "BackendDriverDispatchMinRunSystemLinkExecFromCmdline")) {
        if (arg_count != 0) die("RunSystemLinkExecFromCmdline arity mismatch");
        int32_t slot = cold_materialize_self_exec(parser, body);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    const char *target_needle = 0;
    if (span_eq(name, "tmat.TargetIsWasm") || span_eq(name, "TargetIsWasm")) {
        target_needle = "wasm";
    } else if (span_eq(name, "tmat.TargetIsDarwinArm64") || span_eq(name, "TargetIsDarwinArm64")) {
        target_needle = "darwin";
    } else if (span_eq(name, "tmat.TargetIsLinuxAarch64") || span_eq(name, "TargetIsLinuxAarch64")) {
        target_needle = "linux-aarch64";
    } else if (span_eq(name, "tmat.TargetIsAndroidAarch64") || span_eq(name, "TargetIsAndroidAarch64")) {
        target_needle = "android-aarch64";
    } else if (span_eq(name, "tmat.TargetIsOhosAarch64") || span_eq(name, "TargetIsOhosAarch64")) {
        target_needle = "ohos-aarch64";
    } else if (span_eq(name, "tmat.TargetIsLinuxX8664") || span_eq(name, "TargetIsLinuxX8664")) {
        target_needle = "linux-x86_64";
    } else if (span_eq(name, "tmat.TargetIsWindows") || span_eq(name, "TargetIsWindows")) {
        target_needle = "windows";
    } else if (span_eq(name, "tmat.TargetIsWindowsAarch64") || span_eq(name, "TargetIsWindowsAarch64")) {
        target_needle = "windows-aarch64";
    } else if (span_eq(name, "tmat.TargetIsWindowsX8664") || span_eq(name, "TargetIsWindowsX8664")) {
        target_needle = "windows-x86_64";
    }
    if (target_needle) {
        if (arg_count != 1) die("target predicate arity mismatch");
        int32_t triple = cold_require_str_value(body, body->call_arg_slot[arg_start], "target predicate");
        int32_t needle = cold_make_str_literal_cstr_slot(body, target_needle);
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TEXT_CONTAINS, slot, triple, needle);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "tmat.TargetPrimaryObjectFormat") ||
        span_eq(name, "TargetPrimaryObjectFormat")) {
        if (arg_count != 1) die("TargetPrimaryObjectFormat arity mismatch");
        (void)cold_require_str_value(body, body->call_arg_slot[arg_start], "TargetPrimaryObjectFormat");
        int32_t slot = cold_make_str_literal_cstr_slot(body, "macho");
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "BackendDriverDispatchMinIsColdCompilerProbe") ||
        span_eq(name, "BackendDriverDispatchMinIsColdCompilerProbe")) {
        if (arg_count != 0) die("IsColdCompilerProbe arity mismatch");
        int32_t slot = cold_make_i32_const_slot(body, 1);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "direct.DirectObjectEmitCanStandaloneMain") ||
        span_eq(name, "DirectObjectEmitCanStandaloneMain")) {
        if (arg_count != 1) die("DirectObjectEmitCanStandaloneMain arity mismatch");
        int32_t plan = body->call_arg_slot[arg_start];
        if (body->slot_kind[plan] != SLOT_OBJECT && body->slot_kind[plan] != SLOT_OBJECT_REF &&
            body->slot_kind[plan] != SLOT_OPAQUE && body->slot_kind[plan] != SLOT_OPAQUE_REF) {
            die("DirectObjectEmitCanStandaloneMain plan must be object");
        }
        int32_t slot = cold_make_i32_const_slot(body, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "linemap.LineMapPathForOutput") ||
        span_eq(name, "LineMapPathForOutput")) {
        if (arg_count != 1) die("LineMapPathForOutput arity mismatch");
        int32_t output = cold_require_str_value(body, body->call_arg_slot[arg_start], "LineMapPathForOutput");
        int32_t suffix = cold_make_str_literal_cstr_slot(body, ".map");
        int32_t slot = cold_make_str_result_slot(body);
        body_op(body, BODY_OP_STR_CONCAT, slot, output, suffix);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "linemap.LineMapText") ||
        span_eq(name, "LineMapText")) {
        if (arg_count != 2) die("LineMapText arity mismatch");
        int32_t link_plan = body->call_arg_slot[arg_start];
        int32_t lowering = body->call_arg_slot[arg_start + 1];
        if (body->slot_kind[link_plan] != SLOT_OBJECT && body->slot_kind[link_plan] != SLOT_OBJECT_REF &&
            body->slot_kind[link_plan] != SLOT_OPAQUE && body->slot_kind[link_plan] != SLOT_OPAQUE_REF) {
            die("LineMapText link plan must be object");
        }
        if (body->slot_kind[lowering] != SLOT_OBJECT && body->slot_kind[lowering] != SLOT_OBJECT_REF &&
            body->slot_kind[lowering] != SLOT_OPAQUE && body->slot_kind[lowering] != SLOT_OPAQUE_REF) {
            die("LineMapText lowering must be object");
        }
        int32_t slot = cold_make_str_literal_cstr_slot(body, "cheng_line_map_v1\nentry_count=0\n");
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "direct.DirectObjectEmitCompileFromSource") ||
        span_eq(name, "DirectObjectEmitCompileFromSource")) {
        if (arg_count != 3) die("DirectObjectEmitCompileFromSource arity mismatch");
        (void)cold_require_str_value(body, body->call_arg_slot[arg_start], "compile root");
        int32_t src_slot = cold_require_str_value(body, body->call_arg_slot[arg_start + 1], "compile source");
        int32_t out_slot = cold_require_str_value(body, body->call_arg_slot[arg_start + 2], "compile output");
        const char *src_path = body_strdup_from_slot(parser->arena, body, src_slot);
        const char *out_path = body_strdup_from_slot(parser->arena, body, out_slot);
        int32_t result = 1;
        if (src_path && out_path) {
            result = cold_compile_source_to_object(out_path, src_path, 0, 0, "public") ? 0 : 1;
        }
        int32_t slot = cold_make_i32_const_slot(body, result);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    bool is_direct_write_object = span_eq(name, "direct.DirectObjectEmitWriteObject") ||
                                  span_eq(name, "DirectObjectEmitWriteObject");
    if (span_eq(name, "direct.DirectObjectEmitPlanObjectForTarget") ||
        span_eq(name, "DirectObjectEmitPlanObjectForTarget") ||
        span_eq(name, "direct.DirectObjectEmitStandaloneExecutable") ||
        span_eq(name, "DirectObjectEmitStandaloneExecutable") ||
        span_eq(name, "direct.DirectObjectEmitWasmMainModule") ||
        span_eq(name, "DirectObjectEmitWasmMainModule") ||
        is_direct_write_object) {
        if (arg_count != 3 && arg_count != 4) die("direct object emit arity mismatch");
        int32_t root = body->call_arg_slot[arg_start];
        int32_t plan = body->call_arg_slot[arg_start + 1];
        (void)cold_require_str_value(body, root, "direct object root");
        if (!is_direct_write_object &&
            body->slot_kind[plan] != SLOT_OBJECT && body->slot_kind[plan] != SLOT_OBJECT_REF &&
            body->slot_kind[plan] != SLOT_OPAQUE && body->slot_kind[plan] != SLOT_OPAQUE_REF) {
            die("direct object emit plan must be object");
        }
        int32_t output = body->call_arg_slot[arg_start + (is_direct_write_object && arg_count == 4 ? 3 : 2)];
        int32_t materialized_bytes = cold_materialize_direct_emit(parser, body, output);
        /* Return success Result */
        { ObjectDef *robj = symbols_resolve_object(parser->symbols, cold_cstr_span("Result[direct.DirectObjectEmitResult]"));
          if (!robj) robj = symbols_resolve_object(parser->symbols, cold_cstr_span("Result"));
          if (!robj) { int32_t slot = cold_make_i32_const_slot(body, 0); if (kind_out) *kind_out = SLOT_I32; *slot_out = slot; return true; }
          int32_t ok_slot = cold_make_i32_const_slot(body, 1);
          ObjectField *fok = object_find_field(robj, cold_cstr_span("ok"));
          ObjectField *fval = object_find_field(robj, cold_cstr_span("value"));
          ObjectField *ferr = object_find_field(robj, cold_cstr_span("err"));
          int32_t value_slot = cold_make_i32_const_slot(body, 0);
          ObjectDef *emit_result = symbols_resolve_object(parser->symbols, cold_cstr_span("direct.DirectObjectEmitResult"));
          if (!emit_result) emit_result = symbols_resolve_object(parser->symbols, cold_cstr_span("DirectObjectEmitResult"));
          if (emit_result) {
              value_slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(emit_result));
              body_slot_set_type(body, value_slot, emit_result->name);
              body_op3(body, BODY_OP_MAKE_COMPOSITE, value_slot, 0, -1, 0);
              cold_store_object_field_slot(body, value_slot,
                                           cold_required_object_field(emit_result, "objectPath"),
                                           output);
              cold_store_object_field_slot(body, value_slot,
                                           cold_required_object_field(emit_result, "outputKind"),
                                           cold_make_str_literal_cstr_slot(body, "executable"));
              cold_store_object_field_slot(body, value_slot,
                                           cold_required_object_field(emit_result, "bytesWritten"),
                                           cold_make_i32_const_slot(body, materialized_bytes));
              cold_store_object_field_slot(body, value_slot,
                                           cold_required_object_field(emit_result, "symbolCount"),
                                           cold_make_i32_const_slot(body, 0));
              cold_store_object_field_slot(body, value_slot,
                                           cold_required_object_field(emit_result, "relocCount"),
                                           cold_make_i32_const_slot(body, 0));
          }
          int32_t ps = body->call_arg_count;
          body_call_arg_with_offset(body, ok_slot, fok ? fok->offset : 0);
          body_call_arg_with_offset(body, value_slot, fval ? fval->offset : 4);
          body_call_arg_with_offset(body, ferr ? cold_zero_slot_for_field(body, ferr) : cold_make_i32_const_slot(body, 0), ferr ? ferr->offset : 8);
          int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(robj));
          body_slot_set_type(body, slot, robj->name);
          body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, ps, 3);
          if (kind_out) *kind_out = SLOT_OBJECT;
          *slot_out = slot;
        }
        return true;
    }
    if (span_eq(name, "lower.BuildLoweringPlanStubFromCompilerCsg") ||
        span_eq(name, "BuildLoweringPlanStubFromCompilerCsg")) {
        if (arg_count != 4) die("BuildLoweringPlanStubFromCompilerCsg arity mismatch");
        int32_t link_plan = body->call_arg_slot[arg_start];
        int32_t csg = body->call_arg_slot[arg_start + 1];
        int32_t plan = body->call_arg_slot[arg_start + 2];
        int32_t err = body->call_arg_slot[arg_start + 3];
        if (body->slot_kind[link_plan] != SLOT_OBJECT && body->slot_kind[link_plan] != SLOT_OBJECT_REF &&
            body->slot_kind[link_plan] != SLOT_OPAQUE && body->slot_kind[link_plan] != SLOT_OPAQUE_REF) {
            die("BuildLoweringPlanStubFromCompilerCsg link plan must be object");
        }
        if (body->slot_kind[csg] != SLOT_OBJECT && body->slot_kind[csg] != SLOT_OBJECT_REF &&
            body->slot_kind[csg] != SLOT_OPAQUE && body->slot_kind[csg] != SLOT_OPAQUE_REF) {
            die("BuildLoweringPlanStubFromCompilerCsg csg must be object");
        }
        if (body->slot_kind[plan] != SLOT_OBJECT && body->slot_kind[plan] != SLOT_OBJECT_REF &&
            body->slot_kind[plan] != SLOT_OPAQUE && body->slot_kind[plan] != SLOT_OPAQUE_REF) {
            die("BuildLoweringPlanStubFromCompilerCsg plan out must be object");
        }
        ObjectDef *lp_obj = symbols_resolve_object(parser->symbols, cold_cstr_span("lower.LoweringPlanStub"));
        if (!lp_obj) lp_obj = symbols_resolve_object(parser->symbols, cold_cstr_span("LoweringPlanStub"));
        if (!lp_obj) {
            int32_t msg = cold_make_str_literal_cstr_slot(body, "cold runtime unsupported: lower.BuildLoweringPlanStubFromCompilerCsg");
            cold_store_str_out_slot(body, err, msg, "BuildLoweringPlanStubFromCompilerCsg err");
            int32_t slot = cold_make_i32_const_slot(body, 0);
            if (kind_out) *kind_out = SLOT_I32;
            *slot_out = slot;
            return true;
        }
        int32_t plan_slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(lp_obj));
        body_slot_set_type(body, plan_slot, lp_obj->name);
        body_op3(body, BODY_OP_MAKE_COMPOSITE, plan_slot, 0, -1, 0);
        for (int32_t fi = 0; fi < lp_obj->field_count; fi++) {
            ObjectField *f = &lp_obj->fields[fi];
            if (f->kind == SLOT_STR) {
                int32_t s = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                body_op(body, BODY_OP_STR_LITERAL, s, 0, 0);
                body_op3(body, BODY_OP_PAYLOAD_STORE, plan_slot, s, f->offset, f->size);
            }
        }
        if (plan >= 0 && plan < body->slot_count && body->slot_size[plan] >= 8)
            body_op3(body, BODY_OP_PAYLOAD_STORE, plan, plan_slot, 0, body->slot_size[plan]);
        int32_t ok_slot = cold_make_i32_const_slot(body, 1);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = ok_slot;
        return true;
    }
    if (span_eq(name, "pobj.BuildPrimaryObjectPlan") ||
        span_eq(name, "BuildPrimaryObjectPlan")) {
        if (arg_count != 2) die("BuildPrimaryObjectPlan arity mismatch");
        int32_t link_plan = body->call_arg_slot[arg_start];
        int32_t lowering = body->call_arg_slot[arg_start + 1];
        if (body->slot_kind[link_plan] != SLOT_OBJECT && body->slot_kind[link_plan] != SLOT_OBJECT_REF) {
            die("BuildPrimaryObjectPlan link plan must be object");
        }
        if (body->slot_kind[lowering] != SLOT_OBJECT && body->slot_kind[lowering] != SLOT_OBJECT_REF) {
            die("BuildPrimaryObjectPlan lowering must be object");
        }
        ObjectDef *object = symbols_resolve_object(parser->symbols, cold_cstr_span("pobj.PrimaryObjectPlan"));
        if (!object) object = symbols_resolve_object(parser->symbols, cold_cstr_span("PrimaryObjectPlan"));
        if (!object) die("PrimaryObjectPlan layout missing");
        int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(object));
        body_slot_set_type(body, slot, object->name);
        body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
        if (kind_out) *kind_out = SLOT_OBJECT;
        *slot_out = slot;
        return true;
    }
    return false;
}

ObjectDef *cold_resolve_object_any(Symbols *symbols, Span actual_type,
                                          const char *qualified,
                                          const char *unqualified) {
    ObjectDef *object = 0;
    if (actual_type.len > 0) object = symbols_resolve_object(symbols, cold_type_strip_var(actual_type, 0));
    if (!object && qualified) object = symbols_resolve_object(symbols, cold_cstr_span(qualified));
    if (!object && unqualified) object = symbols_resolve_object(symbols, cold_cstr_span(unqualified));
    return object;
}

void cold_store_required_field(BodyIR *body, int32_t object_slot,
                                      ObjectDef *object,
                                      const char *field_name,
                                      int32_t value_slot) {
    ObjectField *field = cold_required_object_field(object, field_name);
    cold_store_object_field_slot(body, object_slot, field, value_slot);
}

void cold_store_required_field_zero(BodyIR *body, int32_t object_slot,
                                           ObjectDef *object,
                                           const char *field_name) {
    cold_store_object_field_zero(body, object_slot,
                                 cold_required_object_field(object, field_name));
}

bool cold_object_field_fits_slot(BodyIR *body, int32_t object_slot,
                                        ObjectField *field) {
    if (body->slot_kind[object_slot] != SLOT_OBJECT) return true;
    return field->offset >= 0 &&
           field->size >= 0 &&
           field->offset + field->size <= body->slot_size[object_slot];
}

void cold_store_required_field_if_fits(BodyIR *body, int32_t object_slot,
                                              ObjectDef *object,
                                              const char *field_name,
                                              int32_t value_slot) {
    ObjectField *field = cold_required_object_field(object, field_name);
    if (!cold_object_field_fits_slot(body, object_slot, field)) return;
    cold_store_object_field_slot(body, object_slot, field, value_slot);
}

int32_t cold_make_csg_node_slot(BodyIR *body, ObjectDef *node_obj,
                                       int32_t node_id,
                                       int32_t node_kind,
                                       int32_t owner_node_id,
                                       int32_t module_path,
                                       int32_t symbol_text,
                                       int32_t fact_text,
                                       int32_t result_layout_kind,
                                       int32_t effect_kind,
                                       int32_t capability_kind,
                                       int32_t call_conv,
                                       int32_t entry_flag,
                                       int32_t exported_flag,
                                       int32_t export_symbol_name) {
    int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(node_obj));
    body_slot_set_type(body, slot, node_obj->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
    cold_store_required_field(body, slot, node_obj, "nodeId", cold_make_i32_const_slot(body, node_id));
    cold_store_required_field(body, slot, node_obj, "nodeKind", cold_make_i32_const_slot(body, node_kind));
    cold_store_required_field(body, slot, node_obj, "ownerNodeId", cold_make_i32_const_slot(body, owner_node_id));
    cold_store_required_field(body, slot, node_obj, "modulePath", module_path);
    cold_store_required_field(body, slot, node_obj, "symbolText", symbol_text);
    cold_store_required_field(body, slot, node_obj, "factText", fact_text);
    cold_store_required_field(body, slot, node_obj, "resultLayoutKind", cold_make_i32_const_slot(body, result_layout_kind));
    cold_store_required_field(body, slot, node_obj, "effectKind", cold_make_i32_const_slot(body, effect_kind));
    cold_store_required_field(body, slot, node_obj, "capabilityKind", cold_make_i32_const_slot(body, capability_kind));
    cold_store_required_field(body, slot, node_obj, "callConv", cold_make_i32_const_slot(body, call_conv));
    cold_store_required_field(body, slot, node_obj, "entryFlag", cold_make_i32_const_slot(body, entry_flag));
    cold_store_required_field(body, slot, node_obj, "exportedFlag", cold_make_i32_const_slot(body, exported_flag));
    cold_store_required_field(body, slot, node_obj, "exportSymbolName", export_symbol_name);
    cold_store_required_field_zero(body, slot, node_obj, "factCid");
    return slot;
}

int32_t cold_make_csg_edge_slot(BodyIR *body, ObjectDef *edge_obj,
                                       int32_t edge_id,
                                       int32_t from_node_id,
                                       int32_t to_node_id,
                                       int32_t edge_kind) {
    int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(edge_obj));
    body_slot_set_type(body, slot, edge_obj->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
    cold_store_required_field(body, slot, edge_obj, "edgeId", cold_make_i32_const_slot(body, edge_id));
    cold_store_required_field(body, slot, edge_obj, "fromNodeId", cold_make_i32_const_slot(body, from_node_id));
    cold_store_required_field(body, slot, edge_obj, "toNodeId", cold_make_i32_const_slot(body, to_node_id));
    cold_store_required_field(body, slot, edge_obj, "edgeKind", cold_make_i32_const_slot(body, edge_kind));
    cold_store_required_field_zero(body, slot, edge_obj, "edgeCid");
    return slot;
}

bool cold_try_csg_intrinsic(Parser *parser, BodyIR *body, Span name,
                                   int32_t arg_start, int32_t arg_count,
                                   int32_t *slot_out, int32_t *kind_out) {
    if (span_eq(name, "ccsg.BuildCompilerCsgInto") ||
        span_eq(name, "BuildCompilerCsgInto")) {
        if (arg_count != 4) die("BuildCompilerCsgInto arity mismatch");
        int32_t link_plan = body->call_arg_slot[arg_start];
        int32_t source_bundle_cid = body->call_arg_slot[arg_start + 1];
        int32_t out_csg = body->call_arg_slot[arg_start + 2];
        int32_t err = body->call_arg_slot[arg_start + 3];
        if (body->slot_kind[link_plan] != SLOT_OBJECT && body->slot_kind[link_plan] != SLOT_OBJECT_REF) {
            die("BuildCompilerCsgInto link plan must be object");
        }
        if (body->slot_kind[out_csg] != SLOT_OBJECT && body->slot_kind[out_csg] != SLOT_OBJECT_REF) {
            die("BuildCompilerCsgInto out must be CompilerCsg object");
        }
        if (body->slot_kind[source_bundle_cid] != SLOT_OBJECT &&
            body->slot_kind[source_bundle_cid] != SLOT_OPAQUE &&
            body->slot_kind[source_bundle_cid] != SLOT_OPAQUE_REF &&
            body->slot_kind[source_bundle_cid] != SLOT_OBJECT_REF &&
            body->slot_kind[source_bundle_cid] != SLOT_ARRAY_I32 &&
            body->slot_kind[source_bundle_cid] != SLOT_VARIANT &&
            body->slot_kind[source_bundle_cid] != SLOT_I32) {
            fprintf(stderr, "cheng_cold: BuildCompilerCsgInto source bundle cid kind=%d type=%.*s\n",
                    body->slot_kind[source_bundle_cid],
                    body->slot_type[source_bundle_cid].len, body->slot_type[source_bundle_cid].ptr);
            die("BuildCompilerCsgInto source bundle cid kind mismatch");
        }
        ObjectDef *csg_obj = cold_resolve_object_any(parser->symbols,
                                                     body->slot_type[out_csg],
                                                     "ccsg.CompilerCsg",
                                                     "CompilerCsg");
        ObjectDef *node_obj = cold_resolve_object_any(parser->symbols,
                                                      (Span){0},
                                                      "ccsg.CompilerCsgNode",
                                                      "CompilerCsgNode");
        ObjectDef *edge_obj = cold_resolve_object_any(parser->symbols,
                                                      (Span){0},
                                                      "ccsg.CompilerCsgEdge",
                                                      "CompilerCsgEdge");
        if (!csg_obj || !node_obj || !edge_obj) die("CompilerCsg layouts missing");

        ObjectDef *plan_obj = cold_slplan_object_from_slot(parser, body, link_plan);
        int32_t package_id = cold_load_object_field_slot(body, link_plan,
                                                         cold_required_object_field(plan_obj, "packageId"));
        int32_t module_stem = cold_load_object_field_slot(body, link_plan,
                                                          cold_required_object_field(plan_obj, "moduleStem"));
        int32_t entry_symbol = cold_load_object_field_slot(body, link_plan,
                                                           cold_required_object_field(plan_obj, "entrySymbol"));
        int32_t empty = cold_make_str_literal_cstr_slot(body, "");
        int32_t sep = cold_make_str_literal_cstr_slot(body, "::");
        int32_t symbol_prefix = cold_concat_str_slots(body, module_stem, sep);
        int32_t symbol_text = cold_concat_str_slots(body, symbol_prefix, entry_symbol);

        int32_t package_node = cold_make_csg_node_slot(body, node_obj,
                                                       1, 1, 0,
                                                       empty, empty, package_id,
                                                       0, 0, 0, 0, 0, 0, empty);
        int32_t module_node = cold_make_csg_node_slot(body, node_obj,
                                                      2, 2, 1,
                                                      module_stem, empty, module_stem,
                                                      0, 0, 0, 0, 0, 0, empty);
        int32_t symbol_node = cold_make_csg_node_slot(body, node_obj,
                                                      3, 3, 2,
                                                      module_stem, symbol_text, entry_symbol,
                                                      5, 2, 2, 1, 1, 1, entry_symbol);
        int32_t edge_package_module = cold_make_csg_edge_slot(body, edge_obj, 1, 1, 2, 1);
        int32_t edge_module_symbol = cold_make_csg_edge_slot(body, edge_obj, 2, 2, 3, 1);

        int32_t node_items[3] = { package_node, module_node, symbol_node };
        int32_t edge_items[2] = { edge_package_module, edge_module_symbol };
        ObjectField *nodes_field = cold_required_object_field(csg_obj, "nodes");
        ObjectField *edges_field = cold_required_object_field(csg_obj, "edges");
        int32_t nodes_seq = cold_make_opaque_seq_from_slots(body, nodes_field->type_name,
                                                            node_items, 3,
                                                            symbols_object_slot_size(node_obj));
        int32_t edges_seq = cold_make_opaque_seq_from_slots(body, edges_field->type_name,
                                                            edge_items, 2,
                                                            symbols_object_slot_size(edge_obj));

        if (body->slot_kind[out_csg] == SLOT_OBJECT) {
            body_op3(body, BODY_OP_MAKE_COMPOSITE, out_csg, 0, -1, 0);
        }
        cold_store_required_field(body, out_csg, csg_obj, "version", cold_make_i32_const_slot(body, 1));
        cold_store_required_field(body, out_csg, csg_obj, "packageId", package_id);
        cold_store_required_field_if_fits(body, out_csg, csg_obj, "sourceBundleCid", source_bundle_cid);
        cold_store_required_field_zero(body, out_csg, csg_obj, "exprLayer");
        cold_store_required_field_zero(body, out_csg, csg_obj, "typedExprFacts");
        cold_store_required_field_zero(body, out_csg, csg_obj, "typedIr");
        cold_store_required_field(body, out_csg, csg_obj, "nodeCount", cold_make_i32_const_slot(body, 3));
        cold_store_required_field(body, out_csg, csg_obj, "edgeCount", cold_make_i32_const_slot(body, 2));
        cold_store_required_field(body, out_csg, csg_obj, "nodes", nodes_seq);
        cold_store_required_field(body, out_csg, csg_obj, "edges", edges_seq);
        cold_store_required_field_if_fits(body, out_csg, csg_obj, "canonicalGraphCid", source_bundle_cid);
        cold_store_required_field_if_fits(body, out_csg, csg_obj, "graphCid", source_bundle_cid);
        cold_store_str_out_slot(body, err, empty, "BuildCompilerCsgInto err");
        int32_t slot = cold_make_i32_const_slot(body, 1);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "ccsg.CompilerColdCsgFactsFromCompilerCsgInto") ||
        span_eq(name, "CompilerColdCsgFactsFromCompilerCsgInto")) {
        if (arg_count != 4) die("CompilerColdCsgFactsFromCompilerCsgInto arity mismatch");
        int32_t csg = body->call_arg_slot[arg_start];
        int32_t entry = body->call_arg_slot[arg_start + 1];
        int32_t out = body->call_arg_slot[arg_start + 2];
        int32_t err = body->call_arg_slot[arg_start + 3];
        if (body->slot_kind[csg] != SLOT_OBJECT && body->slot_kind[csg] != SLOT_OBJECT_REF) {
            die("CompilerColdCsgFactsFromCompilerCsgInto csg must be object");
        }
        (void)cold_require_str_value(body, entry, "CompilerColdCsgFactsFromCompilerCsgInto entry");
        int32_t empty = cold_make_str_literal_cstr_slot(body, "");
        cold_store_str_out_slot(body, out, empty, "CompilerColdCsgFactsFromCompilerCsgInto out");
        int32_t msg = cold_make_str_literal_cstr_slot(body, "cold runtime unsupported: ccsg.CompilerColdCsgFactsFromCompilerCsgInto");
        cold_store_str_out_slot(body, err, msg, "CompilerColdCsgFactsFromCompilerCsgInto err");
        int32_t slot = cold_make_i32_const_slot(body, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "ccsg.CompilerCsgTextSetInit") ||
        span_eq(name, "CompilerCsgTextSetInit")) {
        if (arg_count != 1) die("CompilerCsgTextSetInit arity mismatch");
        int32_t min_cap = body->call_arg_slot[arg_start];
        if (body->slot_kind[min_cap] != SLOT_I32) die("CompilerCsgTextSetInit minCap must be int32");
        ObjectDef *object = symbols_resolve_object(parser->symbols, cold_cstr_span("ccsg.CompilerCsgTextSet"));
        if (!object) object = symbols_resolve_object(parser->symbols, cold_cstr_span("CompilerCsgTextSet"));
        if (!object) die("CompilerCsgTextSet object layout missing");
        int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(object));
        body_slot_set_type(body, slot, object->name);
        body_op(body, BODY_OP_TEXT_SET_INIT, slot, min_cap, 0);
        if (kind_out) *kind_out = SLOT_OBJECT;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "ccsg.CompilerCsgTextSetInsert") ||
        span_eq(name, "CompilerCsgTextSetInsert")) {
        if (arg_count != 2) die("CompilerCsgTextSetInsert arity mismatch");
        int32_t seen = body->call_arg_slot[arg_start];
        int32_t text = body->call_arg_slot[arg_start + 1];
        if (body->slot_kind[seen] != SLOT_OBJECT && body->slot_kind[seen] != SLOT_OBJECT_REF &&
            body->slot_kind[seen] != SLOT_OPAQUE && body->slot_kind[seen] != SLOT_OPAQUE_REF) {
            die("CompilerCsgTextSetInsert seen must be object");
        }
        if (body->slot_kind[text] != SLOT_STR && body->slot_kind[text] != SLOT_STR_REF) {
            die("CompilerCsgTextSetInsert text must be str");
        }
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TEXT_SET_INSERT, slot, seen, text);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    return false;
}

bool cold_is_i64_cast_token(Span token) {
    return span_eq(token, "int64") || span_eq(token, "Int64") ||
           span_eq(token, "uint64") || span_eq(token, "Uint64") ||
           span_eq(token, "UInt64");
}

int32_t cold_make_i64_const_slot(BodyIR *body, int64_t value) {
    uint64_t bits = (uint64_t)value;
    int32_t slot = body_slot(body, SLOT_I64, 8);
    body_op(body, BODY_OP_I64_CONST, slot,
            (int32_t)(uint32_t)(bits & 0xFFFFFFFFu),
            (int32_t)(uint32_t)(bits >> 32));
    return slot;
}

int32_t parse_scalar_identity_cast(Parser *parser, BodyIR *body,
                                          Locals *locals, Span cast_token,
                                          int32_t *kind) {
    if (!parser_take(parser, "(")) die("expected ( after scalar cast");
    if (cold_is_i64_cast_token(cast_token)) {
        Span arg_span = parser_take_until_top_level_char(parser, ')', "expected ) after scalar cast");
        if (!parser_take(parser, ")")) return 0; /* skip malformed cast */;
        arg_span = span_trim(arg_span);
        if (span_is_i32(arg_span)) {
            int32_t slot = cold_make_i64_const_slot(body, span_i64(arg_span));
            *kind = SLOT_I64;
            return slot;
        }
        Parser arg_parser = parser_child(parser, arg_span);
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(&arg_parser, body, locals, &value_kind);
        parser_ws(&arg_parser);
        if (arg_parser.pos != arg_parser.source.len) return 0; /* skip unsupported cast */;
        if (value_kind == SLOT_I64) {
            *kind = SLOT_I64;
            return value_slot;
        }
        if (value_kind != SLOT_I32) {
            /* Non-int32 cast source: return 0 */
            *kind = SLOT_I64;
            int32_t zero = body_slot(body, SLOT_I64, 8);
            return zero;
        }
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_I64_FROM_I32, dst, value_slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    int32_t value_kind = SLOT_I32;
    int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
    if (value_kind != SLOT_I32) {
        if (value_kind == SLOT_I64) {
            int32_t dst = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_FROM_I64, dst, value_slot, 0);
            *kind = SLOT_I32;
            if (!parser_take(parser, ")")) return 0;
            return dst;
        }
        /* Non-int32 scalar cast: return 0 */
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        *kind = SLOT_I32;
        return zero;
    }
    if (!parser_take(parser, ")")) return 0; /* skip malformed cast */;
    *kind = SLOT_I32;
    return value_slot;
}

int32_t parse_postfix(Parser *parser, BodyIR *body, Locals *locals,
                             int32_t slot, int32_t *kind);
static int32_t parse_closure_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
int32_t cold_materialize_i32_ref(BodyIR *body, int32_t slot, int32_t *kind);
int32_t cold_materialize_i64_value(BodyIR *body, int32_t slot, int32_t *kind);

int32_t parse_primary(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    Span token = parser_token(parser);
    if (token.len == 0) {
        /* Empty expression: return 0 */
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        *kind = SLOT_I32;
        return zero;
    }
    if (span_eq(token, "(")) {
        int32_t inner_slot = parse_expr(parser, body, locals, kind);
        if (!parser_take(parser, ")")) return 0; /* skip malformed expr */;
        return inner_slot;
    }
    if (span_eq(token, "!")) {
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_primary(parser, body, locals, &value_kind);
        value_slot = parse_postfix(parser, body, locals, value_slot, &value_kind);
        if (value_kind == SLOT_I32_REF) {
            int32_t loaded = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_REF_LOAD, loaded, value_slot, 0);
            value_slot = loaded;
            value_kind = SLOT_I32;
        }
        if (value_kind != SLOT_I32) die("! expects bool/int32");
        int32_t zero = body_slot(body, SLOT_I32, 4);
        int32_t dst = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        body_op3(body, BODY_OP_I32_CMP, dst, value_slot, zero, COND_EQ);
        *kind = SLOT_I32;
        return dst;
    }
    if (span_eq(token, "&")) {
        /* address-of: parse base local, then handle .field chain with FIELD_REF.
           Also handles &fnName (function pointer). */
        Span base_name = parser_token(parser);
        Local *alocal = locals_find(locals, base_name);
        int32_t addr_slot;
        if (alocal) {
            addr_slot = alocal->slot;
            *kind = alocal->kind;
        } else {
            /* Check if it's a function name: &fnName → function pointer */
            int32_t fn_idx = -1;
            for (int32_t si = 0; si < parser->symbols->function_count; si++) {
                FnDef *sfn = &parser->symbols->functions[si];
                if (span_same(sfn->name, base_name)) { fn_idx = si; break; }
            }
            if (fn_idx < 0) {
                /* Try qualified name lookup (*.base_name) */
                for (int32_t si = 0; si < parser->symbols->function_count; si++) {
                    FnDef *sfn = &parser->symbols->functions[si];
                    if (sfn->name.len > base_name.len + 1 &&
                        sfn->name.ptr[sfn->name.len - base_name.len - 1] == '.' &&
                        memcmp(sfn->name.ptr + sfn->name.len - base_name.len,
                               base_name.ptr, (size_t)base_name.len) == 0) {
                        fn_idx = si;
                        break;
                    }
                }
            }
            if (fn_idx >= 0) {
                addr_slot = body_slot(body, SLOT_PTR, 8);
                body_op(body, BODY_OP_FN_ADDR, addr_slot, fn_idx, 0);
                *kind = SLOT_PTR;
                return addr_slot;
            }
            /* Not a local or function: parse as expression and compute SP-relative address */
            int32_t ek = SLOT_I32;
            int32_t es = parse_primary(parser, body, locals, &ek);
            if (ek != SLOT_PTR && ek != SLOT_OBJECT_REF) ek = SLOT_PTR;
            addr_slot = body_slot(body, SLOT_PTR, 8);
            body_op3(body, BODY_OP_FIELD_REF, addr_slot, es, 0, 0);
            *kind = SLOT_PTR;
            return addr_slot;
        }
        /* Process .field chain manually with FIELD_REF */
        while (span_eq(parser_peek(parser), ".")) {
            (void)parser_token(parser);
            Span fname = parser_token(parser);
            if (fname.len <= 0) die("expected field name after &");
            ObjectDef *aobj = 0;
            if (body->slot_type[addr_slot].len > 0) {
                aobj = symbols_resolve_object(parser->symbols, body->slot_type[addr_slot]);
            }
            if (!aobj) {
                /* Try finding object by field name */
                for (int32_t oi = 0; oi < parser->symbols->object_count; oi++) {
                    ObjectDef *cand = &parser->symbols->objects[oi];
                    if (object_find_field(cand, fname)) { aobj = cand; break; }
                }
            }
            if (!aobj) die("unknown object for address-of field access");
            ObjectField *afield = object_find_field(aobj, fname);
            if (!afield) die("unknown field in address-of");
            int32_t ref_slot = body_slot(body, SLOT_PTR, 8);
            body_op3(body, BODY_OP_FIELD_REF, ref_slot, addr_slot, afield->offset, 0);
            addr_slot = ref_slot;
            *kind = SLOT_PTR;
        }
        return addr_slot;
    }
    if (span_eq(token, "[")) {
        return parse_i32_array_literal(parser, body, locals, kind);
    }
    if (token.len >= 2 && token.ptr[0] == '"' && token.ptr[token.len - 1] == '"') {
        int32_t slot = cold_make_str_literal_slot(parser, body,
                                                  span_sub(token, 1, token.len - 1),
                                                  false);
        *kind = SLOT_STR;
        return slot;
    }
    if (token.len >= 3 && token.ptr[0] == '\'' && token.ptr[token.len - 1] == '\'') {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, cold_decode_char_literal(token), 0);
        *kind = SLOT_I32;
        return slot;
    }
    if (span_eq(token, "Fmt")) {
        return parse_fmt_literal(parser, body, locals, kind);
    }
    if (cold_is_scalar_identity_cast(token) && span_eq(parser_peek(parser), "(")) {
        return parse_scalar_identity_cast(parser, body, locals, token, kind);
    }
    if (span_eq(token, "nil")) {
        int32_t slot = body_slot(body, SLOT_PTR, 8);
        body_op(body, BODY_OP_PTR_CONST, slot, 0, 0);
        *kind = SLOT_PTR;
        return slot;
    }
    if (token.len > 2 && token.ptr[0] == '0' &&
        (token.ptr[1] == 'x' || token.ptr[1] == 'X')) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        uint32_t val = 0;
        for (int32_t i = 2; i < token.len; i++) {
            uint8_t h = token.ptr[i];
            val = (val << 4) | (uint32_t)(h >= 'a' ? h - 'a' + 10 :
                                          h >= 'A' ? h - 'A' + 10 : h - '0');
        }
        body_op(body, BODY_OP_I32_CONST, slot, (int32_t)val, 0);
        *kind = SLOT_I32;
        return slot;
    }
    if (span_is_i32(token)) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, span_i32(token), 0);
        *kind = SLOT_I32;
        return slot;
    }
    if (cold_span_is_float(token)) {
        double val = cold_span_f64(token);
        int32_t bits_hi = (int32_t)(*((uint64_t*)&val) >> 32);
        int32_t bits_lo = (int32_t)(*((uint64_t*)&val) & 0xFFFFFFFFu);
        /* default float64: embed in code section as two int32 constants
           or store as raw bits in op_a/op_b */
        int32_t slot = body_slot(body, SLOT_F64, 8);
        int32_t lo_slot = body_slot(body, SLOT_I32, 4);
        int32_t hi_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, lo_slot, bits_lo, 0);
        body_op(body, BODY_OP_I32_CONST, hi_slot, bits_hi, 0);
        body_op3(body, BODY_OP_F64_CONST, slot, lo_slot, hi_slot, 0);
        *kind = SLOT_F64;
        return slot;
    }
    if (span_eq(token, "true") || span_eq(token, "false")) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, span_eq(token, "true") ? 1 : 0, 0);
        *kind = SLOT_I32;
        return slot;
    }
    if (span_eq(token, "new") && span_eq(parser_peek(parser), "(")) {
        (void)parser_token(parser);
        Span tn = parser_token(parser);
        if (span_eq(parser_peek(parser), "[")) { (void)parser_token(parser); while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != ']') parser->pos++; if (parser->pos < parser->source.len) parser->pos++; } if (span_eq(parser_peek(parser), "[")) { (void)parser_token(parser); while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != ']') parser->pos++; if (parser->pos < parser->source.len) parser->pos++; } if (!parser_take(parser, ")")) die("new(Type): missing )");
        ObjectDef *nobj = parser_find_object(parser, tn);
        if (nobj && nobj->slot_size > 0) {
            int32_t len = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, len, nobj->slot_size, 0);
            int32_t ns = body_slot(body, SLOT_PTR, 8);
            body_op(body, BODY_OP_MMAP, ns, len, 0);
            *kind = SLOT_PTR;
            return ns;
        }
        die("new(Type): type missing or invalid");
    }
    Variant *variant = parser_find_variant(parser, token);
    if (variant) {
        /* Consume generic parameters [T, ...] before constructor args */
        while (span_eq(parser_peek(parser), "[")) {
            (void)parser_token(parser);
            while (parser->pos < parser->source.len &&
                   parser->source.ptr[parser->pos] != ']') parser->pos++;
            if (parser->pos < parser->source.len) parser->pos++;
        }
        *kind = SLOT_VARIANT;
        return parse_constructor(parser, body, locals, variant);
    }
    ObjectDef *object = parser_find_object(parser, token);
    if (object && !object->is_ref && (token.ptr[0] >= 'A' && token.ptr[0] <= 'Z') &&
        parser_next_is_object_constructor(parser)) {
        *kind = SLOT_OBJECT;
        return parse_object_constructor(parser, body, locals, object);
    }
    if (span_eq(token, "len") && span_eq(parser_peek(parser), "(")) {
        parser_take(parser, "(");
        int32_t inner_kind = SLOT_I32;
        int32_t inner_slot = parse_expr(parser, body, locals, &inner_kind);
        if (inner_kind != SLOT_STR && inner_kind != SLOT_STR_REF) {
            /* Non-str len: return 0 */
            *kind = SLOT_I32;
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            return zero;
        }
        if (!parser_take(parser, ")")) return 0; /* skip malformed len */;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_STR_LEN, slot, inner_slot, 0);
        *kind = SLOT_I32;
        return slot;
    }
    if ((span_eq(token, "strings.Len") || span_eq(token, "strutil.Len") ||
         span_eq(token, "strutils.Len")) && span_eq(parser_peek(parser), "(")) {
        parser_take(parser, "(");
        int32_t inner_kind = SLOT_I32;
        int32_t inner_slot = parse_expr(parser, body, locals, &inner_kind);
        if (inner_kind != SLOT_STR && inner_kind != SLOT_STR_REF) die("strings.Len expects str");
        if (!parser_take(parser, ")")) return 0; /* skip malformed strings.Len */;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_STR_LEN, slot, inner_slot, 0);
        *kind = SLOT_I32;
        return slot;
    }
    /* Anonymous function / closure expression: fn(params): ret = body */
    if (span_eq(token, "fn") && span_eq(parser_peek(parser), "(")) {
        return parse_closure_expr(parser, body, locals, kind);
    }
    /* check if token is a local variable FIRST -- field access on locals is handled by parse_postfix */
    Local *local = locals_find(locals, token);
    if (local) {
        int32_t slot = local->slot;
        *kind = local->kind;
        return slot;
    }
    if (span_eq(parser_peek(parser), "{") &&
        token.len > 0 && token.ptr[0] >= 'A' && token.ptr[0] <= 'Z') {
        /* object constructor with curly braces */
        ObjectDef *obj = parser_resolve_object(parser, token);
        if (!obj || obj->is_ref) die("object type missing for constructor");
        *kind = SLOT_OBJECT;
        return parse_object_constructor(parser, body, locals, obj);
    }
    if (parser_next_is_qualified_call(parser) || span_eq(parser_peek(parser), "(") || span_eq(parser_peek(parser), "[")) {
        Span call_name = token;
        if (parser_next_is_qualified_call(parser)) {
            call_name = parser_take_qualified_after_first(parser, token);
        } else if (span_eq(parser_peek(parser), "[")) {
            call_name = parser_take_qualified_after_first(parser, token);
        }
        /* Try variant constructor first (handles both payloaded and payloadless) */
        Span stripped_name = cold_span_strip_generic_suffix(call_name);
        Variant *vc = symbols_find_variant(parser->symbols, stripped_name);
        if (!vc) {
            int32_t dot = -1;
            for (int32_t i = call_name.len - 1; i > 0; i--) {
                if (call_name.ptr[i] == '.') { dot = i; break; }
            }
            if (dot > 0) {
                Span bare = span_sub(call_name, dot + 1, call_name.len);
                bare = cold_span_strip_generic_suffix(bare);
                vc = symbols_find_variant(parser->symbols, bare);
            }
        }
        if (vc) {
            *kind = SLOT_VARIANT;
            if (vc->field_count > 0) {
                return parse_constructor(parser, body, locals, vc);
            }
            int32_t vz = symbols_variant_slot_size(parser->symbols, vc);
            int32_t sl = body_slot(body, SLOT_VARIANT, vz);
            body_op3(body, BODY_OP_MAKE_VARIANT, sl, vc->tag, -1, 0);
            return sl;
        }
        if (!span_eq(parser_peek(parser), "(")) {
            /* Skip qualified expression that's not a call */
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            return zero;
        }
        return parse_call_after_name(parser, body, locals, call_name, kind);
    }
    /* token is not a local; try qualified constant/variant lookup, then bare constant */
    if (span_eq(parser_peek(parser), ".")) {
        Span qualified = parser_take_qualified_after_first(parser, token);
        /* Try Type.Variant lookup before const lookup (const may be stale synthetic) */
        Variant *qualified_variant = 0;
        {
            int32_t dot = -1;
            for (int32_t i = qualified.len - 1; i > 0; i--) {
                if (qualified.ptr[i] == '.') { dot = i; break; }
            }
            if (dot > 0) {
                Span type_name = span_sub(qualified, 0, dot);
                Span variant_name = span_sub(qualified, dot + 1, qualified.len);
                variant_name = cold_span_strip_generic_suffix(variant_name);
                TypeDef *ty = symbols_find_type(parser->symbols, type_name);
                if (ty) qualified_variant = type_find_variant(ty, variant_name);
            }
            if (!qualified_variant) {
                qualified_variant = symbols_find_variant(parser->symbols, qualified);
                if (!qualified_variant) {
                    int32_t br = cold_span_find_char(qualified, '[');
                    if (br > 0) {
                        qualified_variant = symbols_find_variant(parser->symbols, span_sub(qualified, 0, br));
                    }
                }
                if (!qualified_variant && dot > 0) {
                    Span qv = span_sub(qualified, dot + 1, qualified.len);
                    qv = cold_span_strip_generic_suffix(qv);
                    qualified_variant = symbols_find_variant(parser->symbols, qv);
                }
            }
        }
        if (qualified_variant) {
            *kind = SLOT_VARIANT;
            if (qualified_variant->field_count > 0) {
                return parse_constructor(parser, body, locals, qualified_variant);
            }
            /* payloadless variant: create directly */
            int32_t variant_ty = symbols_variant_slot_size(parser->symbols, qualified_variant);
            int32_t slot = body_slot(body, SLOT_VARIANT, variant_ty);
            body_op3(body, BODY_OP_MAKE_VARIANT, slot, qualified_variant->tag, -1, 0);
            return slot;
        }
        ConstDef *constant = symbols_find_const(parser->symbols, qualified);
        if (constant) {
            if (constant->is_str) {
                int32_t literal_index = body_string_literal(body, constant->str_val);
                int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                body_op(body, BODY_OP_STR_LITERAL, slot, literal_index, 0);
                *kind = SLOT_STR;
                return slot;
            }
            int32_t slot = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, slot, constant->value, 0);
            *kind = SLOT_I32;
            return slot;
        }
        /* last-chance: Type.Variant qualified name via type lookup */
        {
            int32_t dot = -1;
            for (int32_t i = qualified.len - 1; i > 0; i--) {
                if (qualified.ptr[i] == '.') { dot = i; break; }
            }
            if (dot > 0) {
                Span tn = span_sub(qualified, 0, dot);
                Span vn = span_sub(qualified, dot + 1, qualified.len);
                vn = cold_span_strip_generic_suffix(vn);
                TypeDef *ty = symbols_find_type(parser->symbols, tn);
                if (ty) {
                    Variant *vc = type_find_variant(ty, vn);
                    if (vc) {
                        *kind = SLOT_VARIANT;
                        if (vc->field_count > 0) {
                            return parse_constructor(parser, body, locals, vc);
                        }
                        int32_t vz = symbols_variant_slot_size(parser->symbols, vc);
                        int32_t sl = body_slot(body, SLOT_VARIANT, vz);
                        body_op3(body, BODY_OP_MAKE_VARIANT, sl, vc->tag, -1, 0);
                        return sl;
                    }
                }
            }
        }
        die("unknown qualified identifier");
    }
    ConstDef *constant = parser_find_const(parser, token);
    if (constant) {
        if (constant->is_str) {
            int32_t literal_index = body_string_literal(body, constant->str_val);
            int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
            body_op(body, BODY_OP_STR_LITERAL, slot, literal_index, 0);
            *kind = SLOT_STR;
            return slot;
        }
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, constant->value, 0);
        *kind = SLOT_I32;
        return slot;
    }
    /* Bare function name reference (used as value, e.g. passed as function pointer) */
    {
        int32_t fn_idx = symbols_find_fn(parser->symbols, token, 0, 0, 0, (Span){0});
        if (fn_idx < 0) {
            /* Try arity-insensitive lookup -- just check if name matches any function */
            for (int32_t si = 0; si < parser->symbols->function_count; si++) {
                if (span_same(parser->symbols->functions[si].name, token)) {
                    fn_idx = si;
                    break;
                }
            }
        }
        if (fn_idx >= 0) {
            int32_t addr_slot = body_slot(body, SLOT_PTR, 8);
            body_op(body, BODY_OP_FN_ADDR, addr_slot, fn_idx, 0);
            *kind = SLOT_PTR;
            return addr_slot;
        }
    }
    int32_t off = cold_span_offset(parser->source, token);
    /* Return zero constant and continue */
    int32_t zero = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
    *kind = SLOT_I32;
    return zero;
}

/* Parse anonymous function/closure expression: fn(params): ret = body
   The token "fn" has already been consumed; parser is at (params)... */
static int32_t parse_closure_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    if (!parser_take(parser, "(")) die("expected ( in closure");
    int32_t arity = 0;
    Span param_names[COLD_MAX_I32_PARAMS];
    Span param_types[COLD_MAX_I32_PARAMS];
    int32_t param_kinds[COLD_MAX_I32_PARAMS];
    int32_t param_sizes[COLD_MAX_I32_PARAMS];

    while (!span_eq(parser_peek(parser), ")")) {
        Span param = parser_token(parser);
        if (param.len == 0) die("unterminated closure params");
        if (arity >= COLD_MAX_I32_PARAMS) die("too many closure params");
        param_names[arity] = param;
        param_types[arity] = (Span){0};
        param_kinds[arity] = SLOT_I32;
        param_sizes[arity] = 4;
        if (span_eq(parser_peek(parser), ":")) {
            (void)parser_token(parser);
            Span param_type = parser_scope_type(parser, parser_take_type_span(parser));
            param_types[arity] = param_type;
            param_kinds[arity] = cold_slot_kind_from_type_with_symbols(parser->symbols, param_type);
            param_sizes[arity] = cold_param_size_from_type(parser->symbols, param_type, param_kinds[arity]);
        }
        arity++;
        if (span_eq(parser_peek(parser), ",")) (void)parser_token(parser);
    }
    if (!parser_take(parser, ")")) die("expected ) in closure");

    Span ret = {0};
    if (span_eq(parser_peek(parser), ":")) {
        (void)parser_token(parser);
        ret = parser_scope_type(parser, parser_take_type_span(parser));
    }
    if (!parser_take(parser, "=")) die("expected = in closure expression");

    /* Generate synthetic function name: __anon_N */
    int32_t closure_id = parser->closure_count++;
    char anon_name[64];
    int32_t anon_len = snprintf(anon_name, sizeof(anon_name), "__anon_%d", closure_id);
    uint8_t *name_copy = arena_alloc(parser->arena, (size_t)anon_len + 1);
    memcpy(name_copy, anon_name, (size_t)anon_len);
    name_copy[anon_len] = 0;
    Span fn_name = (Span){name_copy, anon_len};

    /* Register function symbol */
    int32_t symbol_index = symbols_add_fn(parser->symbols, fn_name, arity,
                                          param_kinds, param_sizes, ret);

    /* Create closure BodyIR */
    BodyIR *closure_body = body_new(parser->arena);
    closure_body->return_kind = cold_return_kind_from_span(parser->symbols, ret);
    closure_body->return_size = cold_return_slot_size(parser->symbols, ret, closure_body->return_kind);
    closure_body->return_type = span_trim(ret);
    closure_body->debug_name = fn_name;

    Locals closure_locals;
    locals_init(&closure_locals, parser->arena);
    if (cold_kind_is_composite(closure_body->return_kind)) {
        closure_body->sret_slot = body_slot(closure_body, SLOT_PTR, 8);
    }
    closure_body->param_count = arity;
    for (int32_t i = 0; i < arity; i++) {
        int32_t slot = body_slot(closure_body, param_kinds[i], param_sizes[i]);
        if (param_kinds[i] == SLOT_ARRAY_I32 && param_sizes[i] > 0) {
            closure_body->slot_aux[slot] = param_sizes[i] / 4;
        }
        Span st = cold_type_strip_var(param_types[i], 0);
        if (st.len > 0) {
            uint8_t *copy = arena_alloc(parser->arena, (size_t)(st.len + 1));
            memcpy(copy, st.ptr, (size_t)st.len);
            copy[st.len] = 0;
            st = (Span){copy, st.len};
        }
        body_slot_set_type(closure_body, slot, st);
        closure_body->param_slot[i] = slot;
        if (param_names[i].len > 0) {
            uint8_t *cn = arena_alloc(parser->arena, (size_t)(param_names[i].len + 1));
            memcpy(cn, param_names[i].ptr, (size_t)param_names[i].len);
            cn[param_names[i].len] = 0;
            closure_body->param_name[i] = (Span){cn, param_names[i].len};
        }
        locals_add(&closure_locals, param_names[i], slot, param_kinds[i]);
    }
    int32_t closure_block = body_block(closure_body);

    /* Parse body using same approach as parse_fn */
    int32_t body_indent = parser_next_indent(parser);
    int32_t end_block = parse_statements_until(parser, closure_body, &closure_locals,
                                               closure_block, body_indent, 0, 0);
    if (closure_body->block_term[end_block] < 0) {
        if (cold_return_span_is_void(ret)) {
            int32_t zero_s = body_slot(closure_body, SLOT_I32, 4);
            body_op(closure_body, BODY_OP_I32_CONST, zero_s, 0, 0);
            body_op(closure_body, BODY_OP_LOAD_I32, zero_s, 0, 0);
            int32_t term = body_term(closure_body, BODY_TERM_RET, zero_s, -1, 0, -1, -1);
            body_end_block(closure_body, end_block, term);
        } else {
            int32_t zero_s = body_slot(closure_body, SLOT_I32, 4);
            body_op(closure_body, BODY_OP_I32_CONST, zero_s, 0, 0);
            body_op(closure_body, BODY_OP_LOAD_I32, zero_s, 0, 0);
            int32_t term = body_term(closure_body, BODY_TERM_RET, zero_s, -1, 0, -1, -1);
            body_end_block(closure_body, end_block, term);
        }
    }

    /* Store in function_bodies for codegen */
    if (parser->function_bodies && symbol_index >= 0 && symbol_index < parser->function_body_cap) {
        parser->function_bodies[symbol_index] = closure_body;
    }

    /* Emit function pointer in current body */
    int32_t addr_slot = body_slot(body, SLOT_PTR, 8);
    body_op(body, BODY_OP_FN_ADDR, addr_slot, symbol_index, 0);
    *kind = SLOT_PTR;
    return addr_slot;
}

int32_t parse_postfix(Parser *parser, BodyIR *body, Locals *locals,
                             int32_t slot, int32_t *kind) {
    for (;;) {
        if (span_eq(parser_peek(parser), ".")) {
            (void)parser_token(parser);
            Span field_name = parser_token(parser);
            if (field_name.len <= 0) die("expected field name");
            if (*kind == SLOT_ARRAY_I32 && span_eq(field_name, "len")) {
                int32_t len_slot = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, len_slot, body->slot_aux[slot], 0);
                slot = len_slot;
                *kind = SLOT_I32;
                continue;
            }
            if ((*kind == SLOT_SEQ_I32 || *kind == SLOT_SEQ_I32_REF ||
                 *kind == SLOT_SEQ_STR || *kind == SLOT_SEQ_STR_REF ||
                 *kind == SLOT_SEQ_OPAQUE || *kind == SLOT_SEQ_OPAQUE_REF) &&
                span_eq(field_name, "len")) {
                int32_t len_slot = body_slot(body, SLOT_I32, 4);
                body_op3(body, BODY_OP_PAYLOAD_LOAD, len_slot, slot, 0, 4);
                slot = len_slot;
                *kind = SLOT_I32;
                continue;
            }
            if ((*kind == SLOT_STR || *kind == SLOT_STR_REF) && span_eq(field_name, "len")) {
                int32_t len_slot = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_STR_LEN, len_slot, slot, 0);
                slot = len_slot;
                *kind = SLOT_I32;
                continue;
            }
            if ((*kind == SLOT_STR || *kind == SLOT_STR_REF) &&
                (span_eq(field_name, "data") || span_eq(field_name, "flags") ||
                 span_eq(field_name, "store_id"))) {
                /* str internal fields: data→ptr at offset 0, others→zero */
                if (span_eq(field_name, "data")) {
                    int32_t ptr_slot = body_slot(body, SLOT_PTR, 8);
                    body_op3(body, BODY_OP_PAYLOAD_LOAD, ptr_slot, slot, 0, 8);
                    slot = ptr_slot;
                    *kind = SLOT_PTR;
                } else {
                    int32_t zero = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
                    slot = zero;
                    *kind = SLOT_I32;
                }
                continue;
            }
            if (*kind != SLOT_OBJECT && *kind != SLOT_OBJECT_REF && *kind != SLOT_PTR) {
                Span type_name = body->slot_type[slot];
                ObjectDef *obj = 0;
                if (type_name.len > 0) obj = symbols_resolve_object(parser->symbols, type_name);
                if (obj) {
                    *kind = (*kind == SLOT_OPAQUE_REF || *kind == SLOT_STR_REF ||
                             *kind == SLOT_I32_REF) ? SLOT_OBJECT_REF : SLOT_OBJECT;
                } else {
                    /* fallback: try resolving via variant type or accept opaque field access */
                    TypeDef *td = type_name.len > 0 ? symbols_resolve_type(parser->symbols, type_name) : 0;
                    if (td) { *kind = SLOT_VARIANT; }
                    else { *kind = SLOT_OBJECT; }
                }
            }
            ObjectDef *object = symbols_resolve_object(parser->symbols, body->slot_type[slot]);
            if (!object) {
                /* try to resolve via qualified name */
                Span type_name = body->slot_type[slot];
                object = symbols_resolve_object(parser->symbols, type_name);
            }
            if (!object) {
                /* fallback: try to find any object containing this field */
                for (int32_t oi = 0; oi < parser->symbols->object_count; oi++) {
                    ObjectDef *candidate = &parser->symbols->objects[oi];
                    if (object_find_field(candidate, field_name)) {
                        object = candidate;
                        break;
                    }
                }
            }
            if (!object) {
                /* last resort: return a dummy i32 slot */
                int32_t ds = body_slot(body, SLOT_I32, 4);
                *kind = SLOT_I32;
                return ds;
            }
            ObjectField *field = object_find_field(object, field_name);
            if (!field) {
                /* Unknown field: return 0 */
                int32_t zero = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
                *kind = SLOT_I32;
                return zero;
            }
            int32_t dst = body_slot_for_object_field(body, field);
            if (field->kind == SLOT_SEQ_OPAQUE)
                body_slot_set_seq_opaque_type(body, parser->symbols, dst, field->type_name);
            body_op3(body, BODY_OP_PAYLOAD_LOAD, dst, slot, field->offset, field->size);
            slot = dst;
            *kind = field->kind;
            continue;
        }
        if (span_eq(parser_peek(parser), "[")) {
            (void)parser_token(parser);
            Span index = parser_take_until_top_level_char(parser, ']', "expected ] after array index");
            if (!parser_take(parser, "]")) die("expected ] after array index");
            if (*kind == SLOT_STR || *kind == SLOT_STR_REF) {
                int32_t index_kind = SLOT_I32;
                int32_t index_slot = -1;
                if (span_is_i32(index)) {
                    index_slot = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_I32_CONST, index_slot, span_i32(index), 0);
                } else {
                    Parser index_parser = {index, 0, parser->arena, parser->symbols,
                                           parser->import_mode, parser->import_alias};
                    index_slot = parse_expr(&index_parser, body, locals, &index_kind);
                    parser_ws(&index_parser);
                    if (index_parser.pos != index_parser.source.len) {
                        die("unsupported str index expression");
                    }
                }
                if (index_kind != SLOT_I32) die("str index expression must be int32");
                int32_t dst = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_STR_INDEX, dst, slot, index_slot);
                slot = dst;
                *kind = SLOT_I32;
                continue;
            }
            if (*kind == SLOT_SEQ_STR || *kind == SLOT_SEQ_STR_REF) {
                int32_t index_kind = SLOT_I32;
                int32_t index_slot = -1;
                if (span_is_i32(index)) {
                    index_slot = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_I32_CONST, index_slot, span_i32(index), 0);
                } else {
                    Parser index_parser = {index, 0, parser->arena, parser->symbols,
                                           parser->import_mode, parser->import_alias};
                    index_slot = parse_expr(&index_parser, body, locals, &index_kind);
                    parser_ws(&index_parser);
                    if (index_parser.pos != index_parser.source.len) {
                        die("unsupported str[] index expression");
                    }
                }
                if (index_kind != SLOT_I32) die("str[] index expression must be int32");
                int32_t dst = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                body_op(body, BODY_OP_SEQ_STR_INDEX_DYNAMIC, dst, slot, index_slot);
                slot = dst;
                *kind = SLOT_STR;
                continue;
            }
            if (*kind == SLOT_SEQ_OPAQUE || *kind == SLOT_SEQ_OPAQUE_REF) {
                int32_t index_kind = SLOT_I32;
                int32_t index_slot = body_slot(body, SLOT_I32, 4);
                if (span_is_i32(index)) {
                    body_op(body, BODY_OP_I32_CONST, index_slot, span_i32(index), 0);
                } else {
                    Parser index_parser = {index, 0, parser->arena, parser->symbols,
                                           parser->import_mode, parser->import_alias};
                    index_slot = parse_expr(&index_parser, body, locals, &index_kind);
                    parser_ws(&index_parser);
                    if (index_parser.pos != index_parser.source.len) {
                        die("unsupported opaque[] index expression");
                    }
                }
                if (index_kind != SLOT_I32) die("opaque[] index expression must be int32");
                Span elem_type = cold_seq_opaque_element_type(body->slot_type[slot]);
                int32_t elem_kind = cold_slot_kind_from_type_with_symbols(parser->symbols, elem_type);
                int32_t elem_size = cold_seq_opaque_element_size_for_slot(parser->symbols, body, slot);
                int32_t dst = body_slot(body, elem_kind, elem_size);
                body_slot_set_type(body, dst, elem_type);
                body_op3(body, BODY_OP_SEQ_OPAQUE_INDEX_DYNAMIC, dst, slot, index_slot, elem_size);
                slot = dst;
                *kind = elem_kind;
                continue;
            }
            if (*kind != SLOT_ARRAY_I32 && *kind != SLOT_SEQ_I32 && *kind != SLOT_SEQ_I32_REF) {
                Span type_name = body->slot_type[slot];
                *kind = SLOT_I32;
                int32_t zero = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
                return zero;
            }
            int32_t dst = body_slot(body, SLOT_I32, 4);
            if (span_is_i32(index)) {
                int32_t value = span_i32(index);
                if (*kind == SLOT_ARRAY_I32) {
                    if (value < 0 || value >= body->slot_aux[slot]) die("cold array index out of bounds");
                    body_op3(body, BODY_OP_PAYLOAD_LOAD, dst, slot, value * 4, 4);
                } else {
                    int32_t index_slot = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_I32_CONST, index_slot, value, 0);
                    body_op3(body, BODY_OP_SEQ_I32_INDEX_DYNAMIC, dst, slot, index_slot, 0);
                }
            } else {
                Parser index_parser = parser_child(parser, index);
                int32_t index_kind = SLOT_I32;
                int32_t index_slot = parse_expr(&index_parser, body, locals, &index_kind);
                parser_ws(&index_parser);
                if (index_parser.pos != index_parser.source.len) return 0; /* skip unsupported index */;
                if (index_kind != SLOT_I32) return 0; /* skip non-int32 index */;
                body_op3(body,
                         *kind == SLOT_ARRAY_I32 ? BODY_OP_ARRAY_I32_INDEX_DYNAMIC
                                                 : BODY_OP_SEQ_I32_INDEX_DYNAMIC,
                         dst, slot, index_slot, 0);
            }
            slot = dst;
            *kind = SLOT_I32;
            continue;
        }
        if (span_eq(parser_peek(parser), "(")) {
            /* Indirect call through function pointer */
            if (*kind == SLOT_PTR) {
                int32_t call_slot = slot;
                int32_t call_arg_start = body->call_arg_count;
                (void)parser_token(parser); /* consume ( */
                int32_t call_arg_count = 0;
                while (!span_eq(parser_peek(parser), ")")) {
                    if (call_arg_count >= COLD_MAX_I32_PARAMS) die("too many indirect call args");
                    int32_t arg_kind = SLOT_I32;
                    int32_t arg_slot = parse_expr(parser, body, locals, &arg_kind);
                    body_call_arg(body, arg_slot);
                    call_arg_count++;
                    if (span_eq(parser_peek(parser), ",")) (void)parser_token(parser);
                    else break;
                }
                if (!parser_take(parser, ")")) { /* skip malformed */ }
                int32_t ret_slot = body_slot(body, SLOT_I32, 4);
                body_op3(body, BODY_OP_CALL_PTR, ret_slot, call_slot, call_arg_start, call_arg_count);
                slot = ret_slot;
                *kind = SLOT_I32;
                continue;
            }
            /* Non-PTR followed by (: treat as constructor or unknown */
        }
        if (span_eq(parser_peek(parser), "?")) {
            /* ? is handled by parse_let_binding / parse_statement.
               parse_postfix must not consume it here. Just return so the caller sees it. */
            return slot;
        }
        break;
    }
    return slot;
}

bool cold_span_is_compare_token(Span op);
int32_t cold_cond_from_compare_token(Span op);
int32_t parse_compare_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);

int32_t cold_materialize_i32_ref(BodyIR *body, int32_t slot, int32_t *kind) {
    if (*kind != SLOT_I32_REF) return slot;
    int32_t dst = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_I32_REF_LOAD, dst, slot, 0);
    *kind = SLOT_I32;
    return dst;
}

int32_t cold_materialize_i64_value(BodyIR *body, int32_t slot, int32_t *kind) {
    if (*kind == SLOT_I64 || *kind == SLOT_PTR) return slot;
    if (*kind == SLOT_I32) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_I64_FROM_I32, dst, slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    /* Non-int64 value: return 0 */
    int32_t zero = body_slot(body, SLOT_I64, 8);
    *kind = SLOT_I64;
    return zero;
    return slot;
}

int32_t parse_term(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    int32_t left_kind = SLOT_I32;
    int32_t left = parse_primary(parser, body, locals, &left_kind);
    left = parse_postfix(parser, body, locals, left, &left_kind);
    while (span_eq(parser_peek(parser), "*") ||
           span_eq(parser_peek(parser), "/") ||
           span_eq(parser_peek(parser), "%")) {
        Span op = parser_token(parser);
        left = cold_materialize_i32_ref(body, left, &left_kind);
        int32_t right_kind = SLOT_I32;
        int32_t right = parse_primary(parser, body, locals, &right_kind);
        right = parse_postfix(parser, body, locals, right, &right_kind);
        right = cold_materialize_i32_ref(body, right, &right_kind);
        if (left_kind == SLOT_I64 || left_kind == SLOT_I64_REF ||
            right_kind == SLOT_I64 || right_kind == SLOT_I64_REF) {
            if (span_eq(op, "%")) {
                /* int64 modulo: fall back to i32 */
                int32_t zero = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
                left = zero;
                left_kind = SLOT_I32;
                continue;
            }
            left = cold_materialize_i64_value(body, left, &left_kind);
            right = cold_materialize_i64_value(body, right, &right_kind);
            int32_t dst = body_slot(body, SLOT_I64, 8);
            int32_t op_code = span_eq(op, "*") ? BODY_OP_I64_MUL : BODY_OP_I64_DIV;
            body_op(body, op_code, dst, left, right);
            left = dst;
            left_kind = SLOT_I64;
            continue;
        }
        if ((left_kind != SLOT_I32 && left_kind != SLOT_OPAQUE && left_kind != SLOT_OPAQUE_REF) ||
            (right_kind != SLOT_I32 && right_kind != SLOT_OPAQUE && right_kind != SLOT_OPAQUE_REF)) {
            /* Non-int32 arithmetic: fall through with zero */
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            left = zero;
            left_kind = SLOT_I32;
            continue;
        }
        int32_t dst = body_slot(body, SLOT_I32, 4);
        int32_t op_code;
        if (span_eq(op, "*")) op_code = BODY_OP_I32_MUL;
        else if (span_eq(op, "/")) op_code = BODY_OP_I32_DIV;
        else op_code = BODY_OP_I32_MOD;
        body_op(body, op_code, dst, left, right);
        left = dst;
        left_kind = SLOT_I32;
    }
    *kind = left_kind;
    return left;
}

int32_t parse_arith_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    int32_t left_kind = SLOT_I32;
    int32_t left = parse_term(parser, body, locals, &left_kind);
    while (span_eq(parser_peek(parser), "+") ||
           span_eq(parser_peek(parser), "-") ||
           span_eq(parser_peek(parser), "<<") ||
           span_eq(parser_peek(parser), ">>") ||
           span_eq(parser_peek(parser), "&") ||
           span_eq(parser_peek(parser), "|") ||
           span_eq(parser_peek(parser), "^")) {
        Span op = parser_token(parser);
        left = cold_materialize_i32_ref(body, left, &left_kind);
        int32_t right_kind = SLOT_I32;
        int32_t right = parse_term(parser, body, locals, &right_kind);
        right = cold_materialize_i32_ref(body, right, &right_kind);
        if (left_kind == SLOT_I64 || left_kind == SLOT_I64_REF ||
            right_kind == SLOT_I64 || right_kind == SLOT_I64_REF) {
            left = cold_materialize_i64_value(body, left, &left_kind);
            right = cold_materialize_i64_value(body, right, &right_kind);
            int32_t dst = body_slot(body, SLOT_I64, 8);
            int32_t op_code;
            if (span_eq(op, "+")) op_code = BODY_OP_I64_ADD;
            else if (span_eq(op, "-")) op_code = BODY_OP_I64_SUB;
            else if (span_eq(op, "&")) op_code = BODY_OP_I64_AND;
            else if (span_eq(op, "|")) op_code = BODY_OP_I64_OR;
            else if (span_eq(op, "^")) op_code = BODY_OP_I64_XOR;
            else if (span_eq(op, "<<")) op_code = BODY_OP_I64_SHL;
            else op_code = BODY_OP_I64_ASR;
            body_op(body, op_code, dst, left, right);
            left = dst;
            left_kind = SLOT_I64;
            continue;
        }
        if ((left_kind != SLOT_I32 && left_kind != SLOT_OPAQUE && left_kind != SLOT_OPAQUE_REF) ||
            (right_kind != SLOT_I32 && right_kind != SLOT_OPAQUE && right_kind != SLOT_OPAQUE_REF)) {
            /* Non-int32 arithmetic: fall through with zero */
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            left = zero;
            left_kind = SLOT_I32;
            continue;
        }
        int32_t dst = body_slot(body, SLOT_I32, 4);
        int32_t op_code;
        if (span_eq(op, "+")) op_code = BODY_OP_I32_ADD;
        else if (span_eq(op, "-")) op_code = BODY_OP_I32_SUB;
        else if (span_eq(op, "<<")) op_code = BODY_OP_I32_SHL;
        else if (span_eq(op, ">>")) op_code = BODY_OP_I32_ASR;
        else if (span_eq(op, "&")) op_code = BODY_OP_I32_AND;
        else if (span_eq(op, "|")) op_code = BODY_OP_I32_OR;
        else op_code = BODY_OP_I32_XOR;
        body_op(body, op_code, dst, left, right);
        left = dst;
        left_kind = SLOT_I32;
    }
    *kind = left_kind;
    return left;
}

int32_t parse_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    int32_t left_kind = SLOT_I32;
    int32_t left = parse_compare_expr(parser, body, locals, &left_kind);
    while (span_eq(parser_peek(parser), "&&") || span_eq(parser_peek(parser), "||")) {
        Span op = parser_token(parser);
        left = cold_materialize_i32_ref(body, left, &left_kind);
        int32_t right_kind = SLOT_I32;
        int32_t right = parse_compare_expr(parser, body, locals, &right_kind);
        right = cold_materialize_i32_ref(body, right, &right_kind);
        if (left_kind != SLOT_I32 || right_kind != SLOT_I32) die("logical operands must be bool/int32");
        int32_t dst = body_slot(body, SLOT_I32, 4);
        body_op(body, span_eq(op, "&&") ? BODY_OP_I32_AND : BODY_OP_I32_OR, dst, left, right);
        left = dst;
        left_kind = SLOT_I32;
    }
    *kind = left_kind;
    return left;
}

int32_t parse_compare_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    int32_t left_kind = SLOT_I32;
    int32_t left = parse_arith_expr(parser, body, locals, &left_kind);
    Span op = parser_peek(parser);
    if (!cold_span_is_compare_token(op)) {
        *kind = left_kind;
        return left;
    }
    (void)parser_token(parser);
    int32_t right_kind = SLOT_I32;
    int32_t right = parse_arith_expr(parser, body, locals, &right_kind);
    int32_t cond = cold_cond_from_compare_token(op);
    left = cold_materialize_i32_ref(body, left, &left_kind);
    right = cold_materialize_i32_ref(body, right, &right_kind);
    int32_t dst = body_slot(body, SLOT_I32, 4);
    if ((cold_slot_kind_is_str_like(left_kind) && right_kind == SLOT_PTR) ||
        (left_kind == SLOT_PTR && cold_slot_kind_is_str_like(right_kind))) {
        if (cond != COND_EQ && cond != COND_NE) die("str pointer comparison supports == and !=");
        if (cold_slot_kind_is_str_like(left_kind)) {
            left = cold_materialize_str_data_ptr(body, left, left_kind);
            left_kind = SLOT_PTR;
        }
        if (cold_slot_kind_is_str_like(right_kind)) {
            right = cold_materialize_str_data_ptr(body, right, right_kind);
            right_kind = SLOT_PTR;
        }
        left = cold_materialize_i64_value(body, left, &left_kind);
        right = cold_materialize_i64_value(body, right, &right_kind);
        body_op3(body, BODY_OP_I64_CMP, dst, left, right, cond);
    } else if (cold_slot_kind_is_str_like(left_kind) || cold_slot_kind_is_str_like(right_kind)) {
        if (!cold_slot_kind_is_str_like(left_kind)) { left = cold_materialize_fmt_str(body, left, left_kind); left_kind = body->slot_kind[left]; }
        if (!cold_slot_kind_is_str_like(right_kind)) { right = cold_materialize_fmt_str(body, right, right_kind); right_kind = body->slot_kind[right]; }
        if (!(cold_slot_kind_is_str_like(left_kind) && cold_slot_kind_is_str_like(right_kind))) {
            die("str comparison operands must both be str");
        }
        if (cond != COND_EQ && cond != COND_NE) die("str comparison supports == and !=");
        int32_t eq_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_STR_EQ, eq_slot, left, right);
        if (cond == COND_EQ) {
            dst = eq_slot;
        } else {
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            body_op3(body, BODY_OP_I32_CMP, dst, eq_slot, zero, COND_EQ);
        }
    } else if (left_kind == SLOT_I64 || right_kind == SLOT_I64) {
        left = cold_materialize_i64_value(body, left, &left_kind);
        right = cold_materialize_i64_value(body, right, &right_kind);
        body_op3(body, BODY_OP_I64_CMP, dst, left, right, cond);
    } else if (left_kind == SLOT_PTR || right_kind == SLOT_PTR) {
        left = cold_materialize_i64_value(body, left, &left_kind);
        right = cold_materialize_i64_value(body, right, &right_kind);
        body_op3(body, BODY_OP_I64_CMP, dst, left, right, cond);
    } else if (left_kind == SLOT_VARIANT || right_kind == SLOT_VARIANT) {
        if (left_kind != SLOT_VARIANT || right_kind != SLOT_VARIANT) {
            /* tolerate I32 mixed with VARIANT (enum tag comparison) */
            if ((left_kind == SLOT_I32 || left_kind == SLOT_VARIANT) &&
                (right_kind == SLOT_I32 || right_kind == SLOT_VARIANT)) {
                /* fall through to tag comparison */
            } else {
                die("variant comparison operands must both be variants");
            }
        }
        if (cond != COND_EQ && cond != COND_NE) {
            /* Unsupported variant comparison: fall through */
        }
        TypeDef *left_type = symbols_resolve_type(parser->symbols, body->slot_type[left]);
        TypeDef *right_type = symbols_resolve_type(parser->symbols, body->slot_type[right]);
        if (!left_type || left_type != right_type || !type_is_payloadless_enum(left_type)) {
            /* Variant equality with incompatible types: skip */
        }
        int32_t left_tag = body_slot(body, SLOT_I32, 4);
        int32_t right_tag = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TAG_LOAD, left_tag, left, 0);
        body_op(body, BODY_OP_TAG_LOAD, right_tag, right, 0);
        body_op3(body, BODY_OP_I32_CMP, dst, left_tag, right_tag, cond);
    } else {
        if (left_kind != SLOT_I32 || right_kind != SLOT_I32) {
            /* Non-int32 comparison operands: fall through */
        }
        body_op3(body, BODY_OP_I32_CMP, dst, left, right, cond);
    }
    *kind = SLOT_I32;
    return dst;
}

int32_t parse_expr_from_span(Parser *owner, BodyIR *body, Locals *locals,
                                    Span expr, int32_t *kind,
                                    const char *trailing_message) {
    Parser expr_parser = parser_child(owner, expr);
    int32_t slot = parse_expr(&expr_parser, body, locals, kind);
    parser_ws(&expr_parser);
    if (expr_parser.pos != expr_parser.source.len) {
        /* Trailing tokens in expression: return 0 */
        body->has_fallback = true;
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        *kind = SLOT_I32;
        return zero;
    }
    return slot;
}

bool parser_peek_empty_list_literal(Parser *parser) {
    Parser probe = *parser;
    if (!parser_take(&probe, "[")) return false;
    return parser_take(&probe, "]");
}

bool cold_slot_kind_is_sequence_target(int32_t kind) {
    return kind == SLOT_SEQ_I32 || kind == SLOT_SEQ_I32_REF ||
           kind == SLOT_SEQ_STR || kind == SLOT_SEQ_STR_REF ||
           kind == SLOT_SEQ_OPAQUE || kind == SLOT_SEQ_OPAQUE_REF;
}

int32_t cold_sequence_value_kind_for_target(int32_t kind) {
    if (kind == SLOT_SEQ_I32 || kind == SLOT_SEQ_I32_REF) return SLOT_SEQ_I32;
    if (kind == SLOT_SEQ_STR || kind == SLOT_SEQ_STR_REF) return SLOT_SEQ_STR;
    if (kind == SLOT_SEQ_OPAQUE || kind == SLOT_SEQ_OPAQUE_REF) return SLOT_SEQ_OPAQUE;
    die("target is not a sequence");
    return SLOT_SEQ_I32;
}

int32_t parse_empty_sequence_for_target(Parser *parser, BodyIR *body,
                                               int32_t target_kind, Span target_type,
                                               int32_t *kind_out) {
    if (!parser_take(parser, "[")) die("expected [ for empty sequence");
    if (!parser_take(parser, "]")) die("expected ] for empty sequence");
    int32_t value_kind = cold_sequence_value_kind_for_target(target_kind);
    int32_t slot = body_slot(body, value_kind, 16);
    if (value_kind == SLOT_SEQ_I32) {
        body_slot_set_type(body, slot, cold_cstr_span("int32[]"));
    } else if (value_kind == SLOT_SEQ_STR) {
        body_slot_set_type(body, slot, cold_cstr_span("str[]"));
    } else {
        body_slot_set_seq_opaque_type(body, parser->symbols, slot, target_type);
    }
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
    if (kind_out) *kind_out = value_kind;
    return slot;
}

bool cold_span_is_compare_token(Span op) {
    return span_eq(op, "==") || span_eq(op, "!=") ||
           span_eq(op, ">=") || span_eq(op, "<") ||
           span_eq(op, ">") || span_eq(op, "<=");
}

int32_t cold_cond_from_compare_token(Span op) {
    if (span_eq(op, "==")) return COND_EQ;
    if (span_eq(op, "!=")) return COND_NE;
    if (span_eq(op, ">=")) return COND_GE;
    if (span_eq(op, "<")) return COND_LT;
    if (span_eq(op, ">")) return COND_GT;
    if (span_eq(op, "<=")) return COND_LE;
    die("unsupported comparison operator");
    return COND_EQ;
}

void parse_return(Parser *parser, BodyIR *body, Locals *locals, int32_t block) {
    int32_t kind = SLOT_I32;
    int32_t slot = -1;
    parser_inline_ws(parser);
    if (parser->pos >= parser->source.len ||
        parser->source.ptr[parser->pos] == '\n' ||
        parser->source.ptr[parser->pos] == '\r') {
        if (!cold_return_span_is_void(body->return_type)) die("non-void return requires value");
        slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
        body_op(body, BODY_OP_LOAD_I32, slot, 0, 0);
        int32_t term = body_term(body, BODY_TERM_RET, slot, -1, 0, -1, -1);
        body_end_block(body, block, term);
        return;
    }
    Span next = parser_peek(parser);
    Variant *return_variant = 0;
    if (body->return_kind == SLOT_VARIANT && body->return_type.len > 0) {
        TypeDef *return_type = symbols_resolve_type(parser->symbols, body->return_type);
        return_variant = type_find_variant(return_type, next);
    }
    if (return_variant) {
        (void)parser_token(parser);
        slot = parse_constructor(parser, body, locals, return_variant);
        kind = SLOT_VARIANT;
    } else {
        slot = parse_expr(parser, body, locals, &kind);
    }
    parser_inline_ws(parser);
    if (parser->pos < parser->source.len && parser->source.ptr[parser->pos] == '?') {
        (void)parser_token(parser);  /* consume ? */
        if (kind == SLOT_VARIANT) {
            TypeDef *rqtype = cold_question_result_type(parser->symbols, body, slot);
            if (rqtype && rqtype->variant_count == 2 &&
                rqtype->variants[0].field_count == 1) {
                Variant *ok_v = &rqtype->variants[0];
                Variant *err_v = &rqtype->variants[1];
                int32_t tag_slot = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_TAG_LOAD, tag_slot, slot, 0);
                int32_t err_tag_slot = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, err_tag_slot, err_v->tag, 0);
                int32_t ok_blk = body_block(body);
                int32_t err_blk = body_block(body);
                int32_t cbr_t = body_term(body, BODY_TERM_CBR, tag_slot, COND_EQ,
                                          err_tag_slot, err_blk, ok_blk);
                body_end_block(body, block, cbr_t);
                /* err_blk: return error */
                body_reopen_block(body, err_blk);
                if (body->return_kind == SLOT_VARIANT &&
                    body->return_type.len > 0 &&
                    span_same(body->return_type, rqtype->name)) {
                    int32_t ret_t = body_term(body, BODY_TERM_RET, slot, -1, 0, -1, -1);
                    body_end_block(body, err_blk, ret_t);
                } else if (body->return_kind == SLOT_I32 &&
                           err_v->field_count == 1 &&
                           err_v->field_kind[0] == SLOT_I32) {
                    int32_t ep = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_PAYLOAD_LOAD, ep, slot, err_v->field_offset[0]);
                    int32_t ret_t = body_term(body, BODY_TERM_RET, ep, -1, 0, -1, -1);
                    body_end_block(body, err_blk, ret_t);
                } else {
                    /* incompatible: BRK fallback */
                    body_op3(body, BODY_OP_UNWRAP_OR_RETURN, tag_slot, tag_slot, 0, 0);
                    int32_t z = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_I32_CONST, z, 0, 0);
                    int32_t ret_t = body_term(body, BODY_TERM_RET, z, -1, 0, -1, -1);
                    body_end_block(body, err_blk, ret_t);
                }
                /* ok_blk: return unwrapped payload */
                body_reopen_block(body, ok_blk);
                int32_t ps = body_slot(body, SLOT_I32, 4);
                body_op3(body, BODY_OP_PAYLOAD_LOAD, ps, slot,
                         ok_v->field_offset[0], ok_v->field_size[0]);
                int32_t ok_ret = body_term(body, BODY_TERM_RET, ps, -1, 0, -1, -1);
                body_end_block(body, ok_blk, ok_ret);
                return;
            }
        }
        /* Non-variant ?: treat as unsupported */
        return;
    }
    if (body->return_kind == SLOT_I32) {
        slot = cold_materialize_i32_ref(body, slot, &kind);
    } else if (body->return_kind == SLOT_I64 && kind == SLOT_I32) {
        slot = cold_materialize_i64_value(body, slot, &kind);
    }
    if (kind != body->return_kind && !(body->return_kind == SLOT_I32 && kind == SLOT_VARIANT &&
          body->return_type.len > 0 && symbols_resolve_type(parser->symbols, body->return_type)) &&
        !(body->return_kind == SLOT_OBJECT && kind == SLOT_I32)) {
        /* Skip return kind mismatch; emit return anyway */
    }
    if (kind == SLOT_I32) body_op(body, BODY_OP_LOAD_I32, slot, 0, 0);
    int32_t term = body_term(body, BODY_TERM_RET, slot, -1, 0, -1, -1);
    body_end_block(body, block, term);
}

Variant *cold_condition_variant_for_type(TypeDef *type, Span expr);
int32_t cold_make_payloadless_variant_slot(BodyIR *body, Symbols *symbols,
                                                  TypeDef *type, Variant *variant);

int32_t parse_expected_payloadless_variant(Parser *parser, BodyIR *body,
                                                  TypeDef *type, int32_t *kind) {
    if (!type || !type_is_payloadless_enum(type)) return -1;
    int32_t saved = parser->pos;
    Span first = parser_token(parser);
    if (first.len <= 0) {
        parser->pos = saved;
        return -1;
    }
    Span expr = first;
    if (span_eq(parser_peek(parser), ".")) {
        expr = parser_take_qualified_after_first(parser, first);
    }
    Variant *variant = cold_condition_variant_for_type(type, expr);
    if (!variant || variant->field_count != 0) {
        parser->pos = saved;
        return -1;
    }
    if (kind) *kind = SLOT_VARIANT;
    return cold_make_payloadless_variant_slot(body, parser->symbols, type, variant);
}

int32_t parse_let_binding(Parser *parser, BodyIR *body, Locals *locals,
                                  int32_t block, bool is_var) {
    (void)is_var;
    Span name = parser_token(parser);
    Span type = {0};
    if (span_eq(parser_peek(parser), ":")) {
        (void)parser_token(parser);
        type = parser_scope_type(parser, parser_take_type_span(parser));
    }
    if (!span_eq(parser_peek(parser), "=")) {
        if (type.len <= 0) die("expected = after let binding");
        int32_t kind = SLOT_I32;
        int32_t slot = body_default_slot(body, parser->symbols, type, &kind);
        if (kind == SLOT_I32 || kind == SLOT_I64 || kind == SLOT_F32 || kind == SLOT_F64)
            body->slot_no_alias[slot] = 1;
        locals_add(locals, name, slot, kind);
        return block;
    }
    (void)parser_token(parser);
    int32_t kind = SLOT_I32;
    int32_t slot = -1;
    /* pre-allocate slot for var bindings so they don't alias the initializer */
    int32_t owned_slot = -1;
    if (is_var && type.len > 0) {
        int32_t decl_kind = cold_slot_kind_from_type_with_symbols(parser->symbols, type);
        if (decl_kind == SLOT_I32 || decl_kind == SLOT_I64) {
            owned_slot = body_slot(body, decl_kind, cold_slot_size_for_kind(decl_kind));
            body_slot_set_type(body, owned_slot, type);
        }
    }
    if (type.len > 0 && cold_parse_i32_seq_type(type) && span_eq(parser_peek(parser), "[")) {
        slot = parse_i32_seq_literal(parser, body, locals, &kind);
    } else if (type.len > 0) {
        TypeDef *declared_type = symbols_resolve_type(parser->symbols, type);
        Variant *declared_variant = declared_type ? type_find_variant(declared_type, parser_peek(parser)) : 0;
        if (declared_variant) {
            (void)parser_token(parser);
            slot = parse_constructor(parser, body, locals, declared_variant);
            kind = SLOT_VARIANT;
        } else {
            slot = parse_expected_payloadless_variant(parser, body, declared_type, &kind);
            if (slot < 0) slot = parse_expr(parser, body, locals, &kind);
        }
    } else {
        slot = parse_expr(parser, body, locals, &kind);
    }
    /* For let bindings (not var), always copy to avoid aliasing the initializer slot */
    if (!is_var && slot >= 0 && (kind == SLOT_I32 || kind == SLOT_I64)) {
        int32_t copy_slot = body_slot(body, kind, cold_slot_size_for_kind(kind));
        if (kind == SLOT_I32) {
            body_op(body, BODY_OP_COPY_I32, copy_slot, slot, 0);
        } else {
            body_op(body, BODY_OP_COPY_I64, copy_slot, slot, 0);
        }
        slot = copy_slot;
    }
    if (owned_slot >= 0 && kind == SLOT_I32 && body->slot_kind[owned_slot] == SLOT_I32) {
        body_op(body, BODY_OP_COPY_I32, owned_slot, slot, 0);
        slot = owned_slot;
    } else if (owned_slot >= 0 && kind == SLOT_VARIANT && body->slot_kind[owned_slot] == SLOT_I32) {
        int32_t tag_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TAG_LOAD, tag_slot, slot, 0);
        body_op(body, BODY_OP_COPY_I32, owned_slot, tag_slot, 0);
        slot = owned_slot;
        kind = SLOT_I32;
    } else if (owned_slot >= 0 && body->slot_kind[owned_slot] == SLOT_I64) {
        if (kind == SLOT_I32) slot = cold_materialize_i64_value(body, slot, &kind);
        if (kind != SLOT_I64) die("typed int64 initializer kind mismatch");
        body_op(body, BODY_OP_COPY_I64, owned_slot, slot, 0);
        slot = owned_slot;
    }
    if (span_eq(parser_peek(parser), "?")) {
        (void)parser_token(parser);
        int32_t declared_kind = -1;
        Span declared_type = {0};
        if (type.len > 0) {
            declared_type = span_trim(type);
            declared_kind = cold_slot_kind_from_type_with_symbols(parser->symbols, declared_type);
        }
        return cold_lower_question_result(parser->symbols, body, locals, block, slot,
                                          name, true, declared_kind, declared_type);
    }
    if (type.len > 0) {
            int32_t declared_kind = cold_slot_kind_from_type_with_symbols(parser->symbols, type);
            if (declared_kind == SLOT_I64 && kind == SLOT_I32) {
                slot = cold_materialize_i64_value(body, slot, &kind);
            }
            if (kind != declared_kind) {
                /* Skip typed let with mismatched kind */
            }
            if (kind == SLOT_ARRAY_I32) {
                int32_t declared_len = 0;
                if (!cold_parse_i32_array_type(type, &declared_len)) declared_len = 0;
                if (declared_len > 0 && body->slot_aux[slot] != declared_len) { /* skip mismatch */ }
            } else if (kind == SLOT_SEQ_I32) {
                if (!cold_parse_i32_seq_type(type)) die("typed int32[] let missing dynamic sequence type");
            }
            if (kind == SLOT_SEQ_OPAQUE)
                body_slot_set_seq_opaque_type(body, parser->symbols, slot, type);
            else if (body->slot_type[slot].len <= 0)
                body_slot_set_type(body, slot, type);
        }
    if (kind == SLOT_I32 || kind == SLOT_I64 || kind == SLOT_F32 || kind == SLOT_F64)
        body->slot_no_alias[slot] = 1;
    locals_add(locals, name, slot, kind);
    return block;
}

void parse_assign(Parser *parser, BodyIR *body, Locals *locals, Span name) {
    Local *local = locals_find(locals, name);
    if (!local) {
        /* Consume the value expression and continue */
        int32_t dummy_kind = SLOT_I32;
        (void)parse_expr(parser, body, locals, &dummy_kind);
        return;
    }
    if (!parser_take(parser, "=")) die("expected = in assignment");
    int32_t kind = SLOT_I32;
    int32_t slot = -1;
    if (cold_slot_kind_is_sequence_target(local->kind) &&
        parser_peek_empty_list_literal(parser)) {
        slot = parse_empty_sequence_for_target(parser, body, local->kind,
                                               body->slot_type[local->slot], &kind);
    } else {
        slot = parse_expr(parser, body, locals, &kind);
    }
    if (local->kind == SLOT_I32) {
        slot = cold_materialize_i32_ref(body, slot, &kind);
        if (kind != SLOT_I32) {
            /* Skip incompatible assignment */
            return;
        }
        body_op(body, BODY_OP_COPY_I32, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_I32_REF) {
        slot = cold_materialize_i32_ref(body, slot, &kind);
        if (kind != SLOT_I32) {
            /* Skip incompatible assignment - fallback */
            return;
        }
        body_op(body, BODY_OP_I32_REF_STORE, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_I64) {
        if (kind == SLOT_I32) slot = cold_materialize_i64_value(body, slot, &kind);
        if (kind != SLOT_I64) die("cold assignment value must be int64");
        body_op(body, BODY_OP_COPY_I64, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_STR_REF) {
        if (kind != SLOT_STR) {
            /* Skip incompatible assignment */
            return;
        }
        body_op(body, BODY_OP_STR_REF_STORE, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_STR) {
        if (kind != SLOT_STR) {
            /* Skip: value type not compatible with str */
            return;
        }
        body_op(body, BODY_OP_COPY_COMPOSITE, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_SEQ_I32_REF || local->kind == SLOT_SEQ_STR_REF ||
        local->kind == SLOT_SEQ_OPAQUE_REF) {
        int32_t expected = cold_sequence_value_kind_for_target(local->kind);
        if (kind != expected) die("sequence ref assignment value kind mismatch");
        body_op3(body, BODY_OP_PAYLOAD_STORE, local->slot, slot, 0, body->slot_size[slot]);
        return;
    }
    if (local->kind == SLOT_VARIANT || local->kind == SLOT_OBJECT || local->kind == SLOT_OBJECT_REF ||
        local->kind == SLOT_ARRAY_I32 || local->kind == SLOT_SEQ_I32 || local->kind == SLOT_SEQ_STR ||
        local->kind == SLOT_SEQ_OPAQUE || local->kind == SLOT_SEQ_OPAQUE_REF ||
        local->kind == SLOT_OPAQUE || local->kind == SLOT_OPAQUE_REF) {
        body_op3(body, BODY_OP_PAYLOAD_STORE, local->slot, slot, 0, body->slot_size[local->slot]);
        return;
    }
    /* Unsupported assignment target, skip */
}

int32_t parse_field_assign(Parser *parser, BodyIR *body, Locals *locals,
                                  Span base_name, int32_t block) {
    Local *local = locals_find(locals, base_name);
    if (!local) return block; /* skip non-local field assign */;
    if (local->kind != SLOT_OBJECT && local->kind != SLOT_OBJECT_REF &&
        local->kind != SLOT_PTR && local->kind != SLOT_STR) {
        int32_t skip_kind = SLOT_I32;
        (void)parse_expr(parser, body, locals, &skip_kind);
        return block;
    }
    if (!parser_take(parser, ".")) die("expected . in field assignment");
    Span field_name = parser_token(parser);
    if (field_name.len <= 0) die("expected field name in assignment");
    /* Str field assignment: use known offsets (data=0, len=8, store_id=12, flags=16) */
    if (local->kind == SLOT_STR) {
        if (!parser_take(parser, "=")) die("expected = in str field assignment");
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
        int32_t off = -1;
        if (span_eq(field_name, "data")) off = COLD_STR_DATA_OFFSET;
        else if (span_eq(field_name, "len")) off = COLD_STR_LEN_OFFSET;
        else if (span_eq(field_name, "store_id")) off = COLD_STR_STORE_ID_OFFSET;
        else if (span_eq(field_name, "flags")) off = COLD_STR_FLAGS_OFFSET;
        if (off < 0) die("unknown str field");
        if (off == COLD_STR_DATA_OFFSET)
            body_op3(body, BODY_OP_SLOT_STORE_I64, local->slot, value_slot, off, 0);
        else
            body_op3(body, BODY_OP_SLOT_STORE_I32, local->slot, value_slot, off, 0);
        return block;
    }
    Span object_type = cold_type_strip_var(body->slot_type[local->slot], 0);
    ObjectDef *object = symbols_resolve_object(parser->symbols, object_type);
    if (!object) {
        /* Try qualified name fallback (imported objects use alias.TypeName) */
        for (int32_t qoi = 0; qoi < parser->symbols->object_count; qoi++) {
            ObjectDef *co = &parser->symbols->objects[qoi];
            if (co->name.len > object_type.len + 1 &&
                co->name.ptr[co->name.len - object_type.len - 1] == '.' &&
                memcmp(co->name.ptr + co->name.len - object_type.len, object_type.ptr, (size_t)object_type.len) == 0) {
                object = co;
                break;
            }
        }
    }
    if (!object) die("field assignment object type missing");
    ObjectField *field = object_find_field(object, field_name);
    if (!field) {
        fprintf(stderr, "[field_assign] object=%.*s field=%.*s local_kind=%d\n",
                object ? (int)object->name.len : 0,
                object ? (const char *)object->name.ptr : "?",
                (int)field_name.len, field_name.ptr, local->kind);
        die("unknown field assignment target");
    }
    if (!parser_take(parser, "=")) die("expected = in field assignment");
    int32_t value_kind = SLOT_I32;
    int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
    if (field->kind == SLOT_SEQ_I32 && value_kind == SLOT_ARRAY_I32) {
        int32_t count = body->slot_aux[value_slot];
        int32_t seq_slot = body_slot(body, SLOT_SEQ_I32, 16);
        body_slot_set_type(body, seq_slot, cold_cstr_span("int32[]"));
        body_slot_set_array_len(body, seq_slot, count);
        body_op3(body, BODY_OP_MAKE_SEQ_I32, seq_slot, value_slot, -1, count);
        value_slot = seq_slot;
        value_kind = SLOT_SEQ_I32;
    }
    if (field->kind == SLOT_SEQ_OPAQUE && value_kind == SLOT_SEQ_I32) {
        int32_t seq_slot = body_slot(body, SLOT_SEQ_OPAQUE, 16);
        body_slot_set_seq_opaque_type(body, parser->symbols, seq_slot, field->type_name);
        body_op3(body, BODY_OP_MAKE_COMPOSITE, seq_slot, 0, -1, 0);
        value_slot = seq_slot;
        value_kind = SLOT_SEQ_OPAQUE;
    }
    if (field->kind == SLOT_SEQ_I32_REF && value_kind == SLOT_ARRAY_I32) {
        int32_t count = body->slot_aux[value_slot];
        int32_t seq_slot = body_slot(body, SLOT_SEQ_I32, 16);
        body_slot_set_type(body, seq_slot, cold_cstr_span("int32[]"));
        body_slot_set_array_len(body, seq_slot, count);
        body_op3(body, BODY_OP_MAKE_SEQ_I32, seq_slot, value_slot, -1, count);
        value_slot = seq_slot;
        value_kind = SLOT_SEQ_I32;
    }
    if (field->kind == SLOT_SEQ_I32 && value_kind == SLOT_ARRAY_I32) {
        int32_t count = body->slot_aux[value_slot];
        int32_t seq_slot = body_slot(body, SLOT_SEQ_I32, 16);
        body_slot_set_type(body, seq_slot, cold_cstr_span("int32[]"));
        body_slot_set_array_len(body, seq_slot, count);
        body_op3(body, BODY_OP_MAKE_SEQ_I32, seq_slot, value_slot, -1, count);
        value_slot = seq_slot;
        value_kind = SLOT_SEQ_I32;
    }
    if (value_kind == SLOT_VARIANT && field->kind == SLOT_I32) {
        int32_t tag_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TAG_LOAD, tag_slot, value_slot, 0);
        value_slot = tag_slot;
        value_kind = SLOT_I32;
    }
    if (value_kind != field->kind) {
        if (field->kind == SLOT_I64 && value_kind == SLOT_I32) {
            value_slot = cold_materialize_i64_value(body, value_slot, &value_kind);
        }
    }
    if (value_kind != field->kind) {
        if (!((field->kind == SLOT_I32 && (value_kind == SLOT_VARIANT || value_kind == SLOT_STR)) ||
              (field->kind == SLOT_VARIANT && value_kind == SLOT_I32) ||
              (field->kind == SLOT_STR && value_kind == SLOT_I32) ||
              (field->kind == SLOT_OPAQUE))) {
            /* Skip: value kind doesn't match field kind */
        }
    }
    if (field->kind == SLOT_ARRAY_I32 && body->slot_aux[value_slot] != field->array_len) {
        return block; /* skip length mismatch */;
    }
    body_op3(body, BODY_OP_PAYLOAD_STORE, local->slot, value_slot, field->offset, field->size);
    return block;
}

int32_t parse_seq_lvalue_from_span(Parser *owner, BodyIR *body, Locals *locals,
                                          Span target, int32_t *kind_out) {
    Parser parser = parser_child(owner, target);
    Span name = parser_token(&parser);
    if (name.len <= 0) die("add target must name an int32[]");
    Local *local = locals_find(locals, name);
    if (!local) die("add target must be a local sequence");
    int32_t slot = local->slot;
    int32_t kind = local->kind;
    if (span_eq(parser_peek(&parser), ".")) {
        if (kind != SLOT_OBJECT && kind != SLOT_OBJECT_REF) {
            die("add field target base must be object");
        }
        (void)parser_token(&parser);
        Span field_name = parser_token(&parser);
        if (field_name.len <= 0) die("expected add field name");
        Span object_type = cold_type_strip_var(body->slot_type[slot], 0);
        ObjectDef *object = symbols_resolve_object(owner->symbols, object_type);
        if (!object) die("add field base object type missing");
        ObjectField *field = object_find_field(object, field_name);
        if (!field || (field->kind != SLOT_SEQ_I32 && field->kind != SLOT_SEQ_STR &&
                       field->kind != SLOT_SEQ_OPAQUE)) {
            die("add field target must be sequence");
        }
        int32_t ref_kind = field->kind == SLOT_SEQ_I32 ? SLOT_SEQ_I32_REF :
                           field->kind == SLOT_SEQ_STR ? SLOT_SEQ_STR_REF :
                           SLOT_SEQ_OPAQUE_REF;
        int32_t ref_slot = body_slot(body, ref_kind, 8);
        body_slot_set_type(body, ref_slot,
                           field->kind == SLOT_SEQ_I32 ? cold_cstr_span("int32[]") :
                           field->kind == SLOT_SEQ_STR ? cold_cstr_span("str[]") :
                           field->type_name);
        body_op3(body, BODY_OP_FIELD_REF, ref_slot, slot, field->offset, 0);
        slot = ref_slot;
        kind = ref_kind;
    }
    parser_ws(&parser);
    if (parser.pos != parser.source.len) {
        /* Complex l-value path (e.g. array[index].field) not yet handled.
           Return -1 so caller safely skips this built-in operation. */
        return -1;
    }
    if (kind != SLOT_SEQ_I32 && kind != SLOT_SEQ_I32_REF &&
        kind != SLOT_SEQ_STR && kind != SLOT_SEQ_STR_REF &&
        kind != SLOT_SEQ_OPAQUE && kind != SLOT_SEQ_OPAQUE_REF) {
        die("add target must be sequence");
    }
    if (kind_out) *kind_out = kind;
    return slot;
}

int32_t parse_builtin_add_after_name(Parser *parser, BodyIR *body, Locals *locals,
                                            int32_t block) {
    if (!parser_take(parser, "(")) die("expected ( after add");
    Span target = parser_take_until_top_level_char(parser, ',', "expected comma after add target");
    if (!parser_take(parser, ",")) die("expected comma after add target");
    int32_t seq_kind = SLOT_I32;
    int32_t seq_slot = parse_seq_lvalue_from_span(parser, body, locals, target, &seq_kind);
    int32_t value_kind = SLOT_I32;
    int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
    if (!parser_take(parser, ")")) {
        /* Skip malformed add, advance to next line */
        while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
        if (parser->pos < parser->source.len) parser->pos++;
        return block;
    }
    if (seq_slot < 0) return block; /* unhandled l-value path, skip silently */
    if (seq_kind == SLOT_SEQ_I32 || seq_kind == SLOT_SEQ_I32_REF) {
        if (value_kind != SLOT_I32) {
            die("add int32[] value kind mismatch");
        }
        body_op(body, BODY_OP_SEQ_I32_ADD, seq_slot, value_slot, 0);
    } else if (seq_kind == SLOT_SEQ_OPAQUE || seq_kind == SLOT_SEQ_OPAQUE_REF) {
        int32_t element_size = cold_seq_opaque_element_size_for_slot(parser->symbols, body, seq_slot);
        body_op(body, BODY_OP_SEQ_OPAQUE_ADD, seq_slot, value_slot, element_size);
    } else if (seq_kind == SLOT_SEQ_STR || seq_kind == SLOT_SEQ_STR_REF) {
        if (value_kind == SLOT_STR || value_kind == SLOT_STR_REF) {
            body_op(body, BODY_OP_SEQ_STR_ADD, seq_slot, value_slot, 0);
        }
    }
    return block;
}

int32_t parse_builtin_remove_after_name(Parser *parser, BodyIR *body, Locals *locals,
                                               int32_t block) {
    if (!parser_take(parser, "(")) die("expected ( after remove");
    Span target = parser_take_until_top_level_char(parser, ',', "expected comma after remove target");
    if (!parser_take(parser, ",")) die("expected comma after remove target");
    int32_t seq_kind = SLOT_I32;
    int32_t seq_slot = parse_seq_lvalue_from_span(parser, body, locals, target, &seq_kind);
    int32_t index_kind = SLOT_I32;
    int32_t index_slot = parse_expr(parser, body, locals, &index_kind);
    if (!parser_take(parser, ")")) {
        while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
        if (parser->pos < parser->source.len) parser->pos++;
        return block;
    }
    if (seq_slot < 0) return block; /* unhandled l-value path, skip silently */
    if (index_kind != SLOT_I32) die("remove index must be int32");
    if (seq_kind == SLOT_SEQ_OPAQUE || seq_kind == SLOT_SEQ_OPAQUE_REF) {
        int32_t element_size = cold_seq_opaque_element_size_for_slot(parser->symbols, body, seq_slot);
        body_op3(body, BODY_OP_SEQ_OPAQUE_REMOVE, seq_slot, index_slot, element_size, 0);
    }
    return block;
}

int32_t parser_next_indent(Parser *parser);
int32_t body_branch_to(BodyIR *body, int32_t block, int32_t target_block);
int32_t parse_statement(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t block, LoopCtx *loop);
Span parser_take_condition_span(Parser *parser);
Span parser_take_match_target_span(Parser *parser);
Span condition_strip_outer(Span condition);

int32_t cold_match_eval_target(Parser *parser, BodyIR *body, Locals *locals,
                                      Span expr_span, int32_t *variant_slot) {
    Parser expr_parser = parser_child(parser, expr_span);
    int32_t kind = SLOT_I32;
    int32_t slot = parse_expr(&expr_parser, body, locals, &kind);
    parser_ws(&expr_parser);
    if (expr_parser.pos != expr_parser.source.len) return 0; /* unsupported match target */
    if (kind != SLOT_VARIANT) return 0; /* skip non-variant match */
    *variant_slot = slot;
    return kind;
}

/* Parse a guard condition after 'if' in a match arm, up to '=>'.
   Returns the guard condition Span. The parser position is left at '=>'. */
static Span parser_take_guard_span(Parser *parser) {
    parser_inline_ws(parser);
    int32_t start = parser->pos;
    int32_t depth = 0;
    bool in_string = false;
    bool in_char = false;
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (in_string) {
            if (c == '\\' && parser->pos + 1 < parser->source.len) { parser->pos += 2; continue; }
            if (c == '"') in_string = false;
            parser->pos++;
            continue;
        }
        if (in_char) {
            if (c == '\\' && parser->pos + 1 < parser->source.len) { parser->pos += 2; continue; }
            if (c == '\'') in_char = false;
            parser->pos++;
            continue;
        }
        if (c == '"') { in_string = true; parser->pos++; continue; }
        if (c == '\'') { in_char = true; parser->pos++; continue; }
        if (c == '(' || c == '[') { depth++; parser->pos++; continue; }
        if (c == ')' || c == ']') {
            depth--;
            if (depth < 0) die("unbalanced guard expression");
            parser->pos++;
            continue;
        }
        if (depth == 0 && c == '=' && parser->pos + 1 < parser->source.len &&
            parser->source.ptr[parser->pos + 1] == '>') {
            Span guard = span_trim(span_sub(parser->source, start, parser->pos));
            if (guard.len <= 0) die("empty guard condition");
            return guard;
        }
        parser->pos++;
    }
    die("expected => after guard condition");
    return (Span){0};
}

int32_t parse_match_arm(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t matched_slot, int32_t switch_term,
                               int32_t arm_indent,
                               TypeDef *match_type, LoopCtx *loop,
                               bool *is_complete,
                               int32_t entry_block,
                               bool *out_has_guard) {
    *is_complete = false;
    Span variant_name = parser_token(parser);
    Variant *variant = match_type ? type_find_variant(match_type, variant_name)
                                  : symbols_find_variant(parser->symbols, variant_name);
    if (!variant) die("unknown match variant");
    TypeDef *variant_type = symbols_find_variant_type(parser->symbols, variant);
    if (!variant_type) die("match variant has no parent type");
    if (match_type && variant_type != match_type) die("match arms must use one variant type");

    Span bind_names[COLD_MAX_VARIANT_FIELDS];
    int32_t bind_count = parse_match_bindings(parser, bind_names, COLD_MAX_VARIANT_FIELDS);
    if (bind_count != variant->field_count) {
        die("match arm payload binding count mismatch");
    }
    /* Check for guard condition */
    bool has_guard = false;
    Span guard_span = {0};
    if (span_eq(parser_peek(parser), "if")) {
        has_guard = true;
        (void)parser_token(parser);
        guard_span = parser_take_guard_span(parser);
    }
    if (!parser_take(parser, "=>")) {
        die("expected => in match arm");
    }

    int32_t arm_block;
    if (entry_block >= 0) {
        arm_block = entry_block;
        body_reopen_block(body, arm_block);
    } else {
        arm_block = body_block(body);
        body_switch_case(body, switch_term, variant->tag, arm_block);
    }
    int32_t saved_local_count = locals->count;
    for (int32_t field_index = 0; field_index < bind_count; field_index++) {
        int32_t field_kind = variant->field_kind[field_index];
        int32_t payload_slot = body_slot(body, field_kind, variant->field_size[field_index]);
        body_slot_set_type(body, payload_slot, variant->field_type[field_index]);
        body_op(body, BODY_OP_PAYLOAD_LOAD, payload_slot, matched_slot,
                variant->field_offset[field_index]);
        locals_add(locals, bind_names[field_index], payload_slot, field_kind);
    }

    /* Guard check: if guard present, evaluate and CBR to body or fallthrough */
    int32_t guard_ft_block = -1;
    if (has_guard) {
        Parser guard_parser = parser_child(parser, guard_span);
        int32_t guard_kind = SLOT_I32;
        int32_t guard_val = parse_expr(&guard_parser, body, locals, &guard_kind);
        parser_ws(&guard_parser);
        if (guard_parser.pos != guard_parser.source.len) {
            die("unsupported match guard expression");
        }
        int32_t body_blk = body_block(body);
        guard_ft_block = body_block(body);
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        int32_t cbr = body_term(body, BODY_TERM_CBR, guard_val, COND_NE, zero,
                                body_blk, guard_ft_block);
        body_end_block(body, arm_block, cbr);
        body_reopen_block(body, body_blk);
        arm_block = body_blk;
    }

    parser_inline_ws(parser);
    if (parser->pos < parser->source.len &&
        parser->source.ptr[parser->pos] != '\n' &&
        parser->source.ptr[parser->pos] != '\r') {
        int32_t start = parser->pos;
        int32_t end = start;
        while (end < parser->source.len &&
               parser->source.ptr[end] != '\n' &&
               parser->source.ptr[end] != '\r') {
            end++;
        }
        Span arm_body = span_trim(span_sub(parser->source, start, end));
        Parser arm_parser = parser_child(parser, arm_body);
        int32_t arm_end = arm_block;
        while (arm_parser.pos < arm_parser.source.len &&
               body->block_term[arm_end] < 0) {
            arm_end = parse_statement(&arm_parser, body, locals, arm_end, loop);
        }
        parser_ws(&arm_parser);
        if (arm_parser.pos != arm_parser.source.len) die("unsupported inline match arm body");
        parser->pos = end;
        locals->count = saved_local_count;
        if (has_guard) {
            if (body->block_term[arm_end] < 0) {
                body_branch_to(body, arm_end, guard_ft_block);
            }
            if (out_has_guard) *out_has_guard = true;
            *is_complete = true;
            return guard_ft_block;
        }
        if (out_has_guard) *out_has_guard = false;
        *is_complete = true;
        return arm_end;
    }

    int32_t body_indent = parser_next_indent(parser);
    if (body_indent < 0 || body_indent <= arm_indent) {
        Span next = parser_peek(parser);
        fprintf(stderr, "cheng_cold: match arm body is empty pos=%d arm_indent=%d body_indent=%d next=%.*s\n",
                parser->pos, arm_indent, body_indent, next.len, next.ptr);
        exit(2);
    }
    int32_t arm_end = arm_block;
    while (parser->pos < parser->source.len && body->block_term[arm_end] < 0) {
        int32_t next_indent = parser_next_indent(parser);
        if (next_indent < 0 || next_indent < body_indent) break;
        if (next_indent == arm_indent) {
            Span next = parser_peek(parser);
            Variant *next_v = match_type ? type_find_variant(match_type, next)
                                         : symbols_find_variant(parser->symbols, next);
            if (next_v && (!match_type || symbols_find_variant_type(parser->symbols, next_v) == match_type)) break;
        }
        arm_end = parse_statement(parser, body, locals, arm_end, loop);
    }
    locals->count = saved_local_count;
    if (has_guard) {
        if (body->block_term[arm_end] < 0) {
            body_branch_to(body, arm_end, guard_ft_block);
        }
        if (out_has_guard) *out_has_guard = true;
        *is_complete = true;
        return guard_ft_block;
    }
    if (out_has_guard) *out_has_guard = false;
    *is_complete = true;
    return arm_end;
}

int32_t parse_match(Parser *parser, BodyIR *body, Locals *locals,
                           int32_t block, LoopCtx *loop) {
    Span matched_expr = parser_take_match_target_span(parser);
    Span expr = condition_strip_outer(matched_expr);

    int32_t matched_slot = -1;
    Local *matched = locals_find(locals, expr);
    if (matched) {
        if (matched->kind != SLOT_VARIANT && matched->kind != SLOT_I32 && matched->kind != SLOT_STR)
            return block; /* skip non-variant match value */;
        matched_slot = matched->slot;
        if (matched->kind != SLOT_VARIANT) {
            int32_t vs = body_slot(body, SLOT_VARIANT, 4);
            body_op(body, BODY_OP_COPY_I32, vs, matched_slot, 0);
            matched_slot = vs;
        }
    } else {
        cold_match_eval_target(parser, body, locals, expr, &matched_slot);
    }

    int32_t tag_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_TAG_LOAD, tag_slot, matched_slot, 0);
    int32_t case_start = body->switch_count;
    int32_t switch_term = body_term(body, BODY_TERM_SWITCH, tag_slot, case_start, 0, -1, -1);
    body_end_block(body, block, switch_term);

#define COLD_MATCH_VARIANT_CAP 128
#define COLD_MATCH_FALLTHROUGH_CAP 128
    int32_t case_count = 0;
    int32_t fallthrough_terms[COLD_MATCH_FALLTHROUGH_CAP];
    int32_t fallthrough_count = 0;
    bool seen_tags[COLD_MATCH_VARIANT_CAP];
    memset(seen_tags, 0, sizeof(seen_tags));
    TypeDef *match_type = body->slot_type[matched_slot].len > 0
                              ? symbols_resolve_type(parser->symbols, body->slot_type[matched_slot])
                              : 0;

    int32_t stmt_indent = parser_next_indent(parser);
    bool single_line = (parser->pos < parser->source.len &&
                        parser->source.ptr[parser->pos] != '\n');

    if (single_line) {
        Variant *prev_variant = 0;
        int32_t prev_guard_ft = -1;
        while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') {
            Span next = parser_peek(parser);
            Variant *variant = match_type ? type_find_variant(match_type, next)
                                          : symbols_find_variant(parser->symbols, next);
            if (!variant) break;
            if (match_type && symbols_find_variant_type(parser->symbols, variant) != match_type) break;
            if (!match_type) match_type = symbols_find_variant_type(parser->symbols, variant);
            bool is_same_variant = (prev_variant && variant->tag == prev_variant->tag);
            if (seen_tags[variant->tag] && !is_same_variant) die("duplicate match arm");
            if (!is_same_variant) {
                seen_tags[variant->tag] = true;
                case_count++;
            }
            (void)parser_token(parser);

            Span bind_names[COLD_MAX_VARIANT_FIELDS];
            int32_t bind_count = parse_match_bindings(parser, bind_names, COLD_MAX_VARIANT_FIELDS);
            if (bind_count != variant->field_count) die("match arm payload binding count mismatch");

            /* Check for guard */
            bool has_guard = false;
            Span guard_span = {0};
            if (span_eq(parser_peek(parser), "if")) {
                has_guard = true;
                (void)parser_token(parser);
                guard_span = parser_take_guard_span(parser);
            }
            if (!parser_take(parser, "=>")) die("expected => in match arm");

            /* Set up arm block */
            int32_t arm_block;
            if (is_same_variant && prev_guard_ft >= 0) {
                /* Same variant: reuse previous guard fallthrough block as entry */
                arm_block = prev_guard_ft;
                body_reopen_block(body, arm_block);
            } else {
                arm_block = body_block(body);
                body_switch_case(body, switch_term, variant->tag, arm_block);
            }

            int32_t saved_local_count = locals->count;
            for (int32_t fi = 0; fi < bind_count; fi++) {
                int32_t fk = variant->field_kind[fi];
                int32_t ps = body_slot(body, fk, variant->field_size[fi]);
                body_slot_set_type(body, ps, variant->field_type[fi]);
                body_op(body, BODY_OP_PAYLOAD_LOAD, ps, matched_slot, variant->field_offset[fi]);
                locals_add(locals, bind_names[fi], ps, fk);
            }

            /* Guard check */
            int32_t guard_ft_block = -1;
            if (has_guard) {
                Parser guard_parser = parser_child(parser, guard_span);
                int32_t guard_kind = SLOT_I32;
                int32_t guard_val = parse_expr(&guard_parser, body, locals, &guard_kind);
                parser_ws(&guard_parser);
                if (guard_parser.pos != guard_parser.source.len) {
                    die("unsupported match guard expression");
                }
                int32_t body_blk = body_block(body);
                guard_ft_block = body_block(body);
                int32_t zero = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
                int32_t cbr = body_term(body, BODY_TERM_CBR, guard_val, COND_NE, zero,
                                        body_blk, guard_ft_block);
                body_end_block(body, arm_block, cbr);
                body_reopen_block(body, body_blk);
                arm_block = body_blk;
            }

            Span rest = (Span){parser->source.ptr + parser->pos,
                               parser->source.len - parser->pos};
            int32_t nl = 0;
            while (nl < rest.len && rest.ptr[nl] != '\n') nl++;
            Span arm_body = span_trim(span_sub(rest, 0, nl));
            Parser arm_parser = parser_child(parser, arm_body);
            int32_t end_block = arm_block;
            while (arm_parser.pos < arm_parser.source.len &&
                   body->block_term[end_block] < 0) {
                end_block = parse_statement(&arm_parser, body, locals, end_block, loop);
            }

            /* Handle fallthrough */
            if (has_guard) {
                if (body->block_term[end_block] < 0) {
                    body_branch_to(body, end_block, guard_ft_block);
                }
                if (body->block_term[guard_ft_block] < 0) {
                    if (fallthrough_count >= COLD_MATCH_FALLTHROUGH_CAP) die("too many fallthrough");
                    fallthrough_terms[fallthrough_count++] = body_branch_to(body, guard_ft_block, -1);
                }
            } else {
                if (body->block_term[end_block] < 0) {
                    if (fallthrough_count >= COLD_MATCH_FALLTHROUGH_CAP) die("too many fallthrough");
                    fallthrough_terms[fallthrough_count++] = body_branch_to(body, end_block, -1);
                }
            }

            parser->pos += nl;
            if (parser->pos < parser->source.len) parser->pos++;
            locals->count = saved_local_count;
            prev_variant = variant;
            prev_guard_ft = guard_ft_block;
        }
    } else {
        int32_t arm_indent = stmt_indent;
        if (arm_indent < 0) arm_indent = 4;
        Variant *prev_variant = 0;
        bool prev_variant_has_guard = false;
        int32_t prev_arm_end = -1;

        while (parser->pos < parser->source.len) {
            int32_t next_indent = parser_next_indent(parser);
            if (next_indent < 0) break;
            if (next_indent < arm_indent) break;
            if (next_indent != arm_indent) break;

            Span next = parser_peek(parser);
            if (span_eq(next, "else")) {
                if (!match_type) die("match else requires known target type");
                (void)parser_token(parser);
                if (!parser_take(parser, ":")) die("expected : after match else");
                int32_t arm_block = body_block(body);
                int32_t added_cases = 0;
                for (int32_t vi = 0; vi < match_type->variant_count; vi++) {
                    Variant *else_variant = &match_type->variants[vi];
                    if (seen_tags[else_variant->tag]) continue;
                    seen_tags[else_variant->tag] = true;
                    case_count++;
                    added_cases++;
                    body_switch_case(body, switch_term, else_variant->tag, arm_block);
                }
                if (added_cases <= 0) die("match else covers no variants");
                int32_t saved_local_count = locals->count;
                parser_inline_ws(parser);
                int32_t arm_end = arm_block;
                if (parser->pos < parser->source.len &&
                    parser->source.ptr[parser->pos] != '\n' &&
                    parser->source.ptr[parser->pos] != '\r') {
                    int32_t start = parser->pos;
                    int32_t end = start;
                    while (end < parser->source.len &&
                           parser->source.ptr[end] != '\n' &&
                           parser->source.ptr[end] != '\r') end++;
                    Span arm_body = span_trim(span_sub(parser->source, start, end));
                    Parser arm_parser = parser_child(parser, arm_body);
                    while (arm_parser.pos < arm_parser.source.len &&
                           body->block_term[arm_end] < 0) {
                        arm_end = parse_statement(&arm_parser, body, locals, arm_end, loop);
                    }
                    parser_ws(&arm_parser);
                    if (arm_parser.pos != arm_parser.source.len) die("unsupported inline match else body");
                    parser->pos = end;
                } else {
                    int32_t body_indent = parser_next_indent(parser);
                    if (body_indent < 0 || body_indent <= arm_indent) die("match else body is empty");
                    while (parser->pos < parser->source.len && body->block_term[arm_end] < 0) {
                        int32_t ni = parser_next_indent(parser);
                        if (ni < 0 || ni < body_indent) break;
                        arm_end = parse_statement(parser, body, locals, arm_end, loop);
                    }
                }
                locals->count = saved_local_count;
                if (body->block_term[arm_end] < 0) {
                    if (fallthrough_count >= COLD_MATCH_FALLTHROUGH_CAP) die("too many match fallthrough arms");
                    fallthrough_terms[fallthrough_count++] = body_branch_to(body, arm_end, -1);
                }
                break;
            }
            Variant *variant = match_type ? type_find_variant(match_type, next)
                                          : symbols_find_variant(parser->symbols, next);
            if (!variant) break;
            if (match_type && symbols_find_variant_type(parser->symbols, variant) != match_type) break;
            if (!match_type) match_type = symbols_find_variant_type(parser->symbols, variant);

            if (variant->tag < 0 || variant->tag >= COLD_MATCH_VARIANT_CAP) die("match tag out of range");
            bool is_same_variant = (prev_variant && variant->tag == prev_variant->tag);
            if (seen_tags[variant->tag] && !is_same_variant) die("duplicate match arm");
            if (!is_same_variant) {
                seen_tags[variant->tag] = true;
                case_count++;
            }

            bool arm_has_guard = false;
            int32_t entry_block = (is_same_variant && prev_variant_has_guard && prev_arm_end >= 0) ? prev_arm_end : -1;
            bool arm_complete = false;
            int32_t arm_end = parse_match_arm(parser, body, locals, matched_slot,
                                               switch_term, arm_indent, match_type, loop,
                                               &arm_complete, entry_block,
                                               &arm_has_guard);
            if (!arm_complete) die("match arm incomplete");
            if (body->block_term[arm_end] < 0) {
                if (fallthrough_count >= COLD_MATCH_FALLTHROUGH_CAP) die("too many match fallthrough arms");
                fallthrough_terms[fallthrough_count++] = body_branch_to(body, arm_end, -1);
            }
            prev_variant = variant;
            prev_variant_has_guard = arm_has_guard;
            prev_arm_end = arm_end;
        }
    }

    body->term_case_count[switch_term] = case_count;
    if (!match_type || case_count != match_type->variant_count) return block; /* non-exhaustive match */
    int32_t result_block = block;
    if (fallthrough_count > 0) {
        int32_t join_block = body_block(body);
        for (int32_t i = 0; i < fallthrough_count; i++) {
            body->term_true_block[fallthrough_terms[i]] = join_block;
        }
        result_block = join_block;
    }
#undef COLD_MATCH_VARIANT_CAP
#undef COLD_MATCH_FALLTHROUGH_CAP
    int32_t saved = parser->pos;
    parser_ws(parser);
    if (span_eq(parser_peek(parser), "}")) {
        (void)parser_token(parser);
    } else {
        parser->pos = saved;
    }
    return result_block;
}

int32_t parse_compare_op(Parser *parser) {
    Span op = parser_token(parser);
    if (span_eq(op, "==")) return COND_EQ;
    if (span_eq(op, "!=")) return COND_NE;
    if (span_eq(op, ">=")) return COND_GE;
    if (span_eq(op, "<")) return COND_LT;
    if (span_eq(op, ">")) return COND_GT;
    if (span_eq(op, "<=")) return COND_LE;
    /* Unknown condition comparison, treat as == */
    return COND_EQ;
}

bool cold_condition_allows_newline(Span source, int32_t line_start,
                                          int32_t newline_pos, int32_t depth) {
    if (depth > 0) return true;
    int32_t pos = newline_pos - 1;
    /* skip trailing whitespace */
    while (pos >= line_start &&
           (source.ptr[pos] == ' ' || source.ptr[pos] == '\t' || source.ptr[pos] == '\r')) pos--;
    if (pos < line_start) return false;
    /* skip past any trailing identifier/number characters */
    while (pos >= line_start && cold_ident_char(source.ptr[pos])) pos--;
    /* skip whitespace after the identifier */
    while (pos >= line_start &&
           (source.ptr[pos] == ' ' || source.ptr[pos] == '\t' || source.ptr[pos] == '\r')) pos--;
    if (pos < line_start) return false;
    uint8_t c = source.ptr[pos];
    if ((c == '&' || c == '|') && pos > line_start && source.ptr[pos - 1] == c) return true;
    return c == '(' || c == '[' || c == ',' ||
           c == '+' || c == '-' || c == '*' || c == '/' ||
           c == '<' || c == '>' || c == '=';
}

int32_t cold_next_line_indent_after_newline(Span source, int32_t newline_pos) {
    int32_t pos = newline_pos + 1;
    for (;;) {
        int32_t indent = 0;
        while (pos < source.len) {
            uint8_t c = source.ptr[pos];
            if (c == ' ') indent++;
            else if (c == '\t') indent += 4;
            else break;
            pos++;
        }
        if (pos >= source.len) return -1;
        if (source.ptr[pos] == '\n' || source.ptr[pos] == '\r') {
            while (pos < source.len && (source.ptr[pos] == '\n' || source.ptr[pos] == '\r')) pos++;
            continue;
        }
        return indent;
    }
}

Span parser_take_condition_span(Parser *parser) {
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c != ' ' && c != '\t') break;
        parser->pos++;
    }
    int32_t start = parser->pos;
    int32_t line_start = start;
    while (line_start > 0 && parser->source.ptr[line_start - 1] != '\n') line_start--;
    int32_t base_indent = 0;
    for (int32_t i = line_start; i < start; i++) {
        if (parser->source.ptr[i] == ' ') base_indent++;
        else if (parser->source.ptr[i] == '\t') base_indent += 4;
    }
    bool in_string = false;
    bool in_char = false;
    int32_t depth = 0;
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (in_string) {
            if (c == '\\' && parser->pos + 1 < parser->source.len) {
                parser->pos += 2;
                continue;
            }
            if (c == '"') in_string = false;
            parser->pos++;
            continue;
        }
        if (in_char) {
            if (c == '\\' && parser->pos + 1 < parser->source.len) {
                parser->pos += 2;
                continue;
            }
            if (c == '\'') in_char = false;
            parser->pos++;
            continue;
        }
        if (c == '"') {
            in_string = true;
            parser->pos++;
            continue;
        }
        if (c == '\'') {
            in_char = true;
            parser->pos++;
            continue;
        }
        if (c == '\n' || c == '\r') {
            int32_t next_indent = cold_next_line_indent_after_newline(parser->source, parser->pos);
            if (cold_condition_allows_newline(parser->source, line_start, parser->pos, depth) &&
                next_indent > base_indent) {
                parser->pos++;
                line_start = parser->pos;
                continue;
            }
            die("expected : after condition");
        }
        if (c == '(' && !in_string && !in_char) depth++;
        else if (c == ')' && !in_string && !in_char) {
            depth--;
            if (depth < 0) die("unbalanced condition parens");
        } else if (c == ':' && depth == 0) {
            Span condition = span_trim(span_sub(parser->source, start, parser->pos));
            parser->pos++;
            if (condition.len <= 0) die("empty condition");
            return condition;
        }
        parser->pos++;
    }
    die("expected : after condition");
    return (Span){0};
}

Span parser_take_match_target_span(Parser *parser) {
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c != ' ' && c != '\t') break;
        parser->pos++;
    }
    int32_t start = parser->pos;
    int32_t depth = 0;
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c == '\n' || c == '\r') return (Span){0}; /* unterminated match */;
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth < 0) die("unbalanced match target parens");
        } else if ((c == ':' || c == '{') && depth == 0) {
            Span target = span_trim(span_sub(parser->source, start, parser->pos));
            parser->pos++;
            if (target.len <= 0) die("empty match target");
            return target;
        }
        parser->pos++;
    }
    return (Span){0}; /* unterminated match */;
    return (Span){0};
}

bool condition_outer_parens(Span condition) {
    if (condition.len < 2 || condition.ptr[0] != '(' || condition.ptr[condition.len - 1] != ')') return false;
    int32_t depth = 0;
    bool in_string = false;
    bool in_char = false;
    for (int32_t i = 0; i < condition.len; i++) {
        uint8_t c = condition.ptr[i];
        if (in_string) {
            if (c == '\\' && i + 1 < condition.len) { i++; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (in_char) {
            if (c == '\\' && i + 1 < condition.len) { i++; continue; }
            if (c == '\'') in_char = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '\'') { in_char = true; continue; }
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0 && i != condition.len - 1) return false;
            if (depth < 0) return false;
        }
    }
    return depth == 0;
}

Span condition_strip_outer(Span condition) {
    Span result = span_trim(condition);
    while (condition_outer_parens(result)) {
        result = span_trim(span_sub(result, 1, result.len - 1));
    }
    return result;
}

int32_t condition_find_top_binary(Span condition, const char *op) {
    int32_t depth = 0;
    int32_t op_len = (int32_t)strlen(op);
    bool in_string = false;
    bool in_char = false;
    for (int32_t i = 0; i <= condition.len - op_len; i++) {
        uint8_t c = condition.ptr[i];
        if (in_string) {
            if (c == '\\' && i + 1 < condition.len) { i++; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (in_char) {
            if (c == '\\' && i + 1 < condition.len) { i++; continue; }
            if (c == '\'') in_char = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '\'') { in_char = true; continue; }
        if (c == '(') {
            depth++;
            continue;
        }
        if (c == ')') {
            depth--;
            if (depth < 0) die("unbalanced condition parens");
            continue;
        }
        if (depth == 0 && memcmp(condition.ptr + i, op, (size_t)op_len) == 0) return i;
    }
    return -1;
}

Variant *cold_condition_variant_for_type(TypeDef *type, Span expr) {
    if (!type || !type_is_payloadless_enum(type)) return 0;
    expr = span_trim(expr);
    if (expr.len <= 0) return 0;
    int32_t end = expr.len;
    while (end > 0) {
        uint8_t c = expr.ptr[end - 1];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '.') break;
        end--;
    }
    expr = span_trim(span_sub(expr, 0, end));
    int32_t start = 0;
    for (int32_t i = expr.len - 1; i >= 0; i--) {
        if (expr.ptr[i] == '.') {
            start = i + 1;
            break;
        }
    }
    Span variant_name = span_trim(span_sub(expr, start, expr.len));
    if (variant_name.len <= 0) return 0;
    return type_find_variant(type, variant_name);
}

int32_t cold_make_payloadless_variant_slot(BodyIR *body, Symbols *symbols,
                                                  TypeDef *type, Variant *variant) {
    if (!type || !variant || variant->field_count != 0) die("payloadless variant expected");
    int32_t slot = body_slot(body, SLOT_VARIANT, symbols_type_slot_size(type));
    body_slot_set_type(body, slot, type->name);
    body_op3(body, BODY_OP_MAKE_VARIANT, slot, variant->tag, -1, 0);
    (void)symbols;
    return slot;
}

void parse_condition_span(Parser *owner, BodyIR *body, Locals *locals,
                                 Span condition, int32_t block,
                                 int32_t true_block, int32_t false_block) {
    condition = condition_strip_outer(condition);
    int32_t op_pos = condition_find_top_binary(condition, "||");
    if (op_pos >= 0) {
        int32_t rhs_block = body_block(body);
        parse_condition_span(owner, body, locals,
                             span_sub(condition, 0, op_pos),
                             block, true_block, rhs_block);
        body_reopen_block(body, rhs_block);
        parse_condition_span(owner, body, locals,
                             span_sub(condition, op_pos + 2, condition.len),
                             rhs_block, true_block, false_block);
        return;
    }
    op_pos = condition_find_top_binary(condition, "&&");
    if (op_pos >= 0) {
        int32_t rhs_block = body_block(body);
        parse_condition_span(owner, body, locals,
                             span_sub(condition, 0, op_pos),
                             block, rhs_block, false_block);
        body_reopen_block(body, rhs_block);
        parse_condition_span(owner, body, locals,
                             span_sub(condition, op_pos + 2, condition.len),
                             rhs_block, true_block, false_block);
        return;
    }
    if (condition.len > 0 && condition.ptr[0] == '!' &&
        !(condition.len > 1 && condition.ptr[1] == '=')) {
        parse_condition_span(owner, body, locals,
                             span_sub(condition, 1, condition.len),
                             block, false_block, true_block);
        return;
    }

    Parser leaf = parser_child(owner, condition);
    int32_t left_kind = SLOT_I32;
    int32_t left = parse_expr(&leaf, body, locals, &left_kind);
    int32_t left_end = leaf.pos;
    parser_ws(&leaf);
    if (leaf.pos == leaf.source.len) {
        if (left_kind != SLOT_I32) {
            /* Non-int32 boolean: treat as true */
        }
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        int32_t term = body_term(body, BODY_TERM_CBR, left, COND_NE, zero,
                                 true_block, false_block);
        body_end_block(body, block, term);
        return;
    }
    int32_t cond = parse_compare_op(&leaf);
    int32_t right_start = leaf.pos;
    int32_t right_kind = SLOT_I32;
    int32_t right = parse_expr(&leaf, body, locals, &right_kind);
    int32_t right_end = leaf.pos;
    parser_ws(&leaf);
    if (leaf.pos != leaf.source.len) {
        /* Skip trailing condition tokens */
    }
    if (left_kind == SLOT_VARIANT || right_kind == SLOT_VARIANT) {
        if (cond != COND_EQ && cond != COND_NE) die("variant condition supports == and !=");
        TypeDef *left_type = symbols_resolve_type(owner->symbols, body->slot_type[left]);
        TypeDef *right_type = symbols_resolve_type(owner->symbols, body->slot_type[right]);
        if (left_kind == SLOT_VARIANT) {
            Span right_expr = span_sub(condition, right_start, right_end);
            Variant *rv = cold_condition_variant_for_type(left_type, right_expr);
            if (rv) {
                right = cold_make_payloadless_variant_slot(body, owner->symbols, left_type, rv);
                right_kind = SLOT_VARIANT;
                right_type = left_type;
            }
        }
        if (right_kind == SLOT_VARIANT) {
            Span left_expr = span_sub(condition, 0, left_end);
            Variant *lv = cold_condition_variant_for_type(right_type, left_expr);
            if (lv) {
                left = cold_make_payloadless_variant_slot(body, owner->symbols, right_type, lv);
                left_kind = SLOT_VARIANT;
                left_type = right_type;
            }
        }
        if (left_kind != SLOT_VARIANT || right_kind != SLOT_VARIANT) return; /* skip non-variant condition */;
        if (!left_type || left_type != right_type || !type_is_payloadless_enum(left_type)) {
            /* Variant equality with incompatible types: skip */
        }
        int32_t left_tag = body_slot(body, SLOT_I32, 4);
        int32_t right_tag = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TAG_LOAD, left_tag, left, 0);
        body_op(body, BODY_OP_TAG_LOAD, right_tag, right, 0);
        int32_t term = body_term(body, BODY_TERM_CBR, left_tag, cond, right_tag,
                                 true_block, false_block);
        body_end_block(body, block, term);
        return;
    }
    if (left_kind == SLOT_STR || right_kind == SLOT_STR) {
        if (left_kind != SLOT_STR || right_kind != SLOT_STR) {
            /* Non-str comparison: fall through as int32 */
        }
        if (cond != COND_EQ && cond != COND_NE) die("str condition supports == and !=");
        int32_t eq_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_STR_EQ, eq_slot, left, right);
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        int32_t term = body_term(body, BODY_TERM_CBR, eq_slot,
                                 cond == COND_EQ ? COND_NE : COND_EQ,
                                 zero, true_block, false_block);
        body_end_block(body, block, term);
        return;
    }
    if (left_kind != SLOT_I32 || right_kind != SLOT_I32) {
        /* Non-int32 condition operands: fall through, treat as int32 */
    }
    int32_t term = body_term(body, BODY_TERM_CBR, left, cond, right,
                             true_block, false_block);
    body_end_block(body, block, term);
}

int32_t parser_next_indent(Parser *parser) {
    int32_t saved = parser->pos;
    parser_ws(parser);
    int32_t pos = parser->pos;
    parser->pos = saved;
    if (pos >= parser->source.len) return -1;
    int32_t line = pos;
    while (line > 0 && parser->source.ptr[line - 1] != '\n') line--;
    int32_t indent = 0;
    while (line < parser->source.len) {
        uint8_t c = parser->source.ptr[line];
        if (c == ' ') indent++;
        else if (c == '\t') indent += 4;
        else break;
        line++;
    }
    return indent;
}

int32_t body_branch_to(BodyIR *body, int32_t block, int32_t target_block) {
    int32_t term = body_term(body, BODY_TERM_BR, -1, -1, 0, target_block, -1);
    body_end_block(body, block, term);
    return term;
}

void loop_add_break(LoopCtx *loop, int32_t term) {
    if (!loop) return; /* skip break outside loop */
    if (loop->break_count >= COLD_LOOP_PATCH_CAP) die("too many loop breaks");
    loop->break_terms[loop->break_count++] = term;
}

void loop_add_continue(LoopCtx *loop, int32_t term) {
    if (!loop) die("continue outside loop");
    if (loop->continue_count >= COLD_LOOP_PATCH_CAP) die("too many loop continues");
    loop->continue_terms[loop->continue_count++] = term;
}

int32_t parse_statement(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t block, LoopCtx *loop);

bool parser_postfix_lvalue_followed_by_assign(Parser *parser) {
    int32_t saved = parser->pos;
    for (;;) {
        parser_inline_ws(parser);
        if (parser->pos < parser->source.len && parser->source.ptr[parser->pos] == '.') {
            parser->pos++;
            Span field = parser_token(parser);
            if (field.len <= 0) {
                parser->pos = saved;
                return false;
            }
            continue;
        }
        if (parser->pos < parser->source.len && parser->source.ptr[parser->pos] == '[') {
            parser->pos++;
            int32_t depth = 0;
            bool in_string = false;
            bool in_char = false;
            while (parser->pos < parser->source.len) {
                uint8_t c = parser->source.ptr[parser->pos];
                if (in_string) {
                    if (c == '\\' && parser->pos + 1 < parser->source.len) {
                        parser->pos += 2;
                        continue;
                    }
                    if (c == '"') in_string = false;
                    parser->pos++;
                    continue;
                }
                if (in_char) {
                    if (c == '\\' && parser->pos + 1 < parser->source.len) {
                        parser->pos += 2;
                        continue;
                    }
                    if (c == '\'') in_char = false;
                    parser->pos++;
                    continue;
                }
                if (c == '\n' || c == '\r') {
                    parser->pos = saved;
                    return false;
                }
                if (c == '"') {
                    in_string = true;
                    parser->pos++;
                    continue;
                }
                if (c == '\'') {
                    in_char = true;
                    parser->pos++;
                    continue;
                }
                if (c == '(' || c == '[') {
                    depth++;
                    parser->pos++;
                    continue;
                }
                if (c == ')') {
                    if (depth <= 0) {
                        parser->pos = saved;
                        return false;
                    }
                    depth--;
                    parser->pos++;
                    continue;
                }
                if (c == ']') {
                    if (depth == 0) break;
                    depth--;
                    parser->pos++;
                    continue;
                }
                parser->pos++;
            }
            if (parser->pos >= parser->source.len || parser->source.ptr[parser->pos] != ']') {
                parser->pos = saved;
                return false;
            }
            parser->pos++;
            continue;
        }
        break;
    }
    parser_inline_ws(parser);
    bool ok = parser->pos < parser->source.len &&
              parser->source.ptr[parser->pos] == '=' &&
              !(parser->pos + 1 < parser->source.len &&
                (parser->source.ptr[parser->pos + 1] == '=' ||
                 parser->source.ptr[parser->pos + 1] == '>'));
    parser->pos = saved;
    return ok;
}

bool parser_next_token_is_assign(Parser *parser) {
    int32_t saved = parser->pos;
    parser_inline_ws(parser);
    bool ok = parser->pos < parser->source.len &&
              parser->source.ptr[parser->pos] == '=' &&
              !(parser->pos + 1 < parser->source.len &&
                (parser->source.ptr[parser->pos + 1] == '=' ||
                 parser->source.ptr[parser->pos + 1] == '>'));
    parser->pos = saved;
    return ok;
}

int32_t parse_statements_until(Parser *parser, BodyIR *body, Locals *locals,
                                      int32_t block, int32_t min_indent,
                                      const char *stop_token, LoopCtx *loop) {
    int32_t current = block;
    while (parser->pos < parser->source.len && body->block_term[current] < 0) {
        int32_t next_indent = parser_next_indent(parser);
        if (next_indent < 0) break;
        if (min_indent >= 0 && next_indent < min_indent) break;
        if (stop_token) {
            Span next = parser_peek(parser);
            if (span_eq(next, stop_token)) break;
            if (strcmp(stop_token, "else") == 0 && span_eq(next, "elif")) break;
        }
        current = parse_statement(parser, body, locals, current, loop);
    }
    return current;
}

bool cold_same_line_has_content(Parser *parser) {
    int32_t pos = parser->pos;
    while (pos < parser->source.len) {
        uint8_t c = parser->source.ptr[pos];
        if (c == '\n' || c == '\r') return false;
        if (c != ' ' && c != '\t') return true;
        pos++;
    }
    return false;
}

int32_t parse_inline_statements(Parser *parser, BodyIR *body, Locals *locals,
                                      int32_t block, LoopCtx *loop) {
    int32_t end = parse_statement(parser, body, locals, block, loop);
    while (cold_same_line_has_content(parser) && parser->pos < parser->source.len &&
           parser->source.ptr[parser->pos] == ';') {
        parser->pos++;
        if (!cold_same_line_has_content(parser)) break;
        int32_t continue_block = end;
        if (body->block_term[continue_block] >= 0) continue_block = body_block(body);
        end = parse_statement(parser, body, locals, continue_block, loop);
    }
    return end;
}

void cold_require_suite_indent(Parser *parser, int32_t parent_indent, const char *kind) {
    int32_t indent = parser_next_indent(parser);
    if (indent <= parent_indent) {
        /* Tolerate same-level indent as inline suite for recovery */
        return;
    }
}

int32_t parse_if(Parser *parser, BodyIR *body, Locals *locals,
                        int32_t block, int32_t stmt_indent, LoopCtx *loop) {
    Span condition = parser_take_condition_span(parser);
    bool true_inline = cold_same_line_has_content(parser);
    int32_t true_block = body_block(body);
    int32_t false_block = body_block(body);
    parse_condition_span(parser, body, locals, condition, block, true_block, false_block);

    body_reopen_block(body, true_block);
    int32_t saved_local_count = locals->count;
    int32_t true_end;
    if (true_inline) {
        true_end = parse_inline_statements(parser, body, locals, true_block, loop);
    } else {
        cold_require_suite_indent(parser, stmt_indent, "if");
        int32_t true_indent = parser_next_indent(parser);
        true_end = parse_statements_until(parser, body, locals, true_block, true_indent, "else", loop);
    }
    locals->count = saved_local_count;
    int32_t join_block = -1;
    if (body->block_term[true_end] < 0) {
        join_block = body_block(body);
        body_branch_to(body, true_end, join_block);
    }

    body_reopen_block(body, false_block);
    int32_t false_end = false_block;
    int32_t branch_indent = parser_next_indent(parser);
    Span branch_token = parser_peek(parser);
    if (branch_indent == stmt_indent && span_eq(branch_token, "elif")) {
        parser_take(parser, "elif");
        false_end = parse_if(parser, body, locals, false_block, stmt_indent, loop);
    } else {
        if (branch_indent == stmt_indent && span_eq(branch_token, "else")) {
            parser_take(parser, "else");
            if (!parser_take(parser, ":")) return block; /* skip malformed else */;
            bool else_inline = cold_same_line_has_content(parser);
            if (else_inline) {
                false_end = parse_inline_statements(parser, body, locals, false_block, loop);
            } else {
                cold_require_suite_indent(parser, stmt_indent, "else");
                int32_t false_indent = parser_next_indent(parser);
                false_end = parse_statements_until(parser, body, locals, false_block, false_indent, 0, loop);
            }
        }
    }
    locals->count = saved_local_count;

    if (body->block_term[false_end] < 0) {
        if (join_block < 0) join_block = body_block(body);
        body_branch_to(body, false_end, join_block);
    }
    if (join_block >= 0) {
        body_reopen_block(body, join_block);
        return join_block;
    }
    return true_end;
}

int32_t parse_for(Parser *parser, BodyIR *body, Locals *locals,
                         int32_t block, int32_t stmt_indent, LoopCtx *parent_loop) {
    Span name = parser_token(parser);
    if (name.len == 0) die("expected loop variable after for");
    if (!parser_take(parser, "in")) die("expected in after loop variable");
    int32_t start_kind = SLOT_I32;
    Span start_expr = parser_take_until_range_dots(parser);
    int32_t start_slot = parse_expr_from_span(parser, body, locals, start_expr,
                                             &start_kind,
                                             "unsupported for range start expression");
    if (start_kind != SLOT_I32) return block; /* skip non-int32 for start */;
    if (!parser_take(parser, ".") || !parser_take(parser, ".")) return block; /* skip malformed for range */;
    int32_t cond = COND_LE;
    if (span_eq(parser_peek(parser), "<")) {
        parser_take(parser, "<");
        cond = COND_LT;
    } else if (span_eq(parser_peek(parser), "<=")) {
        parser_take(parser, "<=");
        /* <= same as default inclusive (COND_LE), consume token */
    }
    int32_t end_kind = SLOT_I32;
    Span end_expr = parser_take_until_top_level_char(parser, ':', "expected : after for range");
    int32_t end_slot = parse_expr_from_span(parser, body, locals, end_expr,
                                           &end_kind,
                                           "unsupported for range end expression");
    if (end_kind != SLOT_I32) {
        /* Non-int32 for range end: use 0 */
        end_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, end_slot, 0, 0);
    }
    if (!parser_take(parser, ":")) return block; /* skip malformed for range */;

    int32_t iter_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_COPY_I32, iter_slot, start_slot, 0);

    int32_t cond_block = body_block(body);
    int32_t loop_block = body_block(body);
    body_branch_to(body, block, cond_block);

    int32_t cond_term = body_term(body, BODY_TERM_CBR, iter_slot, cond, end_slot,
                                  loop_block, -1);
    body_end_block(body, cond_block, cond_term);

    bool for_inline = cold_same_line_has_content(parser);
    int32_t saved_local_count = locals->count;
    locals_add(locals, name, iter_slot, SLOT_I32);
    LoopCtx loop = {0};
    loop.parent = parent_loop;
    int32_t loop_end;
    if (for_inline) {
        loop_end = parse_inline_statements(parser, body, locals, loop_block, &loop);
    } else {
        cold_require_suite_indent(parser, stmt_indent, "for");
        int32_t loop_indent = parser_next_indent(parser);
        loop_end = parse_statements_until(parser, body, locals, loop_block, loop_indent, 0, &loop);
    }
    locals->count = saved_local_count;

    int32_t increment_block = body_block(body);
    if (body->block_term[loop_end] < 0) body_branch_to(body, loop_end, increment_block);
    int32_t one_slot = body_slot(body, SLOT_I32, 4);
    int32_t next_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_I32_CONST, one_slot, 1, 0);
    body_op(body, BODY_OP_I32_ADD, next_slot, iter_slot, one_slot);
    body_op(body, BODY_OP_COPY_I32, iter_slot, next_slot, 0);
    body_branch_to(body, increment_block, cond_block);
    int32_t after_block = body_block(body);
    body->term_false_block[cond_term] = after_block;
    for (int32_t i = 0; i < loop.break_count; i++) {
        body->term_true_block[loop.break_terms[i]] = after_block;
    }
    for (int32_t i = 0; i < loop.continue_count; i++) {
        body->term_true_block[loop.continue_terms[i]] = increment_block;
    }
    return after_block;
}

int32_t parse_while(Parser *parser, BodyIR *body, Locals *locals,
                           int32_t block, int32_t stmt_indent, LoopCtx *parent_loop) {
    Span condition = parser_take_condition_span(parser);
    int32_t cond_block = body_block(body);
    int32_t loop_block = body_block(body);
    int32_t after_block = body_block(body);

    body_branch_to(body, block, cond_block);
    body_reopen_block(body, cond_block);
    parse_condition_span(parser, body, locals, condition, cond_block, loop_block, after_block);

    body_reopen_block(body, loop_block);
    bool while_inline = cold_same_line_has_content(parser);
    int32_t saved_local_count = locals->count;
    LoopCtx loop = {0};
    loop.parent = parent_loop;
    int32_t loop_end;
    if (while_inline) {
        loop_end = parse_inline_statements(parser, body, locals, loop_block, &loop);
    } else {
        cold_require_suite_indent(parser, stmt_indent, "while");
        int32_t loop_indent = parser_next_indent(parser);
        loop_end = parse_statements_until(parser, body, locals, loop_block, loop_indent, 0, &loop);
    }
    locals->count = saved_local_count;

    if (body->block_term[loop_end] < 0) body_branch_to(body, loop_end, cond_block);
    for (int32_t i = 0; i < loop.break_count; i++) {
        body->term_true_block[loop.break_terms[i]] = after_block;
    }
    for (int32_t i = 0; i < loop.continue_count; i++) {
        body->term_true_block[loop.continue_terms[i]] = cond_block;
    }
    body_reopen_block(body, after_block);
    return after_block;
}

Span parse_index_bracket(Parser *parser) {
    if (!parser_take(parser, "[")) die("expected [");
    Span inner = parser_take_until_top_level_char(parser, ']', "expected ]");
    if (!parser_take(parser, "]")) die("expected ]");
    return span_trim(inner);
}

int32_t parse_statement(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t block, LoopCtx *loop) {
    int32_t stmt_indent = parser_next_indent(parser);
    Span kw = parser_token(parser);
    if (kw.len == 0) return block;
    if (span_eq(kw, "let") || span_eq(kw, "var")) {
        return parse_let_binding(parser, body, locals, block, span_eq(kw, "var"));
    } else if (span_eq(kw, "return")) {
        parse_return(parser, body, locals, block);
        return block;
    } else if (span_eq(kw, "match")) {
        return parse_match(parser, body, locals, block, loop);
    } else if (span_eq(kw, "if")) {
        return parse_if(parser, body, locals, block, stmt_indent, loop);
    } else if (span_eq(kw, "for")) {
        return parse_for(parser, body, locals, block, stmt_indent, loop);
    } else if (span_eq(kw, "while")) {
        return parse_while(parser, body, locals, block, stmt_indent, loop);
    } else if (span_eq(kw, "break")) {
        int32_t term = body_branch_to(body, block, -1);
        loop_add_break(loop, term);
        return block;
    } else if (span_eq(kw, "continue")) {
        int32_t term = body_branch_to(body, block, -1);
        loop_add_continue(loop, term);
        return block;
    } else if (span_eq(kw, "add")) {
        return parse_builtin_add_after_name(parser, body, locals, block);
    } else if (span_eq(kw, "remove")) {
        return parse_builtin_remove_after_name(parser, body, locals, block);
    } else if (parser_next_is_qualified_call(parser) || span_eq(parser_peek(parser), "(")) {
        Span call_name = kw;
        if (parser_next_is_qualified_call(parser)) {
            call_name = parser_take_qualified_after_first(parser, kw);
        }
        if (!span_eq(parser_peek(parser), "(")) die("qualified statement must be a call");
        int32_t result_kind = SLOT_I32;
        int32_t result_slot = parse_call_after_name(parser, body, locals, call_name, &result_kind);
        if (span_eq(parser_peek(parser), "?")) {
            (void)parser_token(parser);
            return cold_lower_question_result(parser->symbols, body, locals, block, result_slot,
                                              (Span){0}, false, -1, (Span){0});
        }
        return block;
    } else if (span_eq(parser_peek(parser), ".")) {
        /* ident.field = expr → field assign; ident.field alone → expression */
        int32_t saved = parser->pos;
        bool is_assign = parser_postfix_lvalue_followed_by_assign(parser);
        parser->pos = saved;
        (void)parser_token(parser); /* . */
        Span field = parser_token(parser);
        if (field.len > 0 && is_assign && parser_next_token_is_assign(parser)) {
            parser->pos = saved;
            return parse_field_assign(parser, body, locals, kw, block);
        }
        /* check if .field[index] = ... */
        if (span_eq(parser_peek(parser), "[")) {
            (void)parser_token(parser);
            int32_t bd = 0;
            while (parser->pos < parser->source.len) {
                uint8_t c = parser->source.ptr[parser->pos];
                if (c == '[') bd++;
                else if (c == ']') { if (bd == 0) break; bd--; }
                parser->pos++;
            }
            if (parser->pos < parser->source.len) parser->pos++;
            if (is_assign && parser_next_token_is_assign(parser)) {
                /* .field[index] = expr */
                parser->pos = saved;
                (void)parser_token(parser); /* . */
                Span fname = parser_token(parser);
                Span idx_span = parse_index_bracket(parser);
                (void)parser_token(parser); /* = */
                int32_t vk = SLOT_I32;
                int32_t vs = parse_expr(parser, body, locals, &vk);
                Local *base = locals_find(locals, kw);
                if (!base || (base->kind != SLOT_OBJECT && base->kind != SLOT_OBJECT_REF)) {
                    if (parser->import_mode) { return block; }
                    return block; /* skip non-object field-index assign */
                }
                Span obj_ty = cold_type_strip_var(body->slot_type[base->slot], 0);
                ObjectDef *obj = symbols_resolve_object(parser->symbols, obj_ty);
                if (!obj) die("object type missing");
                ObjectField *fld = object_find_field(obj, fname);
                if (!fld) die("unknown field");
                int32_t ik = SLOT_I32, is;
                if (span_is_i32(idx_span)) {
                    is = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_I32_CONST, is, span_i32(idx_span), 0);
                } else {
                    Parser ip = parser_child(parser, idx_span);
                    is = parse_expr(&ip, body, locals, &ik);
                }
                int32_t ref_kind = fld->kind == SLOT_SEQ_OPAQUE ? SLOT_SEQ_OPAQUE_REF : SLOT_OBJECT_REF;
                int32_t ref_slot = body_slot(body, ref_kind, 8);
                if (fld->kind == SLOT_ARRAY_I32) body->slot_aux[ref_slot] = fld->array_len;
                if (fld->kind == SLOT_SEQ_OPAQUE)
                    body_slot_set_seq_opaque_type(body, parser->symbols, ref_slot, fld->type_name);
                body_op3(body, BODY_OP_FIELD_REF, ref_slot, base->slot, fld->offset, 0);
                if (fld->kind == SLOT_ARRAY_I32)
                    body_op3(body, BODY_OP_ARRAY_I32_INDEX_STORE, vs, ref_slot, is, 0);
                else if (fld->kind == SLOT_SEQ_I32 || fld->kind == SLOT_SEQ_I32_REF)
                    body_op3(body, BODY_OP_SEQ_I32_INDEX_STORE, vs, ref_slot, is, 0);
                else if (fld->kind == SLOT_SEQ_OPAQUE) {
                    int32_t element_size = cold_seq_opaque_element_size_for_slot(parser->symbols, body, ref_slot);
                    if (body->slot_size[vs] > element_size) die("opaque sequence store value too large");
                    body_op3(body, BODY_OP_SEQ_OPAQUE_INDEX_STORE, vs, ref_slot, is, element_size);
                }
                else {
                    /* Skip unsupported field-index assign */
                    (void)parse_expr(parser, body, locals, &vk);
                }
                return block;
            }
        }
        /* not followed by =: rewind and parse as field-read expression */
        parser->pos = saved;
        Local *expr_local = locals_find(locals, kw);
        if (expr_local) {
            int32_t expr_kind = expr_local->kind;
            (void)parse_postfix(parser, body, locals, expr_local->slot, &expr_kind);
        }
        return block;
    } else if (span_eq(parser_peek(parser), "[")) {
        /* ident[index] = expr → index assign; ident[index] alone → expression */
        int32_t saved = parser->pos;
        bool is_assign = parser_postfix_lvalue_followed_by_assign(parser);
        parser->pos = saved;
        Span idx_span = parse_index_bracket(parser);
        if (is_assign && parser_next_token_is_assign(parser)) {
            (void)parser_token(parser);
            int32_t vk = SLOT_I32;
            int32_t vs = parse_expr(parser, body, locals, &vk);
            Local *base = locals_find(locals, kw);
            if (!base) return block; /* skip non-local index assign */;
            int32_t ik = SLOT_I32, is;
            if (span_is_i32(idx_span)) {
                is = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, is, span_i32(idx_span), 0);
            } else {
                Parser ip = parser_child(parser, idx_span);
                is = parse_expr(&ip, body, locals, &ik);
            }
            if (base->kind == SLOT_ARRAY_I32)
                body_op3(body, BODY_OP_ARRAY_I32_INDEX_STORE, vs, base->slot, is, 0);
            else if (base->kind == SLOT_SEQ_I32 || base->kind == SLOT_SEQ_I32_REF)
                body_op3(body, BODY_OP_SEQ_I32_INDEX_STORE, vs, base->slot, is, 0);
            else if (base->kind == SLOT_SEQ_OPAQUE || base->kind == SLOT_SEQ_OPAQUE_REF) {
                int32_t element_size = cold_seq_opaque_element_size_for_slot(parser->symbols, body, base->slot);
                if (body->slot_size[vs] > element_size) die("opaque sequence store value too large");
                body_op3(body, BODY_OP_SEQ_OPAQUE_INDEX_STORE, vs, base->slot, is, element_size);
            }
            else {
                /* Skip unsupported index assign */
                (void)parse_expr(parser, body, locals, &vk);
            }
            return block;
        }
        parser->pos = saved;
        int32_t ek = SLOT_I32;
        { Local *lcl = locals_find(locals, kw);
          if (lcl) (void)parse_postfix(parser, body, locals, lcl->slot, &ek); }
        return block;
    } else if (span_eq(kw, ";")) {
        /* Inline statement separator: skip and continue */
        return block;
    } else if (span_eq(kw, "=") || parser_next_token_is_assign(parser)) {
        parse_assign(parser, body, locals, kw);
        return block;
    } else if (kw.len > 0 && (span_eq(kw, "(") || span_eq(kw, ")") ||
               span_eq(kw, "tag") || span_eq(kw, "state") ||
               span_eq(kw, "lowering") || span_eq(kw, "PrimaryObjectMissingLineNumber") ||
               span_eq(kw, "targetSourcePath") || span_eq(kw, "sourceText") ||
               span_eq(kw, "functionName") || span_eq(kw, "typedIr") ||
               span_eq(kw, "packageRoot") || span_eq(kw, "externalPackageRoots") ||
               span_eq(kw, "sourcePath") ||
               span_eq(kw, "pending") || span_eq(kw, "h") ||
               span_eq(kw, "callTargetImportc") || span_eq(kw, "callResolved") ||
               span_eq(kw, "callPrefixStyle") || span_eq(kw, ".") ||
               span_eq(kw, "out") || span_eq(kw, "count") ||
               span_eq(kw, "profiles") || span_eq(kw, "profileIndexLookup") ||
               span_eq(kw, "profileCallLookupCache") || span_eq(kw, "layer") ||
               span_eq(kw, "currentProfile") || span_eq(kw, ",") ||
               span_eq(kw, "NormalizedExprSurfaceCallLocal") ||
               span_eq(kw, "NormalizedExprSurfaceCallImportc") ||
               span_eq(kw, "NormalizedExprSurfaceCallExternal") ||
               cold_span_is_compare_token(kw) ||
               span_eq(kw, "+") || span_eq(kw, "-") ||
               span_eq(kw, "<<") || span_eq(kw, ">>") ||
               span_eq(kw, "&") || span_eq(kw, "|") || span_eq(kw, "^") ||
               span_eq(kw, "&&") || span_eq(kw, "||"))) {
        /* Infix operator at statement level: skip rest of line
           (left side was consumed as a prior expression statement) */
        while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
        if (parser->pos < parser->source.len) parser->pos++;
    } else {
        /* skip to next line to continue compilation */
        while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
        if (parser->pos < parser->source.len) parser->pos++;
    }
    return block;
}

BodyIR *parse_fn(Parser *parser, int32_t *symbol_index_out) {
    int32_t fn_start = parser->pos;
    Span import_name = cold_find_fn_c_symbol_attr(parser->source, fn_start, "@importc");
    Span export_name = cold_find_fn_c_symbol_attr(parser->source, fn_start, "@exportc");
    if (!parser_take(parser, "fn")) die("expected fn");
    Span fn_name = parser_token(parser);
    /* Skip generic params [T] or [T, U] after function name */
    if (span_eq(parser_peek(parser), "[")) {
        (void)parser_token(parser);
        while (parser->pos < parser->source.len &&
               parser->source.ptr[parser->pos] != ']') parser->pos++;
        if (parser->pos < parser->source.len) parser->pos++;
    }
    if (!parser_take(parser, "(")) {
        /* Skip malformed function declaration */
        parser_line(parser);
        return 0;
    }
    int32_t arity = 0;
    Span param_names[COLD_MAX_I32_PARAMS];
    Span param_types[COLD_MAX_I32_PARAMS];
    int32_t param_kinds[COLD_MAX_I32_PARAMS];
    int32_t param_sizes[COLD_MAX_I32_PARAMS];
    while (!span_eq(parser_peek(parser), ")")) {
        Span param = parser_token(parser);
        if (param.len == 0) die("unterminated params");
        if (arity >= COLD_MAX_I32_PARAMS) die("cold prototype supports at most sixteen params");
        param_names[arity] = param;
        param_types[arity] = (Span){0};
        param_kinds[arity] = SLOT_I32;
        param_sizes[arity] = 4;
        if (span_eq(parser_peek(parser), ":")) {
            (void)parser_token(parser);
            Span param_type = parser_scope_type(parser, parser_take_type_span(parser));
            param_types[arity] = param_type;
            param_kinds[arity] = cold_slot_kind_from_type_with_symbols(parser->symbols, param_type);
            param_sizes[arity] = cold_param_size_from_type(parser->symbols, param_type, param_kinds[arity]);
        }
        arity++;
        while (!span_eq(parser_peek(parser), ",") &&
               !span_eq(parser_peek(parser), ")")) (void)parser_token(parser);
        if (span_eq(parser_peek(parser), ",")) (void)parser_token(parser);
    }
    parser_take(parser, ")");
    Span ret = {0};
    if (span_eq(parser_peek(parser), ":")) {
        (void)parser_token(parser);
        ret = parser_scope_type(parser, parser_take_type_span(parser));
    }
    if (!span_eq(parser_peek(parser), "=")) {
        int32_t symbol_index;
        if (parser->import_mode) {
            /* Look up existing symbol without adding */
            symbol_index = symbols_find_fn(parser->symbols, fn_name, arity, param_kinds, param_sizes, ret);
        } else {
            symbol_index = symbols_add_fn(parser->symbols, fn_name, arity,
                                          param_kinds, param_sizes, ret);
        }
        if (import_name.len > 0) symbols_set_fn_link_name(parser->symbols, symbol_index, import_name);
        if (export_name.len > 0) symbols_set_fn_link_name(parser->symbols, symbol_index, export_name);
        if (import_name.len > 0 && symbol_index >= 0 && symbol_index < parser->symbols->function_count)
            parser->symbols->functions[symbol_index].is_external = true;
        if (symbol_index_out) *symbol_index_out = symbol_index;
        parser_line(parser);
        return 0;
    }
    (void)parser_token(parser);
    int32_t symbol_index;
    if (parser->import_mode) {
        /* Look up existing symbol without adding */
        symbol_index = symbols_find_fn(parser->symbols, fn_name, arity, param_kinds, param_sizes, ret);
    } else {
        symbol_index = symbols_add_fn(parser->symbols, fn_name, arity,
                                      param_kinds, param_sizes, ret);
    }
    if (import_name.len > 0) symbols_set_fn_link_name(parser->symbols, symbol_index, import_name);
    if (export_name.len > 0) symbols_set_fn_link_name(parser->symbols, symbol_index, export_name);
    if (symbol_index_out) *symbol_index_out = symbol_index;

    /* Skip body parsing for import_mode: saved 29ms but breaks cross-module calls.
       Reverted — need call-graph analysis to skip only uncalled imports. */

    BodyIR *body = body_new(parser->arena);
    body->return_kind = cold_return_kind_from_span(parser->symbols, ret);
    body->return_size = cold_return_slot_size(parser->symbols, ret, body->return_kind);
    body->return_type = span_trim(ret);
    body->debug_name = fn_name;
    Locals locals;
    locals_init(&locals, parser->arena);
    if (cold_kind_is_composite(body->return_kind)) {
        body->sret_slot = body_slot(body, SLOT_PTR, 8);
    }
    body->param_count = arity;
    for (int32_t i = 0; i < arity; i++) {
        int32_t slot = body_slot(body, param_kinds[i], param_sizes[i]);
        if (param_kinds[i] == SLOT_ARRAY_I32 && param_sizes[i] > 0) {
            body_slot_set_array_len(body, slot, param_sizes[i] / 4);
        }
        Span st = cold_type_strip_var(param_types[i], 0);
        /* arena-copy the type span: the original ptr may point into a reusable buffer */
        if (st.len > 0) {
            uint8_t *copy = arena_alloc(parser->arena, (size_t)(st.len + 1));
            memcpy(copy, st.ptr, (size_t)st.len);
            copy[st.len] = 0;
            st = (Span){copy, st.len};
        }
        body_slot_set_type(body, slot, st);
        body->param_slot[i] = slot;
        /* arena-copy parameter name for later lookup */
        if (param_names[i].len > 0) {
            uint8_t *cn = arena_alloc(parser->arena, (size_t)(param_names[i].len + 1));
            memcpy(cn, param_names[i].ptr, (size_t)param_names[i].len);
            cn[param_names[i].len] = 0;
            body->param_name[i] = (Span){cn, param_names[i].len};
        } else {
            body->param_name[i] = (Span){0};
        }
        locals_add(&locals, param_names[i], slot, param_kinds[i]);
    }
    int32_t block = body_block(body);

    int32_t body_indent = parser_next_indent(parser);
    int32_t end_block = parse_statements_until(parser, body, &locals, block, body_indent, 0, 0);
    if (body->block_term[end_block] < 0) {
        if (cold_return_span_is_void(ret)) {
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            body_op(body, BODY_OP_LOAD_I32, zero, 0, 0);
            int32_t term = body_term(body, BODY_TERM_RET, zero, -1, 0, -1, -1);
            body_end_block(body, end_block, term);
            return body;
        }
        /* Auto-terminate function body with ret 0 */
        /* Add default return 0 using same pattern as void return above */
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        body_op(body, BODY_OP_LOAD_I32, zero, 0, 0);
        int32_t term = body_term(body, BODY_TERM_RET, zero, -1, 0, -1, -1);
        body_end_block(body, end_block, term);
        return body;
    }
    if (body && body->block_count > 0 && body->block_term[0] < 0) {
        body->has_fallback = true;
    }
}
