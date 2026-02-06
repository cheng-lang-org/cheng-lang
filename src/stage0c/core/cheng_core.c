#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "cheng_core.h"
#include "asm_emit.h"
#include "c_emit.h"
#include "diagnostics.h"
#include "lexer.h"
#include "macro_expand.h"
#include "monomorphize.h"
#include "parser.h"
#include "semantics.h"
#include "strlist.h"

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    char buf[8192];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

static int file_mtime(const char *path, time_t *out) {
    struct stat st;
    if (path == NULL || out == NULL) {
        return -1;
    }
    if (stat(path, &st) != 0) {
        return -1;
    }
    *out = st.st_mtime;
    return 0;
}

static int path_is_abs(const char *path) {
    return path != NULL && path[0] == '/';
}

static char *path_dirname(const char *path) {
    if (!path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return strdup(".");
    }
    size_t len = (size_t)(slash - path);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static int has_suffix(const char *s, const char *suffix) {
    size_t n = s ? strlen(s) : 0;
    size_t m = suffix ? strlen(suffix) : 0;
    if (n < m) {
        return 0;
    }
    return strcmp(s + (n - m), suffix) == 0;
}

static int has_prefix(const char *s, const char *prefix) {
    size_t n = s ? strlen(s) : 0;
    size_t m = prefix ? strlen(prefix) : 0;
    if (n < m) {
        return 0;
    }
    return strncmp(s, prefix, m) == 0;
}

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} ArgList;

static void args_init(ArgList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static void args_free(ArgList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static int args_reserve(ArgList *list, size_t extra) {
    if (!list) {
        return -1;
    }
    size_t need = list->len + extra + 1;
    if (need <= list->cap) {
        return 0;
    }
    size_t next = list->cap == 0 ? 8 : list->cap * 2;
    while (next < need) {
        next *= 2;
    }
    char **items = (char **)realloc(list->items, next * sizeof(char *));
    if (!items) {
        return -1;
    }
    list->items = items;
    list->cap = next;
    return 0;
}

static int args_push(ArgList *list, const char *value) {
    if (!list || !value) {
        return -1;
    }
    if (args_reserve(list, 1) != 0) {
        return -1;
    }
    char *copy = strdup(value);
    if (!copy) {
        return -1;
    }
    list->items[list->len++] = copy;
    return 0;
}

static int split_args(const char *input, ArgList *out) {
    if (!input || !out) {
        return 0;
    }
    size_t i = 0;
    while (input[i] != '\0') {
        while (input[i] != '\0' && isspace((unsigned char)input[i])) {
            i++;
        }
        if (input[i] == '\0') {
            break;
        }
        size_t cap = 64;
        size_t len = 0;
        char *buf = (char *)malloc(cap);
        if (!buf) {
            return -1;
        }
        char quote = 0;
        while (input[i] != '\0') {
            char c = input[i];
            if (quote == 0 && isspace((unsigned char)c)) {
                break;
            }
            if (quote == 0 && (c == '\'' || c == '"')) {
                quote = c;
                i++;
                continue;
            }
            if (quote != 0 && c == quote) {
                quote = 0;
                i++;
                continue;
            }
            if (c == '\\' && input[i + 1] != '\0' && quote != '\'') {
                i++;
                c = input[i];
            }
            if (len + 1 >= cap) {
                cap *= 2;
                char *next = (char *)realloc(buf, cap);
                if (!next) {
                    free(buf);
                    return -1;
                }
                buf = next;
            }
            buf[len++] = c;
            i++;
        }
        buf[len] = '\0';
        if (len > 0) {
            if (args_push(out, buf) != 0) {
                free(buf);
                return -1;
            }
        }
        free(buf);
        while (input[i] != '\0' && isspace((unsigned char)input[i])) {
            i++;
        }
    }
    return 0;
}

static int is_trim_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static char *path_join(const char *a, const char *b) {
    if (!a || !b) {
        return NULL;
    }
    size_t alen = strlen(a);
    size_t blen = strlen(b);
    size_t need = alen + blen + 2;
    char *out = (char *)malloc(need);
    if (!out) {
        return NULL;
    }
    memcpy(out, a, alen);
    if (alen == 0 || a[alen - 1] != '/') {
        out[alen] = '/';
        memcpy(out + alen + 1, b, blen);
        out[alen + 1 + blen] = '\0';
    } else {
        memcpy(out + alen, b, blen);
        out[alen + blen] = '\0';
    }
    return out;
}

static char *ensure_cheng_suffix(const char *path) {
    if (!path) {
        return NULL;
    }
    if (has_suffix(path, ".cheng")) {
        return strdup(path);
    }
    size_t n = strlen(path);
    char *out = (char *)malloc(n + 7);
    if (!out) {
        return NULL;
    }
    memcpy(out, path, n);
    memcpy(out + n, ".cheng", 7);
    return out;
}

static char *path_abs(const char *path) {
    if (!path || path[0] == '\0') {
        return NULL;
    }
    char buf[PATH_MAX];
    if (realpath(path, buf)) {
        return strdup(buf);
    }
    return strdup(path);
}

static int path_is_within_root(const char *root, const char *path) {
    if (!root || !path) {
        return 0;
    }
    size_t root_len = strlen(root);
    if (root_len == 0) {
        return 0;
    }
    while (root_len > 1 && root[root_len - 1] == '/') {
        root_len--;
    }
    if (strncmp(path, root, root_len) != 0) {
        return 0;
    }
    return path[root_len] == '\0' || path[root_len] == '/';
}

static int path_is_within_env_root(const char *env_name, const char *abs_path) {
    const char *root = getenv(env_name);
    if (!root || root[0] == '\0' || !abs_path || abs_path[0] == '\0') {
        return 0;
    }
    char *root_abs = path_abs(root);
    if (!root_abs) {
        return 0;
    }
    int ok = path_is_within_root(root_abs, abs_path);
    free(root_abs);
    return ok;
}

static int path_is_within_pkg_roots(const char *abs_path) {
    const char *raw = getenv("CHENG_PKG_ROOTS");
    if (!raw || raw[0] == '\0' || !abs_path || abs_path[0] == '\0') {
        return 0;
    }
    int use_colon = 1;
    for (const char *p = raw; *p; p++) {
        if (*p == ',' || *p == ';') {
            use_colon = 0;
            break;
        }
    }
    size_t len = strlen(raw);
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || raw[i] == ',' || raw[i] == ';' || (use_colon && raw[i] == ':')) {
            size_t seg_start = start;
            size_t seg_end = i;
            while (seg_start < seg_end && is_trim_char(raw[seg_start])) {
                seg_start++;
            }
            while (seg_end > seg_start && is_trim_char(raw[seg_end - 1])) {
                seg_end--;
            }
            if (seg_end > seg_start) {
                size_t seg_len = seg_end - seg_start;
                char *root = (char *)malloc(seg_len + 1);
                if (!root) {
                    return 0;
                }
                memcpy(root, raw + seg_start, seg_len);
                root[seg_len] = '\0';
                char *root_abs = path_abs(root);
                free(root);
                if (!root_abs) {
                    return 0;
                }
                int ok = path_is_within_root(root_abs, abs_path);
                free(root_abs);
                if (ok) {
                    return 1;
                }
            }
            start = i + 1;
        }
    }
    return 0;
}

static int path_is_within_extra_roots(const char *abs_path) {
    if (path_is_within_env_root("CHENG_GUI_ROOT", abs_path)) {
        return 1;
    }
    if (path_is_within_env_root("CHENG_IDE_ROOT", abs_path)) {
        return 1;
    }
    if (path_is_within_pkg_roots(abs_path)) {
        return 1;
    }
    return 0;
}

