#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "monomorphize.h"
#include "strlist.h"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static int mono_keep_dot_calls(void) {
    const char *env = getenv("CHENG_C_DOT_CALL");
    if (!env || env[0] == '\0') {
        return 0;
    }
    if (strcmp(env, "0") == 0 || strcmp(env, "false") == 0 || strcmp(env, "no") == 0) {
        return 0;
    }
    return 1;
}

static void sb_init(StrBuf *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int sb_reserve(StrBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) {
        return 0;
    }
    size_t next = sb->cap == 0 ? 64 : sb->cap * 2;
    while (next < need) {
        next *= 2;
    }
    char *buf = (char *)realloc(sb->data, next);
    if (!buf) {
        return -1;
    }
    sb->data = buf;
    sb->cap = next;
    return 0;
}

static int sb_append(StrBuf *sb, const char *text) {
    if (!text) {
        return 0;
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
    if (sb_reserve(sb, 1) != 0) {
        return -1;
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 0;
}

static char *sb_build(StrBuf *sb) {
    if (!sb->data) {
        return NULL;
    }
    char *out = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

static char *mono_strdup(const char *s) {
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

static int mono_str_eq_ci(const char *a, const char *b) {
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

static int mono_env_is_truthy(const char *value) {
    if (!value || value[0] == '\0') {
        return 0;
    }
    if (strcmp(value, "1") == 0) {
        return 1;
    }
    if (strcmp(value, "0") == 0) {
        return 0;
    }
    if (mono_str_eq_ci(value, "true") || mono_str_eq_ci(value, "yes") || mono_str_eq_ci(value, "on")) {
        return 1;
    }
    if (mono_str_eq_ci(value, "false") || mono_str_eq_ci(value, "no") || mono_str_eq_ci(value, "off")) {
        return 0;
    }
    return 0;
}

static int mono_env_is_hard_rt(void) {
    const char *value = getenv("CHENG_ASM_RT");
    if (!value || value[0] == '\0') {
        value = getenv("CHENG_HARD_RT");
    }
    if (!value || value[0] == '\0') {
        return 0;
    }
    if (mono_str_eq_ci(value, "hard") || mono_str_eq_ci(value, "hard-rt") || mono_str_eq_ci(value, "hard_rt")) {
        return 1;
    }
    return mono_env_is_truthy(value);
}

typedef struct {
    char **names;
    ChengNode **nodes;
    size_t len;
    size_t cap;
} GenericMap;

static void generic_map_init(GenericMap *map) {
    map->names = NULL;
    map->nodes = NULL;
    map->len = 0;
    map->cap = 0;
}

static void generic_map_free(GenericMap *map) {
    for (size_t i = 0; i < map->len; i++) {
        free(map->names[i]);
    }
    free(map->names);
    free(map->nodes);
    map->names = NULL;
    map->nodes = NULL;
    map->len = 0;
    map->cap = 0;
}

static ChengNode *generic_map_get(const GenericMap *map, const char *name) {
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < map->len; i++) {
        if (map->names[i] && strcmp(map->names[i], name) == 0) {
            return map->nodes[i];
        }
    }
    return NULL;
}

static int generic_map_add(GenericMap *map, const char *name, ChengNode *node) {
    if (!name || !node) {
        return -1;
    }
    if (generic_map_get(map, name)) {
        return 0;
    }
    if (map->len + 1 > map->cap) {
        size_t next = map->cap == 0 ? 16 : map->cap * 2;
        char **names = (char **)realloc(map->names, next * sizeof(char *));
        ChengNode **nodes = (ChengNode **)realloc(map->nodes, next * sizeof(ChengNode *));
        if (!names || !nodes) {
            free(names);
            free(nodes);
            return -1;
        }
        map->names = names;
        map->nodes = nodes;
        map->cap = next;
    }
    map->names[map->len] = mono_strdup(name);
    map->nodes[map->len] = node;
    if (!map->names[map->len]) {
        return -1;
    }
    map->len++;
    return 0;
}

static int generic_map_set(GenericMap *map, const char *name, ChengNode *node) {
    if (!map || !name || !node) {
        return -1;
    }
    for (size_t i = 0; i < map->len; i++) {
        if (map->names[i] && strcmp(map->names[i], name) == 0) {
            map->nodes[i] = node;
            return 0;
        }
    }
    return generic_map_add(map, name, node);
}

typedef struct {
    const char **names;
    const ChengNode **types;
    size_t len;
    size_t cap;
} TypeMap;

static void type_map_init(TypeMap *map) {
    map->names = NULL;
    map->types = NULL;
    map->len = 0;
    map->cap = 0;
}

static void type_map_free(TypeMap *map) {
    free(map->names);
    free(map->types);
    map->names = NULL;
    map->types = NULL;
    map->len = 0;
    map->cap = 0;
}

static const ChengNode *type_map_get(const TypeMap *map, const char *name) {
    if (!map || !name) {
        return NULL;
    }
    for (size_t i = 0; i < map->len; i++) {
        if (map->names[i] && strcmp(map->names[i], name) == 0) {
            return map->types[i];
        }
    }
    return NULL;
}

static int type_map_add(TypeMap *map, const char *name, const ChengNode *type) {
    if (!map || !name || !type) {
        return -1;
    }
    if (type_map_get(map, name)) {
        return 0;
    }
    if (map->len + 1 > map->cap) {
        size_t next = map->cap == 0 ? 8 : map->cap * 2;
        const char **names = (const char **)realloc(map->names, next * sizeof(const char *));
        const ChengNode **types = (const ChengNode **)realloc(map->types, next * sizeof(const ChengNode *));
        if (!names || !types) {
            free(names);
            free(types);
            return -1;
        }
        map->names = names;
        map->types = types;
        map->cap = next;
    }
    map->names[map->len] = name;
    map->types[map->len] = type;
    map->len++;
    return 0;
}

typedef struct {
    char *name;
    const ChengNode *type;
} TypeBinding;

typedef struct {
    TypeBinding *items;
    size_t len;
    size_t cap;
    size_t *scopes;
    size_t scope_len;
    size_t scope_cap;
} TypeEnv;

static void type_env_init(TypeEnv *env) {
    memset(env, 0, sizeof(*env));
}

static void type_env_free(TypeEnv *env) {
    for (size_t i = 0; i < env->len; i++) {
        free(env->items[i].name);
    }
    free(env->items);
    free(env->scopes);
    memset(env, 0, sizeof(*env));
}

static int type_env_push(TypeEnv *env) {
    if (env->scope_len + 1 > env->scope_cap) {
        size_t next = env->scope_cap == 0 ? 8 : env->scope_cap * 2;
        size_t *scopes = (size_t *)realloc(env->scopes, next * sizeof(size_t));
        if (!scopes) {
            return -1;
        }
        env->scopes = scopes;
        env->scope_cap = next;
    }
    env->scopes[env->scope_len++] = env->len;
    return 0;
}

static void type_env_pop(TypeEnv *env) {
    if (env->scope_len == 0) {
        return;
    }
    size_t start = env->scopes[env->scope_len - 1];
    for (size_t i = start; i < env->len; i++) {
        free(env->items[i].name);
    }
    env->len = start;
    env->scope_len--;
}

static int type_env_define(TypeEnv *env, const char *name, const ChengNode *type) {
    if (!name || !type) {
        return -1;
    }
    if (env->len + 1 > env->cap) {
        size_t next = env->cap == 0 ? 16 : env->cap * 2;
        TypeBinding *items = (TypeBinding *)realloc(env->items, next * sizeof(TypeBinding));
        if (!items) {
            return -1;
        }
        env->items = items;
        env->cap = next;
    }
    env->items[env->len].name = mono_strdup(name);
    env->items[env->len].type = type;
    if (!env->items[env->len].name) {
        return -1;
    }
    env->len++;
    return 0;
}

static const ChengNode *type_env_lookup(TypeEnv *env, const char *name) {
    if (!env || !name) {
        return NULL;
    }
    for (size_t i = env->len; i > 0; i--) {
        TypeBinding *b = &env->items[i - 1];
        if (b->name && strcmp(b->name, name) == 0) {
            return b->type;
        }
    }
    return NULL;
}

static int mono_base_is_value(TypeEnv *env, const ChengNode *base) {
    if (!env || !base || base->kind != NK_IDENT || !base->ident) {
        return 0;
    }
    return type_env_lookup(env, base->ident) != NULL;
}

typedef struct {
    const ChengNode **items;
    size_t len;
    size_t cap;
} TypeArgList;

static void type_args_init(TypeArgList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static void type_args_free(TypeArgList *list) {
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static int type_args_push(TypeArgList *list, const ChengNode *node) {
    if (!list || !node) {
        return -1;
    }
    if (list->len + 1 > list->cap) {
        size_t next = list->cap == 0 ? 4 : list->cap * 2;
        const ChengNode **items = (const ChengNode **)realloc(list->items, next * sizeof(const ChengNode *));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->cap = next;
    }
    list->items[list->len++] = node;
    return 0;
}

typedef struct {
    ChengDiagList *diags;
    ChengNode *root_list;
    GenericMap generic_types;
    GenericMap generic_fns;
    GenericMap templates;
    GenericMap macros;
    ChengStrList instantiated;
    size_t unwrap_counter;
    int hard_rt;
} MonoCtx;

static char *sanitize_key(const char *s) {
    if (!s) {
        return mono_strdup("anon");
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
        sb_append(&sb, "anon");
    }
    return sb_build(&sb);
}

static char *format_int(long long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", value);
    return mono_strdup(buf);
}

static char *type_key(const ChengNode *node);

static char *type_key(const ChengNode *node) {
    if (!node) {
        return mono_strdup("");
    }
    switch (node->kind) {
        case NK_IDENT:
        case NK_SYMBOL:
            return sanitize_key(node->ident);
        case NK_IDENT_DEFS:
            if (node->len > 1) {
                return type_key(node->kids[1]);
            }
            return mono_strdup("");
        case NK_CALL_ARG:
            if (node->len > 1) {
                return type_key(node->kids[1]);
            }
            if (node->len > 0) {
                return type_key(node->kids[0]);
            }
            return mono_strdup("");
        case NK_INT_LIT:
            return format_int(node->int_val);
        case NK_PTR_TY:
            if (node->len > 0) {
                char *inner = type_key(node->kids[0]);
                if (!inner) {
                    return mono_strdup("ptr");
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
            return mono_strdup("ptr");
        case NK_REF_TY:
            if (node->len > 0) {
                char *inner = type_key(node->kids[0]);
                if (!inner) {
                    return mono_strdup("ref");
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
            return mono_strdup("ref");
        case NK_VAR_TY:
            if (node->len > 0) {
                char *inner = type_key(node->kids[0]);
                if (!inner) {
                    return mono_strdup("var");
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
            return mono_strdup("var");
        case NK_SET_TY:
            if (node->len > 0) {
                char *inner = type_key(node->kids[0]);
                if (!inner) {
                    return mono_strdup("set");
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
            return mono_strdup("set");
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
        case NK_BRACKET_EXPR: {
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
            return mono_strdup("");
        }
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
                    if (!defs || defs->kind != NK_IDENT_DEFS || defs->len < 2) {
                        continue;
                    }
                    char *key = type_key(defs->kids[1]);
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
        case NK_PAR:
            if (node->len == 1) {
                return type_key(node->kids[0]);
            }
            return mono_strdup("");
        default:
            if (node->ident && node->ident[0] != '\0') {
                return sanitize_key(node->ident);
            }
            return mono_strdup("node");
    }
}

static int mono_is_const_int(const ChengNode *node) {
    const ChengNode *cur = node;
    while (cur && cur->kind == NK_PAR && cur->len > 0) {
        cur = cur->kids[0];
    }
    if (!cur) {
        return 0;
    }
    if (cur->kind == NK_INT_LIT) {
        return 1;
    }
    if (cur->kind == NK_PREFIX && cur->len > 1 && cur->kids[0] && cur->kids[0]->ident &&
        cur->kids[1] && cur->kids[1]->kind == NK_INT_LIT) {
        const char *op = cur->kids[0]->ident;
        return strcmp(op, "+") == 0 || strcmp(op, "-") == 0;
    }
    return 0;
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

static char *mangle_instance(const char *name, const TypeArgList *args) {
    if (!name) {
        return mono_strdup("");
    }
    const char *base = normalize_mangle_base(name);
    StrBuf sb;
    sb_init(&sb);
    if (sb_append(&sb, base) != 0) {
        sb_free(&sb);
        return NULL;
    }
    if (args) {
        for (size_t i = 0; i < args->len; i++) {
            char *key = type_key(args->items[i]);
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
    return sb_build(&sb);
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

static ChengNode *replace_type_vars(const ChengNode *node, const TypeMap *map) {
    if (!node) {
        return NULL;
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        const ChengNode *mapped = type_map_get(map, node->ident);
        if (mapped) {
            return clone_tree(mapped);
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
        ChengNode *child = replace_type_vars(node->kids[i], map);
        if (child) {
            cheng_node_add(out, child);
        }
    }
    return out;
}

static int is_builtin_type_ident(const char *name) {
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
           strcmp(name, "float") == 0 ||
           strcmp(name, "float32") == 0 ||
           strcmp(name, "float64") == 0 ||
           strcmp(name, "bool") == 0 ||
           strcmp(name, "char") == 0 ||
           strcmp(name, "byte") == 0 ||
           strcmp(name, "str") == 0 ||
           strcmp(name, "cstring") == 0 ||
           strcmp(name, "void") == 0 ||
           strcmp(name, "seq") == 0 ||
           strcmp(name, "Table") == 0 ||
           strcmp(name, "array") == 0 ||
           strcmp(name, "set") == 0 ||
           strcmp(name, "tuple") == 0 ||
           strcmp(name, "typedesc") == 0;
}

static int is_comparable_type_node(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_PTR_TY || node->kind == NK_REF_TY) {
        return 1;
    }
    if (node->kind == NK_VAR_TY && node->len > 0) {
        return is_comparable_type_node(node->kids[0]);
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        const char *name = node->ident;
        if (strcmp(name, "int") == 0 || strcmp(name, "int8") == 0 ||
            strcmp(name, "int16") == 0 || strcmp(name, "int32") == 0 ||
            strcmp(name, "int64") == 0) {
            return 1;
        }
        if (strcmp(name, "uint") == 0 || strcmp(name, "uint8") == 0 ||
            strcmp(name, "uint16") == 0 || strcmp(name, "uint32") == 0 ||
            strcmp(name, "uint64") == 0) {
            return 1;
        }
        if (strcmp(name, "float") == 0 || strcmp(name, "float32") == 0 ||
            strcmp(name, "float64") == 0) {
            return 1;
        }
        if (strcmp(name, "bool") == 0 || strcmp(name, "char") == 0 ||
            strcmp(name, "byte") == 0) {
            return 1;
        }
        if (strcmp(name, "str") == 0 || strcmp(name, "cstring") == 0) {
            return 1;
        }
    }
    return 0;
}

static int looks_like_type_expr(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_IDENT:
        case NK_SYMBOL:
            if (node->ident && (is_builtin_type_ident(node->ident) ||
                                (node->ident[0] >= 'A' && node->ident[0] <= 'Z'))) {
                return 1;
            }
            return 0;
        case NK_BRACKET_EXPR:
        case NK_PTR_TY:
        case NK_REF_TY:
        case NK_VAR_TY:
        case NK_SET_TY:
        case NK_TUPLE_TY:
        case NK_FN_TY:
            return 1;
        case NK_PAR:
            return node->len == 1 && looks_like_type_expr(node->kids[0]);
        default:
            return 0;
    }
}

static int is_definite_type_callee(const MonoCtx *ctx, const ChengNode *node) {
    if (!node) {
        return 0;
    }
    switch (node->kind) {
        case NK_PTR_TY:
        case NK_REF_TY:
        case NK_VAR_TY:
        case NK_SET_TY:
        case NK_TUPLE_TY:
        case NK_FN_TY:
            return 1;
        case NK_PAR:
            return node->len == 1 && is_definite_type_callee(ctx, node->kids[0]);
        case NK_BRACKET_EXPR: {
            if (node->len < 1) {
                return 0;
            }
            const ChengNode *base = node->kids[0];
            if (!base || (base->kind != NK_IDENT && base->kind != NK_SYMBOL) || !base->ident) {
                return 0;
            }
            if (is_builtin_type_ident(base->ident)) {
                return 1;
            }
            if (generic_map_get(&ctx->generic_types, base->ident)) {
                return 1;
            }
            return 0;
        }
        case NK_IDENT:
        case NK_SYMBOL:
            if (!node->ident) {
                return 0;
            }
            if (is_builtin_type_ident(node->ident)) {
                return 1;
            }
            if (generic_map_get(&ctx->generic_types, node->ident)) {
                return 1;
            }
            return 0;
        default:
            return 0;
    }
}

static int is_generic_params(const ChengNode *node) {
    return node && node->kind == NK_GENERIC_PARAMS && node->len > 0;
}

static int is_generic_name_node(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_BRACKET_EXPR) {
        return node->len > 1;
    }
    if (node->kind == NK_DOT_EXPR && node->len > 1) {
        return is_generic_name_node(node->kids[1]);
    }
    return 0;
}

static int is_generic_type_decl(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind != NK_TYPE_DECL && node->kind != NK_OBJECT_DECL) {
        return 0;
    }
    if (node->len > 2 && is_generic_params(node->kids[2])) {
        return 1;
    }
    return node->len > 0 && is_generic_name_node(node->kids[0]);
}

static int is_generic_fn_decl(const ChengNode *node) {
    if (!node || node->kind != NK_FN_DECL) {
        return 0;
    }
    if (node->len > 4 && is_generic_params(node->kids[4])) {
        return 1;
    }
    return node->len > 0 && is_generic_name_node(node->kids[0]);
}

static void collect_param_names(const ChengNode *generics, ChengStrList *out) {
    if (!generics || generics->kind != NK_GENERIC_PARAMS) {
        return;
    }
    for (size_t i = 0; i < generics->len; i++) {
        ChengNode *defn = generics->kids[i];
        if (!defn || defn->kind != NK_IDENT_DEFS || defn->len == 0) {
            continue;
        }
        ChengNode *name = defn->kids[0];
        if (name && name->kind == NK_IDENT && name->ident) {
            cheng_strlist_push_unique(out, name->ident);
        }
    }
}

static int param_names_has(const ChengStrList *params, const char *name) {
    if (!params || !name) {
        return 0;
    }
    return cheng_strlist_has(params, name);
}

static int type_arg_has_generic_param(const ChengNode *node, const ChengStrList *params) {
    if (!node || !params || params->len == 0) {
        return 0;
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        if (param_names_has(params, node->ident)) {
            return 1;
        }
    }
    for (size_t i = 0; i < node->len; i++) {
        if (type_arg_has_generic_param(node->kids[i], params)) {
            return 1;
        }
    }
    return 0;
}

static int type_args_have_generic_param(const TypeArgList *args, const ChengStrList *params) {
    if (!args || !params || args->len == 0 || params->len == 0) {
        return 0;
    }
    for (size_t i = 0; i < args->len; i++) {
        if (type_arg_has_generic_param(args->items[i], params)) {
            return 1;
        }
    }
    return 0;
}

static void bind_type_vars(const ChengNode *generic_ty,
                           const ChengNode *actual_ty,
                           const ChengStrList *param_names,
                           TypeMap *map) {
    if (!generic_ty || !actual_ty) {
        return;
    }
    if ((generic_ty->kind == NK_IDENT || generic_ty->kind == NK_SYMBOL) && generic_ty->ident) {
        if (param_names_has(param_names, generic_ty->ident)) {
            type_map_add(map, generic_ty->ident, actual_ty);
        }
        return;
    }
    if (generic_ty->kind == NK_BRACKET_EXPR && actual_ty->kind == NK_BRACKET_EXPR) {
        size_t count = generic_ty->len < actual_ty->len ? generic_ty->len : actual_ty->len;
        for (size_t i = 0; i < count; i++) {
            bind_type_vars(generic_ty->kids[i], actual_ty->kids[i], param_names, map);
        }
        return;
    }
    if ((generic_ty->kind == NK_PTR_TY || generic_ty->kind == NK_REF_TY ||
         generic_ty->kind == NK_VAR_TY || generic_ty->kind == NK_SET_TY) &&
        actual_ty->kind == generic_ty->kind) {
        if (generic_ty->len > 0 && actual_ty->len > 0) {
            bind_type_vars(generic_ty->kids[0], actual_ty->kids[0], param_names, map);
        }
        return;
    }
    if (generic_ty->kind == NK_TUPLE_TY && actual_ty->kind == NK_TUPLE_TY) {
        size_t count = generic_ty->len < actual_ty->len ? generic_ty->len : actual_ty->len;
        for (size_t i = 0; i < count; i++) {
            bind_type_vars(generic_ty->kids[i], actual_ty->kids[i], param_names, map);
        }
        return;
    }
    if (generic_ty->kind == NK_FN_TY && actual_ty->kind == NK_FN_TY) {
        if (generic_ty->len > 0 && actual_ty->len > 0) {
            ChengNode *gp = generic_ty->kids[0];
            ChengNode *ap = actual_ty->kids[0];
            if (gp && ap && gp->kind == NK_FORMAL_PARAMS && ap->kind == NK_FORMAL_PARAMS) {
                size_t count = gp->len < ap->len ? gp->len : ap->len;
                for (size_t i = 0; i < count; i++) {
                    ChengNode *gdef = gp->kids[i];
                    ChengNode *adef = ap->kids[i];
                    if (!gdef || !adef || gdef->kind != NK_IDENT_DEFS || adef->kind != NK_IDENT_DEFS) {
                        continue;
                    }
                    if (gdef->len > 1 && adef->len > 1) {
                        bind_type_vars(gdef->kids[1], adef->kids[1], param_names, map);
                    }
                }
            }
        }
        if (generic_ty->len > 1 && actual_ty->len > 1) {
            bind_type_vars(generic_ty->kids[1], actual_ty->kids[1], param_names, map);
        }
        return;
    }
}

static ChengNode *make_type_ident(const char *name, const ChengNode *src) {
    ChengNode *node = cheng_node_new(NK_IDENT, src ? src->line : 0, src ? src->col : 0);
    if (node) {
        cheng_node_set_ident(node, name);
    }
    return node;
}

static ChengNode *infer_lit_type(const ChengNode *node) {
    if (!node) {
        return NULL;
    }
    switch (node->kind) {
        case NK_INT_LIT:
            if (node->int_val > INT32_MAX || node->int_val < INT32_MIN) {
                return make_type_ident("int64", node);
            }
            return make_type_ident("int32", node);
        case NK_FLOAT_LIT:
            return make_type_ident("float64", node);
        case NK_BOOL_LIT:
            return make_type_ident("bool", node);
        case NK_CHAR_LIT:
            return make_type_ident("char", node);
        case NK_STR_LIT:
            return make_type_ident("str", node);
        case NK_NIL_LIT:
            return make_type_ident("nil", node);
        default:
            break;
    }
    return NULL;
}

static const ChengNode *mono_find_fn_return_type_in_node(const ChengNode *node, const char *name) {
    if (!node || !name) {
        return NULL;
    }
    switch (node->kind) {
        case NK_FN_DECL:
        case NK_ITERATOR_DECL:
            if (node->len > 0 && node->kids[0] && node->kids[0]->kind == NK_IDENT &&
                node->kids[0]->ident && strcmp(node->kids[0]->ident, name) == 0) {
                return node->len > 2 ? node->kids[2] : NULL;
            }
            break;
        default:
            break;
    }
    for (size_t i = 0; i < node->len; i++) {
        const ChengNode *found = mono_find_fn_return_type_in_node(node->kids[i], name);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static const ChengNode *mono_find_fn_return_type(MonoCtx *ctx, const char *name) {
    if (!ctx || !ctx->root_list || !name) {
        return NULL;
    }
    return mono_find_fn_return_type_in_node(ctx->root_list, name);
}

static ChengNode *infer_expr_type(MonoCtx *ctx, TypeEnv *env, ChengNode *expr) {
    if (!expr) {
        return NULL;
    }
    if (expr->kind == NK_IDENT && expr->ident) {
        return (ChengNode *)type_env_lookup(env, expr->ident);
    }
    if (expr->kind == NK_CALL_ARG) {
        if (expr->len > 1) {
            return infer_expr_type(ctx, env, expr->kids[1]);
        }
        if (expr->len > 0) {
            return infer_expr_type(ctx, env, expr->kids[0]);
        }
    }
    if (expr->kind == NK_PAR && expr->len == 1) {
        return infer_expr_type(ctx, env, expr->kids[0]);
    }
    ChengNode *lit = infer_lit_type(expr);
    if (lit) {
        return lit;
    }
    if (expr->kind == NK_CALL && expr->len > 0) {
        ChengNode *callee = expr->kids[0];
        if (callee && looks_like_type_expr(callee)) {
            return callee;
        }
        if (callee && callee->kind == NK_BRACKET_EXPR && callee->len > 0) {
            ChengNode *base = callee->kids[0];
            if (base && base->kind == NK_IDENT && base->ident) {
                ChengNode *gen = generic_map_get(&ctx->generic_fns, base->ident);
                if (gen && gen->len > 2) {
                    ChengStrList param_names;
                    cheng_strlist_init(&param_names);
                    ChengNode *gens = gen->len > 4 ? gen->kids[4] : NULL;
                    collect_param_names(gens, &param_names);
                    TypeArgList args;
                    type_args_init(&args);
                    for (size_t i = 1; i < callee->len; i++) {
                        type_args_push(&args, callee->kids[i]);
                    }
                    TypeMap map;
                    type_map_init(&map);
                    for (size_t i = 0; i < param_names.len && i < args.len; i++) {
                        type_map_add(&map, param_names.items[i], args.items[i]);
                    }
                    ChengNode *ret = replace_type_vars(gen->kids[2], &map);
                    type_map_free(&map);
                    type_args_free(&args);
                    cheng_strlist_free(&param_names);
                    return ret;
                }
            }
        }
        if (callee && callee->kind == NK_IDENT && callee->ident) {
            const ChengNode *ret = mono_find_fn_return_type(ctx, callee->ident);
            if (ret) {
                return (ChengNode *)ret;
            }
        }
        if (callee && callee->kind == NK_DOT_EXPR && callee->len > 1) {
            ChengNode *member = callee->kids[1];
            if (member && member->kind == NK_IDENT && member->ident) {
                const ChengNode *ret = mono_find_fn_return_type(ctx, member->ident);
                if (ret) {
                    return (ChengNode *)ret;
                }
            }
        }
    }
    return NULL;
}

static void clear_node_children(ChengNode *node) {
    if (!node) {
        return;
    }
    for (size_t i = 0; i < node->len; i++) {
        cheng_node_free(node->kids[i]);
    }
    free(node->kids);
    node->kids = NULL;
    node->len = 0;
    node->cap = 0;
}

static void replace_node_with_ident(ChengNode *node, const char *name) {
    if (!node || !name) {
        return;
    }
    clear_node_children(node);
    free(node->ident);
    node->ident = NULL;
    free(node->str_val);
    node->str_val = NULL;
    node->kind = NK_IDENT;
    cheng_node_set_ident(node, name);
}

static void replace_node_with(ChengNode *node, ChengNode *other) {
    if (!node || !other || node == other) {
        return;
    }
    clear_node_children(node);
    free(node->ident);
    node->ident = NULL;
    free(node->str_val);
    node->str_val = NULL;
    node->kind = other->kind;
    node->line = other->line;
    node->col = other->col;
    node->type_id = other->type_id;
    node->ident = other->ident;
    node->str_val = other->str_val;
    node->int_val = other->int_val;
    node->float_val = other->float_val;
    node->call_style = other->call_style;
    node->kids = other->kids;
    node->len = other->len;
    node->cap = other->cap;
    other->ident = NULL;
    other->str_val = NULL;
    other->kids = NULL;
    other->len = 0;
    other->cap = 0;
    cheng_node_free(other);
}

static int call_insert_arg(ChengNode *call, size_t index, ChengNode *arg) {
    if (!call || !arg || index > call->len) {
        return -1;
    }
    if (call->len + 1 > call->cap) {
        size_t next = call->cap == 0 ? 8 : call->cap * 2;
        ChengNode **kids = (ChengNode **)realloc(call->kids, next * sizeof(ChengNode *));
        if (!kids) {
            return -1;
        }
        call->kids = kids;
        call->cap = next;
    }
    memmove(call->kids + index + 1, call->kids + index, (call->len - index) * sizeof(ChengNode *));
    call->kids[index] = arg;
    call->len++;
    return 0;
}

static const ChengNode *mono_call_arg_value(const ChengNode *arg) {
    if (!arg) {
        return NULL;
    }
    if (arg->kind == NK_CALL_ARG) {
        if (arg->len > 1) {
            return arg->kids[1];
        }
        if (arg->len > 0) {
            return arg->kids[0];
        }
    }
    return arg;
}

static ChengNode *mono_make_empty(const ChengNode *ref);
static ChengNode *mono_make_ident(const char *name, const ChengNode *ref);

static void instantiate_fn(MonoCtx *ctx, const char *name, const TypeArgList *args);
static void mono_transform(MonoCtx *ctx, TypeEnv *env, ChengNode *node, int in_type_context);
static ChengNode *expand_template_call(MonoCtx *ctx, TypeEnv *env, ChengNode *templ, ChengNode *call);
static void lower_result_unwrap_node(MonoCtx *ctx, TypeEnv *env, ChengNode *node, int returns_result);

static void instantiate_type(MonoCtx *ctx, const char *name, const TypeArgList *args) {
    ChengNode *gen = generic_map_get(&ctx->generic_types, name);
    if (!gen) {
        return;
    }
    ChengNode *gens = gen->len > 2 ? gen->kids[2] : NULL;
    ChengStrList param_names;
    cheng_strlist_init(&param_names);
    collect_param_names(gens, &param_names);
    if (!args || args->len < param_names.len) {
        cheng_strlist_free(&param_names);
        return;
    }
    if (type_args_have_generic_param(args, &param_names)) {
        cheng_strlist_free(&param_names);
        return;
    }
    TypeMap map;
    type_map_init(&map);
    for (size_t i = 0; i < param_names.len && args && i < args->len; i++) {
        type_map_add(&map, param_names.items[i], args->items[i]);
    }
    char *inst_name = mangle_instance(name, args);
    if (!inst_name) {
        type_map_free(&map);
        cheng_strlist_free(&param_names);
        return;
    }
    if (cheng_strlist_has(&ctx->instantiated, inst_name)) {
        free(inst_name);
        type_map_free(&map);
        cheng_strlist_free(&param_names);
        return;
    }
    cheng_strlist_push_unique(&ctx->instantiated, inst_name);
    ChengNode *name_ident = cheng_node_new(NK_IDENT, gen->line, gen->col);
    if (name_ident) {
        cheng_node_set_ident(name_ident, inst_name);
    }
    ChengNode *def = NULL;
    if (gen->kind == NK_TYPE_DECL && gen->len > 1) {
        def = replace_type_vars(gen->kids[1], &map);
    } else if (gen->kind == NK_OBJECT_DECL && gen->len > 1) {
        def = replace_type_vars(gen->kids[1], &map);
    }
    ChengNode *decl = cheng_node_new(gen->kind, gen->line, gen->col);
    if (decl) {
        cheng_node_add(decl, name_ident);
        if (def) {
            cheng_node_add(decl, def);
        } else {
            cheng_node_add(decl, cheng_node_new(NK_EMPTY, gen->line, gen->col));
        }
        cheng_node_add(decl, cheng_node_new(NK_EMPTY, gen->line, gen->col));
        cheng_node_add(decl, cheng_node_new(NK_EMPTY, gen->line, gen->col));
    }
    if (decl) {
        TypeEnv env_local;
        type_env_init(&env_local);
        type_env_push(&env_local);
        mono_transform(ctx, &env_local, decl, 0);
        type_env_pop(&env_local);
        type_env_free(&env_local);
        cheng_node_add(ctx->root_list, decl);
    } else {
        cheng_node_free(name_ident);
        cheng_node_free(def);
    }
    if (strcmp(name, "seq") == 0) {
        instantiate_fn(ctx, "newSeq", args);
        instantiate_fn(ctx, "addPtr", args);
        instantiate_fn(ctx, "get", args);
        instantiate_fn(ctx, "getPtr", args);
        instantiate_fn(ctx, "[]", args);
        instantiate_fn(ctx, "[]=", args);
        instantiate_fn(ctx, "arcRetainSeq", args);
        instantiate_fn(ctx, "arcReleaseSeq", args);
        if (args && args->len > 0 && is_comparable_type_node(args->items[0])) {
            instantiate_fn(ctx, "__cheng_vec_contains", args);
        }
        instantiate_fn(ctx, "__cheng_slice_vec", args);
    }
    if (strcmp(name, "Table") == 0) {
        instantiate_fn(ctx, "TableNext", args);
        instantiate_fn(ctx, "TableHas", args);
        instantiate_fn(ctx, "TableGet", args);
        instantiate_fn(ctx, "TablePut", args);
        instantiate_fn(ctx, "TableLen", args);
        instantiate_fn(ctx, "arcRetainTable", args);
        instantiate_fn(ctx, "arcReleaseTable", args);
    }
    free(inst_name);
    type_map_free(&map);
    cheng_strlist_free(&param_names);
}

static void instantiate_fn(MonoCtx *ctx, const char *name, const TypeArgList *args) {
    ChengNode *gen = generic_map_get(&ctx->generic_fns, name);
    if (!gen) {
        return;
    }
    ChengNode *gens = gen->len > 4 ? gen->kids[4] : NULL;
    ChengStrList param_names;
    cheng_strlist_init(&param_names);
    collect_param_names(gens, &param_names);
    if (!args || args->len < param_names.len) {
        cheng_strlist_free(&param_names);
        return;
    }
    if (type_args_have_generic_param(args, &param_names)) {
        cheng_strlist_free(&param_names);
        return;
    }
    TypeMap map;
    type_map_init(&map);
    for (size_t i = 0; i < param_names.len && args && i < args->len; i++) {
        type_map_add(&map, param_names.items[i], args->items[i]);
    }
    char *inst_name = mangle_instance(name, args);
    if (!inst_name) {
        type_map_free(&map);
        cheng_strlist_free(&param_names);
        return;
    }
    if (cheng_strlist_has(&ctx->instantiated, inst_name)) {
        free(inst_name);
        type_map_free(&map);
        cheng_strlist_free(&param_names);
        return;
    }
    cheng_strlist_push_unique(&ctx->instantiated, inst_name);
    ChengNode *name_ident = cheng_node_new(NK_IDENT, gen->line, gen->col);
    if (name_ident) {
        cheng_node_set_ident(name_ident, inst_name);
    }
    ChengNode *params = gen->len > 1 ? replace_type_vars(gen->kids[1], &map) : NULL;
    ChengNode *ret_type = gen->len > 2 ? replace_type_vars(gen->kids[2], &map) : NULL;
    ChengNode *body = gen->len > 3 ? replace_type_vars(gen->kids[3], &map) : NULL;
    ChengNode *decl = cheng_node_new(NK_FN_DECL, gen->line, gen->col);
    if (decl) {
        cheng_node_add(decl, name_ident);
        cheng_node_add(decl, params ? params : cheng_node_new(NK_FORMAL_PARAMS, gen->line, gen->col));
        cheng_node_add(decl, ret_type ? ret_type : cheng_node_new(NK_EMPTY, gen->line, gen->col));
        cheng_node_add(decl, body ? body : cheng_node_new(NK_EMPTY, gen->line, gen->col));
        cheng_node_add(decl, cheng_node_new(NK_EMPTY, gen->line, gen->col));
        cheng_node_add(decl, cheng_node_new(NK_EMPTY, gen->line, gen->col));
        TypeEnv env_local;
        type_env_init(&env_local);
        type_env_push(&env_local);
        if (params && params->kind == NK_FORMAL_PARAMS) {
            for (size_t i = 0; i < params->len; i++) {
                ChengNode *defs = params->kids[i];
                if (!defs || defs->kind != NK_IDENT_DEFS || defs->len < 2) {
                    continue;
                }
                ChengNode *name_node = defs->kids[0];
                ChengNode *type_node = defs->kids[1];
                if (name_node && name_node->kind == NK_IDENT && name_node->ident && type_node) {
                    type_env_define(&env_local, name_node->ident, type_node);
                }
            }
        }
        mono_transform(ctx, &env_local, decl, 0);
        type_env_pop(&env_local);
        type_env_free(&env_local);
        cheng_node_add(ctx->root_list, decl);
    } else {
        cheng_node_free(name_ident);
        cheng_node_free(params);
        cheng_node_free(ret_type);
        cheng_node_free(body);
    }
    free(inst_name);
    type_map_free(&map);
    cheng_strlist_free(&param_names);
}

static void infer_type_args_from_call(MonoCtx *ctx,
                                      TypeEnv *env,
                                      ChengNode *gen,
                                      ChengNode *call,
                                      TypeArgList *out_args) {
    if (!gen || !call || call->kind != NK_CALL) {
        return;
    }
    ChengNode *gens = gen->len > 4 ? gen->kids[4] : NULL;
    ChengStrList param_names;
    cheng_strlist_init(&param_names);
    collect_param_names(gens, &param_names);
    TypeMap map;
    type_map_init(&map);
    ChengNode *params = gen->len > 1 ? gen->kids[1] : NULL;
    size_t arg_index = 1;
    if (params && params->kind == NK_FORMAL_PARAMS) {
        for (size_t i = 0; i < params->len; i++) {
            ChengNode *defs = params->kids[i];
            if (!defs || defs->kind != NK_IDENT_DEFS || defs->len < 2) {
                continue;
            }
            ChengNode *param_type = defs->kids[1];
            if (arg_index >= call->len) {
                break;
            }
            ChengNode *arg = call->kids[arg_index++];
            ChengNode *arg_val = arg;
            if (arg && arg->kind == NK_CALL_ARG) {
                if (arg->len > 1) {
                    arg_val = arg->kids[1];
                } else if (arg->len > 0) {
                    arg_val = arg->kids[0];
                }
            }
            ChengNode *actual_type = infer_expr_type(ctx, env, arg_val);
            if (actual_type && param_type) {
                bind_type_vars(param_type, actual_type, &param_names, &map);
            }
        }
    }
    for (size_t i = 0; i < param_names.len; i++) {
        const char *pname = param_names.items[i];
        const ChengNode *mapped = type_map_get(&map, pname);
        if (!mapped) {
            type_map_free(&map);
            cheng_strlist_free(&param_names);
            return;
        }
        type_args_push(out_args, mapped);
    }
    type_map_free(&map);
    cheng_strlist_free(&param_names);
}

static void mono_transform_ident_defs(MonoCtx *ctx,
                                      TypeEnv *env,
                                      ChengNode *node,
                                      int defaults_are_types) {
    if (!node || node->kind != NK_IDENT_DEFS) {
        return;
    }
    for (size_t i = 0; i < node->len; i++) {
        ChengNode *child = node->kids[i];
        if (!child) {
            continue;
        }
        if (i == 1) {
            mono_transform(ctx, env, child, 1);
        } else if (i == 2 && defaults_are_types) {
            mono_transform(ctx, env, child, 1);
        } else {
            mono_transform(ctx, env, child, 0);
        }
    }
}

static void mono_transform(MonoCtx *ctx, TypeEnv *env, ChengNode *node, int in_type_context) {
    if (!node) {
        return;
    }
    if (in_type_context && node->kind == NK_BRACKET_EXPR && node->len > 0) {
        for (size_t i = 1; i < node->len; i++) {
            mono_transform(ctx, env, node->kids[i], 1);
        }
        ChengNode *base = node->kids[0];
        if (base && base->kind == NK_IDENT && base->ident) {
            if ((strcmp(base->ident, "seq") == 0 || strcmp(base->ident, "seqT") == 0) &&
                node->len > 2 && mono_is_const_int(node->kids[2])) {
                cheng_node_set_ident(base, "seq_fixed");
            } else if ((strcmp(base->ident, "Table") == 0 || strcmp(base->ident, "table") == 0) &&
                       node->len > 2 && mono_is_const_int(node->kids[2])) {
                cheng_node_set_ident(base, "Table_fixed");
            } else if ((strcmp(base->ident, "str") == 0 || strcmp(base->ident, "cstring") == 0) &&
                       node->len > 1 && mono_is_const_int(node->kids[1])) {
                cheng_node_set_ident(base, "str_fixed");
            }
            TypeArgList args;
            type_args_init(&args);
            for (size_t i = 1; i < node->len; i++) {
                type_args_push(&args, node->kids[i]);
            }
            if (generic_map_get(&ctx->generic_types, base->ident)) {
                instantiate_type(ctx, base->ident, &args);
                char *inst_name = mangle_instance(base->ident, &args);
                if (inst_name) {
                    replace_node_with_ident(node, inst_name);
                    free(inst_name);
                }
            }
            type_args_free(&args);
        }
        return;
    }
    switch (node->kind) {
        case NK_TYPE_DECL:
        case NK_OBJECT_DECL:
            if (is_generic_type_decl(node)) {
                return;
            }
            if (node->len > 0) {
                mono_transform(ctx, env, node->kids[0], 0);
            }
            if (node->len > 1) {
                mono_transform(ctx, env, node->kids[1], 1);
            }
            if (node->len > 2) {
                mono_transform(ctx, env, node->kids[2], 1);
            }
            if (node->len > 3) {
                mono_transform(ctx, env, node->kids[3], 0);
            }
            return;
        case NK_FN_DECL: {
            if (is_generic_fn_decl(node)) {
                return;
            }
            TypeEnv local;
            type_env_init(&local);
            type_env_push(&local);
            if (node->len > 1 && node->kids[1] && node->kids[1]->kind == NK_FORMAL_PARAMS) {
                ChengNode *params = node->kids[1];
                for (size_t i = 0; i < params->len; i++) {
                    ChengNode *defs = params->kids[i];
                    if (!defs || defs->kind != NK_IDENT_DEFS || defs->len < 2) {
                        continue;
                    }
                    ChengNode *name = defs->kids[0];
                    ChengNode *type_node = defs->kids[1];
                    if (name && name->kind == NK_IDENT && name->ident && type_node) {
                        type_env_define(&local, name->ident, type_node);
                    }
                }
                for (size_t i = 0; i < params->len; i++) {
                    mono_transform_ident_defs(ctx, &local, params->kids[i], 0);
                }
            }
            if (node->len > 2 && node->kids[2]) {
                mono_transform(ctx, &local, node->kids[2], 1);
            }
            if (node->len > 3 && node->kids[3]) {
                mono_transform(ctx, &local, node->kids[3], 0);
            }
            type_env_pop(&local);
            type_env_free(&local);
            return;
        }
        case NK_GENERIC_PARAMS:
            for (size_t i = 0; i < node->len; i++) {
                mono_transform_ident_defs(ctx, env, node->kids[i], 1);
            }
            return;
        case NK_IDENT_DEFS:
            mono_transform_ident_defs(ctx, env, node, 0);
            return;
        case NK_TEMPLATE_DECL:
            if (node->len > 0 && node->kids[0] && node->kids[0]->ident) {
                generic_map_add(&ctx->templates, node->kids[0]->ident, node);
            }
            return;
        case NK_MACRO_DECL:
            if (node->len > 0 && node->kids[0] && node->kids[0]->ident) {
                generic_map_add(&ctx->macros, node->kids[0]->ident, node);
            }
            return;
        case NK_LET:
        case NK_VAR:
        case NK_CONST: {
            ChengNode *pattern = node->len > 0 ? node->kids[0] : NULL;
            ChengNode *type_node = node->len > 1 ? node->kids[1] : NULL;
            ChengNode *value = node->len > 2 ? node->kids[2] : NULL;
            if (type_node) {
                mono_transform(ctx, env, type_node, 1);
            }
            if (value) {
                mono_transform(ctx, env, value, 0);
            }
            if (pattern && pattern->kind == NK_PATTERN && pattern->len > 0 &&
                pattern->kids[0] && pattern->kids[0]->kind == NK_IDENT &&
                pattern->kids[0]->ident) {
                const ChengNode *bind_type = NULL;
                if (type_node && type_node->kind != NK_EMPTY) {
                    bind_type = type_node;
                } else if (value) {
                    bind_type = infer_expr_type(ctx, env, value);
                }
                if (bind_type) {
                    type_env_define(env, pattern->kids[0]->ident, bind_type);
                }
            }
            return;
        }
        case NK_ASGN: {
            if (node->len >= 2) {
                ChengNode *lhs = node->kids[0];
                ChengNode *rhs = node->kids[1];
                if (lhs && lhs->kind == NK_BRACKET_EXPR) {
                    ChengNode *call = cheng_node_new(NK_CALL, node->line, node->col);
                    if (!call) {
                        return;
                    }
                    cheng_node_add(call, mono_make_ident("[]=", node));
                    for (size_t i = 0; i < lhs->len; i++) {
                        ChengNode *arg = clone_tree(lhs->kids[i]);
                        cheng_node_add(call, arg ? arg : mono_make_empty(node));
                    }
                    if (rhs) {
                        ChengNode *arg = clone_tree(rhs);
                        cheng_node_add(call, arg ? arg : mono_make_empty(node));
                    }
                    replace_node_with(node, call);
                    mono_transform(ctx, env, node, 0);
                    return;
                }
            }
            break;
        }
        case NK_BRACKET_EXPR: {
            if (!in_type_context && node->len >= 2) {
                if (node->kids[0] &&
                    (node->kids[0]->kind == NK_IDENT || node->kids[0]->kind == NK_SYMBOL) &&
                    node->kids[0]->ident && strcmp(node->kids[0]->ident, "default") == 0) {
                    return;
                }
                ChengNode *call = cheng_node_new(NK_CALL, node->line, node->col);
                if (!call) {
                    return;
                }
                cheng_node_add(call, mono_make_ident("[]", node));
                for (size_t i = 0; i < node->len; i++) {
                    ChengNode *arg = clone_tree(node->kids[i]);
                    cheng_node_add(call, arg ? arg : mono_make_empty(node));
                }
                replace_node_with(node, call);
                mono_transform(ctx, env, node, 0);
                return;
            }
            break;
        }
        case NK_CALL: {
            if (node->len == 0) {
                return;
            }
            ChengNode *callee = node->kids[0];
            if (!mono_keep_dot_calls()) {
                if (callee && callee->kind == NK_BRACKET_EXPR && callee->len > 0 &&
                    callee->kids[0] && callee->kids[0]->kind == NK_DOT_EXPR) {
                    ChengNode *dot = callee->kids[0];
                    ChengNode *base = dot->len > 0 ? dot->kids[0] : NULL;
                    ChengNode *member = dot->len > 1 ? dot->kids[1] : NULL;
                    if (member && member->kind == NK_IDENT && member->ident && base) {
                        int base_is_value = mono_base_is_value(env, base);
                        if (base_is_value) {
                            ChengNode *base_arg = clone_tree(base);
                            if (base_arg) {
                                call_insert_arg(node, 1, base_arg);
                            }
                        }
                        for (size_t i = 1; i < callee->len; i++) {
                            mono_transform(ctx, env, callee->kids[i], 1);
                        }
                        TypeArgList args;
                        type_args_init(&args);
                        for (size_t i = 1; i < callee->len; i++) {
                            type_args_push(&args, callee->kids[i]);
                        }
                        if (generic_map_get(&ctx->generic_fns, member->ident)) {
                            instantiate_fn(ctx, member->ident, &args);
                            char *inst_name = mangle_instance(member->ident, &args);
                            if (inst_name) {
                                ChengNode *new_callee = cheng_node_new(NK_IDENT, callee->line, callee->col);
                                if (new_callee) {
                                    cheng_node_set_ident(new_callee, inst_name);
                                    node->kids[0] = new_callee;
                                    cheng_node_free(callee);
                                }
                                free(inst_name);
                            }
                        } else {
                            ChengNode *new_callee = cheng_node_new(NK_IDENT, callee->line, callee->col);
                            if (new_callee) {
                                cheng_node_set_ident(new_callee, member->ident);
                                node->kids[0] = new_callee;
                                cheng_node_free(callee);
                            }
                        }
                        type_args_free(&args);
                    }
                } else if (callee && callee->kind == NK_DOT_EXPR) {
                    ChengNode *base = callee->len > 0 ? callee->kids[0] : NULL;
                    ChengNode *member = callee->len > 1 ? callee->kids[1] : NULL;
                    if (member && member->kind == NK_IDENT && member->ident && base) {
                        int base_is_value = mono_base_is_value(env, base);
                        if (base_is_value) {
                            ChengNode *base_arg = clone_tree(base);
                            if (base_arg) {
                                call_insert_arg(node, 1, base_arg);
                            }
                        }
                        ChengNode *new_callee = cheng_node_new(NK_IDENT, callee->line, callee->col);
                        if (new_callee) {
                            cheng_node_set_ident(new_callee, member->ident);
                            node->kids[0] = new_callee;
                            cheng_node_free(callee);
                        }
                    }
                }
            }
            if (!ctx->hard_rt) {
                callee = node->kids[0];
                if (callee && (callee->kind == NK_IDENT || callee->kind == NK_SYMBOL) && callee->ident) {
                    ChengNode *templ = generic_map_get(&ctx->templates, callee->ident);
                    if (!templ) {
                        templ = generic_map_get(&ctx->macros, callee->ident);
                    }
                    if (templ) {
                        ChengNode *expanded = expand_template_call(ctx, env, templ, node);
                        if (expanded) {
                            replace_node_with(node, expanded);
                            return;
                        }
                    }
                }
            }
            callee = node->kids[0];
            int is_type_call = 0;
            if (node->len == 2 && callee && is_definite_type_callee(ctx, callee)) {
                is_type_call = 1;
            }
            if (callee && callee->kind == NK_BRACKET_EXPR && callee->len > 0) {
                ChengNode *base = callee->kids[0];
                if (base && base->kind == NK_IDENT && base->ident &&
                    generic_map_get(&ctx->generic_fns, base->ident)) {
                    for (size_t i = 1; i < callee->len; i++) {
                        mono_transform(ctx, env, callee->kids[i], 1);
                    }
                    TypeArgList args;
                    type_args_init(&args);
                    for (size_t i = 1; i < callee->len; i++) {
                        type_args_push(&args, callee->kids[i]);
                    }
                    instantiate_fn(ctx, base->ident, &args);
                    char *inst_name = mangle_instance(base->ident, &args);
                    if (inst_name) {
                        ChengNode *new_callee = cheng_node_new(NK_IDENT, callee->line, callee->col);
                        if (new_callee) {
                            cheng_node_set_ident(new_callee, inst_name);
                            node->kids[0] = new_callee;
                            cheng_node_free(callee);
                        }
                        free(inst_name);
                    }
                    type_args_free(&args);
                }
            } else if (callee && callee->kind == NK_IDENT && callee->ident &&
                       generic_map_get(&ctx->generic_fns, callee->ident) && !is_type_call) {
                TypeArgList inferred;
                type_args_init(&inferred);
                ChengNode *gen = generic_map_get(&ctx->generic_fns, callee->ident);
                infer_type_args_from_call(ctx, env, gen, node, &inferred);
                if (inferred.len > 0) {
                    instantiate_fn(ctx, callee->ident, &inferred);
                    char *inst_name = mangle_instance(callee->ident, &inferred);
                    if (inst_name) {
                        replace_node_with_ident(callee, inst_name);
                        free(inst_name);
                    }
                }
                type_args_free(&inferred);
            }
            callee = node->kids[0];
            if (callee && looks_like_type_expr(callee)) {
                mono_transform(ctx, env, callee, 1);
            } else if (callee) {
                mono_transform(ctx, env, callee, 0);
            }
            int args_are_types = 0;
            if (callee && callee->kind == NK_IDENT && callee->ident) {
                if (strcmp(callee->ident, "default") == 0 ||
                    strcmp(callee->ident, "sizeof") == 0 ||
                    strcmp(callee->ident, "alignof") == 0) {
                    args_are_types = 1;
                }
            }
            for (size_t i = 1; i < node->len; i++) {
                mono_transform(ctx, env, node->kids[i], args_are_types ? 1 : 0);
            }
            return;
        }
        default:
            break;
    }
    for (size_t i = 0; i < node->len; i++) {
        mono_transform(ctx, env, node->kids[i], in_type_context);
    }
}

static ChengNode *expand_template_call(MonoCtx *ctx, TypeEnv *env, ChengNode *templ, ChengNode *call) {
    if (!ctx || !templ || !call) {
        return NULL;
    }
    ChengNode *params = templ->len > 1 ? templ->kids[1] : NULL;
    ChengNode *body = templ->len > 3 ? templ->kids[3] : NULL;
    if (!params || params->kind != NK_FORMAL_PARAMS || !body) {
        return NULL;
    }
    TypeMap map;
    type_map_init(&map);
    size_t arg_index = 1;
    for (size_t i = 0; i < params->len; i++) {
        ChengNode *defs = params->kids[i];
        if (!defs || defs->len == 0) {
            continue;
        }
        if (arg_index >= call->len) {
            break;
        }
        ChengNode *name_node = defs->kids[0];
        const ChengNode *arg = mono_call_arg_value(call->kids[arg_index]);
        if (name_node && name_node->kind == NK_IDENT && name_node->ident && arg) {
            type_map_add(&map, name_node->ident, arg);
        }
        arg_index++;
    }
    ChengNode *expanded = replace_type_vars(body, &map);
    type_map_free(&map);
    if (!expanded) {
        return NULL;
    }
    if (expanded->kind == NK_STMT_LIST && expanded->len == 1 && expanded->kids[0]) {
        ChengNode *only = expanded->kids[0];
        expanded->kids[0] = NULL;
        expanded->len = 0;
        cheng_node_free(expanded);
        expanded = only;
    }
    mono_transform(ctx, env, expanded, 0);
    return expanded;
}

static int mono_is_result_unwrap_expr(const ChengNode *expr) {
    if (!expr || expr->kind != NK_POSTFIX || expr->len < 2) {
        return 0;
    }
    const ChengNode *op = expr->kids[0];
    if (!op || (op->kind != NK_IDENT && op->kind != NK_SYMBOL) || !op->ident) {
        return 0;
    }
    return strcmp(op->ident, "?") == 0;
}

static const ChengNode *mono_unwrap_postfix_value(const ChengNode *expr) {
    if (!expr || expr->kind != NK_POSTFIX) {
        return NULL;
    }
    if (expr->len > 1) {
        return expr->kids[1];
    }
    if (expr->len > 0) {
        return expr->kids[0];
    }
    return NULL;
}

static ChengNode *mono_make_empty(const ChengNode *ref) {
    return cheng_node_new(NK_EMPTY, ref ? ref->line : 0, ref ? ref->col : 0);
}

static ChengNode *mono_make_ident(const char *name, const ChengNode *ref) {
    ChengNode *node = cheng_node_new(NK_IDENT, ref ? ref->line : 0, ref ? ref->col : 0);
    if (node && name) {
        cheng_node_set_ident(node, name);
    }
    return node;
}

static ChengNode *mono_make_symbol(const char *name, const ChengNode *ref) {
    ChengNode *node = cheng_node_new(NK_SYMBOL, ref ? ref->line : 0, ref ? ref->col : 0);
    if (node && name) {
        cheng_node_set_ident(node, name);
    }
    return node;
}

static ChengNode *mono_make_pattern_ident(const char *name, const ChengNode *ref) {
    ChengNode *pattern = cheng_node_new(NK_PATTERN, ref ? ref->line : 0, ref ? ref->col : 0);
    ChengNode *ident = mono_make_ident(name, ref);
    if (pattern && ident) {
        cheng_node_add(pattern, ident);
    }
    return pattern;
}

static ChengNode *mono_make_dot_expr(ChengNode *base, const char *member, const ChengNode *ref) {
    ChengNode *dot = cheng_node_new(NK_DOT_EXPR, ref ? ref->line : 0, ref ? ref->col : 0);
    if (!dot) {
        cheng_node_free(base);
        return NULL;
    }
    cheng_node_add(dot, base ? base : mono_make_empty(ref));
    cheng_node_add(dot, mono_make_ident(member, ref));
    return dot;
}

static ChengNode *mono_make_prefix_not(ChengNode *expr, const ChengNode *ref) {
    ChengNode *node = cheng_node_new(NK_PREFIX, ref ? ref->line : 0, ref ? ref->col : 0);
    if (!node) {
        cheng_node_free(expr);
        return NULL;
    }
    cheng_node_add(node, mono_make_symbol("!", ref));
    cheng_node_add(node, expr ? expr : mono_make_empty(ref));
    return node;
}

static ChengNode *mono_make_call(const char *name, ChengNode *arg, const ChengNode *ref) {
    ChengNode *call = cheng_node_new(NK_CALL, ref ? ref->line : 0, ref ? ref->col : 0);
    if (!call) {
        cheng_node_free(arg);
        return NULL;
    }
    cheng_node_add(call, mono_make_ident(name, ref));
    if (arg) {
        cheng_node_add(call, arg);
    }
    return call;
}

static ChengNode *mono_make_stmt_list(const ChengNode *ref, ChengNode *stmt) {
    ChengNode *list = cheng_node_new(NK_STMT_LIST, ref ? ref->line : 0, ref ? ref->col : 0);
    if (!list) {
        cheng_node_free(stmt);
        return NULL;
    }
    if (stmt) {
        cheng_node_add(list, stmt);
    }
    return list;
}

static ChengNode *mono_make_if(ChengNode *cond, ChengNode *body, const ChengNode *ref) {
    ChengNode *node = cheng_node_new(NK_IF, ref ? ref->line : 0, ref ? ref->col : 0);
    if (!node) {
        cheng_node_free(cond);
        cheng_node_free(body);
        return NULL;
    }
    cheng_node_add(node, cond ? cond : mono_make_empty(ref));
    cheng_node_add(node, body ? body : mono_make_empty(ref));
    return node;
}

static ChengNode *mono_make_return(ChengNode *expr, const ChengNode *ref) {
    ChengNode *node = cheng_node_new(NK_RETURN, ref ? ref->line : 0, ref ? ref->col : 0);
    if (!node) {
        cheng_node_free(expr);
        return NULL;
    }
    if (expr) {
        cheng_node_add(node, expr);
    } else {
        cheng_node_add(node, mono_make_empty(ref));
    }
    return node;
}

static ChengNode *mono_make_var_decl_typed(const char *name,
                                           ChengNode *type_expr,
                                           ChengNode *value,
                                           const ChengNode *ref) {
    ChengNode *node = cheng_node_new(NK_VAR, ref ? ref->line : 0, ref ? ref->col : 0);
    if (!node) {
        cheng_node_free(type_expr);
        cheng_node_free(value);
        return NULL;
    }
    cheng_node_add(node, mono_make_pattern_ident(name, ref));
    cheng_node_add(node, type_expr ? type_expr : mono_make_empty(ref));
    cheng_node_add(node, value ? value : mono_make_empty(ref));
    cheng_node_add(node, mono_make_empty(ref));
    return node;
}

static ChengNode *mono_make_var_decl(const char *name, ChengNode *value, const ChengNode *ref) {
    return mono_make_var_decl_typed(name, NULL, value, ref);
}

static ChengNode *mono_make_assign(ChengNode *lhs, ChengNode *rhs, const ChengNode *ref) {
    ChengNode *node = cheng_node_new(NK_ASGN, ref ? ref->line : 0, ref ? ref->col : 0);
    if (!node) {
        cheng_node_free(lhs);
        cheng_node_free(rhs);
        return NULL;
    }
    cheng_node_add(node, lhs ? lhs : mono_make_empty(ref));
    cheng_node_add(node, rhs ? rhs : mono_make_empty(ref));
    return node;
}

static const ChengNode *mono_strip_var_type(const ChengNode *node) {
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_VAR_TY && node->len > 0) {
        return node->kids[0];
    }
    return node;
}

static const ChengNode *mono_pattern_ident(const ChengNode *pattern) {
    if (!pattern) {
        return NULL;
    }
    if (pattern->kind == NK_PATTERN && pattern->len > 0) {
        pattern = pattern->kids[0];
    }
    if (pattern && pattern->kind == NK_IDENT) {
        return pattern;
    }
    return NULL;
}

static int mono_is_result_base_name(const ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (node->kind == NK_DOT_EXPR && node->len > 1) {
        node = node->kids[1];
    }
    if ((node->kind == NK_IDENT || node->kind == NK_SYMBOL) && node->ident) {
        if (strcmp(node->ident, "Result") == 0) {
            return 1;
        }
        if (strncmp(node->ident, "Result_", 7) == 0) {
            return 1;
        }
    }
    return 0;
}

static int mono_is_result_type_node(const ChengNode *node) {
    node = mono_strip_var_type(node);
    if (!node) {
        return 0;
    }
    if (node->kind == NK_BRACKET_EXPR && node->len > 0) {
        return mono_is_result_base_name(node->kids[0]);
    }
    return mono_is_result_base_name(node);
}

static const ChengNode *mono_result_value_type(const ChengNode *node) {
    node = mono_strip_var_type(node);
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_BRACKET_EXPR && node->len > 1) {
        ChengNode *base = node->kids[0];
        if (mono_is_result_base_name(base)) {
            return node->kids[1];
        }
    }
    return NULL;
}

static char *mono_next_unwrap_name(MonoCtx *ctx) {
    char buf[64];
    size_t idx = ctx ? ctx->unwrap_counter++ : 0;
    snprintf(buf, sizeof(buf), "__res_q%zu", idx);
    return mono_strdup(buf);
}

static int stmt_list_push(ChengNode ***items, size_t *len, size_t *cap, ChengNode *node) {
    if (!node) {
        return 0;
    }
    if (*len + 1 > *cap) {
        size_t next = *cap == 0 ? 8 : *cap * 2;
        ChengNode **out = (ChengNode **)realloc(*items, next * sizeof(ChengNode *));
        if (!out) {
            return -1;
        }
        *items = out;
        *cap = next;
    }
    (*items)[(*len)++] = node;
    return 0;
}

static void lower_result_unwrap_stmt_list(MonoCtx *ctx, TypeEnv *env, ChengNode *list, int returns_result) {
    if (!list || list->kind != NK_STMT_LIST) {
        return;
    }
    int pushed = 0;
    if (env && type_env_push(env) == 0) {
        pushed = 1;
    }
    ChengNode **out = NULL;
    size_t out_len = 0;
    size_t out_cap = 0;
    for (size_t i = 0; i < list->len; i++) {
        ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        lower_result_unwrap_node(ctx, env, stmt, returns_result);
        const ChengNode *value = NULL;
        int kind = 0;
        if (stmt->kind == NK_LET || stmt->kind == NK_VAR || stmt->kind == NK_CONST) {
            if (stmt->len > 2) {
                value = stmt->kids[2];
            }
            kind = 1;
        } else if (stmt->kind == NK_ASGN) {
            if (stmt->len > 1) {
                value = stmt->kids[1];
            }
            kind = 2;
        } else if (stmt->kind == NK_RETURN) {
            if (stmt->len > 0) {
                value = stmt->kids[0];
            }
            kind = 3;
        } else if (mono_is_result_unwrap_expr(stmt)) {
            value = stmt;
            kind = 4;
        }
        if (kind != 0 && mono_is_result_unwrap_expr(value)) {
            const ChengNode *inner = mono_unwrap_postfix_value(value);
            char *tmp_name = mono_next_unwrap_name(ctx);
            const ChengNode *inner_type = infer_expr_type(ctx, env, (ChengNode *)inner);
            ChengNode *tmp_type = inner_type ? clone_tree(inner_type) : NULL;
            if (tmp_type) {
                mono_transform(ctx, env, tmp_type, 1);
            }
            ChengNode *tmp_decl = mono_make_var_decl_typed(tmp_name, tmp_type, clone_tree(inner), value);
            if (inner_type) {
                type_env_define(env, tmp_name, inner_type);
            }
            ChengNode *cond = mono_make_prefix_not(
                mono_make_dot_expr(mono_make_ident(tmp_name, value), "ok", value),
                value);
            ChengNode *body_stmt = NULL;
            if (returns_result) {
                body_stmt = mono_make_return(mono_make_ident(tmp_name, value), value);
            } else {
                body_stmt = mono_make_call("panic",
                                           mono_make_dot_expr(mono_make_ident(tmp_name, value), "err", value),
                                           value);
            }
            ChengNode *body_list = mono_make_stmt_list(value, body_stmt);
            ChengNode *if_stmt = mono_make_if(cond, body_list, value);
            ChengNode *new_stmt = NULL;
            if (kind == 1) {
                ChengNode *pattern = stmt->len > 0 ? clone_tree(stmt->kids[0]) : mono_make_empty(value);
                const ChengNode *decl_type = stmt->len > 1 ? stmt->kids[1] : NULL;
                const ChengNode *bind_type = NULL;
                if (decl_type && decl_type->kind != NK_EMPTY) {
                    bind_type = decl_type;
                } else if (inner_type) {
                    bind_type = mono_result_value_type(inner_type);
                }
                ChengNode *type_node = bind_type ? clone_tree(bind_type) : mono_make_empty(value);
                ChengNode *pragma_node = stmt->len > 3 ? clone_tree(stmt->kids[3]) : mono_make_empty(value);
                ChengNode *rhs = mono_make_dot_expr(mono_make_ident(tmp_name, value), "value", value);
                ChengNode *decl = cheng_node_new(stmt->kind, stmt->line, stmt->col);
                if (decl) {
                    cheng_node_add(decl, pattern ? pattern : mono_make_empty(value));
                    cheng_node_add(decl, type_node ? type_node : mono_make_empty(value));
                    cheng_node_add(decl, rhs ? rhs : mono_make_empty(value));
                    cheng_node_add(decl, pragma_node ? pragma_node : mono_make_empty(value));
                    new_stmt = decl;
                } else {
                    cheng_node_free(pattern);
                    cheng_node_free(type_node);
                    cheng_node_free(pragma_node);
                    cheng_node_free(rhs);
                }
                const ChengNode *ident = mono_pattern_ident(stmt->len > 0 ? stmt->kids[0] : NULL);
                if (ident && bind_type) {
                    type_env_define(env, ident->ident, bind_type);
                }
            } else if (kind == 2) {
                ChengNode *lhs = stmt->len > 0 ? clone_tree(stmt->kids[0]) : mono_make_empty(value);
                ChengNode *rhs = mono_make_dot_expr(mono_make_ident(tmp_name, value), "value", value);
                new_stmt = mono_make_assign(lhs, rhs, value);
            } else if (kind == 3) {
                new_stmt = mono_make_return(mono_make_dot_expr(mono_make_ident(tmp_name, value), "value", value), value);
            }
            if (stmt_list_push(&out, &out_len, &out_cap, tmp_decl) != 0 ||
                stmt_list_push(&out, &out_len, &out_cap, if_stmt) != 0 ||
                (new_stmt && stmt_list_push(&out, &out_len, &out_cap, new_stmt) != 0)) {
                cheng_node_free(tmp_decl);
                cheng_node_free(if_stmt);
                cheng_node_free(new_stmt);
            }
            cheng_node_free(stmt);
            free(tmp_name);
            continue;
        }
        if (stmt->kind == NK_LET || stmt->kind == NK_VAR || stmt->kind == NK_CONST) {
            const ChengNode *pattern = stmt->len > 0 ? stmt->kids[0] : NULL;
            const ChengNode *ident = mono_pattern_ident(pattern);
            const ChengNode *type_node = stmt->len > 1 ? stmt->kids[1] : NULL;
            const ChengNode *rhs = stmt->len > 2 ? stmt->kids[2] : NULL;
            const ChengNode *bind_type = NULL;
            if (type_node && type_node->kind != NK_EMPTY) {
                bind_type = type_node;
            } else if (rhs) {
                bind_type = infer_expr_type(ctx, env, (ChengNode *)rhs);
            }
            if (ident && bind_type) {
                type_env_define(env, ident->ident, bind_type);
            }
        }
        if (stmt_list_push(&out, &out_len, &out_cap, stmt) != 0) {
            cheng_node_free(stmt);
        }
    }
    free(list->kids);
    list->kids = out;
    list->len = out_len;
    list->cap = out_cap;
    if (pushed) {
        type_env_pop(env);
    }
}

static void lower_result_unwrap_node(MonoCtx *ctx, TypeEnv *env, ChengNode *node, int returns_result) {
    if (!node) {
        return;
    }
    switch (node->kind) {
        case NK_FN_DECL:
        case NK_ITERATOR_DECL: {
            ChengNode *ret_type = node->len > 2 ? node->kids[2] : NULL;
            ChengNode *body = node->len > 3 ? node->kids[3] : NULL;
            int fn_returns_result = mono_is_result_type_node(ret_type);
            if (body) {
                TypeEnv local;
                type_env_init(&local);
                type_env_push(&local);
                if (node->len > 1 && node->kids[1] && node->kids[1]->kind == NK_FORMAL_PARAMS) {
                    ChengNode *params = node->kids[1];
                    for (size_t i = 0; i < params->len; i++) {
                        ChengNode *defs = params->kids[i];
                        if (!defs || defs->kind != NK_IDENT_DEFS || defs->len < 2) {
                            continue;
                        }
                        ChengNode *name = defs->kids[0];
                        ChengNode *type_node = defs->kids[1];
                        if (name && name->kind == NK_IDENT && name->ident && type_node) {
                            type_env_define(&local, name->ident, type_node);
                        }
                    }
                }
                lower_result_unwrap_node(ctx, &local, body, fn_returns_result);
                type_env_pop(&local);
                type_env_free(&local);
            }
            return;
        }
        case NK_STMT_LIST:
            lower_result_unwrap_stmt_list(ctx, env, node, returns_result);
            return;
        default:
            break;
    }
    for (size_t i = 0; i < node->len; i++) {
        lower_result_unwrap_node(ctx, env, node->kids[i], returns_result);
    }
}

static const char *fn_first_param_base_name(const ChengNode *fn_node) {
    if (!fn_node || fn_node->len < 2) {
        return NULL;
    }
    ChengNode *params = fn_node->kids[1];
    if (!params || params->kind != NK_FORMAL_PARAMS || params->len == 0) {
        return NULL;
    }
    ChengNode *defs = params->kids[0];
    if (!defs || defs->kind != NK_IDENT_DEFS || defs->len < 2) {
        return NULL;
    }
    ChengNode *param_type = defs->kids[1];
    while (param_type && (param_type->kind == NK_VAR_TY || param_type->kind == NK_REF_TY ||
                          param_type->kind == NK_PTR_TY) &&
           param_type->len > 0) {
        param_type = param_type->kids[0];
    }
    if (param_type && param_type->kind == NK_BRACKET_EXPR && param_type->len > 0 &&
        param_type->kids[0] && param_type->kids[0]->kind == NK_IDENT) {
        return param_type->kids[0]->ident;
    }
    if (param_type && param_type->kind == NK_IDENT && param_type->ident) {
        return param_type->ident;
    }
    return NULL;
}

static void collect_generics(MonoCtx *ctx, ChengNode *node) {
    if (!node) {
        return;
    }
    if (is_generic_type_decl(node)) {
        ChengNode *name = node->kids[0];
        if (name && name->kind == NK_IDENT && name->ident) {
            generic_map_add(&ctx->generic_types, name->ident, node);
        }
    }
    if (is_generic_fn_decl(node)) {
        ChengNode *name = node->kids[0];
        if (name && name->kind == NK_IDENT && name->ident) {
            ChengNode *existing = generic_map_get(&ctx->generic_fns, name->ident);
            if (existing && (strcmp(name->ident, "[]") == 0 || strcmp(name->ident, "[]=") == 0)) {
                const char *existing_base = fn_first_param_base_name(existing);
                const char *new_base = fn_first_param_base_name(node);
                if (existing_base && new_base &&
                    strcmp(existing_base, "seq_fixed") == 0 && strcmp(new_base, "seq") == 0) {
                    generic_map_set(&ctx->generic_fns, name->ident, node);
                }
            } else {
                generic_map_add(&ctx->generic_fns, name->ident, node);
            }
        }
    }
    for (size_t i = 0; i < node->len; i++) {
        collect_generics(ctx, node->kids[i]);
    }
}

int cheng_monomorphize(ChengNode *root, ChengDiagList *diags) {
    if (!root) {
        return -1;
    }
    ChengNode *list = root;
    if (root->kind == NK_MODULE && root->len > 0) {
        list = root->kids[0];
    }
    if (!list || list->kind != NK_STMT_LIST) {
        return 0;
    }
    MonoCtx ctx;
    ctx.diags = diags;
    ctx.root_list = list;
    generic_map_init(&ctx.generic_types);
    generic_map_init(&ctx.generic_fns);
    generic_map_init(&ctx.templates);
    generic_map_init(&ctx.macros);
    cheng_strlist_init(&ctx.instantiated);
    ctx.unwrap_counter = 0;
    ctx.hard_rt = mono_env_is_hard_rt();
    collect_generics(&ctx, list);
    TypeEnv env;
    type_env_init(&env);
    type_env_push(&env);
    for (size_t i = 0; i < list->len; i++) {
        mono_transform(&ctx, &env, list->kids[i], 0);
    }
    type_env_pop(&env);
    type_env_free(&env);
    size_t out = 0;
    for (size_t i = 0; i < list->len; i++) {
        ChengNode *stmt = list->kids[i];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_TEMPLATE_DECL || stmt->kind == NK_MACRO_DECL) {
            if (ctx.hard_rt) {
                list->kids[out++] = stmt;
            }
            continue;
        }
        if (stmt->kind == NK_TYPE_DECL && stmt->len > 0 && stmt->kids[0] &&
            (stmt->kids[0]->kind == NK_OBJECT_DECL || stmt->kids[0]->kind == NK_TYPE_DECL)) {
            size_t child_out = 0;
            for (size_t j = 0; j < stmt->len; j++) {
                ChengNode *child = stmt->kids[j];
                if (!child) {
                    continue;
                }
                if (is_generic_type_decl(child)) {
                    cheng_node_free(child);
                    continue;
                }
                stmt->kids[child_out++] = child;
            }
            stmt->len = child_out;
            if (child_out == 0) {
                continue;
            }
        }
        if (is_generic_type_decl(stmt) || is_generic_fn_decl(stmt)) {
            continue;
        }
        list->kids[out++] = stmt;
    }
    list->len = out;
    if (!ctx.hard_rt) {
        TypeEnv env_local;
        type_env_init(&env_local);
        lower_result_unwrap_node(&ctx, &env_local, list, 0);
        type_env_free(&env_local);
    }
    generic_map_free(&ctx.generic_types);
    generic_map_free(&ctx.generic_fns);
    generic_map_free(&ctx.templates);
    generic_map_free(&ctx.macros);
    cheng_strlist_free(&ctx.instantiated);
    return 0;
}
