#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

/*
 * Compatibility shim:
 * Stage1 frontend may lower cmdline helpers to plain C symbols `paramCount/paramStr`.
 * Provide weak fallbacks so binaries remain runnable even when the std/cmdline
 * Cheng module is not linked in this path.
 */
WEAK int32_t paramCount(void) {
#if defined(__APPLE__)
    int* argc_ptr = _NSGetArgc();
    if (argc_ptr == NULL) {
        return 0;
    }
    int argc = *argc_ptr;
    if (argc <= 0 || argc > 4096) {
        return 0;
    }
    return (int32_t)(argc - 1);
#else
    return 0;
#endif
}

WEAK const char* paramStr(int32_t i) {
#if defined(__APPLE__)
    if (i < 0) {
        return "";
    }
    int* argc_ptr = _NSGetArgc();
    char*** argv_ptr = _NSGetArgv();
    if (argc_ptr == NULL || argv_ptr == NULL || *argv_ptr == NULL) {
        return "";
    }
    int argc = *argc_ptr;
    if (argc <= 0 || argc > 4096 || i >= argc) {
        return "";
    }
    char* s = (*argv_ptr)[i];
    return s != NULL ? s : "";
#else
    (void)i;
    return "";
#endif
}

#if !defined(_WIN32)
int32_t SystemFunction036(void* buf, int32_t len) {
    (void)buf;
    (void)len;
    return 0;
}
#endif

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef struct ChengMemScope ChengMemScope;

typedef struct ChengMemBlock {
    struct ChengMemBlock* prev;
    struct ChengMemBlock* next;
    ChengMemScope* scope;
    size_t size;
    int32_t rc;
} ChengMemBlock;

struct ChengMemScope {
    ChengMemScope* parent;
    ChengMemBlock* head;
};

static ChengMemScope cheng_global_scope = {NULL, NULL};
static ChengMemScope* cheng_scope_current = &cheng_global_scope;
static int64_t cheng_mm_retain_total = 0;
static int64_t cheng_mm_release_total = 0;
static int64_t cheng_mm_alloc_total = 0;
static int64_t cheng_mm_free_total = 0;
static int64_t cheng_mm_live_total = 0;
static int cheng_mm_diag = -1;
static int cheng_mm_ptrmap_enabled = -1;
static int cheng_mm_ptrmap_scan_enabled = -1;
static int cheng_mm_disabled = -1;
static int cheng_mm_atomic = -1;

static int cheng_mm_is_disabled(void) {
    if (cheng_mm_disabled >= 0) {
        return cheng_mm_disabled;
    }
    const char* env = getenv("MM");
    if (env == NULL || env[0] == '\0') {
        cheng_mm_disabled = 0;
        return cheng_mm_disabled;
    }
    if (env[0] == '0') {
        cheng_mm_disabled = 1;
        return cheng_mm_disabled;
    }
    if (strcmp(env, "off") == 0 || strcmp(env, "none") == 0 ||
        strcmp(env, "false") == 0 || strcmp(env, "no") == 0) {
        cheng_mm_disabled = 1;
        return cheng_mm_disabled;
    }
    cheng_mm_disabled = 0;
    return cheng_mm_disabled;
}

static int cheng_mm_atomic_enabled(void) {
    if (cheng_mm_atomic >= 0) {
        return cheng_mm_atomic;
    }
    const char* env = getenv("MM_ATOMIC");
    if (!env || env[0] == '\0') {
        cheng_mm_atomic = 1;
        return cheng_mm_atomic;
    }
    if (strcmp(env, "0") == 0 || strcmp(env, "false") == 0 || strcmp(env, "no") == 0) {
        cheng_mm_atomic = 0;
        return cheng_mm_atomic;
    }
    cheng_mm_atomic = 1;
    return cheng_mm_atomic;
}

typedef struct ChengPtrMap {
    uintptr_t* keys;
    ChengMemBlock** vals;
    size_t cap;
    size_t count;
    size_t tombs;
} ChengPtrMap;

static ChengPtrMap cheng_block_map = {NULL, NULL, 0, 0, 0};
static const uintptr_t cheng_ptrmap_tomb = 1;
static const size_t cheng_ptrmap_init_cap = 65536;

static inline uintptr_t cheng_ptr_hash(uintptr_t v) {
#if UINTPTR_MAX > 0xffffffffu
    // Fast pointer hash: ignore alignment bits, then mix with one multiply.
    // This is cheaper than a full finalizer but distributes sequential allocs well.
    v >>= 3;
    v ^= v >> 33;
    v *= (uintptr_t)0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    return v;
#else
    v >>= 2;
    v ^= v >> 16;
    v *= (uintptr_t)0x7feb352dU;
    v ^= v >> 15;
    return v;
#endif
}

static void cheng_ptrmap_init(size_t cap) {
    size_t n = 1;
    while (n < cap) {
        n <<= 1;
    }
    cheng_block_map.keys = (uintptr_t*)calloc(n, sizeof(uintptr_t));
    cheng_block_map.vals = (ChengMemBlock**)calloc(n, sizeof(ChengMemBlock*));
    if (!cheng_block_map.keys || !cheng_block_map.vals) {
        free(cheng_block_map.keys);
        free(cheng_block_map.vals);
        cheng_block_map.keys = NULL;
        cheng_block_map.vals = NULL;
        cheng_block_map.cap = 0;
        cheng_block_map.count = 0;
        cheng_block_map.tombs = 0;
        return;
    }
    cheng_block_map.cap = n;
    cheng_block_map.count = 0;
    cheng_block_map.tombs = 0;
}

static void cheng_ptrmap_grow(void) {
    size_t oldcap = cheng_block_map.cap;
    uintptr_t* oldkeys = cheng_block_map.keys;
    ChengMemBlock** oldvals = cheng_block_map.vals;
    size_t newcap = oldcap ? oldcap * 2 : cheng_ptrmap_init_cap;
    size_t n = 1;
    while (n < newcap) {
        n <<= 1;
    }
    uintptr_t* newkeys = (uintptr_t*)calloc(n, sizeof(uintptr_t));
    ChengMemBlock** newvals = (ChengMemBlock**)calloc(n, sizeof(ChengMemBlock*));
    if (!newkeys || !newvals) {
        free(newkeys);
        free(newvals);
        return;
    }
    cheng_block_map.keys = newkeys;
    cheng_block_map.vals = newvals;
    cheng_block_map.cap = n;
    cheng_block_map.count = 0;
    cheng_block_map.tombs = 0;
    if (oldkeys != NULL) {
        size_t mask = cheng_block_map.cap - 1;
        for (size_t i = 0; i < oldcap; i++) {
            uintptr_t k = oldkeys[i];
            if (k > cheng_ptrmap_tomb) {
                ChengMemBlock* val = oldvals[i];
                if (val != NULL) {
                    size_t idx = (size_t)(cheng_ptr_hash(k) & mask);
                    for (;;) {
                        uintptr_t cur = cheng_block_map.keys[idx];
                        if (cur == 0) {
                            cheng_block_map.keys[idx] = k;
                            cheng_block_map.vals[idx] = val;
                            cheng_block_map.count++;
                            break;
                        }
                        idx = (idx + 1) & mask;
                    }
                }
            }
        }
        free(oldkeys);
        free(oldvals);
    }
}

static ChengMemBlock* cheng_ptrmap_get(void* key) {
    if (key == NULL || cheng_block_map.cap == 0) {
        return NULL;
    }
    uintptr_t k = (uintptr_t)key;
    if (k == 0 || k == cheng_ptrmap_tomb) {
        return NULL;
    }
    size_t mask = cheng_block_map.cap - 1;
    size_t idx = (size_t)(cheng_ptr_hash(k) & mask);
    for (;;) {
        uintptr_t cur = cheng_block_map.keys[idx];
        if (cur == 0) {
            return NULL;
        }
        if (cur == k) {
            return cheng_block_map.vals[idx];
        }
        idx = (idx + 1) & mask;
    }
}