static char *path_abs_checked(const char *root, const char *path) {
    char *abs = path_abs(path);
    if (!abs) {
        return NULL;
    }
    if (root && root[0] != '\0') {
        char *root_abs = path_abs(root);
        if (!root_abs) {
            free(abs);
            return NULL;
        }
        int ok = path_is_within_root(root_abs, abs);
        free(root_abs);
        if (!ok && !path_is_within_extra_roots(abs)) {
            free(abs);
            return NULL;
        }
    }
    return abs;
}

static char *strip_alias(const char *item) {
    if (!item) {
        return NULL;
    }
    const char *as = strstr(item, " as ");
    if (!as) {
        return strdup(item);
    }
    size_t len = (size_t)(as - item);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, item, len);
    out[len] = '\0';
    return out;
}

static char *parse_manifest_package_id(const char *manifest_path) {
    if (!manifest_path || manifest_path[0] == '\0') {
        return NULL;
    }
    FILE *f = fopen(manifest_path, "rb");
    if (!f) {
        return NULL;
    }
    char line[4096];
    while (fgets(line, (int)sizeof(line), f)) {
        const char *s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
            s++;
        }
        const char *key = "package_id";
        size_t key_len = strlen(key);
        if (strncmp(s, key, key_len) != 0) {
            continue;
        }
        char next = s[key_len];
        if (!(next == '\0' || next == ' ' || next == '\t' || next == '=' || next == ':')) {
            continue;
        }
        const char *q1 = strchr(s, '"');
        if (!q1) {
            continue;
        }
        const char *q2 = strchr(q1 + 1, '"');
        if (!q2 || q2 <= q1 + 1) {
            continue;
        }
        size_t n = (size_t)(q2 - (q1 + 1));
        char *out = (char *)malloc(n + 1);
        if (!out) {
            fclose(f);
            return NULL;
        }
        memcpy(out, q1 + 1, n);
        out[n] = '\0';
        fclose(f);
        return out;
    }
    fclose(f);
    return NULL;
}

static char *try_pkg_file_dir(const char *pkg_root, const char *dir, const char *rel) {
    if (!pkg_root || pkg_root[0] == '\0' || !rel || rel[0] == '\0') {
        return NULL;
    }
    char *base = NULL;
    if (dir && dir[0] != '\0') {
        base = path_join(pkg_root, dir);
    } else {
        base = strdup(pkg_root);
    }
    char *joined = base ? path_join(base, rel) : NULL;
    free(base);
    if (!joined) {
        return NULL;
    }
    char *full = ensure_cheng_suffix(joined);
    free(joined);
    if (full && file_exists(full)) {
        char *abs = path_abs_checked(pkg_root, full);
        free(full);
        return abs;
    }
    free(full);
    return NULL;
}

static char *resolve_pkg_path_in_root(const char *pkg_root, const char *rel_in_pkg, const char *rel_for_pkg_root) {
    if (!pkg_root || pkg_root[0] == '\0') {
        return NULL;
    }

    if (rel_in_pkg && rel_in_pkg[0] != '\0') {
        char *res = try_pkg_file_dir(pkg_root, "src", rel_in_pkg);
        if (res) {
            return res;
        }
        res = try_pkg_file_dir(pkg_root, "cheng", rel_in_pkg);
        if (res) {
            return res;
        }
        res = try_pkg_file_dir(pkg_root, "", rel_in_pkg);
        if (res) {
            return res;
        }
    }

    if (rel_for_pkg_root && rel_for_pkg_root[0] != '\0') {
        char *res = try_pkg_file_dir(pkg_root, "src", rel_for_pkg_root);
        if (res) {
            return res;
        }
        res = try_pkg_file_dir(pkg_root, "cheng", rel_for_pkg_root);
        if (res) {
            return res;
        }
        res = try_pkg_file_dir(pkg_root, "", rel_for_pkg_root);
        if (res) {
            return res;
        }
    }

    return NULL;
}

static char *resolve_pkg_import_path(const char *import_path) {
    const char *raw = getenv("CHENG_PKG_ROOTS");
    if (!raw || raw[0] == '\0' || !import_path || import_path[0] == '\0') {
        return NULL;
    }

    const char *pkg_prefix = "cheng/";
    if (!has_prefix(import_path, pkg_prefix)) {
        return NULL;
    }
    const char *rel_for_pkg_root = import_path + 6;
    const char *slash = strchr(rel_for_pkg_root, '/');
    if (!slash || slash == rel_for_pkg_root || slash[1] == '\0') {
        return NULL;
    }
    size_t pkg_name_len = (size_t)(slash - rel_for_pkg_root);
    char *pkg_name = (char *)malloc(pkg_name_len + 1);
    if (!pkg_name) {
        return NULL;
    }
    memcpy(pkg_name, rel_for_pkg_root, pkg_name_len);
    pkg_name[pkg_name_len] = '\0';
    if (strcmp(pkg_name, "stdlib") == 0) {
        free(pkg_name);
        return NULL;
    }
    const char *rel_in_pkg = slash + 1;

    const char *expected_prefix = "pkg://cheng/";
    size_t expected_len = strlen(expected_prefix) + pkg_name_len;
    char *expected_pkg_id = (char *)malloc(expected_len + 1);
    if (!expected_pkg_id) {
        free(pkg_name);
        return NULL;
    }
    memcpy(expected_pkg_id, expected_prefix, strlen(expected_prefix));
    memcpy(expected_pkg_id + strlen(expected_prefix), pkg_name, pkg_name_len);
    expected_pkg_id[expected_len] = '\0';

    int use_colon = 1;
    for (const char *p = raw; *p; p++) {
        if (*p == ',' || *p == ';') {
            use_colon = 0;
            break;
        }
    }
    size_t len = strlen(raw);
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || raw[i] == ',' || raw[i] == ';' || (use_colon && raw[i] == ':')) {
            size_t seg_start = start;
            size_t seg_end = i;
            while (seg_start < seg_end && is_trim_char(raw[seg_start])) {
                seg_start++;
            }
            while (seg_end > seg_start && is_trim_char(raw[seg_end - 1])) {
                seg_end--;
            }
            if (seg_end > seg_start) {
                size_t seg_len = seg_end - seg_start;
                char *root = (char *)malloc(seg_len + 1);
                if (!root) {
                    free(pkg_name);
                    free(expected_pkg_id);
                    return NULL;
                }
                memcpy(root, raw + seg_start, seg_len);
                root[seg_len] = '\0';

                char *manifest = path_join(root, "cheng-package.toml");
                if (manifest && file_exists(manifest)) {
                    char *got = parse_manifest_package_id(manifest);
                    free(manifest);
                    if (got && strcmp(got, expected_pkg_id) == 0) {
                        free(got);
                        char *resolved = resolve_pkg_path_in_root(root, rel_in_pkg, rel_for_pkg_root);
                        free(root);
                        if (resolved) {
                            free(pkg_name);
                            free(expected_pkg_id);
                            return resolved;
                        }
                    } else {
                        free(got);
                    }
                } else {
                    free(manifest);

                    size_t pref_len = strlen("cheng-") + pkg_name_len;
                    char *pref_name = (char *)malloc(pref_len + 1);
                    if (pref_name) {
                        memcpy(pref_name, "cheng-", 6);
                        memcpy(pref_name + 6, pkg_name, pkg_name_len);
                        pref_name[pref_len] = '\0';
                        char *pref_root = path_join(root, pref_name);
                        free(pref_name);
                        if (pref_root) {
                            char *pref_manifest = path_join(pref_root, "cheng-package.toml");
                            if (pref_manifest && file_exists(pref_manifest)) {
                                char *got = parse_manifest_package_id(pref_manifest);
                                free(pref_manifest);
                                if (got && strcmp(got, expected_pkg_id) == 0) {
                                    free(got);
                                    char *resolved = resolve_pkg_path_in_root(pref_root, rel_in_pkg, rel_for_pkg_root);
                                    free(pref_root);
                                    free(root);
                                    if (resolved) {
                                        free(pkg_name);
                                        free(expected_pkg_id);
                                        return resolved;
                                    }
                                } else {
                                    free(got);
                                }
                            } else {
                                free(pref_manifest);
                                char *resolved = resolve_pkg_path_in_root(pref_root, rel_in_pkg, rel_for_pkg_root);
                                free(pref_root);
                                if (resolved) {
                                    free(root);
                                    free(pkg_name);
                                    free(expected_pkg_id);
                                    return resolved;
                                }
                            }
                        }
                    }

                    char *plain_root = path_join(root, pkg_name);
                    if (plain_root) {
                        char *plain_manifest = path_join(plain_root, "cheng-package.toml");
                        if (plain_manifest && file_exists(plain_manifest)) {
                            char *got = parse_manifest_package_id(plain_manifest);
                            free(plain_manifest);
                            if (got && strcmp(got, expected_pkg_id) == 0) {
                                free(got);
                                char *resolved = resolve_pkg_path_in_root(plain_root, rel_in_pkg, rel_for_pkg_root);
                                free(plain_root);
                                free(root);
                                if (resolved) {
                                    free(pkg_name);
                                    free(expected_pkg_id);
                                    return resolved;
                                }
                            } else {
                                free(got);
                            }
                        } else {
                            free(plain_manifest);
                            char *resolved = resolve_pkg_path_in_root(plain_root, rel_in_pkg, rel_for_pkg_root);
                            free(plain_root);
                            if (resolved) {
                                free(root);
                                free(pkg_name);
                                free(expected_pkg_id);
                                return resolved;
                            }
                        }
                    }
                }
                free(root);
            }
            start = i + 1;
        }
    }
    free(pkg_name);
    free(expected_pkg_id);
    return NULL;
}

