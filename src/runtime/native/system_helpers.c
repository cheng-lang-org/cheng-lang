#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#if !defined(_WIN32) && !defined(__ANDROID__)
#include <sched.h>
#include <pthread.h>
#endif
#if !defined(_WIN32) && !defined(__ANDROID__)
#if defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define CHENG_HAS_EXECINFO 1
#endif
#endif
#endif

#ifndef CHENG_HAS_EXECINFO
#define CHENG_HAS_EXECINFO 0
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#include "cheng_sidecar_loader.h"
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define CHENG_THREAD_LOCAL __declspec(thread)
#else
#define CHENG_THREAD_LOCAL
#endif

typedef struct ChengSeqHeader {
    int32_t len;
    int32_t cap;
    void* buffer;
} ChengSeqHeader;

typedef struct ChengErrorInfoCompat {
    int32_t code;
    char* msg;
} ChengErrorInfoCompat;

void* cheng_malloc(int32_t size);
int32_t cheng_strlen(char* s);
void cheng_mem_retain(void* p);
void cheng_mem_release(void* p);
void* cheng_seq_set_grow(void* seq_ptr, int32_t idx, int32_t elem_size);
int32_t driver_c_str_has_prefix(const char* s, const char* prefix);
int32_t driver_c_str_has_suffix(const char* s, const char* suffix);
int32_t driver_c_str_contains_char(const char* s, int32_t value);
int32_t driver_c_str_contains_str(const char* s, const char* sub);
char* driver_c_str_clone(const char* s);
char* driver_c_str_slice(const char* s, int32_t start, int32_t count);
char* driver_c_char_to_str(int32_t value);
char* driver_c_i32_to_str(int32_t value);
char* driver_c_i64_to_str(int64_t value);
char* driver_c_bool_to_str(bool value);
static char* cheng_copy_string_bytes(const char* src, size_t len);

static bool cheng_probably_valid_ptr(const void* p) {
    uintptr_t raw = (uintptr_t)p;
    if (p == NULL) return false;
    if (raw < (uintptr_t)65536) return false;
    if ((raw & (uintptr_t)(sizeof(void*) - 1)) != 0) return false;
    return true;
}

int32_t astRefIsNil(void* v) {
    return v == NULL ? 1 : 0;
}

int32_t astRefNonNil(void* v) {
    return v != NULL ? 1 : 0;
}

int32_t rawmemIsNil(void* v) {
    return v == NULL ? 1 : 0;
}

char* cheng_rawmem_str_from_offset(char* s, int32_t off) {
    if (s == NULL) {
        return "";
    }
    if (off <= 0) {
        return s;
    }
    int32_t n = cheng_strlen(s);
    if (off >= n) {
        return "";
    }
    return s + off;
}

void cheng_seq_delete_shift(void* buffer, int32_t at, int32_t len, int32_t elem_size) {
    if (buffer == NULL || elem_size <= 0 || at < 0 || at >= len) {
        return;
    }
    if (at + 1 >= len) {
        return;
    }
    char* base = (char*)buffer;
    size_t dst_off = (size_t)at * (size_t)elem_size;
    size_t src_off = (size_t)(at + 1) * (size_t)elem_size;
    size_t move_bytes = (size_t)(len - at - 1) * (size_t)elem_size;
    memmove(base + dst_off, base + src_off, move_bytes);
}

void cheng_seq_add_ptr_value(ChengSeqHeader* seq_hdr, void* val) {
    if (seq_hdr == NULL) {
        return;
    }
    if (seq_hdr->buffer == NULL || seq_hdr->len >= seq_hdr->cap) {
        int32_t need = seq_hdr->len + 1;
        int32_t new_cap = seq_hdr->cap;
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
        size_t old_bytes = seq_hdr->buffer != NULL ? (size_t)seq_hdr->cap * sizeof(void*) : 0;
        size_t new_bytes = (size_t)new_cap * sizeof(void*);
        void* new_buffer = realloc(seq_hdr->buffer, new_bytes);
        if (new_buffer == NULL) {
            abort();
        }
        seq_hdr->buffer = new_buffer;
        seq_hdr->cap = new_cap;
        if (new_bytes > old_bytes) {
            memset((char*)new_buffer + old_bytes, 0, new_bytes - old_bytes);
        }
    }
    ((void**)seq_hdr->buffer)[seq_hdr->len] = val;
  seq_hdr->len += 1;
}

int32_t cheng_seq_header_len_get(void* seq_ptr) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL) return 0;
    return seq->len;
}

void cheng_seq_header_len_set(void* seq_ptr, int32_t value) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL) return;
    seq->len = value;
}

int32_t cheng_seq_header_cap_get(void* seq_ptr) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL) return 0;
    return seq->cap;
}

void cheng_seq_header_cap_set(void* seq_ptr, int32_t value) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL) return;
    seq->cap = value;
}

void* cheng_seq_header_buffer_get(void* seq_ptr) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL) return NULL;
    return seq->buffer;
}

void cheng_seq_header_buffer_set(void* seq_ptr, void* value) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL) return;
    seq->buffer = value;
}

void cheng_seq_grow_to_raw(void** p_buffer, int32_t* p_cap, int32_t min_cap, int32_t elem_size) {
    if (p_buffer == NULL || p_cap == NULL || min_cap <= 0 || elem_size <= 0) {
        return;
    }
    int32_t cur_cap = *p_cap;
    void* raw_buffer = *p_buffer;
    if (raw_buffer != NULL && min_cap <= cur_cap) {
        return;
    }
    int32_t new_cap = cur_cap;
    if (new_cap < min_cap) {
        if (new_cap < 4) {
            new_cap = 4;
        }
        while (new_cap < min_cap) {
            int32_t doubled = new_cap * 2;
            if (doubled <= 0) {
                new_cap = min_cap;
                break;
            }
            new_cap = doubled;
        }
    }
    size_t old_bytes = raw_buffer != NULL ? (size_t)cur_cap * (size_t)elem_size : 0;
    size_t new_bytes = (size_t)new_cap * (size_t)elem_size;
    void* new_buffer = realloc(raw_buffer, new_bytes);
    if (new_buffer == NULL) {
        abort();
    }
    *p_buffer = new_buffer;
    *p_cap = new_cap;
    if (new_bytes > old_bytes) {
        memset((char*)new_buffer + old_bytes, 0, new_bytes - old_bytes);
    }
}

void cheng_seq_grow_inst(void* seq_ptr, int32_t min_cap, int32_t elem_size) {
    if (seq_ptr == NULL || min_cap <= 0 || elem_size <= 0) {
        return;
    }
    ChengSeqHeader* seq_hdr = (ChengSeqHeader*)seq_ptr;
    int32_t buf_cap = seq_hdr->cap;
    void* raw_buffer = seq_hdr->buffer;
    if (raw_buffer == NULL || min_cap > buf_cap) {
        cheng_seq_grow_to_raw(&raw_buffer, &buf_cap, min_cap, elem_size);
        seq_hdr->buffer = raw_buffer;
        seq_hdr->cap = buf_cap;
    }
}

int32_t cheng_seq_string_init_cap(int32_t seq_len, int32_t seq_cap) {
    int32_t base_cap = seq_cap < seq_len ? seq_len : seq_cap;
    if (base_cap <= 0) {
        return 0;
    }
    if (base_cap < 4) {
        return 4;
    }
    return base_cap;
}

int32_t cheng_seq_string_elem_bytes_compat(void) {
    return (int32_t)sizeof(void*);
}

void* cheng_seq_string_alloc_compat(int32_t buf_cap) {
    if (buf_cap <= 0) {
        return NULL;
    }
    int32_t total_bytes = buf_cap * (int32_t)sizeof(void*);
    void* raw_buffer = realloc(NULL, (size_t)total_bytes);
    if (raw_buffer == NULL) {
        abort();
    }
    memset(raw_buffer, 0, (size_t)total_bytes);
    return raw_buffer;
}

void cheng_seq_string_init_compat(void* seq_ptr, int32_t seq_len, int32_t seq_cap) {
    if (seq_ptr == NULL) {
        return;
    }
    ChengSeqHeader* seq_hdr = (ChengSeqHeader*)seq_ptr;
    int32_t buf_cap = cheng_seq_string_init_cap(seq_len, seq_cap);
    void* raw_buffer = NULL;
    if (buf_cap > 0) {
        raw_buffer = cheng_seq_string_alloc_compat(buf_cap);
    }
    seq_hdr->buffer = raw_buffer;
    seq_hdr->cap = buf_cap;
    seq_hdr->len = seq_len;
}

ChengSeqHeader cheng_new_seq_string_compat(int32_t seq_len, int32_t seq_cap) {
    ChengSeqHeader out;
    out.len = 0;
    out.cap = 0;
    out.buffer = NULL;
    cheng_seq_string_init_compat(&out, seq_len, seq_cap);
    return out;
}

void cheng_seq_free(void* seq_ptr) {
    if (seq_ptr == NULL) {
        return;
    }
    ChengSeqHeader* seq_hdr = (ChengSeqHeader*)seq_ptr;
    if (seq_hdr->buffer != NULL) {
        cheng_mem_release(seq_hdr->buffer);
    }
    seq_hdr->buffer = NULL;
    seq_hdr->len = 0;
    seq_hdr->cap = 0;
}

void cheng_seq_arc_retain(ChengSeqHeader seq) {
    if (seq.buffer != NULL) {
        cheng_mem_retain(seq.buffer);
    }
}

void cheng_seq_arc_release(ChengSeqHeader seq) {
    if (seq.buffer != NULL) {
        cheng_mem_release(seq.buffer);
    }
}

char* cheng_seq_string_get_compat(ChengSeqHeader seq, int32_t at) {
    if (!cheng_probably_valid_ptr(seq.buffer) || at < 0 || seq.len < 0 || seq.cap < 0 || seq.len > seq.cap || at >= seq.len || at >= seq.cap) {
        return "";
    }
    char* value = ((char**)seq.buffer)[at];
    if (!cheng_probably_valid_ptr(value)) {
        return "";
    }
    return value;
}

void cheng_seq_string_add_compat(void* seq_ptr, const char* value) {
    if (seq_ptr == NULL) {
        return;
    }
    ChengSeqHeader* seq_hdr = (ChengSeqHeader*)seq_ptr;
    int32_t write_at = seq_hdr->len;
    char** write_ptr = (char**)cheng_seq_set_grow(seq_ptr, write_at, (int32_t)sizeof(void*));
    if (write_ptr != NULL) {
        *write_ptr = (char*)value;
    }
}

int32_t cheng_rawbytes_get_at(void* base, int32_t idx) {
    if (base == NULL || idx < 0) {
        return 0;
    }
    return (int32_t)((uint8_t*)base)[idx];
}

void cheng_rawbytes_set_at(void* base, int32_t idx, int32_t value) {
    if (base == NULL || idx < 0) {
        return;
    }
    ((uint8_t*)base)[idx] = (uint8_t)value;
}

void cheng_rawmem_write_i8(void* dst, int32_t idx, int8_t value) {
    if (dst == NULL || idx < 0) {
        return;
    }
    ((uint8_t*)dst)[idx] = (uint8_t)value;
}

void cheng_rawmem_write_char(void* dst, int32_t idx, int32_t value) {
    if (dst == NULL || idx < 0) {
        return;
    }
    ((uint8_t*)dst)[idx] = (uint8_t)value;
}

void* cheng_seq_slice_alloc(void* buffer, int32_t len, int32_t start_pos, int32_t stop_pos,
                            int32_t exclusive, int32_t elem_size,
                            int32_t* out_len, int32_t* out_cap) {
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_cap != NULL) {
        *out_cap = 0;
    }
    if (buffer == NULL || len <= 0 || elem_size <= 0) {
        return NULL;
    }
    int32_t start = start_pos;
    if (start < 0) {
        start = 0;
    }
    if (start >= len) {
        return NULL;
    }
    int32_t end_exclusive = stop_pos;
    if (exclusive == 0) {
        if (stop_pos < 0) {
            return NULL;
        }
        if (stop_pos >= INT32_MAX) {
            end_exclusive = len;
        } else {
            end_exclusive = stop_pos + 1;
        }
    }
    if (end_exclusive <= start) {
        return NULL;
    }
    if (end_exclusive > len) {
        end_exclusive = len;
    }
    int32_t slice_len = end_exclusive - start;
    if (slice_len <= 0) {
        return NULL;
    }
    size_t total = (size_t)slice_len * (size_t)elem_size;
    void* out = cheng_malloc((int32_t)total);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, (char*)buffer + ((size_t)start * (size_t)elem_size), total);
    if (out_len != NULL) {
        *out_len = slice_len;
    }
    if (out_cap != NULL) {
        *out_cap = slice_len;
    }
    return out;
}

typedef struct ChengStrBridge {
    const char* ptr;
    int32_t len;
    int32_t store_id;
    int32_t flags;
} ChengStrBridge;

enum {
    CHENG_STR_BRIDGE_FLAG_NONE = 0,
    CHENG_STR_BRIDGE_FLAG_OWNED = 1,
};

static ChengStrBridge cheng_str_bridge_empty(void);
static ChengStrBridge cheng_str_bridge_from_ptr_flags(const char* ptr, int32_t flags);
static ChengStrBridge cheng_str_bridge_from_owned(char* ptr);

typedef void (*cheng_emit_obj_from_module_fn)(ChengSeqHeader* out, void* module, int32_t optLevel,
                                              const char* target, const char* objWriter,
                                              int32_t validateModule, int32_t uirSimdEnabled,
                                              int32_t uirSimdMaxWidth, const char* uirSimdPolicy);
typedef int32_t (*cheng_driver_emit_obj_default_fn)(void* module, const char* target,
                                                    const char* outPath);
typedef void* (*cheng_build_active_module_ptrs_fn)(void* inputRaw, void* targetRaw);
typedef void* (*cheng_build_module_stage1_fn)(const char* path, const char* target);

typedef enum ChengDriverModuleKind {
    CHENG_DRIVER_MODULE_KIND_LEGACY_RAW = 0,
    CHENG_DRIVER_MODULE_KIND_RAW = 1,
    CHENG_DRIVER_MODULE_KIND_RETAINED = 2,
    CHENG_DRIVER_MODULE_KIND_SIDECAR = 3,
} ChengDriverModuleKind;

typedef struct ChengDriverModuleHandle {
    uint32_t magic;
    uint32_t kind;
    void* payload;
} ChengDriverModuleHandle;

enum {
    CHENG_DRIVER_MODULE_HANDLE_MAGIC = 0x43444d48u,
};

static ChengDriverModuleHandle* driver_c_module_handle_try(void* module) {
    ChengDriverModuleHandle* handle = (ChengDriverModuleHandle*)module;
    if (module == NULL) return NULL;
    if (handle->magic != CHENG_DRIVER_MODULE_HANDLE_MAGIC) return NULL;
    return handle;
}

static void* driver_c_module_wrap(void* payload, ChengDriverModuleKind kind) {
    ChengDriverModuleHandle* handle;
    if (payload == NULL) return NULL;
    handle = (ChengDriverModuleHandle*)malloc(sizeof(ChengDriverModuleHandle));
    if (handle == NULL) return payload;
    handle->magic = CHENG_DRIVER_MODULE_HANDLE_MAGIC;
    handle->kind = (uint32_t)kind;
    handle->payload = payload;
    return (void*)handle;
}

static ChengDriverModuleKind driver_c_module_kind(void* module) {
    ChengDriverModuleHandle* handle = driver_c_module_handle_try(module);
    if (handle == NULL) return CHENG_DRIVER_MODULE_KIND_LEGACY_RAW;
    return (ChengDriverModuleKind)handle->kind;
}

static void* driver_c_module_payload(void* module) {
    ChengDriverModuleHandle* handle = driver_c_module_handle_try(module);
    if (handle == NULL) return module;
    return handle->payload;
}

static void driver_c_module_dispose(void* module) {
    ChengDriverModuleHandle* handle = driver_c_module_handle_try(module);
    if (handle != NULL) free(handle);
}

typedef struct ChengStrMetaEntry {
    const char* ptr;
    int32_t len;
} ChengStrMetaEntry;