static void cheng_ptrmap_put(void* key, ChengMemBlock* val) {
    if (key == NULL) {
        return;
    }
    uintptr_t k = (uintptr_t)key;
    if (k == 0 || k == cheng_ptrmap_tomb) {
        return;
    }
    if (cheng_block_map.cap == 0) {
        cheng_ptrmap_init(cheng_ptrmap_init_cap);
        if (cheng_block_map.cap == 0) {
            return;
        }
    }
    if ((cheng_block_map.count + cheng_block_map.tombs + 1) * 10 >= cheng_block_map.cap * 7) {
        cheng_ptrmap_grow();
    }
    size_t mask = cheng_block_map.cap - 1;
    size_t idx = (size_t)(cheng_ptr_hash(k) & mask);
    size_t tomb = (size_t)-1;
    for (;;) {
        uintptr_t cur = cheng_block_map.keys[idx];
        if (cur == 0) {
            if (tomb != (size_t)-1) {
                idx = tomb;
                cheng_block_map.tombs--;
            }
            cheng_block_map.keys[idx] = k;
            cheng_block_map.vals[idx] = val;
            cheng_block_map.count++;
            return;
        }
        if (cur == cheng_ptrmap_tomb) {
            if (tomb == (size_t)-1) {
                tomb = idx;
            }
        } else if (cur == k) {
            cheng_block_map.vals[idx] = val;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

static void cheng_ptrmap_del(void* key) {
    if (key == NULL || cheng_block_map.cap == 0) {
        return;
    }
    uintptr_t k = (uintptr_t)key;
    if (k == 0 || k == cheng_ptrmap_tomb) {
        return;
    }
    size_t mask = cheng_block_map.cap - 1;
    size_t idx = (size_t)(cheng_ptr_hash(k) & mask);
    for (;;) {
        uintptr_t cur = cheng_block_map.keys[idx];
        if (cur == 0) {
            return;
        }
        if (cur == k) {
            cheng_block_map.keys[idx] = cheng_ptrmap_tomb;
            cheng_block_map.vals[idx] = NULL;
            if (cheng_block_map.count > 0) {
                cheng_block_map.count--;
            }
            cheng_block_map.tombs++;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

static int cheng_mm_diag_enabled(void) {
    if (cheng_mm_diag >= 0) {
        return cheng_mm_diag;
    }
    const char* env = getenv("MM_DIAG");
    if (env && env[0] != '\0' && env[0] != '0') {
        cheng_mm_diag = 1;
    } else {
        cheng_mm_diag = 0;
    }
    return cheng_mm_diag;
}

static int cheng_mm_ptrmap_check(void) {
    if (cheng_mm_ptrmap_enabled >= 0) {
        return cheng_mm_ptrmap_enabled;
    }
    const char* env = getenv("MM_PTRMAP");
    if (env && env[0] != '\0') {
        if (env[0] == '0' || env[0] == 'f' || env[0] == 'F' || env[0] == 'n' || env[0] == 'N') {
            cheng_mm_ptrmap_enabled = 0;
        } else {
            cheng_mm_ptrmap_enabled = 1;
        }
    } else {
        cheng_mm_ptrmap_enabled = 1;
    }
    return cheng_mm_ptrmap_enabled;
}

static int cheng_mm_ptrmap_scan_check(void) {
    if (cheng_mm_ptrmap_scan_enabled >= 0) {
        return cheng_mm_ptrmap_scan_enabled;
    }
    const char* env = getenv("MM_PTRMAP_SCAN");
    if (env && env[0] != '\0') {
        if (env[0] == '0' || env[0] == 'f' || env[0] == 'F' || env[0] == 'n' || env[0] == 'N') {
            cheng_mm_ptrmap_scan_enabled = 0;
        } else {
            cheng_mm_ptrmap_scan_enabled = 1;
        }
    } else {
        cheng_mm_ptrmap_scan_enabled = 0;
    }
    return cheng_mm_ptrmap_scan_enabled;
}

static ChengMemScope* cheng_mem_current(void) {
    return cheng_scope_current;
}

static ChengMemBlock* cheng_mem_find_block(ChengMemScope* scope, void* p) {
    if (scope == NULL || p == NULL) {
        return NULL;
    }
    ChengMemBlock* cur = scope->head;
    while (cur) {
        if ((void*)(cur + 1) == p) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static void cheng_mem_link(ChengMemScope* scope, ChengMemBlock* block) {
    block->scope = scope;
    block->prev = NULL;
    block->next = scope->head;
    if (scope->head) {
        scope->head->prev = block;
    }
    scope->head = block;
}

static void cheng_mem_unlink(ChengMemBlock* block) {
    if (block == NULL || block->scope == NULL) {
        return;
    }
    ChengMemScope* scope = block->scope;
    if (block->prev) {
        block->prev->next = block->next;
    } else if (scope->head == block) {
        scope->head = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    block->prev = NULL;
    block->next = NULL;
    block->scope = NULL;
}

static ChengMemBlock* cheng_mem_find_block_any(void* p) {
    if (p == NULL) {
        return NULL;
    }
    if (cheng_mm_ptrmap_check()) {
        ChengMemBlock* block = cheng_ptrmap_get(p);
        if (block != NULL || !cheng_mm_ptrmap_scan_check()) {
            return block;
        }
    }
    ChengMemScope* scope = cheng_mem_current();
    while (scope) {
        ChengMemBlock* block = cheng_mem_find_block(scope, p);
        if (block != NULL) {
            return block;
        }
        scope = scope->parent;
    }
    return NULL;
}

static ChengMemBlock* cheng_mem_find_block_any_scan(void* p) {
    if (p == NULL) {
        return NULL;
    }
    if (cheng_mm_ptrmap_check()) {
        ChengMemBlock* block = cheng_ptrmap_get(p);
        if (block != NULL) {
            return block;
        }
    }
    ChengMemScope* scope = cheng_mem_current();
    while (scope) {
        ChengMemBlock* block = cheng_mem_find_block(scope, p);
        if (block != NULL) {
            return block;
        }
        scope = scope->parent;
    }
    return NULL;
}

void* cheng_mem_scope_push(void) {
    ChengMemScope* scope = (ChengMemScope*)malloc(sizeof(ChengMemScope));
    if (!scope) {
        return NULL;
    }
    scope->parent = cheng_scope_current;
    scope->head = NULL;
    cheng_scope_current = scope;
    return scope;
}

void cheng_mem_scope_pop(void) {
    ChengMemScope* scope = cheng_scope_current;
    if (scope == NULL || scope == &cheng_global_scope) {
        return;
    }
    ChengMemBlock* cur = scope->head;
    while (cur) {
        ChengMemBlock* next = cur->next;
        void* payload = (void*)(cur + 1);
        cheng_ptrmap_del(payload);
        free(cur);
        cheng_mm_free_total += 1;
        if (cheng_mm_live_total > 0) {
            cheng_mm_live_total -= 1;
        }
        cur = next;
    }
    cheng_scope_current = scope->parent ? scope->parent : &cheng_global_scope;
    free(scope);
}

void cheng_mem_scope_escape(void* p) {
    if (p == NULL) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    ChengMemScope* scope = block->scope;
    if (scope == NULL || scope == &cheng_global_scope) {
        return;
    }
    ChengMemScope* target = scope->parent ? scope->parent : &cheng_global_scope;
    if (target == scope) {
        return;
    }
    cheng_mem_unlink(block);
    cheng_mem_link(target, block);
}

void cheng_mem_scope_escape_global(void* p) {
    if (p == NULL) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    ChengMemScope* scope = block->scope;
    if (scope == NULL || scope == &cheng_global_scope) {
        return;
    }
    cheng_mem_unlink(block);
    cheng_mem_link(&cheng_global_scope, block);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void* ptr_add(void* p, int32_t offset) {
    return (void*)((uint8_t*)p + offset);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
uint64_t cheng_ptr_to_u64(void* p) {
    return (uint64_t)(uintptr_t)p;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int32_t cheng_ptr_size(void) {
    return (int32_t)sizeof(void*);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void store_int32(void* p, int32_t val) {
    *(int32_t*)p = val;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int32_t load_int32(void* p) {
    return *(int32_t*)p;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void store_bool(void* p, int8_t val) {
    *(int8_t*)p = val;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int8_t load_bool(void* p) {
    return *(int8_t*)p;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void store_ptr(void* p, void* val) {
    *(void**)p = val;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void* load_ptr(void* p) {
    return *(void**)p;
}

int32_t xor_0(int32_t a, int32_t b) {
    return a ^ b;
}

int32_t shl_0(int32_t a, int32_t b) {
    return a << b;
}

int32_t shr_0(int32_t a, int32_t b) {
    return a >> b;
}

int32_t mod_0(int32_t a, int32_t b) {
    return a % b;
}

int32_t bitand_0(int32_t a, int32_t b) {
    return a & b;
}

int32_t bitor_0(int32_t a, int32_t b) { return a | b; }
int32_t bitnot_0(int32_t a) { return ~a; }
int32_t mul_0(int32_t a, int32_t b) { return a * b; }
int32_t div_0(int32_t a, int32_t b) { return a / b; }
bool not_0(bool a) { return !a; }
int32_t cheng_puts(const char* text) { return puts(text ? text : ""); }
void cheng_exit(int32_t code) { exit(code); }
void cheng_bounds_check(int32_t len, int32_t idx) {
    if (idx < 0 || idx >= len) {
        fprintf(stderr, "[cheng] bounds check failed: idx=%d len=%d\n", idx, len);
#if defined(__ANDROID__)
        __android_log_print(
            ANDROID_LOG_ERROR,
            "ChengRuntime",
            "bounds check failed: idx=%d len=%d",
            idx,
            len
        );
#endif
        cheng_exit(1);
    }
}

static void cheng_log_bounds_site(int32_t len, int32_t idx, int32_t elem_size, void* caller) {
#if defined(__ANDROID__)
    Dl_info info;
    if (dladdr(caller, &info) != 0) {
        uintptr_t pc = (uintptr_t)caller;
        uintptr_t base = (uintptr_t)info.dli_fbase;
        uintptr_t offset = pc >= base ? (pc - base) : 0;
        __android_log_print(
            ANDROID_LOG_ERROR,
            "ChengRuntime",
            "bounds fail idx=%d len=%d elem=%d caller=%p module=%s symbol=%s offset=0x%zx",
            idx,
            len,
            elem_size,
            caller,
            info.dli_fname ? info.dli_fname : "?",
            info.dli_sname ? info.dli_sname : "?",
            (size_t)offset
        );
        return;
    }
    __android_log_print(
        ANDROID_LOG_ERROR,
        "ChengRuntime",
        "bounds fail idx=%d len=%d elem=%d caller=%p dladdr=none",
        idx,
        len,
        elem_size,
        caller
    );
#else
    (void)len;
    (void)idx;
    (void)elem_size;
    (void)caller;
#endif
}

static void* cheng_index_ptr(void* base, int32_t len, int32_t idx, int32_t elem_size, void* caller) {
    if (idx < 0 || idx >= len) {
        cheng_log_bounds_site(len, idx, elem_size, caller);
    }
    cheng_bounds_check(len, idx);
    if (!base || elem_size <= 0) {
        return base;
    }
    size_t offset = (size_t)((int64_t)idx * (int64_t)elem_size);
    return (void*)((uint8_t*)base + offset);
}

void* cheng_seq_get(void* buffer, int32_t len, int32_t idx, int32_t elem_size) {
    return cheng_index_ptr(buffer, len, idx, elem_size, __builtin_return_address(0));
}

void* cheng_seq_set(void* buffer, int32_t len, int32_t idx, int32_t elem_size) {
    return cheng_index_ptr(buffer, len, idx, elem_size, __builtin_return_address(0));
}

typedef struct ChengSeqHeader {
    int32_t len;
    int32_t cap;
    void* buffer;
} ChengSeqHeader;

void* cheng_seq_set_grow(void* seq_ptr, int32_t idx, int32_t elem_size) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (!seq || elem_size <= 0) {
        return cheng_index_ptr(NULL, 0, idx, elem_size, __builtin_return_address(0));
    }
    if (idx < 0) {
        return cheng_index_ptr(seq->buffer, seq->len, idx, elem_size, __builtin_return_address(0));
    }

    int32_t need = idx + 1;
    if (need > seq->cap || seq->buffer == NULL) {
        int32_t new_cap = seq->cap;
        if (new_cap < 4) {
            new_cap = 4;
        }
        while (new_cap < need) {
            int32_t doubled = new_cap * 2;
            if (doubled <= 0) {
                new_cap = need;
                break;
            }
            new_cap = doubled;
        }

        int32_t old_cap = seq->cap;
        size_t bytes = (size_t)new_cap * (size_t)elem_size;
        void* new_buf = realloc(seq->buffer, bytes);
        if (!new_buf) {
            return cheng_index_ptr(seq->buffer, seq->len, idx, elem_size, __builtin_return_address(0));
        }

        if (new_cap > old_cap) {
            size_t old_bytes = (size_t)old_cap * (size_t)elem_size;
            size_t grow_bytes = (size_t)(new_cap - old_cap) * (size_t)elem_size;
            memset((uint8_t*)new_buf + old_bytes, 0, grow_bytes);
        }
        seq->buffer = new_buf;
        seq->cap = new_cap;
    }

    if (need > seq->len) {
        seq->len = need;
    }
    return cheng_index_ptr(seq->buffer, seq->len, idx, elem_size, __builtin_return_address(0));
}

static int32_t cheng_seq_next_cap(int32_t cur_cap, int32_t need) {
    if (need <= 0) {
        return need;
    }
    int32_t cap = cur_cap;
    if (cap < 4) {
        cap = 4;
    }
    while (cap < need) {
        int32_t doubled = cap * 2;
        if (doubled <= 0) {
            return need;
        }
        cap = doubled;
    }
    return cap;
}

WEAK void reserve(void* seq_ptr, int32_t new_cap) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL || new_cap < 0) {
        return;
    }
    if (new_cap == 0) {
        return;
    }
    if (seq->buffer != NULL && new_cap <= seq->cap) {
        return;
    }
    int32_t target = cheng_seq_next_cap(seq->cap, new_cap);
    if (target <= 0) {
        return;
    }
    void* new_buf = realloc(seq->buffer, (size_t)target);
    if (new_buf == NULL) {
        return;
    }
    seq->buffer = new_buf;
    seq->cap = target;
}

WEAK void reserve_ptr_void(void* seq_ptr, int32_t new_cap) {
    reserve(seq_ptr, new_cap);
}

WEAK void setLen(void* seq_ptr, int32_t new_len) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL) {
        return;
    }
    int32_t target = new_len;
    if (target < 0) {
        target = 0;
    }
    if (target > seq->cap) {
        reserve(seq_ptr, target);
    }
    seq->len = target;
}

void* cheng_slice_get(void* ptr, int32_t len, int32_t idx, int32_t elem_size) {
    return cheng_index_ptr(ptr, len, idx, elem_size, __builtin_return_address(0));
}

void* cheng_slice_set(void* ptr, int32_t len, int32_t idx, int32_t elem_size) {
    return cheng_index_ptr(ptr, len, idx, elem_size, __builtin_return_address(0));
}

typedef void (*ChengTaskFn)(void*);

typedef struct ChengTask {
    ChengTaskFn fn;
    void* ctx;
    struct ChengTask* next;
} ChengTask;

static ChengTask* cheng_sched_head = NULL;
static ChengTask* cheng_sched_tail = NULL;
static int32_t cheng_sched_count = 0;

void cheng_spawn(void* fn_ptr, void* ctx) {
    if (!fn_ptr) {
        return;
    }
    ChengTaskFn fn = (ChengTaskFn)fn_ptr;
    ChengTask* task = (ChengTask*)malloc(sizeof(ChengTask));
    if (!task) {
        fn(ctx);
        return;
    }
    task->fn = fn;
    task->ctx = ctx;
    task->next = NULL;
    if (cheng_sched_tail) {
        cheng_sched_tail->next = task;
    } else {
        cheng_sched_head = task;
    }
    cheng_sched_tail = task;
    cheng_sched_count += 1;
}

int32_t cheng_sched_pending(void) {
    return cheng_sched_count;
}

int32_t cheng_sched_run_once(void) {
    ChengTask* task = cheng_sched_head;
    if (!task) {
        return 0;
    }
    cheng_sched_head = task->next;
    if (!cheng_sched_head) {
        cheng_sched_tail = NULL;
    }
    if (cheng_sched_count > 0) {
        cheng_sched_count -= 1;
    }
    ChengTaskFn fn = task->fn;
    void* ctx = task->ctx;
    free(task);
    if (fn) {
        fn(ctx);
    }
    return 1;
}

void cheng_sched_run(void) {
    while (cheng_sched_run_once()) {
        ;
    }
}

typedef struct ChengAwaitI32 {
    int32_t status;
    int32_t value;
} ChengAwaitI32;

typedef struct ChengAwaitVoid {
    int32_t status;
} ChengAwaitVoid;

static ChengAwaitI32* cheng_async_make_i32(int32_t ready, int32_t value) {
    ChengAwaitI32* st = (ChengAwaitI32*)malloc(sizeof(ChengAwaitI32));
    if (!st) {
        return NULL;
    }
    st->status = ready;
    st->value = value;
    return st;
}

static ChengAwaitVoid* cheng_async_make_void(int32_t ready) {
    ChengAwaitVoid* st = (ChengAwaitVoid*)malloc(sizeof(ChengAwaitVoid));
    if (!st) {
        return NULL;
    }
    st->status = ready;
    return st;
}

ChengAwaitI32* cheng_async_pending_i32(void) {
    return cheng_async_make_i32(0, 0);
}

ChengAwaitI32* cheng_async_ready_i32(int32_t value) {
    return cheng_async_make_i32(1, value);
}

void cheng_async_set_i32(ChengAwaitI32* st, int32_t value) {
    if (!st) {
        return;
    }
    st->value = value;
    st->status = 1;
}

int32_t cheng_await_i32(ChengAwaitI32* st) {
    if (!st) {
        return 0;
    }
    while (st->status == 0) {
        if (!cheng_sched_run_once()) {
            continue;
        }
    }
    return st->value;
}

ChengAwaitVoid* cheng_async_pending_void(void) {
    return cheng_async_make_void(0);
}

ChengAwaitVoid* cheng_async_ready_void(void) {
    return cheng_async_make_void(1);
}

void cheng_async_set_void(ChengAwaitVoid* st) {
    if (!st) {
        return;
    }
    st->status = 1;
}

void cheng_await_void(ChengAwaitVoid* st) {
    if (!st) {
        return;
    }
    while (st->status == 0) {
        if (!cheng_sched_run_once()) {
            continue;
        }
    }
}

typedef struct ChengChanI32 {
    int32_t cap;
    int32_t count;
    int32_t head;
    int32_t tail;
    int32_t* buffer;
} ChengChanI32;

ChengChanI32* cheng_chan_i32_new(int32_t cap) {
    if (cap <= 0) {
        cap = 1;
    }
    ChengChanI32* ch = (ChengChanI32*)malloc(sizeof(ChengChanI32));
    if (!ch) {
        return NULL;
    }
    ch->cap = cap;
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->buffer = (int32_t*)malloc(sizeof(int32_t) * (size_t)cap);
    if (!ch->buffer) {
        free(ch);
        return NULL;
    }
    return ch;
}

int32_t cheng_chan_i32_send(ChengChanI32* ch, int32_t value) {
    if (!ch) {
        return 0;
    }
    while (ch->count >= ch->cap) {
        if (!cheng_sched_run_once()) {
            return 0;
        }
    }
    ch->buffer[ch->tail] = value;
    ch->tail = (ch->tail + 1) % ch->cap;
    ch->count += 1;
    return 1;
}

int32_t cheng_chan_i32_recv(ChengChanI32* ch, int32_t* out) {
    if (!ch || !out) {
        return 0;
    }
    while (ch->count == 0) {
        if (!cheng_sched_run_once()) {
            return 0;
        }
    }
    *out = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->cap;
    ch->count -= 1;
    return 1;
}

void spawn(void* fn_ptr, void* ctx) {
    cheng_spawn(fn_ptr, ctx);
}

int32_t schedPending(void) {
    return cheng_sched_pending();
}

int32_t schedRunOnce(void) {
    return cheng_sched_run_once();
}

void schedRun(void) {
    cheng_sched_run();
}

void* asyncPendingI32(void) {
    return cheng_async_pending_i32();
}

void* asyncReadyI32(int32_t value) {
    return cheng_async_ready_i32(value);
}

void asyncSetI32(void* state, int32_t value) {
    cheng_async_set_i32((ChengAwaitI32*)state, value);
}

int32_t awaitI32(void* state) {
    return cheng_await_i32((ChengAwaitI32*)state);
}

void* asyncPendingVoid(void) {
    return cheng_async_pending_void();
}

void* asyncReadyVoid(void) {
    return cheng_async_ready_void();
}

void asyncSetVoid(void* state) {
    cheng_async_set_void((ChengAwaitVoid*)state);
}

void awaitVoid(void* state) {
    cheng_await_void((ChengAwaitVoid*)state);
}

void* chanI32New(int32_t cap) {
    return cheng_chan_i32_new(cap);
}

int32_t chanI32Send(void* ch, int32_t value) {
    return cheng_chan_i32_send((ChengChanI32*)ch, value);
}

int32_t chanI32Recv(void* ch, int32_t* out) {
    return cheng_chan_i32_recv((ChengChanI32*)ch, out);
}

#include <stdio.h>
#include <string.h>

void* cheng_malloc(int32_t size) {
    if (size <= 0) {
        size = 1;
    }
    ChengMemScope* scope = cheng_mem_current();
    size_t total = sizeof(ChengMemBlock) + (size_t)size;
    ChengMemBlock* block = (ChengMemBlock*)malloc(total);
    if (!block) {
        return NULL;
    }
    block->size = (size_t)size;
    block->rc = 1;
    cheng_mem_link(scope, block);
    void* out = (void*)(block + 1);
    cheng_ptrmap_put(out, block);
    cheng_mm_alloc_total += 1;
    cheng_mm_live_total += 1;
    return out;
}

void cheng_free(void* p) {
    if (!p) {
        return;
    }
    ChengMemBlock* block = ((ChengMemBlock*)p) - 1;
    cheng_mem_unlink(block);
    cheng_ptrmap_del(p);
    free(block);
    cheng_mm_free_total += 1;
    if (cheng_mm_live_total > 0) {
        cheng_mm_live_total -= 1;
    }
}

void cheng_mem_retain(void* p) {
    if (cheng_mm_is_disabled()) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    if (cheng_mm_atomic_enabled()) {
        (void)__atomic_fetch_add(&block->rc, 1, __ATOMIC_RELAXED);
    } else {
        if (block->rc < INT32_MAX) {
            block->rc += 1;
        }
    }
    cheng_mm_retain_total += 1;
    if (cheng_mm_diag_enabled()) {
        int32_t rc = cheng_mm_atomic_enabled()
                         ? __atomic_load_n(&block->rc, __ATOMIC_RELAXED)
                         : block->rc;
        fprintf(stderr, "[mm] retain p=%p rc=%d\n", p, rc);
    }
}

void cheng_mem_release(void* p) {
    if (cheng_mm_is_disabled()) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    int32_t next_rc = 0;
    if (cheng_mm_atomic_enabled()) {
        int32_t prev = __atomic_fetch_sub(&block->rc, 1, __ATOMIC_RELEASE);
        next_rc = prev - 1;
    } else {
        if (block->rc > 0) {
            block->rc -= 1;
        }
        next_rc = block->rc;
    }
    cheng_mm_release_total += 1;
    if (cheng_mm_diag_enabled()) {
        int32_t rc = cheng_mm_atomic_enabled()
                         ? __atomic_load_n(&block->rc, __ATOMIC_RELAXED)
                         : block->rc;
        fprintf(stderr, "[mm] release p=%p rc=%d\n", p, rc);
    }
    if (cheng_mm_atomic_enabled()) {
        if (next_rc <= 0) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            cheng_mem_unlink(block);
            block->rc = 0;
            cheng_ptrmap_del(p);
            free(block);
            cheng_mm_free_total += 1;
            if (cheng_mm_live_total > 0) {
                cheng_mm_live_total -= 1;
            }
        }
    } else if (block->rc <= 0) {
        cheng_mem_unlink(block);
        block->rc = 0;
        cheng_ptrmap_del(p);
        free(block);
        cheng_mm_free_total += 1;
        if (cheng_mm_live_total > 0) {
            cheng_mm_live_total -= 1;
        }
    }
}

int32_t cheng_mem_refcount(void* p) {
    if (cheng_mm_is_disabled()) {
        return 0;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return 0;
    }
    if (cheng_mm_atomic_enabled()) {
        return __atomic_load_n(&block->rc, __ATOMIC_RELAXED);
    }
    return block->rc;
}

void cheng_mem_retain_atomic(void* p) {
    if (cheng_mm_is_disabled()) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    (void)__atomic_fetch_add(&block->rc, 1, __ATOMIC_RELAXED);
    cheng_mm_retain_total += 1;
    if (cheng_mm_diag_enabled()) {
        int32_t rc = __atomic_load_n(&block->rc, __ATOMIC_RELAXED);
        fprintf(stderr, "[mm] retain_atomic p=%p rc=%d\n", p, rc);
    }
}

void cheng_mem_release_atomic(void* p) {
    if (cheng_mm_is_disabled()) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    int32_t prev = __atomic_fetch_sub(&block->rc, 1, __ATOMIC_RELEASE);
    cheng_mm_release_total += 1;
    if (cheng_mm_diag_enabled()) {
        int32_t rc = prev - 1;
        fprintf(stderr, "[mm] release_atomic p=%p rc=%d\n", p, rc);
    }
    if (prev <= 1) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        cheng_mem_unlink(block);
        block->rc = 0;
        cheng_ptrmap_del(p);
        free(block);
        cheng_mm_free_total += 1;
        if (cheng_mm_live_total > 0) {
            cheng_mm_live_total -= 1;
        }
    }
}

int32_t cheng_mem_refcount_atomic(void* p) {
    if (cheng_mm_is_disabled()) {
        return 0;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return 0;
    }
    return __atomic_load_n(&block->rc, __ATOMIC_RELAXED);
}

WEAK void memRetain(void* p) { cheng_mem_retain(p); }
WEAK void memRelease(void* p) { cheng_mem_release(p); }
WEAK int32_t memRefcount(void* p) { return cheng_mem_refcount(p); }
WEAK void memScopeEscape(void* p) { cheng_mem_scope_escape(p); }
WEAK void memScopeEscapeGlobal(void* p) { cheng_mem_scope_escape_global(p); }
WEAK void memRetainAtomic(void* p) { cheng_mem_retain_atomic(p); }
WEAK void memReleaseAtomic(void* p) { cheng_mem_release_atomic(p); }
WEAK int32_t memRefcountAtomic(void* p) { return cheng_mem_refcount_atomic(p); }

int32_t cheng_atomic_cas_i32(int32_t* p, int32_t expect, int32_t desired) {
    if (!p) {
        return 0;
    }
    return __atomic_compare_exchange_n(p, &expect, desired, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
}

void cheng_atomic_store_i32(int32_t* p, int32_t val) {
    if (!p) {
        return;
    }
    __atomic_store_n(p, val, __ATOMIC_RELEASE);
}

int32_t cheng_atomic_load_i32(int32_t* p) {
    if (!p) {
        return 0;
    }
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

int64_t cheng_mm_retain_count(void) { return cheng_mm_retain_total; }
int64_t cheng_mm_release_count(void) { return cheng_mm_release_total; }
int64_t cheng_mm_alloc_count(void) { return cheng_mm_alloc_total; }
int64_t cheng_mm_free_count(void) { return cheng_mm_free_total; }
int64_t cheng_mm_live_count(void) { return cheng_mm_live_total; }
void cheng_mm_diag_reset(void) {
    cheng_mm_retain_total = 0;
    cheng_mm_release_total = 0;
}

WEAK int64_t memRetainCount(void) { return cheng_mm_retain_count(); }
WEAK int64_t memReleaseCount(void) { return cheng_mm_release_count(); }
WEAK int64_t mmAllocCount(void) { return cheng_mm_alloc_count(); }
WEAK int64_t mmFreeCount(void) { return cheng_mm_free_count(); }
WEAK int64_t mmLiveCount(void) { return cheng_mm_live_count(); }
WEAK void mmDiagReset(void) { cheng_mm_diag_reset(); }

void* cheng_realloc(void* p, int32_t size) {
    if (!p) {
        return cheng_malloc(size);
    }
    if (size <= 0) {
        size = 1;
    }
    ChengMemBlock* block = ((ChengMemBlock*)p) - 1;
    // Copy-on-write for shared blocks (rc>1). In-place `realloc` would invalidate
    // other aliases and can lead to heap corruption when sequences are copied
    // and later grown.
    if (!cheng_mm_is_disabled()) {
        int32_t rc = cheng_mm_atomic_enabled() ? __atomic_load_n(&block->rc, __ATOMIC_RELAXED) : block->rc;
        if (rc > 1) {
            size_t total_new = sizeof(ChengMemBlock) + (size_t)size;
            ChengMemBlock* fresh = (ChengMemBlock*)malloc(total_new);
            if (!fresh) {
                return NULL;
            }
            fresh->size = (size_t)size;
            fresh->rc = 1;
            ChengMemScope* scope = block->scope ? block->scope : cheng_mem_current();
            cheng_mem_link(scope, fresh);
            void* out = (void*)(fresh + 1);
            size_t copy = block->size;
            if (copy > (size_t)size) {
                copy = (size_t)size;
            }
            if (copy > 0) {
                memcpy(out, p, copy);
            }
            cheng_ptrmap_put(out, fresh);
            if (cheng_mm_atomic_enabled()) {
                (void)__atomic_fetch_sub(&block->rc, 1, __ATOMIC_RELEASE);
            } else {
                if (block->rc > 0) {
                    block->rc -= 1;
                }
            }
            cheng_mm_alloc_total += 1;
            cheng_mm_live_total += 1;
            return out;
        }
    }
    size_t total = sizeof(ChengMemBlock) + (size_t)size;
    ChengMemBlock* resized = (ChengMemBlock*)realloc(block, total);
    if (!resized) {
        return NULL;
    }
    resized->size = (size_t)size;
    if (resized != block) {
        ChengMemScope* scope = resized->scope;
        if (scope) {
            if (resized->prev) {
                resized->prev->next = resized;
            } else if (scope->head == block) {
                scope->head = resized;
            }
            if (resized->next) {
                resized->next->prev = resized;
            }
        }
    }
    void* out = (void*)(resized + 1);
    if (out != p) {
        cheng_ptrmap_del(p);
        cheng_ptrmap_put(out, resized);
    }
    return out;
}
static const char* cheng_safe_cstr(const char* s) {
    if (s == NULL) {
        return "";
    }
    uintptr_t raw = (uintptr_t)s;
#if defined(__ANDROID__) && UINTPTR_MAX > 0xffffffffu
    if ((raw & (uintptr_t)0x1U) != (uintptr_t)0U) {
        raw -= (uintptr_t)0x1U;
    }
    /* Android crash guard: reject clearly invalid tagged/non-canonical pointers. */
    uint16_t hi16 = (uint16_t)(raw >> 48);
    bool hi_ok =
        hi16 == (uint16_t)0x0000U ||
        hi16 == (uint16_t)0xb400U ||
        hi16 == (uint16_t)0xb500U ||
        hi16 == (uint16_t)0xb600U ||
        hi16 == (uint16_t)0xb700U;
    bool suspicious_low_region = raw < (uintptr_t)0x7b10000000ULL;
    bool suspicious_page_base = (raw & (uintptr_t)0xffffffffULL) == (uintptr_t)0x00000000ULL;
    if (!hi_ok || suspicious_low_region || suspicious_page_base) {
        return "";
    }
#endif
    return (const char*)raw;
}

int32_t cheng_strlen(char* s) {
    const char* safe = cheng_safe_cstr((const char*)s);
    return (int32_t)strlen(safe);
}
void* cheng_memcpy(void* dest, void* src, int64_t n) { return memcpy(dest, src, (size_t)n); }
void* cheng_memset(void* dest, int32_t val, int64_t n) { return memset(dest, val, (size_t)n); }
void* cheng_memcpy_ffi(void* dest, void* src, int64_t n) { return cheng_memcpy(dest, src, n); }
void* cheng_memset_ffi(void* dest, int32_t val, int64_t n) { return cheng_memset(dest, val, n); }
__attribute__((weak)) void* alloc(int32_t size) { return cheng_malloc(size); }
__attribute__((weak)) void copyMem(void* dest, void* src, int32_t size) { (void)cheng_memcpy(dest, src, (int64_t)size); }
__attribute__((weak)) void setMem(void* dest, int32_t val, int32_t size) { (void)cheng_memset(dest, val, (int64_t)size); }
int32_t cheng_memcmp(void* a, void* b, int64_t n) { return memcmp(a, b, (size_t)n); }
int32_t cheng_strcmp(const char* a, const char* b) {
    return strcmp(cheng_safe_cstr(a), cheng_safe_cstr(b));
}
double cheng_bits_to_f32(int32_t bits) {
    union {
        uint32_t u;
        float f;
    } v;
    v.u = (uint32_t)bits;
    return (double)v.f;
}
int32_t cheng_f32_to_bits(double value) {
    union {
        uint32_t u;
        float f;
    } v;
    v.f = (float)value;
    return (int32_t)v.u;
}
int64_t cheng_f64_to_bits(double value) {
    union {
        uint64_t u;
        double f;
    } v;
    v.f = value;
    return (int64_t)v.u;
}
int64_t cheng_parse_f64_bits(const char* s) {
    if (!s) s = "0";
    char* end = NULL;
    double v = strtod(s, &end);
    (void)end;
    return cheng_f64_to_bits(v);
}
double cheng_bits_to_f64(int64_t bits) {
    union {
        uint64_t u;
        double f;
    } v;
    v.u = (uint64_t)bits;
    return v.f;
}
int64_t cheng_f64_add_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return cheng_f64_to_bits(a + b);
}
int64_t cheng_f64_sub_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return cheng_f64_to_bits(a - b);
}
int64_t cheng_f64_mul_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return cheng_f64_to_bits(a * b);
}
int64_t cheng_f64_div_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return cheng_f64_to_bits(a / b);
}
int64_t cheng_f64_neg_bits(int64_t a_bits) {
    double a = cheng_bits_to_f64(a_bits);
    return cheng_f64_to_bits(-a);
}
int64_t cheng_f64_lt_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return (a < b) ? 1 : 0;
}
int64_t cheng_f64_le_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return (a <= b) ? 1 : 0;
}
int64_t cheng_f64_gt_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return (a > b) ? 1 : 0;
}
int64_t cheng_f64_ge_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return (a >= b) ? 1 : 0;
}
int64_t cheng_i64_to_f64_bits(int64_t x) {
    return cheng_f64_to_bits((double)x);
}
int64_t cheng_u64_to_f64_bits(uint64_t x) {
    return cheng_f64_to_bits((double)x);
}
int64_t cheng_f64_bits_to_i64(int64_t bits) {
    return (int64_t)cheng_bits_to_f64(bits);
}
uint64_t cheng_f64_bits_to_u64(int64_t bits) {
    return (uint64_t)cheng_bits_to_f64(bits);
}
int64_t cheng_f32_bits_to_f64_bits(int32_t bits) {
    return cheng_f64_to_bits(cheng_bits_to_f32(bits));
}
int32_t cheng_f64_bits_to_f32_bits(int64_t bits) {
    return cheng_f32_to_bits(cheng_bits_to_f64(bits));
}
int64_t cheng_f32_bits_to_i64(int32_t bits) {
    return (int64_t)cheng_bits_to_f32(bits);
}
uint64_t cheng_f32_bits_to_u64(int32_t bits) {
    return (uint64_t)cheng_bits_to_f32(bits);
}
int32_t cheng_jpeg_decode(const uint8_t* data, int32_t len, int32_t* out_w, int32_t* out_h, uint8_t** out_rgba) {
    if (!data || len <= 0 || !out_w || !out_h || !out_rgba) return 0;
    int w = 0, h = 0, comp = 0;
    unsigned char* rgba = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
    if (!rgba) return 0;
    *out_w = (int32_t)w;
    *out_h = (int32_t)h;
    *out_rgba = rgba;
    return 1;
}
void cheng_jpeg_free(void* p) {
    if (p) {
        stbi_image_free(p);
    }
}

void* cheng_fopen(const char* filename, const char* mode) {
    return (void*)fopen(filename, mode);
}

int32_t cheng_fclose(void* f) { return fclose((FILE*)f); }
int32_t cheng_fread(void* ptr, int64_t size, int64_t n, void* stream) {
    return (int32_t)fread(ptr, (size_t)size, (size_t)n, (FILE*)stream);
}
int32_t cheng_fwrite(void* ptr, int64_t size, int64_t n, void* stream) {
    return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, (FILE*)stream);
}
int32_t cheng_fseek(void* stream, int64_t offset, int32_t whence) {
#if defined(_WIN32)
    return _fseeki64((FILE*)stream, offset, whence);
#else
    return fseeko((FILE*)stream, (off_t)offset, whence);
#endif
}
int64_t cheng_ftell(void* stream) {
#if defined(_WIN32)
    return (int64_t)_ftelli64((FILE*)stream);
#else
    return (int64_t)ftello((FILE*)stream);
#endif
}
int32_t cheng_fflush(void* stream) { return fflush((FILE*)stream); }
int32_t cheng_fgetc(void* stream) { return fgetc((FILE*)stream); }

void* get_stdin() { return (void*)stdin; }
void* get_stdout() { return (void*)stdout; }
void* get_stderr() { return (void*)stderr; }

// Backend fallback: provide `__addr` symbol (Mach-O uses leading underscore).
// The backend should lower `__addr(...)` as an intrinsic, but during bootstrap
// we keep this as a safe identity for pointer-sized values.
int64_t __addr(int64_t value) { return value; }
int64_t _addr(int64_t value) { return __addr(value); }

typedef void (*cheng_iometer_hook_t)(int32_t op, int64_t bytes);

void cheng_iometer_call(void* hook, int32_t op, int64_t bytes) {
    if (!hook) return;
    cheng_iometer_hook_t fn = (cheng_iometer_hook_t)hook;
    fn(op, bytes);
}

void* sys_memset(void* dest, int32_t val, int32_t n) {
    return memset(dest, val, n);
}

// ---- Minimal cross-platform std helpers ----

#include <time.h>

#if defined(_WIN32)
  #include <windows.h>
  #include <direct.h>   // _mkdir, _getcwd
  #include <sys/stat.h> // _stat
#else
  #if defined(__APPLE__)
    #include <mach/mach_time.h>
  #endif
  #include <dirent.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>   // getcwd
  #include <sys/stat.h> // stat, mkdir
  #include <sys/wait.h>
  #include <sys/ioctl.h>
  #include <poll.h>
#endif

int32_t cheng_file_exists(const char* path) {
    if (!path || !path[0]) return 0;
    FILE* f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

int32_t cheng_dir_exists(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (st.st_mode & _S_IFDIR) ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

int64_t cheng_file_mtime(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (int64_t)st.st_mtime;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int64_t)st.st_mtime;
#endif
}

int64_t cheng_file_size(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (int64_t)st.st_size;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int64_t)st.st_size;
#endif
}

int32_t cheng_mkdir1(const char* path) {
    if (!path || !path[0]) return -1;
#if defined(_WIN32)
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

char* cheng_getcwd(void) {
    static char buf[4096];
#if defined(_WIN32)
    if (_getcwd(buf, (int)sizeof(buf)) == NULL) return "";
#else
    if (getcwd(buf, sizeof(buf)) == NULL) return "";
#endif
    buf[sizeof(buf) - 1] = 0;
    return buf;
}

double cheng_epoch_time(void) {
    return (double)time(NULL);
}

int64_t cheng_monotime_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) return 0;
    if (!QueryPerformanceCounter(&counter)) return 0;
    return (int64_t)((counter.QuadPart * 1000000000LL) / freq.QuadPart);
#elif defined(__APPLE__)
    static mach_timebase_info_data_t info;
    if (info.denom == 0) {
        (void)mach_timebase_info(&info);
    }
    uint64_t t = mach_absolute_time();
    __uint128_t ns = (__uint128_t)t * (uint64_t)info.numer;
    ns /= (uint64_t)info.denom;
    return (int64_t)ns;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
#endif
}

static char* cheng_strdup(const char* s) {
    if (!s) {
        char* out = (char*)cheng_malloc(1);
        if (out) out[0] = 0;
        return out;
    }
    size_t len = strlen(s);
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = 0;
    return out;
}

static int cheng_buf_reserve(char** buf, size_t* cap, size_t need) {
    if (need <= *cap) return 1;
    size_t newCap = (*cap == 0) ? 256 : *cap;
    while (newCap < need) newCap *= 2;
    char* next = (char*)cheng_realloc(*buf, (int32_t)newCap);
    if (!next) return 0;
    *buf = next;
    *cap = newCap;
    return 1;
}

char* cheng_list_dir(const char* path) {
    if (!path || !path[0]) return cheng_strdup("");
    size_t cap = 256;
    size_t len = 0;
    char* out = (char*)cheng_malloc((int32_t)cap);
    if (!out) return NULL;
    out[0] = 0;
#if defined(_WIN32)
    size_t baseLen = strlen(path);
    int needsSep = (baseLen > 0 && path[baseLen - 1] != '\\' && path[baseLen - 1] != '/');
    size_t patLen = baseLen + (needsSep ? 1 : 0) + 2;
    char* pattern = (char*)malloc(patLen);
    if (!pattern) return out;
    size_t pos = 0;
    memcpy(pattern + pos, path, baseLen);
    pos += baseLen;
    if (needsSep) pattern[pos++] = '\\';
    pattern[pos++] = '*';
    pattern[pos] = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const char* name = fd.cFileName;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
        size_t nlen = strlen(name);
        size_t need = len + nlen + 2;
        if (!cheng_buf_reserve(&out, &cap, need)) break;
        memcpy(out + len, name, nlen);
        len += nlen;
        out[len++] = '\n';
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(path);
    if (!dir) return out;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
        size_t nlen = strlen(name);
        size_t need = len + nlen + 2;
        if (!cheng_buf_reserve(&out, &cap, need)) break;
        memcpy(out + len, name, nlen);
        len += nlen;
        out[len++] = '\n';
    }
    closedir(dir);
#endif
    if (len > 0 && out[len - 1] == '\n') len -= 1;
    if (!cheng_buf_reserve(&out, &cap, len + 1)) return out;
    out[len] = 0;
    return out;
}

static char g_cheng_empty_str[1] = {0};
static char* g_cheng_read_file_buf = NULL;
static size_t g_cheng_read_file_cap = 0;

static int cheng_read_file_reserve(size_t need) {
    if (need <= g_cheng_read_file_cap) return 1;
    size_t newCap = (g_cheng_read_file_cap == 0) ? 256 : g_cheng_read_file_cap;
    while (newCap < need) newCap *= 2;
    char* next = (char*)realloc(g_cheng_read_file_buf, newCap);
    if (!next) return 0;
    g_cheng_read_file_buf = next;
    g_cheng_read_file_cap = newCap;
    return 1;
}

char* cheng_read_file(const char* path) {
    if (!path || !path[0]) return g_cheng_empty_str;
    FILE* f = fopen(path, "rb");
    if (!f) return g_cheng_empty_str;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return g_cheng_empty_str;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return g_cheng_empty_str;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return g_cheng_empty_str;
    }
    size_t need = (size_t)size + 1;
    // Keep this buffer outside Cheng-managed memory: the returned pointer is treated as `str`
    // by the caller and may be released by ORC. Using malloc/realloc avoids dangling pointers
    // when the caller releases the temporary string.
    if (!cheng_read_file_reserve(need)) {
        fclose(f);
        return g_cheng_empty_str;
    }
    size_t n = fread(g_cheng_read_file_buf, 1, (size_t)size, f);
    g_cheng_read_file_buf[n] = 0;
    fclose(f);
    return g_cheng_read_file_buf;
}

int32_t cheng_write_file(const char* path, const char* content) {
    if (!path || !path[0]) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    if (!content) content = "";
    size_t len = strlen(content);
    size_t n = 0;
    if (len > 0) {
        n = fwrite(content, 1, len, f);
    }
    fclose(f);
    return (n == len) ? 1 : 0;
}

char* cheng_exec_cmd_ex(const char* command, const char* workingDir, int32_t mergeStderr, int64_t* exitCode) {
    if (exitCode) *exitCode = -1;
    if (!command || !command[0]) return cheng_strdup("");
    const char* suffix = mergeStderr ? " 2>&1" : "";
    size_t cmdLen = strlen(command);
    size_t sufLen = strlen(suffix);
    char* cmd = (char*)malloc(cmdLen + sufLen + 1);
    if (!cmd) return cheng_strdup("");
    memcpy(cmd, command, cmdLen);
    memcpy(cmd + cmdLen, suffix, sufLen);
    cmd[cmdLen + sufLen] = 0;

    char oldCwd[4096];
    int hasOld = 0;
#if defined(_WIN32)
    if (workingDir && workingDir[0]) {
        if (_getcwd(oldCwd, (int)sizeof(oldCwd)) != NULL) hasOld = 1;
        _chdir(workingDir);
    }
    FILE* pipe = _popen(cmd, "r");
#else
    if (workingDir && workingDir[0]) {
        if (getcwd(oldCwd, sizeof(oldCwd)) != NULL) hasOld = 1;
        chdir(workingDir);
    }
    FILE* pipe = popen(cmd, "r");
#endif
    free(cmd);
    if (!pipe) {
        if (hasOld) {
#if defined(_WIN32)
            _chdir(oldCwd);
#else
            chdir(oldCwd);
#endif
        }
        return cheng_strdup("");
    }

    size_t cap = 1024;
    size_t len = 0;
    char* out = (char*)cheng_malloc((int32_t)cap);
    if (!out) {
        out = cheng_strdup("");
        cap = out ? 1 : 0;
    }
    char buffer[4096];
    while (out && !feof(pipe)) {
        size_t n = fread(buffer, 1, sizeof(buffer), pipe);
        if (n == 0) break;
        if (!cheng_buf_reserve(&out, &cap, len + n + 1)) break;
        memcpy(out + len, buffer, n);
        len += n;
    }
    if (out) {
        if (!cheng_buf_reserve(&out, &cap, len + 1)) {
            if (cap > len) out[len] = 0;
        } else {
            out[len] = 0;
        }
    }

#if defined(_WIN32)
    int status = _pclose(pipe);
    if (exitCode) *exitCode = (int64_t)status;
    if (hasOld) _chdir(oldCwd);
#else
    int status = pclose(pipe);
    if (exitCode) {
        if (WIFEXITED(status)) *exitCode = (int64_t)WEXITSTATUS(status);
        else *exitCode = (int64_t)status;
    }
    if (hasOld) chdir(oldCwd);
#endif
    if (!out) return cheng_strdup("");
    return out;
}

char* chengQ_execQ_cmdQ_ex_0(char* command, char* workingDir, int32_t mergeStderr, int64_t* exitCode) {
    return cheng_exec_cmd_ex(command, workingDir, mergeStderr, exitCode);
}

int32_t cheng_pty_is_supported(void) {
#if defined(_WIN32)
    return 0;
#else
    return 1;
#endif
}

int32_t cheng_pty_spawn(const char* command, const char* workingDir, int32_t* outMasterFd, int64_t* outPid) {
#if defined(_WIN32)
    if (outMasterFd) *outMasterFd = -1;
    if (outPid) *outPid = -1;
    return 0;
#else
    if (outMasterFd) *outMasterFd = -1;
    if (outPid) *outPid = -1;
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) return 0;
    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        close(master_fd);
        return 0;
    }
    char* slave_name = ptsname(master_fd);
    if (!slave_name) {
        close(master_fd);
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(master_fd);
        return 0;
    }
    if (pid == 0) {
        setsid();
        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) _exit(127);
