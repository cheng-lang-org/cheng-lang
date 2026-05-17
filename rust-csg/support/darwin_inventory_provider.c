#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    const char *data;
    int32_t len;
    int32_t store_id;
    int32_t flags;
    int32_t _pad;
} ChengFullStr;

typedef struct {
    int32_t len;
    int32_t cap;
    ChengFullStr *buffer;
} ChengStrSeq;

typedef struct {
    ChengFullStr *items;
    int32_t len;
    int32_t cap;
} ChengStrVec;

static int32_t csg_meta_len(uint64_t meta) {
    return (int32_t)(meta & 0xffffffffu);
}

static char *csg_copy_bytes(const char *data, int32_t len) {
    if (!data || len <= 0) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    char *out = (char *)malloc((size_t)len + 1);
    if (!out) return NULL;
    memcpy(out, data, (size_t)len);
    out[len] = '\0';
    return out;
}

static char *csg_arg_to_c(const char *data, uint64_t meta) {
    return csg_copy_bytes(data, csg_meta_len(meta));
}

static ChengFullStr csg_str_from_owned(char *data, int32_t len) {
    ChengFullStr out;
    out.data = data ? data : "";
    out.len = data ? len : 0;
    out.store_id = 0;
    out.flags = 0;
    out._pad = 0;
    return out;
}

static void csg_store_str(ChengFullStr *out, char *data, int32_t len) {
    *out = csg_str_from_owned(data, len);
}

static int csg_vec_push(ChengStrVec *vec, ChengFullStr value) {
    if (vec->len >= vec->cap) {
        int32_t next = vec->cap > 0 ? vec->cap * 2 : 64;
        ChengFullStr *items = (ChengFullStr *)realloc(vec->items, (size_t)next * sizeof(ChengFullStr));
        if (!items) return 0;
        vec->items = items;
        vec->cap = next;
    }
    vec->items[vec->len++] = value;
    return 1;
}

static int csg_path_join_raw(const char *a, const char *b, char **out_path, int32_t *out_len) {
    size_t alen = a ? strlen(a) : 0;
    size_t blen = b ? strlen(b) : 0;
    int need_slash = alen > 0 && a[alen - 1] != '/';
    size_t total = alen + (need_slash ? 1u : 0u) + blen;
    char *out = (char *)malloc(total + 1);
    if (!out) return 0;
    size_t pos = 0;
    if (alen > 0) {
        memcpy(out + pos, a, alen);
        pos += alen;
    }
    if (need_slash) out[pos++] = '/';
    if (blen > 0) {
        memcpy(out + pos, b, blen);
        pos += blen;
    }
    out[pos] = '\0';
    *out_path = out;
    *out_len = (int32_t)pos;
    return 1;
}

static int csg_walk_rec(const char *root, ChengStrVec *vec) {
    DIR *dir = opendir(root);
    if (!dir) return 1;
    for (;;) {
        struct dirent *entry = readdir(dir);
        if (!entry) break;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char *child = NULL;
        int32_t child_len = 0;
        if (!csg_path_join_raw(root, entry->d_name, &child, &child_len)) {
            closedir(dir);
            return 0;
        }
        if (!csg_vec_push(vec, csg_str_from_owned(child, child_len))) {
            free(child);
            closedir(dir);
            return 0;
        }
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!csg_walk_rec(child, vec)) {
                closedir(dir);
                return 0;
            }
        }
    }
    closedir(dir);
    return 1;
}

int32_t RustCsgEndsWith(const char *text_data, uint64_t text_meta, const char *suffix_data, uint64_t suffix_meta) {
    int32_t text_len = csg_meta_len(text_meta);
    int32_t suffix_len = csg_meta_len(suffix_meta);
    if (suffix_len < 0 || text_len < suffix_len) return 0;
    if (suffix_len == 0) return 1;
    if (!text_data || !suffix_data) return 0;
    return memcmp(text_data + text_len - suffix_len, suffix_data, (size_t)suffix_len) == 0;
}