static ChengStrMetaEntry* cheng_strmeta_entries = NULL;
static int32_t cheng_strmeta_len = 0;
static int32_t cheng_strmeta_cap = 0;
static volatile int cheng_strmeta_lock = 0;

static int driver_c_diag_enabled(void);
static void driver_c_diagf(const char* fmt, ...);
typedef void (*cheng_global_init_fn)(void);

static int32_t driver_c_env_i32(const char* name, int32_t fallback) {
    char* end = NULL;
    long value;
    const char* raw = getenv(name);
    if (raw == NULL || raw[0] == '\0') return fallback;
    value = strtol(raw, &end, 10);
    if (end == raw) return fallback;
    return (int32_t)value;
}

#define CHENG_CRASH_TRACE_RING 128

typedef struct ChengCrashTraceEntry {
    const char* tag;
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t d;
} ChengCrashTraceEntry;

static ChengCrashTraceEntry cheng_crash_trace_ring[CHENG_CRASH_TRACE_RING];
static volatile int32_t cheng_crash_trace_next = 0;
static volatile int32_t cheng_crash_trace_enabled_cache = -1;
static volatile int32_t cheng_crash_trace_installed = 0;
static volatile sig_atomic_t cheng_crash_trace_handling = 0;
static const char* cheng_crash_trace_phase = NULL;

static int cheng_flag_enabled(const char* raw) {
    if (raw == NULL || raw[0] == '\0') return 0;
    if (raw[0] == '0' && raw[1] == '\0') return 0;
    return 1;
}

static int cheng_crash_trace_env_enabled(void) {
    if (cheng_flag_enabled(getenv("CHENG_CRASH_TRACE"))) return 1;
    if (cheng_flag_enabled(getenv("BACKEND_CRASH_TRACE"))) return 1;
    if (cheng_flag_enabled(getenv("STAGE1_CRASH_TRACE"))) return 1;
    return 0;
}

static size_t cheng_crash_trace_cstrnlen(const char* s, size_t limit) {
    size_t n = 0;
    if (s == NULL) return 0;
    while (n < limit && s[n] != '\0') n++;
    return n;
}

static void cheng_crash_trace_write_buf(const char* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t wrote = write(2, buf + off, len - off);
        if (wrote <= 0) return;
        off += (size_t)wrote;
    }
}

static void cheng_crash_trace_write_cstr(const char* s) {
    const char* text = (s != NULL) ? s : "?";
    cheng_crash_trace_write_buf(text, cheng_crash_trace_cstrnlen(text, 160));
}

static void cheng_crash_trace_write_i32(int32_t value) {
    char buf[24];
    char* out = buf + sizeof(buf);
    uint32_t mag;
    *--out = '\0';
    if (value < 0) {
        mag = (uint32_t)(-(value + 1)) + 1u;
    } else {
        mag = (uint32_t)value;
    }
    do {
        *--out = (char)('0' + (mag % 10u));
        mag /= 10u;
    } while (mag != 0u);
    if (value < 0) {
        *--out = '-';
    }
    cheng_crash_trace_write_cstr(out);
}

static void cheng_crash_trace_dump_async(const char* reason, int32_t sig) {
    cheng_crash_trace_write_cstr("[cheng-crash-trace] reason=");
    cheng_crash_trace_write_cstr(reason);
    if (sig != 0) {
        cheng_crash_trace_write_cstr(" sig=");
        cheng_crash_trace_write_i32(sig);
    }
    if (cheng_crash_trace_phase != NULL) {
        cheng_crash_trace_write_cstr(" phase=");
        cheng_crash_trace_write_cstr(cheng_crash_trace_phase);
    }
    cheng_crash_trace_write_cstr("\n");
    int32_t end = cheng_crash_trace_next;
    int32_t start = end > CHENG_CRASH_TRACE_RING ? end - CHENG_CRASH_TRACE_RING : 0;
    for (int32_t i = start; i < end; ++i) {
        const ChengCrashTraceEntry* entry = &cheng_crash_trace_ring[(uint32_t)i % CHENG_CRASH_TRACE_RING];
        if (entry->tag == NULL) continue;
        cheng_crash_trace_write_cstr("[cheng-crash-trace] #");
        cheng_crash_trace_write_i32(i);
        cheng_crash_trace_write_cstr(" ");
        cheng_crash_trace_write_cstr(entry->tag);
        cheng_crash_trace_write_cstr(" a=");
        cheng_crash_trace_write_i32(entry->a);
        cheng_crash_trace_write_cstr(" b=");
        cheng_crash_trace_write_i32(entry->b);
        cheng_crash_trace_write_cstr(" c=");
        cheng_crash_trace_write_i32(entry->c);
        cheng_crash_trace_write_cstr(" d=");
        cheng_crash_trace_write_i32(entry->d);
        cheng_crash_trace_write_cstr("\n");
    }
}

static void cheng_crash_trace_signal_handler(int sig) {
    if (cheng_crash_trace_handling) _exit(128 + sig);
    cheng_crash_trace_handling = 1;
    cheng_crash_trace_dump_async("signal", sig);
    _exit(128 + sig);
}

static void cheng_crash_trace_install_handlers(void) {
    if (__sync_lock_test_and_set(&cheng_crash_trace_installed, 1) != 0) return;
#if !defined(_WIN32)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = cheng_crash_trace_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
#endif
}

WEAK int32_t cheng_crash_trace_enabled(void) {
    int32_t cached = cheng_crash_trace_enabled_cache;
    if (cached >= 0) return cached;
    cached = cheng_crash_trace_env_enabled() ? 1 : 0;
    cheng_crash_trace_enabled_cache = cached;
    if (cached) cheng_crash_trace_install_handlers();
    return cached;
}

WEAK void cheng_crash_trace_set_phase(const char* phase) {
    if (!cheng_crash_trace_enabled()) return;
    cheng_crash_trace_phase = phase;
}

WEAK void cheng_crash_trace_mark(const char* tag, int32_t a, int32_t b, int32_t c, int32_t d) {
    if (!cheng_crash_trace_enabled()) return;
    int32_t serial = __sync_fetch_and_add(&cheng_crash_trace_next, 1);
    ChengCrashTraceEntry* entry = &cheng_crash_trace_ring[(uint32_t)serial % CHENG_CRASH_TRACE_RING];
    entry->a = a;
    entry->b = b;
    entry->c = c;
    entry->d = d;
    __sync_synchronize();
    entry->tag = tag;
}

static void cheng_crash_trace_dump_now(const char* reason) {
    if (!cheng_crash_trace_enabled()) return;
    cheng_crash_trace_dump_async(reason, 0);
}

static ChengSidecarBundleCache cheng_sidecar_cache = {0};

static void driver_c_sidecar_after_open(void* handle, ChengSidecarDiagFn diag) {
    dlerror();
    cheng_global_init_fn global_init =
        (cheng_global_init_fn)dlsym(handle, "__cheng_global_init");
    const char* init_err = dlerror();
    if (diag != NULL) {
        diag("[driver_c_sidecar_bundle_handle] global_init=%p err='%s'\n",
             (void*)global_init, init_err != NULL ? init_err : "");
    }
    if (global_init != NULL) {
        global_init();
    }
}

static int driver_c_diag_enabled(void) {
    const char* raw = getenv("BACKEND_DEBUG_BOOT");
    if (raw == NULL || raw[0] == '\0') return 0;
    if (raw[0] == '0' && raw[1] == '\0') return 0;
    return 1;
}

static void driver_c_diagf(const char* fmt, ...) {
    if (!driver_c_diag_enabled()) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
}

static int cheng_backtrace_enabled(void) {
    const char* raw = getenv("CHENG_PANIC_BACKTRACE");
    if (raw == NULL || raw[0] == '\0') {
        raw = getenv("BACKEND_DEBUG_BACKTRACE");
    }
    if (raw == NULL || raw[0] == '\0') return 0;
    if (raw[0] == '0' && raw[1] == '\0') return 0;
    return 1;
}

void cheng_dump_backtrace_if_enabled(void) {
    cheng_crash_trace_dump_now("panic");
    if (!cheng_backtrace_enabled()) return;
#if CHENG_HAS_EXECINFO
    void* frames[48];
    int count = backtrace(frames, 48);
    if (count > 0) {
        backtrace_symbols_fd(frames, count, 2);
        return;
    }
#endif
    fprintf(stderr, "[cheng] backtrace unavailable\n");
    fflush(stderr);
}

static int cheng_arg_is_help(const char* arg) {
    if (arg == NULL) return 0;
    return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

WEAK int32_t driver_c_backend_usage(void) {
    fputs("Usage:\n", stderr);
    fputs("  backend_driver [--input:<file>|<file>] [--output:<out>] [options]\n", stderr);
    fputs("\n", stderr);
    fputs("Core options:\n", stderr);
    fputs("  --emit:<exe> --target:<triple|auto> --frontend:<stage1>\n", stderr);
    fputs("  --linker:<self|system> --obj-writer:<auto|elf|macho|coff>\n", stderr);
    fputs("  --opt-level:<N> --opt2 --no-opt2 --opt --no-opt\n", stderr);
    fputs("  --multi --no-multi --multi-force --no-multi-force\n", stderr);
    fputs("  --incremental --no-incremental --allow-no-main\n", stderr);
    fputs("  --skip-global-init --runtime-obj:<path> --runtime-c:<path> --no-runtime-c\n", stderr);
    fputs("  --generic-mode:<dict> --generic-spec-budget:<N>\n", stderr);
    fputs("  --borrow-ir:<mir|stage1> --generic-lowering:<mir_dict>\n", stderr);
    fputs("  --abi:<v2_noptr> --android-api:<N> --compile-stamp-out:<path>\n", stderr);
    fputs("  --profile --no-profile --uir-profile --no-uir-profile\n", stderr);
    fputs("  --uir-simd --no-uir-simd --uir-simd-max-width:<N> --uir-simd-policy:<name>\n", stderr);
    return 0;
}

WEAK int32_t driver_c_write_stderr(const char* text) {
    if (text == NULL) return 0;
    size_t n = strlen(text);
    if (n == 0) return 0;
    size_t wrote = fwrite(text, 1, n, stderr);
    fflush(stderr);
    return wrote == n ? 0 : -1;
}

WEAK int32_t driver_c_write_stderr_line(const char* text) {
    if (text != NULL && text[0] != '\0') {
        size_t n = strlen(text);
        if (n > 0) {
            size_t wrote = fwrite(text, 1, n, stderr);
            if (wrote != n) {
                fflush(stderr);
                return -1;
            }
        }
    }
    if (fwrite("\n", 1, 1, stderr) != 1) {
        fflush(stderr);
        return -1;
    }
    fflush(stderr);
    return 0;
}

WEAK void driver_c_boot_marker(int32_t code);
WEAK void* driver_c_build_module_stage1(const char* inputPath, const char* target);
WEAK void* driver_c_build_module_stage1_direct(const char* inputPath, const char* target);
typedef int32_t (*cheng_build_emit_obj_stage1_target_fn)(const char* inputPath, const char* target,
                                                         const char* outputPath);
WEAK int32_t driver_c_arg_count(void);
WEAK char* driver_c_arg_copy(int32_t i);
WEAK int32_t __cheng_rt_paramCount(void);
WEAK const char* __cheng_rt_paramStr(int32_t i);
WEAK char* __cheng_rt_paramStrCopy(int32_t i);

int32_t cheng_strlen(char* s);
void* cheng_malloc(int32_t size);
void cheng_free(void* p);
int32_t cheng_file_exists(const char* path);
int64_t cheng_file_size(const char* path);
int32_t cheng_open_w_trunc(const char* path);
int32_t cheng_fd_write(int32_t fd, const char* data, int32_t len);
int32_t libc_close(int32_t fd);
WEAK int32_t driver_c_finish_single_link(const char* path, const char* objPath);
WEAK int32_t driver_c_finish_emit_obj(const char* path);
WEAK void* driver_c_build_module_stage1(const char* inputPath, const char* target);

static void cheng_strmeta_acquire(void) {
    while (__sync_lock_test_and_set(&cheng_strmeta_lock, 1) != 0) {
    }
}

static void cheng_strmeta_release(void) {
    __sync_lock_release(&cheng_strmeta_lock);
}

static void cheng_strmeta_put(const char* ptr, int32_t len) {
    if (ptr == NULL || len < 0) return;
    cheng_strmeta_acquire();
    if (cheng_strmeta_len >= cheng_strmeta_cap) {
        int32_t next_cap = cheng_strmeta_cap < 256 ? 256 : (cheng_strmeta_cap * 2);
        ChengStrMetaEntry* next_entries =
            (ChengStrMetaEntry*)realloc(cheng_strmeta_entries, (size_t)next_cap * sizeof(ChengStrMetaEntry));
        if (next_entries != NULL) {
            cheng_strmeta_entries = next_entries;
            cheng_strmeta_cap = next_cap;
        }
    }
    if (cheng_strmeta_entries != NULL && cheng_strmeta_len < cheng_strmeta_cap) {
        cheng_strmeta_entries[cheng_strmeta_len].ptr = ptr;
        cheng_strmeta_entries[cheng_strmeta_len].len = len;
        cheng_strmeta_len += 1;
    }
    cheng_strmeta_release();
}

WEAK void cheng_register_string_meta(const char* ptr, int32_t len) {
    cheng_strmeta_put(ptr, len);
}

static bool cheng_strmeta_get(const char* ptr, size_t* out_len) {
    if (ptr == NULL) return false;
    bool found = false;
    cheng_strmeta_acquire();
    for (int32_t i = cheng_strmeta_len - 1; i >= 0; --i) {
        if (cheng_strmeta_entries[i].ptr == ptr) {
            if (out_len != NULL) *out_len = (size_t)cheng_strmeta_entries[i].len;
            found = true;
            break;
        }
    }
    cheng_strmeta_release();
    return found;
}

#if defined(CHENG_BACKEND_DRIVER_ENTRY_SHIM)
extern int32_t driver_backendBuildActiveObjNativePtrs(void* inputRaw, void* targetRaw, void* outputRaw);
extern int32_t driver_backendBuildActiveExeNativePtrs(void* inputRaw, void* targetRaw, void* outputRaw, void* linkerRaw);
#endif

#if defined(CHENG_BACKEND_DRIVER_ENTRY_SHIM)
extern int32_t driver_c_run_default(void);
WEAK void __cheng_setCmdLine(int32_t argc, const char** argv);
WEAK int32_t backendMain(void) {
    return driver_c_run_default();
}
WEAK int main(int argc, char** argv) {
    __cheng_setCmdLine((int32_t)argc, (const char**)argv);
    for (int i = 1; i < argc; ++i) {
        if (cheng_arg_is_help(argv[i])) {
            return driver_c_backend_usage();
        }
    }
    return driver_c_run_default();
}
#elif defined(CHENG_TOOLING_ENTRY_SHIM)
extern int32_t toolingMainWithCount(int32_t count);
WEAK void __cheng_setCmdLine(int32_t argc, const char** argv);
WEAK int main(int argc, char** argv) {
    int32_t count = 0;
    __cheng_setCmdLine((int32_t)argc, (const char**)argv);
    if (argc > 1 && argc <= 4096) {
        count = (int32_t)argc - 1;
    }
    return toolingMainWithCount(count);
}
#endif

/*
 * Command-line runtime bridge:
 * `std/cmdline` now reads argv from `__cheng_rt_paramCount/__cheng_rt_paramStr`.
 * Keep `paramCount/paramStr` as weak compatibility aliases for one migration cycle.
 */
static int32_t cheng_saved_argc = 0;
static const char** cheng_saved_argv = NULL;

WEAK void __cheng_setCmdLine(int32_t argc, const char** argv) {
    if (argc <= 0 || argc > 4096 || argv == NULL) {
        cheng_saved_argc = 0;
        cheng_saved_argv = NULL;
        return;
    }
    cheng_saved_argc = argc;
    cheng_saved_argv = argv;
}


WEAK int32_t __cheng_rt_paramCount(void) {
    if (cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL) {
        return cheng_saved_argc;
    }
#if defined(__APPLE__)
    int* argc_ptr = _NSGetArgc();
    if (argc_ptr == NULL) {
        return 0;
    }
    int argc = *argc_ptr;
    if (argc <= 0 || argc > 4096) {
        return 0;
    }
    return (int32_t)argc;
#else
    return 0;
#endif
}

/*
 * Compatibility shim for legacy stage0 codegen that can externalize
 * cmdline-local helper calls.
 */
WEAK int32_t __cmdCountFromRuntime(void) {
    int32_t argc = __cheng_rt_paramCount();
    if (argc <= 0 || argc > 4096) {
        return 0;
    }
    return argc;
}

WEAK int32_t cmdCountFromRuntime(void) {
    return __cmdCountFromRuntime();
}

WEAK bool cheng_cmd_ready_compat(void) {
    return __cmdCountFromRuntime() > 0;
}

WEAK char* cheng_program_name_compat(void) {
    return __cheng_rt_paramStrCopy(0);
}

WEAK char* cheng_param_str_compat(int32_t i) {
    int32_t argc = __cmdCountFromRuntime();
    if (i < 0 || argc <= 0 || i >= argc) {
        return cheng_copy_string_bytes("", 0u);
    }
    return __cheng_rt_paramStrCopy(i);
}

WEAK const char* __cheng_rt_paramStr(int32_t i) {
    if (i >= 0 && cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL && i < cheng_saved_argc) {
        const char* s = cheng_saved_argv[i];
        return s != NULL ? s : "";
    }
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

WEAK char* __cheng_rt_paramStrCopy(int32_t i) {
    const char* s = __cheng_rt_paramStr(i);
    if (s == NULL) {
        s = "";
    }
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1u);
    if (out == NULL) {
        return (char*)"";
    }
    if (n > 0) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

WEAK ChengStrBridge __cheng_rt_paramStrCopyBridge(int32_t i) {
    return cheng_str_bridge_from_owned(__cheng_rt_paramStrCopy(i));
}

WEAK void __cheng_rt_paramStrCopyBridgeInto(int32_t i, ChengStrBridge* out) {
    if (out == NULL) return;
    *out = __cheng_rt_paramStrCopyBridge(i);
}

WEAK char* __cheng_rt_programBaseNameCopy(void) {
    const char* s = __cheng_rt_paramStr(0);
    if (s == NULL) {
        s = "";
    }
    const char* base = s;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    size_t n = strlen(base);
    if (n > 3 && base[n - 3] == '.' && base[n - 2] == 's' && base[n - 1] == 'h') {
        n -= 3;
    }
    char* out = (char*)malloc(n + 1u);
    if (out == NULL) {
        return (char*)"";
    }
    if (n > 0) {
        memcpy(out, base, n);
    }
    out[n] = '\0';
    return out;
}

WEAK int32_t paramCount(void) {
    int32_t argc = __cheng_rt_paramCount();
    if (argc <= 0) {
        return 0;
    }
    return argc - 1;
}

WEAK const char* paramStr(int32_t i) {
    return __cheng_rt_paramStr(i);
}

#if !defined(_WIN32)
int32_t SystemFunction036(void* buf, int32_t len) {
    (void)buf;
    (void)len;
    return 0;
}
#endif

WEAK int32_t c_puts(char* text) {
    return puts(text != NULL ? text : "");
}

WEAK char* c_getenv(char* name) {
    if (name == NULL) {
        return NULL;
    }
    return getenv(name);
}

WEAK char* driver_c_getenv(const char* name) {
    if (name == NULL) {
        return NULL;
    }
    return getenv(name);
}

WEAK int32_t driver_c_env_bool(const char* name, int32_t defaultValue) {
    const char* raw = getenv(name != NULL ? name : "");
    if (raw == NULL || raw[0] == 0) {
        return defaultValue ? 1 : 0;
    }
    if (strcmp(raw, "1") == 0 || strcmp(raw, "true") == 0 || strcmp(raw, "TRUE") == 0 ||
        strcmp(raw, "yes") == 0 || strcmp(raw, "YES") == 0 || strcmp(raw, "on") == 0 ||
        strcmp(raw, "ON") == 0) {
        return 1;
    }
    if (strcmp(raw, "0") == 0 || strcmp(raw, "false") == 0 || strcmp(raw, "FALSE") == 0 ||
        strcmp(raw, "no") == 0 || strcmp(raw, "NO") == 0 || strcmp(raw, "off") == 0 ||
        strcmp(raw, "OFF") == 0) {
        return 0;
    }
    return defaultValue ? 1 : 0;
}

static int32_t driver_c_env_present_nonempty(const char* name) {
    const char* raw = getenv(name != NULL ? name : "");
    return (raw != NULL && raw[0] != 0) ? 1 : 0;
}

static int32_t driver_c_env_is_falsey_text(const char* raw) {
    if (raw == NULL || raw[0] == 0) {
        return 1;
    }
    if ((raw[0] == '0' && raw[1] == 0) ||
        strcmp(raw, "false") == 0 || strcmp(raw, "FALSE") == 0 ||
        strcmp(raw, "no") == 0 || strcmp(raw, "NO") == 0) {
        return 1;
    }
    return 0;
}

static int32_t driver_c_reject_removed_stage1_skip_envs(void) {
    const char* fixed_sem_raw = NULL;
    const char* fixed_ownership_raw = NULL;
    if (driver_c_env_present_nonempty("STAGE1_SKIP_SEM")) {
        fprintf(stderr, "backend_driver: removed env STAGE1_SKIP_SEM, use STAGE1_SEM_FIXED_0=0\n");
        return 2;
    }
    if (driver_c_env_present_nonempty("STAGE1_SKIP_OWNERSHIP")) {
        fprintf(stderr, "backend_driver: removed env STAGE1_SKIP_OWNERSHIP, use STAGE1_OWNERSHIP_FIXED_0=0\n");
        return 2;
    }
    fixed_sem_raw = getenv("STAGE1_SEM_FIXED_0");
    if (fixed_sem_raw != NULL && fixed_sem_raw[0] != 0 &&
        !driver_c_env_is_falsey_text(fixed_sem_raw)) {
        fprintf(stderr, "backend_driver: STAGE1_SEM_FIXED_0 is fixed=0\n");
        return 2;
    }
    fixed_ownership_raw = getenv("STAGE1_OWNERSHIP_FIXED_0");
    if (fixed_ownership_raw != NULL && fixed_ownership_raw[0] != 0 &&
        !driver_c_env_is_falsey_text(fixed_ownership_raw)) {
        fprintf(stderr, "backend_driver: STAGE1_OWNERSHIP_FIXED_0 is fixed=0\n");
        return 2;
    }
    return 0;
}

WEAK char* driver_c_getenv_copy(const char* name) {
    const char* raw = getenv(name != NULL ? name : "");
    if (raw == NULL) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) {
            out[0] = 0;
            cheng_strmeta_put(out, 0);
        }
        return out;
    }
    size_t len = strlen(raw);
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (out == NULL) return NULL;
    memcpy(out, raw, len);
    out[len] = 0;
    cheng_strmeta_put(out, (int32_t)len);
    return out;
}