#ifdef TIOCSCTTY
        ioctl(slave_fd, TIOCSCTTY, 0);
#endif
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO) close(slave_fd);
        if (workingDir && workingDir[0]) chdir(workingDir);
        if (command && command[0]) {
            execl("/bin/sh", "sh", "-lc", command, (char*)NULL);
        } else {
            const char* shell = getenv("SHELL");
            if (!shell || !shell[0]) shell = "/bin/sh";
            execl(shell, shell, "-l", (char*)NULL);
        }
        _exit(127);
    }
    int flags = fcntl(master_fd, F_GETFL);
    if (flags != -1) {
        fcntl(master_fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    if (outMasterFd) *outMasterFd = master_fd;
    if (outPid) *outPid = (int64_t)pid;
    return 1;
#endif
}

int32_t cheng_pipe_spawn(const char* command, const char* workingDir, int32_t* outReadFd, int32_t* outWriteFd, int64_t* outPid) {
#if defined(_WIN32)
    if (outReadFd) *outReadFd = -1;
    if (outWriteFd) *outWriteFd = -1;
    if (outPid) *outPid = -1;
    return 0;
#else
    if (outReadFd) *outReadFd = -1;
    if (outWriteFd) *outWriteFd = -1;
    if (outPid) *outPid = -1;
    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) != 0) return 0;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return 0;
    }
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (workingDir && workingDir[0]) chdir(workingDir);
        if (command && command[0]) {
            execl("/bin/sh", "sh", "-lc", command, (char*)NULL);
        } else {
            const char* shell = getenv("SHELL");
            if (!shell || !shell[0]) shell = "/bin/sh";
            execl(shell, shell, "-l", (char*)NULL);
        }
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    int flags = fcntl(out_pipe[0], F_GETFL);
    if (flags != -1) {
        fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }
    flags = fcntl(in_pipe[1], F_GETFL);
    if (flags != -1) {
        fcntl(in_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }
    if (outReadFd) *outReadFd = out_pipe[0];
    if (outWriteFd) *outWriteFd = in_pipe[1];
    if (outPid) *outPid = (int64_t)pid;
    return 1;
#endif
}

