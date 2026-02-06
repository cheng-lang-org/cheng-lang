#include <string.h>

#include "asm_emit_runtime.h"

static int has_prefix(const char *s, const char *prefix) {
    size_t n = s ? strlen(s) : 0;
    size_t m = prefix ? strlen(prefix) : 0;
    if (n < m) {
        return 0;
    }
    return strncmp(s, prefix, m) == 0;
}

int asm_runtime_lookup_symbol(const char *name, const char **out_symbol) {
    if (!out_symbol) {
        return 0;
    }
    *out_symbol = NULL;
    if (!name || name[0] == '\0') {
        return 0;
    }
    if (strcmp(name, "memRetain") == 0) {
        *out_symbol = "cheng_mem_retain";
        return 1;
    }
    if (strcmp(name, "memRelease") == 0) {
        *out_symbol = "cheng_mem_release";
        return 1;
    }
    if (strcmp(name, "memRetainAtomic") == 0) {
        *out_symbol = "cheng_mem_retain_atomic";
        return 1;
    }
    if (strcmp(name, "memReleaseAtomic") == 0) {
        *out_symbol = "cheng_mem_release_atomic";
        return 1;
    }
    if (strcmp(name, "memRefCount") == 0) {
        *out_symbol = "cheng_mem_refcount";
        return 1;
    }
    if (strcmp(name, "memRefCountAtomic") == 0) {
        *out_symbol = "cheng_mem_refcount_atomic";
        return 1;
    }
    if (strcmp(name, "memRetainCount") == 0) {
        *out_symbol = "cheng_mm_retain_count";
        return 1;
    }
    if (strcmp(name, "memReleaseCount") == 0) {
        *out_symbol = "cheng_mm_release_count";
        return 1;
    }
    if (strcmp(name, "memDiagReset") == 0) {
        *out_symbol = "cheng_mm_diag_reset";
        return 1;
    }
    if (strcmp(name, "concat") == 0 || strcmp(name, "__cheng_concat_str") == 0) {
        *out_symbol = "__cheng_concat_str";
        return 1;
    }
    if (strcmp(name, "c_strlen") == 0) {
        *out_symbol = "cheng_strlen";
        return 1;
    }
    if (strcmp(name, "c_memcpy") == 0) {
        *out_symbol = "cheng_memcpy";
        return 1;
    }
    if (strcmp(name, "c_memset") == 0) {
        *out_symbol = "cheng_memset";
        return 1;
    }
    if (strcmp(name, "echo") == 0) {
        *out_symbol = "echo";
        return 1;
    }
    if (strcmp(name, "panic") == 0) {
        *out_symbol = "panic";
        return 1;
    }
    if (has_prefix(name, "__cheng_vec_") || has_prefix(name, "__cheng_slice_vec")) {
        *out_symbol = name;
        return 1;
    }
    return 0;
}