static char *resolve_import_path(const char *from_file, const char *import_path, const char *root_dir) {
    if (!import_path || import_path[0] == '\0') {
        return NULL;
    }
    const char *c_system = getenv("CHENG_C_SYSTEM");
    if (c_system && c_system[0] != '\0' && strcmp(import_path, "system") == 0) {
        if (!(strcmp(c_system, "0") == 0 || strcmp(c_system, "false") == 0 ||
              strcmp(c_system, "no") == 0 || strcmp(c_system, "system") == 0)) {
            import_path = "system_c";
        }
    }
    if (path_is_abs(import_path)) {
        char *full = ensure_cheng_suffix(import_path);
        char *abs = path_abs_checked(root_dir, full);
        free(full);
        return abs;
    }

    if (root_dir && has_prefix(import_path, "std/")) {
        const char *rest = import_path + 4;
        char *base = path_join(root_dir, "src/std");
        char *joined = base ? path_join(base, rest) : NULL;
        free(base);
        if (!joined) {
            return NULL;
        }
        char *full = ensure_cheng_suffix(joined);
        free(joined);
        if (!full) {
            return NULL;
        }
        if (file_exists(full)) {
            char *abs = path_abs_checked(root_dir, full);
            free(full);
            return abs;
        }
        char *abs = path_abs_checked(root_dir, full);
        free(full);
        return abs;
    }
    if (has_prefix(import_path, "gui/")) {
        const char *gui_root = getenv("CHENG_GUI_ROOT");
        if (gui_root && gui_root[0] != '\0') {
            const char *rest = import_path + 4;
            char *joined = path_join(gui_root, rest);
            if (!joined) {
                return NULL;
            }
            char *full = ensure_cheng_suffix(joined);
            free(joined);
            char *abs = path_abs_checked(gui_root, full);
            free(full);
            return abs;
        }
    }
    if (has_prefix(import_path, "ide/gui/")) {
        const char *gui_root = getenv("CHENG_GUI_ROOT");
        if (gui_root && gui_root[0] != '\0') {
            const char *rest = import_path + 8;
            char *joined = path_join(gui_root, rest);
            if (!joined) {
                return NULL;
            }
            char *full = ensure_cheng_suffix(joined);
            free(joined);
            char *abs = path_abs_checked(gui_root, full);
            free(full);
            return abs;
        }
        const char *ide_root = getenv("CHENG_IDE_ROOT");
        if (ide_root && ide_root[0] != '\0') {
            const char *rest = import_path + 4;
            char *joined = path_join(ide_root, rest);
            if (!joined) {
                return NULL;
            }
            char *full = ensure_cheng_suffix(joined);
            free(joined);
            char *abs = path_abs_checked(ide_root, full);
            free(full);
            return abs;
        }
    }
    if (has_prefix(import_path, "ide/")) {
        const char *ide_root = getenv("CHENG_IDE_ROOT");
        if (ide_root && ide_root[0] != '\0') {
            const char *rest = import_path + 4;
            char *joined = path_join(ide_root, rest);
            if (!joined) {
                return NULL;
            }
            char *full = ensure_cheng_suffix(joined);
            free(joined);
            char *abs = path_abs_checked(ide_root, full);
            free(full);
            return abs;
        }
    }
    if (root_dir && has_prefix(import_path, "cheng/")) {
        const char *rest = import_path + 6;
        char *base = path_join(root_dir, "src");
        char *joined = base ? path_join(base, rest) : NULL;
        free(base);
        if (joined) {
            char *full = ensure_cheng_suffix(joined);
            free(joined);
            if (full && file_exists(full)) {
                char *abs = path_abs_checked(root_dir, full);
                free(full);
                return abs;
            }
            free(full);
        }
    }
    if (root_dir && (has_prefix(import_path, "cheng/") || has_prefix(import_path, "ide/"))) {
        char *joined = path_join(root_dir, import_path);
        char *full = ensure_cheng_suffix(joined);
        free(joined);
        if (!full) {
            return NULL;
        }
        if (has_prefix(import_path, "cheng/")) {
            if (file_exists(full)) {
                char *abs = path_abs_checked(root_dir, full);
                free(full);
                return abs;
            }
            char *pkg = resolve_pkg_import_path(import_path);
            if (pkg) {
                free(full);
                return pkg;
            }
        }
        char *abs = path_abs_checked(root_dir, full);
        free(full);
        return abs;
    }

    char *full = ensure_cheng_suffix(import_path);
    if (!full) {
        return NULL;
    }
    char *dir = path_dirname(from_file);
    if (dir && dir[0] != '\0') {
        char *candidate = path_join(dir, full);
        if (candidate && file_exists(candidate)) {
            char *abs = path_abs_checked(root_dir, candidate);
            free(candidate);
            free(dir);
            free(full);
            return abs;
        }
        if (candidate) {
            free(candidate);
        }
    }
    if (root_dir) {
        char *core = path_join(root_dir, "src/core");
        char *candidate = core ? path_join(core, full) : NULL;
        if (candidate && file_exists(candidate)) {
            char *abs = path_abs_checked(root_dir, candidate);
            free(candidate);
            free(core);
            free(dir);
            free(full);
            return abs;
        }
        if (candidate) {
            free(candidate);
        }
        char *system_root = path_join(root_dir, "src/system");
        candidate = system_root ? path_join(system_root, full) : NULL;
        if (candidate && file_exists(candidate)) {
            char *abs = path_abs_checked(root_dir, candidate);
            free(candidate);
            free(system_root);
            free(core);
            free(dir);
            free(full);
            return abs;
        }
        if (candidate) {
            free(candidate);
        }
        char *std_root = path_join(root_dir, "src/std");
        candidate = std_root ? path_join(std_root, full) : NULL;
        if (candidate && file_exists(candidate)) {
            char *abs = path_abs_checked(root_dir, candidate);
            free(candidate);
            free(std_root);
            free(system_root);
            free(core);
            free(dir);
            free(full);
            return abs;
        }
        if (candidate) {
            free(candidate);
        }
        free(std_root);
        free(system_root);
        free(core);
    }
    char *fallback = NULL;
    if (dir && dir[0] != '\0') {
        char *candidate = path_join(dir, full);
        if (root_dir) {
            fallback = path_abs_checked(root_dir, candidate);
        } else {
            fallback = path_abs(candidate);
        }
        free(candidate);
    } else {
        if (root_dir) {
            fallback = path_abs_checked(root_dir, full);
        } else {
            fallback = path_abs(full);
        }
    }
    free(dir);
    free(full);
    return fallback;
}