char* cheng_pty_read(int32_t fd, int32_t maxBytes, int32_t* outEof) {
#if defined(_WIN32)
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#else
    if (outEof) *outEof = 0;
    if (fd < 0 || maxBytes <= 0) return cheng_strdup("");
    if (maxBytes > 65536) maxBytes = 65536;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, 0);
    if (rc <= 0 || (pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return cheng_strdup("");
    }
    char* buf = (char*)cheng_malloc(maxBytes + 1);
    if (!buf) return cheng_strdup("");
    ssize_t n = read(fd, buf, (size_t)maxBytes);
    if (n > 0) {
        buf[n] = 0;
        return buf;
    }
    cheng_free(buf);
    if (n == 0) {
        if (outEof) *outEof = 1;
        return cheng_strdup("");
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return cheng_strdup("");
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#endif
}

char* cheng_fd_read(int32_t fd, int32_t maxBytes, int32_t* outEof) {
#if defined(_WIN32)
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#else
    if (outEof) *outEof = 0;
    if (fd < 0 || maxBytes <= 0) return cheng_strdup("");
    if (maxBytes > 65536) maxBytes = 65536;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, 0);
    if (rc <= 0 || (pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return cheng_strdup("");
    }
    char* buf = (char*)cheng_malloc(maxBytes + 1);
    if (!buf) return cheng_strdup("");
    ssize_t n = read(fd, buf, (size_t)maxBytes);
    if (n > 0) {
        buf[n] = 0;
        return buf;
    }
    cheng_free(buf);
    if (n == 0) {
        if (outEof) *outEof = 1;
        return cheng_strdup("");
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return cheng_strdup("");
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#endif
}

char* cheng_fd_read_wait(int32_t fd, int32_t maxBytes, int32_t timeoutMs, int32_t* outEof) {
#if defined(_WIN32)
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#else
    if (outEof) *outEof = 0;
    if (fd < 0 || maxBytes <= 0) return cheng_strdup("");
    if (maxBytes > 65536) maxBytes = 65536;
    if (timeoutMs < 0) timeoutMs = 0;
    if (timeoutMs > 50) timeoutMs = 50;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, timeoutMs);
    if (rc <= 0 || (pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return cheng_strdup("");
    }
    char* buf = (char*)cheng_malloc(maxBytes + 1);
    if (!buf) return cheng_strdup("");
    ssize_t n = read(fd, buf, (size_t)maxBytes);
    if (n > 0) {
        buf[n] = 0;
        return buf;
    }
    cheng_free(buf);
    if (n == 0) {
        if (outEof) *outEof = 1;
        return cheng_strdup("");
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return cheng_strdup("");
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#endif
}

int32_t cheng_pty_write(int32_t fd, const char* data, int32_t len) {
#if defined(_WIN32)
    return -1;
#else
    if (fd < 0 || !data || len <= 0) return 0;
    ssize_t n = write(fd, data, (size_t)len);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int rc = poll(&pfd, 1, 100);
        if (rc > 0 && (pfd.revents & POLLOUT)) {
            n = write(fd, data, (size_t)len);
        }
    }
    if (n < 0) {
        static int pty_write_errs = 0;
        if (pty_write_errs < 8) {
            fprintf(stderr, "[pty] write failed fd=%d errno=%d (%s)\n", fd, errno, strerror(errno));
            pty_write_errs++;
        }
        return -1;
    }
    return (int32_t)n;
#endif
}

int32_t cheng_pty_close(int32_t fd) {
#if defined(_WIN32)
    return -1;
#else
    if (fd < 0) return 0;
    return close(fd);
#endif
}

int32_t cheng_pty_wait(int64_t pid, int32_t* outExitCode) {
#if defined(_WIN32)
    if (outExitCode) *outExitCode = -1;
    return -1;
#else
    if (outExitCode) *outExitCode = -1;
    if (pid <= 0) return -1;
    int status = 0;
    pid_t rc = waitpid((pid_t)pid, &status, WNOHANG);
    if (rc == 0) return 0;
    if (rc < 0) return -1;
    if (outExitCode) {
        if (WIFEXITED(status)) *outExitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *outExitCode = 128 + WTERMSIG(status);
        else *outExitCode = status;
    }
    return 1;
#endif
}

int32_t cheng_tcp_listener(int32_t port, int32_t* outPort) {
#if defined(_WIN32)
    if (outPort) *outPort = -1;
    return -1;
#else
    if (getenv("CODEX_LOGIN_DEBUG")) {
        fprintf(stderr, "[login] tcp_listener port=%d\n", port);
    }
    if (outPort) *outPort = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes)) != 0) {
        if (getenv("CODEX_LOGIN_DEBUG")) {
            fprintf(stderr, "[login] setsockopt SO_REUSEADDR failed errno=%d (%s)\n", errno, strerror(errno));
        }
    }