WEAK ChengStrBridge driver_c_getenv_copy_bridge(const char* name) {
    return cheng_str_bridge_from_owned(driver_c_getenv_copy(name));
}

WEAK void driver_c_getenv_copy_bridge_into(const char* name, ChengStrBridge* out) {
    if (out == NULL) return;
    *out = driver_c_getenv_copy_bridge(name);
}

static char* driver_c_dup_cstr(const char* raw) {
    if (raw == NULL) {
        raw = "";
    }
    size_t len = strlen(raw);
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (out == NULL) return NULL;
    if (len > 0) {
        memcpy(out, raw, len);
    }
    out[len] = 0;
    cheng_strmeta_put(out, (int32_t)len);
    return out;
}

WEAK char* driver_c_dup_string(const char* raw) {
    return driver_c_dup_cstr(raw);
}

static int32_t driver_c_str_has_suffix_text(const char* text, const char* suffix) {
    if (text == NULL || suffix == NULL) return 0;
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (suffix_len == 0) return 1;
    if (suffix_len > text_len) return 0;
    return memcmp(text + (text_len - suffix_len), suffix, suffix_len) == 0 ? 1 : 0;
}

static char* driver_c_default_output_path_copy(const char* input_path) {
    const char* input = input_path != NULL ? input_path : "";
    size_t input_len = strlen(input);
    const char* emit = getenv("BACKEND_EMIT");
    int32_t emit_obj = (emit != NULL && strcmp(emit, "obj") == 0) ? 1 : 0;
    if (input_len == 0) {
        return driver_c_dup_cstr("");
    }
    if (driver_c_str_has_suffix_text(input, ".cheng")) {
        size_t stem_len = input_len - 6u;
        const char* suffix = emit_obj ? ".o" : "";
        size_t suffix_len = strlen(suffix);
        char* out = (char*)cheng_malloc((int32_t)(stem_len + suffix_len) + 1);
        if (out == NULL) return NULL;
        if (stem_len > 0) memcpy(out, input, stem_len);
        if (suffix_len > 0) memcpy(out + stem_len, suffix, suffix_len);
        out[stem_len + suffix_len] = 0;
        cheng_strmeta_put(out, (int32_t)(stem_len + suffix_len));
        return out;
    }
    {
        const char* suffix = emit_obj ? ".o" : ".out";
        size_t suffix_len = strlen(suffix);
        char* out = (char*)cheng_malloc((int32_t)(input_len + suffix_len) + 1);
        if (out == NULL) return NULL;
        memcpy(out, input, input_len);
        memcpy(out + input_len, suffix, suffix_len);
        out[input_len + suffix_len] = 0;
        cheng_strmeta_put(out, (int32_t)(input_len + suffix_len));
        return out;
    }
}

static char* driver_c_cli_value_copy(const char* key) {
    if (key == NULL || key[0] == '\0') {
        return driver_c_dup_cstr("");
    }
    int32_t argc = driver_c_arg_count();
    size_t key_len = strlen(key);
    if (argc <= 0 || argc > 4096) {
        return driver_c_dup_cstr("");
    }
    for (int32_t i = 1; i <= argc; ++i) {
        const char* arg = driver_c_arg_copy(i);
        if (arg == NULL || arg[0] == '\0') {
            continue;
        }
        if (strcmp(arg, key) == 0) {
            if (i + 1 <= argc) {
                return driver_c_dup_cstr(driver_c_arg_copy(i + 1));
            }
            return driver_c_dup_cstr("");
        }
        if (strncmp(arg, key, key_len) == 0 && (arg[key_len] == ':' || arg[key_len] == '=')) {
            return driver_c_dup_cstr(arg + key_len + 1);
        }
    }
    return driver_c_dup_cstr("");
}

WEAK char* driver_c_cli_input_copy(void) {
    char* value = driver_c_cli_value_copy("--input");
    if (value != NULL && value[0] != '\0') {
        return value;
    }
    int32_t argc = driver_c_arg_count();
    if (argc > 0 && argc <= 4096) {
        for (int32_t i = 1; i <= argc; ++i) {
            const char* arg = driver_c_arg_copy(i);
            if (arg == NULL || arg[0] == '\0') {
                continue;
            }
            if (arg[0] != '-') {
                return driver_c_dup_cstr(arg);
            }
        }
    }
    return value;
}

WEAK ChengStrBridge driver_c_cli_input_copy_bridge(void) {
    return cheng_str_bridge_from_owned(driver_c_cli_input_copy());
}

WEAK void driver_c_cli_input_copy_bridge_into(ChengStrBridge* out) {
    if (out == NULL) return;
    *out = driver_c_cli_input_copy_bridge();
}

WEAK char* driver_c_cli_output_copy(void) {
    return driver_c_cli_value_copy("--output");
}

WEAK ChengStrBridge driver_c_cli_output_copy_bridge(void) {
    return cheng_str_bridge_from_owned(driver_c_cli_output_copy());
}

WEAK void driver_c_cli_output_copy_bridge_into(ChengStrBridge* out) {
    if (out == NULL) return;
    *out = driver_c_cli_output_copy_bridge();
}

WEAK char* driver_c_cli_target_copy(void) {
    return driver_c_cli_value_copy("--target");
}

WEAK ChengStrBridge driver_c_cli_target_copy_bridge(void) {
    return cheng_str_bridge_from_owned(driver_c_cli_target_copy());
}

WEAK void driver_c_cli_target_copy_bridge_into(ChengStrBridge* out) {
    if (out == NULL) return;
    *out = driver_c_cli_target_copy_bridge();
}

WEAK char* driver_c_cli_linker_copy(void) {
    return driver_c_cli_value_copy("--linker");
}

WEAK ChengStrBridge driver_c_cli_linker_copy_bridge(void) {
    return cheng_str_bridge_from_owned(driver_c_cli_linker_copy());
}

WEAK void driver_c_cli_linker_copy_bridge_into(ChengStrBridge* out) {
    if (out == NULL) return;
    *out = driver_c_cli_linker_copy_bridge();
}

WEAK char* driver_c_new_string(int32_t n) {
    if (n <= 0) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) {
            out[0] = 0;
            cheng_strmeta_put(out, 0);
        }
        return out;
    }
    char* out = (char*)cheng_malloc(n + 1);
    if (out == NULL) return NULL;
    memset(out, 0, (size_t)n + 1u);
    cheng_strmeta_put(out, n);
    return out;
}

WEAK char* driver_c_new_string_copy_n(void* raw, int32_t n) {
    if (raw == NULL || n <= 0) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) {
            out[0] = 0;
            cheng_strmeta_put(out, 0);
        }
        return out;
    }
    char* out = (char*)cheng_malloc(n + 1);
    if (out == NULL) return NULL;
    memcpy(out, raw, (size_t)n);
    out[n] = 0;
    cheng_strmeta_put(out, n);
    return out;
}