static int path_is_exec(const char *path) {
    return path != NULL && access(path, X_OK) == 0;
}

static int mkdir_if_missing(const char *path) {
    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

static int run_cmd_args(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        return 127;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 127;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static int core_trace_enabled(void) {
    const char *val = getenv("CHENG_STAGE0C_CORE_TRACE");
    if (val == NULL || val[0] == '\0') {
        return 0;
    }
    if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0 || strcmp(val, "yes") == 0) {
        return 1;
    }
    return 0;
}

static int inc_has_header(const char *dir) {
    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/chengbase.h", dir);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return 0;
    }
    return file_exists(path);
}

static const char *pick_runtime_inc(void) {
    const char *env = getenv("CHENG_RT_INC");
    if (inc_has_header(env)) {
        return env;
    }
    env = getenv("CHENG_RUNTIME_INC");
    if (inc_has_header(env)) {
        return env;
    }
    if (inc_has_header("runtime/include")) {
        return "runtime/include";
    }
    return "runtime/include";
}

static const char *pick_cc(void) {
    const char *cc = getenv("CC");
    if (cc != NULL && cc[0] != '\0') {
        return cc;
    }
    return "cc";
}

static const char *pick_cflags(void) {
    const char *flags = getenv("CHENG_STAGE1_CFLAGS");
    if (flags != NULL && flags[0] != '\0') {
        return flags;
    }
    flags = getenv("CFLAGS");
    if (flags != NULL && flags[0] != '\0') {
        return flags;
    }
    return "-O2";
}

static int ensure_stage1_runner(void) {
    const char *seed_c = "src/stage1/stage1_runner.seed.c";
    const char *cache_c = "chengcache/stage1_runner.c";
    const char *helpers = "src/runtime/native/system_helpers.c";
    int needs_rebuild = 0;
    const char *force = getenv("CHENG_STAGE1_REBUILD");
    if (force != NULL && force[0] == '1') {
        needs_rebuild = 1;
    }
    if (!path_is_exec("./stage1_runner")) {
        needs_rebuild = 1;
    }
    if (!needs_rebuild) {
        time_t bin_ts = 0;
        time_t seed_ts = 0;
        time_t helpers_ts = 0;
        if (file_mtime("./stage1_runner", &bin_ts) != 0) {
            needs_rebuild = 1;
        } else {
            if (file_mtime(seed_c, &seed_ts) == 0 && seed_ts > bin_ts) {
                needs_rebuild = 1;
            }
            if (file_mtime(helpers, &helpers_ts) == 0 && helpers_ts > bin_ts) {
                needs_rebuild = 1;
            }
        }
    }
    if (!needs_rebuild) {
        return CHENG_CORE_OK;
    }
    if (mkdir_if_missing("chengcache") != 0) {
        fprintf(stderr, "[stage0c-core] failed to create chengcache: %s\n", strerror(errno));
        return CHENG_CORE_ERR_IO;
    }
    const char *src_c = NULL;
    if (file_exists(seed_c)) {
        src_c = seed_c;
    } else if (file_exists(cache_c)) {
        src_c = cache_c;
    }
    if (src_c == NULL) {
        fprintf(stderr,
                "[stage0c-core] missing seed C: %s\n"
                "        (expected seed at %s or cached C at %s)\n",
                seed_c,
                seed_c,
                cache_c);
        return CHENG_CORE_ERR_IO;
    }
    const char *cc = pick_cc();
    const char *cflags = pick_cflags();
    const char *inc = pick_runtime_inc();
    {
        ArgList compile_seed;
        args_init(&compile_seed);
        if (args_push(&compile_seed, cc) != 0 ||
            split_args(cflags, &compile_seed) != 0 ||
            args_push(&compile_seed, "-I") != 0 ||
            args_push(&compile_seed, inc) != 0 ||
            args_push(&compile_seed, "-I") != 0 ||
            args_push(&compile_seed, "src/runtime/native") != 0 ||
            args_push(&compile_seed, "-c") != 0 ||
            args_push(&compile_seed, src_c) != 0 ||
            args_push(&compile_seed, "-o") != 0 ||
            args_push(&compile_seed, "chengcache/stage1_runner.o") != 0) {
            args_free(&compile_seed);
            fprintf(stderr, "[stage0c-core] failed: %s (stage1_runner.o)\n", cc);
            return CHENG_CORE_ERR_IO;
        }
        compile_seed.items[compile_seed.len] = NULL;
        if (run_cmd_args(compile_seed.items) != 0) {
            args_free(&compile_seed);
            fprintf(stderr, "[stage0c-core] failed: %s (stage1_runner.o)\n", cc);
            return CHENG_CORE_ERR_IO;
        }
        args_free(&compile_seed);
        ArgList compile_helpers;
        args_init(&compile_helpers);
        if (args_push(&compile_helpers, cc) != 0 ||
            split_args(cflags, &compile_helpers) != 0 ||
            args_push(&compile_helpers, "-I") != 0 ||
            args_push(&compile_helpers, inc) != 0 ||
            args_push(&compile_helpers, "-I") != 0 ||
            args_push(&compile_helpers, "src/runtime/native") != 0 ||
            args_push(&compile_helpers, "-c") != 0 ||
            args_push(&compile_helpers, helpers) != 0 ||
            args_push(&compile_helpers, "-o") != 0 ||
            args_push(&compile_helpers, "chengcache/stage1_runner.system_helpers.o") != 0) {
            args_free(&compile_helpers);
            fprintf(stderr, "[stage0c-core] failed: %s (system_helpers.o)\n", cc);
            return CHENG_CORE_ERR_IO;
        }
        compile_helpers.items[compile_helpers.len] = NULL;
        if (run_cmd_args(compile_helpers.items) != 0) {
            args_free(&compile_helpers);
            fprintf(stderr, "[stage0c-core] failed: %s (system_helpers.o)\n", cc);
            return CHENG_CORE_ERR_IO;
        }
        args_free(&compile_helpers);
        char *const argv_link[] = {
            (char *)cc,
            "chengcache/stage1_runner.o",
            "chengcache/stage1_runner.system_helpers.o",
            "-o",
            "stage1_runner",
            NULL
        };
        if (run_cmd_args(argv_link) != 0) {
            fprintf(stderr, "[stage0c-core] failed: %s (link stage1_runner)\n", cc);
            return CHENG_CORE_ERR_IO;
        }
    }
    if (!path_is_exec("./stage1_runner")) {
        fprintf(stderr, "[stage0c-core] stage1_runner build failed\n");
        return CHENG_CORE_ERR_IO;
    }
    return CHENG_CORE_OK;
}