#ifdef SO_REUSEPORT
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, (socklen_t)sizeof(yes)) != 0) {
        if (getenv("CODEX_LOGIN_DEBUG")) {
            fprintf(stderr, "[login] setsockopt SO_REUSEPORT failed errno=%d (%s)\n", errno, strerror(errno));
        }
    }
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
#if defined(__APPLE__)
    addr.sin_len = (uint8_t)sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr)) != 0) {
        if (getenv("CODEX_LOGIN_DEBUG")) {
            fprintf(stderr, "[login] bind loopback failed errno=%d (%s)\n", errno, strerror(errno));
        }
        if (getenv("CODEX_LOGIN_ALLOW_ANY")) {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr)) != 0) {
                if (getenv("CODEX_LOGIN_DEBUG")) {
                    fprintf(stderr, "[login] bind any failed errno=%d (%s)\n", errno, strerror(errno));
                }
                close(fd);
                return -1;
            }
        } else {
            close(fd);
            return -1;
        }
    }
    if (listen(fd, 64) != 0) {
        if (getenv("CODEX_LOGIN_DEBUG")) {
            fprintf(stderr, "[login] listen failed errno=%d (%s)\n", errno, strerror(errno));
        }
        close(fd);
        return -1;
    }
    if (outPort) {
        struct sockaddr_in bound;
        socklen_t len = (socklen_t)sizeof(bound);
        memset(&bound, 0, sizeof(bound));
        if (getsockname(fd, (struct sockaddr*)&bound, &len) == 0) {
            *outPort = (int32_t)ntohs(bound.sin_port);
        }
    }
    return fd;