WEAK char* driver_c_read_file_all(const char* path) {
    if (path == NULL || path[0] == '\0') {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    long sizeLong = ftell(f);
    if (sizeLong < 0) {
        fclose(f);
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    int32_t size = (int32_t)sizeLong;
    char* out = (char*)cheng_malloc(size + 1);
    if (out == NULL) {
        fclose(f);
        return NULL;
    }
    int32_t got = 0;
    while (got < size) {
        size_t chunk = fread(out + got, 1, (size_t)(size - got), f);
        if (chunk == 0u) break;
        got += (int32_t)chunk;
    }
    fclose(f);
    if (got != size) {
        cheng_free(out);
        char* empty = (char*)cheng_malloc(1);
        if (empty != NULL) empty[0] = 0;
        return empty;
    }
    out[size] = 0;
    cheng_strmeta_put(out, size);
    return out;
}

WEAK ChengStrBridge driver_c_read_file_all_bridge(const char* path) {
    return cheng_str_bridge_from_owned(driver_c_read_file_all(path));
}

WEAK void driver_c_read_file_all_bridge_into(const char* path, ChengStrBridge* out) {
    if (out == NULL) return;
    *out = driver_c_read_file_all_bridge(path);
}

WEAK int32_t driver_c_argv_is_help(int32_t argc, void* argv_void) {
    char** argv = (char**)argv_void;
    if (argc != 2 || argv == NULL) return 0;
    const char* arg1 = argv[1];
    if (arg1 == NULL) return 0;
    return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
}

WEAK int32_t driver_c_cli_help_requested(void) {
    if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) {
        return driver_c_argv_is_help(cheng_saved_argc, (void*)cheng_saved_argv);
    }
    if (__cheng_rt_paramCount != NULL && __cheng_rt_paramStrCopy != NULL) {
        int32_t argc = __cheng_rt_paramCount();
        if (argc != 2) return 0;
        char* arg1 = __cheng_rt_paramStrCopy(1);
        if (arg1 == NULL) return 0;
        return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
    }
    return 0;
}

WEAK int32_t driver_c_env_nonempty(const char* name) {
    const char* raw = getenv(name != NULL ? name : "");
    return (raw != NULL && raw[0] != '\0') ? 1 : 0;
}

WEAK int32_t driver_c_env_eq(const char* name, const char* expected) {
    const char* raw = getenv(name != NULL ? name : "");
    if (raw == NULL || raw[0] == '\0' || expected == NULL) {
        return 0;
    }
    return strcmp(raw, expected) == 0 ? 1 : 0;
}

static const char* driver_c_host_target_default(void) {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    return "arm64-apple-darwin";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x86_64-apple-darwin";
#elif defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
    return "aarch64-unknown-linux-gnu";
#elif defined(__linux__) && defined(__x86_64__)
    return "x86_64-unknown-linux-gnu";
#else
    return "";
#endif
}

WEAK char* driver_c_active_output_path(const char* inputPath) {
    const char* raw = getenv("BACKEND_OUTPUT");
    if (raw != NULL && raw[0] != '\0') {
        return driver_c_getenv_copy("BACKEND_OUTPUT");
    }
    return driver_c_default_output_path_copy(inputPath);
}

WEAK char* driver_c_active_output_copy(void) {
    const char* raw = getenv("BACKEND_OUTPUT");
    if (raw != NULL && raw[0] != '\0') {
        return driver_c_getenv_copy("BACKEND_OUTPUT");
    }
    return (char*)"";
}

WEAK char* driver_c_active_input_path(void) {
    const char* raw = getenv("BACKEND_INPUT");
    if (raw != NULL && raw[0] != '\0') {
        return driver_c_getenv_copy("BACKEND_INPUT");
    }
    return (char*)"";
}

WEAK char* driver_c_active_target(void) {
    const char* raw = getenv("BACKEND_TARGET");
    if (raw == NULL || raw[0] == '\0' ||
        strcmp(raw, "auto") == 0 || strcmp(raw, "native") == 0 || strcmp(raw, "host") == 0) {
        return (char*)driver_c_host_target_default();
    }
    return driver_c_getenv_copy("BACKEND_TARGET");
}

WEAK char* driver_c_active_linker(void) {
    const char* raw = getenv("BACKEND_LINKER");
    if (raw == NULL || raw[0] == '\0') {
        return (char*)"system";
    }
    return driver_c_getenv_copy("BACKEND_LINKER");
}

static int32_t driver_c_target_is_darwin_alias(const char* raw) {
    if (raw == NULL || raw[0] == '\0') return 0;
    return strcmp(raw, "arm64-apple-darwin") == 0 ||
           strcmp(raw, "aarch64-apple-darwin") == 0 ||
           strcmp(raw, "arm64-darwin") == 0 ||
           strcmp(raw, "aarch64-darwin") == 0 ||
           strcmp(raw, "darwin_arm64") == 0 ||
           strcmp(raw, "darwin_aarch64") == 0;
}

static char* driver_c_resolve_input_path(void) {
    char* cli = driver_c_cli_input_copy();
    if (cli != NULL && cli[0] != '\0') return cli;
    return driver_c_active_input_path();
}

static char* driver_c_resolve_output_path(const char* input_path) {
    char* cli = driver_c_cli_output_copy();
    if (cli != NULL && cli[0] != '\0') return cli;
    return driver_c_active_output_path(input_path);
}

static char* driver_c_resolve_target(void) {
    char* cli = driver_c_cli_target_copy();
    if (cli != NULL && cli[0] != '\0') {
        if (strcmp(cli, "auto") == 0 || strcmp(cli, "native") == 0 || strcmp(cli, "host") == 0) {
            return (char*)driver_c_host_target_default();
        }
        if (driver_c_target_is_darwin_alias(cli)) return (char*)"arm64-apple-darwin";
        return cli;
    }
    {
        char* active = driver_c_active_target();
        if (active == NULL || active[0] == '\0') return (char*)driver_c_host_target_default();
        if (strcmp(active, "auto") == 0 || strcmp(active, "native") == 0 || strcmp(active, "host") == 0) {
            return (char*)driver_c_host_target_default();
        }
        if (driver_c_target_is_darwin_alias(active)) return (char*)"arm64-apple-darwin";
        return active;
    }
}

static char* driver_c_resolve_linker(void) {
    char* cli = driver_c_cli_linker_copy();
    if (cli != NULL && cli[0] != '\0') return cli;
    return driver_c_active_linker();
}

static int32_t driver_c_emit_is_obj_mode(void) {
    const char* raw = getenv("BACKEND_EMIT");
    if (raw == NULL || raw[0] == '\0' || strcmp(raw, "exe") == 0) return 0;
    if (strcmp(raw, "obj") == 0 && driver_c_env_bool("BACKEND_INTERNAL_ALLOW_EMIT_OBJ", 0) != 0) {
        return 1;
    }
    fputs("backend_driver: invalid emit mode (expected exe)\n", stderr);
    return -50;
}

static void driver_c_require_output_file_or_die(const char* path, const char* phase) {
    const char* phase_text = (phase != NULL && phase[0] != '\0') ? phase : "<unknown>";
    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "backend_driver: missing output path after %s\n", phase_text);
        exit(1);
    }
    if (cheng_file_exists((char*)path) == 0) {
        fprintf(stderr, "backend_driver: missing output after %s: %s\n", phase_text, path);
        exit(1);
    }
    if (cheng_file_size((char*)path) <= 0) {
        fprintf(stderr, "backend_driver: empty output after %s: %s\n", phase_text, path);
        exit(1);
    }
}

WEAK int32_t driver_c_finish_emit_obj(const char* path) {
    driver_c_require_output_file_or_die(path, "emit_obj");
    driver_c_boot_marker(5);
    return 0;
}

WEAK int32_t driver_c_write_exact_file(const char* path, void* buffer, int32_t len) {
    if (path == NULL || path[0] == '\0') return -1;
    if (buffer == NULL || len <= 0) return -2;
    int32_t fd = cheng_open_w_trunc(path);
    if (fd < 0) return -3;
    int32_t wrote = cheng_fd_write(fd, (const char*)buffer, len);
    (void)libc_close(fd);
    if (wrote != len) return -4;
    if (cheng_file_exists((char*)path) == 0) return -5;
    if (cheng_file_size((char*)path) != (int64_t)len) return -6;
    return 0;
}

WEAK int32_t driver_c_emit_obj_default(void* module, const char* target, const char* path) {
    ChengDriverModuleKind moduleKind;
    void* modulePayload;
    int32_t rc = -10;
    driver_c_diagf("[driver_c_emit_obj_default] module=%p target='%s' path='%s'\n",
                   module, target != NULL ? target : "", path != NULL ? path : "");
    if (module == NULL) {
        driver_c_diagf("[driver_c_emit_obj_default] module_null\n");
        return -11;
    }
    if (path == NULL || path[0] == '\0') {
        driver_c_diagf("[driver_c_emit_obj_default] path_missing\n");
        return -12;
    }
    if (target == NULL) target = "";
    moduleKind = driver_c_module_kind(module);
    modulePayload = driver_c_module_payload(module);
    driver_c_diagf("[driver_c_emit_obj_default] module_kind=%d payload=%p\n",
                   (int32_t)moduleKind, modulePayload);
    if (modulePayload == NULL) {
        driver_c_module_dispose(module);
        return -11;
    }
    if (moduleKind == CHENG_DRIVER_MODULE_KIND_RETAINED) {
        cheng_driver_emit_obj_default_fn emitObjRetained =
            (cheng_driver_emit_obj_default_fn)dlsym(
                RTLD_DEFAULT, "driver_export_emit_obj_from_module_default_impl");
        if (emitObjRetained == NULL) {
            emitObjRetained =
                (cheng_driver_emit_obj_default_fn)dlsym(RTLD_DEFAULT, "driver_emit_obj_from_module_default_impl");
        }
        if (emitObjRetained != NULL) {
            rc = emitObjRetained(modulePayload, target, path);
            driver_c_diagf("[driver_c_emit_obj_default] retained_emit_rc=%d\n", rc);
        }
        driver_c_module_dispose(module);
        return rc;
    }
    if (moduleKind == CHENG_DRIVER_MODULE_KIND_SIDECAR) {
        cheng_driver_emit_obj_default_fn sidecarEmitFn =
            (cheng_driver_emit_obj_default_fn)cheng_sidecar_driver_emit_obj_symbol(
                &cheng_sidecar_cache, target, driver_c_diagf, driver_c_sidecar_after_open);
        if (sidecarEmitFn != NULL) {
            rc = sidecarEmitFn(modulePayload, target, path);
            driver_c_diagf("[driver_c_emit_obj_default] sidecar_rc=%d\n", rc);
        }
        driver_c_module_dispose(module);
        return rc;
    }
    {
        cheng_driver_emit_obj_default_fn emitObjRaw =
            (cheng_driver_emit_obj_default_fn)dlsym(RTLD_DEFAULT, "driver_emit_obj_from_module_default_impl");
        if (emitObjRaw != NULL) {
            rc = emitObjRaw(modulePayload, target, path);
            driver_c_diagf("[driver_c_emit_obj_default] raw_emit_rc=%d\n", rc);
            if (rc == 0) {
                driver_c_module_dispose(module);
                return rc;
            }
        }
    }
    ChengSeqHeader objBytes;
    memset(&objBytes, 0, sizeof(objBytes));
    cheng_emit_obj_from_module_fn emitObj =
        (cheng_emit_obj_from_module_fn)dlsym(RTLD_DEFAULT, "uirEmitObjFromModuleOrPanic");
    if (emitObj == NULL) {
        driver_c_diagf("[driver_c_emit_obj_default] no_emit_symbol\n");
        driver_c_module_dispose(module);
        return -10;
    }
    emitObj(&objBytes, modulePayload, driver_c_env_i32("BACKEND_OPT_LEVEL", 0), target, "", 0, 0, 0, "autovec");
    driver_c_diagf("[driver_c_emit_obj_default] weak_uir_emit len=%d buffer=%p\n",
                   objBytes.len, objBytes.buffer);
    if (objBytes.len <= 0) {
        driver_c_module_dispose(module);
        return -13;
    }
    if (objBytes.buffer == NULL) {
        driver_c_module_dispose(module);
        return -14;
    }
    {
        int32_t rcWrite = driver_c_write_exact_file(path, objBytes.buffer, objBytes.len);
        driver_c_diagf("[driver_c_emit_obj_default] write_rc=%d\n", rcWrite);
        driver_c_module_dispose(module);
        return rcWrite;
    }
}

WEAK int32_t driver_c_link_tmp_obj_default(const char* outputPath, const char* objPath,
                                           const char* target, const char* linker) {
    const char* targetText = (target != NULL) ? target : "";
    const char* linkerText = (linker != NULL && linker[0] != '\0') ? linker : "system";
    const char* runtimeC = "/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c";
    const char* cflags = getenv("BACKEND_CFLAGS");
    const char* ldflags = getenv("BACKEND_LDFLAGS");
    const char* cflagsText = (cflags != NULL) ? cflags : "";
    const char* ldflagsText = (ldflags != NULL) ? ldflags : "";
    char fullLdflags[512];
    char extraLdflags[96];
    fullLdflags[0] = '\0';
    extraLdflags[0] = '\0';
    if (outputPath == NULL || outputPath[0] == '\0') return -20;
    if (objPath == NULL || objPath[0] == '\0') return -21;
    if (strcmp(linkerText, "system") != 0) return -22;
    const char* base = strrchr(objPath, '/');
    const char* objName = (base != NULL) ? (base + 1) : objPath;
    int useDriverEntryShim = strstr(objName, "backend_driver") != NULL ||
                             driver_c_env_bool("BACKEND_FORCE_DRIVER_ENTRY_SHIM", 0) != 0;
    int needsDarwinCodesign = 0;
    if (strstr(targetText, "darwin") != NULL || strstr(targetText, "apple-darwin") != NULL) {
        needsDarwinCodesign = 1;
        if (strstr(ldflagsText, "-Wl,-no_adhoc_codesign") == NULL) {
            snprintf(extraLdflags + strlen(extraLdflags),
                     sizeof(extraLdflags) - strlen(extraLdflags),
                     " -Wl,-no_adhoc_codesign");
        }
        if (strstr(ldflagsText, "-Wl,-export_dynamic") == NULL) {
            snprintf(extraLdflags + strlen(extraLdflags),
                     sizeof(extraLdflags) - strlen(extraLdflags),
                     " -Wl,-export_dynamic");
        }
    }
    snprintf(fullLdflags, sizeof(fullLdflags), "%s%s", ldflagsText, extraLdflags);
    size_t need = strlen(objPath) + strlen(runtimeC) + strlen(outputPath) +
                  strlen(cflagsText) + strlen(ldflagsText) + strlen(extraLdflags) + 128u;
    char* cmd = (char*)malloc(need);
    if (cmd == NULL) return -23;
    if (useDriverEntryShim) {
        snprintf(cmd, need, "cc -DCHENG_BACKEND_DRIVER_ENTRY_SHIM %s '%s' '%s' %s -o '%s'",
                 cflagsText, objPath, runtimeC, fullLdflags, outputPath);
    } else {
        snprintf(cmd, need, "cc %s '%s' '%s' %s -o '%s'",
                 cflagsText, objPath, runtimeC, fullLdflags, outputPath);
    }
    if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
        fprintf(stderr, "[driver_c_link_tmp_obj_default] obj=%s out=%s target=%s sidecar=<bundle-only>\n",
                objPath, outputPath, targetText);
        fprintf(stderr, "[driver_c_link_tmp_obj_default] cmd=%s\n", cmd);
    }
    int rc = system(cmd);
    if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
        fprintf(stderr, "[driver_c_link_tmp_obj_default] system_rc=%d\n", rc);
    }
    free(cmd);
    if (rc != 0) return -24;
    if (cheng_file_exists((char*)outputPath) == 0) {
        if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
            fprintf(stderr, "[driver_c_link_tmp_obj_default] missing_output=%s\n", outputPath);
        }
        return -25;
    }
    if (cheng_file_size((char*)outputPath) <= 0) {
        if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
            fprintf(stderr, "[driver_c_link_tmp_obj_default] empty_output=%s\n", outputPath);
        }
        return -26;
    }
    if (needsDarwinCodesign) {
        size_t signNeed = strlen(outputPath) * 2u + 128u;
        char* signCmd = (char*)malloc(signNeed);
        if (signCmd == NULL) return -27;
        snprintf(signCmd, signNeed,
                 "codesign --force --sign - '%s' >/dev/null 2>&1 && "
                 "codesign --verify --verbose=2 '%s' >/dev/null 2>&1",
                 outputPath, outputPath);
        rc = system(signCmd);
        if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
            fprintf(stderr, "[driver_c_link_tmp_obj_default] codesign_rc=%d\n", rc);
        }
        free(signCmd);
        if (rc != 0) return -28;
    }
    return 0;
}

WEAK int32_t driver_c_link_tmp_obj_system(const char* outputPath, const char* objPath,
                                          const char* target) {
    return driver_c_link_tmp_obj_default(outputPath, objPath, target, "system");
}

WEAK int32_t driver_c_build_emit_obj_default(const char* inputPath, const char* target,
                                             const char* outputPath) {
    if (inputPath == NULL || inputPath[0] == '\0') return -30;
    if (target == NULL || target[0] == '\0') return -31;
    if (outputPath == NULL || outputPath[0] == '\0') return -32;
    cheng_build_emit_obj_stage1_target_fn buildEmitObjDirect =
        (cheng_build_emit_obj_stage1_target_fn)dlsym(
            RTLD_DEFAULT, "driver_export_build_emit_obj_from_file_stage1_target_impl");
    if (buildEmitObjDirect != NULL) {
        int32_t directRc = buildEmitObjDirect(inputPath, target, outputPath);
        driver_c_diagf("[driver_c_build_emit_obj_default] direct_build_emit_rc=%d\n", directRc);
        if (directRc != 0) return directRc;
        return driver_c_finish_emit_obj(outputPath);
    }
    void* module = driver_c_build_module_stage1(inputPath, target);
    if (module == NULL) return -33;
    int32_t emitRc = driver_c_emit_obj_default(module, target, outputPath);
    if (emitRc != 0) return emitRc;
    return driver_c_finish_emit_obj(outputPath);
}

