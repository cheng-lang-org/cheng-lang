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
    child.function_bodies = owner->function_bodies;
    child.function_body_cap = owner->function_body_cap;
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
                   span_eq(type, "int8") ||
                   span_eq(type, "char") || span_eq(type, "uint32"))) return SLOT_I32_REF;
    if (is_var && span_eq(type, "str")) return SLOT_STR_REF;
    if (is_var && span_eq(type, "cstring")) return SLOT_OPAQUE_REF;
    if (is_var && cold_parse_i32_seq_type(type)) return SLOT_SEQ_I32_REF;
    if (is_var && cold_parse_str_seq_type(type)) return SLOT_SEQ_STR_REF;
    if (cold_type_has_pointer_suffix(type))
        return is_var ? SLOT_OPAQUE_REF : SLOT_OPAQUE;
    if (is_var && cold_type_has_qualified_name(type)) return SLOT_OPAQUE_REF;
    if (is_var) return SLOT_OBJECT_REF;
    if (type.len == 0 || span_eq(type, "i") || span_eq(type, "int32") ||
        span_eq(type, "int") || span_eq(type, "bool") ||
        span_eq(type, "uint8") || span_eq(type, "int8") ||
        span_eq(type, "char") ||
        span_eq(type, "uint32") || span_eq(type, "void")) {
        return SLOT_I32;
    }
    if (span_eq(type, "int64") || span_eq(type, "uint64")) return SLOT_I64;
    if (span_eq(type, "s") || span_eq(type, "str")) {
        return SLOT_STR;
    }
    if (span_eq(type, "cstring")) return SLOT_OPAQUE;
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
    if (cold_span_starts_with(span_trim(type), "uint8[") &&
        !span_eq(span_trim(type), "uint8[]")) return SLOT_ARRAY_I32;
    return 'i'; /* default to int32 */
    return SLOT_I32;
}

bool cold_parse_uint8_fixed_array_type(Symbols *symbols, Span type, int32_t *len_out) {
    type = span_trim(cold_type_strip_var(type, 0));
    if (!cold_span_starts_with(type, "uint8[")) return false;
    if (span_eq(type, "uint8[]")) return false;
    if (type.len < 8 || type.ptr[type.len - 1] != ']') return false;
    Span count = span_trim(span_sub(type, 6, type.len - 1));
    int32_t len = 0;
    if (span_is_i32(count)) {
        len = span_i32(count);
    } else if (symbols) {
        ConstDef *constant = symbols_find_const(symbols, count);
        if (constant) len = constant->value;
    }
    if (len <= 0) die("cold uint8 fixed array length must be positive");
    if (len_out) *len_out = len;
    return true;
}

bool cold_parse_fixed_array_type(Symbols *symbols, Span type, int32_t *len_out) {
    if (cold_parse_i32_array_type(type, len_out)) return true;
    return cold_parse_uint8_fixed_array_type(symbols, type, len_out);
}

int32_t cold_parser_slot_kind_from_type(Symbols *symbols, Span type) {
    bool is_var = false;
    Span stripped = span_trim(cold_type_strip_var(type, &is_var));
    if (cold_parse_opaque_seq_type(stripped)) {
        return is_var ? SLOT_SEQ_OPAQUE_REF : SLOT_SEQ_OPAQUE;
    }
    if (symbols) return cold_slot_kind_from_type_with_symbols(symbols, type);
    return cold_param_kind_from_type(type);
}

int32_t cold_parser_slot_size_from_type(Symbols *symbols, Span type, int32_t kind) {
    bool is_var = false;
    Span stripped = span_trim(cold_type_strip_var(type, &is_var));
    if (cold_parse_opaque_seq_type(stripped)) {
        if (kind == SLOT_SEQ_OPAQUE_REF) return 8;
        if (kind == SLOT_SEQ_OPAQUE) return 16;
    }
    return cold_slot_size_from_type_with_symbols(symbols, type, kind);
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

static bool cold_type_matches_generic_names(Span type, Span *generic_names,
                                            int32_t generic_count,
                                            bool *is_var_out) {
    bool is_var = false;
    Span stripped = span_trim(cold_type_strip_var(type, &is_var));
    if (is_var_out) *is_var_out = is_var;
    for (int32_t gi = 0; gi < generic_count; gi++) {
        if (span_same(stripped, generic_names[gi])) return true;
    }
    return false;
}

int32_t parse_param_specs_with_generics(Symbols *symbols, Span params,
                                               Span *generic_names,
                                               int32_t generic_count,
                                               Span *names,
                                               int32_t *kinds,
                                               int32_t *sizes, Span *types,
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
            bool generic_is_var = false;
            if (cold_type_matches_generic_names(type, generic_names,
                                                generic_count, &generic_is_var)) {
                kind = generic_is_var ? SLOT_I32_REF : SLOT_I32;
            } else {
                kind = cold_param_kind_from_type(type);
                if (symbols) kind = cold_parser_slot_kind_from_type(symbols, type);
            }
        }
        names[count] = name;
        if (kinds) kinds[count] = kind;
        if (sizes) sizes[count] = cold_type_matches_generic_names(type, generic_names,
                                                                  generic_count, 0)
            ? (kind == SLOT_I32_REF ? 8 : 4)
            : cold_param_size_from_type(symbols, type, kind);
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

int32_t parse_param_specs(Symbols *symbols, Span params, Span *names,
                                 int32_t *kinds, int32_t *sizes, Span *types,
                                 int32_t cap) {
    return parse_param_specs_with_generics(symbols, params, 0, 0, names,
                                           kinds, sizes, types, cap);
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
           span_eq(type, "int8") ||
           span_eq(type, "char") || span_eq(type, "uint32") ||
           span_eq(type, "uint64") || span_eq(type, "int64") ||
           span_eq(type, "void") || span_eq(type, "str") ||
           span_eq(type, "cstring") || span_eq(type, "ptr") ||
           cold_type_has_pointer_suffix(type) ||
           span_eq(type, "Bytes") ||
           cold_span_starts_with(type, "fn(") ||
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

ObjectDef *parser_resolve_object_constructor(Parser *parser, Span name,
                                             Span *slot_type_out) {
    ObjectDef *object = parser_resolve_object(parser, name);
    if (object) {
        if (slot_type_out) *slot_type_out = name;
        return object;
    }
    Span stripped = cold_span_strip_generic_suffix(name);
    if (!span_same(stripped, name)) {
        object = parser_resolve_object(parser, stripped);
        if (object && slot_type_out) *slot_type_out = name;
        return object;
    }
    if (slot_type_out) *slot_type_out = name;
    return 0;
}

bool parser_next_is_object_constructor(Parser *parser);
int32_t parse_object_constructor_typed(Parser *parser, BodyIR *body,
                                       Locals *locals, ObjectDef *object,
                                       Span slot_type);

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
    ConstDef *constant = symbols_find_const(parser->symbols, name);
    if (constant) return constant;
    if (!parser->import_mode && name.len > 0 && cold_span_find_char(name, '.') < 0) {
        for (int32_t si = 0; si < parser->import_source_count; si++) {
            Span alias = parser->import_sources[si].alias;
            if (alias.len <= 0) continue;
            Span scoped = cold_arena_join3(parser->arena, alias, ".", name);
            constant = symbols_find_const(parser->symbols, scoped);
            if (constant) return constant;
        }
    }
    return 0;
}

GlobalDef *parser_find_global(Parser *parser, Span name) {
    if (parser_can_scope_bare_name(parser, name)) {
        Span scoped = parser_scoped_bare_name(parser, name);
        GlobalDef *global = symbols_find_global(parser->symbols, scoped);
        if (global) return global;
    }
    GlobalDef *global = symbols_find_global(parser->symbols, name);
    if (global) return global;
    if (!parser->import_mode && name.len > 0 && cold_span_find_char(name, '.') < 0) {
        for (int32_t si = 0; si < parser->import_source_count; si++) {
            Span alias = parser->import_sources[si].alias;
            if (alias.len <= 0) continue;
            Span scoped = cold_arena_join3(parser->arena, alias, ".", name);
            global = symbols_find_global(parser->symbols, scoped);
            if (global) return global;
        }
    }
    return 0;
}

int32_t cold_global_ref_kind(int32_t kind) {
    if (kind == SLOT_I32) return SLOT_I32_REF;
    if (kind == SLOT_I64) return SLOT_I64_REF;
    if (kind == SLOT_STR) return SLOT_STR_REF;
    if (kind == SLOT_SEQ_I32) return SLOT_SEQ_I32_REF;
    if (kind == SLOT_SEQ_STR) return SLOT_SEQ_STR_REF;
    if (kind == SLOT_SEQ_OPAQUE) return SLOT_SEQ_OPAQUE_REF;
    if (kind == SLOT_OBJECT || kind == SLOT_VARIANT || kind == SLOT_ARRAY_I32) return SLOT_OBJECT_REF;
    return SLOT_OPAQUE_REF;
}

Local *locals_add_global_shadow(Parser *parser, BodyIR *body, Locals *locals,
                                Span local_name, GlobalDef *global) {
    Local *existing = locals_find(locals, local_name);
    if (existing) return existing;
    int32_t value_slot = body_slot(body, global->kind, global->size);
    body_slot_set_type(body, value_slot, global->type_name);
    int32_t ref_kind = cold_global_ref_kind(global->kind);
    int32_t ref_slot = body_slot(body, ref_kind, 8);
    body_slot_set_type(body, ref_slot, global->type_name);
    body_op3(body, BODY_OP_FIELD_REF, ref_slot, value_slot, 0, 0);
    locals_add(locals, local_name, ref_slot, ref_kind);
    (void)parser;
    return locals_find(locals, local_name);
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
                                         int32_t *sizes, Span *types,
                                         int32_t cap) {
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
            /* Manual type span: skip balanced brackets (child parser peek limitation) */
            int32_t ts = parser.pos;
            while (parser.pos < parser.source.len) {
                if (parser.source.ptr[parser.pos] == '[') {
                    int32_t d = 1;
                    parser.pos++;
                    while (parser.pos < parser.source.len && d > 0) {
                        if (parser.source.ptr[parser.pos] == '[') d++;
                        else if (parser.source.ptr[parser.pos] == ']') d--;
                        parser.pos++;
                    }
                    continue;
                }
                if (parser.source.ptr[parser.pos] == ',' || parser.source.ptr[parser.pos] == ')')
                    break;
                parser.pos++;
            }
            Span raw_type = span_trim(span_sub(parser.source, ts, parser.pos));
            type = cold_qualify_import_type(symbols->arena, alias, raw_type);
        }
        int32_t kind = cold_parser_slot_kind_from_type(symbols, type);
        names[count] = name;
        kinds[count] = kind;
        sizes[count] = cold_param_size_from_type(symbols, type, kind);
        if (types) types[count] = type;
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
void cold_collect_import_module_consts(Symbols *symbols, Span alias, Span source);

void cold_collect_import_module_types(Symbols *symbols, Span alias, Span source) {
    ColdErrorRecoveryEnabled = true;
    if (setjmp(ColdErrorJumpBuf) != 0) { ColdErrorRecoveryEnabled = false; return; }
    Symbols *local_symbols = symbols_new(symbols->arena);
    cold_collect_import_module_consts(local_symbols, (Span){0}, source);
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
        if (src->alias_type.len <= 0) continue;
        Span qualified_type = cold_arena_join3(symbols->arena, alias, ".", src->name);
        TypeDef *dst = symbols_find_type(symbols, qualified_type);
        if (!dst) dst = symbols_add_type(symbols, qualified_type, 0);
        if (dst->variant_count != 0 || dst->is_enum) die("imported type alias conflicts with concrete type");
        dst->generic_count = src->generic_count;
        for (int32_t gi = 0; gi < src->generic_count; gi++) {
            dst->generic_names[gi] = cold_arena_span_copy(symbols->arena, src->generic_names[gi]);
        }
        Span qualified_alias = cold_qualify_import_type(symbols->arena, alias, src->alias_type);
        symbols_set_type_alias(dst, qualified_alias);
    }
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
            int32_t fk = generic_field ? SLOT_I32 : cold_parser_slot_kind_from_type(symbols, field_type);
            dst->fields[fi].name = cold_arena_span_copy(symbols->arena, src->fields[fi].name);
            dst->fields[fi].type_name = field_type;
            dst->fields[fi].kind = fk;
            dst->fields[fi].size = generic_field ? 4
                                                 : cold_parser_slot_size_from_type(symbols, field_type, fk);
            dst->fields[fi].array_len = 0;
            if (fk == SLOT_ARRAY_I32 && !cold_parse_fixed_array_type(symbols, field_type, &dst->fields[fi].array_len)) {
                dst->fields[fi].array_len = 4; /* default */
            }
        }
        dst->is_ref = src->is_ref;
        object_finalize_fields(dst);
    }
    ColdErrorRecoveryEnabled = false;
}