#endif
}

int32_t cheng_errno(void) {
    return errno;
}

const char* cheng_strerror(int32_t err) {
    return strerror(err);
}

int64_t cheng_abi_sum9_i64(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                           int64_t f, int64_t g, int64_t h, int64_t i) {
    return a + b + c + d + e + f + g + h + i;
}

int32_t cheng_abi_sum9_i32(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
                           int32_t f, int32_t g, int32_t h, int32_t i) {
    return a + b + c + d + e + f + g + h + i;
}

int64_t cheng_abi_sum16_i64(int64_t a, int64_t b, int64_t c, int64_t d,
                            int64_t e, int64_t f, int64_t g, int64_t h,
                            int64_t i, int64_t j, int64_t k, int64_t l,
                            int64_t m, int64_t n, int64_t o, int64_t p) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
}

int32_t cheng_abi_sum16_i32(int32_t a, int32_t b, int32_t c, int32_t d,
                            int32_t e, int32_t f, int32_t g, int32_t h,
                            int32_t i, int32_t j, int32_t k, int32_t l,
                            int32_t m, int32_t n, int32_t o, int32_t p) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
}

int64_t cheng_abi_mix_i32_i64(int32_t a, int64_t b, int32_t c, int64_t d) {
    return (int64_t)a + b + (int64_t)c + d;
}