static int needs_system_dep(const char *in_path) {
    const char *suffix = "system.cheng";
    const char *suffix_c = "system_c.cheng";
    size_t n = strlen(in_path);
    size_t m = strlen(suffix);
    if (n >= m && strcmp(in_path + (n - m), suffix) == 0) {
        return 0;
    }
    m = strlen(suffix_c);
    if (n >= m && strcmp(in_path + (n - m), suffix_c) == 0) {
        return 0;
    }
    return 1;
}

static int write_deps_list(const ChengStrList *imports, const char *out_path) {
    FILE *f = stdout;
    if (out_path != NULL && out_path[0] != '\0') {
        f = fopen(out_path, "w");
        if (!f) {
            fprintf(stderr, "[stage0c-core] open failed: %s\n", strerror(errno));
            return CHENG_CORE_ERR_IO;
        }
    }
    ChengStrList normalized;
    cheng_strlist_init(&normalized);
    for (size_t idx = 0; idx < imports->len; idx++) {
        const char *item = imports->items[idx];
        if (!item || item[0] == '\0') {
            continue;
        }
        const char *emit = item;
        char *abs = NULL;
        if (file_exists(item)) {
            abs = path_abs(item);
            if (abs && abs[0] != '\0') {
                emit = abs;
            }
        }
        cheng_strlist_push_unique(&normalized, emit);
        free(abs);
    }
    for (size_t idx = 0; idx < normalized.len; idx++) {
        const char *item = normalized.items[idx];
        if (item && item[0] != '\0') {
            fputs(item, f);
            fputc('\n', f);
        }
    }
    cheng_strlist_free(&normalized);
    if (f != stdout) {
        fclose(f);
    }
    return CHENG_CORE_OK;
}
static void print_diags(const ChengDiagList *diags) {
    for (size_t i = 0; i < diags->len; i++) {
        const ChengDiagnostic *d = &diags->data[i];
        fprintf(stderr,
                "%s:%d:%d: [%s] %s\n",
                d->filename ? d->filename : "",
                d->line,
                d->col,
                cheng_severity_label(d->severity),
                d->message ? d->message : "");
    }
}

static char *module_name_from_path(const char *path) {
    if (!path) {
        return NULL;
    }
    const char *start = path;
    const char *slash = strrchr(path, '/');
    if (slash) {
        start = slash + 1;
    }
    size_t len = strlen(start);
    if (len >= 6 && strcmp(start + len - 6, ".cheng") == 0) {
        len -= 6;
    }
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *import_alias_name(const ChengNode *node) {
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_IMPORT_AS) {
        if (node->len >= 2 && node->kids[1] && node->kids[1]->ident) {
            return strdup(node->kids[1]->ident);
        }
        if (node->len >= 1) {
            node = node->kids[0];
        }
    }
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_IDENT && node->ident) {
        return module_name_from_path(node->ident);
    }
    if (node->kind == NK_STR_LIT && node->str_val) {
        return module_name_from_path(node->str_val);
    }
    return NULL;
}

static void collect_import_aliases(const ChengNode *node, ChengStrList *aliases) {
    if (!node || !aliases) {
        return;
    }
    if (node->kind == NK_IMPORT_STMT) {
        for (size_t i = 0; i < node->len; i++) {
            char *alias = import_alias_name(node->kids[i]);
            if (alias && alias[0] != '\0') {
                cheng_strlist_push_unique(aliases, alias);
            }
            free(alias);
        }
        return;
    }
    for (size_t i = 0; i < node->len; i++) {
        collect_import_aliases(node->kids[i], aliases);
    }
}

static ChengNode *lower_module_qualifiers_ctx(ChengNode *node, const ChengStrList *aliases, int keep_dot) {
    if (!node) {
        return NULL;
    }
    if (node->kind == NK_DOT_EXPR && node->len >= 2 && node->kids[0] && node->kids[1]) {
        ChengNode *base = node->kids[0];
        if (!keep_dot && base && (base->kind == NK_IDENT || base->kind == NK_SYMBOL) && base->ident &&
            aliases && cheng_strlist_has(aliases, base->ident)) {
            return lower_module_qualifiers_ctx(node->kids[1], aliases, keep_dot);
        }
    }
    for (size_t i = 0; i < node->len; i++) {
        ChengNode *child = node->kids[i];
        int child_keep = 0;
        if (keep_dot) {
            if (node->kind == NK_BRACKET_EXPR) {
                child_keep = (i == 0);
            } else {
                child_keep = 1;
            }
        } else if (node->kind == NK_CALL && i == 0) {
            child_keep = 1;
        }
        ChengNode *lowered = lower_module_qualifiers_ctx(child, aliases, child_keep);
        if (lowered && lowered != child) {
            node->kids[i] = lowered;
        }
    }
    return node;
}

static ChengNode *lower_module_qualifiers(ChengNode *node, const ChengStrList *aliases) {
    return lower_module_qualifiers_ctx(node, aliases, 0);
}