void cold_register_import_enum(Symbols *symbols, Span alias, Span type_name,
                                      Span *variant_names, int32_t variant_count) {
    if (variant_count <= 0) return;
    Span qualified_type = cold_arena_join3(symbols->arena, alias, ".", type_name);
    TypeDef *type = symbols_find_type(symbols, qualified_type);
    if (!type) {
        type = symbols_add_type(symbols, qualified_type, variant_count);
    }
    if (type->variant_count != variant_count) {
        /* Already registered with a different count — keep the first registration. */
        return;
    }
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
        if (!span_eq(trimmed, "const")) {
            /* Single-line "const Name: Type = Value" format */
            if (cold_span_starts_with(trimmed, "const ") ||
                cold_span_starts_with(trimmed, "const\t")) {
                Span rest = span_trim(span_sub(trimmed, 5, trimmed.len));
                int32_t ceq = cold_span_find_char(rest, '=');
                if (ceq > 0) {
                    Span cname = span_trim(span_sub(rest, 0, ceq));
                    int32_t colon = cold_span_find_char(cname, ':');
                    if (colon > 0) cname = span_trim(span_sub(cname, 0, colon));
                    Span cval = span_trim(span_sub(rest, ceq + 1, rest.len));
                    if (cname.len > 0) {
                        Span aliased = alias.len > 0
                                           ? cold_arena_join3(symbols->arena, alias, ".", cname)
                                           : cold_arena_span_copy(symbols->arena, cname);
                        if (cval.len >= 2 && cval.ptr[0] == '"' && cval.ptr[cval.len - 1] == '"') {
                            Span inner = span_sub(cval, 1, cval.len - 1);
                            symbols_add_str_const(symbols, aliased, cold_decode_string_content(symbols->arena, inner, false));
                        } else if (span_is_i32(cval)) {
                            symbols_add_const(symbols, aliased, span_i32(cval));
                        }
                    }
                }
            }
            continue;
        }
        /* found const block: scan indented name=value lines */
        while (pos < source.len) {
            int32_t cs = pos;
            while (pos < source.len && source.ptr[pos] != '\n') pos++;
            int32_t ce = pos;
            if (pos < source.len) pos++;
            Span cline = span_sub(source, cs, ce);
            Span ct = span_trim(cline);
            if (ct.len <= 0) continue;
            if (cold_line_top_level(cline)) { pos = cs; break; }
            int32_t ceq = cold_span_find_char(ct, '=');
            if (ceq <= 0) continue;
            Span cname = span_trim(span_sub(ct, 0, ceq));
            /* Strip type annotation (e.g. "Name: Type = Value" → "Name") */
            int32_t colon = cold_span_find_char(cname, ':');
            if (colon > 0) cname = span_trim(span_sub(cname, 0, colon));
            Span cval = span_trim(span_sub(ct, ceq + 1, ct.len));
            if (cname.len <= 0) continue;
            Span aliased = alias.len > 0
                               ? cold_arena_join3(symbols->arena, alias, ".", cname)
                               : cold_arena_span_copy(symbols->arena, cname);
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
    cold_collect_import_module_consts(symbols, alias, source);
    cold_collect_import_module_types(symbols, alias, source);
    cold_collect_import_module_enum_blocks(symbols, alias, source);
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
            int32_t kind = cold_parser_slot_kind_from_type(symbols, field->type_name);
            if (kind == SLOT_VARIANT && !symbols_resolve_type(symbols, field->type_name)) kind = SLOT_I32;
            field->kind = kind;
            field->size = cold_parser_slot_size_from_type(symbols, field->type_name, kind);
            field->array_len = 0;
            if (kind == SLOT_ARRAY_I32 &&
                !cold_parse_fixed_array_type(symbols, field->type_name, &field->array_len)) {
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
        if (!cold_span_starts_with(trimmed, "fn ") &&
            !cold_span_starts_with(trimmed, "importc fn ")) continue;
        ColdFunctionSymbol symbol;
        if (!cold_parse_function_symbol_at(source, start, line_no, &symbol)) {
            if (trimmed.len > 3 && trimmed.ptr[3] == '`') continue;
            fprintf(stderr, "cheng_cold: cannot parse imported signature path=%s line=%d\n",
                    path, line_no);
            continue;
        }
        Span names[COLD_MAX_I32_PARAMS];
        Span types[COLD_MAX_I32_PARAMS];
        int32_t kinds[COLD_MAX_I32_PARAMS];
        int32_t sizes[COLD_MAX_I32_PARAMS];
        int32_t arity = cold_imported_param_specs(symbols, alias, symbol.params,
                                                  names, kinds, sizes, types,
                                                  COLD_MAX_I32_PARAMS);
        Span qualified_name = cold_arena_join3(symbols->arena, alias, ".", symbol.name);
        Span qualified_ret = cold_qualify_import_type(symbols->arena, alias,
                                                      symbol.return_type);
        int32_t fi = symbols_add_fn(symbols, qualified_name, arity, kinds, sizes, qualified_ret);
        symbols_set_fn_param_types(symbols, fi, types, arity);
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
    Parser type_parser = {source, 0, symbols ? symbols->arena : 0, symbols};
    while (type_parser.pos < source.len) {
        parser_ws(&type_parser);
        if (type_parser.pos >= source.len) break;
        Span next = parser_peek(&type_parser);
        if (span_eq(next, "type")) {
            parse_type(&type_parser);
        } else {
            parser_line(&type_parser);
        }
    }

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
        if (!cold_span_starts_with(trimmed, "fn ") &&
            !cold_span_starts_with(trimmed, "importc fn ")) continue;
        ColdFunctionSymbol symbol;
        if (!cold_parse_function_symbol_at(source, start, line_no, &symbol)) {
            continue; /* skip unparseable signature */
        }
        Span param_names[COLD_MAX_I32_PARAMS];
        Span param_types[COLD_MAX_I32_PARAMS];
        int32_t param_kinds[COLD_MAX_I32_PARAMS];
        int32_t param_sizes[COLD_MAX_I32_PARAMS];
        int32_t arity = parse_param_specs_with_generics(symbols, symbol.params,
                                                        symbol.generic_names,
                                                        symbol.generic_count,
                                                        param_names, param_kinds,
                                                        param_sizes, param_types,
                                                        COLD_MAX_I32_PARAMS);
        int32_t fi = symbols_add_fn(symbols, symbol.name, arity, param_kinds, param_sizes, symbol.return_type);
        symbols_set_fn_param_types(symbols, fi, param_types, arity);
        if (symbol.import_name.len > 0) symbols_set_fn_link_name(symbols, fi, symbol.import_name);
        if (symbol.export_name.len > 0) symbols_set_fn_link_name(symbols, fi, symbol.export_name);
        if (fi >= 0 && fi < symbols->function_cap) {
            FnDef *fn = &symbols->functions[fi];
            if (symbol.import_name.len > 0 && !symbol.has_body)
                fn->is_external = true;
            fn->generic_count = symbol.generic_count;
            for (int32_t gi = 0; gi < symbol.generic_count && gi < 4; gi++)
                fn->generic_names[gi] = symbol.generic_names[gi];
            /* Re-parse params to find default values (= <int32> after type) */
            Parser pp = {symbol.params, 0, 0, 0};
            int32_t pi = 0;
            while (pp.pos < pp.source.len && pi < arity) {
                (void)parser_token(&pp); /* param name */
                if (span_eq(parser_peek(&pp), ":")) {
                    (void)parser_token(&pp);
                    (void)parser_take_type_span(&pp); /* skip type */
                }
                if (span_eq(parser_peek(&pp), "=")) {
                    (void)parser_token(&pp);
                    Span val = parser_token(&pp);
                    if (span_is_i32(val)) {
                        fn->param_has_default[pi] = true;
                        fn->param_default_value[pi] = span_i32(val);
                    }
                }
                while (pp.pos < pp.source.len &&
                       !span_eq(parser_peek(&pp), ",") &&
                       !span_eq(parser_peek(&pp), ")")) (void)parser_token(&pp);
                if (span_eq(parser_peek(&pp), ",")) (void)parser_token(&pp);
                pi++;
            }
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
    Span first_token = parser_token(&line);
    bool saw_type_keyword = span_eq(first_token, "type");
    Span type_name;
    if (saw_type_keyword) {
        type_name = parser_token(&line);
    } else {
        type_name = first_token;
        if (!cold_span_is_simple_ident(type_name)) die("expected type declaration");
        int32_t saved = line.pos;
        parser_ws(&line);
        if (!span_eq(parser_peek(&line), "=")) { line.pos = saved; die("expected type"); }
        line.pos = saved;
    }
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
            Span qn = {(const uint8_t *)qn_ptr, qn_len};
            symbols_add_const(parser->symbols, qn, vi);
        }
        parser->pos = line_end;
        return;
    }
    if (cold_span_starts_with(rhs_check, "fn(") ||
        cold_type_is_builtin_surface(rhs_check)) {
        TypeDef *type = symbols_add_type(parser->symbols, type_name, 0);
        type->generic_count = generic_count;
        for (int32_t gi = 0; gi < generic_count; gi++) type->generic_names[gi] = generic_names[gi];
        symbols_set_type_alias(type, parser_scope_type(parser, rhs_check));
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
                    int32_t fk = cold_parser_slot_kind_from_type(parser->symbols, rft[fi]);
                    if (fk == SLOT_VARIANT && !symbols_resolve_type(parser->symbols, rft[fi])) fk = SLOT_I32;
                    robj->fields[fi].kind = fk;
                    robj->fields[fi].size = cold_parser_slot_size_from_type(parser->symbols, rft[fi], fk);
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
        TypeDef *type = symbols_add_type(parser->symbols, type_name, 0);
        type->generic_count = generic_count;
        for (int32_t gi = 0; gi < generic_count; gi++) type->generic_names[gi] = generic_names[gi];
        symbols_set_type_alias(type, parser_scope_type(parser, rhs_check));
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
        if (fields_span.len <= 0 && implicit_object) {
            fields_span = rhs_check;
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
                                       : cold_parser_slot_kind_from_type(parser->symbols, field_types[fi]);
            if (fk == SLOT_VARIANT && !symbols_resolve_type(parser->symbols, field_types[fi])) {
                fk = SLOT_I32;
            }
            object->fields[fi].kind = fk;
            object->fields[fi].size = generic_field ? 4
                                                    : cold_parser_slot_size_from_type(parser->symbols, field_types[fi], fk);
            object->fields[fi].type_name = field_types[fi];
            object->fields[fi].array_len = 0;
            if (fk == SLOT_ARRAY_I32 && !cold_parse_fixed_array_type(parser->symbols, field_types[fi], &object->fields[fi].array_len)) {
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
                    int32_t fk = cold_parser_slot_kind_from_type(parser->symbols, field_type);
                    field_kinds[field_count] = fk;
                    field_sizes[field_count] = cold_parser_slot_size_from_type(parser->symbols, field_type, fk);
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
        Span qn = {(const uint8_t *)qn_ptr, qn_len};
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
           span_eq(type, "int8") ||
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

bool cold_fn_is_rawbytes_bytes_alloc(FnDef *fn, Span lookup_name) {
    if (span_eq(lookup_name, "rawbytes.BytesAlloc") ||
        span_eq(lookup_name, "rawbytes.bytesAlloc")) return true;
    if (!fn) return false;
    return span_eq(fn->name, "rawbytes.BytesAlloc") ||
           span_eq(fn->name, "rawbytes.bytesAlloc");
}

int32_t cold_emit_bytes_alloc_call(Parser *parser, BodyIR *body, FnDef *fn,
                                          int32_t arg_start, int32_t arg_count,
                                          int32_t *kind_out) {
    if (arg_count != 1) die("BytesAlloc intrinsic arity mismatch");
    int32_t len = body->call_arg_slot[arg_start];
    if (body->slot_kind[len] != SLOT_I32) die("BytesAlloc length must be int32");
    int32_t ret_kind = cold_return_kind_from_span(parser->symbols, fn->ret);
    if (ret_kind != SLOT_OBJECT) die("BytesAlloc must return Bytes object");
    int32_t slot = body_slot(body, SLOT_OBJECT,
                             cold_return_slot_size(parser->symbols, fn->ret, ret_kind));
    body_slot_set_type(body, slot, fn->ret);
    body_op(body, BODY_OP_BYTES_ALLOC, slot, len, 0);
    if (kind_out) *kind_out = SLOT_OBJECT;
    return slot;
}

static bool cold_trailing_params_have_defaults(FnDef *fn, int32_t arg_count) {
    for (int32_t pi = arg_count; pi < fn->arity; pi++) {
        if (!fn->param_has_default[pi]) return false;
    }
    return true;
}

static void cold_die_call_arg_mismatch(BodyIR *body, FnDef *fn,
                                       int32_t arg_start, int32_t arg_count) {
    fprintf(stderr,
            "cheng_cold: call arg mismatch callee=%.*s expected=%d actual=%d\n",
            (int)fn->name.len, fn->name.ptr, fn->arity, arg_count);
    int32_t n = arg_count < fn->arity ? arg_count : fn->arity;
    for (int32_t i = 0; i < n; i++) {
        int32_t arg_slot = body->call_arg_slot[arg_start + i];
        fprintf(stderr,
                "cheng_cold:   arg[%d] param_kind=%d param_size=%d actual_kind=%d actual_size=%d actual_type=%.*s\n",
                i,
                fn->param_kind[i], fn->param_size[i],
                body->slot_kind[arg_slot], body->slot_size[arg_slot],
                (int)body->slot_type[arg_slot].len, body->slot_type[arg_slot].ptr);
    }
    die("cold call arg mismatch");
}

static void cold_print_same_name_call_candidates(Symbols *symbols, BodyIR *body,
                                                 Span name, int32_t arg_start,
                                                 int32_t arg_count) {
    if (!symbols || !body) return;
    for (int32_t i = 0; i < arg_count; i++) {
        int32_t arg_slot = body->call_arg_slot[arg_start + i];
        fprintf(stderr,
                "cheng_cold:   actual[%d] kind=%d size=%d type=%.*s\n",
                i, body->slot_kind[arg_slot], body->slot_size[arg_slot],
                (int)body->slot_type[arg_slot].len, body->slot_type[arg_slot].ptr);
    }
    int32_t printed = 0;
    for (int32_t fi = 0; fi < symbols->function_count; fi++) {
        FnDef *fn = &symbols->functions[fi];
        if (!span_same(fn->name, name)) continue;
        fprintf(stderr,
                "cheng_cold:   candidate[%d] arity=%d ret=%.*s",
                fi, fn->arity, (int)fn->ret.len, fn->ret.ptr);
        for (int32_t pi = 0; pi < fn->arity; pi++) {
            fprintf(stderr, " param[%d]=kind:%d size:%d type:%.*s",
                    pi, fn->param_kind[pi], fn->param_size[pi],
                    (int)fn->param_type[pi].len, fn->param_type[pi].ptr);
        }
        fprintf(stderr, "\n");
        printed++;
        if (printed >= 8) return;
    }
    if (printed == 0) {
        fprintf(stderr, "cheng_cold:   no same-name candidates\n");
    }
}

static bool cold_try_field_function_pointer_call(Parser *parser, BodyIR *body,
                                                 Locals *locals, Span name,
                                                 int32_t arg_start,
                                                 int32_t arg_count,
                                                 int32_t *slot_out,
                                                 int32_t *kind_out) {
    int32_t dot = cold_span_find_char(name, '.');
    if (dot <= 0 || dot + 1 >= name.len) return false;
    Span base_name = span_sub(name, 0, dot);
    Span field_name = span_sub(name, dot + 1, name.len);
    if (cold_span_find_char(field_name, '.') >= 0) return false;
    Local *base = locals_find(locals, base_name);
    if (!base) return false;
    if (base->kind != SLOT_PTR && base->kind != SLOT_OBJECT_REF &&
        base->kind != SLOT_OBJECT && base->kind != SLOT_OPAQUE &&
        base->kind != SLOT_OPAQUE_REF) {
        return false;
    }
    Span type_name = cold_type_strip_var(body->slot_type[base->slot], 0);
    ObjectDef *object = symbols_resolve_object(parser->symbols, type_name);
    if (!object) return false;
    ObjectField *field = object_find_field(object, field_name);
    if (!field || field->kind != SLOT_PTR || field->size != 8) return false;
    int32_t fn_slot = body_slot(body, SLOT_PTR, 8);
    body_slot_set_type(body, fn_slot, field->type_name);
    body_op3(body, BODY_OP_PAYLOAD_LOAD, fn_slot, base->slot, field->offset, field->size);
    int32_t ret_slot = body_slot(body, SLOT_I32, 4);
    body_op3(body, BODY_OP_CALL_PTR, ret_slot, fn_slot, arg_start, arg_count);
    if (kind_out) *kind_out = SLOT_I32;
    if (slot_out) *slot_out = ret_slot;
    return true;
}

static int32_t cold_param_sequence_value_kind(int32_t param_kind) {
    if (param_kind == SLOT_SEQ_I32 || param_kind == SLOT_SEQ_I32_REF) return SLOT_SEQ_I32;
    if (param_kind == SLOT_SEQ_STR || param_kind == SLOT_SEQ_STR_REF) return SLOT_SEQ_STR;
    if (param_kind == SLOT_SEQ_OPAQUE || param_kind == SLOT_SEQ_OPAQUE_REF) return SLOT_SEQ_OPAQUE;
    return 0;
}

static bool cold_slot_is_empty_sequence_literal(BodyIR *body, int32_t slot) {
    if (slot < 0 || slot >= body->slot_count) return false;
    if (body->slot_size[slot] != 16) return false;
    int32_t kind = body->slot_kind[slot];
    if (kind != SLOT_SEQ_I32 && kind != SLOT_SEQ_STR && kind != SLOT_SEQ_OPAQUE) return false;
    for (int32_t oi = body->op_count - 1; oi >= 0; oi--) {
        if (body->op_dst[oi] != slot) continue;
        return body->op_kind[oi] == BODY_OP_MAKE_COMPOSITE &&
               body->op_b[oi] == -1 &&
               body->op_c[oi] == 0;
    }
    return false;
}

static bool cold_empty_sequence_arg_matches_param(BodyIR *body, FnDef *fn,
                                                  int32_t arg_start, int32_t index) {
    int32_t expected = cold_param_sequence_value_kind(fn->param_kind[index]);
    if (expected == 0) return false;
    int32_t arg_slot = body->call_arg_slot[arg_start + index];
    return cold_slot_is_empty_sequence_literal(body, arg_slot);
}

static bool cold_exact_ref_arg_matches_ptr_param(BodyIR *body, FnDef *fn,
                                                 int32_t arg_start, int32_t index) {
    int32_t arg_slot = body->call_arg_slot[arg_start + index];
    int32_t arg_kind = body->slot_kind[arg_slot];
    if (fn->param_kind[index] != SLOT_PTR) return false;
    if (arg_kind != SLOT_OPAQUE_REF && arg_kind != SLOT_OBJECT_REF) return false;
    Span param_type = span_trim(fn->param_type[index]);
    Span arg_type = span_trim(body->slot_type[arg_slot]);
    return param_type.len > 0 && span_same(param_type, arg_type);
}

static void cold_apply_contextual_empty_sequence_args(BodyIR *body, Symbols *symbols, FnDef *fn,
                                                      int32_t arg_start,
                                                      int32_t arg_count) {
    int32_t n = arg_count < fn->arity ? arg_count : fn->arity;
    for (int32_t i = 0; i < n; i++) {
        int32_t expected = cold_param_sequence_value_kind(fn->param_kind[i]);
        if (expected == 0) continue;
        int32_t arg_slot = body->call_arg_slot[arg_start + i];
        if (!cold_slot_is_empty_sequence_literal(body, arg_slot)) continue;
        body->slot_kind[arg_slot] = expected;
        body->slot_size[arg_slot] = 16;
        body->slot_aux[arg_slot] = 0;
        if (expected == SLOT_SEQ_I32) {
            body_slot_set_type(body, arg_slot, cold_cstr_span("int32[]"));
        } else if (expected == SLOT_SEQ_STR) {
            body_slot_set_type(body, arg_slot, cold_cstr_span("str[]"));
        } else if (expected == SLOT_SEQ_OPAQUE) {
            body_slot_set_seq_opaque_type(body, symbols, arg_slot, fn->param_type[i]);
        } else {
            body_slot_set_type(body, arg_slot, cold_cstr_span("opaque"));
        }
    }
}

bool cold_validate_call_args(BodyIR *body, FnDef *fn, int32_t arg_start, int32_t arg_count, bool import_mode) {
    (void)import_mode;
    if (arg_count > fn->arity) { return false; }
    if (arg_count < fn->arity && !cold_trailing_params_have_defaults(fn, arg_count)) { return false; }
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
            if (arg_kind != SLOT_OBJECT && arg_kind != SLOT_OBJECT_REF && arg_kind != SLOT_SEQ_OPAQUE && arg_kind != SLOT_SEQ_OPAQUE_REF) { return false; }
        } else if (fn->param_kind[i] == SLOT_VARIANT) {
            if (arg_kind != SLOT_VARIANT && arg_kind != SLOT_I32 && arg_kind != SLOT_OBJECT && arg_kind != SLOT_PTR && arg_kind != SLOT_OPAQUE) { return false; }
        } else if (fn->param_kind[i] == SLOT_SEQ_I32 && arg_kind == SLOT_SEQ_I32_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_SEQ_STR && arg_kind == SLOT_SEQ_STR_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_SEQ_OPAQUE && arg_kind == SLOT_SEQ_OPAQUE_REF) {
            continue;
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
                    arg_kind == SLOT_SEQ_OPAQUE_REF || arg_kind == SLOT_PTR ||
                    arg_kind == SLOT_STR || arg_kind == SLOT_STR_REF ||
                    arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF)) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OPAQUE_REF &&
                   (arg_kind == SLOT_I32 || arg_kind == SLOT_I64 ||
                    arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                    arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                    arg_kind == SLOT_PTR ||
                    arg_kind == SLOT_STR || arg_kind == SLOT_STR_REF ||
                    arg_kind == SLOT_SEQ_OPAQUE || arg_kind == SLOT_SEQ_OPAQUE_REF ||
                    arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF ||
                    arg_kind == SLOT_VARIANT)) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OBJECT && arg_kind == SLOT_OBJECT_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OBJECT && arg_kind == SLOT_OBJECT) {
            continue;
        } else if (cold_exact_ref_arg_matches_ptr_param(body, fn, arg_start, i)) {
            continue;
        } else if (cold_empty_sequence_arg_matches_param(body, fn, arg_start, i)) {
            continue;
        } else if (arg_kind != fn->param_kind[i] && arg_kind != 0 && fn->param_kind[i] != 0) {
            { return false; }
        }
        int32_t arg_size = body->slot_size[arg_slot];
        int32_t param_size = fn->param_size[i] > 0 ? fn->param_size[i] : arg_size;
        if ((arg_kind == SLOT_VARIANT || arg_kind == SLOT_OBJECT || arg_kind == SLOT_ARRAY_I32 ||
             arg_kind == SLOT_SEQ_OPAQUE) &&
            fn->param_kind[i] != SLOT_I32_REF &&
            fn->param_kind[i] != SLOT_I32 &&
            fn->param_kind[i] != SLOT_OBJECT_REF &&
            fn->param_kind[i] != SLOT_OBJECT &&
            fn->param_kind[i] != SLOT_VARIANT &&
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
            if (arg_kind != SLOT_OBJECT && arg_kind != SLOT_OBJECT_REF &&
                arg_kind != SLOT_I32 && arg_kind != SLOT_I64 &&
                arg_kind != SLOT_I32_REF && arg_kind != SLOT_I64_REF &&
                arg_kind != SLOT_STR_REF && arg_kind != SLOT_SEQ_I32_REF &&
                arg_kind != SLOT_SEQ_STR_REF && arg_kind != SLOT_SEQ_OPAQUE_REF &&
                arg_kind != SLOT_OPAQUE_REF) return false;
        } else if (fn->param_kind[i] == SLOT_SEQ_I32 && arg_kind == SLOT_SEQ_I32_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_SEQ_STR && arg_kind == SLOT_SEQ_STR_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_SEQ_OPAQUE && arg_kind == SLOT_SEQ_OPAQUE_REF) {
            continue;
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
                    arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                    arg_kind == SLOT_VARIANT || arg_kind == SLOT_SEQ_OPAQUE ||
                    arg_kind == SLOT_SEQ_OPAQUE_REF || arg_kind == SLOT_PTR ||
                    arg_kind == SLOT_STR || arg_kind == SLOT_STR_REF ||
                    arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF)) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OPAQUE_REF &&
                   (arg_kind == SLOT_I32 || arg_kind == SLOT_I64 ||
                    arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                    arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                    arg_kind == SLOT_VARIANT || arg_kind == SLOT_PTR ||
                    arg_kind == SLOT_STR || arg_kind == SLOT_STR_REF ||
                    arg_kind == SLOT_SEQ_OPAQUE || arg_kind == SLOT_SEQ_OPAQUE_REF ||
                    arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF)) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OBJECT && arg_kind == SLOT_OBJECT_REF) {
            continue;
        } else if (fn->param_kind[i] == SLOT_OBJECT && arg_kind == SLOT_OBJECT) {
            continue;
        } else if (cold_exact_ref_arg_matches_ptr_param(body, fn, arg_start, i)) {
            continue;
        } else if (cold_empty_sequence_arg_matches_param(body, fn, arg_start, i)) {
            continue;
        } else if (arg_kind != fn->param_kind[i] && arg_kind != 0 && fn->param_kind[i] != 0) {
            return false;
        }
        int32_t arg_size = body->slot_size[arg_slot];
        int32_t param_size = fn->param_size[i] > 0 ? fn->param_size[i] : arg_size;
        if ((arg_kind == SLOT_VARIANT || arg_kind == SLOT_OBJECT || arg_kind == SLOT_ARRAY_I32 ||
             arg_kind == SLOT_SEQ_OPAQUE) &&
            fn->param_kind[i] != SLOT_I32_REF &&
            fn->param_kind[i] != SLOT_I32 &&
            fn->param_kind[i] != SLOT_OBJECT_REF &&
            fn->param_kind[i] != SLOT_OBJECT &&
            fn->param_kind[i] != SLOT_VARIANT &&
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
    if (found >= 0) return cold_ensure_specialized(symbols, found, body, arg_start, arg_count);
    /* second pass: allow fewer args when trailing params have defaults */
    for (int32_t i = 0; i < symbols->function_count; i++) {
        FnDef *fn = &symbols->functions[i];
        if (fn->arity <= arg_count || !span_same(fn->name, name)) continue;
        if (!cold_trailing_params_have_defaults(fn, arg_count)) continue;
        /* Match only the supplied args (first arg_count params) */
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
                 arg_kind == SLOT_SEQ_OPAQUE || arg_kind == SLOT_SEQ_OPAQUE_REF ||
                 arg_kind == SLOT_VARIANT || arg_kind == SLOT_PTR ||
                 arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF)) continue;
            match = false;
            break;
        }
        if (!match) continue;
        if (found >= 0) continue;
        found = i;
    }
    if (found >= 0) return cold_ensure_specialized(symbols, found, body, arg_start, arg_count);
    /* third pass: OPAQUE-tolerant match (for CSG lowerer with less precise types) */
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
                 arg_kind == SLOT_SEQ_OPAQUE || arg_kind == SLOT_SEQ_OPAQUE_REF ||
                 arg_kind == SLOT_VARIANT || arg_kind == SLOT_PTR ||
                 arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF)) continue;
            if (param_kind == SLOT_OBJECT_REF &&
                (arg_kind == SLOT_I32 || arg_kind == SLOT_I64 ||
                 arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                 arg_kind == SLOT_STR_REF || arg_kind == SLOT_SEQ_I32_REF ||
                 arg_kind == SLOT_SEQ_STR_REF || arg_kind == SLOT_SEQ_OPAQUE_REF ||
                 arg_kind == SLOT_OBJECT_REF || arg_kind == SLOT_OPAQUE_REF)) continue;
            match = false;
            break;
        }
        if (!match) continue;
        if (found >= 0) continue;
        found = i;
    }
    if (found >= 0) return cold_ensure_specialized(symbols, found, body, arg_start, arg_count);
    return found;
}

static int32_t cold_generic_template_param_index(FnDef *fn, Span generic_name) {
    for (int32_t i = 0; i < fn->arity; i++) {
        Span param_type = span_trim(fn->param_type[i]);
        if (span_same(param_type, generic_name)) return i;
        if (param_type.len > 4 && memcmp(param_type.ptr, "var ", 4) == 0 &&
            span_same(span_trim(span_sub(param_type, 4, param_type.len)), generic_name)) return i;
    }
    return -1;
}

static Span cold_specialized_type_for_template_slot(Arena *arena, FnDef *tmpl, FnDef *spec,
                                                     Span type, int32_t slot_kind) {
    type = span_trim(type);
    bool is_var_type = false;
    Span stripped_var = span_trim(cold_type_strip_var(type, &is_var_type));
    if (is_var_type) {
        Span inner = cold_specialized_type_for_template_slot(arena, tmpl, spec,
                                                             stripped_var, slot_kind);
        if (inner.len > 0 && !span_same(inner, stripped_var)) {
            return cold_arena_join3(arena, cold_cstr_span("var "), "", inner);
        }
        return type;
    }
    bool slot_is_ref = (slot_kind == SLOT_I32_REF || slot_kind == SLOT_I64_REF ||
                        slot_kind == SLOT_STR_REF || slot_kind == SLOT_SEQ_I32_REF ||
                        slot_kind == SLOT_SEQ_STR_REF || slot_kind == SLOT_SEQ_OPAQUE_REF ||
                        slot_kind == SLOT_OBJECT_REF || slot_kind == SLOT_OPAQUE_REF);
    for (int32_t gi = 0; gi < tmpl->generic_count; gi++) {
        if (!span_same(type, tmpl->generic_names[gi])) continue;
        for (int32_t pi = 0; pi < tmpl->arity && pi < spec->arity; pi++) {
            bool param_type_is_var = false;
            Span param_stripped = span_trim(cold_type_strip_var(tmpl->param_type[pi], &param_type_is_var));
            if (!span_same(param_stripped, tmpl->generic_names[gi])) continue;
            bool param_is_ref = (tmpl->param_kind[pi] == SLOT_I32_REF ||
                                  tmpl->param_kind[pi] == SLOT_I64_REF ||
                                  tmpl->param_kind[pi] == SLOT_STR_REF ||
                                  tmpl->param_kind[pi] == SLOT_SEQ_I32_REF ||
                                  tmpl->param_kind[pi] == SLOT_SEQ_STR_REF ||
                                  tmpl->param_kind[pi] == SLOT_SEQ_OPAQUE_REF ||
                                  tmpl->param_kind[pi] == SLOT_OBJECT_REF ||
                                  tmpl->param_kind[pi] == SLOT_OPAQUE_REF);
            if ((param_type_is_var || param_is_ref) == slot_is_ref && spec->param_type[pi].len > 0)
                return spec->param_type[pi];
        }
    }
    Span base = {0};
    Span args_span = {0};
    if (cold_type_parse_generic_instance(type, &base, &args_span)) {
        Span args[COLD_MAX_VARIANT_FIELDS];
        Span specialized_args[COLD_MAX_VARIANT_FIELDS];
        int32_t count = cold_split_top_level_commas(args_span, args, COLD_MAX_VARIANT_FIELDS);
        bool changed = false;
        for (int32_t i = 0; i < count; i++) {
            specialized_args[i] = cold_specialized_type_for_template_slot(arena, tmpl, spec,
                                                                          args[i], slot_kind);
            if (!span_same(specialized_args[i], args[i])) changed = true;
        }
        if (changed) return cold_generic_join_type(arena, base, specialized_args, count);
    }
    return type;
}

static int32_t *cold_clone_i32_array(Arena *arena, const int32_t *src, int32_t count) {
    if (!src || count <= 0) return 0;
    int32_t *dst = arena_alloc(arena, (size_t)count * sizeof(int32_t));
    memcpy(dst, src, (size_t)count * sizeof(int32_t));
    return dst;
}

static Span *cold_clone_span_array(Arena *arena, const Span *src, int32_t count) {
    if (!src || count <= 0) return 0;
    Span *dst = arena_alloc(arena, (size_t)count * sizeof(Span));
    memcpy(dst, src, (size_t)count * sizeof(Span));
    return dst;
}

static BodyIR *cold_clone_specialized_body(Parser *parser, int32_t spec_index) {
    if (!parser || !parser->function_bodies) return 0;
    if (spec_index < 0 || spec_index >= parser->symbols->function_count) return 0;
    FnDef *spec = &parser->symbols->functions[spec_index];
    int32_t tmpl_index = spec->template_index;
    if (tmpl_index < 0 || tmpl_index >= parser->symbols->function_count) return 0;
    if (tmpl_index >= parser->function_body_cap || spec_index >= parser->function_body_cap)
        die("generic specialization exceeds function body capacity");
    BodyIR *src = parser->function_bodies[tmpl_index];
    if (!src) return 0;
    FnDef *tmpl = &parser->symbols->functions[tmpl_index];
    BodyIR *dst = body_new(parser->arena);
    dst->op_count = src->op_count;
    dst->op_cap = src->op_count;
    dst->op_kind = cold_clone_i32_array(parser->arena, src->op_kind, src->op_count);
    dst->op_dst = cold_clone_i32_array(parser->arena, src->op_dst, src->op_count);
    dst->op_a = cold_clone_i32_array(parser->arena, src->op_a, src->op_count);
    dst->op_b = cold_clone_i32_array(parser->arena, src->op_b, src->op_count);
    dst->op_c = cold_clone_i32_array(parser->arena, src->op_c, src->op_count);

    dst->term_count = src->term_count;
    dst->term_cap = src->term_count;
    dst->term_kind = cold_clone_i32_array(parser->arena, src->term_kind, src->term_count);
    dst->term_value = cold_clone_i32_array(parser->arena, src->term_value, src->term_count);
    dst->term_case_start = cold_clone_i32_array(parser->arena, src->term_case_start, src->term_count);
    dst->term_case_count = cold_clone_i32_array(parser->arena, src->term_case_count, src->term_count);
    dst->term_true_block = cold_clone_i32_array(parser->arena, src->term_true_block, src->term_count);
    dst->term_false_block = cold_clone_i32_array(parser->arena, src->term_false_block, src->term_count);

    dst->block_count = src->block_count;
    dst->block_cap = src->block_count;
    dst->block_op_start = cold_clone_i32_array(parser->arena, src->block_op_start, src->block_count);
    dst->block_op_count = cold_clone_i32_array(parser->arena, src->block_op_count, src->block_count);
    dst->block_term = cold_clone_i32_array(parser->arena, src->block_term, src->block_count);

    dst->slot_count = src->slot_count;
    dst->slot_cap = src->slot_count;
    dst->slot_kind = cold_clone_i32_array(parser->arena, src->slot_kind, src->slot_count);
    dst->slot_offset = cold_clone_i32_array(parser->arena, src->slot_offset, src->slot_count);
    dst->slot_size = cold_clone_i32_array(parser->arena, src->slot_size, src->slot_count);
    dst->slot_aux = cold_clone_i32_array(parser->arena, src->slot_aux, src->slot_count);
    dst->slot_type = cold_clone_span_array(parser->arena, src->slot_type, src->slot_count);
    dst->slot_no_alias = cold_clone_i32_array(parser->arena, src->slot_no_alias, src->slot_count);
    for (int32_t si = 0; si < dst->slot_count; si++) {
        Span concrete = cold_specialized_type_for_template_slot(parser->arena, tmpl, spec,
                                                                dst->slot_type[si], dst->slot_kind[si]);
        if (concrete.len > 0 && !span_same(concrete, dst->slot_type[si])) {
            int32_t kind = cold_parser_slot_kind_from_type(parser->symbols, concrete);
            dst->slot_kind[si] = kind;
            dst->slot_size[si] = cold_parser_slot_size_from_type(parser->symbols, concrete, kind);
            dst->slot_type[si] = concrete;
        }
    }
    dst->frame_size = src->frame_size;

    dst->switch_count = src->switch_count;
    dst->switch_cap = src->switch_count;
    dst->switch_tag = cold_clone_i32_array(parser->arena, src->switch_tag, src->switch_count);
    dst->switch_block = cold_clone_i32_array(parser->arena, src->switch_block, src->switch_count);
    dst->switch_term = cold_clone_i32_array(parser->arena, src->switch_term, src->switch_count);

    dst->call_arg_count = src->call_arg_count;
    dst->call_arg_cap = src->call_arg_count;
    dst->call_arg_slot = cold_clone_i32_array(parser->arena, src->call_arg_slot, src->call_arg_count);
    dst->call_arg_offset = cold_clone_i32_array(parser->arena, src->call_arg_offset, src->call_arg_count);
    dst->string_literal_count = src->string_literal_count;
    dst->string_literal_cap = src->string_literal_count;
    dst->string_literal = cold_clone_span_array(parser->arena, src->string_literal, src->string_literal_count);

    dst->param_count = src->param_count;
    for (int32_t pi = 0; pi < COLD_MAX_I32_PARAMS; pi++) {
        dst->param_slot[pi] = src->param_slot[pi];
        dst->param_name[pi] = src->param_name[pi];
    }
    dst->return_kind = cold_return_kind_from_span(parser->symbols, spec->ret);
    dst->return_size = cold_return_slot_size(parser->symbols, spec->ret, dst->return_kind);
    dst->return_type = spec->ret;
    dst->debug_name = spec->name;
    if (cold_kind_is_composite(src->return_kind) != cold_kind_is_composite(dst->return_kind))
        die("generic specialization changes composite ABI");
    dst->sret_slot = src->sret_slot;
    dst->has_fallback = src->has_fallback;
    return dst;
}

