#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm_emit_layout.h"

typedef enum {
    ASM_TYPE_OBJECT = 0,
    ASM_TYPE_ALIAS
} AsmTypeKind;

typedef struct {
    AsmTypeLayout layout;
    AsmTypeKind kind;
    const ChengNode *base;
    const ChengNode *rec_list;
    char *field_base;
    AsmFieldLayout *fields;
    size_t field_len;
    size_t field_cap;
    int computing;
    int computed;
} AsmTypeEntry;

typedef struct {
    char *name;
    long long value;
} AsmConstInt;

typedef struct {
    char *value;
    char *label;
} AsmStringEntry;

struct AsmLayout {
    AsmLayoutConfig config;
    AsmTypeEntry *types;
    size_t type_len;
    size_t type_cap;
    AsmDataItem *data;
    size_t data_len;
    size_t data_cap;
    AsmConstInt *const_ints;
    size_t const_int_len;
    size_t const_int_cap;
    AsmStringEntry *strings;
    size_t string_len;
    size_t string_cap;
};

typedef struct {
    int valid;
    int is_bool;
    int64_t value;
} LayoutConstVal;

static char *layout_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static void layout_add_diag(ChengDiagList *diags, const char *filename, const ChengNode *node, const char *msg) {
    if (!diags) {
        return;
    }
    int line = 1;
    int col = 1;
    if (node) {
        line = node->line;
        col = node->col;
    }
    cheng_diag_add(diags, CHENG_SV_ERROR, filename, line, col, msg);
}

static size_t align_up(size_t value, size_t align) {
    if (align == 0) {
        return value;
    }
    size_t rem = value % align;
    if (rem == 0) {
        return value;
    }
    return value + (align - rem);
}

void asm_layout_config_default(AsmLayoutConfig *config, size_t ptr_size) {
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->ptr_size = ptr_size;
    config->ptr_align = ptr_size == 0 ? 1 : ptr_size;
    config->int_size = 4;
    config->int_align = 4;
    config->long_size = 8;
    config->long_align = 8;
    config->bool_size = 1;
    config->bool_align = 1;
    config->char_size = 1;
    config->char_align = 1;
    config->float_size = 4;
    config->float_align = 4;
    config->double_size = 8;
    config->double_align = 8;
}

void asm_layout_init(AsmLayout *layout, const AsmLayoutConfig *config) {
    if (!layout) {
        return;
    }
    memset(layout, 0, sizeof(*layout));
    if (config) {
        layout->config = *config;
    }
}

static void free_fields(AsmTypeEntry *entry) {
    if (!entry || !entry->fields) {
        return;
    }
    for (size_t i = 0; i < entry->field_len; i++) {
        free((char *)entry->fields[i].name);
    }
    free(entry->fields);
    entry->fields = NULL;
    entry->field_len = 0;
    entry->field_cap = 0;
}

void asm_layout_free(AsmLayout *layout) {
    if (!layout) {
        return;
    }
    for (size_t i = 0; i < layout->type_len; i++) {
        AsmTypeEntry *entry = &layout->types[i];
        free((char *)entry->layout.name);
        free(entry->field_base);
        free_fields(entry);
    }
    for (size_t i = 0; i < layout->data_len; i++) {
        AsmDataItem *item = &layout->data[i];
        free((char *)item->name);
        free((char *)item->label);
        free((uint8_t *)item->bytes);
    }
    for (size_t i = 0; i < layout->const_int_len; i++) {
        free(layout->const_ints[i].name);
    }
    for (size_t i = 0; i < layout->string_len; i++) {
        free(layout->strings[i].value);
        free(layout->strings[i].label);
    }
    free(layout->types);
    free(layout->data);
    free(layout->const_ints);
    free(layout->strings);
    memset(layout, 0, sizeof(*layout));
}

AsmLayout *asm_layout_create(const AsmLayoutConfig *config) {
    AsmLayout *layout = (AsmLayout *)malloc(sizeof(*layout));
    if (!layout) {
        return NULL;
    }
    asm_layout_init(layout, config);
    return layout;
}

void asm_layout_destroy(AsmLayout *layout) {
    if (!layout) {
        return;
    }
    asm_layout_free(layout);
    free(layout);
}