static int append_module_stmts(ChengNode *dst, ChengNode *module) {
    if (!dst || !module) {
        return -1;
    }
    ChengNode *stmts = module;
    if (module->kind == NK_MODULE && module->len > 0) {
        stmts = module->kids[0];
    }
    if (!stmts || stmts->kind != NK_STMT_LIST) {
        return -1;
    }
    for (size_t i = 0; i < stmts->len; i++) {
        ChengNode *stmt = stmts->kids[i];
        if (!stmt) {
            continue;
        }
        if (stmt->kind == NK_IMPORT_STMT || stmt->kind == NK_IMPORT_GROUP || stmt->kind == NK_INCLUDE_STMT ||
            stmt->kind == NK_EMPTY) {
            continue;
        }
        if (stmt->kind == NK_STMT_LIST) {
            for (size_t j = 0; j < stmt->len; j++) {
                ChengNode *child = stmt->kids[j];
                if (!child || child->kind == NK_EMPTY) {
                    continue;
                }
                if (child->kind == NK_IMPORT_STMT || child->kind == NK_IMPORT_GROUP ||
                    child->kind == NK_INCLUDE_STMT) {
                    continue;
                }
                if (cheng_node_add(dst, child) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (cheng_node_add(dst, stmt) != 0) {
            return -1;
        }
    }
    return 0;
}

typedef struct {
    char *path;
    const ChengNode *stmts;
} ChengModuleItem;

typedef struct {
    ChengModuleItem *items;
    size_t len;
    size_t cap;
} ChengModuleList;

static void module_list_init(ChengModuleList *list) {
    if (!list) {
        return;
    }
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static void module_list_free(ChengModuleList *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->len; i++) {
        free(list->items[i].path);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static const ChengNode *module_list_find(const ChengModuleList *list, const char *path) {
    if (!list || !path) {
        return NULL;
    }
    for (size_t i = 0; i < list->len; i++) {
        if (list->items[i].path && strcmp(list->items[i].path, path) == 0) {
            return list->items[i].stmts;
        }
    }
    return NULL;
}

static int module_list_add(ChengModuleList *list, const char *path, const ChengNode *stmts) {
    if (!list || !path) {
        return -1;
    }
    if (module_list_find(list, path)) {
        return 0;
    }
    if (list->len + 1 > list->cap) {
        size_t next = list->cap == 0 ? 8 : list->cap * 2;
        ChengModuleItem *items = (ChengModuleItem *)realloc(list->items, next * sizeof(*items));
        if (!items) {
            return -1;
        }
        list->items = items;
        list->cap = next;
    }
    char *copy = strdup(path);
    if (!copy) {
        return -1;
    }
    list->items[list->len].path = copy;
    list->items[list->len].stmts = stmts;
    list->len++;
    return 0;
}

static int load_module_recursive(const char *path,
                                 const char *root_dir,
                                 ChengStrList *visited,
                                 ChengNode *dst_stmts,
                                 ChengModuleList *modules,
                                 ChengDiagList *diags) {
    if (!path || !dst_stmts || !visited || !diags) {
        return CHENG_CORE_ERR_USAGE;
    }
    char *abs = path_abs(path);
    const char *use_path = abs ? abs : path;
    if (cheng_strlist_has(visited, use_path)) {
        free(abs);
        return CHENG_CORE_OK;
    }
    if (cheng_strlist_push_unique(visited, use_path) != 0) {
        free(abs);
        return CHENG_CORE_ERR_IO;
    }
    if (core_trace_enabled()) {
        fprintf(stderr, "[stage0c-core] load %s\n", use_path);
    }
    if (!file_exists(use_path)) {
        cheng_diag_add(diags, CHENG_SV_ERROR, use_path, 1, 1, "cannot read import module");
        free(abs);
        return CHENG_CORE_ERR_IO;
    }

    ChengLexed lexed;
    if (cheng_lex_file(use_path, &lexed) != 0) {
        print_diags(&lexed.diags);
        cheng_lexed_free(&lexed);
        free(abs);
        return CHENG_CORE_ERR_IO;
    }
    if (cheng_diags_has_error(&lexed.diags)) {
        print_diags(&lexed.diags);
        cheng_lexed_free(&lexed);
        free(abs);
        return CHENG_CORE_ERR_IO;
    }
    ChengParser parser;
    cheng_parser_init(&parser, &lexed.tokens, use_path, &lexed.diags);
    ChengNode *root = cheng_parse_module(&parser);
    if (!root || cheng_diags_has_error(&lexed.diags)) {
        print_diags(&lexed.diags);
        cheng_node_free(root);
        cheng_lexed_free(&lexed);
        free(abs);
        return CHENG_CORE_ERR_IO;
    }

    ChengStrList aliases;
    cheng_strlist_init(&aliases);
    collect_import_aliases(root, &aliases);
    if (aliases.len > 0) {
        lower_module_qualifiers(root, &aliases);
    }

    ChengStrList imports;
    cheng_strlist_init(&imports);
    cheng_collect_imports_from_ast(root, &imports);
    for (size_t i = 0; i < imports.len; i++) {
        char *raw = strip_alias(imports.items[i]);
        if (!raw) {
            cheng_strlist_free(&imports);
            cheng_strlist_free(&aliases);
            cheng_lexed_free(&lexed);
            free(abs);
            return CHENG_CORE_ERR_IO;
        }
        char *resolved = resolve_import_path(use_path, raw, root_dir);
        if (!resolved || !file_exists(resolved)) {
            cheng_diag_add(diags, CHENG_SV_ERROR, use_path, 1, 1, "cannot read import module");
            free(resolved);
            free(raw);
            cheng_strlist_free(&imports);
            cheng_strlist_free(&aliases);
            cheng_lexed_free(&lexed);
            free(abs);
            return CHENG_CORE_ERR_IO;
        }
        int rc = load_module_recursive(resolved, root_dir, visited, dst_stmts, modules, diags);
        free(resolved);
        free(raw);
        if (rc != CHENG_CORE_OK) {
            cheng_strlist_free(&imports);
            cheng_strlist_free(&aliases);
            cheng_lexed_free(&lexed);
            free(abs);
            return rc;
        }
    }

    if (append_module_stmts(dst_stmts, root) != 0) {
        cheng_strlist_free(&imports);
        cheng_strlist_free(&aliases);
        cheng_lexed_free(&lexed);
        free(abs);
        return CHENG_CORE_ERR_IO;
    }
    if (modules) {
        const ChengNode *module_stmts = root;
        if (root->kind == NK_MODULE && root->len > 0) {
            module_stmts = root->kids[0];
        }
        if (module_list_add(modules, use_path, module_stmts) != 0) {
            cheng_strlist_free(&imports);
            cheng_strlist_free(&aliases);
            cheng_lexed_free(&lexed);
            free(abs);
            return CHENG_CORE_ERR_IO;
        }
    }

    cheng_strlist_free(&imports);
    cheng_strlist_free(&aliases);
    cheng_lexed_free(&lexed);
    free(abs);
    return CHENG_CORE_OK;
}

int cheng_core_compile(const ChengCoreOpts *opts) {
    if (opts == NULL) {
        fprintf(stderr, "[stage0c-core] missing options\n");
        return CHENG_CORE_ERR_USAGE;
    }
    const char *mode = opts->mode != NULL ? opts->mode : "";
    if (opts->in_path == NULL || opts->in_path[0] == '\0') {
        fprintf(stderr, "[stage0c-core] missing --file:<input.cheng>\n");
        return CHENG_CORE_ERR_USAGE;
    }
    if (opts->trace) {
        fprintf(stderr, "[stage0c-core] mode=%s file=%s out=%s\n",
                mode,
                opts->in_path != NULL ? opts->in_path : "",
                opts->out_path != NULL ? opts->out_path : "");
    }
    if (strcmp(mode, "deps") == 0) {
        fprintf(stderr, "[stage0c-core] --mode:deps is removed; use --mode:deps-list instead\n");
        return CHENG_CORE_ERR_USAGE;
    }
    if (strcmp(mode, "deps-list") == 0) {
        int resolve = 0;
        const char *resolve_env = getenv("CHENG_DEPS_RESOLVE");
        if (resolve_env && resolve_env[0] != '\0') {
            if (strcmp(resolve_env, "1") == 0 || strcmp(resolve_env, "true") == 0 || strcmp(resolve_env, "yes") == 0) {
                resolve = 1;
            }
        }
        char cwd[PATH_MAX];
        const char *root_dir = getcwd(cwd, sizeof(cwd)) ? cwd : NULL;
        int load_system = needs_system_dep(opts->in_path);
        const char *system_import = "system_c";
        const char *c_system = getenv("CHENG_C_SYSTEM");
        if (c_system && c_system[0] != '\0') {
            if (strcmp(c_system, "0") == 0 || strcmp(c_system, "false") == 0 || strcmp(c_system, "no") == 0) {
                load_system = 0;
            } else if (strcmp(c_system, "system") == 0) {
                system_import = "system";
            } else {
                system_import = "system_c";
            }
        }
        if (resolve) {
            ChengNode *root = cheng_node_new(NK_MODULE, 0, 0);
            ChengNode *stmts = cheng_node_new(NK_STMT_LIST, 0, 0);
            if (!root || !stmts || cheng_node_add(root, stmts) != 0) {
                cheng_node_free(root);
                cheng_node_free(stmts);
                return CHENG_CORE_ERR_IO;
            }
            ChengDiagList diags;
            cheng_diags_init(&diags);
            ChengStrList visited;
            cheng_strlist_init(&visited);
            ChengModuleList modules;
            module_list_init(&modules);
            if (load_system) {
                char *sys_path = resolve_import_path(opts->in_path, system_import, root_dir);
                if (!sys_path || !file_exists(sys_path)) {
                    cheng_diag_add(&diags, CHENG_SV_ERROR, opts->in_path, 1, 1, "cannot read system module");
                    free(sys_path);
                    print_diags(&diags);
                    cheng_diags_free(&diags);
                    cheng_strlist_free(&visited);
                    module_list_free(&modules);
                    cheng_node_free(root);
                    return CHENG_CORE_ERR_IO;
                }
                int rc = load_module_recursive(sys_path, root_dir, &visited, stmts, &modules, &diags);
                free(sys_path);
                if (rc != CHENG_CORE_OK || cheng_diags_has_error(&diags)) {
                    print_diags(&diags);
                    cheng_diags_free(&diags);
                    cheng_strlist_free(&visited);
                    module_list_free(&modules);
                    cheng_node_free(root);
                    return rc;
                }
            }
            int rc = load_module_recursive(opts->in_path, root_dir, &visited, stmts, &modules, &diags);
            if (rc != CHENG_CORE_OK || cheng_diags_has_error(&diags)) {
                print_diags(&diags);
                cheng_diags_free(&diags);
                cheng_strlist_free(&visited);
                module_list_free(&modules);
                cheng_node_free(root);
                return rc;
            }
            rc = write_deps_list(&visited, opts->out_path);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return rc;
        }
        ChengLexed lexed;
        if (cheng_lex_file(opts->in_path, &lexed) != 0) {
            print_diags(&lexed.diags);
            cheng_lexed_free(&lexed);
            return CHENG_CORE_ERR_IO;
        }
        if (cheng_diags_has_error(&lexed.diags)) {
            print_diags(&lexed.diags);
            cheng_lexed_free(&lexed);
            return CHENG_CORE_ERR_IO;
        }
        ChengStrList imports;
        cheng_strlist_init(&imports);
        ChengParser parser;
        cheng_parser_init(&parser, &lexed.tokens, opts->in_path, &lexed.diags);
        ChengNode *root = cheng_parse_module(&parser);
        if (!root || cheng_diags_has_error(&lexed.diags)) {
            print_diags(&lexed.diags);
            cheng_node_free(root);
            cheng_lexed_free(&lexed);
            cheng_strlist_free(&imports);
            return CHENG_CORE_ERR_IO;
        }
        cheng_collect_imports_from_ast(root, &imports);
        cheng_node_free(root);
        ChengStrList deps_out;
        cheng_strlist_init(&deps_out);
        for (size_t i = 0; i < imports.len; i++) {
            const char *item = imports.items[i];
            if (!item || item[0] == '\0') {
                continue;
            }
            if (cheng_strlist_push_unique(&deps_out, item) != 0) {
                cheng_lexed_free(&lexed);
                cheng_strlist_free(&imports);
                cheng_strlist_free(&deps_out);
                return CHENG_CORE_ERR_IO;
            }
        }
        if (load_system) {
            if (cheng_strlist_push_unique(&deps_out, system_import) != 0) {
                cheng_lexed_free(&lexed);
                cheng_strlist_free(&imports);
                cheng_strlist_free(&deps_out);
                return CHENG_CORE_ERR_IO;
            }
        }
        int rc = write_deps_list(&deps_out, opts->out_path);
        cheng_lexed_free(&lexed);
        cheng_strlist_free(&imports);
        cheng_strlist_free(&deps_out);
        return rc;
    }
    if (strcmp(mode, "sem") == 0) {
        ChengLexed lexed;
        if (cheng_lex_file(opts->in_path, &lexed) != 0) {
            print_diags(&lexed.diags);
            cheng_lexed_free(&lexed);
            return CHENG_CORE_ERR_IO;
        }
        if (cheng_diags_has_error(&lexed.diags)) {
            print_diags(&lexed.diags);
            cheng_lexed_free(&lexed);
            return CHENG_CORE_ERR_IO;
        }
        ChengParser parser;
        cheng_parser_init(&parser, &lexed.tokens, opts->in_path, &lexed.diags);
        ChengNode *root = cheng_parse_module(&parser);
        if (!root || cheng_diags_has_error(&lexed.diags)) {
            print_diags(&lexed.diags);
            cheng_node_free(root);
            cheng_lexed_free(&lexed);
            return CHENG_CORE_ERR_IO;
        }
        if (cheng_expand_macros(root, opts->in_path, &lexed.diags) != 0 ||
            cheng_diags_has_error(&lexed.diags)) {
            print_diags(&lexed.diags);
            cheng_node_free(root);
            cheng_lexed_free(&lexed);
            return CHENG_CORE_ERR_IO;
        }
        if (cheng_sem_analyze(root, opts->in_path, &lexed.diags) != 0 ||
            cheng_diags_has_error(&lexed.diags)) {
            print_diags(&lexed.diags);
            cheng_node_free(root);
            cheng_lexed_free(&lexed);
            return CHENG_CORE_ERR_IO;
        }
        cheng_node_free(root);
        if (opts->out_path && opts->out_path[0] != '\0') {
            FILE *f = fopen(opts->out_path, "w");
            if (!f) {
                cheng_lexed_free(&lexed);
                return CHENG_CORE_ERR_IO;
            }
            fputs("ok\n", f);
            fclose(f);
        }
        cheng_lexed_free(&lexed);
        return CHENG_CORE_OK;
    }
    if (strcmp(mode, "asm") == 0 || strcmp(mode, "hrt") == 0) {
        if (opts->out_path == NULL || opts->out_path[0] == '\0') {
            fprintf(stderr, "[stage0c-core] missing --out:<output.s>\n");
            return CHENG_CORE_ERR_USAGE;
        }
        if (strcmp(mode, "hrt") == 0) {
            setenv("CHENG_ASM_RT", "hard", 1);
        }
        ChengNode *root = cheng_node_new(NK_MODULE, 0, 0);
        ChengNode *stmts = cheng_node_new(NK_STMT_LIST, 0, 0);
        if (!root || !stmts || cheng_node_add(root, stmts) != 0) {
            cheng_node_free(root);
            cheng_node_free(stmts);
            return CHENG_CORE_ERR_IO;
        }
        ChengDiagList diags;
        cheng_diags_init(&diags);
        ChengStrList visited;
        cheng_strlist_init(&visited);
        ChengModuleList modules;
        module_list_init(&modules);
        char cwd[PATH_MAX];
        const char *root_dir = getcwd(cwd, sizeof(cwd)) ? cwd : NULL;

        if (needs_system_dep(opts->in_path)) {
            char *sys_path = resolve_import_path(opts->in_path, "system", root_dir);
            if (!sys_path || !file_exists(sys_path)) {
                cheng_diag_add(&diags, CHENG_SV_ERROR, opts->in_path, 1, 1, "cannot read system module");
                free(sys_path);
                print_diags(&diags);
                cheng_diags_free(&diags);
                cheng_strlist_free(&visited);
                module_list_free(&modules);
                cheng_node_free(root);
                return CHENG_CORE_ERR_IO;
            }
            int rc = load_module_recursive(sys_path, root_dir, &visited, stmts, &modules, &diags);
            free(sys_path);
            if (rc != CHENG_CORE_OK || cheng_diags_has_error(&diags)) {
                print_diags(&diags);
                cheng_diags_free(&diags);
                cheng_strlist_free(&visited);
                module_list_free(&modules);
                cheng_node_free(root);
                return rc;
            }
        }

        int rc = load_module_recursive(opts->in_path, root_dir, &visited, stmts, &modules, &diags);
        if (rc != CHENG_CORE_OK || cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return rc;
        }
        if (core_trace_enabled()) {
            fprintf(stderr, "[stage0c-core] load complete: %zu modules\n", visited.len);
        }

        const ChengNode *module_stmts = NULL;
        const char *module_env = getenv("CHENG_ASM_MODULE");
        if (module_env && module_env[0] != '\0') {
            char *module_abs = path_abs(module_env);
            const char *module_path = module_abs ? module_abs : module_env;
            module_stmts = module_list_find(&modules, module_path);
            free(module_abs);
            if (!module_stmts) {
                cheng_diag_add(&diags, CHENG_SV_ERROR, opts->in_path, 1, 1,
                               "CHENG_ASM_MODULE not found in dependency graph");
                print_diags(&diags);
                cheng_diags_free(&diags);
                cheng_strlist_free(&visited);
                module_list_free(&modules);
                cheng_node_free(root);
                return CHENG_CORE_ERR_USAGE;
            }
        }

        if (cheng_expand_macros(root, opts->in_path, &diags) != 0 ||
            cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return CHENG_CORE_ERR_IO;
        }

        if (cheng_monomorphize(root, &diags) != 0 || cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return CHENG_CORE_ERR_IO;
        }
        if (core_trace_enabled()) {
            fprintf(stderr, "[stage0c-core] monomorphize ok\n");
        }

        if (cheng_sem_analyze(root, opts->in_path, &diags) != 0 ||
            cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return CHENG_CORE_ERR_IO;
        }
        if (core_trace_enabled()) {
            fprintf(stderr, "[stage0c-core] semantics ok\n");
        }
        if (cheng_emit_asm(root, opts->in_path, opts->out_path, &diags, module_stmts) != 0 ||
            cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return CHENG_CORE_ERR_NYI;
        }
        if (core_trace_enabled()) {
            fprintf(stderr, "[stage0c-core] asm emit ok\n");
        }
        cheng_diags_free(&diags);
        cheng_strlist_free(&visited);
        module_list_free(&modules);
        cheng_node_free(root);
        return CHENG_CORE_OK;
    }
    if (strcmp(mode, "c") == 0) {
        if (opts->out_path == NULL || opts->out_path[0] == '\0') {
            fprintf(stderr, "[stage0c-core] missing --out:<output.c>\n");
            return CHENG_CORE_ERR_USAGE;
        }
        ChengNode *root = cheng_node_new(NK_MODULE, 0, 0);
        ChengNode *stmts = cheng_node_new(NK_STMT_LIST, 0, 0);
        if (!root || !stmts || cheng_node_add(root, stmts) != 0) {
            cheng_node_free(root);
            cheng_node_free(stmts);
            return CHENG_CORE_ERR_IO;
        }
        ChengDiagList diags;
        cheng_diags_init(&diags);
        ChengStrList visited;
        cheng_strlist_init(&visited);
        ChengModuleList modules;
        module_list_init(&modules);
        char cwd[PATH_MAX];
        const char *root_dir = getcwd(cwd, sizeof(cwd)) ? cwd : NULL;

        int load_system = needs_system_dep(opts->in_path);
        const char *system_import = "system_c";
        const char *c_system = getenv("CHENG_C_SYSTEM");
        if (c_system && c_system[0] != '\0') {
            if (strcmp(c_system, "0") == 0 || strcmp(c_system, "false") == 0 || strcmp(c_system, "no") == 0) {
                load_system = 0;
            } else if (strcmp(c_system, "system") == 0) {
                system_import = "system";
            } else {
                system_import = "system_c";
            }
        }
        if (load_system) {
            char *sys_path = resolve_import_path(opts->in_path, system_import, root_dir);
            if (!sys_path || !file_exists(sys_path)) {
                cheng_diag_add(&diags, CHENG_SV_ERROR, opts->in_path, 1, 1, "cannot read system module");
                free(sys_path);
                print_diags(&diags);
                cheng_diags_free(&diags);
                cheng_strlist_free(&visited);
                cheng_node_free(root);
                return CHENG_CORE_ERR_IO;
            }
            int rc = load_module_recursive(sys_path, root_dir, &visited, stmts, &modules, &diags);
            free(sys_path);
            if (rc != CHENG_CORE_OK || cheng_diags_has_error(&diags)) {
                print_diags(&diags);
                cheng_diags_free(&diags);
                cheng_strlist_free(&visited);
                module_list_free(&modules);
                cheng_node_free(root);
                return rc;
            }
        }

        int rc = load_module_recursive(opts->in_path, root_dir, &visited, stmts, &modules, &diags);
        if (rc != CHENG_CORE_OK || cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return rc;
        }
        if (core_trace_enabled()) {
            fprintf(stderr, "[stage0c-core] load complete: %zu modules\n", visited.len);
        }

        const ChengNode *module_stmts = NULL;
        const char *module_env = getenv("CHENG_C_MODULE");
        if (module_env && module_env[0] != '\0') {
            char *module_abs = path_abs(module_env);
            const char *module_path = module_abs ? module_abs : module_env;
            module_stmts = module_list_find(&modules, module_path);
            free(module_abs);
            if (!module_stmts) {
                cheng_diag_add(&diags, CHENG_SV_ERROR, opts->in_path, 1, 1,
                               "CHENG_C_MODULE not found in dependency graph");
                print_diags(&diags);
                cheng_diags_free(&diags);
                cheng_strlist_free(&visited);
                module_list_free(&modules);
                cheng_node_free(root);
                return CHENG_CORE_ERR_USAGE;
            }
        }

        if (!getenv("CHENG_C_DOT_CALL")) {
            setenv("CHENG_C_DOT_CALL", "1", 1);
        }

        if (cheng_expand_macros(root, opts->in_path, &diags) != 0 ||
            cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return CHENG_CORE_ERR_IO;
        }

        if (cheng_monomorphize(root, &diags) != 0 || cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return CHENG_CORE_ERR_IO;
        }
        if (core_trace_enabled()) {
            fprintf(stderr, "[stage0c-core] monomorphize ok\n");
        }

        if (cheng_sem_analyze(root, opts->in_path, &diags) != 0 ||
            cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            return CHENG_CORE_ERR_IO;
        }
        if (core_trace_enabled()) {
            fprintf(stderr, "[stage0c-core] semantics ok\n");
        }
        ChengCModuleInfo *module_infos = NULL;
        size_t module_infos_len = modules.len;
        if (modules.len > 0) {
            module_infos = (ChengCModuleInfo *)calloc(modules.len, sizeof(*module_infos));
            if (!module_infos) {
                cheng_diags_free(&diags);
                cheng_strlist_free(&visited);
                module_list_free(&modules);
                cheng_node_free(root);
                return CHENG_CORE_ERR_IO;
            }
            for (size_t i = 0; i < modules.len; i++) {
                module_infos[i].path = modules.items[i].path;
                module_infos[i].stmts = modules.items[i].stmts;
            }
        }

        if (cheng_emit_c(root,
                         opts->in_path,
                         opts->out_path,
                         &diags,
                         module_stmts,
                         module_infos,
                         module_infos_len) != 0 ||
            cheng_diags_has_error(&diags)) {
            print_diags(&diags);
            cheng_diags_free(&diags);
            cheng_strlist_free(&visited);
            module_list_free(&modules);
            cheng_node_free(root);
            free(module_infos);
            return CHENG_CORE_ERR_NYI;
        }
        if (core_trace_enabled()) {
            fprintf(stderr, "[stage0c-core] c emit ok\n");
        }
        cheng_diags_free(&diags);
        cheng_strlist_free(&visited);
        module_list_free(&modules);
        cheng_node_free(root);
        free(module_infos);
        return CHENG_CORE_OK;
    }
    fprintf(stderr, "[stage0c-core] unsupported mode: %s\n", mode);
    return CHENG_CORE_ERR_USAGE;
}