typedef struct ChengAbiSeqI32View {
    int32_t len;
    int32_t cap;
    int32_t* buffer;
} ChengAbiSeqI32View;

static int32_t cheng_abi_sum_seq_i32_raw(const int32_t* ptr, int32_t len) {
    if (ptr == NULL || len <= 0) {
        return 0;
    }
    int64_t sum = 0;
    for (int32_t i = 0; i < len; i += 1) {
        sum += (int64_t)ptr[i];
    }
    return (int32_t)sum;
}

int32_t cheng_abi_sum_seq_i32(uint64_t seqLike0, uint64_t seqLike1, uint64_t seqLike2) {
    /*
     * RPSPAR-03 slice bridge helper:
     * - pointer mode: arg0 is pointer to seq header (len/cap/buffer)
     * - packed mode:  arg0 packs len/cap (low/high 32b), arg1 is data pointer
     * - split mode:   arg0=len, arg1=cap, arg2=data pointer
     */
    if (seqLike0 <= (uint64_t)(1u << 20) &&
        seqLike1 <= (uint64_t)(1u << 20) &&
        seqLike2 > (uint64_t)4096) {
        return cheng_abi_sum_seq_i32_raw((const int32_t*)(uintptr_t)seqLike2, (int32_t)seqLike0);
    }
    uint32_t lo = (uint32_t)(seqLike0 & 0xffffffffu);
    uint32_t hi = (uint32_t)((seqLike0 >> 32) & 0xffffffffu);
    if (lo <= hi && hi <= (uint32_t)(1u << 20) && seqLike1 > (uint64_t)4096) {
        return cheng_abi_sum_seq_i32_raw((const int32_t*)(uintptr_t)seqLike1, (int32_t)lo);
    }
    if (seqLike0 == 0) {
        return 0;
    }
    const ChengAbiSeqI32View* seq = (const ChengAbiSeqI32View*)(uintptr_t)seqLike0;
    if (seq->len < 0 || seq->len > (1 << 26)) {
        return 0;
    }
    return cheng_abi_sum_seq_i32_raw((const int32_t*)seq->buffer, seq->len);
}