int32_t RustCsgContains(const char *text_data, uint64_t text_meta, const char *needle_data, uint64_t needle_meta) {
    int32_t text_len = csg_meta_len(text_meta);
    int32_t needle_len = csg_meta_len(needle_meta);
    if (needle_len < 0 || text_len < needle_len) return 0;
    if (needle_len == 0) return 1;
    if (!text_data || !needle_data) return 0;
    for (int32_t i = 0; i <= text_len - needle_len; i++) {
        if (memcmp(text_data + i, needle_data, (size_t)needle_len) == 0) return 1;
    }
    return 0;
}

int32_t StartsWith(const char *text_data, uint64_t text_meta, const char *prefix_data, uint64_t prefix_meta) {
    int32_t text_len = csg_meta_len(text_meta);
    int32_t prefix_len = csg_meta_len(prefix_meta);
    if (prefix_len < 0 || text_len < prefix_len) return 0;
    if (prefix_len == 0) return 1;
    if (!text_data || !prefix_data) return 0;
    return memcmp(text_data, prefix_data, (size_t)prefix_len) == 0;
}

void JoinPath_impl(ChengFullStr *out, const char *left_data, uint64_t left_meta, const char *right_data, uint64_t right_meta) {
    char *a = csg_arg_to_c(left_data, left_meta);
    char *b = csg_arg_to_c(right_data, right_meta);
    char *joined = NULL;
    int32_t len = 0;
    if (a && b) csg_path_join_raw(a, b, &joined, &len);
    free(a);
    free(b);
    csg_store_str(out, joined, len);
}

void ReadFile_impl(ChengFullStr *out, const char *path_data, uint64_t path_meta) {
    char *raw_path = csg_arg_to_c(path_data, path_meta);
    if (!raw_path) {
        csg_store_str(out, NULL, 0);
        return;
    }
    FILE *file = fopen(raw_path, "rb");
    free(raw_path);
    if (!file) {
        csg_store_str(out, NULL, 0);
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        csg_store_str(out, NULL, 0);
        return;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        csg_store_str(out, NULL, 0);
        return;
    }
    rewind(file);
    char *data = (char *)malloc((size_t)size + 1);
    if (!data) {
        fclose(file);
        csg_store_str(out, NULL, 0);
        return;
    }
    size_t got = fread(data, 1, (size_t)size, file);
    fclose(file);
    data[got] = '\0';
    csg_store_str(out, data, (int32_t)got);
}

void WalkDirRec_impl(ChengStrSeq *out, const char *root_data, uint64_t root_meta) {
    out->len = 0;
    out->cap = 0;
    out->buffer = NULL;
    char *raw_root = csg_arg_to_c(root_data, root_meta);
    if (!raw_root) return;
    ChengStrVec vec;
    vec.items = NULL;
    vec.len = 0;
    vec.cap = 0;
    (void)csg_walk_rec(raw_root, &vec);
    free(raw_root);
    out->len = vec.len;
    out->cap = vec.cap;
    out->buffer = vec.items;
}

__asm__(
    ".globl _JoinPath\n"
    "_JoinPath:\n"
    "    mov x4, x3\n"
    "    mov x3, x2\n"
    "    mov x2, x1\n"
    "    mov x1, x0\n"
    "    mov x0, x8\n"
    "    b _JoinPath_impl\n"
);

__asm__(
    ".globl _ReadFile\n"
    "_ReadFile:\n"
    "    mov x2, x1\n"
    "    mov x1, x0\n"
    "    mov x0, x8\n"
    "    b _ReadFile_impl\n"
);

__asm__(
    ".globl _WalkDirRec\n"
    "_WalkDirRec:\n"
    "    mov x2, x1\n"
    "    mov x1, x0\n"
    "    mov x0, x8\n"
    "    b _WalkDirRec_impl\n"
);