WEAK int32_t driver_c_build_active_obj_default(void) {
    driver_c_boot_marker(34);
    char* inputPath = driver_c_active_input_path();
    if (inputPath == NULL || inputPath[0] == '\0') return 2;
    char* target = driver_c_active_target();
    char* outputPath = driver_c_active_output_path(inputPath);
    driver_c_boot_marker(35);
    int32_t emitRc = driver_c_build_emit_obj_default(inputPath, target, outputPath);
    if (emitRc != 0) return emitRc;
    driver_c_boot_marker(36);
    return 0;
}

WEAK int32_t driver_c_build_link_exe_default(const char* inputPath, const char* target,
                                             const char* outputPath, const char* linker) {
    (void)linker;
    if (inputPath == NULL || inputPath[0] == '\0') return -40;
    if (target == NULL || target[0] == '\0') return -41;
    if (outputPath == NULL || outputPath[0] == '\0') return -42;
    const char* suffix = ".tmp.linkobj";
    size_t need = strlen(outputPath) + strlen(suffix) + 1u;
    char* objPath = (char*)malloc(need);
    if (objPath == NULL) return -43;
    snprintf(objPath, need, "%s%s", outputPath, suffix);
    int32_t emitRc = driver_c_build_emit_obj_default(inputPath, target, objPath);
    if (emitRc != 0) {
        free(objPath);
        return emitRc;
    }
    int32_t linkRc = driver_c_link_tmp_obj_system(outputPath, objPath, target);
    if (linkRc != 0) {
        free(objPath);
        return linkRc;
    }
    int32_t finishRc = driver_c_finish_single_link(outputPath, objPath);
    free(objPath);
    return finishRc;
}

WEAK void* driver_c_build_module_stage1(const char* inputPath, const char* target) {
    driver_c_diagf("[driver_c_build_module_stage1] input='%s' target='%s'\n",
                   inputPath != NULL ? inputPath : "", target != NULL ? target : "");
    if (inputPath == NULL || inputPath[0] == '\0') {
        driver_c_diagf("[driver_c_build_module_stage1] input_missing\n");
        return NULL;
    }
    if (target == NULL) target = "";
    if (driver_c_env_bool("BACKEND_UIR_SIDECAR_DISABLE", 0) != 0) {
        void* directModule = driver_c_build_module_stage1_direct(inputPath, target);
        driver_c_diagf("[driver_c_build_module_stage1] sidecar_disabled direct_module=%p\n",
                       directModule);
        if (directModule != NULL) return driver_c_module_wrap(directModule, CHENG_DRIVER_MODULE_KIND_RAW);
        driver_c_diagf("[driver_c_build_module_stage1] sidecar_disabled direct_failed\n");
        return NULL;
    }
    {
        cheng_build_module_stage1_fn buildModuleStage1TargetRetained =
            (cheng_build_module_stage1_fn)dlsym(
                RTLD_DEFAULT, "driver_export_buildModuleFromFileStage1TargetRetained");
        if (buildModuleStage1TargetRetained != NULL) {
            void* module = buildModuleStage1TargetRetained(inputPath, target);
            driver_c_diagf("[driver_c_build_module_stage1] dlsym_retained_target_module=%p\n", module);
            if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RETAINED);
        }
    }
    cheng_build_module_stage1_fn buildModuleStage1 =
        (cheng_build_module_stage1_fn)dlsym(RTLD_DEFAULT, "uirCoreBuildModuleFromFileStage1OrPanic");
    if (buildModuleStage1 != NULL) {
        void* module = buildModuleStage1(inputPath, target);
        driver_c_diagf("[driver_c_build_module_stage1] dlsym_builder_module=%p\n", module);
        if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
    }
    cheng_build_active_module_ptrs_fn buildActiveModulePtrsFn =
        (cheng_build_active_module_ptrs_fn)dlsym(RTLD_DEFAULT, "driver_buildActiveModulePtrs");
    if (buildActiveModulePtrsFn != NULL) {
        void* module = buildActiveModulePtrsFn((void*)inputPath, (void*)target);
        driver_c_diagf("[driver_c_build_module_stage1] dlsym_sidecar_module=%p\n", module);
        if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
    }
    buildActiveModulePtrsFn =
        (cheng_build_active_module_ptrs_fn)cheng_sidecar_build_module_symbol(
            &cheng_sidecar_cache, target, driver_c_diagf, driver_c_sidecar_after_open);
    if (buildActiveModulePtrsFn != NULL) {
        void* module = buildActiveModulePtrsFn((void*)inputPath, (void*)target);
        driver_c_diagf("[driver_c_build_module_stage1] sidecar_module=%p\n", module);
        if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_SIDECAR);
    }
    driver_c_diagf("[driver_c_build_module_stage1] no_module\n");
    return NULL;
}

WEAK void* driver_c_build_module_stage1_direct(const char* inputPath, const char* target) {
    driver_c_diagf("[driver_c_build_module_stage1_direct] input='%s' target='%s'\n",
                   inputPath != NULL ? inputPath : "", target != NULL ? target : "");
    if (inputPath == NULL || inputPath[0] == '\0') {
        driver_c_diagf("[driver_c_build_module_stage1_direct] input_missing\n");
        return NULL;
    }
    if (target == NULL) target = "";
    {
        cheng_build_module_stage1_fn buildModuleStage1TargetRetained =
            (cheng_build_module_stage1_fn)dlsym(
                RTLD_DEFAULT, "driver_export_buildModuleFromFileStage1TargetRetained");
        if (buildModuleStage1TargetRetained != NULL) {
            void* module = buildModuleStage1TargetRetained(inputPath, target);
            driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_retained_target_module=%p\n",
                           module);
            if (module != NULL) return module;
        }
    }
    cheng_build_module_stage1_fn buildModuleStage1 =
        (cheng_build_module_stage1_fn)dlsym(RTLD_DEFAULT, "uirCoreBuildModuleFromFileStage1OrPanic");
    if (buildModuleStage1 != NULL) {
        void* module = buildModuleStage1(inputPath, target);
        driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_builder_module=%p\n", module);
        if (module != NULL) return module;
    }
    if (driver_c_env_bool("BACKEND_UIR_SIDECAR_DISABLE", 0) != 0) {
        driver_c_diagf("[driver_c_build_module_stage1_direct] sidecar_disabled skip_sidecar\n");
        return NULL;
    }
    cheng_build_active_module_ptrs_fn buildActiveModulePtrsFn =
        (cheng_build_active_module_ptrs_fn)dlsym(RTLD_DEFAULT, "driver_buildActiveModulePtrs");
    if (buildActiveModulePtrsFn != NULL) {
        void* module = buildActiveModulePtrsFn((void*)inputPath, (void*)target);
        driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_sidecar_module=%p\n", module);
        if (module != NULL) return module;
    }
    buildActiveModulePtrsFn =
        (cheng_build_active_module_ptrs_fn)cheng_sidecar_build_module_symbol(
            &cheng_sidecar_cache, target, driver_c_diagf, driver_c_sidecar_after_open);
    if (buildActiveModulePtrsFn != NULL) {
        void* module = buildActiveModulePtrsFn((void*)inputPath, (void*)target);
        driver_c_diagf("[driver_c_build_module_stage1_direct] sidecar_module=%p\n", module);
        if (module != NULL) return module;
    }
    driver_c_diagf("[driver_c_build_module_stage1_direct] no_module\n");
    return NULL;
}

WEAK int32_t driver_c_build_active_exe_default(void) {
    driver_c_boot_marker(37);
    char* inputPath = driver_c_active_input_path();
    if (inputPath == NULL || inputPath[0] == '\0') return 2;
    char* target = driver_c_active_target();
    char* outputPath = driver_c_active_output_path(inputPath);
    char* linker = driver_c_active_linker();
    driver_c_boot_marker(38);
    return driver_c_build_link_exe_default(inputPath, target, outputPath, linker);
}

WEAK int32_t driver_c_run_default(void) {
    int32_t env_gate_rc = driver_c_reject_removed_stage1_skip_envs();
    if (env_gate_rc != 0) return env_gate_rc;
    int32_t emit_mode = driver_c_emit_is_obj_mode();
    if (emit_mode < 0) return emit_mode;
    char* input_path = driver_c_resolve_input_path();
    char* target = driver_c_resolve_target();
    char* output_path = driver_c_resolve_output_path(input_path);
    char* linker = driver_c_resolve_linker();
    driver_c_diagf("[driver_c_run_default] emit=%d input='%s' target='%s' output='%s' linker='%s'\n",
                   emit_mode,
                   input_path != NULL ? input_path : "",
                   target != NULL ? target : "",
                   output_path != NULL ? output_path : "",
                   linker != NULL ? linker : "");
    if (input_path == NULL || input_path[0] == '\0') return 2;
    if (target == NULL || target[0] == '\0') return 2;
    if (output_path == NULL || output_path[0] == '\0') return 2;
    if (emit_mode != 0) {
        int32_t rc = driver_c_build_emit_obj_default(input_path, target, output_path);
        driver_c_diagf("[driver_c_run_default] emit_obj_rc=%d\n", rc);
        return rc;
    }
    {
        int32_t rc = driver_c_build_link_exe_default(input_path, target, output_path, linker);
        driver_c_diagf("[driver_c_run_default] link_exe_rc=%d\n", rc);
        return rc;
    }
}

WEAK int32_t driver_c_finish_single_link(const char* path, const char* objPath) {
    driver_c_require_output_file_or_die(path, "single_link");
    if (driver_c_env_bool("BACKEND_KEEP_TMP_LINKOBJ", 0) == 0 &&
        objPath != NULL && objPath[0] != '\0') {
        unlink(objPath);
    }
    driver_c_boot_marker(8);
    return 0;
}

static int32_t driver_c_boot_marker_enabled(void) {
    const char* raw = getenv("BACKEND_DEBUG_BOOT");
    if (raw == NULL || raw[0] == '\0') {
        return 0;
    }
    if (raw[0] == '0' && raw[1] == '\0') {
        return 0;
    }
    return 1;
}

static void driver_c_boot_marker_write(const char* text, size_t len) {
    if (!driver_c_boot_marker_enabled()) {
        return;
    }
    if (text == NULL || len == 0) {
        return;
    }
    (void)write(2, text, len);
}

WEAK void driver_c_boot_marker(int32_t code) {
    switch (code) {
        case 1: driver_c_boot_marker_write("[boot]01\n", sizeof("[boot]01\n") - 1u); break;
        case 2: driver_c_boot_marker_write("[boot]02\n", sizeof("[boot]02\n") - 1u); break;
        case 3: driver_c_boot_marker_write("[boot]03\n", sizeof("[boot]03\n") - 1u); break;
        case 4: driver_c_boot_marker_write("[boot]04\n", sizeof("[boot]04\n") - 1u); break;
        case 5: driver_c_boot_marker_write("[boot]05\n", sizeof("[boot]05\n") - 1u); break;
        case 6: driver_c_boot_marker_write("[boot]06\n", sizeof("[boot]06\n") - 1u); break;
        case 7: driver_c_boot_marker_write("[boot]07\n", sizeof("[boot]07\n") - 1u); break;
        case 8: driver_c_boot_marker_write("[boot]08\n", sizeof("[boot]08\n") - 1u); break;
        case 9: driver_c_boot_marker_write("[boot]09\n", sizeof("[boot]09\n") - 1u); break;
        case 10: driver_c_boot_marker_write("[boot]10\n", sizeof("[boot]10\n") - 1u); break;
        case 11: driver_c_boot_marker_write("[boot]11\n", sizeof("[boot]11\n") - 1u); break;
        case 20: driver_c_boot_marker_write("[boot]20\n", sizeof("[boot]20\n") - 1u); break;
        case 21: driver_c_boot_marker_write("[boot]21\n", sizeof("[boot]21\n") - 1u); break;
        case 30: driver_c_boot_marker_write("[boot]30\n", sizeof("[boot]30\n") - 1u); break;
        case 31: driver_c_boot_marker_write("[boot]31\n", sizeof("[boot]31\n") - 1u); break;
        case 32: driver_c_boot_marker_write("[boot]32\n", sizeof("[boot]32\n") - 1u); break;
        case 33: driver_c_boot_marker_write("[boot]33\n", sizeof("[boot]33\n") - 1u); break;
        case 34: driver_c_boot_marker_write("[boot]34\n", sizeof("[boot]34\n") - 1u); break;
        case 35: driver_c_boot_marker_write("[boot]35\n", sizeof("[boot]35\n") - 1u); break;
        case 36: driver_c_boot_marker_write("[boot]36\n", sizeof("[boot]36\n") - 1u); break;
        case 37: driver_c_boot_marker_write("[boot]37\n", sizeof("[boot]37\n") - 1u); break;
        case 38: driver_c_boot_marker_write("[boot]38\n", sizeof("[boot]38\n") - 1u); break;
        default: break;
    }
}

WEAK void driver_c_capture_cmdline(int32_t argc, void* argv_void) {
    cheng_saved_argc = argc;
    cheng_saved_argv = (const char**)argv_void;
}

WEAK int32_t driver_c_capture_cmdline_keep(int32_t argc, void* argv_void) {
    driver_c_capture_cmdline(argc, argv_void);
    return argc;
}

WEAK int32_t driver_c_arg_count(void) {
    if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) return cheng_saved_argc - 1;
    int32_t argc = __cheng_rt_paramCount();
    if (argc > 1 && argc <= 257) return argc - 1;
    return 0;
}

WEAK char* driver_c_arg_copy(int32_t i) {
    if (cheng_saved_argv == NULL && i > 0) {
        char* rt = __cheng_rt_paramStrCopy(i);
        if (rt != NULL) return rt;
    }
    if (cheng_saved_argv == NULL || i <= 0 || i >= cheng_saved_argc) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) {
            out[0] = 0;
            cheng_strmeta_put(out, 0);
        }
        return out;
    }
    const char* raw = cheng_saved_argv[i];
    if (raw == NULL) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) {
            out[0] = 0;
            cheng_strmeta_put(out, 0);
        }
        return out;
    }
    size_t len = strlen(raw);
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (out == NULL) return NULL;
    memcpy(out, raw, len);
    out[len] = 0;
    cheng_strmeta_put(out, (int32_t)len);
    return out;
}

WEAK int32_t driver_c_help_requested(void) {
    if (cheng_saved_argv != NULL && cheng_saved_argc > 1 && cheng_saved_argc <= 4096) {
        for (int32_t i = 1; i < cheng_saved_argc; ++i) {
            const char* arg = cheng_saved_argv[i];
            if (arg == NULL) continue;
            if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
        }
        return 0;
    }
    int32_t argc = __cheng_rt_paramCount();
    if (argc > 1 && argc <= 4096) {
        for (int32_t i = 1; i < argc; ++i) {
            char* arg = __cheng_rt_paramStrCopy(i);
            if (arg == NULL) continue;
            if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
        }
    }
    return 0;
}

WEAK int32_t c_strcmp(char* a, char* b) {
    return strcmp(a != NULL ? a : "", b != NULL ? b : "");
}

static bool cheng_safe_cstr_view(const char* s, const char** out_ptr, size_t* out_len);