void cheng_abi_store_i32(int64_t p, int32_t v) {
    if (p == 0) {
        return;
    }
    int32_t* pp = (int32_t*)(uintptr_t)p;
    *pp = v;
}

int32_t cheng_abi_load_i32(int64_t p) {
    if (p == 0) {
        return 0;
    }
    int32_t* pp = (int32_t*)(uintptr_t)p;
    return *pp;
}

typedef int32_t (*ChengAbiCbCtxI32)(int64_t ctx, int32_t a, int32_t b);

int32_t cheng_abi_call_cb_ctx_i32(void* fn_ptr, int64_t ctx, int32_t a, int32_t b) {
    if (!fn_ptr) {
        return 0;
    }
    ChengAbiCbCtxI32 fn = (ChengAbiCbCtxI32)fn_ptr;
    return fn(ctx, a, b);
}

int32_t cheng_abi_varargs_sum_i32(int32_t n, ...) {
    if (n <= 0) {
        return 0;
    }
    va_list ap;
    va_start(ap, n);
    int32_t sum = 0;
    for (int32_t i = 0; i < n; i += 1) {
        sum += (int32_t)va_arg(ap, int);
    }
    va_end(ap);
    return sum;
}

int32_t cheng_abi_varargs_sum10_i32_fixed(int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4,
                                         int32_t a5, int32_t a6, int32_t a7, int32_t a8, int32_t a9) {
    return cheng_abi_varargs_sum_i32(10, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

typedef struct ChengAbiPairI32 {
    int32_t a;
    int32_t b;
} ChengAbiPairI32;

ChengAbiPairI32 cheng_abi_ret_pair_i32(int32_t a, int32_t b) {
    ChengAbiPairI32 out;
    out.a = a;
    out.b = b;
    return out;
}

void cheng_abi_ret_pair_i32_out(int64_t outA, int64_t outB, int32_t a, int32_t b) {
    ChengAbiPairI32 r = cheng_abi_ret_pair_i32(a, b);
    if (outA != 0) {
        int32_t* pa = (int32_t*)(uintptr_t)outA;
        *pa = r.a;
    }
    if (outB != 0) {
        int32_t* pb = (int32_t*)(uintptr_t)outB;
        *pb = r.b;
    }
}

void cheng_abi_out_pair_i32(int64_t outA, int64_t outB, int32_t a, int32_t b) {
    if (outA != 0) {
        int32_t* pa = (int32_t*)(uintptr_t)outA;
        *pa = a;
    }
    if (outB != 0) {
        int32_t* pb = (int32_t*)(uintptr_t)outB;
        *pb = b;
    }
}

void cheng_abi_borrow_mut_pair_i32(int64_t pairPtr, int32_t da, int32_t db) {
    if (pairPtr == 0) {
        return;
    }
    ChengAbiPairI32* pair = (ChengAbiPairI32*)(uintptr_t)pairPtr;
    pair->a += da;
    pair->b += db;
}

typedef struct ChengFfiHandleSlot {
    void* ptr;
    uint32_t generation;
} ChengFfiHandleSlot;

static ChengFfiHandleSlot* cheng_ffi_handle_slots = NULL;
static uint32_t cheng_ffi_handle_slots_len = 0;
static uint32_t cheng_ffi_handle_slots_cap = 0;
static const int32_t cheng_ffi_handle_err_invalid = -1;

static int cheng_ffi_handle_ensure_capacity(uint32_t min_cap) {
    if (cheng_ffi_handle_slots_cap >= min_cap) {
        return 1;
    }
    uint32_t new_cap = cheng_ffi_handle_slots_cap;
    if (new_cap < 16) {
        new_cap = 16;
    }
    while (new_cap < min_cap) {
        if (new_cap > (UINT32_MAX / 2U)) {
            new_cap = min_cap;
            break;
        }
        new_cap *= 2U;
    }
    size_t bytes = (size_t)new_cap * sizeof(ChengFfiHandleSlot);
    if (new_cap != 0U && bytes / sizeof(ChengFfiHandleSlot) != (size_t)new_cap) {
        return 0;
    }
    ChengFfiHandleSlot* grown = (ChengFfiHandleSlot*)realloc(cheng_ffi_handle_slots, bytes);
    if (grown == NULL) {
        return 0;
    }
    for (uint32_t i = cheng_ffi_handle_slots_cap; i < new_cap; i += 1U) {
        grown[i].ptr = NULL;
        grown[i].generation = 1U;
    }
    cheng_ffi_handle_slots = grown;
    cheng_ffi_handle_slots_cap = new_cap;
    return 1;
}

static int cheng_ffi_handle_decode(uint64_t handle, uint32_t* out_idx, uint32_t* out_generation) {
    if (handle == 0ULL) {
        return 0;
    }
    uint32_t low = (uint32_t)(handle & 0xffffffffULL);
    uint32_t generation = (uint32_t)(handle >> 32U);
    if (low == 0U || generation == 0U) {
        return 0;
    }
    uint32_t idx = low - 1U;
    if (idx >= cheng_ffi_handle_slots_len) {
        return 0;
    }
    if (out_idx != NULL) {
        *out_idx = idx;
    }
    if (out_generation != NULL) {
        *out_generation = generation;
    }
    return 1;
}

uint64_t cheng_ffi_handle_register_ptr(void* ptr) {
    if (ptr == NULL) {
        return 0ULL;
    }
    uint32_t idx = UINT32_MAX;
    for (uint32_t i = 0; i < cheng_ffi_handle_slots_len; i += 1U) {
        if (cheng_ffi_handle_slots[i].ptr == NULL) {
            idx = i;
            break;
        }
    }
    if (idx == UINT32_MAX) {
        idx = cheng_ffi_handle_slots_len;
        if (!cheng_ffi_handle_ensure_capacity(idx + 1U)) {
            return 0ULL;
        }
        cheng_ffi_handle_slots_len = idx + 1U;
    }
    ChengFfiHandleSlot* slot = &cheng_ffi_handle_slots[idx];
    slot->ptr = ptr;
    if (slot->generation == 0U) {
        slot->generation = 1U;
    }
    return ((uint64_t)slot->generation << 32U) | (uint64_t)(idx + 1U);
}

void* cheng_ffi_handle_resolve_ptr(uint64_t handle) {
    uint32_t idx = 0;
    uint32_t generation = 0;
    if (!cheng_ffi_handle_decode(handle, &idx, &generation)) {
        return NULL;
    }
    ChengFfiHandleSlot* slot = &cheng_ffi_handle_slots[idx];
    if (slot->ptr == NULL || slot->generation != generation) {
        return NULL;
    }
    return slot->ptr;
}

int32_t cheng_ffi_handle_invalidate(uint64_t handle) {
    uint32_t idx = 0;
    uint32_t generation = 0;
    if (!cheng_ffi_handle_decode(handle, &idx, &generation)) {
        return cheng_ffi_handle_err_invalid;
    }
    ChengFfiHandleSlot* slot = &cheng_ffi_handle_slots[idx];
    if (slot->ptr == NULL || slot->generation != generation) {
        return cheng_ffi_handle_err_invalid;
    }
    slot->ptr = NULL;
    if (slot->generation == UINT32_MAX) {
        slot->generation = 1U;
    } else {
        slot->generation += 1U;
    }
    return 0;
}

uint64_t cheng_ffi_handle_new_i32(int32_t value) {
    int32_t* cell = (int32_t*)malloc(sizeof(int32_t));
    if (cell == NULL) {
        return 0ULL;
    }
    *cell = value;
    uint64_t handle = cheng_ffi_handle_register_ptr((void*)cell);
    if (handle == 0ULL) {
        free(cell);
    }
    return handle;
}

int32_t cheng_ffi_handle_get_i32(uint64_t handle, int32_t* out_value) {
    if (out_value == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    *out_value = *cell;
    return 0;
}

int32_t cheng_ffi_handle_add_i32(uint64_t handle, int32_t delta, int32_t* out_value) {
    if (out_value == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    *cell += delta;
    *out_value = *cell;
    return 0;
}

int32_t cheng_ffi_handle_release_i32(uint64_t handle) {
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    if (cheng_ffi_handle_invalidate(handle) != 0) {
        return cheng_ffi_handle_err_invalid;
    }
    free(cell);
    return 0;
}