static int ensure_type_cap(AsmLayout *layout, size_t extra) {
    size_t need = layout->type_len + extra;
    if (need <= layout->type_cap) {
        return 0;
    }
    size_t next = layout->type_cap == 0 ? 8 : layout->type_cap * 2;
    while (next < need) {
        next *= 2;
    }
    AsmTypeEntry *items = (AsmTypeEntry *)realloc(layout->types, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    layout->types = items;
    layout->type_cap = next;
    return 0;
}

static int ensure_data_cap(AsmLayout *layout, size_t extra) {
    size_t need = layout->data_len + extra;
    if (need <= layout->data_cap) {
        return 0;
    }
    size_t next = layout->data_cap == 0 ? 8 : layout->data_cap * 2;
    while (next < need) {
        next *= 2;
    }
    AsmDataItem *items = (AsmDataItem *)realloc(layout->data, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    layout->data = items;
    layout->data_cap = next;
    return 0;
}

static int ensure_const_int_cap(AsmLayout *layout, size_t extra) {
    size_t need = layout->const_int_len + extra;
    if (need <= layout->const_int_cap) {
        return 0;
    }
    size_t next = layout->const_int_cap == 0 ? 8 : layout->const_int_cap * 2;
    while (next < need) {
        next *= 2;
    }
    AsmConstInt *items = (AsmConstInt *)realloc(layout->const_ints, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    layout->const_ints = items;
    layout->const_int_cap = next;
    return 0;
}

static int ensure_string_cap(AsmLayout *layout, size_t extra) {
    size_t need = layout->string_len + extra;
    if (need <= layout->string_cap) {
        return 0;
    }
    size_t next = layout->string_cap == 0 ? 8 : layout->string_cap * 2;
    while (next < need) {
        next *= 2;
    }
    AsmStringEntry *items = (AsmStringEntry *)realloc(layout->strings, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    layout->strings = items;
    layout->string_cap = next;
    return 0;
}

static int ensure_field_cap(AsmTypeEntry *entry, size_t extra) {
    size_t need = entry->field_len + extra;
    if (need <= entry->field_cap) {
        return 0;
    }
    size_t next = entry->field_cap == 0 ? 8 : entry->field_cap * 2;
    while (next < need) {
        next *= 2;
    }
    AsmFieldLayout *items = (AsmFieldLayout *)realloc(entry->fields, next * sizeof(*items));
    if (!items) {
        return -1;
    }
    entry->fields = items;
    entry->field_cap = next;
    return 0;
}

static AsmTypeEntry *layout_find_type_entry(AsmLayout *layout, const char *name) {
    if (!layout || !name) {
        return NULL;
    }
    for (size_t i = 0; i < layout->type_len; i++) {
        AsmTypeEntry *entry = &layout->types[i];
        if (entry->layout.name && strcmp(entry->layout.name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

static AsmTypeEntry *layout_add_type_entry(AsmLayout *layout, const char *name, AsmTypeKind kind) {
    if (!layout || !name) {
        return NULL;
    }
    AsmTypeEntry *existing = layout_find_type_entry(layout, name);
    if (existing) {
        return existing;
    }
    if (ensure_type_cap(layout, 1) != 0) {
        return NULL;
    }
    AsmTypeEntry *entry = &layout->types[layout->type_len++];
    memset(entry, 0, sizeof(*entry));
    char *copy = layout_strdup(name);
    if (!copy) {
        layout->type_len--;
        return NULL;
    }
    entry->layout.name = copy;
    entry->kind = kind;
    entry->layout.size = 0;
    entry->layout.align = 1;
    entry->layout.fields = NULL;
    entry->layout.field_len = 0;
    return entry;
}

static int const_int_map_get(const AsmLayout *layout, const char *name, long long *out) {
    if (!layout || !name || !out) {
        return 0;
    }
    for (size_t i = layout->const_int_len; i > 0; i--) {
        AsmConstInt *entry = &layout->const_ints[i - 1];
        if (entry->name && strcmp(entry->name, name) == 0) {
            *out = entry->value;
            return 1;
        }
    }
    return 0;
}

static int const_int_map_add(AsmLayout *layout, const char *name, long long value) {
    if (!layout || !name) {
        return -1;
    }
    for (size_t i = 0; i < layout->const_int_len; i++) {
        AsmConstInt *entry = &layout->const_ints[i];
        if (entry->name && strcmp(entry->name, name) == 0) {
            entry->value = value;
            return 0;
        }
    }
    if (ensure_const_int_cap(layout, 1) != 0) {
        return -1;
    }
    AsmConstInt *entry = &layout->const_ints[layout->const_int_len++];
    memset(entry, 0, sizeof(*entry));
    entry->name = layout_strdup(name);
    if (!entry->name) {
        layout->const_int_len--;
        return -1;
    }
    entry->value = value;
    return 0;
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

static int layout_eval_const_expr(const AsmLayout *layout,
                                  const ChengNode *expr,
                                  LayoutConstVal *out,
                                  const char *filename,
                                  ChengDiagList *diags) {
    if (!expr || !out) {
        layout_add_diag(diags, filename, expr, "asm backend expects a constant expression");
        return 0;
    }
    switch (expr->kind) {
        case NK_PAR:
            if (expr->len > 0) {
                return layout_eval_const_expr(layout, expr->kids[0], out, filename, diags);
            }
            layout_add_diag(diags, filename, expr, "empty paren expression");
            return 0;
        case NK_INT_LIT:
            out->valid = 1;
            out->is_bool = 0;
            out->value = expr->int_val;
            return 1;
        case NK_CHAR_LIT: {
            int ch = 0;
            if (!decode_char_lit(expr->str_val, &ch)) {
                layout_add_diag(diags, filename, expr, "invalid char literal");
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
        case NK_NIL_LIT:
            out->valid = 1;
            out->is_bool = 0;
            out->value = 0;
            return 1;
        case NK_IDENT: {
            long long value = 0;
            if (expr->ident && const_int_map_get(layout, expr->ident, &value)) {
                out->valid = 1;
                out->is_bool = 0;
                out->value = value;
                return 1;
            }
            layout_add_diag(diags, filename, expr, "unknown constant identifier");
            return 0;
        }
        case NK_PREFIX: {
            if (expr->len < 2 || !expr->kids[0] || !expr->kids[0]->ident) {
                layout_add_diag(diags, filename, expr, "invalid prefix expression");
                return 0;
            }
            const char *op = expr->kids[0]->ident;
            LayoutConstVal rhs = {0};
            if (!layout_eval_const_expr(layout, expr->kids[1], &rhs, filename, diags)) {
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
            layout_add_diag(diags, filename, expr, "unsupported prefix operator in asm backend");
            return 0;
        }
        case NK_INFIX: {
            if (expr->len < 3 || !expr->kids[0] || !expr->kids[0]->ident) {
                layout_add_diag(diags, filename, expr, "invalid infix expression");
                return 0;
            }
            const char *op = expr->kids[0]->ident;
            LayoutConstVal lhs = {0};
            LayoutConstVal rhs = {0};
            if (!layout_eval_const_expr(layout, expr->kids[1], &lhs, filename, diags) ||
                !layout_eval_const_expr(layout, expr->kids[2], &rhs, filename, diags)) {
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
                    layout_add_diag(diags, filename, expr, "division by zero in const expression");
                    return 0;
                }
                out->valid = 1;
                out->is_bool = 0;
                out->value = a / b;
                return 1;
            }
            if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
                if (b == 0) {
                    layout_add_diag(diags, filename, expr, "modulo by zero in const expression");
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
                    layout_add_diag(diags, filename, expr, "shift out of range in const expression");
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
            layout_add_diag(diags, filename, expr, "unsupported infix operator in asm backend");
            return 0;
        }
        default:
            layout_add_diag(diags, filename, expr, "asm backend only supports constant expressions");
            return 0;
    }
}

static int builtin_type_info_raw(const AsmLayoutConfig *cfg,
                                 const char *name,
                                 size_t *out_size,
                                 size_t *out_align) {
    if (!cfg || !name || !out_size || !out_align) {
        return 0;
    }
    if (strcmp(name, "void") == 0) {
        *out_size = 0;
        *out_align = 1;
        return 1;
    }
    if (strcmp(name, "bool") == 0) {
        *out_size = cfg->bool_size;
        *out_align = cfg->bool_align;
        return 1;
    }
    if (strcmp(name, "char") == 0 || strcmp(name, "int8") == 0 ||
        strcmp(name, "uint8") == 0 || strcmp(name, "byte") == 0) {
        *out_size = cfg->char_size;
        *out_align = cfg->char_align;
        return 1;
    }
    if (strcmp(name, "int16") == 0 || strcmp(name, "uint16") == 0) {
        *out_size = 2;
        *out_align = 2;
        return 1;
    }
    if (strcmp(name, "int32") == 0 || strcmp(name, "uint32") == 0 ||
        strcmp(name, "int") == 0 || strcmp(name, "uint") == 0) {
        *out_size = cfg->int_size;
        *out_align = cfg->int_align;
        return 1;
    }
    if (strcmp(name, "int64") == 0 || strcmp(name, "uint64") == 0) {
        *out_size = cfg->long_size;
        *out_align = cfg->long_align;
        return 1;
    }
    if (strcmp(name, "float") == 0 || strcmp(name, "float32") == 0) {
        *out_size = cfg->float_size;
        *out_align = cfg->float_align;
        return 1;
    }
    if (strcmp(name, "float64") == 0 || strcmp(name, "double") == 0) {
        *out_size = cfg->double_size;
        *out_align = cfg->double_align;
        return 1;
    }
    if (strcmp(name, "str") == 0 || strcmp(name, "cstring") == 0 || strcmp(name, "usize") == 0) {
        *out_size = cfg->ptr_size;
        *out_align = cfg->ptr_align;
        return 1;
    }
    return 0;
}

static int builtin_type_info(const AsmLayoutConfig *cfg,
                             const char *name,
                             size_t *out_size,
                             size_t *out_align) {
    if (!name) {
        return 0;
    }
    if (builtin_type_info_raw(cfg, name, out_size, out_align)) {
        return 1;
    }
    const char *underscore = strchr(name, '_');
    if (underscore && underscore != name) {
        size_t len = (size_t)(underscore - name);
        if (len > 0 && len < 32) {
            char base[32];
            memcpy(base, name, len);
            base[len] = '\0';
            if (builtin_type_info_raw(cfg, base, out_size, out_align)) {
                return 1;
            }
        }
    }
    return 0;
}

static const ChengNode *object_rec_list(const ChengNode *obj) {
    if (!obj || obj->kind != NK_OBJECT_DECL) {
        return NULL;
    }
    for (size_t i = 0; i < obj->len; i++) {
        if (obj->kids[i] && obj->kids[i]->kind == NK_REC_LIST) {
            return obj->kids[i];
        }
    }
    return NULL;
}

static const ChengNode *object_base_type(const ChengNode *obj) {
    if (!obj || obj->kind != NK_OBJECT_DECL) {
        return NULL;
    }
    if (obj->len == 0) {
        return NULL;
    }
    return obj->kids[0];
}

static int layout_type_info(AsmLayout *layout,
                            const ChengNode *node,
                            size_t *out_size,
                            size_t *out_align,
                            const char *filename,
                            ChengDiagList *diags);

static int layout_add_field(AsmTypeEntry *entry,
                            const char *name,
                            size_t offset,
                            size_t size,
                            size_t align) {
    if (!entry || !name) {
        return -1;
    }
    if (ensure_field_cap(entry, 1) != 0) {
        return -1;
    }
    AsmFieldLayout *field = &entry->fields[entry->field_len++];
    field->name = layout_strdup(name);
    if (!field->name) {
        entry->field_len--;
        return -1;
    }
    field->offset = offset;
    field->size = size;
    field->align = align;
    return 0;
}

static int layout_compute_rec_list_size(AsmLayout *layout,
                                        const ChengNode *rec_list,
                                        size_t *out_size,
                                        size_t *out_align,
                                        const char *filename,
                                        ChengDiagList *diags);

static int layout_compute_object(AsmLayout *layout,
                                 AsmTypeEntry *entry,
                                 const char *filename,
                                 ChengDiagList *diags) {
    if (!layout || !entry) {
        return 0;
    }
    size_t offset = 0;
    size_t max_align = 1;
    if (entry->base && entry->base->kind != NK_EMPTY) {
        size_t base_size = 0;
        size_t base_align = 1;
        if (!layout_type_info(layout, entry->base, &base_size, &base_align, filename, diags)) {
            return 0;
        }
        offset = base_size;
        if (base_align > max_align) {
            max_align = base_align;
        }
    }
    const ChengNode *rec_list = entry->rec_list;
    if (rec_list && rec_list->kind == NK_REC_LIST) {
        for (size_t i = 0; i < rec_list->len; i++) {
            const ChengNode *field = rec_list->kids[i];
            if (!field) {
                continue;
            }
            if (field->kind == NK_IDENT_DEFS) {
                const ChengNode *fname = field->len > 0 ? field->kids[0] : NULL;
                const ChengNode *ftype = field->len > 1 ? field->kids[1] : NULL;
                if (!fname || fname->kind != NK_IDENT || !fname->ident) {
                    continue;
                }
                size_t fsize = 0;
                size_t falign = 1;
                if (!layout_type_info(layout, ftype, &fsize, &falign, filename, diags)) {
                    return 0;
                }
                offset = align_up(offset, falign);
                if (layout_add_field(entry, fname->ident, offset, fsize, falign) != 0) {
                    return 0;
                }
                offset += fsize;
                if (falign > max_align) {
                    max_align = falign;
                }
            } else if (field->kind == NK_REC_CASE) {
                if (field->len > 0 && field->kids[0]) {
                    const ChengNode *case_defs = field->kids[0];
                    const ChengNode *case_name = case_defs->len > 0 ? case_defs->kids[0] : NULL;
                    const ChengNode *case_type = case_defs->len > 1 ? case_defs->kids[1] : NULL;
                    if (case_name && case_name->kind == NK_IDENT && case_name->ident) {
                        size_t fsize = 0;
                        size_t falign = 1;
                        if (!layout_type_info(layout, case_type, &fsize, &falign, filename, diags)) {
                            return 0;
                        }
                        offset = align_up(offset, falign);
                        if (layout_add_field(entry, case_name->ident, offset, fsize, falign) != 0) {
                            return 0;
                        }
                        offset += fsize;
                        if (falign > max_align) {
                            max_align = falign;
                        }
                    }
                }
                size_t union_size = 0;
                size_t union_align = 1;
                for (size_t j = 1; j < field->len; j++) {
                    const ChengNode *branch = field->kids[j];
                    const ChengNode *branch_body = NULL;
                    if (!branch) {
                        continue;
                    }
                    if (branch->kind == NK_OF_BRANCH && branch->len > 1) {
                        branch_body = branch->kids[1];
                    } else if (branch->kind == NK_ELSE && branch->len > 0) {
                        branch_body = branch->kids[0];
                    }
                    if (!branch_body || branch_body->kind != NK_REC_LIST) {
                        continue;
                    }
                    size_t branch_size = 0;
                    size_t branch_align = 1;
                    if (!layout_compute_rec_list_size(layout, branch_body, &branch_size, &branch_align, filename, diags)) {
                        return 0;
                    }
                    if (branch_size > union_size) {
                        union_size = branch_size;
                    }
                    if (branch_align > union_align) {
                        union_align = branch_align;
                    }
                }
                offset = align_up(offset, union_align);
                offset += union_size;
                if (union_align > max_align) {
                    max_align = union_align;
                }
            }
        }
    }
    entry->layout.size = align_up(offset, max_align);
    entry->layout.align = max_align;
    entry->layout.fields = entry->fields;
    entry->layout.field_len = entry->field_len;
    return 1;
}

static int layout_compute_tuple(AsmLayout *layout,
                                AsmTypeEntry *entry,
                                const ChengNode *tuple_node,
                                size_t *out_size,
                                size_t *out_align,
                                const char *filename,
                                ChengDiagList *diags) {
    if (!layout || !tuple_node || tuple_node->kind != NK_TUPLE_TY) {
        return 0;
    }
    size_t offset = 0;
    size_t max_align = 1;
    for (size_t i = 0; i < tuple_node->len; i++) {
        const ChengNode *elem = tuple_node->kids[i];
        const ChengNode *name_node = NULL;
        const ChengNode *type_node = elem;
        if (elem && elem->kind == NK_IDENT_DEFS) {
            name_node = elem->len > 0 ? elem->kids[0] : NULL;
            type_node = elem->len > 1 ? elem->kids[1] : NULL;
        }
        size_t elem_size = 0;
        size_t elem_align = 1;
        if (!layout_type_info(layout, type_node, &elem_size, &elem_align, filename, diags)) {
            return 0;
        }
        offset = align_up(offset, elem_align);
        if (entry && name_node && name_node->kind == NK_IDENT && name_node->ident) {
            if (layout_add_field(entry, name_node->ident, offset, elem_size, elem_align) != 0) {
                return 0;
            }
        }
        offset += elem_size;
        if (elem_align > max_align) {
            max_align = elem_align;
        }
    }
    size_t total_size = align_up(offset, max_align);
    if (out_size) {
        *out_size = total_size;
    }
    if (out_align) {
        *out_align = max_align;
    }
    if (entry) {
        entry->layout.size = total_size;
        entry->layout.align = max_align;
        entry->layout.fields = entry->fields;
        entry->layout.field_len = entry->field_len;
    }
    return 1;
}

static int layout_compute_rec_list_size(AsmLayout *layout,
                                        const ChengNode *rec_list,
                                        size_t *out_size,
                                        size_t *out_align,
                                        const char *filename,
                                        ChengDiagList *diags) {
    if (!out_size || !out_align) {
        return 0;
    }
    size_t offset = 0;
    size_t max_align = 1;
    if (rec_list && rec_list->kind == NK_REC_LIST) {
        for (size_t i = 0; i < rec_list->len; i++) {
            const ChengNode *field = rec_list->kids[i];
            if (!field) {
                continue;
            }
            if (field->kind == NK_IDENT_DEFS) {
                const ChengNode *ftype = field->len > 1 ? field->kids[1] : NULL;
                size_t fsize = 0;
                size_t falign = 1;
                if (!layout_type_info(layout, ftype, &fsize, &falign, filename, diags)) {
                    return 0;
                }
                offset = align_up(offset, falign);
                offset += fsize;
                if (falign > max_align) {
                    max_align = falign;
                }
            } else if (field->kind == NK_REC_CASE) {
                if (field->len > 0 && field->kids[0]) {
                    const ChengNode *case_defs = field->kids[0];
                    const ChengNode *case_type = case_defs->len > 1 ? case_defs->kids[1] : NULL;
                    size_t fsize = 0;
                    size_t falign = 1;
                    if (!layout_type_info(layout, case_type, &fsize, &falign, filename, diags)) {
                        return 0;
                    }
                    offset = align_up(offset, falign);
                    offset += fsize;
                    if (falign > max_align) {
                        max_align = falign;
                    }
                }
                size_t union_size = 0;
                size_t union_align = 1;
                for (size_t j = 1; j < field->len; j++) {
                    const ChengNode *branch = field->kids[j];
                    const ChengNode *branch_body = NULL;
                    if (!branch) {
                        continue;
                    }
                    if (branch->kind == NK_OF_BRANCH && branch->len > 1) {
                        branch_body = branch->kids[1];
                    } else if (branch->kind == NK_ELSE && branch->len > 0) {
                        branch_body = branch->kids[0];
                    }
                    if (!branch_body || branch_body->kind != NK_REC_LIST) {
                        continue;
                    }
                    size_t branch_size = 0;
                    size_t branch_align = 1;
                    if (!layout_compute_rec_list_size(layout, branch_body, &branch_size, &branch_align, filename, diags)) {
                        return 0;
                    }
                    if (branch_size > union_size) {
                        union_size = branch_size;
                    }
                    if (branch_align > union_align) {
                        union_align = branch_align;
                    }
                }
                offset = align_up(offset, union_align);
                offset += union_size;
                if (union_align > max_align) {
                    max_align = union_align;
                }
            }
        }
    }
    *out_size = align_up(offset, max_align);
    *out_align = max_align;
    return 1;
}

static int layout_compute_type_entry(AsmLayout *layout,
                                     AsmTypeEntry *entry,
                                     const char *filename,
                                     ChengDiagList *diags) {
    if (entry->computed) {
        return 1;
    }
    if (entry->computing) {
        layout_add_diag(diags, filename, NULL, "cyclic type layout");
        return 0;
    }
    entry->computing = 1;
    int ok = 0;
    if (entry->kind == ASM_TYPE_ALIAS) {
        if (entry->base && entry->base->kind == NK_TUPLE_TY) {
            ok = layout_compute_tuple(layout, entry, entry->base, NULL, NULL, filename, diags);
        } else {
            size_t size = 0;
            size_t align = 1;
            if (!layout_type_info(layout, entry->base, &size, &align, filename, diags)) {
                ok = 0;
            } else {
                entry->layout.size = size;
                entry->layout.align = align;
                ok = 1;
            }
        }
    } else {
        ok = layout_compute_object(layout, entry, filename, diags);
    }
    entry->computing = 0;
    entry->computed = ok ? 1 : 0;
    return ok;
}

static int layout_type_info(AsmLayout *layout,
                            const ChengNode *node,
                            size_t *out_size,
                            size_t *out_align,
                            const char *filename,
                            ChengDiagList *diags) {
    if (!layout || !out_size || !out_align) {
        return 0;
    }
    if (!node || node->kind == NK_EMPTY) {
        *out_size = layout->config.int_size;
        *out_align = layout->config.int_align;
        return 1;
    }
    if (node->kind == NK_PAR && node->len > 0) {
        return layout_type_info(layout, node->kids[0], out_size, out_align, filename, diags);
    }
    if (node->kind == NK_IDENT || node->kind == NK_SYMBOL) {
        if (node->ident && builtin_type_info(&layout->config, node->ident, out_size, out_align)) {
            return 1;
        }
        if (node->ident) {
            AsmTypeEntry *entry = layout_find_type_entry(layout, node->ident);
            if (entry && layout_compute_type_entry(layout, entry, filename, diags)) {
                *out_size = entry->layout.size;
                *out_align = entry->layout.align;
                return 1;
            }
        }
        return 0;
    }
    if (node->kind == NK_PTR_TY || node->kind == NK_REF_TY || node->kind == NK_VAR_TY) {
        *out_size = layout->config.ptr_size;
        *out_align = layout->config.ptr_align;
        return 1;
    }
    if (node->kind == NK_TUPLE_TY) {
        return layout_compute_tuple(layout, NULL, node, out_size, out_align, filename, diags);
    }
    if (node->kind == NK_BRACKET_EXPR && node->len > 0 && node->kids[0] &&
        node->kids[0]->kind == NK_IDENT && node->kids[0]->ident) {
        const char *base = node->kids[0]->ident;
        if (strcmp(base, "array") == 0 && node->len >= 3) {
            size_t elem_size = 0;
            size_t elem_align = 1;
            if (!layout_type_info(layout, node->kids[1], &elem_size, &elem_align, filename, diags)) {
                return 0;
            }
            LayoutConstVal count_val = {0};
            if (!layout_eval_const_expr(layout, node->kids[2], &count_val, filename, diags) || count_val.is_bool) {
                return 0;
            }
            long long count = count_val.value;
            if (count < 0) {
                layout_add_diag(diags, filename, node, "array size must be non-negative");
                return 0;
            }
            *out_size = elem_size * (size_t)count;
            *out_align = elem_align;
            return 1;
        }
        if (strcmp(base, "seq") == 0 || strcmp(base, "table") == 0) {
            *out_size = layout->config.ptr_size;
            *out_align = layout->config.ptr_align;
            return 1;
        }
    }
    return 0;
}

int asm_layout_type_info(AsmLayout *layout,
                         const ChengNode *type_expr,
                         size_t *out_size,
                         size_t *out_align) {
    return layout_type_info(layout, type_expr, out_size, out_align, NULL, NULL);
}

const AsmTypeLayout *asm_layout_find_type(AsmLayout *layout, const char *name) {
    AsmTypeEntry *entry = layout_find_type_entry(layout, name);
    if (!entry) {
        return NULL;
    }
    if (!layout_compute_type_entry(layout, entry, NULL, NULL)) {
        return NULL;
    }
    return &entry->layout;
}

static AsmTypeEntry *resolve_field_type_entry(AsmLayout *layout, AsmTypeEntry *entry) {
    if (!entry) {
        return NULL;
    }
    if (entry->kind == ASM_TYPE_OBJECT) {
        return entry;
    }
    if (entry->field_base) {
        return layout_find_type_entry(layout, entry->field_base);
    }
    if (entry->base) {
        const ChengNode *base = entry->base;
        if (base->kind == NK_PAR && base->len > 0) {
            base = base->kids[0];
        }
        if (base->kind == NK_IDENT && base->ident) {
            return layout_find_type_entry(layout, base->ident);
        }
        if ((base->kind == NK_PTR_TY || base->kind == NK_REF_TY || base->kind == NK_VAR_TY) && base->len > 0) {
            const ChengNode *inner = base->kids[0];
            if (inner && inner->kind == NK_IDENT && inner->ident) {
                return layout_find_type_entry(layout, inner->ident);
            }
        }
    }
    return NULL;
}

int asm_layout_field_offset(AsmLayout *layout,
                            const char *type_name,
                            const char *field_name,
                            size_t *out_offset) {
    if (!layout || !type_name || !field_name || !out_offset) {
        return 0;
    }
    AsmTypeEntry *entry = layout_find_type_entry(layout, type_name);
    entry = resolve_field_type_entry(layout, entry);
    if (!entry) {
        return 0;
    }
    if (!layout_compute_type_entry(layout, entry, NULL, NULL)) {
        return 0;
    }
    for (size_t i = 0; i < entry->field_len; i++) {
        AsmFieldLayout *field = &entry->fields[i];
        if (field->name && strcmp(field->name, field_name) == 0) {
            *out_offset = field->offset;
            return 1;
        }
    }
    return 0;
}

static int layout_add_data_item(AsmLayout *layout, const AsmDataItem *item) {
    if (!layout || !item) {
        return -1;
    }
    if (ensure_data_cap(layout, 1) != 0) {
        return -1;
    }
    AsmDataItem *dst = &layout->data[layout->data_len++];
    *dst = *item;
    return 0;
}

static const char *layout_add_string(AsmLayout *layout, const char *value) {
    if (!layout || !value) {
        return NULL;
    }
    for (size_t i = 0; i < layout->string_len; i++) {
        AsmStringEntry *entry = &layout->strings[i];
        if (entry->value && strcmp(entry->value, value) == 0) {
            return entry->label;
        }
    }
    if (ensure_string_cap(layout, 1) != 0) {
        return NULL;
    }
    char label_buf[32];
    snprintf(label_buf, sizeof(label_buf), ".Lstr%zu", layout->string_len);
    AsmStringEntry *entry = &layout->strings[layout->string_len++];
    entry->value = layout_strdup(value);
    entry->label = layout_strdup(label_buf);
    if (!entry->value || !entry->label) {
        free(entry->value);
        free(entry->label);
        layout->string_len--;
        return NULL;
    }
    size_t len = strlen(value);
    uint8_t *bytes = (uint8_t *)malloc(len + 1);
    if (!bytes) {
        free(entry->value);
        free(entry->label);
        layout->string_len--;
        return NULL;
    }
    memcpy(bytes, value, len);
    bytes[len] = 0;
    AsmDataItem item;
    memset(&item, 0, sizeof(item));
    item.name = layout_strdup(label_buf);
    item.section = ASM_DATA_RODATA;
    item.init_kind = ASM_DATA_INIT_BYTES;
    item.size = len + 1;
    item.align = 1;
    item.is_global = 0;
    item.bytes = bytes;
    item.bytes_len = len + 1;
    if (!item.name || layout_add_data_item(layout, &item) != 0) {
        free((char *)item.name);
        free(bytes);
        free(entry->value);
        free(entry->label);
        layout->string_len--;
        return NULL;
    }
    return entry->label;
}

const AsmDataItem *asm_layout_data_items(const AsmLayout *layout, size_t *out_len) {
    if (out_len) {
        *out_len = layout ? layout->data_len : 0;
    }
    return layout ? layout->data : NULL;
}

const AsmDataItem *asm_layout_find_data_item(const AsmLayout *layout, const char *name) {
    if (!layout || !name) {
        return NULL;
    }
    for (size_t i = 0; i < layout->data_len; i++) {
        const AsmDataItem *item = &layout->data[i];
        if (item->name && strcmp(item->name, name) == 0) {
            return item;
        }
    }
    return NULL;
}

const char *asm_layout_string_label(const AsmLayout *layout, const char *value) {
    if (!layout || !value) {
        return NULL;
    }
    for (size_t i = 0; i < layout->string_len; i++) {
        AsmStringEntry *entry = &layout->strings[i];
        if (entry->value && strcmp(entry->value, value) == 0) {
            return entry->label;
        }
    }
    return NULL;
}

static uint64_t layout_hash_bytes(uint64_t hash, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t layout_hash_u64(uint64_t hash, uint64_t value) {
    return layout_hash_bytes(hash, &value, sizeof(value));
}

static uint64_t layout_hash_str(uint64_t hash, const char *value) {
    if (!value) {
        return layout_hash_u64(hash, 0);
    }
    return layout_hash_bytes(hash, value, strlen(value));
}

static uint64_t layout_hash_type_entry(const AsmTypeEntry *entry) {
    uint64_t hash = 1469598103934665603ULL;
    if (!entry) {
        return hash;
    }
    hash = layout_hash_str(hash, entry->layout.name);
    hash = layout_hash_u64(hash, entry->layout.size);
    hash = layout_hash_u64(hash, entry->layout.align);
    for (size_t i = 0; i < entry->field_len; i++) {
        const AsmFieldLayout *field = &entry->fields[i];
        hash = layout_hash_str(hash, field->name);
        hash = layout_hash_u64(hash, field->offset);
        hash = layout_hash_u64(hash, field->size);
        hash = layout_hash_u64(hash, field->align);
    }
    return hash;
}

int asm_layout_dump(const AsmLayout *layout, FILE *out) {
    if (!layout || !out) {
        return -1;
    }
    fprintf(out, "layout_version=1\n");
    fprintf(out, "ptr_size=%zu\n", layout->config.ptr_size);
    fprintf(out, "ptr_align=%zu\n", layout->config.ptr_align);
    fprintf(out, "int_size=%zu\n", layout->config.int_size);
    fprintf(out, "int_align=%zu\n", layout->config.int_align);
    fprintf(out, "long_size=%zu\n", layout->config.long_size);
    fprintf(out, "long_align=%zu\n", layout->config.long_align);
    fprintf(out, "bool_size=%zu\n", layout->config.bool_size);
    fprintf(out, "bool_align=%zu\n", layout->config.bool_align);
    fprintf(out, "char_size=%zu\n", layout->config.char_size);
    fprintf(out, "char_align=%zu\n", layout->config.char_align);
    fprintf(out, "float_size=%zu\n", layout->config.float_size);
    fprintf(out, "float_align=%zu\n", layout->config.float_align);
    fprintf(out, "double_size=%zu\n", layout->config.double_size);
    fprintf(out, "double_align=%zu\n", layout->config.double_align);
    for (size_t i = 0; i < layout->type_len; i++) {
        const AsmTypeEntry *entry = &layout->types[i];
        if (!entry->computed || !entry->layout.name) {
            continue;
        }
        uint64_t hash = layout_hash_type_entry(entry);
        fprintf(out, "type %s size=%zu align=%zu hash=0x%016llx\n",
                entry->layout.name,
                entry->layout.size,
                entry->layout.align,
                (unsigned long long)hash);
        for (size_t j = 0; j < entry->field_len; j++) {
            const AsmFieldLayout *field = &entry->fields[j];
            if (!field->name) {
                continue;
            }
            fprintf(out, "  field %s offset=%zu size=%zu align=%zu\n",
                    field->name,
                    field->offset,
                    field->size,
                    field->align);
        }
    }
    return 0;
}

static int collect_const_ints(AsmLayout *layout, const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_CONST && node->len >= 3) {
        const ChengNode *pattern = node->kids[0];
        const ChengNode *value = node->kids[2];
        if (pattern && pattern->kind == NK_PATTERN && pattern->len > 0 &&
            pattern->kids[0] && pattern->kids[0]->kind == NK_IDENT &&
            pattern->kids[0]->ident && value && value->kind == NK_INT_LIT) {
            if (const_int_map_add(layout, pattern->kids[0]->ident, value->int_val) != 0) {
                return -1;
            }
        }
    }
    for (size_t i = 0; i < node->len; i++) {
        if (collect_const_ints(layout, node->kids[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int collect_strings(AsmLayout *layout, const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_STR_LIT && node->str_val) {
        if (!layout_add_string(layout, node->str_val)) {
            return -1;
        }
    }
    for (size_t i = 0; i < node->len; i++) {
        if (collect_strings(layout, node->kids[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static char *make_ref_object_name(const AsmLayout *layout, const char *base) {
    if (!layout || !base || base[0] == '\0') {
        return NULL;
    }
    size_t base_len = strlen(base);
    size_t len = base_len + 4;
    char *name = (char *)malloc(len);
    if (!name) {
        return NULL;
    }
    snprintf(name, len, "%sObj", base);
    if (!layout_find_type_entry((AsmLayout *)layout, name)) {
        return name;
    }
    free(name);
    for (int i = 1; i < 1000; i++) {
        int extra = snprintf(NULL, 0, "%d", i);
        len = base_len + 3 + (size_t)extra + 1;
        name = (char *)malloc(len);
        if (!name) {
            return NULL;
        }
        snprintf(name, len, "%sObj%d", base, i);
        if (!layout_find_type_entry((AsmLayout *)layout, name)) {
            return name;
        }
        free(name);
    }
    return NULL;
}

static int add_object_type(AsmLayout *layout,
                           const char *name,
                           const ChengNode *base,
                           const ChengNode *rec_list) {
    if (!layout || !name) {
        return -1;
    }
    AsmTypeEntry *entry = layout_add_type_entry(layout, name, ASM_TYPE_OBJECT);
    if (!entry) {
        return -1;
    }
    entry->base = base;
    entry->rec_list = rec_list;
    return 0;
}

static int add_alias_type(AsmLayout *layout,
                          const char *name,
                          const ChengNode *base,
                          const char *field_base) {
    if (!layout || !name) {
        return -1;
    }
    AsmTypeEntry *entry = layout_add_type_entry(layout, name, ASM_TYPE_ALIAS);
    if (!entry) {
        return -1;
    }
    entry->base = base;
    if (field_base) {
        entry->field_base = layout_strdup(field_base);
        if (!entry->field_base) {
            return -1;
        }
    }
    return 0;
}

static int collect_type_entry(AsmLayout *layout, const ChengNode *node, const char *filename, ChengDiagList *diags) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_OBJECT_DECL) {
        if (node->len >= 3 && node->kids[0] && node->kids[0]->kind == NK_IDENT && node->kids[0]->ident) {
            const ChengNode *rec = object_rec_list(node);
            if (add_object_type(layout, node->kids[0]->ident, NULL, rec) != 0) {
                layout_add_diag(diags, filename, node, "failed to record object type");
                return -1;
            }
        }
        return 0;
    }
    if (node->kind == NK_TYPE_DECL) {
        if (node->len > 0 && node->kids[0] &&
            (node->kids[0]->kind == NK_OBJECT_DECL || node->kids[0]->kind == NK_TYPE_DECL)) {
            for (size_t i = 0; i < node->len; i++) {
                if (collect_type_entry(layout, node->kids[i], filename, diags) != 0) {
                    return -1;
                }
            }
            return 0;
        }
        if (node->len >= 2 && node->kids[0] && node->kids[0]->kind == NK_IDENT && node->kids[0]->ident) {
            const char *name = node->kids[0]->ident;
            const ChengNode *body = node->kids[1];
            if (body && (body->kind == NK_REF_TY || body->kind == NK_PTR_TY) && body->len > 0 &&
                body->kids[0] && body->kids[0]->kind == NK_OBJECT_DECL) {
                char *obj_name = make_ref_object_name(layout, name);
                if (!obj_name) {
                    layout_add_diag(diags, filename, node, "failed to name ref object");
                    return -1;
                }
                const ChengNode *rec = object_rec_list(body->kids[0]);
                const ChengNode *base = object_base_type(body->kids[0]);
                if (add_object_type(layout, obj_name, base, rec) != 0) {
                    free(obj_name);
                    layout_add_diag(diags, filename, node, "failed to record ref object");
                    return -1;
                }
                if (add_alias_type(layout, name, body, obj_name) != 0) {
                    free(obj_name);
                    layout_add_diag(diags, filename, node, "failed to record ref alias");
                    return -1;
                }
                free(obj_name);
                return 0;
            }
            if (body && body->kind == NK_OBJECT_DECL) {
                const ChengNode *rec = object_rec_list(body);
                const ChengNode *base = object_base_type(body);
                if (add_object_type(layout, name, base, rec) != 0) {
                    layout_add_diag(diags, filename, node, "failed to record object type");
                    return -1;
                }
                return 0;
            }
            if (add_alias_type(layout, name, body, NULL) != 0) {
                layout_add_diag(diags, filename, node, "failed to record alias type");
                return -1;
            }
        }
        return 0;
    }
    return 0;
}

static int collect_types(AsmLayout *layout, const ChengNode *list, const char *filename, ChengDiagList *diags) {
    if (!list || list->kind != NK_STMT_LIST) {
        return 0;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        if (collect_type_entry(layout, stmt, filename, diags) != 0) {
            return -1;
        }
    }
    return 0;
}

static int collect_global_decl(AsmLayout *layout,
                               const ChengNode *stmt,
                               const char *filename,
                               ChengDiagList *diags) {
    if (!stmt || (stmt->kind != NK_VAR && stmt->kind != NK_LET && stmt->kind != NK_CONST)) {
        return 0;
    }
    const ChengNode *pattern = stmt->len > 0 ? stmt->kids[0] : NULL;
    const ChengNode *type_node = stmt->len > 1 ? stmt->kids[1] : NULL;
    const ChengNode *value = stmt->len > 2 ? stmt->kids[2] : NULL;
    if (!pattern || pattern->kind != NK_PATTERN || pattern->len == 0) {
        return 0;
    }
    const ChengNode *name = pattern->kids[0];
    if (!name || name->kind != NK_IDENT || !name->ident) {
        return 0;
    }
    int is_const = (stmt->kind == NK_CONST);
    int has_value = value && value->kind != NK_EMPTY;
    if (is_const && !has_value) {
        layout_add_diag(diags, filename, stmt, "const requires initializer");
        return -1;
    }
    size_t size = 0;
    size_t align = 1;
    int has_type = type_node && type_node->kind != NK_EMPTY;
    if (has_type) {
        if (!layout_type_info(layout, type_node, &size, &align, filename, diags) || size == 0) {
            layout_add_diag(diags, filename, type_node, "unknown type layout for global");
            return -1;
        }
    }
    AsmDataItem item;
    memset(&item, 0, sizeof(item));
    item.name = layout_strdup(name->ident);
    if (!item.name) {
        return -1;
    }
    item.align = align;
    item.size = size;
    item.is_global = 1;
    item.section = is_const ? ASM_DATA_RODATA : ASM_DATA_DATA;
    if (!has_value) {
        if (!has_type) {
            size = layout->config.int_size;
            align = layout->config.int_align;
            item.size = size;
            item.align = align;
        }
        item.section = is_const ? ASM_DATA_RODATA : ASM_DATA_BSS;
        item.init_kind = ASM_DATA_INIT_NONE;
        if (layout_add_data_item(layout, &item) != 0) {
            free((char *)item.name);
            return -1;
        }
        return 0;
    }
    if (value->kind == NK_STR_LIT && value->str_val) {
        const char *label = layout_add_string(layout, value->str_val);
        if (!label) {
            free((char *)item.name);
            return -1;
        }
        if (!has_type) {
            size = layout->config.ptr_size;
            align = layout->config.ptr_align;
            item.size = size;
            item.align = align;
        }
        item.init_kind = ASM_DATA_INIT_LABEL;
        item.label = layout_strdup(label);
        item.label_needs_prefix = 0;
        if (!item.label) {
            free((char *)item.name);
            return -1;
        }
        if (layout_add_data_item(layout, &item) != 0) {
            free((char *)item.name);
            free((char *)item.label);
            return -1;
        }
        return 0;
    }
    LayoutConstVal cval = {0};
    if (!layout_eval_const_expr(layout, value, &cval, filename, diags)) {
        free((char *)item.name);
        return -1;
    }
    if (!has_type) {
        if (cval.is_bool) {
            size = layout->config.bool_size;
            align = layout->config.bool_align;
        } else if (cval.value >= INT32_MIN && cval.value <= INT32_MAX) {
            size = layout->config.int_size;
            align = layout->config.int_align;
        } else {
            size = layout->config.long_size;
            align = layout->config.long_align;
        }
        item.size = size;
        item.align = align;
    }
    if (!is_const && cval.value == 0) {
        item.section = ASM_DATA_BSS;
        item.init_kind = ASM_DATA_INIT_NONE;
    } else {
        item.init_kind = ASM_DATA_INIT_INT;
        item.int_value = cval.value;
        item.int_size = item.size;
    }
    if (layout_add_data_item(layout, &item) != 0) {
        free((char *)item.name);
        return -1;
    }
    return 0;
}

static int collect_globals(AsmLayout *layout,
                           const ChengNode *list,
                           const char *filename,
                           ChengDiagList *diags) {
    if (!list || list->kind != NK_STMT_LIST) {
        return 0;
    }
    for (size_t i = 0; i < list->len; i++) {
        const ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_VAR || stmt->kind == NK_LET || stmt->kind == NK_CONST) {
            if (collect_global_decl(layout, stmt, filename, diags) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static const ChengNode *root_list(const ChengNode *root) {
    if (!root) {
        return NULL;
    }
    if (root->kind == NK_MODULE && root->len > 0) {
        return root->kids[0];
    }
    return root;
}

int asm_layout_collect(AsmLayout *layout,
                       const ChengNode *root,
                       const char *filename,
                       ChengDiagList *diags) {
    if (!layout || !root) {
        return -1;
    }
    const ChengNode *list = root_list(root);
    if (!list || list->kind != NK_STMT_LIST) {
        return 0;
    }
    if (collect_const_ints(layout, list) != 0) {
        return -1;
    }
    if (collect_strings(layout, list) != 0) {
        return -1;
    }
    if (collect_types(layout, list, filename, diags) != 0) {
        return -1;
    }
    if (collect_globals(layout, list, filename, diags) != 0) {
        return -1;
    }
    return 0;
}