WEAK int32_t __cheng_str_eq(const char* a, const char* b) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (a == b) {
        return 1;
    }
    if (!cheng_safe_cstr_view(a, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_cstr_view(b, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    if (la != lb) {
        return 0;
    }
    if (la == 0) {
        return 1;
    }
    return memcmp(sa, sb, la) == 0 ? 1 : 0;
}

WEAK int32_t c_strlen(char* s) {
    return cheng_strlen(s);
}

WEAK bool cheng_str_is_empty(const char* s) {
    return cheng_strlen((char*)s) == 0;
}

WEAK bool cheng_str_nonempty(const char* s) {
    return cheng_strlen((char*)s) != 0;
}

WEAK bool cheng_str_has_prefix_bool(const char* s, const char* prefix) {
    return driver_c_str_has_prefix(s, prefix) != 0;
}

WEAK bool cheng_str_contains_char_bool(const char* s, int32_t value) {
    return driver_c_str_contains_char(s, value) != 0;
}

WEAK bool cheng_str_contains_str_bool(const char* s, const char* sub) {
    return driver_c_str_contains_str(s, sub) != 0;
}

WEAK char* cheng_str_drop_prefix(const char* s, const char* prefix) {
    if (!cheng_str_has_prefix_bool(s, prefix)) {
        return driver_c_str_clone(s);
    }
    return driver_c_str_slice(s, cheng_strlen((char*)prefix), cheng_strlen((char*)s));
}

WEAK char* cheng_str_slice_bytes(const char* text, int32_t start, int32_t count) {
    return driver_c_str_slice(text, start, count);
}

WEAK void cheng_str_arc_retain(const char* s) {
    if (s != NULL) {
        cheng_mem_retain((void*)s);
    }
}

WEAK void cheng_str_arc_release(const char* s) {
    if (s != NULL) {
        cheng_mem_release((void*)s);
    }
}

WEAK char* cheng_str_from_bytes_compat(ChengSeqHeader data, int32_t len) {
    const char* raw = (const char*)data.buffer;
    if (raw == NULL || len <= 0 || data.len < len) {
        return cheng_copy_string_bytes("", 0u);
    }
    return cheng_copy_string_bytes(raw, (size_t)len);
}

WEAK char* cheng_char_to_str_compat(int32_t value) {
    return driver_c_char_to_str(value);
}

WEAK char* cheng_slice_string_compat(const char* s, int32_t start, int32_t stop, bool exclusive) {
    int32_t s0 = start < 0 ? 0 : start;
    int32_t e0 = exclusive ? (stop - 1) : stop;
    int32_t n;
    int32_t e;
    int32_t count;
    if (e0 < s0) {
        return cheng_copy_string_bytes("", 0u);
    }
    n = cheng_strlen((char*)s);
    if (s0 >= n) {
        return cheng_copy_string_bytes("", 0u);
    }
    e = e0 >= n ? (n - 1) : e0;
    count = e - s0 + 1;
    return driver_c_str_slice(s, s0, count);
}

WEAK bool cheng_str_contains_compat(const char* text, const char* sub) {
    return driver_c_str_contains_str(text, sub) != 0;
}

WEAK void c_iometer_call(void* hook, int32_t op, int64_t bytes) {
    (void)hook;
    (void)op;
    (void)bytes;
}

WEAK int32_t libc_remove(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    return remove(path);
}

WEAK int32_t libc_open(const char* path, int32_t flags, int32_t mode) {
    if (path == NULL) {
        return -1;
    }
    return open(path, flags, mode);
}

WEAK int32_t cheng_open_w_trunc(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    return creat(path, 0644);
}

WEAK int32_t libc_close(int32_t fd) {
    if (fd < 0) {
        return -1;
    }
    return close(fd);
}

WEAK int64_t libc_write(int32_t fd, void* data, int64_t n) {
    if (fd < 0 || data == NULL || n <= 0) {
        return 0;
    }
    ssize_t wrote = write(fd, data, (size_t)n);
    if (wrote < 0) {
        return -1;
    }
    return (int64_t)wrote;
}

WEAK void zeroMem(void* p, int64_t n) {
    if (p == NULL || n <= 0) {
        return;
    }
    memset(p, 0, (size_t)n);
}

WEAK void* openImpl(char* filename, int32_t mode) {
    const char* path = filename != NULL ? filename : "";
    const char* openMode = "rb";
    if (mode == 1) {
        openMode = "wb";
    } else if (mode != 0) {
        openMode = "rb+";
    }
    return fopen(path, openMode);
}

WEAK uint64_t processOptionMask(int32_t opt) {
    if (opt < 0 || opt >= 63) {
        return 0;
    }
    return ((uint64_t)1) << (uint64_t)opt;
}

WEAK char* sliceStr(char* text, int32_t start, int32_t stop) {
    if (text == NULL) {
        return (char*)"";
    }
    int32_t n = cheng_strlen(text);
    if (n <= 0) {
        return (char*)"";
    }
    int32_t a = start < 0 ? 0 : start;
    int32_t b = stop;
    if (b >= n) {
        b = n - 1;
    }
    if (b < a) {
        return (char*)"";
    }
    int32_t span = b - a + 1;
    char* out = (char*)malloc((size_t)span + 1u);
    if (out == NULL) {
        return (char*)"";
    }
    memcpy(out, text + a, (size_t)span);
    out[span] = '\0';
    return out;
}

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

static int cheng_mem_block_registered(ChengMemBlock* block) {
    if (block == NULL) {
        return 0;
    }
    if (!cheng_mm_ptrmap_check()) {
        return 1;
    }
    void* payload = (void*)(block + 1);
    return cheng_ptrmap_get(payload) == block;
}

static ChengMemBlock* cheng_mem_head_sanitize(ChengMemScope* scope) {
    if (scope == NULL) {
        return NULL;
    }
    ChengMemBlock* head = scope->head;
    if (head == NULL) {
        return NULL;
    }
    if (!cheng_mem_block_registered(head)) {
        scope->head = NULL;
        return NULL;
    }
    if (head->scope != scope) {
        scope->head = NULL;
        return NULL;
    }
    return head;
}

static ChengMemBlock* cheng_mem_find_block(ChengMemScope* scope, void* p) {
    if (scope == NULL || p == NULL) {
        return NULL;
    }
    ChengMemBlock* cur = cheng_mem_head_sanitize(scope);
    while (cur) {
        if ((void*)(cur + 1) == p) {
            return cur;
        }
        ChengMemBlock* next = cur->next;
        if (next != NULL && !cheng_mem_block_registered(next)) {
            cur->next = NULL;
            break;
        }
        cur = next;
    }
    return NULL;
}

static void cheng_mem_link(ChengMemScope* scope, ChengMemBlock* block) {
    if (scope == NULL) {
        scope = &cheng_global_scope;
    }
    block->scope = scope;
    block->prev = NULL;
    ChengMemBlock* head = cheng_mem_head_sanitize(scope);
    block->next = head;
    if (head) {
        head->prev = block;
    }
    scope->head = block;
}

static void cheng_mem_unlink(ChengMemBlock* block) {
    if (block == NULL || block->scope == NULL) {
        return;
    }
    ChengMemScope* scope = block->scope;
    ChengMemBlock* head = cheng_mem_head_sanitize(scope);
    if (block->prev && cheng_mem_block_registered(block->prev) && block->prev->next == block) {
        block->prev->next = block->next;
    } else if (head == block) {
        scope->head = block->next;
    }
    if (block->next && cheng_mem_block_registered(block->next) && block->next->prev == block) {
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
void* rawmemAsVoid(void* p) {
    return p;
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
int32_t elemSize_ptr(void) {
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
        const char* trace = getenv("BOUNDS_TRACE");
        if (trace != NULL && trace[0] != '\0' && trace[0] != '0') {
            void* caller = __builtin_return_address(0);
            Dl_info info;
            if (dladdr(caller, &info) != 0) {
                uintptr_t pc = (uintptr_t)caller;
                uintptr_t base = (uintptr_t)info.dli_fbase;
                uintptr_t off = pc >= base ? (pc - base) : 0;
                fprintf(
                    stderr,
                    "[cheng] bounds caller=%p module=%s symbol=%s offset=0x%zx\n",
                    caller,
                    info.dli_fname ? info.dli_fname : "?",
                    info.dli_sname ? info.dli_sname : "?",
                    (size_t)off
                );
            } else {
                fprintf(stderr, "[cheng] bounds caller=%p module=? symbol=? offset=0x0\n", caller);
            }
#if CHENG_HAS_EXECINFO
            void* frames[24];
            int count = backtrace(frames, 24);
            if (count > 0) {
                backtrace_symbols_fd(frames, count, 2);
            }
#endif
        }
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

void* cheng_ptr_seq_get_value(ChengSeqHeader seq, int32_t idx) {
    void* slot = cheng_seq_get(seq.buffer, seq.len, idx, (int32_t)sizeof(void*));
    void* out = NULL;
    if (!slot) return NULL;
    memcpy(&out, slot, sizeof(void*));
    return out;
}

void* cheng_ptr_seq_get_compat(ChengSeqHeader seq, int32_t idx) {
    return cheng_ptr_seq_get_value(seq, idx);
}

void cheng_ptr_seq_set_compat(ChengSeqHeader* seq_ptr, int32_t idx, void* value) {
    if (!seq_ptr) {
        return;
    }
    void* slot = cheng_seq_set(seq_ptr->buffer, seq_ptr->len, idx, (int32_t)sizeof(void*));
    if (!slot) {
        return;
    }
    memcpy(slot, &value, sizeof(void*));
}

void cheng_error_info_init_compat(ChengErrorInfoCompat* out, int32_t code, char* msg) {
    if (!out) {
        return;
    }
    out->code = code;
    out->msg = msg;
}

int32_t cheng_error_info_code_compat(ChengErrorInfoCompat e) {
    return e.code;
}

char* cheng_error_info_msg_compat(ChengErrorInfoCompat e) {
    return e.msg;
}

#define CHENG_FUNC_PTR_SHADOW_CAP 4096u
static uintptr_t cheng_func_ptr_shadow_keys[CHENG_FUNC_PTR_SHADOW_CAP];
static uintptr_t cheng_func_ptr_shadow_vals[CHENG_FUNC_PTR_SHADOW_CAP];

static uint32_t cheng_func_ptr_shadow_slot(uintptr_t key) {
    return (uint32_t)(key & (CHENG_FUNC_PTR_SHADOW_CAP - 1u));
}

typedef struct ChengMachineInst {
    uint32_t magic;
    int32_t op;
    int32_t rd;
    int32_t rn;
    int32_t rm;
    int32_t ra;
    int64_t imm;
    char* label;
    int32_t cond;
} ChengMachineInst;

enum {
    CHENG_MACHINE_INST_MAGIC = 0x434d494eu,
};

static char* cheng_machine_inst_dup_label(const char* s) {
    size_t len = 0u;
    char* out = NULL;
    if (s == NULL || s[0] == '\0') {
        out = (char*)cheng_malloc(1);
        if (out != NULL) {
            out[0] = '\0';
            cheng_strmeta_put(out, 0);
        }
        return out;
    }
    len = strlen(s);
    out = (char*)cheng_malloc((int32_t)len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    cheng_strmeta_put(out, (int32_t)len);
    return out;
}

static char* cheng_machine_inst_dup_label_n(const char* s, int32_t len_in) {
    char* out = NULL;
    int32_t len = len_in > 0 ? len_in : 0;
    if (s == NULL || len <= 0) {
        out = (char*)cheng_malloc(1);
        if (out != NULL) {
            out[0] = '\0';
            cheng_strmeta_put(out, 0);
        }
        return out;
    }
    out = (char*)cheng_malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, (size_t)len);
    out[len] = '\0';
    cheng_strmeta_put(out, len);
    return out;
}

static ChengMachineInst* cheng_machine_inst_try(void* inst) {
    ChengMachineInst* raw = (ChengMachineInst*)inst;
    if (raw == NULL) {
        return NULL;
    }
    if (raw->magic != CHENG_MACHINE_INST_MAGIC) {
        return NULL;
    }
    return raw;
}

void cheng_func_ptr_shadow_remember(void* p) {
    uintptr_t ptr_val = (uintptr_t)p;
    if (ptr_val == 0) {
        return;
    }
    uintptr_t low_key = ptr_val & UINT32_C(0xffffffff);
    if (low_key == 0) {
        return;
    }
    uint32_t slot = cheng_func_ptr_shadow_slot(low_key);
    for (uint32_t scanned = 0; scanned < CHENG_FUNC_PTR_SHADOW_CAP; ++scanned) {
        uintptr_t cur_key = cheng_func_ptr_shadow_keys[slot];
        if (cur_key == 0 || cur_key == low_key) {
            cheng_func_ptr_shadow_keys[slot] = low_key;
            cheng_func_ptr_shadow_vals[slot] = ptr_val;
            return;
        }
        slot = (slot + 1u) & (CHENG_FUNC_PTR_SHADOW_CAP - 1u);
    }
}

void* cheng_func_ptr_shadow_recover(uint64_t p) {
    uintptr_t ptr_val = (uintptr_t)p;
    if (ptr_val == 0) {
        return NULL;
    }
    if ((p >> 32) != 0) {
        return (void*)ptr_val;
    }
    uintptr_t low_key = ptr_val & UINT32_C(0xffffffff);
    if (low_key == 0) {
        return NULL;
    }
    uint32_t slot = cheng_func_ptr_shadow_slot(low_key);
    void* found = NULL;
    for (uint32_t scanned = 0; scanned < CHENG_FUNC_PTR_SHADOW_CAP; ++scanned) {
        uintptr_t cur_key = cheng_func_ptr_shadow_keys[slot];
        if (cur_key == 0) {
            return found;
        }
        if (cur_key == low_key) {
            uintptr_t cur_val = cheng_func_ptr_shadow_vals[slot];
            if (cur_val != 0) {
                found = (void*)cur_val;
            }
        }
        slot = (slot + 1u) & (CHENG_FUNC_PTR_SHADOW_CAP - 1u);
    }
    return found;
}

void* cheng_machine_inst_new(int32_t op, int32_t rd, int32_t rn, int32_t rm,
                             int32_t ra, int64_t imm, const char* label, int32_t cond) {
    ChengMachineInst* inst = (ChengMachineInst*)cheng_malloc((int32_t)sizeof(ChengMachineInst));
    if (inst == NULL) {
        return NULL;
    }
    memset(inst, 0, sizeof(ChengMachineInst));
    inst->magic = CHENG_MACHINE_INST_MAGIC;
    inst->op = op;
    inst->rd = rd;
    inst->rn = rn;
    inst->rm = rm;
    inst->ra = ra;
    inst->imm = imm;
    inst->label = cheng_machine_inst_dup_label(label);
    inst->cond = cond;
    cheng_func_ptr_shadow_remember((void*)inst);
    return (void*)inst;
}

void* cheng_machine_inst_new_n(int32_t op, int32_t rd, int32_t rn, int32_t rm,
                               int32_t ra, int64_t imm, const char* label, int32_t label_len,
                               int32_t cond) {
    ChengMachineInst* inst = (ChengMachineInst*)cheng_malloc((int32_t)sizeof(ChengMachineInst));
    if (inst == NULL) {
        return NULL;
    }
    memset(inst, 0, sizeof(ChengMachineInst));
    inst->magic = CHENG_MACHINE_INST_MAGIC;
    inst->op = op;
    inst->rd = rd;
    inst->rn = rn;
    inst->rm = rm;
    inst->ra = ra;
    inst->imm = imm;
    inst->label = cheng_machine_inst_dup_label_n(label, label_len);
    inst->cond = cond;
    cheng_func_ptr_shadow_remember((void*)inst);
    return (void*)inst;
}

void* cheng_machine_inst_clone(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    if (raw == NULL) {
        return NULL;
    }
    return cheng_machine_inst_new(raw->op, raw->rd, raw->rn, raw->rm,
                                  raw->ra, raw->imm, raw->label, raw->cond);
}

int32_t cheng_machine_inst_valid(void* inst) {
    return cheng_machine_inst_try(inst) != NULL ? 1 : 0;
}

int32_t cheng_machine_inst_op(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    return raw != NULL ? raw->op : 0;
}

int32_t cheng_machine_inst_rd(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    return raw != NULL ? raw->rd : 0;
}

int32_t cheng_machine_inst_rn(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    return raw != NULL ? raw->rn : 0;
}

int32_t cheng_machine_inst_rm(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    return raw != NULL ? raw->rm : 0;
}

int32_t cheng_machine_inst_ra(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    return raw != NULL ? raw->ra : 0;
}

int64_t cheng_machine_inst_imm(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    return raw != NULL ? raw->imm : 0;
}

char* cheng_machine_inst_label(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    if (raw == NULL || raw->label == NULL) {
        return "";
    }
    return raw->label;
}

int32_t cheng_machine_inst_cond(void* inst) {
    ChengMachineInst* raw = cheng_machine_inst_try(inst);
    return raw != NULL ? raw->cond : 0;
}

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

int32_t cheng_seq_next_cap(int32_t cur_cap, int32_t need_len) {
    if (need_len <= 0) {
        return need_len;
    }
    int32_t cap = cur_cap;
    if (cap < 4) {
        cap = 4;
    }
    while (cap < need_len) {
        int32_t doubled = cap * 2;
        if (doubled <= 0) {
            return need_len;
        }
        cap = doubled;
    }
    return cap;
}

void cheng_seq_zero_tail_raw(void* buffer, int32_t seq_cap, int32_t len, int32_t target, int32_t elem_size) {
    (void)seq_cap;
    if (buffer == NULL || elem_size <= 0) {
        return;
    }
    if (target <= len) {
        return;
    }
    int32_t from_bytes = len * elem_size;
    int32_t to_bytes = target * elem_size;
    if (to_bytes <= from_bytes) {
        return;
    }
    memset((uint8_t*)buffer + from_bytes, 0, (size_t)(to_bytes - from_bytes));
}

static void cheng_seq_sanitize(ChengSeqHeader* seq) {
    if (seq == NULL) {
        return;
    }
    if (seq->cap < 0 || seq->cap > (1 << 27) || seq->len < 0 || seq->len > seq->cap) {
        seq->len = 0;
        seq->cap = 0;
        seq->buffer = NULL;
        return;
    }
    if (seq->cap == 0) {
        seq->buffer = NULL;
    }
    if (seq->buffer == NULL && seq->len != 0) {
        seq->len = 0;
    }
    if (seq->buffer != NULL) {
        uintptr_t p = (uintptr_t)seq->buffer;
        if (p < 4096u || p > 0x0000FFFFFFFFFFFFull) {
            seq->len = 0;
            seq->cap = 0;
            seq->buffer = NULL;
        }
    }
}

WEAK void reserve(void* seq_ptr, int32_t new_cap) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL || new_cap < 0) {
        return;
    }
    cheng_seq_sanitize(seq);
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
    const size_t slot_bytes = sizeof(void*) < 32u ? 32u : sizeof(void*);
    const size_t old_bytes = (size_t)(seq->cap > 0 ? seq->cap : 0) * slot_bytes;
    const size_t new_bytes = (size_t)target * slot_bytes;
    void* new_buf = realloc(seq->buffer, new_bytes);
    if (new_buf == NULL) {
        return;
    }
    if (new_bytes > old_bytes) {
        memset((char*)new_buf + old_bytes, 0, new_bytes - old_bytes);
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
    cheng_seq_sanitize(seq);
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
typedef void (*ChengTaskI32Fn)(int32_t);

typedef struct ChengTask {
    ChengTaskFn fn;
    void* ctx;
    struct ChengTask* next;
} ChengTask;

typedef struct ChengThreadJob {
    ChengTaskFn fn;
    void* ctx;
} ChengThreadJob;

typedef struct ChengThreadI32Job {
    ChengTaskI32Fn fn;
    int32_t ctx;
} ChengThreadI32Job;

static ChengTask* cheng_sched_head = NULL;
static ChengTask* cheng_sched_tail = NULL;
static int32_t cheng_sched_count = 0;
static CHENG_THREAD_LOCAL int32_t cheng_thread_local_i32_slots[4];

#if !defined(_WIN32) && !defined(__ANDROID__)
typedef int (*ChengPThreadCreateFn)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
typedef int (*ChengPThreadDetachFn)(pthread_t);

static int cheng_thread_runtime_checked = 0;
static void* cheng_thread_runtime_handle = NULL;
static ChengPThreadCreateFn cheng_pthread_create_fn = NULL;
static ChengPThreadDetachFn cheng_pthread_detach_fn = NULL;

static void cheng_thread_runtime_init(void) {
    if (cheng_thread_runtime_checked) {
        return;
    }
    cheng_thread_runtime_checked = 1;
    cheng_pthread_create_fn = (ChengPThreadCreateFn)dlsym(RTLD_DEFAULT, "pthread_create");
    cheng_pthread_detach_fn = (ChengPThreadDetachFn)dlsym(RTLD_DEFAULT, "pthread_detach");
    if (cheng_pthread_create_fn != NULL && cheng_pthread_detach_fn != NULL) {
        return;
    }
#if !defined(__APPLE__)
    cheng_thread_runtime_handle = dlopen("libpthread.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (cheng_thread_runtime_handle == NULL) {
        cheng_thread_runtime_handle = dlopen("libpthread.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (cheng_thread_runtime_handle != NULL) {
        cheng_pthread_create_fn =
            (ChengPThreadCreateFn)dlsym(cheng_thread_runtime_handle, "pthread_create");
        cheng_pthread_detach_fn =
            (ChengPThreadDetachFn)dlsym(cheng_thread_runtime_handle, "pthread_detach");
    }
#endif
}

static int cheng_thread_runtime_available(void) {
    cheng_thread_runtime_init();
    return cheng_pthread_create_fn != NULL && cheng_pthread_detach_fn != NULL;
}

static void* cheng_thread_job_main(void* raw) {
    ChengThreadJob* job = (ChengThreadJob*)raw;
    if (job == NULL) {
        return NULL;
    }
    ChengTaskFn fn = job->fn;
    void* ctx = job->ctx;
    free(job);
    if (fn != NULL) {
        fn(ctx);
    }
    return NULL;
}

static void* cheng_thread_job_i32_main(void* raw) {
    ChengThreadI32Job* job = (ChengThreadI32Job*)raw;
    if (job == NULL) {
        return NULL;
    }
    ChengTaskI32Fn fn = job->fn;
    int32_t ctx = job->ctx;
    free(job);
    if (fn != NULL) {
        fn(ctx);
    }
    return NULL;
}
#endif

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

int32_t cheng_thread_spawn(void* fn_ptr, void* ctx) {
    if (!fn_ptr) {
        return 0;
    }
#if !defined(_WIN32) && !defined(__ANDROID__)
    if (cheng_thread_runtime_available()) {
        ChengThreadJob* job = (ChengThreadJob*)malloc(sizeof(ChengThreadJob));
        if (job != NULL) {
            pthread_t tid;
            job->fn = (ChengTaskFn)fn_ptr;
            job->ctx = ctx;
            if (cheng_pthread_create_fn(&tid, NULL, cheng_thread_job_main, job) == 0) {
                (void)cheng_pthread_detach_fn(tid);
                return 1;
            }
            free(job);
        }
    }
#endif
    cheng_spawn(fn_ptr, ctx);
    return 1;
}

int32_t cheng_thread_spawn_i32(void* fn_ptr, int32_t ctx) {
    if (!fn_ptr) {
        return 0;
    }
#if !defined(_WIN32) && !defined(__ANDROID__)
    if (cheng_thread_runtime_available()) {
        ChengThreadI32Job* job = (ChengThreadI32Job*)malloc(sizeof(ChengThreadI32Job));
        if (job != NULL) {
            pthread_t tid;
            job->fn = (ChengTaskI32Fn)fn_ptr;
            job->ctx = ctx;
            if (cheng_pthread_create_fn(&tid, NULL, cheng_thread_job_i32_main, job) == 0) {
                (void)cheng_pthread_detach_fn(tid);
                return 1;
            }
            free(job);
        }
    }
#endif
    ((ChengTaskI32Fn)fn_ptr)(ctx);
    return 1;
}

int32_t cheng_thread_parallelism(void) {
#if !defined(_WIN32) && !defined(__ANDROID__)
    if (!cheng_thread_runtime_available()) {
        return 1;
    }
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 1 && ncpu <= INT32_MAX) {
        return (int32_t)ncpu;
    }
#endif
    return 1;
}

void cheng_thread_yield(void) {
#if !defined(_WIN32) && !defined(__ANDROID__)
    sched_yield();
#else
    (void)cheng_sched_run_once();
#endif
}

int32_t cheng_thread_local_i32_get(int32_t slot) {
    if (slot < 0 || slot >= (int32_t)(sizeof(cheng_thread_local_i32_slots) / sizeof(cheng_thread_local_i32_slots[0]))) {
        return 0;
    }
    return cheng_thread_local_i32_slots[slot];
}

void cheng_thread_local_i32_set(int32_t slot, int32_t value) {
    if (slot < 0 || slot >= (int32_t)(sizeof(cheng_thread_local_i32_slots) / sizeof(cheng_thread_local_i32_slots[0]))) {
        return;
    }
    cheng_thread_local_i32_slots[slot] = value;
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
    __atomic_store_n(&st->status, 1, __ATOMIC_RELEASE);
}

int32_t cheng_await_i32(ChengAwaitI32* st) {
    if (!st) {
        return 0;
    }
    while (__atomic_load_n(&st->status, __ATOMIC_ACQUIRE) == 0) {
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
    __atomic_store_n(&st->status, 1, __ATOMIC_RELEASE);
}

void cheng_await_void(ChengAwaitVoid* st) {
    if (!st) {
        return;
    }
    while (__atomic_load_n(&st->status, __ATOMIC_ACQUIRE) == 0) {
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
WEAK void stage1_memRelease(void* p) {
    if (p != NULL) memRelease(p);
}
WEAK void trackLexString(void* p) {
    if (p != NULL) memScopeEscape(p);
}
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
            ChengMemBlock* head = cheng_mem_head_sanitize(scope);
            if (resized->prev && cheng_mem_block_registered(resized->prev) &&
                resized->prev->next == block) {
                resized->prev->next = resized;
            } else if (head == block) {
                scope->head = resized;
            }
            if (resized->next && cheng_mem_block_registered(resized->next) &&
                resized->next->prev == block) {
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
#if defined(__APPLE__) && UINTPTR_MAX > 0xffffffffu
typedef struct {
    mach_vm_address_t addr;
    mach_vm_size_t size;
    int valid;
} cheng_cstr_region_cache_entry;

static cheng_cstr_region_cache_entry cheng_cstr_region_cache[16];

static bool cheng_find_cached_region(uintptr_t raw, mach_vm_address_t* out_addr, mach_vm_size_t* out_size) {
    size_t slot = (size_t)((raw >> 12) & 15u);
    cheng_cstr_region_cache_entry* entry = &cheng_cstr_region_cache[slot];
    if (!entry->valid) {
        return false;
    }
    mach_vm_address_t start = entry->addr;
    mach_vm_address_t end = entry->addr + entry->size;
    if ((mach_vm_address_t)raw < start || (mach_vm_address_t)raw >= end) {
        return false;
    }
    if (out_addr != NULL) *out_addr = entry->addr;
    if (out_size != NULL) *out_size = entry->size;
    return true;
}

static void cheng_store_cached_region(uintptr_t raw, mach_vm_address_t addr, mach_vm_size_t size) {
    size_t slot = (size_t)((raw >> 12) & 15u);
    cheng_cstr_region_cache[slot].addr = addr;
    cheng_cstr_region_cache[slot].size = size;
    cheng_cstr_region_cache[slot].valid = 1;
}
#endif

static bool cheng_safe_cstr_view(const char* s, const char** out_ptr, size_t* out_len) {
    if (out_ptr != NULL) *out_ptr = "";
    if (out_len != NULL) *out_len = 0u;
    if (s == NULL) {
        return true;
    }
    uintptr_t raw = (uintptr_t)s;
    size_t known_len = 0u;
    if (cheng_strmeta_get(s, &known_len)) {
        if (out_ptr != NULL) *out_ptr = s;
        if (out_len != NULL) *out_len = known_len;
        return true;
    }
#if defined(__APPLE__) && UINTPTR_MAX > 0xffffffffu
    if (raw < (uintptr_t)0x1000ULL) {
        return false;
    }
    mach_vm_address_t region_addr = 0;
    mach_vm_size_t region_size = 0;
    if (!cheng_find_cached_region(raw, &region_addr, &region_size)) {
        region_addr = (mach_vm_address_t)raw;
        region_size = 0;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        vm_region_basic_info_data_64_t info;
        mach_port_t object_name = MACH_PORT_NULL;
        kern_return_t kr = mach_vm_region(mach_task_self(),
                                          &region_addr,
                                          &region_size,
                                          VM_REGION_BASIC_INFO_64,
                                          (vm_region_info_t)&info,
                                          &count,
                                          &object_name);
        if (kr != KERN_SUCCESS) {
            return false;
        }
        if ((mach_vm_address_t)raw < region_addr || (mach_vm_address_t)raw >= region_addr + region_size) {
            return false;
        }
        if ((info.protection & VM_PROT_READ) == 0) {
            return false;
        }
        cheng_store_cached_region(raw, region_addr, region_size);
    }
    size_t span = (size_t)((region_addr + region_size) - (mach_vm_address_t)raw);
    size_t limit = span;
    if (limit > (size_t)(1 << 20)) {
        limit = (size_t)(1 << 20);
    }
    for (size_t i = 0; i < limit; ++i) {
        if (s[i] == '\0') {
            if (out_ptr != NULL) *out_ptr = s;
            if (out_len != NULL) *out_len = i;
            return true;
        }
    }
    return false;
#endif
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
        return false;
    }
#endif
    if (out_ptr != NULL) *out_ptr = (const char*)raw;
    if (out_len != NULL) *out_len = strlen((const char*)raw);
    return true;
}

static ChengStrBridge cheng_str_bridge_empty(void) {
    ChengStrBridge out;
    out.ptr = "";
    out.len = 0;
    out.store_id = 0;
    out.flags = CHENG_STR_BRIDGE_FLAG_NONE;
    return out;
}

static ChengStrBridge cheng_str_bridge_from_ptr_flags(const char* ptr, int32_t flags) {
    ChengStrBridge out = cheng_str_bridge_empty();
    const char* safe = "";
    size_t n = 0;
    if (!cheng_safe_cstr_view(ptr, &safe, &n)) {
        return out;
    }
    out.ptr = safe;
    out.len = n > (size_t)INT32_MAX ? INT32_MAX : (int32_t)n;
    out.flags = flags;
    return out;
}

static ChengStrBridge cheng_str_bridge_from_owned(char* ptr) {
    return cheng_str_bridge_from_ptr_flags((const char*)ptr, CHENG_STR_BRIDGE_FLAG_OWNED);
}

static const char* cheng_safe_cstr(const char* s) {
    const char* out = "";
    if (!cheng_safe_cstr_view(s, &out, NULL)) {
        return "";
    }
    return out;
}

int32_t cheng_strlen(char* s) {
    size_t n = 0;
    if (cheng_strmeta_get((const char*)s, &n)) {
        return (n > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)n;
    }
    if (!cheng_safe_cstr_view((const char*)s, NULL, &n)) {
        return 0;
    }
    return (n > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)n;
}
char* cheng_str_concat(char* a, char* b) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_safe_cstr_view((const char*)a, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_cstr_view((const char*)b, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    size_t total = la + lb;
    if (total > (size_t)INT32_MAX - 1) {
        total = (size_t)INT32_MAX - 1;
    }
    char* out = (char*)cheng_malloc((int32_t)total + 1);
    if (!out) {
        return NULL;
    }
    if (la > 0) {
        memcpy(out, sa, la);
    }
    if (lb > 0) {
        memcpy(out + la, sb, lb);
    }
    out[total] = '\0';
    cheng_strmeta_put(out, (int32_t)total);
    return out;
}
static char* cheng_copy_string_bytes(const char* src, size_t len) {
    if (len > (size_t)INT32_MAX - 1) {
        len = (size_t)INT32_MAX - 1;
    }
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (!out) {
        return NULL;
    }
    if (src != NULL && len > 0) {
        memcpy(out, src, len);
    }
    out[len] = '\0';
    cheng_strmeta_put(out, (int32_t)len);
    return out;
}
WEAK char* __cheng_str_concat(char* a, char* b) { return cheng_str_concat(a, b); }
WEAK char* __cheng_sym_2b(char* a, char* b) { return cheng_str_concat(a, b); }
WEAK char* driver_c_str_clone(const char* s) {
    const char* safe = "";
    size_t n = 0;
    if (!cheng_safe_cstr_view(s, &safe, &n)) {
        safe = "";
        n = 0;
    }
    return cheng_copy_string_bytes(safe, n);
}
WEAK ChengStrBridge driver_c_str_clone_bridge(const char* s) {
    return cheng_str_bridge_from_owned(driver_c_str_clone(s));
}
WEAK char* driver_c_str_slice(const char* s, int32_t start, int32_t count) {
    const char* safe = "";
    size_t n = 0;
    if (!cheng_safe_cstr_view(s, &safe, &n)) {
        safe = "";
        n = 0;
    }
    if (count <= 0) {
        return cheng_copy_string_bytes("", 0u);
    }
    size_t s0 = start < 0 ? 0u : (size_t)start;
    if (s0 >= n) {
        return cheng_copy_string_bytes("", 0u);
    }
    size_t need = (size_t)count;
    if (s0 + need > n) {
        need = n - s0;
    }
    return cheng_copy_string_bytes(safe + s0, need);
}
WEAK ChengStrBridge driver_c_str_slice_bridge(const char* s, int32_t start, int32_t count) {
    return cheng_str_bridge_from_owned(driver_c_str_slice(s, start, count));
}
WEAK char* driver_c_char_to_str(int32_t value) {
    unsigned char ch = (unsigned char)(value & 0xff);
    return cheng_copy_string_bytes((const char*)&ch, 1u);
}
WEAK ChengStrBridge driver_c_str_from_utf8_copy_bridge(const char* raw, int32_t n) {
    if (raw == NULL || n <= 0) {
        return cheng_str_bridge_from_owned(cheng_copy_string_bytes("", 0u));
    }
    return cheng_str_bridge_from_owned(cheng_copy_string_bytes(raw, (size_t)n));
}
WEAK int32_t driver_c_str_get_at(const char* s, int32_t idx) {
    const char* safe = "";
    size_t n = 0;
    if (!cheng_safe_cstr_view(s, &safe, &n)) {
        return 0;
    }
    if (idx < 0 || (size_t)idx >= n) {
        return 0;
    }
    return (unsigned char)safe[idx];
}
WEAK int32_t driver_c_str_get_at_bridge(ChengStrBridge s, int32_t idx) {
    return driver_c_str_get_at(s.ptr, idx);
}
WEAK void driver_c_str_set_at(char* s, int32_t idx, int32_t value) {
    size_t n = 0;
    if (!cheng_safe_cstr_view((const char*)s, NULL, &n)) {
        return;
    }
    if (idx < 0 || (size_t)idx >= n) {
        return;
    }
    s[idx] = (char)(value & 0xff);
}
WEAK void driver_c_str_set_at_bridge(ChengStrBridge* s, int32_t idx, int32_t value) {
    if (s == NULL) return;
    driver_c_str_set_at((char*)s->ptr, idx, value);
    *s = cheng_str_bridge_from_ptr_flags(s->ptr, s->flags);
}
WEAK int32_t driver_c_str_has_prefix(const char* s, const char* prefix) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_safe_cstr_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_cstr_view(prefix, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    if (lb == 0) return 1;
    if (la < lb) return 0;
    return memcmp(sa, sb, lb) == 0 ? 1 : 0;
}
WEAK int32_t driver_c_str_has_suffix(const char* s, const char* suffix) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_safe_cstr_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_cstr_view(suffix, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    if (lb == 0) return 1;
    if (la < lb) return 0;
    return memcmp(sa + (la - lb), sb, lb) == 0 ? 1 : 0;
}
WEAK int32_t driver_c_str_has_prefix_bridge(ChengStrBridge s, ChengStrBridge prefix) {
    return driver_c_str_has_prefix(s.ptr, prefix.ptr);
}
WEAK int32_t driver_c_str_contains_char(const char* s, int32_t value) {
    const char* safe = "";
    size_t n = 0;
    unsigned char target = (unsigned char)(value & 0xff);
    if (!cheng_safe_cstr_view(s, &safe, &n)) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        if ((unsigned char)safe[i] == target) return 1;
    }
    return 0;
}
WEAK int32_t driver_c_chr_i32(int32_t value) {
    return (int32_t)((unsigned char)(value & 0xff));
}
WEAK int32_t driver_c_ord_char(int32_t value) {
    return (int32_t)((unsigned char)(value & 0xff));
}
WEAK int32_t driver_c_chr_i32_compat(int32_t value) {
    return driver_c_chr_i32(value);
}
WEAK int32_t driver_c_ord_char_compat(int32_t value) {
    return driver_c_ord_char(value);
}
WEAK char* driver_c_bool_to_str(bool value) {
    if (value) {
        return cheng_copy_string_bytes("true", 4u);
    }
    return cheng_copy_string_bytes("false", 5u);
}
WEAK bool driver_c_bool_identity(bool value) {
    return value;
}
WEAK int32_t driver_c_str_contains_char_bridge(ChengStrBridge s, int32_t value) {
    return driver_c_str_contains_char(s.ptr, value);
}
WEAK int32_t driver_c_str_contains_str(const char* s, const char* sub) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_safe_cstr_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_cstr_view(sub, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    if (lb == 0) return 1;
    if (lb > la) return 0;
    for (size_t i = 0; i + lb <= la; ++i) {
        if (memcmp(sa + i, sb, lb) == 0) return 1;
    }
    return 0;
}
WEAK int32_t driver_c_str_contains_str_bridge(ChengStrBridge s, ChengStrBridge sub) {
    return driver_c_str_contains_str(s.ptr, sub.ptr);
}
WEAK int32_t driver_c_str_eq_bridge(ChengStrBridge a, ChengStrBridge b) {
    if (a.len != b.len) return 0;
    if (a.len <= 0) return 1;
    if (a.ptr == NULL || b.ptr == NULL) return 0;
    return memcmp(a.ptr, b.ptr, (size_t)a.len) == 0 ? 1 : 0;
}
WEAK char* driver_c_i32_to_str(int32_t value) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", value);
    if (n < 0) n = 0;
    return cheng_copy_string_bytes(buf, (size_t)n);
}
WEAK char* driver_c_i64_to_str(int64_t value) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)value);
    if (n < 0) n = 0;
    return cheng_copy_string_bytes(buf, (size_t)n);
}
WEAK char* driver_c_u64_to_str(uint64_t value) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    if (n < 0) n = 0;
    return cheng_copy_string_bytes(buf, (size_t)n);
}
void* cheng_memcpy(void* dest, void* src, int64_t n) { return memcpy(dest, src, (size_t)n); }
void* cheng_memset(void* dest, int32_t val, int64_t n) { return memset(dest, val, (size_t)n); }
void* cheng_memcpy_ffi(void* dest, void* src, int64_t n) { return cheng_memcpy(dest, src, n); }
void* cheng_memset_ffi(void* dest, int32_t val, int64_t n) { return cheng_memset(dest, val, n); }
__attribute__((weak)) void* alloc(int32_t size) { return cheng_malloc(size); }
__attribute__((weak)) void dealloc(void* p) { cheng_free(p); }
__attribute__((weak)) void copyMem(void* dest, void* src, int32_t size) { (void)cheng_memcpy(dest, src, (int64_t)size); }
__attribute__((weak)) void setMem(void* dest, int32_t val, int32_t size) { (void)cheng_memset(dest, val, (int64_t)size); }
__attribute__((weak)) void cheng_zero_mem_compat(void* dest, int32_t size) { (void)cheng_memset(dest, 0, (int64_t)size); }
int32_t cheng_memcmp(void* a, void* b, int64_t n) { return memcmp(a, b, (size_t)n); }
int32_t cheng_strcmp(const char* a, const char* b) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_safe_cstr_view(a, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_cstr_view(b, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    size_t n = la < lb ? la : lb;
    int cmp = 0;
    if (n > 0) {
        cmp = memcmp(sa, sb, n);
    }
    if (cmp != 0) {
        return cmp;
    }
    if (la < lb) return -1;
    if (la > lb) return 1;
    return 0;
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
bool cheng_f64_bits_is_nan(int64_t bits) {
    uint64_t ubits = (uint64_t)bits;
    uint64_t exp_raw = (ubits >> 52u) & 0x7ffu;
    uint64_t frac = ubits & 0x000fffffffffffffull;
    if (exp_raw != 0x7ffu) {
        return false;
    }
    return frac != 0;
}
bool cheng_f64_bits_is_zero(int64_t bits) {
    uint64_t mag = ((uint64_t)bits) & 0x7fffffffffffffffull;
    return mag == 0;
}
uint64_t cheng_f64_bits_order(int64_t bits) {
    uint64_t ubits = (uint64_t)bits;
    uint64_t sign_mask = 1ull << 63u;
    if ((ubits & sign_mask) != 0) {
        return ~ubits;
    }
    return ubits ^ sign_mask;
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

static FILE* cheng_safe_stream(void* stream) {
    if (stream == (void*)stdout || stream == (void*)stderr || stream == (void*)stdin) {
        return (FILE*)stream;
    }
    if (stream == NULL) return stdout;
    uintptr_t v = (uintptr_t)stream;
    if (v < 0x100000000ull) {
        return stdout;
    }
    return (FILE*)stream;
}

int32_t cheng_fclose(void* f) {
    FILE* stream = cheng_safe_stream(f);
    if (stream == stdout || stream == stderr || stream == stdin) return 0;
    return fclose(stream);
}
int32_t cheng_fread(void* ptr, int64_t size, int64_t n, void* stream) {
    FILE* f = cheng_safe_stream(stream);
    return (int32_t)fread(ptr, (size_t)size, (size_t)n, f);
}
int32_t cheng_fwrite(void* ptr, int64_t size, int64_t n, void* stream) {
    FILE* f = cheng_safe_stream(stream);
    return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, f);
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
    FILE* f = cheng_safe_stream(stream);
    return (int64_t)_ftelli64(f);
#else
    FILE* f = cheng_safe_stream(stream);
    return (int64_t)ftello(f);
#endif
}
int32_t cheng_fflush(void* stream) {
    FILE* f = cheng_safe_stream(stream);
    return fflush(f);
}
int32_t cheng_fgetc(void* stream) {
    FILE* f = cheng_safe_stream(stream);
    return fgetc(f);
}

void* get_stdin() { return (void*)stdin; }
void* get_stdout() { return (void*)stdout; }
void* get_stderr() { return (void*)stderr; }

/*
 * Stage0 compatibility shim:
 * older bootstrap outputs may keep a direct unresolved call to symbol "[]=".
 * Provide a benign fallback body to keep runtime symbol closure deterministic.
 */
int64_t cheng_index_set_compat(void) __asm__("_[]=");
int64_t cheng_index_set_compat(void) { return 0; }

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
  #include <sys/syscall.h>
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

static int cheng_repr_append_bytes(char** buf, size_t* cap, size_t* len, const char* src, size_t n) {
    if (!cheng_buf_reserve(buf, cap, *len + n + 1u)) return 0;
    if (n > 0 && src != NULL) {
        memcpy(*buf + *len, src, n);
    }
    *len += n;
    (*buf)[*len] = 0;
    return 1;
}

static int cheng_repr_append_cstr(char** buf, size_t* cap, size_t* len, const char* src) {
    const char* safe = cheng_safe_cstr(src);
    return cheng_repr_append_bytes(buf, cap, len, safe, strlen(safe));
}

WEAK char* cheng_seq_string_repr_compat(ChengSeqHeader xs) {
    char** items = (char**)xs.buffer;
    size_t cap = 0;
    size_t len = 0;
    char* out = NULL;
    int32_t i;
    if (!cheng_repr_append_bytes(&out, &cap, &len, "[", 1u)) return cheng_copy_string_bytes("", 0u);
    for (i = 0; i < xs.len; ++i) {
        if (i > 0 && !cheng_repr_append_bytes(&out, &cap, &len, ",", 1u)) return cheng_copy_string_bytes("", 0u);
        if (!cheng_repr_append_cstr(&out, &cap, &len, items == NULL ? "" : items[i])) return cheng_copy_string_bytes("", 0u);
    }
    if (!cheng_repr_append_bytes(&out, &cap, &len, "]", 1u)) return cheng_copy_string_bytes("", 0u);
    return out;
}

WEAK char* cheng_seq_bool_repr_compat(ChengSeqHeader xs) {
    const int8_t* items = (const int8_t*)xs.buffer;
    size_t cap = 0;
    size_t len = 0;
    char* out = NULL;
    int32_t i;
    if (!cheng_repr_append_bytes(&out, &cap, &len, "[", 1u)) return cheng_copy_string_bytes("", 0u);
    for (i = 0; i < xs.len; ++i) {
        if (i > 0 && !cheng_repr_append_bytes(&out, &cap, &len, ",", 1u)) return cheng_copy_string_bytes("", 0u);
        if (!cheng_repr_append_cstr(&out, &cap, &len, (items != NULL && items[i] != 0) ? "true" : "false")) return cheng_copy_string_bytes("", 0u);
    }
    if (!cheng_repr_append_bytes(&out, &cap, &len, "]", 1u)) return cheng_copy_string_bytes("", 0u);
    return out;
}

WEAK char* cheng_seq_i32_repr_compat(ChengSeqHeader xs) {
    const int32_t* items = (const int32_t*)xs.buffer;
    size_t cap = 0;
    size_t len = 0;
    char* out = NULL;
    int32_t i;
    if (!cheng_repr_append_bytes(&out, &cap, &len, "[", 1u)) return cheng_copy_string_bytes("", 0u);
    for (i = 0; i < xs.len; ++i) {
        char* part;
        if (i > 0 && !cheng_repr_append_bytes(&out, &cap, &len, ",", 1u)) return cheng_copy_string_bytes("", 0u);
        part = driver_c_i32_to_str(items == NULL ? 0 : items[i]);
        if (!cheng_repr_append_cstr(&out, &cap, &len, part)) return cheng_copy_string_bytes("", 0u);
    }
    if (!cheng_repr_append_bytes(&out, &cap, &len, "]", 1u)) return cheng_copy_string_bytes("", 0u);
    return out;
}

WEAK char* cheng_seq_i64_repr_compat(ChengSeqHeader xs) {
    const int64_t* items = (const int64_t*)xs.buffer;
    size_t cap = 0;
    size_t len = 0;
    char* out = NULL;
    int32_t i;
    if (!cheng_repr_append_bytes(&out, &cap, &len, "[", 1u)) return cheng_copy_string_bytes("", 0u);
    for (i = 0; i < xs.len; ++i) {
        char* part;
        if (i > 0 && !cheng_repr_append_bytes(&out, &cap, &len, ",", 1u)) return cheng_copy_string_bytes("", 0u);
        part = driver_c_i64_to_str(items == NULL ? 0 : items[i]);
        if (!cheng_repr_append_cstr(&out, &cap, &len, part)) return cheng_copy_string_bytes("", 0u);
    }
    if (!cheng_repr_append_bytes(&out, &cap, &len, "]", 1u)) return cheng_copy_string_bytes("", 0u);
    return out;
}

WEAK ChengSeqHeader cheng_seq_return_string_compat(ChengSeqHeader out) {
    return out;
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

int32_t cheng_fd_write(int32_t fd, const char* data, int32_t len) {
#if defined(_WIN32)
    (void)fd;
    (void)data;
    (void)len;
    return -1;
#else
    if (fd < 0 || data == NULL || len <= 0) return 0;
    ssize_t n = (ssize_t)syscall(SYS_write, fd, data, (size_t)len);
    if (n < 0) return -1;
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

int32_t cheng_abi_sum_ptr_len_i32(const int32_t* ptr, int32_t len) {
    return cheng_abi_sum_seq_i32_raw(ptr, len);
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

void* cheng_ffi_raw_new_i32(int32_t value) {
    int32_t* cell = (int32_t*)malloc(sizeof(int32_t));
    if (cell == NULL) {
        return NULL;
    }
    *cell = value;
    return (void*)cell;
}

int32_t cheng_ffi_raw_get_i32(void* p) {
    int32_t* cell = (int32_t*)p;
    if (cell == NULL) {
        return -1;
    }
    return *cell;
}

int32_t cheng_ffi_raw_add_i32(void* p, int32_t delta) {
    int32_t* cell = (int32_t*)p;
    if (cell == NULL) {
        return -1;
    }
    *cell += delta;
    return *cell;
}

int32_t cheng_ffi_raw_release_i32(void* p) {
    int32_t* cell = (int32_t*)p;
    if (cell == NULL) {
        return -1;
    }
    free(cell);
    return 0;
}