static void cold_materialize_specialized_body_if_needed(Parser *parser, int32_t fn_index) {
    if (!parser || !parser->function_bodies) return;
    if (fn_index < 0 || fn_index >= parser->symbols->function_count) return;
    if (fn_index < parser->function_body_cap && parser->function_bodies[fn_index]) return;
    FnDef *fn = &parser->symbols->functions[fn_index];
    if (fn->template_index < 0) return;
    BodyIR *body = cold_clone_specialized_body(parser, fn_index);
    if (body && fn_index < parser->function_body_cap) parser->function_bodies[fn_index] = body;
}

int32_t cold_make_error_result_slot(Parser *parser, BodyIR *body,
                                           Span result_type, const char *message);
int32_t cold_make_i32_const_slot(BodyIR *body, int32_t value);
int32_t cold_zero_slot_for_field(BodyIR *body, ObjectField *field);
ObjectField *cold_required_object_field(ObjectDef *object, const char *name);
ObjectField *cold_required_existing_object_field(ObjectDef *object, const char *name);
/* Forward declarations of static functions from cheng_cold.c used by codegen helpers */
static int32_t cold_materialize_self_exec(Parser *parser, BodyIR *body);
static int32_t cold_materialize_direct_emit(Parser *parser, BodyIR *body, int32_t output);
int32_t parse_constructor(Parser *parser, BodyIR *body, Locals *locals,
                                 Variant *variant);

static Span cold_last_qualified_component(Span name) {
    for (int32_t i = name.len - 1; i >= 0; i--) {
        if (name.ptr[i] == '.') return span_sub(name, i + 1, name.len);
    }
    return name;
}

static bool cold_result_constructor_type(Parser *parser, Span call_name,
                                         Span *base_out, Span *result_type_out) {
    Span base = call_name;
    Span arg_type = {0};
    bool has_generic_args = false;
    if (call_name.len > 0 && call_name.ptr[call_name.len - 1] == ']') {
        int32_t bracket = -1;
        for (int32_t i = 0; i < call_name.len; i++) {
            if (call_name.ptr[i] == '[') { bracket = i; break; }
        }
        if (bracket <= 0) die("Result constructor generic syntax invalid");
        base = span_sub(call_name, 0, bracket);
        has_generic_args = true;
    }
    Span short_base = cold_last_qualified_component(base);
    if (!span_eq(short_base, "Ok") && !span_eq(short_base, "Err")) return false;
    if (has_generic_args) {
        int32_t bracket = -1;
        for (int32_t i = 0; i < call_name.len; i++) {
            if (call_name.ptr[i] == '[') { bracket = i; break; }
        }
        Span args_span = span_trim(span_sub(call_name, bracket + 1, call_name.len - 1));
        Span args[COLD_MAX_VARIANT_FIELDS];
        int32_t arg_count = cold_split_top_level_commas(args_span, args, COLD_MAX_VARIANT_FIELDS);
        if (arg_count != 1) die("Result constructor requires exactly one type argument");
        arg_type = parser_scope_type(parser, args[0]);
    }
    if (arg_type.len <= 0) die("Result constructor requires explicit type argument");
    Span result_args[1] = {arg_type};
    if (base_out) *base_out = short_base;
    if (result_type_out) {
        *result_type_out = cold_join_generic_instance(parser->arena,
                                                      cold_cstr_span("Result"),
                                                      result_args, 1);
    }
    return true;
}

static int32_t cold_make_ok_result_slot(Parser *parser, BodyIR *body,
                                        Span result_type, int32_t value_slot,
                                        int32_t *kind_out) {
    ObjectDef *result = symbols_resolve_object(parser->symbols, result_type);
    if (!result) die("Result layout missing for Ok");
    if (!span_same(result->name, result_type)) die("Result layout exact type mismatch for Ok");
    ObjectField *ok = cold_required_existing_object_field(result, "ok");
    ObjectField *value = cold_required_existing_object_field(result, "value");
    ObjectField *err = cold_required_existing_object_field(result, "err");
    if (ok->kind != SLOT_I32 || err->kind != SLOT_OBJECT) die("Result layout kind mismatch for Ok");
    if (body->slot_kind[value_slot] != value->kind) die("Ok value kind mismatch");
    if (body->slot_size[value_slot] != value->size) die("Ok value size mismatch");
    int32_t payload_start = body->call_arg_count;
    body_call_arg_with_offset(body, cold_make_i32_const_slot(body, 1), ok->offset);
    body_call_arg_with_offset(body, value_slot, value->offset);
    body_call_arg_with_offset(body, cold_zero_slot_for_field(body, err), err->offset);
    int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(result));
    body_slot_set_type(body, slot, result->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, 3);
    if (kind_out) *kind_out = SLOT_OBJECT;
    return slot;
}

bool cold_compile_source_to_object(const char *out_path,
                                   const char *src_path,
                                   const char *target,
                                   const char *export_roots_csv,
                                   const char *symbol_visibility);

bool cold_try_ref_object_cast(Parser *parser, BodyIR *body, Span name,
                                     int32_t arg_start, int32_t arg_count,
                                     int32_t *slot_out, int32_t *kind_out) {
    ObjectDef *object = parser_find_object(parser, name);
    if (!object || !object->is_ref) return false;
    if (arg_count != 1) die("ref object cast expects one pointer argument");
    int32_t arg = body->call_arg_slot[arg_start];
    int32_t arg_kind = body->slot_kind[arg];
    if (arg_kind != SLOT_PTR && arg_kind != SLOT_OPAQUE &&
        arg_kind != SLOT_OBJECT_REF && arg_kind != SLOT_OPAQUE_REF) {
        die("ref object cast expects ptr");
    }
    int32_t slot = body_slot(body, SLOT_PTR, 8);
    body_slot_set_type(body, slot, object->name);
    body_op(body, BODY_OP_COPY_I64, slot, arg, 0);
    if (kind_out) *kind_out = SLOT_PTR;
    *slot_out = slot;
    return true;
}

static bool cold_kind_is_pointer_like(int32_t kind) {
    return kind == SLOT_PTR || kind == SLOT_OPAQUE || kind == SLOT_OPAQUE_REF ||
           kind == SLOT_OBJECT_REF || kind == SLOT_I64 || kind == SLOT_I64_REF;
}

static bool cold_try_type_alias_pointer_cast(Parser *parser, BodyIR *body, Span name,
                                             int32_t arg_start, int32_t arg_count,
                                             int32_t *slot_out, int32_t *kind_out) {
    Span type_name = parser_scope_type(parser, name);
    TypeDef *type = parser->symbols ? symbols_find_type(parser->symbols, type_name) : 0;
    ObjectDef *object = parser->symbols ? symbols_find_object(parser->symbols, type_name) : 0;
    if (!type && !object) return false;
    if (arg_count != 1) die("type pointer cast expects one argument");
    int32_t target_kind = cold_parser_slot_kind_from_type(parser->symbols, type_name);
    if (object && object->is_ref) target_kind = SLOT_PTR;
    if (!cold_kind_is_pointer_like(target_kind))
        return false;
    int32_t src = body->call_arg_slot[arg_start];
    int32_t src_kind = body->slot_kind[src];
    if (!cold_kind_is_pointer_like(src_kind)) die("type pointer cast expects pointer value");
    int32_t slot_kind = (target_kind == SLOT_OBJECT_REF || target_kind == SLOT_PTR)
        ? SLOT_PTR
        : target_kind;
    int32_t slot = body_slot(body, slot_kind, 8);
    body_slot_set_type(body, slot, type_name);
    body_op(body, BODY_OP_COPY_I64, slot, src, 0);
    if (kind_out) *kind_out = slot_kind;
    *slot_out = slot;
    return true;
}

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
        Span got = parser_peek(parser);
        int32_t ls = parser->pos;
        while (ls > 0 && parser->source.ptr[ls - 1] != '\n') ls--;
        int32_t le = parser->pos;
        while (le < parser->source.len && parser->source.ptr[le] != '\n') le++;
        fprintf(stderr,
                "cheng_cold: malformed call name=%.*s body=%.*s got=%.*s pos=%d len=%d line=%.*s\n",
                (int)name.len, name.ptr,
                body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
                body && body->debug_name.len > 0 ? body->debug_name.ptr : (const uint8_t *)"",
                (int)got.len, got.ptr,
                parser->pos, parser->source.len,
                le - ls, parser->source.ptr + ls);
        die("malformed function call missing )");
    }
    int32_t arg_start = body->call_arg_count;
    for (int32_t i = 0; i < arg_count; i++) body_call_arg(body, arg_slots[i]);
    int32_t intrinsic_slot = -1;
    if (cold_try_ref_object_cast(parser, body, stripped_name, arg_start, arg_count,
                                 &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_type_alias_pointer_cast(parser, body, stripped_name, arg_start, arg_count,
                                         &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_len_intrinsic(body, stripped_name, arg_start, arg_count,
                                   &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_slice_intrinsic(body, stripped_name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_is_i32_to_str_intrinsic(stripped_name)) {
        if (arg_count != 1) die("int to str intrinsic arity mismatch");
        int32_t arg_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[arg_slot] != SLOT_I32 && body->slot_kind[arg_slot] != SLOT_I64) {
            if (ColdErrorRecoveryEnabled) {
                int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                if (kind_out) *kind_out = SLOT_STR;
                return slot;
            }
            die("int to str intrinsic requires integer arg");
        }
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_op(body, body->slot_kind[arg_slot] == SLOT_I64 ? BODY_OP_I64_TO_STR : BODY_OP_I32_TO_STR, slot, arg_slot, 0);
        if (kind_out) *kind_out = SLOT_STR;
        return slot;
    }
    if (cold_try_parse_int_intrinsic(body, stripped_name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_join_intrinsic(body, stripped_name, arg_start, arg_count,
                                    &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_split_intrinsic(body, stripped_name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_strip_intrinsic(body, stripped_name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_result_intrinsic(parser, body, stripped_name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_os_intrinsic(parser, body, stripped_name, arg_start, arg_count,
                              &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_path_intrinsic(parser, body, stripped_name, arg_start, arg_count,
                                &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_csg_intrinsic(parser, body, stripped_name, arg_start, arg_count,
                               &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_slplan_intrinsic(parser, body, stripped_name, arg_start, arg_count,
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
        if (arg_count != 3) die("atomicCasI32 arity mismatch");
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t expect = body->call_arg_slot[arg_start + 1];
        int32_t desired = body->call_arg_slot[arg_start + 2];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_ATOMIC_CAS_I32, slot, ptr, desired, expect);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    if (cold_try_backend_intrinsic(parser, body, stripped_name, arg_start, arg_count,
                                   &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_assert_intrinsic(body, stripped_name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_bootstrap_intrinsic(body, stripped_name, arg_start, arg_count,
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
    /* Ok[T]/Err[T] constructors must use the concrete Result[T] layout. */
    { Span ctor_base = {0};
      Span result_type = {0};
      if (cold_result_constructor_type(parser, name, &ctor_base, &result_type)) {
          if (arg_count != 1) die("Result constructor arity mismatch");
          if (span_eq(ctor_base, "Err")) {
              int32_t slot = cold_make_error_result_slot(parser, body, result_type, "error");
              if (kind_out) *kind_out = SLOT_OBJECT;
              return slot;
          }
          int32_t val = body->call_arg_slot[arg_start];
          return cold_make_ok_result_slot(parser, body, result_type, val, kind_out);
      }
    }
    /* ptr(x) intrinsic: type conversion to ptr (generic pointer) */
    if (span_eq(stripped_name, "ptr")) {
        if (arg_count != 1) die("ptr() expects 1 arg");
        int32_t src = body->call_arg_slot[arg_start];
        int32_t src_kind = body->slot_kind[src];
        if (src_kind == SLOT_PTR) { if (kind_out) *kind_out = SLOT_PTR; return src; }
        int32_t slot = body_slot(body, SLOT_PTR, 8);
        if (src_kind == SLOT_OPAQUE || src_kind == SLOT_I64) {
            body_op(body, BODY_OP_COPY_I64, slot, src, 0);
        } else {
            body_op(body, BODY_OP_COPY_I32, slot, src, 0);
        }
        if (kind_out) *kind_out = SLOT_PTR;
        return slot;
    }
    Span lookup_name = parser_can_scope_bare_name(parser, stripped_name)
                           ? parser_scoped_bare_name(parser, stripped_name)
                           : stripped_name;
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
        int32_t field_call_slot = -1;
        if (cold_try_field_function_pointer_call(parser, body, locals, name,
                                                 arg_start, arg_count,
                                                 &field_call_slot, kind_out)) {
            return field_call_slot;
        }
    }
    if (fn_index < 0) {
        /* External function, will be registered below */
        { Span ctor_base = {0};
          Span result_type = {0};
          if (cold_result_constructor_type(parser, name, &ctor_base, &result_type)) {
              if (arg_count != 1) die("Result constructor arity mismatch");
              if (span_eq(ctor_base, "Err")) {
                  int32_t slot = cold_make_error_result_slot(parser, body, result_type, "error");
                  if (kind_out) *kind_out = SLOT_OBJECT;
                  return slot;
              }
              int32_t val = body->call_arg_slot[arg_start];
              return cold_make_ok_result_slot(parser, body, result_type, val, kind_out);
          }
          Span base_name = cold_span_strip_generic_suffix(name);
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
        /* Resolve against modules explicitly imported by the current source. */
        if (parser->import_source_count > 0 && parser->import_sources) {
            for (int32_t isi = 0; isi < parser->import_source_count; isi++) {
                Span qualified = cold_arena_join3(parser->arena, parser->import_sources[isi].alias, ".", stripped_name);
                int32_t resolved = symbols_find_fn_for_call(parser->symbols, qualified, body, arg_start, arg_count);
                if (resolved >= 0) {
                    fn_index = resolved;
                    lookup_name = qualified;
                    break;
                }
            }
        }
    }
    if (fn_index < 0) {
        /* Last chance: enum type constructor like PathComponent(n) */
        {
            TypeDef *enum_ty = symbols_find_type(parser->symbols, stripped_name);
            /* In import mode, stripped_name is the bare name (e.g. "PathComponent")
               but the type is registered under the scoped name (e.g. "os.PathComponent").
               Try lookup_name which was scoped by parser_scoped_bare_name. */
            if (!enum_ty && !span_same(stripped_name, lookup_name))
                enum_ty = symbols_find_type(parser->symbols, lookup_name);
            if (!enum_ty) {
                int32_t dot = -1;
                for (int32_t ci = stripped_name.len - 1; ci > 0; ci--)
                    if (stripped_name.ptr[ci] == '.') { dot = ci; break; }
                if (dot > 0) {
                    Span bare = span_sub(stripped_name, dot + 1, stripped_name.len);
                    enum_ty = symbols_find_type(parser->symbols, bare);
                }
            }
            if (enum_ty && enum_ty->is_enum && arg_count == 1) {
                int32_t arg_slot = body->call_arg_slot[arg_start];
                int32_t arg_kind = body->slot_kind[arg_slot];
                if (arg_kind == SLOT_I32 || arg_kind == SLOT_I32_REF) {
                    int32_t slot = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_COPY_I32, slot, arg_slot, 0);
                    if (kind_out) *kind_out = SLOT_I32;
                    return slot;
                }
            }
        }
        if (ColdErrorRecoveryEnabled) {
            int32_t slot = body_slot(body, SLOT_I32, 4);
            if (kind_out) *kind_out = SLOT_I32;
            return slot;
        }
        fprintf(stderr, "cheng_cold: unresolved function call name=%.*s body=%.*s\n",
                (int)lookup_name.len, lookup_name.ptr,
                body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
                body && body->debug_name.len > 0 ? body->debug_name.ptr : (uint8_t *)"");
        cold_print_same_name_call_candidates(parser->symbols, body, lookup_name,
                                             arg_start, arg_count);
        die("unresolved function call");
    }
	    FnDef *fn = &parser->symbols->functions[fn_index];
        cold_materialize_specialized_body_if_needed(parser, fn_index);
	    cold_apply_contextual_empty_sequence_args(body, parser->symbols, fn, arg_start, arg_count);
	    if (!cold_validate_call_args(body, fn, arg_start, arg_count, parser->import_mode)) {
	        cold_die_call_arg_mismatch(body, fn, arg_start, arg_count);
	    }
    if (cold_fn_is_rawbytes_bytes_alloc(fn, lookup_name)) {
        return cold_emit_bytes_alloc_call(parser, body, fn, arg_start, arg_count, kind_out);
    }
    /* Fill in default values for missing trailing params */
    if (arg_count < fn->arity) {
        for (int32_t pi = arg_count; pi < fn->arity; pi++) {
            if (fn->param_has_default[pi]) {
                int32_t def_slot = cold_make_i32_const_slot(body, fn->param_default_value[pi]);
                body_call_arg(body, def_slot);
            }
        }
        arg_count = fn->arity;
    }
    int32_t ret_kind = cold_return_kind_from_span(parser->symbols, fn->ret);
    int32_t slot = body_slot(body, ret_kind, cold_return_slot_size(parser->symbols, fn->ret, ret_kind));
    body_slot_set_type(body, slot, fn->ret);
    bool has_composite = (ret_kind != SLOT_I32 && ret_kind != SLOT_I64 && ret_kind != SLOT_PTR && ret_kind != SLOT_OPAQUE);
    for (int32_t pi = 0; !has_composite && pi < fn->arity; pi++) {
        if (fn->param_kind[pi] != SLOT_I32 && fn->param_kind[pi] != SLOT_I64 &&
            fn->param_kind[pi] != SLOT_PTR && fn->param_kind[pi] != SLOT_OPAQUE) has_composite = true;
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
    Span object_slot_type = {0};
    ObjectDef *object = parser_resolve_object_constructor(owner, name,
                                                          &object_slot_type);
    if (object && !object->is_ref) {
        if (!owner->arena) die("object constructor call requires parser arena");
        int32_t synthetic_len = args.len + 2;
        char *synthetic = arena_alloc(owner->arena, (size_t)synthetic_len + 1);
        synthetic[0] = '(';
        if (args.len > 0) memcpy(synthetic + 1, args.ptr, (size_t)args.len);
        synthetic[synthetic_len - 1] = ')';
        synthetic[synthetic_len] = '\0';
        Parser ctor_parser = {{(const uint8_t *)synthetic, synthetic_len},
                              0, owner->arena, owner->symbols,
                              owner->import_mode, owner->import_alias};
        if (parser_next_is_object_constructor(&ctor_parser)) {
            int32_t slot = parse_object_constructor_typed(&ctor_parser, body,
                                                          locals, object,
                                                          object_slot_type);
            parser_ws(&ctor_parser);
            if (ctor_parser.pos != ctor_parser.source.len)
                die("object constructor call has trailing tokens");
            if (kind_out) *kind_out = SLOT_OBJECT;
            return slot;
        }
    }
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
    if (cold_try_ref_object_cast(owner, body, stripped_name, arg_start, arg_count,
                                 &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_type_alias_pointer_cast(owner, body, stripped_name, arg_start, arg_count,
                                         &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_len_intrinsic(body, stripped_name, arg_start, arg_count,
                                   &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_slice_intrinsic(body, stripped_name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_is_i32_to_str_intrinsic(stripped_name)) {
        if (arg_count != 1) die("int to str intrinsic arity mismatch");
        int32_t arg_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[arg_slot] != SLOT_I32) {
            if (ColdErrorRecoveryEnabled) {
                int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                if (kind_out) *kind_out = SLOT_STR;
                return slot;
            }
            die("int to str intrinsic expects int32");
        }
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_op(body, BODY_OP_I32_TO_STR, slot, arg_slot, 0);
        if (kind_out) *kind_out = SLOT_STR;
        return slot;
    }
    if (cold_try_parse_int_intrinsic(body, stripped_name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_join_intrinsic(body, stripped_name, arg_start, arg_count,
                                    &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_split_intrinsic(body, stripped_name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_str_strip_intrinsic(body, stripped_name, arg_start, arg_count,
                                     &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_result_intrinsic(owner, body, stripped_name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_os_intrinsic(owner, body, stripped_name, arg_start, arg_count,
                              &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_path_intrinsic(owner, body, stripped_name, arg_start, arg_count,
                                &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_csg_intrinsic(owner, body, stripped_name, arg_start, arg_count,
                               &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_slplan_intrinsic(owner, body, stripped_name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (span_eq(stripped_name, "atomicLoadI32")) {
        if (arg_count != 1) die("atomicLoadI32 expects 1 arg");
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_ATOMIC_LOAD_I32, slot, ptr, 0);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    if (span_eq(stripped_name, "atomicStoreI32")) {
        if (arg_count != 2) die("atomicStoreI32 expects 2 args");
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t val = body->call_arg_slot[arg_start + 1];
        body_op(body, BODY_OP_ATOMIC_STORE_I32, ptr, val, 0);
        if (kind_out) *kind_out = SLOT_I32;
        return cold_make_i32_const_slot(body, 0);
    }
    if (span_eq(stripped_name, "atomicCasI32")) {
        if (arg_count != 3) die("atomicCasI32 arity mismatch");
        int32_t ptr = body->call_arg_slot[arg_start];
        int32_t expect = body->call_arg_slot[arg_start + 1];
        int32_t desired = body->call_arg_slot[arg_start + 2];
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_ATOMIC_CAS_I32, slot, ptr, desired, expect);
        if (kind_out) *kind_out = SLOT_I32;
        return slot;
    }
    if (span_eq(stripped_name, "ptr")) {
        if (arg_count != 1) die("ptr() expects 1 arg");
        int32_t src = body->call_arg_slot[arg_start];
        int32_t src_kind = body->slot_kind[src];
        if (src_kind == SLOT_PTR) {
            if (kind_out) *kind_out = SLOT_PTR;
            return src;
        }
        int32_t slot = body_slot(body, SLOT_PTR, 8);
        if (src_kind == SLOT_OPAQUE || src_kind == SLOT_OPAQUE_REF ||
            src_kind == SLOT_I64 || src_kind == SLOT_I64_REF) {
            body_op(body, BODY_OP_COPY_I64, slot, src, 0);
        } else {
            body_op(body, BODY_OP_COPY_I32, slot, src, 0);
        }
        if (kind_out) *kind_out = SLOT_PTR;
        return slot;
    }
    if (cold_try_backend_intrinsic(owner, body, stripped_name, arg_start, arg_count,
                                   &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_assert_intrinsic(body, stripped_name, arg_start, arg_count,
                                  &intrinsic_slot, kind_out)) {
        return intrinsic_slot;
    }
    if (cold_try_bootstrap_intrinsic(body, stripped_name, arg_start, arg_count,
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
    Span lookup_name = parser_can_scope_bare_name(owner, stripped_name)
                           ? parser_scoped_bare_name(owner, stripped_name)
                           : stripped_name;
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
        int32_t field_call_slot = -1;
        if (cold_try_field_function_pointer_call(owner, body, locals, name,
                                                 arg_start, arg_count,
                                                 &field_call_slot, kind_out)) {
            return field_call_slot;
        }
    }
    if (fn_index < 0) {
        /* Resolve against modules explicitly imported by the current source. */
        if (owner->import_source_count > 0 && owner->import_sources) {
            for (int32_t isi = 0; isi < owner->import_source_count; isi++) {
                Span qualified = cold_arena_join3(owner->arena, owner->import_sources[isi].alias, ".", stripped_name);
                int32_t resolved = symbols_find_fn_for_call(owner->symbols, qualified, body, arg_start, arg_count);
                if (resolved >= 0) {
                    fn_index = resolved;
                    lookup_name = qualified;
                    break;
                }
            }
        }
    }
    if (fn_index < 0) {
        if (ColdErrorRecoveryEnabled) {
            int32_t slot = body_slot(body, SLOT_I32, 4);
            if (kind_out) *kind_out = SLOT_I32;
            return slot;
        }
        fprintf(stderr, "cheng_cold: unresolved function call name=%.*s body=%.*s\n",
                (int)lookup_name.len, lookup_name.ptr,
                body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
                body && body->debug_name.len > 0 ? body->debug_name.ptr : (uint8_t *)"");
        cold_print_same_name_call_candidates(owner->symbols, body, lookup_name,
                                             arg_start, arg_count);
        die("unresolved function call");
    }
	    FnDef *fn = &owner->symbols->functions[fn_index];
        cold_materialize_specialized_body_if_needed(owner, fn_index);
	    cold_apply_contextual_empty_sequence_args(body, owner->symbols, fn, arg_start, arg_count);
	    if (!cold_validate_call_args(body, fn, arg_start, arg_count, owner->import_mode)) {
	        cold_die_call_arg_mismatch(body, fn, arg_start, arg_count);
	    }
    if (cold_fn_is_rawbytes_bytes_alloc(fn, lookup_name)) {
        return cold_emit_bytes_alloc_call(owner, body, fn, arg_start, arg_count, kind_out);
    }
    /* Fill in default values for missing trailing params */
    if (arg_count < fn->arity) {
        for (int32_t pi = arg_count; pi < fn->arity; pi++) {
            if (fn->param_has_default[pi]) {
                int32_t def_slot = cold_make_i32_const_slot(body, fn->param_default_value[pi]);
                body_call_arg(body, def_slot);
            }
        }
        arg_count = fn->arity;
    }
    int32_t ret_kind = cold_return_kind_from_span(owner->symbols, fn->ret);
    int32_t slot = body_slot(body, ret_kind, cold_return_slot_size(owner->symbols, fn->ret, ret_kind));
    body_slot_set_type(body, slot, fn->ret);
    bool has_composite = (ret_kind != SLOT_I32 && ret_kind != SLOT_I64 && ret_kind != SLOT_PTR && ret_kind != SLOT_OPAQUE);
    for (int32_t pi = 0; !has_composite && pi < fn->arity; pi++) {
        if (fn->param_kind[pi] != SLOT_I32 && fn->param_kind[pi] != SLOT_I64 &&
            fn->param_kind[pi] != SLOT_PTR && fn->param_kind[pi] != SLOT_OPAQUE) has_composite = true;
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

int32_t parse_object_constructor_typed(Parser *parser, BodyIR *body, Locals *locals,
                                       ObjectDef *object, Span slot_type) {
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
    body_slot_set_type(body, slot, slot_type.len > 0 ? slot_type : object->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, payload_count);
    return slot;
}

int32_t parse_object_constructor(Parser *parser, BodyIR *body, Locals *locals,
                                        ObjectDef *object) {
    return parse_object_constructor_typed(parser, body, locals, object, object->name);
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

Span parser_take_balanced_statement_expr_span(Parser *parser, const char *missing_message) {
    parser_inline_ws(parser);
    int32_t start = parser->pos;
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
        if (depth == 0 && (c == '\n' || c == '\r' || c == ';')) break;
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
        if (c == '(' || c == '[' || c == '{') {
            depth++;
            parser->pos++;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            if (depth <= 0) die("unbalanced statement expression");
            depth--;
            parser->pos++;
            continue;
        }
        parser->pos++;
    }
    if (depth != 0 || in_string || in_char) die("unterminated statement expression");
    Span expr = span_trim(span_sub(parser->source, start, parser->pos));
    if (expr.len <= 0) die(missing_message);
    return expr;
}

static int32_t parser_line_indent_at(Parser *parser, int32_t pos) {
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

static int32_t parser_next_nonblank_line_start(Parser *parser, int32_t pos) {
    while (pos < parser->source.len) {
        int32_t line_start = pos;
        while (line_start < parser->source.len &&
               (parser->source.ptr[line_start] == ' ' ||
                parser->source.ptr[line_start] == '\t' ||
                parser->source.ptr[line_start] == '\r')) {
            line_start++;
        }
        if (line_start >= parser->source.len) return parser->source.len;
        if (parser->source.ptr[line_start] != '\n') return line_start;
        pos = line_start + 1;
    }
    return parser->source.len;
}

Span parser_take_let_initializer_span(Parser *parser, int32_t stmt_indent,
                                      const char *missing_message) {
    parser_inline_ws(parser);
    if (parser->pos < parser->source.len &&
        parser->source.ptr[parser->pos] != '\n' &&
        parser->source.ptr[parser->pos] != '\r') {
        return parser_take_balanced_statement_expr_span(parser, missing_message);
    }
    int32_t start = parser_next_nonblank_line_start(parser, parser->pos);
    if (start >= parser->source.len ||
        parser_line_indent_at(parser, start) <= stmt_indent) {
        die(missing_message);
    }
    parser->pos = start;
    int32_t scan = start;
    int32_t depth = 0;
    bool in_string = false;
    bool in_char = false;
    while (scan < parser->source.len) {
        uint8_t c = parser->source.ptr[scan];
        if (in_string) {
            if (c == '\\' && scan + 1 < parser->source.len) {
                scan += 2;
                continue;
            }
            if (c == '"') in_string = false;
            scan++;
            continue;
        }
        if (in_char) {
            if (c == '\\' && scan + 1 < parser->source.len) {
                scan += 2;
                continue;
            }
            if (c == '\'') in_char = false;
            scan++;
            continue;
        }
        if (c == '"') { in_string = true; scan++; continue; }
        if (c == '\'') { in_char = true; scan++; continue; }
        if (c == '(' || c == '[' || c == '{') { depth++; scan++; continue; }
        if (c == ')' || c == ']' || c == '}') {
            if (depth <= 0) die("unbalanced let initializer");
            depth--;
            scan++;
            continue;
        }
        if ((c == '\n' || c == '\r') && depth == 0) {
            int32_t next = parser_next_nonblank_line_start(parser, scan + 1);
            if (next >= parser->source.len ||
                parser_line_indent_at(parser, next) <= stmt_indent) {
                break;
            }
        }
        scan++;
    }
    if (depth != 0 || in_string || in_char) die("unterminated let initializer");
    parser->pos = scan;
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
           span_eq(token, "int8") || span_eq(token, "Int8") ||
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
        die("ErrorMessage target must be ErrorInfo object");
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
    if (!result) die("Result intrinsic target is not Result object");
    ObjectField *ok = object_find_field(result, cold_cstr_span("ok"));
    ObjectField *value = object_find_field(result, cold_cstr_span("value"));
    ObjectField *err = object_find_field(result, cold_cstr_span("err"));
    if (!ok || ok->kind != SLOT_I32 || !value || !err || err->kind != SLOT_OBJECT) {
        die("Result intrinsic target has invalid Result layout");
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
           span_eq(name, "GetStdout") || span_eq(name, "get_stdout") ||
           span_eq(name, "getStdout");
}

bool cold_name_is_stderr(Span name) {
    return span_eq(name, "os.GetStderr") || span_eq(name, "os.Get_stderr") ||
           span_eq(name, "os.get_stderr") || span_eq(name, "GetStderr") ||
           span_eq(name, "Get_stderr") || span_eq(name, "get_stderr") ||
           span_eq(name, "getStderr");
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

bool cold_parser_imports_rawbytes(Parser *parser) {
    if (!parser || !parser->import_sources || parser->import_source_count <= 0) return false;
    for (int32_t i = 0; i < parser->import_source_count; i++) {
        if (!span_eq(parser->import_sources[i].alias, "rawbytes")) continue;
        if (strcmp(parser->import_sources[i].path, "src/std/rawbytes.cheng") == 0) return true;
    }
    return false;
}

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
        span_eq(name, "os.os_getrusage_bridge") ||
        span_eq(name, "os_getrusage_bridge") ||
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
        span_eq(name, "os.os_exit_bridge") ||
        span_eq(name, "os_exit_bridge") ||
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
    if (span_eq(name, "system.HeapNewCompat") ||
        span_eq(name, "HeapNewCompat") ||
        span_eq(name, "heapNewCompat")) {
        if (arg_count != 1) die("HeapNewCompat intrinsic arity mismatch");
        int32_t len = body->call_arg_slot[arg_start];
        if (body->slot_kind[len] != SLOT_I32 && body->slot_kind[len] != SLOT_I64)
            die("HeapNewCompat len must be int");
        int32_t slot = body_slot(body, SLOT_PTR, 8);
        body_slot_set_type(body, slot, cold_cstr_span("ptr"));
        body_op(body, BODY_OP_MMAP, slot, len, 0);
        if (kind_out) *kind_out = SLOT_PTR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawbytes.BytesAlloc") ||
        span_eq(name, "rawbytes.bytesAlloc") ||
        ((span_eq(name, "BytesAlloc") || span_eq(name, "bytesAlloc")) &&
         cold_parser_imports_rawbytes(parser))) {
        if (arg_count != 1) die("BytesAlloc intrinsic arity mismatch");
        int32_t len = body->call_arg_slot[arg_start];
        if (body->slot_kind[len] != SLOT_I32) die("BytesAlloc length must be int32");
        ObjectDef *bytes = symbols_resolve_object(parser->symbols, cold_cstr_span("rawbytes.Bytes"));
        if (!bytes) bytes = symbols_resolve_object(parser->symbols, cold_cstr_span("Bytes"));
        if (!bytes) die("BytesAlloc requires Bytes object layout");
        int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(bytes));
        body_slot_set_type(body, slot, bytes->name);
        body_op(body, BODY_OP_BYTES_ALLOC, slot, len, 0);
        if (kind_out) *kind_out = SLOT_OBJECT;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawbytes.BytesLen") ||
        span_eq(name, "rawbytes.bytesLen") ||
        ((span_eq(name, "BytesLen") || span_eq(name, "bytesLen")) &&
         cold_parser_imports_rawbytes(parser))) {
        if (arg_count != 1) die("BytesLen intrinsic arity mismatch");
        int32_t bytes = body->call_arg_slot[arg_start];
        if (body->slot_kind[bytes] != SLOT_OBJECT && body->slot_kind[bytes] != SLOT_OBJECT_REF)
            die("BytesLen expects Bytes object");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_PAYLOAD_LOAD, slot, bytes, 8, 4);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawbytes.BytesGet") ||
        span_eq(name, "rawbytes.bytesGet") ||
        ((span_eq(name, "BytesGet") || span_eq(name, "bytesGet")) &&
         cold_parser_imports_rawbytes(parser))) {
        if (arg_count != 2) die("BytesGet intrinsic arity mismatch");
        int32_t bytes = body->call_arg_slot[arg_start];
        int32_t idx = body->call_arg_slot[arg_start + 1];
        if (body->slot_kind[bytes] != SLOT_OBJECT && body->slot_kind[bytes] != SLOT_OBJECT_REF)
            die("BytesGet expects Bytes object");
        if (body->slot_kind[idx] != SLOT_I32) die("BytesGet index must be int32");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_BYTES_GET, slot, bytes, idx);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawbytes.BytesSet") ||
        span_eq(name, "rawbytes.bytesSet") ||
        ((span_eq(name, "BytesSet") || span_eq(name, "bytesSet")) &&
         cold_parser_imports_rawbytes(parser))) {
        if (arg_count != 3) die("BytesSet intrinsic arity mismatch");
        int32_t bytes = body->call_arg_slot[arg_start];
        int32_t idx = body->call_arg_slot[arg_start + 1];
        int32_t value = body->call_arg_slot[arg_start + 2];
        if (body->slot_kind[bytes] != SLOT_OBJECT && body->slot_kind[bytes] != SLOT_OBJECT_REF)
            die("BytesSet expects Bytes object");
        if (body->slot_kind[idx] != SLOT_I32) die("BytesSet index must be int32");
        if (body->slot_kind[value] != SLOT_I32) die("BytesSet value must be int32");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_BYTES_SET, slot, bytes, idx, value);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawbytes.BytesToHex") ||
        span_eq(name, "rawbytes.bytesToHex") ||
        ((span_eq(name, "BytesToHex") || span_eq(name, "bytesToHex")) &&
         cold_parser_imports_rawbytes(parser))) {
        if (arg_count != 1) die("BytesToHex intrinsic arity mismatch");
        int32_t bytes = body->call_arg_slot[arg_start];
        if (body->slot_kind[bytes] != SLOT_OBJECT && body->slot_kind[bytes] != SLOT_OBJECT_REF)
            die("BytesToHex expects Bytes object");
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_slot_set_type(body, slot, cold_cstr_span("str"));
        body_op(body, BODY_OP_BYTES_TO_HEX, slot, bytes, 0);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawmem_support.rawmemPtrAdd") ||
        span_eq(name, "rawmem_support.RawmemPtrAdd") ||
        span_eq(name, "system.system_ptr_add") ||
        span_eq(name, "system.SystemPtrAdd") ||
        span_eq(name, "system_ptr_add") ||
        span_eq(name, "SystemPtrAdd") ||
        span_eq(name, "ptr_add") ||
        span_eq(name, "rawmemPtrAdd") ||
        span_eq(name, "RawmemPtrAdd")) {
        if (arg_count != 2) die("RawmemPtrAdd intrinsic arity mismatch");
        int32_t ptr_slot = body->call_arg_slot[arg_start];
        int32_t off_slot = body->call_arg_slot[arg_start + 1];
        int32_t ptr_kind = body->slot_kind[ptr_slot];
        int32_t off_kind = body->slot_kind[off_slot];
        if (ptr_kind != SLOT_PTR && ptr_kind != SLOT_OPAQUE) die("RawmemPtrAdd ptr must be ptr");
        if (off_kind == SLOT_I32_REF) {
            int32_t loaded = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_REF_LOAD, loaded, off_slot, 0);
            off_slot = loaded;
            off_kind = SLOT_I32;
        }
        if (off_kind != SLOT_I32 && off_kind != SLOT_I64) die("RawmemPtrAdd offset must be int");
        int32_t slot = body_slot(body, SLOT_PTR, 8);
        body_slot_set_type(body, slot, cold_cstr_span("ptr"));
        body_op(body, BODY_OP_PTR_ADD, slot, ptr_slot, off_slot);
        if (kind_out) *kind_out = SLOT_PTR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawmem_support.rawmemWriteI8") ||
        span_eq(name, "rawmem_support.RawmemWriteI8") ||
        span_eq(name, "rawmemWriteI8") ||
        span_eq(name, "RawmemWriteI8")) {
        if (arg_count != 3) die("RawmemWriteI8 intrinsic arity mismatch");
        int32_t dst_ptr = body->call_arg_slot[arg_start];
        int32_t idx_slot = body->call_arg_slot[arg_start + 1];
        int32_t byte_slot = body->call_arg_slot[arg_start + 2];
        int32_t dst_kind = body->slot_kind[dst_ptr];
        int32_t idx_kind = body->slot_kind[idx_slot];
        int32_t byte_kind = body->slot_kind[byte_slot];
        if (dst_kind != SLOT_PTR && dst_kind != SLOT_OPAQUE) die("RawmemWriteI8 dst must be ptr");
        if (idx_kind == SLOT_I32_REF) {
            int32_t loaded = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_REF_LOAD, loaded, idx_slot, 0);
            idx_slot = loaded;
            idx_kind = SLOT_I32;
        }
        if (idx_kind != SLOT_I32 && idx_kind != SLOT_I64) die("RawmemWriteI8 index must be int");
        if (byte_kind != SLOT_I32 && byte_kind != SLOT_I64) die("RawmemWriteI8 byte must be int");
        int32_t write_ptr = body_slot(body, SLOT_PTR, 8);
        body_slot_set_type(body, write_ptr, cold_cstr_span("ptr"));
        body_op(body, BODY_OP_PTR_ADD, write_ptr, dst_ptr, idx_slot);
        int32_t len_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, len_slot, 1, 0);
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_SET_RAW, slot, write_ptr, byte_slot, len_slot);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawmem_support.rawmemCopyRaw") ||
        span_eq(name, "rawmem_support.rawmemCopy") ||
        span_eq(name, "rawmem_support.RawmemCopyRaw") ||
        span_eq(name, "rawmem_support.RawmemCopy") ||
        span_eq(name, "rawmemCopyRaw") ||
        span_eq(name, "rawmemCopy") ||
        span_eq(name, "RawmemCopyRaw") ||
        span_eq(name, "RawmemCopy")) {
        if (arg_count != 3) die("RawmemCopy intrinsic arity mismatch");
        int32_t dst_ptr = body->call_arg_slot[arg_start];
        int32_t src_ptr = body->call_arg_slot[arg_start + 1];
        int32_t len_slot = body->call_arg_slot[arg_start + 2];
        int32_t dst_kind = body->slot_kind[dst_ptr];
        int32_t src_kind = body->slot_kind[src_ptr];
        int32_t len_kind = body->slot_kind[len_slot];
        if (dst_kind != SLOT_PTR && dst_kind != SLOT_OPAQUE) die("RawmemCopy dst must be ptr");
        if (src_kind != SLOT_PTR && src_kind != SLOT_OPAQUE) die("RawmemCopy src must be ptr");
        if (len_kind != SLOT_I32 && len_kind != SLOT_I64) die("RawmemCopy len must be int");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_COPY_RAW, slot, dst_ptr, src_ptr, len_slot);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "seqs.setLen") ||
        span_eq(name, "setLen") ||
        span_eq(name, "SetLen")) {
        if (arg_count != 2) die("setLen intrinsic arity mismatch");
        int32_t seq_slot = body->call_arg_slot[arg_start];
        int32_t len_slot = body->call_arg_slot[arg_start + 1];
        int32_t seq_kind = body->slot_kind[seq_slot];
        int32_t len_kind = body->slot_kind[len_slot];
        if (len_kind == SLOT_I32_REF) {
            int32_t loaded = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_REF_LOAD, loaded, len_slot, 0);
            len_slot = loaded;
            len_kind = SLOT_I32;
        }
        if (len_kind != SLOT_I32) die("setLen length must be int32");
        int32_t element_size = 0;
        if (seq_kind == SLOT_SEQ_I32 || seq_kind == SLOT_SEQ_I32_REF) {
            element_size = 4;
        } else if (seq_kind == SLOT_SEQ_STR || seq_kind == SLOT_SEQ_STR_REF) {
            element_size = COLD_STR_SLOT_SIZE;
        } else if (seq_kind == SLOT_SEQ_OPAQUE || seq_kind == SLOT_SEQ_OPAQUE_REF ||
                   seq_kind == SLOT_OBJECT_REF || seq_kind == SLOT_PTR) {
            element_size = cold_seq_opaque_element_size_for_slot(parser->symbols, body, seq_slot);
        } else {
            die("setLen target must be sequence");
        }
        body_op3(body, BODY_OP_SEQ_SET_LEN, seq_slot, len_slot, 0, element_size);
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "rawmem_support.rawmemSet") ||
        span_eq(name, "rawmem_support.RawmemSet") ||
        span_eq(name, "rawmemSet") ||
        span_eq(name, "RawmemSet")) {
        if (arg_count != 3) die("RawmemSet intrinsic arity mismatch");
        int32_t dst_ptr = body->call_arg_slot[arg_start];
        int32_t byte_slot = body->call_arg_slot[arg_start + 1];
        int32_t len_slot = body->call_arg_slot[arg_start + 2];
        int32_t dst_kind = body->slot_kind[dst_ptr];
        int32_t byte_kind = body->slot_kind[byte_slot];
        int32_t len_kind = body->slot_kind[len_slot];
        if (dst_kind != SLOT_PTR && dst_kind != SLOT_OPAQUE) die("RawmemSet dst must be ptr");
        if (byte_kind != SLOT_I32 && byte_kind != SLOT_I64) die("RawmemSet byte must be int");
        if (len_kind != SLOT_I32 && len_kind != SLOT_I64) die("RawmemSet len must be int");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_SET_RAW, slot, dst_ptr, byte_slot, len_slot);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "thread.thread_yield_raw") ||
        span_eq(name, "thread_yield_raw")) {
        if (arg_count != 0) die("thread_yield_raw intrinsic arity mismatch");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_THREAD_YIELD, slot, 0, 0);
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
    if (span_eq(name, "os.WriteFileBytes") || span_eq(name, "WriteFileBytes") ||
        span_eq(name, "os.writeFileBytes") || span_eq(name, "writeFileBytes")) {
        if (arg_count != 2) die("WriteFileBytes arity mismatch");
        int32_t path = body->call_arg_slot[arg_start];
        int32_t bytes = body->call_arg_slot[arg_start + 1];
        path = cold_require_str_value(body, path, "WriteFileBytes path");
        int32_t bytes_kind = body->slot_kind[bytes];
        int32_t data_slot = -1;
        int32_t len_slot = -1;
        if (bytes_kind == SLOT_SEQ_OPAQUE || bytes_kind == SLOT_SEQ_OPAQUE_REF) {
            len_slot = body_slot(body, SLOT_I32, 4);
            body_op3(body, BODY_OP_PAYLOAD_LOAD, len_slot, bytes, 0, 4);
            data_slot = body_slot(body, SLOT_PTR, 8);
            body_op3(body, BODY_OP_PAYLOAD_LOAD, data_slot, bytes, 8, 8);
        } else if (bytes_kind == SLOT_OBJECT || bytes_kind == SLOT_OBJECT_REF) {
            ObjectDef *object = symbols_resolve_object(parser->symbols, body->slot_type[bytes]);
            if (!object) object = symbols_resolve_object(parser->symbols, cold_cstr_span("Bytes"));
            if (!object) die("WriteFileBytes bytes object type missing");
            ObjectField *data_field = object_find_field(object, cold_cstr_span("data"));
            if (!data_field) data_field = object_find_field(object, cold_cstr_span("buffer"));
            ObjectField *len_field = object_find_field(object, cold_cstr_span("len"));
            if (!data_field || !len_field) die("WriteFileBytes bytes layout mismatch");
            data_slot = body_slot_for_object_field(body, data_field);
            len_slot = body_slot_for_object_field(body, len_field);
            body_op3(body, BODY_OP_PAYLOAD_LOAD, data_slot, bytes, data_field->offset, data_field->size);
            body_op3(body, BODY_OP_PAYLOAD_LOAD, len_slot, bytes, len_field->offset, len_field->size);
        } else {
            die("WriteFileBytes expects uint8[] or Bytes");
        }
        if (body->slot_kind[len_slot] != SLOT_I32) die("WriteFileBytes len must be int32");
        if (body->slot_kind[data_slot] != SLOT_PTR && body->slot_kind[data_slot] != SLOT_OPAQUE) {
            die("WriteFileBytes data must be ptr");
        }
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_PATH_WRITE_BYTES, slot, path, data_slot, len_slot);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = slot;
        return true;
    }
    if (cold_name_is_param_str(name)) {
        if (arg_count != 1) die("paramStr intrinsic arity mismatch");
        int32_t index_slot = body->call_arg_slot[arg_start];
        if (body->slot_kind[index_slot] == SLOT_I32_REF) {
            int32_t loaded = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_REF_LOAD, loaded, index_slot, 0);
            index_slot = loaded;
        } else if (body->slot_kind[index_slot] != SLOT_I32) {
            die("paramStr index must be int32");
        }
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

    if (span_eq(name, "system.strFromCStringCopy") ||
        span_eq(name, "system.StrFromCStringCopy") ||
        span_eq(name, "strFromCStringCopy")) {
        if (arg_count != 1) {
            if (parser->import_mode) {
                int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                if (kind_out) *kind_out = SLOT_STR;
                *slot_out = slot;
                return true;
            }
            die("strFromCStringCopy intrinsic arity mismatch");
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
        /* fallback for ptr/opaque args: store ptr + zero len as str */
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
	    if (span_eq(name, "addr")) {
	        if (arg_count != 1) return false;
	        int32_t src = body->call_arg_slot[arg_start];
	        int32_t slot = body_slot(body, SLOT_PTR, 8);
	        body_slot_set_type(body, slot, cold_cstr_span("ptr"));
	        body_op3(body, BODY_OP_FIELD_REF, slot, src, 0, 0);
	        if (kind_out) *kind_out = SLOT_PTR;
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
	    if (span_eq(name, "load")) {
	        if (arg_count != 1) return false;
	        int32_t ptr = body->call_arg_slot[arg_start];
	        if (!cold_kind_is_pointer_like(body->slot_kind[ptr])) die("load expects pointer");
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
	    if (span_eq(name, "store")) {
	        if (arg_count != 2) return false;
	        int32_t ptr = body->call_arg_slot[arg_start];
	        int32_t val = body->call_arg_slot[arg_start + 1];
	        if (!cold_kind_is_pointer_like(body->slot_kind[ptr])) die("store expects pointer");
	        if (body->slot_kind[val] != SLOT_I32) die("store expects int32 value");
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

bool body_slot_is_str_literal(BodyIR *body, int32_t slot) {
    for (int32_t i = 0; i < body->op_count; i++) {
        if (body->op_kind[i] == BODY_OP_STR_LITERAL && body->op_dst[i] == slot) {
            return true;
        }
    }
    return false;
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
    if (span_eq(name, "ParserKindToText")) {
        if (arg_count != 1) return false;
        int32_t slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
        body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
        if (kind_out) *kind_out = SLOT_STR;
        *slot_out = slot;
        return true;
    }
    if (span_eq(name, "chengpath.PathIsAbsolute") ||
        span_eq(name, "PathIsAbsolute")) {
        if (arg_count != 1) die("PathIsAbsolute arity mismatch");
        int32_t raw = body->call_arg_slot[arg_start];
        raw = cold_require_str_value(body, raw, "PathIsAbsolute raw");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_PATH_IS_ABSOLUTE, slot, raw, 0);
        if (kind_out) *kind_out = SLOT_I32;
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

ObjectField *cold_required_existing_object_field(ObjectDef *object, const char *name) {
    ObjectField *field = object_find_field(object, cold_cstr_span(name));
    if (!field) {
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

int32_t cold_make_str_seq_from_slots(BodyIR *body, Span type_name,
                                     const int32_t *items,
                                     int32_t item_count) {
    if (item_count < 0) die("invalid str seq materializer");
    int32_t slot = cold_make_empty_str_seq_slot(body, type_name);
    for (int32_t i = 0; i < item_count; i++) {
        if (body->slot_kind[items[i]] != SLOT_STR &&
            body->slot_kind[items[i]] != SLOT_STR_REF) {
            die("str seq materializer item must be str");
        }
        body_op(body, BODY_OP_SEQ_STR_ADD, slot, items[i], 0);
    }
    return slot;
}

int32_t cold_make_str_seq_from_cstrs(BodyIR *body, Span type_name,
                                     const char **items,
                                     int32_t item_count) {
    if (item_count < 0) die("invalid str seq materializer");
    int32_t slots[16];
    if (item_count > (int32_t)(sizeof(slots) / sizeof(slots[0]))) {
        die("str seq materializer too large");
    }
    for (int32_t i = 0; i < item_count; i++) {
        slots[i] = cold_make_str_literal_cstr_slot(body, items[i]);
    }
    return cold_make_str_seq_from_slots(body, type_name, slots, item_count);
}

int32_t cold_make_i32_seq_from_values(BodyIR *body, Span type_name,
                                      const int32_t *items,
                                      int32_t item_count) {
    if (item_count < 0) die("invalid int32 seq materializer");
    int32_t data_slot = -1;
    if (item_count > 0) {
        int32_t payload_start = body->call_arg_count;
        for (int32_t i = 0; i < item_count; i++) {
            body_call_arg_with_offset(body, cold_make_i32_const_slot(body, items[i]), i * 4);
        }
        data_slot = body_slot(body, SLOT_ARRAY_I32, align_i32(item_count * 4, 8));
        body_slot_set_array_len(body, data_slot, item_count);
        body_op3(body, BODY_OP_MAKE_COMPOSITE, data_slot, 0, payload_start, item_count);
    }
    int32_t slot = body_slot(body, SLOT_SEQ_I32, 16);
    body_slot_set_type(body, slot, type_name.len > 0 ? type_name : cold_cstr_span("int32[]"));
    body_slot_set_array_len(body, slot, item_count);
    body_op3(body, BODY_OP_MAKE_SEQ_I32, slot, data_slot, -1, item_count);
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
    if (!span_same(result->name, result_type)) die("Result layout exact type mismatch");
    ObjectField *ok = cold_required_existing_object_field(result, "ok");
    ObjectField *value = cold_required_existing_object_field(result, "value");
    ObjectField *err = cold_required_existing_object_field(result, "err");
    if (ok->kind != SLOT_I32 || err->kind != SLOT_OBJECT) die("Result layout kind mismatch");
    int32_t payload_start = body->call_arg_count;
    body_call_arg_with_offset(body, cold_make_i32_const_slot(body, 0), ok->offset);
    body_call_arg_with_offset(body, cold_zero_slot_for_field(body, value), value->offset);
    body_call_arg_with_offset(body, cold_make_error_info_slot(parser, body, message), err->offset);
    int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(result));
    body_slot_set_type(body, slot, result->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, 3);
    return slot;
}

ObjectDef *cold_resolve_object_any(Symbols *symbols, Span actual_type,
                                   const char *qualified,
                                   const char *unqualified);
void cold_store_required_field(BodyIR *body, int32_t object_slot,
                               ObjectDef *object,
                               const char *field_name,
                               int32_t value_slot);

bool cold_try_backend_intrinsic(Parser *parser, BodyIR *body, Span name,
                                       int32_t arg_start, int32_t arg_count,
                                       int32_t *slot_out, int32_t *kind_out) {
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
        {
          ObjectDef *emit_result = symbols_resolve_object(parser->symbols, cold_cstr_span("direct.DirectObjectEmitResult"));
          if (!emit_result) emit_result = symbols_resolve_object(parser->symbols, cold_cstr_span("DirectObjectEmitResult"));
          if (!emit_result) die("DirectObjectEmitResult layout missing");
          int32_t value_slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(emit_result));
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
          Span args[1] = {emit_result->name};
          Span result_type = cold_join_generic_instance(parser->arena,
                                                        cold_cstr_span("Result"),
                                                        args, 1);
          int32_t slot = cold_make_ok_result_slot(parser, body, result_type, value_slot, kind_out);
          *slot_out = slot;
        }
        return true;
    }
    if (0 && (span_eq(name, "lower.BuildLoweringPlanStubFromCompilerCsg") ||
        span_eq(name, "BuildLoweringPlanStubFromCompilerCsg"))) {
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
        ObjectDef *slp_obj = cold_slplan_object_from_slot(parser, body, link_plan);
        ObjectDef *csg_obj = cold_resolve_object_any(parser->symbols,
                                                     body->slot_type[csg],
                                                     "ccsg.CompilerCsg",
                                                     "CompilerCsg");
        if (!csg_obj) die("CompilerCsg layout missing for lowering materializer");
        int32_t workspace_root = cold_load_object_field_slot(body, link_plan,
                                                             cold_required_object_field(slp_obj, "workspaceRoot"));
        int32_t package_root = cold_load_object_field_slot(body, link_plan,
                                                           cold_required_object_field(slp_obj, "packageRoot"));
        int32_t package_id = cold_load_object_field_slot(body, link_plan,
                                                         cold_required_object_field(slp_obj, "packageId"));
        int32_t entry_path = cold_load_object_field_slot(body, link_plan,
                                                         cold_required_object_field(slp_obj, "entryPath"));
        int32_t entry_symbol = cold_load_object_field_slot(body, link_plan,
                                                           cold_required_object_field(slp_obj, "entrySymbol"));
        int32_t module_stem = cold_load_object_field_slot(body, link_plan,
                                                          cold_required_object_field(slp_obj, "moduleStem"));
        int32_t sep = cold_make_str_literal_cstr_slot(body, "::");
        int32_t symbol_prefix = cold_concat_str_slots(body, module_stem, sep);
        int32_t symbol_text = cold_concat_str_slots(body, symbol_prefix, entry_symbol);

        int32_t source_paths[1] = { entry_path };
        int32_t owner_modules[1] = { module_stem };
        int32_t names[1] = { entry_symbol };
        int32_t symbols[1] = { symbol_text };
        int32_t export_names[1] = { entry_symbol };
        int32_t flag_values[1] = { 1 };
        int32_t name_id_values[1] = { 0 };
        int32_t call_conv_values[1] = { 1 };
        int32_t layout_values[1] = { 5 };
        const char *missing_items[1] = { "lowering_typed_expr_facts_missing" };

        cold_store_required_field(body, plan_slot, lp_obj, "workspaceRoot", workspace_root);
        cold_store_required_field(body, plan_slot, lp_obj, "packageRoot", package_root);
        cold_store_required_field(body, plan_slot, lp_obj, "packageId", package_id);
        cold_store_required_field(body, plan_slot, lp_obj, "entryPath", entry_path);
        cold_store_required_field(body, plan_slot, lp_obj, "entrySymbol", entry_symbol);
        cold_store_required_field(body, plan_slot, lp_obj, "sourceBundleCid",
                                  cold_load_object_field_slot(body, csg,
                                                              cold_required_object_field(csg_obj, "sourceBundleCid")));
        cold_store_required_field(body, plan_slot, lp_obj, "compilerCsgCid",
                                  cold_load_object_field_slot(body, csg,
                                                              cold_required_object_field(csg_obj, "graphCid")));
        cold_store_required_field(body, plan_slot, lp_obj, "exprLayer",
                                  cold_load_object_field_slot(body, csg,
                                                              cold_required_object_field(csg_obj, "exprLayer")));
        cold_store_required_field(body, plan_slot, lp_obj, "typedExprFacts",
                                  cold_load_object_field_slot(body, csg,
                                                              cold_required_object_field(csg_obj, "typedExprFacts")));
        cold_store_required_field(body, plan_slot, lp_obj, "typedIr",
                                  cold_load_object_field_slot(body, csg,
                                                              cold_required_object_field(csg_obj, "typedIr")));
        cold_store_required_field(body, plan_slot, lp_obj, "functionSourcePaths",
                                  cold_make_str_seq_from_slots(body,
                                                               cold_required_object_field(lp_obj, "functionSourcePaths")->type_name,
                                                               source_paths, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionOwnerModulePaths",
                                  cold_make_str_seq_from_slots(body,
                                                               cold_required_object_field(lp_obj, "functionOwnerModulePaths")->type_name,
                                                               owner_modules, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionNames",
                                  cold_make_str_seq_from_slots(body,
                                                               cold_required_object_field(lp_obj, "functionNames")->type_name,
                                                               names, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionSymbolTexts",
                                  cold_make_str_seq_from_slots(body,
                                                               cold_required_object_field(lp_obj, "functionSymbolTexts")->type_name,
                                                               symbols, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionExportedFlags",
                                  cold_make_i32_seq_from_values(body,
                                                                cold_required_object_field(lp_obj, "functionExportedFlags")->type_name,
                                                                flag_values, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionExportSymbolNames",
                                  cold_make_str_seq_from_slots(body,
                                                               cold_required_object_field(lp_obj, "functionExportSymbolNames")->type_name,
                                                               export_names, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionNameIds",
                                  cold_make_i32_seq_from_values(body,
                                                                cold_required_object_field(lp_obj, "functionNameIds")->type_name,
                                                                name_id_values, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionEntryFlags",
                                  cold_make_i32_seq_from_values(body,
                                                                cold_required_object_field(lp_obj, "functionEntryFlags")->type_name,
                                                                flag_values, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionCallConvs",
                                  cold_make_i32_seq_from_values(body,
                                                                cold_required_object_field(lp_obj, "functionCallConvs")->type_name,
                                                                call_conv_values, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "functionResultLayoutKinds",
                                  cold_make_i32_seq_from_values(body,
                                                                cold_required_object_field(lp_obj, "functionResultLayoutKinds")->type_name,
                                                                layout_values, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "missingReasons",
                                  cold_make_str_seq_from_cstrs(body,
                                                               cold_required_object_field(lp_obj, "missingReasons")->type_name,
                                                               missing_items, 1));
        cold_store_required_field(body, plan_slot, lp_obj, "pipelineStage",
                                  cold_make_str_literal_cstr_slot(body, "cold_csg_to_lowering_plan_partial"));
        if (plan >= 0 && plan < body->slot_count && body->slot_size[plan] >= 8)
            body_op3(body, BODY_OP_PAYLOAD_STORE, plan, plan_slot, 0, body->slot_size[plan]);
        int32_t ok_slot = cold_make_i32_const_slot(body, 1);
        if (kind_out) *kind_out = SLOT_I32;
        *slot_out = ok_slot;
        return true;
    }
    if (0 && (span_eq(name, "pobj.BuildPrimaryObjectPlan") ||
        span_eq(name, "BuildPrimaryObjectPlan"))) {
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
        ObjectDef *slp_obj = cold_slplan_object_from_slot(parser, body, link_plan);
        const char *missing_items[1] = { "primary_object_typed_body_ir_missing" };
        cold_store_required_field(body, slot, object, "targetTriple",
                                  cold_load_object_field_slot(body, link_plan,
                                                              cold_required_object_field(slp_obj, "targetTriple")));
        cold_store_required_field(body, slot, object, "objectFormat",
                                  cold_make_str_literal_cstr_slot(body, "macho"));
        cold_store_required_field(body, slot, object, "outputPath",
                                  cold_load_object_field_slot(body, link_plan,
                                                              cold_required_object_field(slp_obj, "outputPath")));
        cold_store_required_field(body, slot, object, "entryPath",
                                  cold_load_object_field_slot(body, link_plan,
                                                              cold_required_object_field(slp_obj, "entryPath")));
        cold_store_required_field(body, slot, object, "entrySymbol",
                                  cold_load_object_field_slot(body, link_plan,
                                                              cold_required_object_field(slp_obj, "entrySymbol")));
        cold_store_required_field(body, slot, object, "missingReasons",
                                  cold_make_str_seq_from_cstrs(body,
                                                               cold_required_object_field(object, "missingReasons")->type_name,
                                                               missing_items, 1));
        cold_store_required_field(body, slot, object, "pipelineStage",
                                  cold_make_str_literal_cstr_slot(body, "cold_primary_object_plan_unmaterialized"));
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
    if (0 && (span_eq(name, "ccsg.BuildCompilerCsgInto") ||
        span_eq(name, "BuildCompilerCsgInto"))) {
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
        cold_store_required_field(body, out_csg, csg_obj, "sourceBundleCid", source_bundle_cid);
        cold_store_required_field_zero(body, out_csg, csg_obj, "exprLayer");
        cold_store_required_field_zero(body, out_csg, csg_obj, "typedExprFacts");
        cold_store_required_field_zero(body, out_csg, csg_obj, "typedIr");
        cold_store_required_field(body, out_csg, csg_obj, "nodeCount", cold_make_i32_const_slot(body, 3));
        cold_store_required_field(body, out_csg, csg_obj, "edgeCount", cold_make_i32_const_slot(body, 2));
        cold_store_required_field(body, out_csg, csg_obj, "nodes", nodes_seq);
        cold_store_required_field(body, out_csg, csg_obj, "edges", edges_seq);
        cold_store_required_field_zero(body, out_csg, csg_obj, "canonicalGraphCid");
        cold_store_required_field_zero(body, out_csg, csg_obj, "graphCid");
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

int32_t parse_cstring_cast(Parser *parser, BodyIR *body,
                           Locals *locals, int32_t *kind) {
    if (!parser_take(parser, "(")) die("expected ( after cstring cast");
    int32_t value_kind = SLOT_I32;
    int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
    if (!parser_take(parser, ")")) die("expected ) after cstring cast");
    int32_t slot = value_slot;
    if (value_kind == SLOT_STR || value_kind == SLOT_STR_REF) {
        slot = cold_materialize_str_data_ptr(body, value_slot, value_kind);
    } else if (value_kind == SLOT_OPAQUE || value_kind == SLOT_OPAQUE_REF ||
               value_kind == SLOT_PTR) {
        if (value_kind == SLOT_OPAQUE_REF) {
            int32_t loaded = body_slot(body, SLOT_OPAQUE, 8);
            body_op(body, BODY_OP_PTR_LOAD_I64, loaded, value_slot, 0);
            slot = loaded;
        }
    } else {
        die("cstring cast expects str or pointer");
    }
    body_slot_set_type(body, slot, cold_cstr_span("cstring"));
    *kind = SLOT_OPAQUE;
    return slot;
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
    if (value_kind == SLOT_VARIANT) {
        int32_t tag_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TAG_LOAD, tag_slot, value_slot, 0);
        value_slot = tag_slot;
        value_kind = SLOT_I32;
    }
    if (value_kind != SLOT_I32) {
        if (value_kind == SLOT_I64) {
            int32_t dst = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_FROM_I64, dst, value_slot, 0);
            *kind = SLOT_I32;
            if (!parser_take(parser, ")")) return 0;
            return dst;
        }
        /* Non-int32 scalar cast: consume ) and return 0 */
        if (!parser_take(parser, ")")) return 0;
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        *kind = SLOT_I32;
        return zero;
    }
    if (!parser_take(parser, ")")) return 0; /* skip malformed cast */;
    *kind = SLOT_I32;
    return value_slot;
}

bool parser_take_until_top_level_else(Parser *parser, Span *then_out);
Span parser_take_inline_rest_of_line(Parser *parser);
Span parser_take_balanced_rest_of_line(Parser *parser);
void cold_store_value_into_slot(BodyIR *body, int32_t dst, int32_t dst_kind,
                                int32_t src, int32_t src_kind);
int32_t parse_expr_from_span(Parser *owner, BodyIR *body, Locals *locals,
                                    Span expr, int32_t *kind,
                                    const char *trailing_message);
void parse_condition_span(Parser *owner, BodyIR *body, Locals *locals,
                                 Span condition, int32_t block,
                                 int32_t true_block, int32_t false_block);

int32_t parse_if_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    Span condition = parser_take_until_top_level_char(parser, ':', "expected : in if");
    if (!parser_take(parser, ":")) {
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        if (kind) *kind = SLOT_I32;
        return zero;
    }
    Span then_span = {0};
    if (!parser_take_until_top_level_else(parser, &then_span)) {
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        if (kind) *kind = SLOT_I32;
        return zero;
    }
    if (!parser_take(parser, "else")) {
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        if (kind) *kind = SLOT_I32;
        return zero;
    }
    if (span_eq(parser_peek(parser), "elif")) {
        (void)parser_token(parser);
        int32_t else_block = body_block(body);
        int32_t join_block = body_block(body);
        int32_t true_block = body_block(body);
        parse_condition_span(parser, body, locals, condition, 0, true_block, else_block);
        body_reopen_block(body, true_block);
        int32_t then_kind = SLOT_I32;
        int32_t then_slot = parse_expr_from_span(parser, body, locals, then_span, &then_kind,
                                                  "unsupported if then expression");
        int32_t result_kind = then_kind;
        int32_t result_slot = body_slot(body, result_kind, body->slot_size[then_slot]);
        if (body->slot_type[then_slot].len > 0)
            body_slot_set_type(body, result_slot, body->slot_type[then_slot]);
        cold_store_value_into_slot(body, result_slot, result_kind, then_slot, then_kind);
        if (body->block_term[true_block] < 0) body_branch_to(body, true_block, join_block);
        body_reopen_block(body, else_block);
        int32_t elif_kind = SLOT_I32;
        int32_t elif_slot = parse_if_expr(parser, body, locals, &elif_kind);
        cold_store_value_into_slot(body, result_slot, result_kind, elif_slot, elif_kind);
        if (body->block_term[else_block] < 0) body_branch_to(body, else_block, join_block);
        body_reopen_block(body, join_block);
        if (kind) *kind = result_kind;
        return result_slot;
    }
    if (!parser_take(parser, ":")) {
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        if (kind) *kind = SLOT_I32;
        return zero;
    }
    Span else_span = parser_take_balanced_rest_of_line(parser);
    if (else_span.len <= 0) {
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        if (kind) *kind = SLOT_I32;
        return zero;
    }
    int32_t join_block = body_block(body);
    int32_t true_block = body_block(body);
    int32_t false_block = body_block(body);
    parse_condition_span(parser, body, locals, condition, 0, true_block, false_block);
    body_reopen_block(body, true_block);
    int32_t then_kind = SLOT_I32;
    int32_t then_slot = parse_expr_from_span(parser, body, locals, then_span, &then_kind,
                                              "unsupported if then expression");
    int32_t result_kind = then_kind;
    int32_t result_slot = body_slot(body, result_kind, body->slot_size[then_slot]);
    if (body->slot_type[then_slot].len > 0)
        body_slot_set_type(body, result_slot, body->slot_type[then_slot]);
    cold_store_value_into_slot(body, result_slot, result_kind, then_slot, then_kind);
    if (body->block_term[true_block] < 0) body_branch_to(body, true_block, join_block);
    body_reopen_block(body, false_block);
    int32_t else_kind = SLOT_I32;
    int32_t else_slot = parse_expr_from_span(parser, body, locals, else_span, &else_kind,
                                              "unsupported if else expression");
    cold_store_value_into_slot(body, result_slot, result_kind, else_slot, else_kind);
    if (body->block_term[false_block] < 0) body_branch_to(body, false_block, join_block);
    body_reopen_block(body, join_block);
    if (kind) *kind = result_kind;
    return result_slot;
}

int32_t parse_postfix(Parser *parser, BodyIR *body, Locals *locals,
                             int32_t slot, int32_t *kind);
static int32_t parse_closure_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
int32_t parse_if_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
int32_t cold_materialize_i32_ref(BodyIR *body, int32_t slot, int32_t *kind);
int32_t cold_materialize_i64_value(BodyIR *body, int32_t slot, int32_t *kind);
static int32_t cold_parse_pointer_deref_expr(Parser *parser, BodyIR *body,
                                             Locals *locals, Span ptr_expr,
                                             int32_t *kind);
static int32_t cold_emit_pointer_load(Parser *parser, BodyIR *body,
                                      int32_t ptr_slot, int32_t *kind);

static ObjectDef *cold_resolve_pointer_object(Symbols *symbols, Span type) {
    Span current = span_trim(type);
    for (int32_t i = 0; i < 8; i++) {
        TypeDef *alias = symbols ? symbols_find_type(symbols, current) : 0;
        if (!alias || alias->alias_type.len <= 0) break;
        current = span_trim(alias->alias_type);
    }
    if (cold_type_has_pointer_suffix(current)) {
        current = span_trim(span_sub(current, 0, current.len - 1));
    }
    if (current.len <= 0) return 0;
    return symbols_resolve_object(symbols, current);
}

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
    if (span_eq(token, "*")) {
        if (span_eq(parser_peek(parser), "(")) {
            (void)parser_token(parser);
            Span ptr_expr = parser_take_until_top_level_char(parser, ')',
                                                            "expected ) in pointer deref");
            if (!parser_take(parser, ")")) die("expected ) in pointer deref");
            return cold_parse_pointer_deref_expr(parser, body, locals, ptr_expr, kind);
        }
        int32_t ptr_kind = SLOT_PTR;
        int32_t ptr_slot = parse_primary(parser, body, locals, &ptr_kind);
        ptr_slot = parse_postfix(parser, body, locals, ptr_slot, &ptr_kind);
        if (!cold_kind_is_pointer_like(ptr_kind)) {
            fprintf(stderr,
                    "cheng_cold: pointer deref non-pointer kind=%d type=%.*s body=%.*s\n",
                    ptr_kind,
                    ptr_slot >= 0 && ptr_slot < body->slot_count
                        ? (int)body->slot_type[ptr_slot].len : 0,
                    ptr_slot >= 0 && ptr_slot < body->slot_count
                        ? body->slot_type[ptr_slot].ptr : (const uint8_t *)"",
                    body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
                    body && body->debug_name.len > 0 ? body->debug_name.ptr : (const uint8_t *)"");
            die("pointer deref expects pointer expression");
        }
        return cold_emit_pointer_load(parser, body, ptr_slot, kind);
    }
    if (span_eq(token, "if")) {
        return parse_if_expr(parser, body, locals, kind);
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
    if (span_eq(token, "-")) {
        int32_t vk = SLOT_I32, vs = parse_primary(parser, body, locals, &vk);
        vs = parse_postfix(parser, body, locals, vs, &vk);
        if (vk == SLOT_I32_REF) { int32_t l = body_slot(body, SLOT_I32, 4); body_op(body, BODY_OP_I32_REF_LOAD, l, vs, 0); vs = l; vk = SLOT_I32; }
        if (vk == SLOT_I32 || vk == SLOT_OPAQUE || vk == SLOT_OPAQUE_REF) {
            int32_t z = body_slot(body, SLOT_I32, 4), d = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, z, 0, 0); body_op3(body, BODY_OP_I32_SUB, d, z, vs, 0);
            *kind = SLOT_I32; return d;
        }
        if (vk == SLOT_I64 || vk == SLOT_I64_REF) {
            if (vk == SLOT_I64_REF) { int32_t l = body_slot(body, SLOT_I64, 8); body_op(body, BODY_OP_PTR_LOAD_I64, l, vs, 0); vs = l; }
            int32_t z = body_slot(body, SLOT_I64, 8), d = body_slot(body, SLOT_I64, 8);
            body_op(body, BODY_OP_I64_CONST, z, 0, 0); body_op3(body, BODY_OP_I64_SUB, d, z, vs, 0);
            *kind = SLOT_I64; return d;
        }
        die("- expects int32 or int64");
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
            GlobalDef *global = parser_find_global(parser, base_name);
            if (global) {
                alocal = locals_add_global_shadow(parser, body, locals, base_name, global);
                if (!alocal) die("global address shadow creation failed");
                addr_slot = alocal->slot;
                *kind = alocal->kind;
            } else {
            /* Check if it's a function name: &fnName → function pointer */
            Span lookup_fn_name = parser_can_scope_bare_name(parser, base_name)
                                      ? parser_scoped_bare_name(parser, base_name)
                                      : base_name;
            int32_t fn_idx = -1;
            for (int32_t si = 0; si < parser->symbols->function_count; si++) {
                FnDef *sfn = &parser->symbols->functions[si];
                if (span_same(sfn->name, lookup_fn_name)) { fn_idx = si; break; }
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
    if (span_eq(token, "cstring") && span_eq(parser_peek(parser), "(")) {
        return parse_cstring_cast(parser, body, locals, kind);
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
            int32_t new_kind = nobj->is_ref ? SLOT_PTR : SLOT_OBJECT_REF;
            int32_t ns = body_slot(body, new_kind, 8);
            body_slot_set_type(body, ns, nobj->name);
            body_op(body, BODY_OP_MMAP, ns, len, 0);
            *kind = new_kind;
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
        /* Payloadless enum variant: produce I32 tag directly instead of VARIANT */
        if (variant->field_count == 0) {
            TypeDef *parenum = symbols_find_variant_type(parser->symbols, variant);
            if (parenum && parenum->is_enum) {
                int32_t slot = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, slot, variant->tag, 0);
                *kind = SLOT_I32;
                return slot;
            }
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
    /* sizeof(type) intrinsic */
    if (span_eq(token, "sizeof") && span_eq(parser_peek(parser), "(")) {
        (void)parser_token(parser); /* consume ( */
        Span type_name = parser_token(parser);
        if (type_name.len <= 0) die("sizeof expects type name");
        int32_t sz = 4; /* default size */
        if (span_eq(type_name, "int32") || span_eq(type_name, "Int32") ||
            span_eq(type_name, "int") || span_eq(type_name, "Int") ||
            span_eq(type_name, "bool") || span_eq(type_name, "Bool") ||
            span_eq(type_name, "uint32") || span_eq(type_name, "Uint32")) {
            sz = 4;
        } else if (span_eq(type_name, "int64") || span_eq(type_name, "Int64") ||
                   span_eq(type_name, "uint64") || span_eq(type_name, "Uint64")) {
            sz = 8;
        } else if (span_eq(type_name, "ptr") || span_eq(type_name, "Ptr") ||
                   span_eq(type_name, "cstring")) {
            sz = 8;
        } else if (span_eq(type_name, "uint8") || span_eq(type_name, "Uint8") ||
                   span_eq(type_name, "int8") || span_eq(type_name, "Int8") ||
                   span_eq(type_name, "char") || span_eq(type_name, "Char")) {
            sz = 1;
        } else if (span_eq(type_name, "str") || span_eq(type_name, "Str")) {
            sz = COLD_STR_SLOT_SIZE;
        } else if (span_eq(type_name, "int16") || span_eq(type_name, "Int16") ||
                   span_eq(type_name, "uint16") || span_eq(type_name, "Uint16")) {
            sz = 2;
        } else {
            /* Try user-defined type (e.g. struct name) */
            TypeDef *ty = symbols_find_type(parser->symbols, type_name);
            if (ty) {
                sz = ty->max_slot_size > 0 ? ty->max_slot_size : 4;
            }
            /* Generic type parameter (e.g. T): stay at default 4 */
        }
        if (!parser_take(parser, ")")) return 0;
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, sz, 0);
        *kind = SLOT_I32;
        return slot;
    }
    /* Anonymous function / closure expression: fn(params): ret = body */
    if (span_eq(token, "fn") && span_eq(parser_peek(parser), "(")) {
        return parse_closure_expr(parser, body, locals, kind);
    }
    /* Bare fn keyword in expression context: treat as function pointer value */
    if (span_eq(token, "fn")) {
        int32_t addr_slot = body_slot(body, SLOT_PTR, 8);
        body_op(body, BODY_OP_I32_CONST, addr_slot, 0, 0);
        *kind = SLOT_PTR;
        return addr_slot;
    }
    /* check if token is a local variable FIRST -- field access on locals is handled by parse_postfix */
    Local *local = locals_find(locals, token);
    if (local) {
        int32_t slot = local->slot;
        *kind = local->kind;
        return slot;
    }
    GlobalDef *global = parser_find_global(parser, token);
    if (global) {
        local = locals_add_global_shadow(parser, body, locals, token, global);
        if (!local) die("global local shadow creation failed");
        *kind = local->kind;
        return local->slot;
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
            /* Payloadless enum variant: produce I32 tag directly */
            if (vc->field_count == 0) {
                TypeDef *parenum = symbols_find_variant_type(parser->symbols, vc);
                if (parenum && parenum->is_enum) {
                    int32_t slot = body_slot(body, SLOT_I32, 4);
                    body_op(body, BODY_OP_I32_CONST, slot, vc->tag, 0);
                    *kind = SLOT_I32;
                    return slot;
                }
            }
            *kind = SLOT_VARIANT;
            if (vc->field_count > 0) {
                return parse_constructor(parser, body, locals, vc);
            }
            int32_t vz = symbols_variant_slot_size(parser->symbols, vc);
            int32_t sl = body_slot(body, SLOT_VARIANT, vz);
            body_op3(body, BODY_OP_MAKE_VARIANT, sl, vc->tag, -1, 0);
            return sl;
        }
        if (!vc) {
            /* Try object constructor for qualified name (e.g. hostops.LoggedRun(label: ...)) */
            Span object_slot_type = {0};
            ObjectDef *obj = parser_resolve_object_constructor(parser, call_name,
                                                               &object_slot_type);
            if (obj && !obj->is_ref && parser_next_is_object_constructor(parser)) {
                *kind = SLOT_OBJECT;
                return parse_object_constructor_typed(parser, body, locals, obj,
                                                      object_slot_type);
            }
        }
        if (!span_eq(parser_peek(parser), "(")) {
            if (ColdErrorRecoveryEnabled) {
                int32_t zero = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
                *kind = SLOT_I32;
                return zero;
            }
            fprintf(stderr, "cheng_cold: unsupported qualified expression name=%.*s offset=%d\n",
                    call_name.len, call_name.ptr,
                    cold_span_offset(parser->source, call_name));
            die("unsupported qualified expression");
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
                /* Import alias fallback: type_name might be an import alias */
                if (!qualified_variant && parser->import_source_count > 0 && parser->import_sources) {
                    for (int32_t isi = 0; isi < parser->import_source_count; isi++) {
                        if (!span_same(parser->import_sources[isi].alias, type_name)) continue;
                        Span aliased = cold_arena_join3(parser->arena,
                            parser->import_sources[isi].alias, ".", variant_name);
                        qualified_variant = symbols_find_variant(parser->symbols, aliased);
                        if (!qualified_variant)
                            qualified_variant = symbols_find_variant(parser->symbols, variant_name);
                        if (qualified_variant) break;
                    }
                }
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
        /* last-chance: Type.Variant or import alias constant/variant qualified name */
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
                /* Import alias fallback: tn might be an import alias for a module.
                   Search for vn as a constant or variant in the imported module. */
                if (parser->import_source_count > 0 && parser->import_sources) {
                    for (int32_t isi = 0; isi < parser->import_source_count; isi++) {
                        if (!span_same(parser->import_sources[isi].alias, tn)) continue;
                        Span aliased = cold_arena_join3(parser->arena,
                            parser->import_sources[isi].alias, ".", vn);
                        ConstDef *qc = symbols_find_const(parser->symbols, aliased);
                        if (qc) {
                            if (qc->is_str) {
                                int32_t li = body_string_literal(body, qc->str_val);
                                int32_t sl = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
                                body_op(body, BODY_OP_STR_LITERAL, sl, li, 0);
                                *kind = SLOT_STR;
                                return sl;
                            }
                            int32_t sl = body_slot(body, SLOT_I32, 4);
                            body_op(body, BODY_OP_I32_CONST, sl, qc->value, 0);
                            *kind = SLOT_I32;
                            return sl;
                        }
                        Variant *qv = symbols_find_variant(parser->symbols, aliased);
                        if (!qv) qv = symbols_find_variant(parser->symbols, vn);
                        if (qv) {
                            *kind = SLOT_VARIANT;
                            if (qv->field_count > 0) {
                                return parse_constructor(parser, body, locals, qv);
                            }
                            int32_t vz = symbols_variant_slot_size(parser->symbols, qv);
                            int32_t sl = body_slot(body, SLOT_VARIANT, vz);
                            body_op3(body, BODY_OP_MAKE_VARIANT, sl, qv->tag, -1, 0);
                            return sl;
                        }
                        break;
                    }
                }
            }
        }
        /* Named parameter in function call: name: value -> parse value */
        if (span_eq(parser_peek(parser), ":")) {
            (void)parser_token(parser);
            int32_t val_kind = SLOT_I32;
            int32_t val_slot = parse_expr(parser, body, locals, &val_kind);
            *kind = val_kind;
            return val_slot;
        }
        if (ColdErrorRecoveryEnabled) {
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            *kind = SLOT_I32;
            return zero;
        }
        fprintf(stderr, "cheng_cold: unknown qualified identifier token=%.*s qualified=%.*s offset=%d\n",
                token.len, token.ptr, qualified.len, qualified.ptr,
                cold_span_offset(parser->source, qualified));
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
        Span lookup_fn_name = parser_can_scope_bare_name(parser, token)
                                  ? parser_scoped_bare_name(parser, token)
                                  : token;
        int32_t fn_idx = symbols_find_fn(parser->symbols, lookup_fn_name, 0, 0, 0, (Span){0});
        if (fn_idx < 0) {
            /* Try arity-insensitive lookup -- just check if name matches any function */
            for (int32_t si = 0; si < parser->symbols->function_count; si++) {
                if (span_same(parser->symbols->functions[si].name, lookup_fn_name)) {
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
    /* Named parameter in function call: name: value -> parse value */
    if (span_eq(parser_peek(parser), ":")) {
        (void)parser_token(parser);
        int32_t val_kind = SLOT_I32;
        int32_t val_slot = parse_expr(parser, body, locals, &val_kind);
        *kind = val_kind;
        return val_slot;
    }
    /* Last-chance: token might be a known type name used as an expression
       (e.g. a struct/ADT type name, builtin type, or generic parameter).
       Return a type-descriptor slot. */
    {
        TypeDef *ty = symbols_find_type(parser->symbols, token);
        if (ty) {
            int32_t sz = ty->max_slot_size > 0 ? ty->max_slot_size : 4;
            int32_t slot = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, slot, sz, 0);
            *kind = SLOT_I32;
            return slot;
        }
        /* Check builtin type names: str, int32, bool, int64, etc. */
        int32_t builtin_kind = cold_parser_slot_kind_from_type(parser->symbols, token);
        int32_t builtin_size = cold_parser_slot_size_from_type(parser->symbols, token, builtin_kind);
        if (builtin_size > 0 && cold_type_is_builtin_surface(token)) {
            /* Not the default int32 fallback — it's a recognized type */
            if (builtin_size > 0) {
                if (cold_type_is_builtin_surface(token) || cold_span_is_simple_ident(token)) {
                    int32_t slot = body_slot(body, builtin_kind, builtin_size);
                    body_slot_set_type(body, slot, token);
                    /* Create a zero-init slot for type-descriptor use */
                    if (builtin_kind == SLOT_STR) {
                        body_op(body, BODY_OP_STR_LITERAL, slot, 0, 0);
                    } else if (builtin_kind == SLOT_I32 || builtin_kind == SLOT_F32) {
                        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
                    } else if (builtin_kind == SLOT_I64 || builtin_kind == SLOT_F64) {
                        body_op(body, BODY_OP_I64_CONST, slot, 0, 0);
                    }
                    *kind = builtin_kind;
                    return slot;
                }
            }
        }
    }
    if (ColdErrorRecoveryEnabled) {
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        *kind = SLOT_I32;
        return zero;
    }
    fprintf(stderr, "cheng_cold: unknown identifier token=%.*s body=%.*s offset=%d\n",
            (int)token.len, token.ptr,
            body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
            body && body->debug_name.len > 0 ? body->debug_name.ptr : (uint8_t *)"",
            cold_span_offset(parser->source, token));
    die("unknown identifier");
    return -1;
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
            param_kinds[arity] = cold_parser_slot_kind_from_type(parser->symbols, param_type);
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
        if (parser->pos < parser->source.len && parser->source.ptr[parser->pos] == '[') {
            parser->pos++;
            Span idx = parser_take_until_top_level_char(parser, ']', "expected ]");
            if (!parser_take(parser, "]")) die("expected ]");
            if (*kind == SLOT_STR || *kind == SLOT_STR_REF) {
                int32_t ik = SLOT_I32, is = -1;
                if (span_is_i32(idx)) { is = body_slot(body, SLOT_I32, 4); body_op(body, BODY_OP_I32_CONST, is, span_i32(idx), 0); }
                else { Parser ip = {idx, 0, parser->arena, parser->symbols, parser->import_mode, parser->import_alias}; is = parse_expr(&ip, body, locals, &ik); }
                is = cold_materialize_i32_ref(body, is, &ik); if (ik != SLOT_I32) die("str idx must be int32");
                int32_t d = body_slot(body, SLOT_I32, 4); body_op(body, BODY_OP_STR_INDEX, d, slot, is);
                slot = d; *kind = SLOT_I32; continue;
            }
            if (*kind == SLOT_SEQ_STR || *kind == SLOT_SEQ_STR_REF) {
                int32_t ik = SLOT_I32, is = -1;
                if (span_is_i32(idx)) { is = body_slot(body, SLOT_I32, 4); body_op(body, BODY_OP_I32_CONST, is, span_i32(idx), 0); }
                else { Parser ip = {idx, 0, parser->arena, parser->symbols, parser->import_mode, parser->import_alias}; is = parse_expr(&ip, body, locals, &ik); }
                is = cold_materialize_i32_ref(body, is, &ik); if (ik != SLOT_I32) die("str[] idx must be int32");
                int32_t d = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE); body_op(body, BODY_OP_SEQ_STR_INDEX_DYNAMIC, d, slot, is);
                slot = d; *kind = SLOT_STR; continue;
            }
            if (*kind == SLOT_SEQ_OPAQUE || *kind == SLOT_SEQ_OPAQUE_REF) {
                int32_t ik = SLOT_I32, is = body_slot(body, SLOT_I32, 4);
                if (span_is_i32(idx)) { body_op(body, BODY_OP_I32_CONST, is, span_i32(idx), 0); }
                else { Parser ip = {idx, 0, parser->arena, parser->symbols, parser->import_mode, parser->import_alias}; is = parse_expr(&ip, body, locals, &ik); }
                is = cold_materialize_i32_ref(body, is, &ik); if (ik != SLOT_I32) die("opaque[] idx must be int32");
                Span et = cold_seq_opaque_element_type(body->slot_type[slot]);
                int32_t ek = cold_parser_slot_kind_from_type(parser->symbols, et);
                int32_t es = cold_seq_opaque_element_size_for_slot(parser->symbols, body, slot);
                int32_t d = body_slot(body, ek, es); body_slot_set_type(body, d, et);
                body_op3(body, BODY_OP_SEQ_OPAQUE_INDEX_DYNAMIC, d, slot, is, es);
                slot = d; *kind = ek; continue;
            }
            if (*kind == SLOT_ARRAY_I32 || *kind == SLOT_SEQ_I32 || *kind == SLOT_SEQ_I32_REF) {
                int32_t d = body_slot(body, SLOT_I32, 4);
                if (span_is_i32(idx)) {
                    int32_t v = span_i32(idx);
                    if (*kind == SLOT_ARRAY_I32) { body_op3(body, BODY_OP_PAYLOAD_LOAD, d, slot, v * 4, 4); }
                    else { int32_t is = body_slot(body, SLOT_I32, 4); body_op(body, BODY_OP_I32_CONST, is, v, 0); body_op3(body, BODY_OP_SEQ_I32_INDEX_DYNAMIC, d, slot, is, 0); }
                } else {
                    int32_t ik = SLOT_I32; Parser ip = parser_child(parser, idx);
                    int32_t is = parse_expr(&ip, body, locals, &ik); is = cold_materialize_i32_ref(body, is, &ik);
                    if (ik != SLOT_I32) die("int32[] idx must be int32");
                    body_op3(body, *kind == SLOT_ARRAY_I32 ? BODY_OP_ARRAY_I32_INDEX_DYNAMIC : BODY_OP_SEQ_I32_INDEX_DYNAMIC, d, slot, is, 0);
                }
                slot = d; *kind = SLOT_I32; continue;
            }
            return 0;
        }
        if (span_eq(parser_peek(parser), ".")) {
            (void)parser_token(parser); /* consume . */
        } else if (span_eq(parser_peek(parser), "-")) {
            int32_t saved = parser->pos;
            (void)parser_token(parser); /* consume - */
            if (!span_eq(parser_peek(parser), ">")) {
                parser->pos = saved;
                break;
            }
            (void)parser_token(parser); /* consume > */
            if (*kind == SLOT_OPAQUE || *kind == SLOT_OPAQUE_REF || *kind == SLOT_PTR) {
                Span tn = body->slot_type[slot];
                if (tn.len > 1 && tn.ptr[tn.len - 1] == '*') tn = span_sub(tn, 0, tn.len - 1);
                tn = span_trim(tn);
                ObjectDef *obj = cold_resolve_pointer_object(parser->symbols, body->slot_type[slot]);
                if (!obj && tn.len > 0) obj = symbols_resolve_object(parser->symbols, tn);
                if (obj) {
                    *kind = SLOT_OBJECT_REF;
                    body->slot_type[slot] = obj->name;
                } else {
                    fprintf(stderr,
                            "cheng_cold: arrow target type unresolved type=%.*s kind=%d body=%.*s\n",
                            (int)body->slot_type[slot].len, body->slot_type[slot].ptr,
                            *kind,
                            body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
                            body && body->debug_name.len > 0 ? body->debug_name.ptr : (const uint8_t *)"");
                    die("arrow field access requires pointer to object");
                }
            }
        } else {
            break;
        }
        {
            Span field_name = parser_token(parser);
            if (field_name.len <= 0) die("expected field name");
            if (*kind == SLOT_ARRAY_I32 && span_eq(field_name, "len")) {
                int32_t len_slot = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, len_slot, body->slot_aux[slot], 0);
                slot = len_slot;
                *kind = SLOT_I32;
                return slot;
            }
            if ((*kind == SLOT_SEQ_I32 || *kind == SLOT_SEQ_I32_REF ||
                 *kind == SLOT_SEQ_STR || *kind == SLOT_SEQ_STR_REF ||
                 *kind == SLOT_SEQ_OPAQUE || *kind == SLOT_SEQ_OPAQUE_REF) &&
                span_eq(field_name, "len")) {
                int32_t len_slot = body_slot(body, SLOT_I32, 4);
                body_op3(body, BODY_OP_PAYLOAD_LOAD, len_slot, slot, 0, 4);
                slot = len_slot;
                *kind = SLOT_I32;
                return slot;
            }
            if ((*kind == SLOT_STR || *kind == SLOT_STR_REF) && span_eq(field_name, "len")) {
                int32_t len_slot = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_STR_LEN, len_slot, slot, 0);
                slot = len_slot;
                *kind = SLOT_I32;
                return slot;
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
                index_slot = cold_materialize_i32_ref(body, index_slot, &index_kind);
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
                index_slot = cold_materialize_i32_ref(body, index_slot, &index_kind);
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
                index_slot = cold_materialize_i32_ref(body, index_slot, &index_kind);
                if (index_kind != SLOT_I32) die("opaque[] index expression must be int32");
                Span elem_type = cold_seq_opaque_element_type(body->slot_type[slot]);
                int32_t elem_kind = cold_parser_slot_kind_from_type(parser->symbols, elem_type);
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
                index_slot = cold_materialize_i32_ref(body, index_slot, &index_kind);
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
            /* Check if this is a ternary conditional (? :) or Result try-postfix.
               Scan ahead for ':' at depth 0 before end-of-line/end-of-expr. */
            int32_t scan_pos = parser->pos + 1;
            int32_t scan_depth = 0;
            bool is_ternary = false;
            while (scan_pos < parser->source.len) {
                uint8_t sc = parser->source.ptr[scan_pos];
                if (sc == '\n' || sc == '\r') break;
                if (sc == '(') scan_depth++;
                if (sc == ')') { if (scan_depth <= 0) break; scan_depth--; }
                if (sc == ':' && scan_depth == 0) { is_ternary = true; break; }
                scan_pos++;
            }
            if (is_ternary) {
                /* Ternary conditional: condition ? thenExpr : elseExpr */
                (void)parser_token(parser); /* consume ? */
                int32_t then_kind = *kind;
                int32_t then_slot = parse_expr(parser, body, locals, &then_kind);
                if (!parser_take(parser, ":")) die("expected : in ternary expression");
                int32_t else_kind = *kind;
                int32_t else_slot = parse_expr(parser, body, locals, &else_kind);
                /* Build conditional blocks */
                int32_t current_block = body->block_count - 1;
                int32_t true_block = body_block(body);
                int32_t false_block = body_block(body);
                int32_t join_block = body_block(body);
                /* Branch from current block based on condition */
                int32_t cond = cold_materialize_i32_ref(body, slot, kind);
                if (*kind != SLOT_I32) *kind = SLOT_I32;
                int32_t zero = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
                int32_t cbr = body_term(body, BODY_TERM_CBR, cond, COND_NE, zero,
                                        true_block, false_block);
                body_end_block(body, current_block, cbr);

                body_reopen_block(body, true_block);
                int32_t result_kind = then_kind;
                if (then_kind == SLOT_I32_REF) then_kind = SLOT_I32;
                int32_t result_size = body->slot_size[then_slot] > 0
                                        ? body->slot_size[then_slot] : 4;
                int32_t result_slot = body_slot(body, result_kind, result_size);
                cold_store_value_into_slot(body, result_slot, result_kind,
                                           then_slot, then_kind);
                if (body->block_term[true_block] < 0)
                    body_branch_to(body, true_block, join_block);

                body_reopen_block(body, false_block);
                if (else_kind == SLOT_I32_REF) else_kind = SLOT_I32;
                cold_store_value_into_slot(body, result_slot, result_kind,
                                           else_slot, else_kind);
                if (body->block_term[false_block] < 0)
                    body_branch_to(body, false_block, join_block);

                body_reopen_block(body, join_block);
                slot = result_slot;
                *kind = result_kind;
                continue; /* allow further postfix ops on the result */
            }
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
    if (*kind == SLOT_I64_REF) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_PTR_LOAD_I64, dst, slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    if (*kind == SLOT_OPAQUE && slot >= 0 && body->slot_size[slot] == 8) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_COPY_I64, dst, slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    if (*kind == SLOT_OPAQUE_REF) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_PTR_LOAD_I64, dst, slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    if ((*kind == SLOT_OBJECT_REF || *kind == SLOT_STR_REF ||
         *kind == SLOT_SEQ_I32_REF || *kind == SLOT_SEQ_STR_REF ||
         *kind == SLOT_SEQ_OPAQUE_REF) &&
        slot >= 0 && body->slot_size[slot] == 8) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_COPY_I64, dst, slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    if (*kind == SLOT_I32) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_I64_FROM_I32, dst, slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    if (*kind == SLOT_VARIANT || *kind == SLOT_OBJECT || *kind == SLOT_PTR) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_COPY_I64, dst, slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    if (ColdErrorRecoveryEnabled) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_I64_FROM_I32, dst, 0, 0);
        *kind = SLOT_I64;
        return dst;
    }
    die("cold cannot materialize int64 value");
    return slot;
}

int32_t cold_materialize_ptr_value(BodyIR *body, int32_t slot, int32_t *kind) {
    if (*kind == SLOT_PTR) return slot;
    if (*kind == SLOT_OPAQUE_REF || *kind == SLOT_I64_REF) {
        int32_t dst = body_slot(body, SLOT_PTR, 8);
        body_op(body, BODY_OP_PTR_LOAD_I64, dst, slot, 0);
        *kind = SLOT_PTR;
        return dst;
    }
    if (*kind == SLOT_OPAQUE || *kind == SLOT_I64 || *kind == SLOT_OBJECT_REF) {
        int32_t dst = body_slot(body, SLOT_PTR, 8);
        body_op(body, BODY_OP_COPY_I64, dst, slot, 0);
        *kind = SLOT_PTR;
        return dst;
    }
    if (ColdErrorRecoveryEnabled) {
        int32_t dst = body_slot(body, SLOT_PTR, 8);
        body_op(body, BODY_OP_I64_FROM_I32, dst, 0, 0);
        *kind = SLOT_PTR;
        return dst;
    }
    die("cold cannot materialize pointer value");
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
    /* Ternary ?: expression (lowest precedence: cond ? then_expr : else_expr)
       Distinguish from postfix ? (Result unwrap) by scanning for matching ':'
       before consuming the '?'. */
    if (span_eq(parser_peek(parser), "?")) {
        /* Advance past the ? token so scanning starts after it.
           parser_peek restores parser->pos to before whitespace, so
           manually skip whitespace and '?' before scanning. */
        int32_t after = parser->pos;
        while (after < parser->source.len && (parser->source.ptr[after] == ' ' || parser->source.ptr[after] == '\t'))
            after++;
        if (after < parser->source.len && parser->source.ptr[after] == '?')
            after++;
        int32_t peek_pos = after;
        int32_t colon_pos = -1;
        int32_t depth = 0;
        int32_t ternary_depth = 1;
        bool in_string = false;
        bool in_char = false;
        for (int32_t i = peek_pos; i < parser->source.len; i++) {
            uint8_t c = parser->source.ptr[i];
            if (in_string) { if (c == '\\') i++; else if (c == '"') in_string = false; continue; }
            if (in_char) { if (c == '\\') i++; else if (c == '\'') in_char = false; continue; }
            if (c == '"') { in_string = true; continue; }
            if (c == '\'') { in_char = true; continue; }
            if (c == '\n' || c == '\r') break;
            if (c == '(' || c == '[' || c == '{') { depth++; continue; }
            if (c == ')' || c == ']' || c == '}') { depth--; continue; }
            if (depth == 0 && c == '?') { ternary_depth++; continue; }
            if (depth == 0 && c == ':') {
                ternary_depth--;
                if (ternary_depth == 0) { colon_pos = i; break; }
                continue;
            }
        }
        if (colon_pos >= 0) {
            (void)parser_token(parser);
            int32_t then_start = parser->pos;
            Span then_span = span_trim(span_sub(parser->source, then_start, colon_pos));
            int32_t else_start = colon_pos + 1;
            while (else_start < parser->source.len &&
                   (parser->source.ptr[else_start] == ' ' || parser->source.ptr[else_start] == '\t'))
                else_start++;
            parser->pos = else_start;
            Span else_span = parser_take_balanced_rest_of_line(parser);
            int32_t current_block = 0;
            for (int32_t bi = 0; bi < body->block_count; bi++) {
                if (body->block_term[bi] < 0) { current_block = bi; break; }
            }
            int32_t cond = cold_materialize_i32_ref(body, left, &left_kind);
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            int32_t tblock = body_block(body);
            int32_t fblock = body_block(body);
            int32_t jblock = body_block(body);
            int32_t cbr = body_term(body, BODY_TERM_CBR, cond, COND_NE, zero, tblock, fblock);
            body_end_block(body, current_block, cbr);
            body_reopen_block(body, tblock);
            Parser then_parser = parser_child(parser, then_span);
            int32_t then_kind = SLOT_I32;
            int32_t then_slot = parse_expr(&then_parser, body, locals, &then_kind);
            int32_t result_kind = then_kind;
            int32_t result_slot = body_slot(body, result_kind, cold_slot_size_for_kind(result_kind));
            cold_store_value_into_slot(body, result_slot, result_kind, then_slot, then_kind);
            body_branch_to(body, tblock, jblock);
            body_reopen_block(body, fblock);
            Parser else_parser = parser_child(parser, else_span);
            int32_t else_kind = SLOT_I32;
            int32_t else_slot = parse_expr(&else_parser, body, locals, &else_kind);
            cold_store_value_into_slot(body, result_slot, result_kind, else_slot, else_kind);
            body_branch_to(body, fblock, jblock);
            body_reopen_block(body, jblock);
            *kind = result_kind;
            return result_slot;
        }
        /* No matching ':' — postfix ? (Result unwrap), leave ? unconsumed */
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
            die("str comparison operands must be str");
        }
        if (cond != COND_EQ && cond != COND_NE) {
            int32_t zero = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
            *kind = SLOT_I32;
            return zero;
        }
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
        fprintf(stderr, "cheng_cold: %s: %.*s\n",
                trailing_message ? trailing_message : "unsupported expression",
                (int)expr.len, expr.ptr);
        die(trailing_message ? trailing_message : "unsupported expression");
    }
    return slot;
}

static bool cold_deref_expr_is_u8_ptr(Span expr) {
    expr = span_trim(expr);
    return cold_span_starts_with(expr, "UInt8Ptr(");
}

static bool cold_deref_expr_is_i32_ptr(Span expr) {
    expr = span_trim(expr);
    return cold_span_starts_with(expr, "Int32Ptr(") ||
           cold_span_starts_with(expr, "int32Ptr(");
}

static Span cold_pointer_element_type(Symbols *symbols, Span type) {
    Span current = span_trim(type);
    for (int32_t i = 0; i < 8; i++) {
        TypeDef *alias = symbols ? symbols_find_type(symbols, current) : 0;
        if (!alias || alias->alias_type.len <= 0) break;
        current = span_trim(alias->alias_type);
    }
    if (!cold_type_has_pointer_suffix(current)) return (Span){0};
    return span_trim(span_sub(current, 0, current.len - 1));
}

static bool cold_pointer_element_is(Span elem, const char *name) {
    return span_eq(elem, name);
}

static int32_t cold_emit_pointer_load(Parser *parser, BodyIR *body,
                                      int32_t ptr_slot, int32_t *kind) {
    int32_t ptr_kind = body->slot_kind[ptr_slot];
    if (!cold_kind_is_pointer_like(ptr_kind)) die("pointer load expects pointer slot");
    Span elem = cold_pointer_element_type(parser->symbols, body->slot_type[ptr_slot]);
    if (cold_pointer_element_is(elem, "uint8") ||
        cold_pointer_element_is(elem, "int8") ||
        cold_pointer_element_is(elem, "char")) {
        int32_t dst = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_PTR_LOAD_U8, dst, ptr_slot, 0);
        *kind = SLOT_I32;
        return dst;
    }
    if (cold_pointer_element_is(elem, "int32") ||
        cold_pointer_element_is(elem, "int") ||
        cold_pointer_element_is(elem, "uint32") ||
        cold_pointer_element_is(elem, "bool")) {
        int32_t dst = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_PTR_LOAD_I32, dst, ptr_slot, 0);
        *kind = SLOT_I32;
        return dst;
    }
    if (cold_pointer_element_is(elem, "int64") ||
        cold_pointer_element_is(elem, "uint64")) {
        int32_t dst = body_slot(body, SLOT_I64, 8);
        body_op(body, BODY_OP_PTR_LOAD_I64, dst, ptr_slot, 0);
        *kind = SLOT_I64;
        return dst;
    }
    int32_t dst = body_slot(body, SLOT_PTR, 8);
    body_slot_set_type(body, dst, cold_cstr_span("ptr"));
    body_op(body, BODY_OP_PTR_LOAD_I64, dst, ptr_slot, 0);
    *kind = SLOT_PTR;
    return dst;
}

static void cold_emit_pointer_store(Parser *parser, BodyIR *body,
                                    int32_t ptr_slot, int32_t value_slot,
                                    int32_t *value_kind) {
    int32_t ptr_kind = body->slot_kind[ptr_slot];
    if (!cold_kind_is_pointer_like(ptr_kind)) die("pointer store expects pointer slot");
    Span elem = cold_pointer_element_type(parser->symbols, body->slot_type[ptr_slot]);
    if (cold_pointer_element_is(elem, "uint8") ||
        cold_pointer_element_is(elem, "int8") ||
        cold_pointer_element_is(elem, "char")) {
        if (*value_kind != SLOT_I32 && *value_kind != SLOT_I64)
            die("uint8 pointer store value must be integer");
        int32_t len_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, len_slot, 1, 0);
        int32_t dst = body_slot(body, SLOT_I32, 4);
        body_op3(body, BODY_OP_SET_RAW, dst, ptr_slot, value_slot, len_slot);
        return;
    }
    if (cold_pointer_element_is(elem, "int32") ||
        cold_pointer_element_is(elem, "int") ||
        cold_pointer_element_is(elem, "uint32") ||
        cold_pointer_element_is(elem, "bool")) {
        value_slot = cold_materialize_i32_ref(body, value_slot, value_kind);
        if (*value_kind != SLOT_I32) die("int32 pointer store value must be int32");
        body_op(body, BODY_OP_PTR_STORE_I32, ptr_slot, value_slot, 0);
        return;
    }
    if (*value_kind == SLOT_I32 || *value_kind == SLOT_I64_REF ||
        *value_kind == SLOT_OPAQUE || *value_kind == SLOT_OPAQUE_REF ||
        *value_kind == SLOT_PTR) {
        value_slot = cold_materialize_i64_value(body, value_slot, value_kind);
    }
    if (*value_kind != SLOT_I64 && *value_kind != SLOT_PTR)
        die("pointer store value must be pointer-sized");
    body_op(body, BODY_OP_PTR_STORE_I64, ptr_slot, value_slot, 0);
}

static int32_t cold_parse_pointer_deref_expr(Parser *parser, BodyIR *body,
                                             Locals *locals, Span ptr_expr,
                                             int32_t *kind) {
    Parser ptr_parser = parser_child(parser, ptr_expr);
    int32_t ptr_kind = SLOT_PTR;
    int32_t ptr_slot = parse_expr(&ptr_parser, body, locals, &ptr_kind);
    if (ptr_kind != SLOT_PTR && ptr_kind != SLOT_OPAQUE &&
        ptr_kind != SLOT_I64 && ptr_kind != SLOT_I64_REF &&
        ptr_kind != SLOT_OPAQUE_REF) {
        die("pointer deref expects pointer expression");
    }
    return cold_emit_pointer_load(parser, body, ptr_slot, kind);
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

int32_t cold_parser_default_slot(BodyIR *body, Symbols *symbols, Span type, int32_t *kind_out) {
    int32_t kind = cold_parser_slot_kind_from_type(symbols, type);
    int32_t size = cold_parser_slot_size_from_type(symbols, type, kind);
    int32_t slot = body_slot(body, kind, size);
    if (kind == SLOT_SEQ_OPAQUE)
        body_slot_set_seq_opaque_type(body, symbols, slot, type);
    else
        body_slot_set_type(body, slot, span_trim(type));
    if (kind == SLOT_ARRAY_I32) {
        int32_t len = 0;
        if (!cold_parse_i32_array_type(type, &len)) {
            (void)cold_parse_uint8_fixed_array_type(symbols, type, &len);
        }
        body_slot_set_array_len(body, slot, len);
    }
    if (kind == SLOT_I32) {
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
    } else if (kind == SLOT_I64) {
        body_op(body, BODY_OP_I64_CONST, slot, 0, 0);
    } else {
        body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
    }
    if (kind_out) *kind_out = kind;
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
    } else if (body->return_kind == SLOT_I64 &&
               (kind == SLOT_I32 || kind == SLOT_I64_REF)) {
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

void parse_condition_span(Parser *owner, BodyIR *body, Locals *locals,
                                 Span condition, int32_t block,
                                 int32_t true_block, int32_t false_block);
int32_t body_branch_to(BodyIR *body, int32_t block, int32_t target_block);

bool parser_take_until_top_level_else(Parser *parser, Span *then_out) {
    parser_inline_ws(parser);
    int32_t start = parser->pos;
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
        if (c == '\n' || c == '\r') return false;
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
        if (c == '(' || c == '[' || c == '{') {
            depth++;
            parser->pos++;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            if (depth <= 0) return false;
            depth--;
            parser->pos++;
            continue;
        }
        if (depth == 0 && parser->pos + 4 <= parser->source.len &&
            memcmp(parser->source.ptr + parser->pos, "else", 4) == 0) {
            uint8_t before = parser->pos > start ? parser->source.ptr[parser->pos - 1] : ' ';
            uint8_t after = parser->pos + 4 < parser->source.len ? parser->source.ptr[parser->pos + 4] : ' ';
            bool before_ok = before == ' ' || before == '\t';
            bool after_ok = after == ':' || after == ' ' || after == '\t';
            if (before_ok && after_ok) {
                *then_out = span_trim(span_sub(parser->source, start, parser->pos));
                return then_out->len > 0;
            }
        }
        parser->pos++;
    }
    return false;
}

Span parser_take_inline_rest_of_line(Parser *parser) {
    parser_inline_ws(parser);
    int32_t start = parser->pos;
    int32_t end = parser->pos;
    while (end < parser->source.len &&
           parser->source.ptr[end] != '\n' &&
           parser->source.ptr[end] != '\r') {
        end++;
    }
    parser->pos = end;
    return span_trim(span_sub(parser->source, start, end));
}

Span parser_take_balanced_rest_of_line(Parser *parser) {
    parser_inline_ws(parser);
    int32_t start = parser->pos;
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
        if (c == '\n' || c == '\r') break;
        if (c == '"') { in_string = true; parser->pos++; continue; }
        if (c == '\'') { in_char = true; parser->pos++; continue; }
        if (c == '(' || c == '[' || c == '{') { depth++; parser->pos++; continue; }
        if (c == ')' || c == ']' || c == '}') {
            if (depth <= 0) break;
            depth--;
            parser->pos++;
            continue;
        }
        parser->pos++;
    }
    return span_trim(span_sub(parser->source, start, parser->pos));
}

void cold_store_value_into_slot(BodyIR *body, int32_t dst, int32_t dst_kind,
                                       int32_t src, int32_t src_kind) {
    if (dst_kind == SLOT_I32) {
        src = cold_materialize_i32_ref(body, src, &src_kind);
        if (src_kind != SLOT_I32) die("inline if int32 branch kind mismatch");
        body_op(body, BODY_OP_COPY_I32, dst, src, 0);
        return;
    }
    if (dst_kind == SLOT_I64) {
        if (src_kind == SLOT_I32 || src_kind == SLOT_I64_REF) {
            src = cold_materialize_i64_value(body, src, &src_kind);
        }
        if (src_kind != SLOT_I64) die("inline if int64 branch kind mismatch");
        body_op(body, BODY_OP_COPY_I64, dst, src, 0);
        return;
    }
    if (dst_kind == SLOT_PTR) {
        if (src_kind == SLOT_STR && body_slot_is_str_literal(body, src)) {
            src = cold_materialize_str_data_ptr(body, src, src_kind);
            src_kind = SLOT_PTR;
        }
        if (src_kind != SLOT_PTR && src_kind != SLOT_OPAQUE &&
            src_kind != SLOT_OBJECT_REF && src_kind != SLOT_OPAQUE_REF) {
            die("inline if ptr branch kind mismatch");
        }
        body_op(body, BODY_OP_COPY_I64, dst, src, 0);
        return;
    }
    if (dst_kind == SLOT_STR && src_kind == SLOT_STR_REF) {
        src = cold_materialize_fmt_str(body, src, src_kind);
        src_kind = SLOT_STR;
    }
    /* Handle compatible type conversions for composite/ref types */
    if (dst_kind == SLOT_OBJECT_REF && (src_kind == SLOT_OBJECT || src_kind == SLOT_OBJECT_REF)) {
        int32_t copy_size = body->slot_size[src];
        body_op3(body, BODY_OP_PAYLOAD_STORE, dst, src, 0, copy_size);
        return;
    }
    if (dst_kind == SLOT_OBJECT && (src_kind == SLOT_OBJECT || src_kind == SLOT_OBJECT_REF)) {
        int32_t copy_size = body->slot_size[src];
        body_op3(body, BODY_OP_PAYLOAD_STORE, dst, src, 0, copy_size);
        return;
    }
    if (dst_kind == SLOT_SEQ_I32_REF && src_kind == SLOT_SEQ_I32) {
        body_op3(body, BODY_OP_PAYLOAD_STORE, dst, src, 0, body->slot_size[src]);
        return;
    }
    if (dst_kind == SLOT_SEQ_STR_REF && src_kind == SLOT_SEQ_STR) {
        body_op3(body, BODY_OP_PAYLOAD_STORE, dst, src, 0, body->slot_size[src]);
        return;
    }
    if (dst_kind == SLOT_SEQ_OPAQUE_REF && src_kind == SLOT_SEQ_OPAQUE) {
        body_op3(body, BODY_OP_PAYLOAD_STORE, dst, src, 0, body->slot_size[src]);
        return;
    }
    /* SLOT_OPAQUE: compatible with pointer-like types (reverse of SLOT_PTR handler) */
    if (dst_kind == SLOT_OPAQUE) {
        if (src_kind == SLOT_STR && body_slot_is_str_literal(body, src)) {
            src = cold_materialize_str_data_ptr(body, src, src_kind);
            src_kind = SLOT_PTR;
        }
        if (src_kind != SLOT_OPAQUE && src_kind != SLOT_PTR &&
            src_kind != SLOT_OPAQUE_REF && src_kind != SLOT_OBJECT_REF) {
            die("inline if opaque branch kind mismatch");
        }
        body_op(body, BODY_OP_COPY_I64, dst, src, 0);
        return;
    }
    /* SLOT_OPAQUE_REF: compatible with pointer/opaque types */
    if (dst_kind == SLOT_OPAQUE_REF &&
        (src_kind == SLOT_PTR || src_kind == SLOT_OPAQUE || src_kind == SLOT_OPAQUE_REF ||
         src_kind == SLOT_OBJECT_REF)) {
        body_op(body, BODY_OP_COPY_I64, dst, src, 0);
        return;
    }
    /* I32_REF dst with I32 src: store int32 through reference */
    if (dst_kind == SLOT_I32_REF && src_kind == SLOT_I32) {
        body_op(body, BODY_OP_I32_REF_STORE, dst, src, 0);
        return;
    }
    /* I64_REF dst with I64/I32/PTR src: store through reference */
    if (dst_kind == SLOT_I64_REF && (src_kind == SLOT_I64 || src_kind == SLOT_I32 || src_kind == SLOT_PTR)) {
        src = cold_materialize_i64_value(body, src, &src_kind);
        body_op(body, BODY_OP_PTR_STORE_I64, dst, src, 0);
        return;
    }
    /* STR_REF dst with STR src: store string through reference */
    if (dst_kind == SLOT_STR_REF && src_kind == SLOT_STR) {
        body_op3(body, BODY_OP_PAYLOAD_STORE, dst, src, 0, body->slot_size[src]);
        return;
    }
    if (src_kind != dst_kind) {
        if (ColdErrorRecoveryEnabled) {
            body_op(body, BODY_OP_COPY_COMPOSITE, dst, src, 0);
            return;
        }
        fprintf(stderr,
                "cheng_cold: value store kind mismatch dst_kind=%d src_kind=%d dst_slot=%d src_slot=%d body=%.*s\n",
                dst_kind, src_kind, dst, src,
                body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
                body && body->debug_name.len > 0 ? body->debug_name.ptr : (uint8_t *)"");
        die("inline if branch kind mismatch");
    }
    body_op(body, BODY_OP_COPY_COMPOSITE, dst, src, 0);
}

int32_t parse_inline_if_let_binding(Parser *parser, BodyIR *body, Locals *locals,
                                           int32_t block, Span name, Span declared_type) {
    int32_t saved = parser->pos;
    if (!parser_take(parser, "if")) {
        parser->pos = saved;
        return -1;
    }
    Span condition = parser_take_until_top_level_char(parser, ':', "expected : in inline if");
    if (!parser_take(parser, ":")) die("expected : in inline if");
    Span then_expr = {0};
    if (!parser_take_until_top_level_else(parser, &then_expr)) {
        parser->pos = saved;
        return -1;
    }
    if (!parser_take(parser, "else")) die("expected else in inline if");
    if (span_eq(parser_peek(parser), "elif")) { parser->pos = saved; return -1; }
    if (!parser_take(parser, ":")) die("expected : after inline if else");
    Span else_expr = parser_take_balanced_rest_of_line(parser);
    if (else_expr.len <= 0) die("inline if else expression is empty");

    int32_t true_block = body_block(body);
    int32_t false_block = body_block(body);
    int32_t join_block = body_block(body);
    parse_condition_span(parser, body, locals, condition, block, true_block, false_block);

    body_reopen_block(body, true_block);
    int32_t then_kind = SLOT_I32;
    int32_t then_slot = parse_expr_from_span(parser, body, locals, then_expr, &then_kind,
                                             "unsupported inline if then expression");
    int32_t result_kind = declared_type.len > 0
                              ? cold_parser_slot_kind_from_type(parser->symbols, declared_type)
                              : then_kind;
    int32_t result_size = declared_type.len > 0
                              ? cold_parser_slot_size_from_type(parser->symbols, declared_type, result_kind)
                              : body->slot_size[then_slot];
    int32_t result_slot = body_slot(body, result_kind, result_size);
    if (declared_type.len > 0) {
        body_slot_set_type(body, result_slot, declared_type);
    } else if (body->slot_type[then_slot].len > 0) {
        body_slot_set_type(body, result_slot, body->slot_type[then_slot]);
    } else if (result_kind == SLOT_STR) {
        body_slot_set_type(body, result_slot, cold_cstr_span("str"));
    }
    cold_store_value_into_slot(body, result_slot, result_kind, then_slot, then_kind);
    if (body->block_term[true_block] < 0) body_branch_to(body, true_block, join_block);

    body_reopen_block(body, false_block);
    int32_t else_kind = SLOT_I32;
    int32_t else_slot = parse_expr_from_span(parser, body, locals, else_expr, &else_kind,
                                             "unsupported inline if else expression");
    cold_store_value_into_slot(body, result_slot, result_kind, else_slot, else_kind);
    if (body->block_term[false_block] < 0) body_branch_to(body, false_block, join_block);

    body_reopen_block(body, join_block);
    if (result_kind == SLOT_I32 || result_kind == SLOT_I64 || result_kind == SLOT_F32 || result_kind == SLOT_F64)
        body->slot_no_alias[result_slot] = 1;
    locals_add(locals, name, result_slot, result_kind);
    return join_block;
}

int32_t parse_let_binding(Parser *parser, BodyIR *body, Locals *locals,
                                  int32_t block, bool is_var, int32_t stmt_indent) {
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
        int32_t slot = cold_parser_default_slot(body, parser->symbols, type, &kind);
        if (kind == SLOT_I32 || kind == SLOT_I64 || kind == SLOT_F32 || kind == SLOT_F64)
            body->slot_no_alias[slot] = 1;
        locals_add(locals, name, slot, kind);
        return block;
    }
    (void)parser_token(parser);
    int32_t inline_if_block = parse_inline_if_let_binding(parser, body, locals, block, name, type);
    if (inline_if_block >= 0) return inline_if_block;
    Span init_expr = parser_take_let_initializer_span(parser, stmt_indent,
                                                      "expected let initializer");
    Parser init_parser = parser_child(parser, init_expr);
    int32_t kind = SLOT_I32;
    int32_t slot = -1;
    /* pre-allocate slot for var bindings so they don't alias the initializer */
    int32_t owned_slot = -1;
    if (is_var && type.len > 0) {
        int32_t decl_kind = cold_parser_slot_kind_from_type(parser->symbols, type);
        if (decl_kind == SLOT_I32 || decl_kind == SLOT_I64) {
            owned_slot = body_slot(body, decl_kind, cold_slot_size_for_kind(decl_kind));
            body_slot_set_type(body, owned_slot, type);
        }
    }
    int32_t initializer_block_count = body->block_count;
    if (type.len > 0 && cold_parse_i32_seq_type(type) && span_eq(parser_peek(&init_parser), "[")) {
        slot = parse_i32_seq_literal(&init_parser, body, locals, &kind);
    } else if (type.len > 0) {
        TypeDef *declared_type = symbols_resolve_type(parser->symbols, type);
        Variant *declared_variant = declared_type ? type_find_variant(declared_type, parser_peek(&init_parser)) : 0;
        if (declared_variant) {
            (void)parser_token(&init_parser);
            slot = parse_constructor(&init_parser, body, locals, declared_variant);
            kind = SLOT_VARIANT;
        } else {
            slot = parse_expected_payloadless_variant(&init_parser, body, declared_type, &kind);
            if (slot < 0) {
                slot = parse_expr(&init_parser, body, locals, &kind);
            }
        }
    } else {
        slot = parse_expr(&init_parser, body, locals, &kind);
    }
    if (body->block_count > initializer_block_count) {
        for (int32_t bi = body->block_count - 1; bi >= 0; bi--) {
            if (body->block_term[bi] < 0) {
                block = bi;
                break;
            }
        }
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
    } else if (owned_slot >= 0 && body->slot_kind[owned_slot] == SLOT_OPAQUE && kind == SLOT_PTR) {
        /* OPAQUE variable assigned from PTR value: compatible, just copy the pointer */
        body_op(body, BODY_OP_COPY_I64, owned_slot, slot, 0);
        slot = owned_slot;
        kind = SLOT_OPAQUE;
    } else if (owned_slot >= 0 && body->slot_kind[owned_slot] == SLOT_PTR && kind == SLOT_OPAQUE) {
        /* PTR variable assigned from OPAQUE value: compatible, just copy the pointer */
        body_op(body, BODY_OP_COPY_I64, owned_slot, slot, 0);
        slot = owned_slot;
        kind = SLOT_PTR;
    }
    if (span_eq(parser_peek(&init_parser), "?")) {
        (void)parser_token(&init_parser);
        parser_ws(&init_parser);
        if (init_parser.pos < init_parser.source.len)
            die("trailing tokens after let result unwrap");
        int32_t declared_kind = -1;
        Span declared_type = {0};
        if (type.len > 0) {
            declared_type = span_trim(type);
            declared_kind = cold_parser_slot_kind_from_type(parser->symbols, declared_type);
        }
        return cold_lower_question_result(parser->symbols, body, locals, block, slot,
                                          name, true, declared_kind, declared_type);
    }
    parser_ws(&init_parser);
    if (init_parser.pos < init_parser.source.len)
        die("trailing tokens in let initializer");
    if (type.len > 0) {
            int32_t declared_kind = cold_parser_slot_kind_from_type(parser->symbols, type);
            if (declared_kind == SLOT_I32 && kind == SLOT_I32_REF) {
                slot = cold_materialize_i32_ref(body, slot, &kind);
            }
            if (declared_kind == SLOT_I64 && kind != SLOT_I64) {
                slot = cold_materialize_i64_value(body, slot, &kind);
            }
            if (declared_kind == SLOT_PTR && kind != SLOT_PTR) {
                slot = cold_materialize_ptr_value(body, slot, &kind);
            }
            if (kind != declared_kind) {
                /* Check for compatible type conversions before dying */
                if ((declared_kind == SLOT_OPAQUE && (kind == SLOT_PTR || kind == SLOT_OPAQUE_REF || kind == SLOT_OBJECT_REF)) ||
                    (declared_kind == SLOT_PTR && kind == SLOT_OPAQUE) ||
                    (declared_kind == SLOT_OPAQUE_REF && kind == SLOT_PTR)) {
                    /* Compatible pointer-like types: update kind to match declared */
                    kind = declared_kind;
                    int32_t dsz = cold_slot_size_for_kind(declared_kind);
                    int32_t owned = body_slot(body, declared_kind, dsz);
                    body_slot_set_type(body, owned, type);
                    body_op(body, BODY_OP_COPY_I64, owned, slot, 0);
                    slot = owned;
                } else if (ColdErrorRecoveryEnabled) {
                    /* Lenient conversion: force kind to declared */
                    kind = declared_kind;
                    int32_t dsz = cold_slot_size_for_kind(declared_kind);
                    int32_t owned = body_slot(body, declared_kind, dsz);
                    body_slot_set_type(body, owned, type);
                    body_op(body, BODY_OP_COPY_COMPOSITE, owned, slot, 0);
                    slot = owned;
                } else {
                    fprintf(stderr,
                            "cheng_cold: typed let kind mismatch name=%.*s declared=%.*s declared_kind=%d actual_kind=%d body=%.*s\n",
                            (int)name.len, name.ptr,
                            (int)type.len, type.ptr,
                            declared_kind, kind,
                            body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
                            body && body->debug_name.len > 0 ? body->debug_name.ptr : (uint8_t *)"");
                    die("typed let initializer kind mismatch");
                }
            }
            if (kind == SLOT_ARRAY_I32) {
                int32_t declared_len = 0;
                if (!cold_parse_fixed_array_type(parser->symbols, type, &declared_len)) declared_len = 0;
                if (declared_len > 0 && body->slot_aux[slot] != declared_len)
                    die("typed fixed array let length mismatch");
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
        GlobalDef *global = parser_find_global(parser, name);
        if (global) local = locals_add_global_shadow(parser, body, locals, name, global);
    }
    if (!local) {
        int32_t ls = parser->pos;
        while (ls > 0 && parser->source.ptr[ls - 1] != '\n') ls--;
        int32_t le = parser->pos;
        while (le < parser->source.len && parser->source.ptr[le] != '\n') le++;
        fprintf(stderr, "cheng_cold: assignment target is not local/global: %.*s body=%.*s pos=%d\n",
                (int)name.len, name.ptr,
                body && body->debug_name.len > 0 ? (int)body->debug_name.len : 0,
                body && body->debug_name.len > 0 ? body->debug_name.ptr : (uint8_t *)"",
                parser->pos);
        fprintf(stderr, "cheng_cold: assignment line: %.*s\n", le - ls, parser->source.ptr + ls);
        die("assignment target must be local or global");
    }
    if (!parser_take(parser, "=")) die("expected = in assignment");
    Span value_expr = parser_take_balanced_statement_expr_span(parser, "expected assignment value");
    Parser value_parser = parser_child(parser, value_expr);
    int32_t kind = SLOT_I32;
    int32_t slot = -1;
    if (cold_slot_kind_is_sequence_target(local->kind) &&
        parser_peek_empty_list_literal(&value_parser)) {
        slot = parse_empty_sequence_for_target(&value_parser, body, local->kind,
                                               body->slot_type[local->slot], &kind);
    } else {
        slot = parse_expr(&value_parser, body, locals, &kind);
    }
    parser_ws(&value_parser);
    if (value_parser.pos < value_parser.source.len)
        die("trailing tokens in assignment value");
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
    if (local->kind == SLOT_I64_REF) {
        if (kind == SLOT_I32 || kind == SLOT_I64_REF)
            slot = cold_materialize_i64_value(body, slot, &kind);
        if (kind != SLOT_I64) die("cold assignment value must be int64");
        body_op(body, BODY_OP_PTR_STORE_I64, local->slot, slot, 0);
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
    if (local->kind == SLOT_OBJECT_REF) {
        if (kind != SLOT_OBJECT && kind != SLOT_OBJECT_REF &&
            kind != SLOT_OPAQUE && kind != SLOT_OPAQUE_REF) {
            int32_t ls = parser->pos;
            while (ls > 0 && parser->source.ptr[ls - 1] != '\n') ls--;
            int32_t le = parser->pos;
            while (le < parser->source.len && parser->source.ptr[le] != '\n') le++;
            fprintf(stderr, "cheng_cold: object ref assignment mismatch target=%.*s local_kind=%d value_kind=%d line=%.*s\n",
                    (int)name.len, name.ptr, local->kind, kind,
                    le - ls, parser->source.ptr + ls);
            die("object ref assignment value kind mismatch");
        }
        int32_t copy_size = body->slot_size[slot];
        if (kind == SLOT_OBJECT_REF) {
            ObjectDef *src_obj = symbols_resolve_object(parser->symbols,
                cold_type_strip_var(body->slot_type[slot], 0));
            if (!src_obj) die("object ref assignment source layout missing");
            copy_size = symbols_object_slot_size(src_obj);
        }
        body_op3(body, BODY_OP_PAYLOAD_STORE, local->slot, slot, 0, copy_size);
        return;
    }
    if (local->kind == SLOT_OBJECT) {
        if (kind != SLOT_OBJECT && kind != SLOT_OBJECT_REF) die("object assignment value kind mismatch");
        int32_t copy_size = body->slot_size[slot];
        if (kind == SLOT_OBJECT_REF) {
            ObjectDef *src_obj = symbols_resolve_object(parser->symbols,
                cold_type_strip_var(body->slot_type[slot], 0));
            if (!src_obj) die("object assignment source layout missing");
            copy_size = symbols_object_slot_size(src_obj);
        }
        if (copy_size > body->slot_size[local->slot]) die("object assignment value size mismatch");
        body_op3(body, BODY_OP_PAYLOAD_STORE, local->slot, slot, 0, copy_size);
        return;
    }
    if (local->kind == SLOT_VARIANT ||
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
        local->kind != SLOT_PTR && local->kind != SLOT_STR &&
        local->kind != SLOT_STR_REF &&
        local->kind != SLOT_SEQ_I32_REF && local->kind != SLOT_SEQ_STR_REF &&
        local->kind != SLOT_SEQ_OPAQUE_REF) {
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
    if (!object) {
        /* Fallback: search all objects for one containing this field (same as parse_postfix) */
        for (int32_t oi = 0; oi < parser->symbols->object_count; oi++) {
            ObjectDef *candidate = &parser->symbols->objects[oi];
            if (object_find_field(candidate, field_name)) {
                object = candidate;
                break;
            }
        }
    }
    if (!object) {
        /* Best-effort: if we can't resolve the type, skip the assignment instead of dying */
        if (parser_take(parser, "=")) {
            int32_t skip_kind = SLOT_I32;
            (void)parse_expr(parser, body, locals, &skip_kind);
        }
        return block;
    }
    ObjectField *field = object_find_field(object, field_name);
    if (!field) {
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
    if (!local) {
        GlobalDef *global = parser_find_global(owner, name);
        if (global) local = locals_add_global_shadow(owner, body, locals, name, global);
    }
    if (!local) {
        fprintf(stderr, "cheng_cold: add target is not local/global: %.*s\n",
                (int)target.len, target.ptr);
        die("add target must be a local sequence");
    }
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
        kind != SLOT_SEQ_OPAQUE && kind != SLOT_SEQ_OPAQUE_REF && kind != SLOT_ARRAY_I32) {
        fprintf(stderr,
                "cheng_cold: add target kind=%d type=%.*s target=%.*s\n",
                kind,
                (int)body->slot_type[slot].len, body->slot_type[slot].ptr,
                (int)target.len, target.ptr);
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
        value_slot = cold_materialize_i32_ref(body, value_slot, &value_kind);
        if (value_kind != SLOT_I32) {
            fprintf(stderr,
                    "cheng_cold: add int32[] target=%.*s value_kind=%d value_type=%.*s\n",
                    (int)target.len, target.ptr, value_kind,
                    (value_slot >= 0 && value_slot < body->slot_count)
                        ? (int)body->slot_type[value_slot].len : 0,
                    (value_slot >= 0 && value_slot < body->slot_count)
                        ? (const char *)body->slot_type[value_slot].ptr : "");
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
    index_slot = cold_materialize_i32_ref(body, index_slot, &index_kind);
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
        left = cold_materialize_i32_ref(body, left, &left_kind);
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
    if (cold_slot_kind_is_str_like(left_kind) || cold_slot_kind_is_str_like(right_kind)) {
        if (!cold_slot_kind_is_str_like(left_kind) || !cold_slot_kind_is_str_like(right_kind)) {
            die("condition str comparison operands must be str");
        }
        if (cond != COND_EQ && cond != COND_NE) return;
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
    bool has_false_suite = false;
    if (branch_indent == stmt_indent && span_eq(branch_token, "elif")) {
        parser_take(parser, "elif");
        has_false_suite = true;
        false_end = parse_if(parser, body, locals, false_block, stmt_indent, loop);
    } else {
        if (branch_indent == stmt_indent && span_eq(branch_token, "else")) {
            parser_take(parser, "else");
            if (!parser_take(parser, ":")) return block; /* skip malformed else */;
            has_false_suite = true;
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

    if (!has_false_suite && join_block < 0) {
        return false_block;
    }
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
    start_slot = cold_materialize_i32_ref(body, start_slot, &start_kind);
    if (start_kind != SLOT_I32) return block; /* skip non-int32 for start */;
    if (!parser_take(parser, ".") || !parser_take(parser, ".")) {
        /* range() call syntax or malformed: skip : and indented body lines */
        parser_line(parser);
        int32_t skip_indent = parser_next_indent(parser);
        for (;;) {
            if (parser->pos >= parser->source.len) break;
            int32_t ni = parser_next_indent(parser);
            if (ni < 0 || ni < skip_indent) break;
            parser_line(parser);
        }
        return block;
    }
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
    end_slot = cold_materialize_i32_ref(body, end_slot, &end_kind);
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
    if (span_eq(kw, "*")) {
        int32_t ptr_kind = SLOT_PTR;
        int32_t ptr_slot = -1;
        Span ptr_expr = {0};
        if (span_eq(parser_peek(parser), "(")) {
            (void)parser_token(parser);
            ptr_expr = parser_take_until_top_level_char(parser, ')',
                                                       "expected ) in pointer store");
            if (!parser_take(parser, ")")) die("expected ) in pointer store");
            Parser ptr_parser = parser_child(parser, ptr_expr);
            ptr_slot = parse_expr(&ptr_parser, body, locals, &ptr_kind);
        } else {
            ptr_expr = parser_take_until_top_level_char(parser, '=',
                                                       "expected = in pointer store");
            Parser ptr_parser = parser_child(parser, ptr_expr);
            ptr_slot = parse_expr(&ptr_parser, body, locals, &ptr_kind);
        }
        if (!parser_take(parser, "=")) die("expected = in pointer store");
        if (ptr_kind != SLOT_PTR && ptr_kind != SLOT_OPAQUE &&
            ptr_kind != SLOT_I64 && ptr_kind != SLOT_I64_REF &&
            ptr_kind != SLOT_OPAQUE_REF) {
            die("pointer store address must be pointer");
        }
        Span value_expr = parser_take_balanced_statement_expr_span(parser,
                                                                   "expected pointer store value");
        Parser value_parser = parser_child(parser, value_expr);
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(&value_parser, body, locals, &value_kind);
        parser_ws(&value_parser);
        if (value_parser.pos < value_parser.source.len)
            die("trailing tokens in pointer store value");
        cold_emit_pointer_store(parser, body, ptr_slot, value_slot, &value_kind);
        return block;
    } else if (span_eq(kw, "let") || span_eq(kw, "var")) {
        return parse_let_binding(parser, body, locals, block, span_eq(kw, "var"), stmt_indent);
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
        if (parser_next_is_object_constructor(parser)) {
            Span object_slot_type = {0};
            ObjectDef *obj = parser_resolve_object_constructor(parser, call_name,
                                                               &object_slot_type);
            if (obj && !obj->is_ref) {
                int32_t result_slot = parse_object_constructor_typed(parser, body, locals,
                                                                     obj, object_slot_type);
                if (!cold_return_span_is_void(body->return_type) &&
                    span_same(span_trim(body->return_type), span_trim(object_slot_type))) {
                    int32_t term = body_term(body, BODY_TERM_RET, result_slot, -1, 0, -1, -1);
                    body_end_block(body, block, term);
                }
                return block;
            }
        }
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
                body_slot_set_type(body, ref_slot, fld->type_name);
                if (fld->kind == SLOT_ARRAY_I32) body_slot_set_array_len(body, ref_slot, fld->array_len);
                if (fld->kind == SLOT_SEQ_OPAQUE)
                    body_slot_set_seq_opaque_type(body, parser->symbols, ref_slot, fld->type_name);
                body_op3(body, BODY_OP_FIELD_REF, ref_slot, base->slot, fld->offset, 0);
                if (fld->kind == SLOT_ARRAY_I32)
                    body_op3(body, BODY_OP_ARRAY_I32_INDEX_STORE, vs, ref_slot, is, 0);
                else if (fld->kind == SLOT_SEQ_I32 || fld->kind == SLOT_SEQ_I32_REF)
                    body_op3(body, BODY_OP_SEQ_I32_INDEX_STORE, vs, ref_slot, is, 0);
                else if (fld->kind == SLOT_SEQ_STR) {
                    if (vk != SLOT_STR && vk != SLOT_STR_REF) die("str[] index store value must be str");
                    body_op3(body, BODY_OP_SEQ_OPAQUE_INDEX_STORE, vs, ref_slot, is, COLD_STR_SLOT_SIZE);
                }
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
        /* not followed by = or [ at first level: check multi-level field assign or field-read */
        parser->pos = saved;
        Local *expr_local = locals_find(locals, kw);
        if (!expr_local) return block;
        if (is_assign) {
            /* Multi-level field assignment: a.b.c = expr */
            int32_t cur_slot = expr_local->slot;
            int32_t total_offset = 0;
            Span obj_type = cold_type_strip_var(body->slot_type[cur_slot], 0);
            ObjectDef *obj = obj_type.len > 0 ? symbols_resolve_object(parser->symbols, obj_type) : 0;
            if (!obj && obj_type.len > 0) {
                for (int32_t oi = 0; oi < parser->symbols->object_count; oi++) {
                    ObjectDef *co = &parser->symbols->objects[oi];
                    if (co->name.len > obj_type.len + 1 &&
                        co->name.ptr[co->name.len - obj_type.len - 1] == '.' &&
                        memcmp(co->name.ptr + co->name.len - obj_type.len, obj_type.ptr, (size_t)obj_type.len) == 0) {
                        obj = co; break;
                    }
                }
            }
            while (span_eq(parser_peek(parser), ".")) {
                (void)parser_token(parser); /* . */
                Span fname = parser_token(parser);
                if (fname.len <= 0) die("expected field name");
                if (!obj) { int32_t vk; (void)parse_expr(parser, body, locals, &vk); break; }
                ObjectField *fld = object_find_field(obj, fname);
                if (!fld) { int32_t vk; (void)parse_expr(parser, body, locals, &vk); break; }
                parser_inline_ws(parser);
                if (parser->pos < parser->source.len && parser->source.ptr[parser->pos] == '=') {
                    /* Last field: parse RHS and store directly at cumulative offset */
                    parser->pos++;
                    int32_t vk = SLOT_I32;
                    int32_t vs = parse_expr(parser, body, locals, &vk);
                    body_op3(body, BODY_OP_PAYLOAD_STORE, cur_slot, vs, total_offset + fld->offset, fld->size);
                    break;
                }
                /* Intermediate field: accumulate offset, resolve type for next iteration */
                total_offset += fld->offset;
                Span next_type = fld->type_name;
                obj = next_type.len > 0 ? symbols_resolve_object(parser->symbols, next_type) : 0;
                if (!obj && next_type.len > 0) {
                    for (int32_t oi = 0; oi < parser->symbols->object_count; oi++) {
                        ObjectDef *co = &parser->symbols->objects[oi];
                        if (co->name.len > next_type.len + 1 &&
                            co->name.ptr[co->name.len - next_type.len - 1] == '.' &&
                            memcmp(co->name.ptr + co->name.len - next_type.len, next_type.ptr, (size_t)next_type.len) == 0) {
                            obj = co; break;
                        }
                    }
                }
            }
        } else {
            int32_t expr_kind = expr_local->kind;
            (void)parse_postfix(parser, body, locals, expr_local->slot, &expr_kind);
        }
        return block;
    } else if (span_eq(parser_peek(parser), "[")) {
        /* Generic function call: name[T](args)
           Check if kw[…] vanishes into a call before trying index. */
        if (!locals_find(locals, kw)) {
            int32_t saved = parser->pos;
            parser_skip_balanced(parser, "[", "]");
            parser_inline_ws(parser);
            bool is_gen_call = span_eq(parser_peek(parser), "(");
            parser->pos = saved;
            if (is_gen_call) {
                Span call_name = parser_take_qualified_after_first(parser, kw);
                if (parser_next_is_object_constructor(parser)) {
                    Span object_slot_type = {0};
                    ObjectDef *obj = parser_resolve_object_constructor(parser, call_name,
                                                                       &object_slot_type);
                    if (obj && !obj->is_ref) {
                        int32_t result_slot = parse_object_constructor_typed(parser, body, locals,
                                                                             obj, object_slot_type);
                        if (!cold_return_span_is_void(body->return_type) &&
                            span_same(span_trim(body->return_type), span_trim(object_slot_type))) {
                            int32_t term = body_term(body, BODY_TERM_RET, result_slot, -1, 0, -1, -1);
                            body_end_block(body, block, term);
                        }
                        return block;
                    }
                }
                int32_t result_kind = SLOT_I32;
                int32_t result_slot = parse_call_after_name(parser, body, locals, call_name, &result_kind);
                if (span_eq(parser_peek(parser), "?")) {
                    (void)parser_token(parser);
                    return cold_lower_question_result(parser->symbols, body, locals, block, result_slot,
                                                      (Span){0}, false, -1, (Span){0});
                }
                return block;
            }
        }
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
            else if (base->kind == SLOT_SEQ_STR || base->kind == SLOT_SEQ_STR_REF) {
                if (vk != SLOT_STR && vk != SLOT_STR_REF) die("str[] index store value must be str");
                body_op3(body, BODY_OP_SEQ_OPAQUE_INDEX_STORE, vs, base->slot, is, COLD_STR_SLOT_SIZE);
            }
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
        /* Check for ident[index].field... = value (nested index + field assign) */
        if (is_assign && !parser_next_token_is_assign(parser) && span_eq(parser_peek(parser), ".")) {
            parser->pos = saved;
            int32_t ek = SLOT_I32;
            Local *lcl = locals_find(locals, kw);
            if (lcl) (void)parse_postfix(parser, body, locals, lcl->slot, &ek);
            /* After parse_postfix, check for = and consume RHS */
            int32_t vk = SLOT_I32;
            if (parser_next_token_is_assign(parser)) {
                (void)parser_token(parser);
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
    Span fn_generic_names[4];
    int32_t fn_generic_count = 0;
    if (span_eq(parser_peek(parser), "[")) {
        (void)parser_token(parser);
        while (!span_eq(parser_peek(parser), "]")) {
            Span generic_name = parser_token(parser);
            if (generic_name.len == 0) die("unterminated generic params");
            if (span_eq(generic_name, ",")) continue;
            if (fn_generic_count >= 4) die("too many function generic params");
            fn_generic_names[fn_generic_count++] = generic_name;
            if (span_eq(parser_peek(parser), ",")) (void)parser_token(parser);
        }
        if (!parser_take(parser, "]")) die("expected ] after function generic params");
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
    bool param_has_default[COLD_MAX_I32_PARAMS] = {0};
    int32_t param_default_value[COLD_MAX_I32_PARAMS] = {0};
    while (!span_eq(parser_peek(parser), ")")) {
        Span param = parser_token(parser);
        if (param.len == 0) die("unterminated params");
        if (arity >= COLD_MAX_I32_PARAMS) die("cold prototype supports at most sixteen params");
        /* Handle 'var' prefix for mutable parameters (var name: Type) */
        bool is_var_param = span_eq(param, "var");
        if (is_var_param) {
            param = parser_token(parser);
            if (param.len == 0) die("expected param name after var");
        }
        param_names[arity] = param;
        param_types[arity] = (Span){0};
        param_kinds[arity] = SLOT_I32;
        param_sizes[arity] = 4;
        if (span_eq(parser_peek(parser), ":")) {
            (void)parser_token(parser);
            Span param_type = parser_scope_type(parser, parser_take_type_span(parser));
            if (is_var_param) {
                param_type = cold_arena_join3(parser->arena, cold_cstr_span("var "), "", param_type);
            }
            param_types[arity] = param_type;
            bool generic_param = false;
            bool generic_param_is_var = false;
            Span param_stripped = span_trim(cold_type_strip_var(param_type, &generic_param_is_var));
            for (int32_t gi = 0; gi < fn_generic_count; gi++) {
                if (span_same(param_stripped, fn_generic_names[gi])) {
                    generic_param = true;
                    break;
                }
            }
            if (generic_param) {
                param_kinds[arity] = generic_param_is_var ? SLOT_I32_REF : SLOT_I32;
                param_sizes[arity] = generic_param_is_var ? 8 : 4;
            } else {
                param_kinds[arity] = cold_parser_slot_kind_from_type(parser->symbols, param_type);
                param_sizes[arity] = cold_param_size_from_type(parser->symbols, param_type, param_kinds[arity]);
            }
        }
        /* Check for default value: param: Type = value */
        if (span_eq(parser_peek(parser), "=")) {
            (void)parser_token(parser);
            Span def_span = parser_token(parser);
            if (span_is_i32(def_span)) {
                param_has_default[arity] = true;
                param_default_value[arity] = span_i32(def_span);
            }
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
        /* Copy default param values to FnDef */
        if (symbol_index >= 0 && symbol_index < parser->symbols->function_count) {
            symbols_set_fn_generics(parser->symbols, symbol_index,
                                    fn_generic_names, fn_generic_count);
            symbols_set_fn_param_types(parser->symbols, symbol_index, param_types, arity);
            for (int32_t di = 0; di < arity; di++) {
                parser->symbols->functions[symbol_index].param_has_default[di] = param_has_default[di];
                parser->symbols->functions[symbol_index].param_default_value[di] = param_default_value[di];
            }
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
    /* Copy default param values to FnDef */
    if (symbol_index >= 0 && symbol_index < parser->symbols->function_count) {
        symbols_set_fn_generics(parser->symbols, symbol_index,
                                fn_generic_names, fn_generic_count);
        symbols_set_fn_param_types(parser->symbols, symbol_index, param_types, arity);
        for (int32_t di = 0; di < arity; di++) {
            parser->symbols->functions[symbol_index].param_has_default[di] = param_has_default[di];
            parser->symbols->functions[symbol_index].param_default_value[di] = param_default_value[di];
        }
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
    /* Register generic type parameters as locals so they resolve in expressions */
    for (int32_t gi = 0; gi < fn_generic_count; gi++) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        locals_add(&locals, fn_generic_names[gi], slot, SLOT_I32);
    }
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

    if (cold_same_line_has_content(parser)) {
        /* Inline body: body on same line as '='. Parse expression or statement
           and stop — do not use parse_statements_until which would consume
           the next top-level declaration at the same indent level. */
        Span first = parser_peek(parser);
        if (span_eq(first, "return") || span_eq(first, "if") ||
            span_eq(first, "while") || span_eq(first, "for") ||
            span_eq(first, "var") || span_eq(first, "let") ||
            span_eq(first, "match") || span_eq(first, "{")) {
            /* Inline statement(s) */
            parse_inline_statements(parser, body, &locals, block, 0);
        } else {
            /* Check for inline assignment: name = expr */
            bool is_assign = false;
            {
                int32_t saved = parser->pos;
                (void)parser_token(parser); /* consume first token */
                if (parser_next_token_is_assign(parser)) is_assign = true;
                parser->pos = saved;
            }
            if (is_assign) {
                parse_inline_statements(parser, body, &locals, block, 0);
            } else {
                /* Bare expression: add implicit return */
                int32_t kind = body->return_kind;
                int32_t slot = parse_expr(parser, body, &locals, &kind);
                if (kind == SLOT_I32) body_op(body, BODY_OP_LOAD_I32, slot, 0, 0);
                int32_t term = body_term(body, BODY_TERM_RET, slot, -1, 0, -1, -1);
                body_end_block(body, block, term);
            }
        }
        /* Implicit return for void inline bodies to ensure block is terminated */
        if (body && body->block_count > 0 && body->block_term[block] < 0) {
            int32_t rs = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_I32_CONST, rs, 0, 0);
            int32_t trm = body_term(body, BODY_TERM_RET, rs, -1, 0, -1, -1);
            body_end_block(body, block, trm);
        }
        /* Advance past current line so main parse loop finds next decl */
        if (parser->pos < parser->source.len) {
            while (parser->pos < parser->source.len &&
                   parser->source.ptr[parser->pos] != '\n') parser->pos++;
            if (parser->pos < parser->source.len) parser->pos++;
        }
    } else {
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
    return body;
}
