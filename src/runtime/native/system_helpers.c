#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#if !defined(_WIN32)
#include <poll.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#endif
#if !defined(_WIN32)
#include <spawn.h>
#if defined(__APPLE__)
#include <sys/event.h>
#endif
#endif
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
#include <TargetConditionals.h>
#include <sys/ucontext.h>
#include <mach-o/dyld.h>
#include <crt_externs.h>
#include <mach/mach.h>
#if !TARGET_OS_IPHONE
#include <mach/mach_vm.h>
#endif
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define CHENG_TARGET_APPLE_MOBILE 1
#else
#define CHENG_TARGET_APPLE_MOBILE 0
#endif
#include "cheng_sidecar_loader.h"
#include "../../../v3/runtime/native/v3_str_twoway_search.h"
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

#if defined(__GNUC__) || defined(__clang__)
extern int32_t cheng_mobile_host_biometric_fingerprint_authorize_bridge(
    const char* request_id,
    int32_t purpose,
    const char* did_text,
    const char* prompt_title,
    const char* prompt_reason,
    const char* device_binding_seed_hint,
    const char* device_label_hint,
    char* out_feature32_hex,
    int32_t out_feature32_cap,
    char* out_device_binding_seed,
    int32_t out_device_binding_seed_cap,
    char* out_device_label,
    int32_t out_device_label_cap,
    char* out_sensor_id,
    int32_t out_sensor_id_cap,
    char* out_hardware_attestation,
    int32_t out_hardware_attestation_cap,
    char* out_error,
    int32_t out_error_cap) __attribute__((weak));
#else
extern int32_t cheng_mobile_host_biometric_fingerprint_authorize_bridge(
    const char* request_id,
    int32_t purpose,
    const char* did_text,
    const char* prompt_title,
    const char* prompt_reason,
    const char* device_binding_seed_hint,
    const char* device_label_hint,
    char* out_feature32_hex,
    int32_t out_feature32_cap,
    char* out_device_binding_seed,
    int32_t out_device_binding_seed_cap,
    char* out_device_label,
    int32_t out_device_label_cap,
    char* out_sensor_id,
    int32_t out_sensor_id_cap,
    char* out_hardware_attestation,
    int32_t out_hardware_attestation_cap,
    char* out_error,
    int32_t out_error_cap);
#endif

#if defined(__APPLE__)
#define CHENG_WEAK_IMPORT __attribute__((weak_import))
#else
#define CHENG_WEAK_IMPORT WEAK
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define CHENG_THREAD_LOCAL __declspec(thread)
#else
#define CHENG_THREAD_LOCAL
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_MAYBE_UNUSED __attribute__((unused))
#else
#define CHENG_MAYBE_UNUSED
#endif

#if defined(__APPLE__) && !defined(INADDR_LOOPBACK)
#define INADDR_LOOPBACK ((uint32_t)0x7f000001U)
#endif

#if defined(__APPLE__) && !defined(RTLD_DEFAULT)
#define RTLD_DEFAULT ((void*)-2)
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

typedef struct ChengBytesBridge {
    void* data;
    int32_t len;
} ChengBytesBridge;

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
static int driver_c_runtime_resolve_self_path(char* out, size_t out_cap);
void cheng_dump_backtrace_if_enabled(void);
static void cheng_dump_backtrace_now(const char* reason);
static void cheng_exec_kill_target(pid_t pid, int sig);
static pid_t cheng_exec_spawn_orphan_guard(pid_t targetPid);
static void cheng_exec_stop_orphan_guard(pid_t guardPid);
static int64_t cheng_exec_wait_status(pid_t pid, int32_t timeoutSec);
int32_t cheng_pipe_spawn(const char* command, const char* workingDir, int32_t* outReadFd, int32_t* outWriteFd, int64_t* outPid);
int32_t cheng_exec_file_pipe_spawn(const char* filePath, void* argvSeqPtr, void* envOverridesSeqPtr,
                                   const char* workingDir, int32_t* outReadFd, int32_t* outWriteFd, int64_t* outPid);
char* cheng_fd_read_wait(int32_t fd, int32_t maxBytes, int32_t timeoutMs, int32_t* outEof);
uint64_t cheng_ffi_handle_register_ptr(void* ptr);
void* cheng_ffi_handle_resolve_ptr(uint64_t handle);
int32_t cheng_ffi_handle_invalidate(uint64_t handle);
#if !defined(_WIN32)
extern char** environ;
#endif

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

static int32_t cheng_seq_elem_size_compat_get(void* key_ptr, int32_t fallback);
static void cheng_seq_elem_size_compat_register(void* seq_ptr, void* buffer, int32_t elem_size);
static void cheng_seq_elem_size_compat_unregister(void* seq_ptr, void* buffer);

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
    int32_t actual_elem_size = cheng_seq_elem_size_compat_get(seq_ptr, elem_size);
    int32_t buf_cap = seq_hdr->cap;
    void* raw_buffer = seq_hdr->buffer;
    if (raw_buffer == NULL || min_cap > buf_cap) {
        cheng_seq_grow_to_raw(&raw_buffer, &buf_cap, min_cap, actual_elem_size);
        seq_hdr->buffer = raw_buffer;
        seq_hdr->cap = buf_cap;
    }
    cheng_seq_elem_size_compat_register(seq_ptr, seq_hdr->buffer, actual_elem_size);
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
    int32_t base = (int32_t)sizeof(void*) + 3 * (int32_t)sizeof(int32_t);
    int32_t align = (int32_t)sizeof(void*);
    int32_t rem = (align > 0) ? (base % align) : 0;
    return rem == 0 ? base : (base + (align - rem));
}

#define CHENG_SEQ_ELEM_SIZE_COMPAT_CAP 4096u
static uintptr_t cheng_seq_elem_size_compat_keys[CHENG_SEQ_ELEM_SIZE_COMPAT_CAP];
static int32_t cheng_seq_elem_size_compat_vals[CHENG_SEQ_ELEM_SIZE_COMPAT_CAP];

static uint32_t cheng_seq_elem_size_compat_slot(uintptr_t key) {
    return (uint32_t)((key >> 3u) & (CHENG_SEQ_ELEM_SIZE_COMPAT_CAP - 1u));
}

static void cheng_seq_elem_size_compat_put(void* key_ptr, int32_t elem_size) {
    uintptr_t key = (uintptr_t)key_ptr;
    if (key == 0 || elem_size <= 0) {
        return;
    }
    uint32_t slot = cheng_seq_elem_size_compat_slot(key);
    for (uint32_t scanned = 0; scanned < CHENG_SEQ_ELEM_SIZE_COMPAT_CAP; ++scanned) {
        uintptr_t cur_key = cheng_seq_elem_size_compat_keys[slot];
        if (cur_key == 0 || cur_key == key) {
            cheng_seq_elem_size_compat_keys[slot] = key;
            cheng_seq_elem_size_compat_vals[slot] = elem_size;
            return;
        }
        slot = (slot + 1u) & (CHENG_SEQ_ELEM_SIZE_COMPAT_CAP - 1u);
    }
}

static int32_t cheng_seq_elem_size_compat_get(void* key_ptr, int32_t fallback) {
    uintptr_t key = (uintptr_t)key_ptr;
    if (key == 0) {
        return fallback;
    }
    uint32_t slot = cheng_seq_elem_size_compat_slot(key);
    for (uint32_t scanned = 0; scanned < CHENG_SEQ_ELEM_SIZE_COMPAT_CAP; ++scanned) {
        uintptr_t cur_key = cheng_seq_elem_size_compat_keys[slot];
        if (cur_key == 0) {
            return fallback;
        }
        if (cur_key == key) {
            int32_t value = cheng_seq_elem_size_compat_vals[slot];
            return value > 0 ? value : fallback;
        }
        slot = (slot + 1u) & (CHENG_SEQ_ELEM_SIZE_COMPAT_CAP - 1u);
    }
    return fallback;
}

static void cheng_seq_elem_size_compat_del(void* key_ptr) {
    uintptr_t key = (uintptr_t)key_ptr;
    if (key == 0) {
        return;
    }
    uint32_t slot = cheng_seq_elem_size_compat_slot(key);
    for (uint32_t scanned = 0; scanned < CHENG_SEQ_ELEM_SIZE_COMPAT_CAP; ++scanned) {
        uintptr_t cur_key = cheng_seq_elem_size_compat_keys[slot];
        if (cur_key == 0) {
            return;
        }
        if (cur_key == key) {
            cheng_seq_elem_size_compat_keys[slot] = 0;
            cheng_seq_elem_size_compat_vals[slot] = 0;
            return;
        }
        slot = (slot + 1u) & (CHENG_SEQ_ELEM_SIZE_COMPAT_CAP - 1u);
    }
}

static void cheng_seq_elem_size_compat_register(void* seq_ptr, void* buffer, int32_t elem_size) {
    cheng_seq_elem_size_compat_put(seq_ptr, elem_size);
    cheng_seq_elem_size_compat_put(buffer, elem_size);
}

static void cheng_seq_elem_size_compat_unregister(void* seq_ptr, void* buffer) {
    cheng_seq_elem_size_compat_del(seq_ptr);
    cheng_seq_elem_size_compat_del(buffer);
}

void cheng_seq_string_register_compat(void* seq_ptr) {
    ChengSeqHeader* seq_hdr = (ChengSeqHeader*)seq_ptr;
    int32_t elem_size = cheng_seq_string_elem_bytes_compat();
    if (seq_hdr == NULL) {
        return;
    }
    cheng_seq_elem_size_compat_register(seq_ptr, seq_hdr->buffer, elem_size);
}

void cheng_seq_string_buffer_register_compat(void* buffer) {
    cheng_seq_elem_size_compat_put(buffer, cheng_seq_string_elem_bytes_compat());
}

void* cheng_seq_string_alloc_compat(int32_t buf_cap) {
    if (buf_cap <= 0) {
        return NULL;
    }
    int32_t total_bytes = buf_cap * cheng_seq_string_elem_bytes_compat();
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
    cheng_seq_string_register_compat(seq_ptr);
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
    void* old_buffer = seq_hdr->buffer;
    if (seq_hdr->buffer != NULL) {
        cheng_mem_release(seq_hdr->buffer);
    }
    cheng_seq_elem_size_compat_unregister(seq_ptr, old_buffer);
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

int32_t driver_c_str_eq_bridge(ChengStrBridge a, ChengStrBridge b);

typedef struct ChengErrorInfoBridgeCompat {
    int32_t code;
    ChengStrBridge msg;
} ChengErrorInfoBridgeCompat;

enum {
    CHENG_STR_BRIDGE_FLAG_NONE = 0,
    CHENG_STR_BRIDGE_FLAG_OWNED = 1,
};

static bool cheng_probably_valid_str_bridge(const ChengStrBridge* s) {
    uintptr_t raw_ptr = 0;
    if (s == NULL) return false;
    if (s->len < 0 || s->len > (1 << 28)) return false;
    if ((s->flags & ~CHENG_STR_BRIDGE_FLAG_OWNED) != 0) return false;
    if (s->ptr == NULL) return s->len == 0;
    raw_ptr = (uintptr_t)s->ptr;
    return raw_ptr >= (uintptr_t)65536;
}

char* cheng_str_param_to_cstring_compat(void* raw) {
    uintptr_t raw_addr = (uintptr_t)raw;
    if (raw == NULL) return NULL;
    if (raw_addr < (uintptr_t)65536) return NULL;
    if ((raw_addr & (uintptr_t)(sizeof(void*) - 1)) != 0) return (char*)raw;
    if (!cheng_probably_valid_str_bridge((const ChengStrBridge*)raw)) return (char*)raw;
    return NULL;
}

char* driver_c_new_string(int32_t n);
char* driver_c_new_string_copy_n(void* raw, int32_t n);

WEAK char* cheng_str_to_cstring_temp_bridge(ChengStrBridge s) {
    char* compat = NULL;
    if (cheng_probably_valid_str_bridge(&s)) {
        if (s.len <= 0 || s.ptr == NULL) return driver_c_new_string(0);
        if ((s.flags & CHENG_STR_BRIDGE_FLAG_OWNED) != 0) return (char*)s.ptr;
        compat = driver_c_new_string_copy_n((void*)s.ptr, s.len);
        if (compat != NULL) return compat;
        return driver_c_new_string(0);
    }
    compat = cheng_str_param_to_cstring_compat((void*)s.ptr);
    if (compat != NULL) return compat;
    if (s.len <= 0 || s.ptr == NULL) return driver_c_new_string(0);
    compat = driver_c_new_string_copy_n((void*)s.ptr, s.len);
    if (compat != NULL) return compat;
    return driver_c_new_string(0);
}

WEAK int32_t cheng_v3_udp_bind_host_port_bridge(ChengStrBridge host,
                                                int32_t port,
                                                int32_t isV6,
                                                int32_t* outFd,
                                                int32_t* outPort,
                                                int32_t* outFamily,
                                                int32_t* outUseLenField) {
    const char* host_text = cheng_str_to_cstring_temp_bridge(host);
    int fd = -1;
    int rc = -1;
    if (outFd == NULL || outPort == NULL || outFamily == NULL || outUseLenField == NULL) {
        return EINVAL;
    }
    *outFd = -1;
    *outPort = 0;
    *outFamily = 0;
    *outUseLenField = 0;
    if (port < 0 || port > 65535) {
        return EINVAL;
    }
    if (isV6 != 0) {
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if (fd < 0) {
            return errno;
        }
#if defined(__APPLE__)
        addr6.sin6_len = (uint8_t)sizeof(addr6);
        *outUseLenField = 1;
#endif
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons((uint16_t)port);
        if (host_text == NULL || host_text[0] == 0 || strcmp(host_text, "localhost") == 0) {
            addr6.sin6_addr = in6addr_loopback;
        } else if (inet_pton(AF_INET6, host_text, &addr6.sin6_addr) != 1) {
            close(fd);
            return EINVAL;
        }
        rc = bind(fd, (const struct sockaddr*)&addr6, (socklen_t)sizeof(addr6));
        if (rc != 0) {
            int err = errno;
            close(fd);
            return err;
        }
        if (port == 0) {
            socklen_t len = (socklen_t)sizeof(addr6);
            if (getsockname(fd, (struct sockaddr*)&addr6, &len) != 0) {
                int err = errno;
                close(fd);
                return err;
            }
        }
        *outFd = fd;
        *outPort = (int32_t)ntohs(addr6.sin6_port);
        *outFamily = AF_INET6;
        return 0;
    }
    {
        struct sockaddr_in addr4;
        memset(&addr4, 0, sizeof(addr4));
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            return errno;
        }
#if defined(__APPLE__)
        addr4.sin_len = (uint8_t)sizeof(addr4);
        *outUseLenField = 1;
#endif
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons((uint16_t)port);
        if (host_text == NULL || host_text[0] == 0 || strcmp(host_text, "localhost") == 0) {
            addr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        } else if (inet_pton(AF_INET, host_text, &addr4.sin_addr) != 1) {
            close(fd);
            return EINVAL;
        }
        rc = bind(fd, (const struct sockaddr*)&addr4, (socklen_t)sizeof(addr4));
        if (rc != 0) {
            int err = errno;
            close(fd);
            return err;
        }
        if (port == 0) {
            socklen_t len = (socklen_t)sizeof(addr4);
            if (getsockname(fd, (struct sockaddr*)&addr4, &len) != 0) {
                int err = errno;
                close(fd);
                return err;
            }
        }
        *outFd = fd;
        *outPort = (int32_t)ntohs(addr4.sin_port);
        *outFamily = AF_INET;
        return 0;
    }
}

WEAK int32_t cheng_v3_udp_bind_fd_bridge(ChengStrBridge host, int32_t port, int32_t isV6) {
    int32_t fd = -1;
    int32_t boundPort = 0;
    int32_t family = 0;
    int32_t useLenField = 0;
    int32_t rc = cheng_v3_udp_bind_host_port_bridge(host, port, isV6, &fd, &boundPort, &family, &useLenField);
    if (rc != 0) {
        return -rc;
    }
    return fd;
}

WEAK int32_t cheng_v3_udp_bound_port_bridge(int32_t fd, int32_t isV6) {
    if (fd < 0) {
        return -EINVAL;
    }
    if (isV6 != 0) {
        struct sockaddr_in6 addr6;
        socklen_t len = (socklen_t)sizeof(addr6);
        memset(&addr6, 0, sizeof(addr6));
        if (getsockname(fd, (struct sockaddr*)&addr6, &len) != 0) {
            return -errno;
        }
        return (int32_t)ntohs(addr6.sin6_port);
    }
    {
        struct sockaddr_in addr4;
        socklen_t len = (socklen_t)sizeof(addr4);
        memset(&addr4, 0, sizeof(addr4));
        if (getsockname(fd, (struct sockaddr*)&addr4, &len) != 0) {
            return -errno;
        }
        return (int32_t)ntohs(addr4.sin_port);
    }
}

WEAK int32_t cheng_v3_udp_recvfrom_addr_bridge(int32_t fd,
                                               void* buf,
                                               int32_t len,
                                               int32_t flags,
                                               void* addr,
                                               int32_t addrCap,
                                               int32_t* outAddrLen,
                                               int32_t* outErr) {
    socklen_t raw_len = 0;
    int32_t rc = 0;
    errno = 0;
    if (outAddrLen == NULL || outErr == NULL) {
        return -1;
    }
    *outAddrLen = 0;
    *outErr = 0;
    if (fd < 0 || buf == NULL || len <= 0 || addr == NULL || addrCap <= 0) {
        return -1;
    }
    raw_len = (socklen_t)addrCap;
    rc = (int32_t)recvfrom(fd, buf, (size_t)len, flags, (struct sockaddr*)addr, &raw_len);
    if (rc < 0) {
        *outErr = errno;
        *outAddrLen = 0;
        return rc;
    }
    *outAddrLen = (int32_t)raw_len;
    return rc;
}

WEAK int32_t cheng_v3_udp_platform_use_len_field_bridge(void) {
#if defined(__APPLE__)
    return 1;
#else
    return 0;
#endif
}

WEAK int32_t cheng_v3_udp_platform_msg_dontwait_bridge(void) {
#if defined(__linux__)
    return 0x40;
#else
    return 0x80;
#endif
}

static int32_t cheng_v3_hex_value_ascii(char c) {
    if (c >= '0' && c <= '9') {
        return (int32_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (int32_t)(c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (int32_t)(c - 'A');
    }
    return -1;
}

void cheng_v3_test_pki_hex_decode_into_bridge(ChengStrBridge text, ChengBytesBridge* out) {
    if (out == NULL) {
        return;
    }
    out->data = NULL;
    out->len = 0;
    char* raw = cheng_str_to_cstring_temp_bridge(text);
    if (raw == NULL) {
        return;
    }
    int32_t text_len = cheng_strlen(raw);
    if (text_len <= 0) {
        return;
    }
    if ((text_len & 1) != 0) {
        abort();
    }
    int32_t pair_count = text_len / 2;
    uint8_t* decoded = (uint8_t*)cheng_malloc(pair_count);
    if (decoded == NULL) {
        abort();
    }
    for (int32_t i = 0; i < pair_count; ++i) {
        int32_t src = i * 2;
        int32_t hi = cheng_v3_hex_value_ascii(raw[src]);
        int32_t lo = cheng_v3_hex_value_ascii(raw[src + 1]);
        if (hi < 0 || lo < 0) {
            abort();
        }
        decoded[i] = (uint8_t)((hi << 4) | lo);
    }
    out->data = decoded;
    out->len = pair_count;
}

ChengBytesBridge cheng_v3_test_pki_hex_decode_bridge(ChengStrBridge text) {
    ChengBytesBridge out;
    out.data = NULL;
    out.len = 0;
    cheng_v3_test_pki_hex_decode_into_bridge(text, &out);
    return out;
}

void* cheng_v3_test_pki_hex_decode_ptr_bridge(ChengStrBridge text) {
    ChengBytesBridge out = cheng_v3_test_pki_hex_decode_bridge(text);
    return out.data;
}

int32_t cheng_v3_test_pki_hex_decode_len_bridge(ChengStrBridge text) {
    char* raw = cheng_str_to_cstring_temp_bridge(text);
    if (raw == NULL) {
        return 0;
    }
    int32_t text_len = cheng_strlen(raw);
    if (text_len <= 0 || (text_len & 1) != 0) {
        return 0;
    }
    return text_len / 2;
}

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
typedef void* (*cheng_build_module_stage1_target_facade_fn)(const char* path,
                                                            const char* target);
typedef void* (*cheng_build_module_stage1_facade_fn)(const char* path);

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
    if (driver_c_module_handle_try(payload) != NULL) return payload;
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
static ChengStrMetaEntry* cheng_strmeta_table = NULL;
static int32_t cheng_strmeta_table_cap = 0;
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

#include "system_helpers_debug_state.inc"

#include "system_helpers_debug_host_base.inc"

#define CHENG_V3_DEBUG_WEAK WEAK
#include "system_helpers_debug_trace.inc"
#undef CHENG_V3_DEBUG_WEAK

static ChengSidecarBundleCache cheng_sidecar_cache = {0};
static CHENG_THREAD_LOCAL int32_t driver_c_build_emit_obj_dispatch_depth = 0;
static CHENG_THREAD_LOCAL int32_t driver_c_build_module_dispatch_depth = 0;

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

static CHENG_MAYBE_UNUSED int cheng_arg_is_help(const char* arg) {
    if (arg == NULL) return 0;
    return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

WEAK int32_t driver_c_backend_usage(void) {
    fputs("Usage:\n", stderr);
    fputs("  backend_driver [--input:<file>|<file>] [--output:<out>] [options]\n", stderr);
    fputs("\n", stderr);
    fputs("Core options:\n", stderr);
    fputs("  --emit:<exe|obj> --target:<triple|auto> --frontend:<stage1>\n", stderr);
    fputs("  --linker:<self|system> --obj-writer:<auto|elf|macho|coff>\n", stderr);
    fputs("  --build-track:<dev|release> --mm:<orc> --fn-sched:<ws>\n", stderr);
    fputs("  --whole-program --whole-program:0|1\n", stderr);
    fputs("  --opt-level:<N> --opt2 --no-opt2 --opt --no-opt\n", stderr);
    fputs("  --ldflags:<text>\n", stderr);
    fputs("  --jobs:<N> --fn-jobs:<N>\n", stderr);
    fputs("  --module-cache:<path>\n", stderr);
    fputs("  --multi-module-cache --multi-module-cache:0|1\n", stderr);
    fputs("  --module-cache-unstable-allow --module-cache-unstable-allow:0|1\n", stderr);
    fputs("  --multi --no-multi --multi-force --no-multi-force\n", stderr);
    fputs("  --incremental --no-incremental --allow-no-main\n", stderr);
    fputs("  --skip-global-init --runtime-obj:<path> --runtime-c:<path> --no-runtime-c\n", stderr);
    fputs("  --generic-mode:<dict> --generic-spec-budget:<N>\n", stderr);
    fputs("  --borrow-ir:<mir|stage1> --generic-lowering:<mir_dict>\n", stderr);
    fputs("  --android-api:<N> --compile-stamp-out:<path>\n", stderr);
    fputs("  --sidecar-mode:<cheng> --sidecar-bundle:<path>\n", stderr);
    fputs("  --sidecar-compiler:<path>\n", stderr);
    fputs("  --sidecar-child-mode:<cli|outer_cli> --sidecar-outer-compiler:<path>\n", stderr);
    fputs("  --profile --no-profile --uir-profile --no-uir-profile\n", stderr);
    fputs("  --uir-simd --no-uir-simd --uir-simd-max-width:<N> --uir-simd-policy:<name>\n", stderr);
    return 0;
}

static void driver_c_maybe_inject_env_cli(int* argc_io, char*** argv_io);
static void driver_c_sync_cli_env(int argc, char** argv);
static int driver_c_has_flag_value(int argc, char** argv, const char* flag);
WEAK void driver_c_capture_cmdline(int32_t argc, void* argv_void);

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
int32_t cheng_dir_exists(const char* path);
int64_t cheng_file_size(const char* path);
int32_t cheng_mkdir1(const char* path);
char* cheng_getcwd(void);
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

#define CHENG_STRMETA_RECENT_SLOTS 8u
static CHENG_THREAD_LOCAL const char* cheng_strmeta_recent_ptrs[CHENG_STRMETA_RECENT_SLOTS];
static CHENG_THREAD_LOCAL size_t cheng_strmeta_recent_lens[CHENG_STRMETA_RECENT_SLOTS];
static CHENG_THREAD_LOCAL unsigned char cheng_strmeta_recent_valids[CHENG_STRMETA_RECENT_SLOTS];

static size_t cheng_strmeta_hash_ptr(const char* ptr) {
    size_t raw = (size_t)(const void*)ptr;
    raw ^= raw >> 17;
    raw ^= raw >> 9;
    raw *= (size_t)1315423911u;
    return raw;
}

static size_t cheng_strmeta_recent_slot(const char* ptr) {
    return cheng_strmeta_hash_ptr(ptr) & (CHENG_STRMETA_RECENT_SLOTS - 1u);
}

static bool cheng_strmeta_recent_get(const char* ptr, size_t* out_len) {
    if (ptr == NULL) {
        return false;
    }
    size_t slot = cheng_strmeta_recent_slot(ptr);
    if (!cheng_strmeta_recent_valids[slot] || cheng_strmeta_recent_ptrs[slot] != ptr) {
        return false;
    }
    if (out_len != NULL) {
        *out_len = cheng_strmeta_recent_lens[slot];
    }
    return true;
}

static void cheng_strmeta_recent_put(const char* ptr, size_t len) {
    if (ptr == NULL) {
        return;
    }
    size_t slot = cheng_strmeta_recent_slot(ptr);
    cheng_strmeta_recent_ptrs[slot] = ptr;
    cheng_strmeta_recent_lens[slot] = len;
    cheng_strmeta_recent_valids[slot] = 1u;
}

static bool cheng_strmeta_probe_table_snapshot(const char* ptr, ChengStrMetaEntry* table,
                                               int32_t table_cap, size_t* out_len) {
    if (ptr == NULL || table == NULL || table_cap <= 0) {
        return false;
    }
    size_t slot = cheng_strmeta_hash_ptr(ptr) % (size_t)table_cap;
    for (int32_t probes = 0; probes < table_cap; ++probes) {
        const char* entry_ptr = __atomic_load_n(&table[slot].ptr, __ATOMIC_ACQUIRE);
        if (entry_ptr == ptr) {
            if (out_len != NULL) {
                *out_len = (size_t)table[slot].len;
            }
            return true;
        }
        if (entry_ptr == NULL) {
            return false;
        }
        slot = (slot + 1u) % (size_t)table_cap;
    }
    return false;
}

static bool cheng_strmeta_reserve_table_unlocked(int32_t need_len) {
    int32_t next_cap = cheng_strmeta_table_cap;
    ChengStrMetaEntry* next_table = NULL;
    if (need_len < 1) need_len = 1;
    if (next_cap < 256) next_cap = 256;
    while ((int64_t)need_len * 10 >= (int64_t)next_cap * 7) {
        next_cap *= 2;
    }
    if (cheng_strmeta_table != NULL && next_cap <= cheng_strmeta_table_cap) {
        return true;
    }
    next_table = (ChengStrMetaEntry*)calloc((size_t)next_cap, sizeof(ChengStrMetaEntry));
    if (next_table == NULL) {
        return false;
    }
    for (int32_t i = 0; i < cheng_strmeta_len; ++i) {
        const char* entry_ptr = cheng_strmeta_entries[i].ptr;
        size_t slot = 0;
        if (entry_ptr == NULL) continue;
        slot = cheng_strmeta_hash_ptr(entry_ptr) % (size_t)next_cap;
        while (next_table[slot].ptr != NULL && next_table[slot].ptr != entry_ptr) {
            slot = (slot + 1u) % (size_t)next_cap;
        }
        next_table[slot] = cheng_strmeta_entries[i];
    }
    __atomic_store_n(&cheng_strmeta_table, next_table, __ATOMIC_RELEASE);
    __atomic_store_n(&cheng_strmeta_table_cap, next_cap, __ATOMIC_RELEASE);
    return true;
}

static void cheng_strmeta_put(const char* ptr, int32_t len) {
    bool have_slot = false;
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
        have_slot = true;
    }
    if (have_slot) {
        if (!cheng_strmeta_reserve_table_unlocked(cheng_strmeta_len)) {
            __atomic_store_n(&cheng_strmeta_table, NULL, __ATOMIC_RELEASE);
            __atomic_store_n(&cheng_strmeta_table_cap, 0, __ATOMIC_RELEASE);
        } else {
            size_t slot = cheng_strmeta_hash_ptr(ptr) % (size_t)cheng_strmeta_table_cap;
            while (cheng_strmeta_table[slot].ptr != NULL && cheng_strmeta_table[slot].ptr != ptr) {
                slot = (slot + 1u) % (size_t)cheng_strmeta_table_cap;
            }
            cheng_strmeta_table[slot].len = len;
            __atomic_store_n(&cheng_strmeta_table[slot].ptr, ptr, __ATOMIC_RELEASE);
        }
    }
    cheng_strmeta_release();
    cheng_strmeta_recent_put(ptr, (size_t)len);
}

WEAK void cheng_register_string_meta(const char* ptr, int32_t len) {
    cheng_strmeta_put(ptr, len);
}

static bool cheng_strmeta_get(const char* ptr, size_t* out_len) {
    if (ptr == NULL) return false;
    if (cheng_strmeta_recent_get(ptr, out_len)) {
        return true;
    }
    int32_t table_cap_snapshot = __atomic_load_n(&cheng_strmeta_table_cap, __ATOMIC_ACQUIRE);
    ChengStrMetaEntry* table_snapshot = __atomic_load_n(&cheng_strmeta_table, __ATOMIC_ACQUIRE);
    if (cheng_strmeta_probe_table_snapshot(ptr, table_snapshot, table_cap_snapshot, out_len)) {
        cheng_strmeta_recent_put(ptr, (out_len != NULL) ? *out_len : 0u);
        return true;
    }
    bool found = false;
    cheng_strmeta_acquire();
    if (cheng_strmeta_table != NULL && cheng_strmeta_table_cap > 0) {
        size_t found_len = 0u;
        if (cheng_strmeta_probe_table_snapshot(ptr, cheng_strmeta_table, cheng_strmeta_table_cap, &found_len)) {
            if (out_len != NULL) *out_len = found_len;
            cheng_strmeta_recent_put(ptr, found_len);
            found = true;
        }
    }
    if (!found) {
        for (int32_t i = cheng_strmeta_len - 1; i >= 0; --i) {
            if (cheng_strmeta_entries[i].ptr == ptr) {
                size_t len = (size_t)cheng_strmeta_entries[i].len;
                if (out_len != NULL) *out_len = len;
                cheng_strmeta_recent_put(ptr, len);
                found = true;
                break;
            }
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
    char** effective_argv = argv;
    int effective_argc = argc;
    driver_c_maybe_inject_env_cli(&effective_argc, &effective_argv);
    __cheng_setCmdLine((int32_t)effective_argc, (const char**)effective_argv);
    driver_c_capture_cmdline((int32_t)effective_argc, (void*)effective_argv);
    driver_c_sync_cli_env(effective_argc, effective_argv);
    for (int i = 1; i < effective_argc; ++i) {
        if (cheng_arg_is_help(effective_argv[i])) {
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

static char* driver_c_make_flag_value(const char* flag, const char* value) {
    size_t flag_len = (flag != NULL) ? strlen(flag) : 0u;
    size_t value_len = (value != NULL) ? strlen(value) : 0u;
    char* out = (char*)cheng_malloc((int32_t)(flag_len + value_len + 2u));
    if (out == NULL) return NULL;
    if (flag_len > 0) memcpy(out, flag, flag_len);
    out[flag_len] = ':';
    if (value_len > 0) memcpy(out + flag_len + 1u, value, value_len);
    out[flag_len + value_len + 1u] = '\0';
    cheng_strmeta_put(out, (int32_t)(flag_len + value_len + 1u));
    return out;
}

static int driver_c_match_flag_value(const char* arg, const char* flag, const char** value_out) {
    size_t flag_len;
    if (value_out != NULL) *value_out = NULL;
    if (arg == NULL || flag == NULL || flag[0] == '\0') return 0;
    flag_len = strlen(flag);
    if (strcmp(arg, flag) == 0) return 1;
    if (strncmp(arg, flag, flag_len) == 0 && (arg[flag_len] == ':' || arg[flag_len] == '=')) {
        if (value_out != NULL) *value_out = arg + flag_len + 1u;
        return 1;
    }
    return 0;
}

static int driver_c_collect_flag_value(int argc, char** argv, const char* flag, const char** value_out) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* inline_value = NULL;
        if (!driver_c_match_flag_value(arg, flag, &inline_value)) continue;
        if (inline_value != NULL) {
            if (value_out != NULL) *value_out = inline_value;
            return 1;
        }
        if (i + 1 < argc && argv[i + 1] != NULL) {
            if (value_out != NULL) *value_out = argv[i + 1];
            return 1;
        }
        if (value_out != NULL) *value_out = "";
        return 1;
    }
    return 0;
}

static int driver_c_collect_positional_input(int argc, char** argv, const char** value_out) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == NULL || arg[0] == '\0') continue;
        if (arg[0] != '-') {
            if (value_out != NULL) *value_out = arg;
            return 1;
        }
    }
    return 0;
}

static void driver_c_sync_env_value_flag(int argc, char** argv,
                                         const char* flag,
                                         const char* env_name) {
    const char* value = NULL;
    if (!driver_c_collect_flag_value(argc, argv, flag, &value)) return;
    if (value == NULL || env_name == NULL || env_name[0] == '\0') return;
    setenv(env_name, value, 1);
}

static void driver_c_sync_env_bool_flag(int argc, char** argv,
                                        const char* positive_flag,
                                        const char* negative_flag,
                                        const char* env_name) {
    const char* value = NULL;
    int seen = 0;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* inline_value = NULL;
        if (driver_c_match_flag_value(arg, positive_flag, &inline_value)) {
            seen = 1;
            value = (inline_value != NULL) ? inline_value : "1";
            continue;
        }
        if (negative_flag != NULL && strcmp(arg != NULL ? arg : "", negative_flag) == 0) {
            seen = 1;
            value = "0";
            continue;
        }
    }
    if (!seen || value == NULL || env_name == NULL || env_name[0] == '\0') return;
    setenv(env_name, value, 1);
}

static void driver_c_sync_env_enable_flag(int argc, char** argv,
                                          const char* flag,
                                          const char* env_name) {
    if (!driver_c_has_flag_value(argc, argv, flag)) return;
    if (env_name == NULL || env_name[0] == '\0') return;
    setenv(env_name, "1", 1);
}

static CHENG_MAYBE_UNUSED void driver_c_sync_cli_env(int argc, char** argv) {
    const char* input_value = NULL;
    const char* env_input = NULL;
    if (argc <= 0 || argv == NULL) return;

    driver_c_sync_env_value_flag(argc, argv, "--input", "BACKEND_INPUT");
    env_input = getenv("BACKEND_INPUT");
    if ((env_input == NULL || env_input[0] == '\0') &&
        driver_c_collect_positional_input(argc, argv, &input_value) &&
        input_value != NULL && input_value[0] != '\0') {
        setenv("BACKEND_INPUT", input_value, 1);
    }
    driver_c_sync_env_value_flag(argc, argv, "--output", "BACKEND_OUTPUT");
    driver_c_sync_env_value_flag(argc, argv, "--target", "BACKEND_TARGET");
    driver_c_sync_env_value_flag(argc, argv, "--linker", "BACKEND_LINKER");
    driver_c_sync_env_value_flag(argc, argv, "--emit", "BACKEND_EMIT");
    driver_c_sync_env_value_flag(argc, argv, "--frontend", "BACKEND_FRONTEND");
    driver_c_sync_env_value_flag(argc, argv, "--build-track", "BACKEND_BUILD_TRACK");
    driver_c_sync_env_value_flag(argc, argv, "--mm", "MM");
    driver_c_sync_env_value_flag(argc, argv, "--fn-sched", "BACKEND_FN_SCHED");
    driver_c_sync_env_value_flag(argc, argv, "--opt-level", "BACKEND_OPT_LEVEL");
    driver_c_sync_env_value_flag(argc, argv, "--ldflags", "BACKEND_LDFLAGS");
    driver_c_sync_env_value_flag(argc, argv, "--runtime-obj", "BACKEND_RUNTIME_OBJ");
    driver_c_sync_env_value_flag(argc, argv, "--generic-mode", "GENERIC_MODE");
    driver_c_sync_env_value_flag(argc, argv, "--generic-spec-budget", "GENERIC_SPEC_BUDGET");
    driver_c_sync_env_value_flag(argc, argv, "--generic-lowering", "GENERIC_LOWERING");
    driver_c_sync_env_value_flag(argc, argv, "--jobs", "BACKEND_JOBS");
    driver_c_sync_env_value_flag(argc, argv, "--fn-jobs", "BACKEND_FN_JOBS");
    driver_c_sync_env_value_flag(argc, argv, "--module-cache", "BACKEND_MODULE_CACHE");
    driver_c_sync_env_value_flag(argc, argv, "--sidecar-mode", "BACKEND_UIR_SIDECAR_MODE");
    driver_c_sync_env_value_flag(argc, argv, "--sidecar-bundle", "BACKEND_UIR_SIDECAR_BUNDLE");
    driver_c_sync_env_value_flag(argc, argv, "--sidecar-compiler", "BACKEND_UIR_SIDECAR_COMPILER");
    driver_c_sync_env_value_flag(argc, argv, "--sidecar-child-mode", "BACKEND_UIR_SIDECAR_CHILD_MODE");
    driver_c_sync_env_value_flag(argc, argv, "--sidecar-outer-compiler", "BACKEND_UIR_SIDECAR_OUTER_COMPILER");
    driver_c_sync_env_value_flag(argc, argv, "--borrow-ir", "BORROW_IR");
    {
        const char* emit_mode = getenv("BACKEND_EMIT");
        if (emit_mode != NULL && strcmp(emit_mode, "obj") == 0) {
            setenv("BACKEND_INTERNAL_ALLOW_EMIT_OBJ", "1", 1);
        }
    }
    {
        const char* sidecar_mode = getenv("BACKEND_UIR_SIDECAR_MODE");
        if (sidecar_mode != NULL && strcmp(sidecar_mode, "cheng") == 0) {
            setenv("BACKEND_UIR_SIDECAR_DISABLE", "0", 1);
            setenv("BACKEND_UIR_PREFER_SIDECAR", "1", 1);
            setenv("BACKEND_UIR_FORCE_SIDECAR", "1", 1);
        }
    }

    driver_c_sync_env_bool_flag(argc, argv, "--opt", "--no-opt", "BACKEND_OPT");
    driver_c_sync_env_bool_flag(argc, argv, "--opt2", "--no-opt2", "BACKEND_OPT2");
    driver_c_sync_env_bool_flag(argc, argv, "--multi", "--no-multi", "BACKEND_MULTI");
    driver_c_sync_env_bool_flag(argc, argv, "--multi-force", "--no-multi-force", "BACKEND_MULTI_FORCE");
    driver_c_sync_env_bool_flag(argc, argv, "--incremental", "--no-incremental", "BACKEND_INCREMENTAL");
    driver_c_sync_env_bool_flag(argc, argv, "--whole-program", "--no-whole-program", "BACKEND_WHOLE_PROGRAM");
    driver_c_sync_env_bool_flag(argc, argv, "--multi-module-cache", "--no-multi-module-cache", "BACKEND_MULTI_MODULE_CACHE");
    driver_c_sync_env_bool_flag(argc, argv, "--module-cache-unstable-allow", "--no-module-cache-unstable-allow", "BACKEND_MODULE_CACHE_UNSTABLE_ALLOW");
    driver_c_sync_env_bool_flag(argc, argv, "--no-runtime-c", NULL, "BACKEND_NO_RUNTIME_C");

    driver_c_sync_env_enable_flag(argc, argv, "--allow-no-main", "BACKEND_ALLOW_NO_MAIN");
    driver_c_sync_env_enable_flag(argc, argv, "--skip-global-init", "BACKEND_SKIP_GLOBAL_INIT");
}

static int driver_c_has_flag_value(int argc, char** argv, const char* flag) {
    if (flag == NULL || flag[0] == '\0') return 0;
    size_t n = strlen(flag);
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == NULL) continue;
        if (strcmp(arg, flag) == 0) return 1;
        if (strncmp(arg, flag, n) == 0 && (arg[n] == ':' || arg[n] == '=')) return 1;
    }
    return 0;
}

static int driver_c_has_positional_input(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == NULL || arg[0] == '\0') continue;
        if (arg[0] != '-') return 1;
    }
    return 0;
}

static CHENG_MAYBE_UNUSED void driver_c_maybe_inject_env_cli(int* argc_io, char*** argv_io) {
    int argc = (argc_io != NULL) ? *argc_io : 0;
    char** argv = (argv_io != NULL) ? *argv_io : NULL;
    if (argc <= 0 || argv == NULL) return;

    const char* env_input = getenv("BACKEND_INPUT");
    const char* env_output = getenv("BACKEND_OUTPUT");
    const char* env_target = getenv("BACKEND_TARGET");
    const char* env_linker = getenv("BACKEND_LINKER");

    int add_input = (env_input != NULL && env_input[0] != '\0' &&
                     !driver_c_has_flag_value(argc, argv, "--input") &&
                     !driver_c_has_positional_input(argc, argv)) ? 1 : 0;
    int add_output = (env_output != NULL && env_output[0] != '\0' &&
                      !driver_c_has_flag_value(argc, argv, "--output")) ? 1 : 0;
    int add_target = (env_target != NULL && env_target[0] != '\0' &&
                      !driver_c_has_flag_value(argc, argv, "--target")) ? 1 : 0;
    int add_linker = (env_linker != NULL && env_linker[0] != '\0' &&
                      !driver_c_has_flag_value(argc, argv, "--linker")) ? 1 : 0;

    int extra = add_input + add_output + add_target + add_linker;
    if (extra <= 0) return;

    char** out = (char**)cheng_malloc((int32_t)(argc + extra + 1) * (int32_t)sizeof(char*));
    if (out == NULL) return;
    for (int i = 0; i < argc; ++i) out[i] = argv[i];
    int w = argc;
    if (add_input) out[w++] = driver_c_make_flag_value("--input", env_input);
    if (add_output) out[w++] = driver_c_make_flag_value("--output", env_output);
    if (add_target) out[w++] = driver_c_make_flag_value("--target", env_target);
    if (add_linker) out[w++] = driver_c_make_flag_value("--linker", env_linker);
    out[w] = NULL;
    if (argc_io != NULL) *argc_io = w;
    if (argv_io != NULL) *argv_io = out;
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

WEAK int32_t driver_c_cli_param1_eq_bridge(ChengStrBridge expected) {
    ChengStrBridge arg1 = cheng_str_bridge_empty();
    if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) {
        const char* raw = cheng_saved_argv[1];
        if (raw == NULL) return 0;
        arg1 = cheng_str_bridge_from_ptr_flags(raw, 0);
        return driver_c_str_eq_bridge(arg1, expected);
    }
    if (__cheng_rt_paramCount == NULL || __cheng_rt_paramStrCopyBridge == NULL) {
        return 0;
    }
    if (__cheng_rt_paramCount() <= 1) {
        return 0;
    }
    arg1 = __cheng_rt_paramStrCopyBridge(1);
    return driver_c_str_eq_bridge(arg1, expected);
}

static int driver_c_flag_key_matches(const char* raw, ChengStrBridge key) {
    size_t key_len;
    if (raw == NULL || key.ptr == NULL || key.len < 0) {
        return 0;
    }
    key_len = (size_t)key.len;
    return strlen(raw) == key_len && memcmp(raw, key.ptr, key_len) == 0;
}

static int driver_c_flag_inline_value(const char* raw, ChengStrBridge key, const char** out_value) {
    size_t key_len;
    if (out_value == NULL) {
        return 0;
    }
    *out_value = NULL;
    if (raw == NULL || key.ptr == NULL || key.len < 0) {
        return 0;
    }
    key_len = (size_t)key.len;
    if (strncmp(raw, key.ptr, key_len) != 0) {
        return 0;
    }
    if (raw[key_len] == ':' || raw[key_len] == '=') {
        *out_value = raw + key_len + 1u;
        return 1;
    }
    return 0;
}

WEAK ChengStrBridge driver_c_read_flag_or_default_bridge(ChengStrBridge key, ChengStrBridge default_value) {
    int32_t argc = 0;
    int32_t i;
    if (cheng_saved_argc > 0 && cheng_saved_argv != NULL) {
        argc = cheng_saved_argc;
    } else if (__cheng_rt_paramCount != NULL) {
        argc = __cheng_rt_paramCount();
    }
    if (argc <= 1) {
        return default_value;
    }
    for (i = 1; i < argc; ++i) {
        const char* raw = NULL;
        const char* inline_value = NULL;
        if (cheng_saved_argc > 0 && cheng_saved_argv != NULL) {
            raw = cheng_saved_argv[i];
        } else if (__cheng_rt_paramStr != NULL) {
            raw = __cheng_rt_paramStr(i);
        }
        if (raw == NULL) {
            continue;
        }
        if (driver_c_flag_inline_value(raw, key, &inline_value)) {
            return cheng_str_bridge_from_ptr_flags(inline_value, 0);
        }
        if (driver_c_flag_key_matches(raw, key)) {
            const char* next_raw = "";
            if (i + 1 >= argc) {
                return default_value;
            }
            if (cheng_saved_argc > 0 && cheng_saved_argv != NULL) {
                next_raw = cheng_saved_argv[i + 1];
            } else if (__cheng_rt_paramStr != NULL) {
                next_raw = __cheng_rt_paramStr(i + 1);
            }
            if (next_raw == NULL) {
                next_raw = "";
            }
            return cheng_str_bridge_from_ptr_flags(next_raw, 0);
        }
    }
    return default_value;
}

WEAK int32_t driver_c_read_flag_value_bridge(ChengStrBridge key, ChengStrBridge* out_value) {
    int32_t argc = 0;
    int32_t i;
    if (out_value == NULL) {
        return 0;
    }
    *out_value = cheng_str_bridge_empty();
    if (cheng_saved_argc > 0 && cheng_saved_argv != NULL) {
        argc = cheng_saved_argc;
    } else if (__cheng_rt_paramCount != NULL) {
        argc = __cheng_rt_paramCount();
    }
    if (argc <= 1) {
        return 0;
    }
    for (i = 1; i < argc; ++i) {
        const char* raw = NULL;
        const char* inline_value = NULL;
        if (cheng_saved_argc > 0 && cheng_saved_argv != NULL) {
            raw = cheng_saved_argv[i];
        } else if (__cheng_rt_paramStr != NULL) {
            raw = __cheng_rt_paramStr(i);
        }
        if (raw == NULL) {
            continue;
        }
        if (driver_c_flag_inline_value(raw, key, &inline_value)) {
            *out_value = cheng_str_bridge_from_ptr_flags(inline_value, 0);
            return 1;
        }
        if (driver_c_flag_key_matches(raw, key)) {
            const char* next_raw = "";
            if (i + 1 >= argc) {
                return 0;
            }
            if (cheng_saved_argc > 0 && cheng_saved_argv != NULL) {
                next_raw = cheng_saved_argv[i + 1];
            } else if (__cheng_rt_paramStr != NULL) {
                next_raw = __cheng_rt_paramStr(i + 1);
            }
            if (next_raw == NULL) {
                next_raw = "";
            }
            *out_value = cheng_str_bridge_from_ptr_flags(next_raw, 0);
            return 1;
        }
    }
    return 0;
}

WEAK int32_t driver_c_read_int32_flag_or_default_bridge(ChengStrBridge key, int32_t default_value) {
    ChengStrBridge raw = driver_c_read_flag_or_default_bridge(key, cheng_str_bridge_empty());
    char* end = NULL;
    long parsed = 0;
    if (raw.ptr == NULL || raw.len <= 0) {
        return default_value;
    }
    parsed = strtol(raw.ptr, &end, 10);
    if (end == raw.ptr || *end != '\0') {
        return default_value;
    }
    if (parsed < -2147483648L || parsed > 2147483647L) {
        return default_value;
    }
    return (int32_t)parsed;
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
#if CHENG_TARGET_APPLE_MOBILE
    (void)outputPath;
    (void)objPath;
    (void)target;
    (void)linker;
    return -22;
#else
    const char* targetText = (target != NULL) ? target : "";
    const char* linkerText = (linker != NULL && linker[0] != '\0') ? linker : "system";
    const char* runtimeC = "/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c";
    const char* runtimeObj = getenv("BACKEND_RUNTIME_OBJ");
    const char* runtimeInput = runtimeC;
    const char* cflags = getenv("BACKEND_CFLAGS");
    const char* ldflags = getenv("BACKEND_LDFLAGS");
    const char* cflagsText = (cflags != NULL) ? cflags : "";
    const char* ldflagsText = (ldflags != NULL) ? ldflags : "";
    const char* fastDarwinAdhocRaw = getenv("BACKEND_DARWIN_FAST_ADHOC");
    char fullLdflags[512];
    char extraLdflags[96];
    fullLdflags[0] = '\0';
    extraLdflags[0] = '\0';
    if (outputPath == NULL || outputPath[0] == '\0') return -20;
    if (objPath == NULL || objPath[0] == '\0') return -21;
    if (strcmp(linkerText, "system") != 0) return -22;
    if (driver_c_env_bool("BACKEND_NO_RUNTIME_C", 0) != 0 &&
        runtimeObj != NULL && runtimeObj[0] != '\0' &&
        access(runtimeObj, R_OK) == 0) {
        runtimeInput = runtimeObj;
    }
    const char* base = strrchr(objPath, '/');
    const char* objName = (base != NULL) ? (base + 1) : objPath;
    int useDriverEntryShim = strstr(objName, "backend_driver") != NULL ||
                             driver_c_env_bool("BACKEND_FORCE_DRIVER_ENTRY_SHIM", 0) != 0;
    int needsDarwinCodesign = 0;
    int fastDarwinAdhoc =
        (fastDarwinAdhocRaw != NULL && fastDarwinAdhocRaw[0] != '\0')
            ? driver_c_env_bool("BACKEND_DARWIN_FAST_ADHOC", 0)
            : driver_c_env_bool("BACKEND_FAST_DEV_PROFILE", 0);
    if (strstr(targetText, "darwin") != NULL || strstr(targetText, "apple-darwin") != NULL) {
        if (strstr(ldflagsText, "-Wl,-no_adhoc_codesign") != NULL) fastDarwinAdhoc = 0;
        needsDarwinCodesign = fastDarwinAdhoc ? 0 : 1;
        if (!fastDarwinAdhoc && strstr(ldflagsText, "-Wl,-no_adhoc_codesign") == NULL) {
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
    size_t need = strlen(objPath) + strlen(runtimeInput) + strlen(outputPath) +
                  strlen(cflagsText) + strlen(ldflagsText) + strlen(extraLdflags) + 128u;
    char* cmd = (char*)malloc(need);
    if (cmd == NULL) return -23;
    if (useDriverEntryShim) {
        snprintf(cmd, need, "cc -DCHENG_BACKEND_DRIVER_ENTRY_SHIM %s '%s' '%s' %s -o '%s'",
                 cflagsText, objPath, runtimeInput, fullLdflags, outputPath);
    } else {
        snprintf(cmd, need, "cc %s '%s' '%s' %s -o '%s'",
                 cflagsText, objPath, runtimeInput, fullLdflags, outputPath);
    }
    if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
        fprintf(stderr, "[driver_c_link_tmp_obj_default] obj=%s out=%s target=%s sidecar=<bundle-only>\n",
                objPath, outputPath, targetText);
        fprintf(stderr, "[driver_c_link_tmp_obj_default] cmd=%s\n", cmd);
        fprintf(stderr, "[driver_c_link_tmp_obj_default] darwin_fast_adhoc=%d needs_codesign=%d\n",
                fastDarwinAdhoc, needsDarwinCodesign);
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
#endif
}

WEAK int32_t driver_c_link_tmp_obj_system(const char* outputPath, const char* objPath,
                                          const char* target) {
    return driver_c_link_tmp_obj_default(outputPath, objPath, target, "system");
}

typedef int32_t (*cheng_driver_bool_dummy_fn)(int32_t);

static cheng_build_module_stage1_target_facade_fn
driver_c_resolve_build_module_stage1_target_retained(void) {
    cheng_build_module_stage1_target_facade_fn fn =
        (cheng_build_module_stage1_target_facade_fn)dlsym(
            RTLD_DEFAULT, "driver_export_buildModuleFromFileStage1TargetRetained");
    if (fn != NULL) return fn;
    return (cheng_build_module_stage1_target_facade_fn)dlsym(
        RTLD_DEFAULT, "driver_buildModuleFromFileStage1TargetRetained");
}

static cheng_build_module_stage1_target_facade_fn
driver_c_resolve_build_module_stage1_target_facade(void) {
    cheng_build_module_stage1_target_facade_fn fn =
        (cheng_build_module_stage1_target_facade_fn)dlsym(
            RTLD_DEFAULT, "driver_export_uirBuildModuleFromFileStage1TargetOrPanic");
    if (fn != NULL) return fn;
    return (cheng_build_module_stage1_target_facade_fn)dlsym(
        RTLD_DEFAULT, "uirBuildModuleFromFileStage1TargetOrPanic");
}

static cheng_build_module_stage1_facade_fn
driver_c_resolve_build_module_stage1_retained(void) {
    cheng_build_module_stage1_facade_fn fn =
        (cheng_build_module_stage1_facade_fn)dlsym(
            RTLD_DEFAULT, "driver_export_buildModuleFromFileStage1Retained");
    if (fn != NULL) return fn;
    return (cheng_build_module_stage1_facade_fn)dlsym(
        RTLD_DEFAULT, "driver_buildModuleFromFileStage1Retained");
}

static cheng_build_module_stage1_fn driver_c_resolve_build_module_stage1_raw(void) {
    return (cheng_build_module_stage1_fn)dlsym(RTLD_DEFAULT, "uirCoreBuildModuleFromFileStage1OrPanic");
}

static cheng_build_module_stage1_facade_fn driver_c_resolve_build_module_stage1_facade(void) {
    cheng_build_module_stage1_facade_fn fn =
        (cheng_build_module_stage1_facade_fn)dlsym(
            RTLD_DEFAULT, "driver_export_uirBuildModuleFromFileStage1OrPanic");
    if (fn != NULL) return fn;
    return (cheng_build_module_stage1_facade_fn)dlsym(
        RTLD_DEFAULT, "uirBuildModuleFromFileStage1OrPanic");
}

static cheng_build_emit_obj_stage1_target_fn driver_c_resolve_build_emit_obj_stage1_target(void) {
    cheng_build_emit_obj_stage1_target_fn fn =
        (cheng_build_emit_obj_stage1_target_fn)dlsym(
            RTLD_DEFAULT, "driver_export_build_emit_obj_from_file_stage1_target_impl");
    if (fn != NULL) return fn;
    return (cheng_build_emit_obj_stage1_target_fn)dlsym(
        RTLD_DEFAULT, "driver_build_emit_obj_from_file_stage1_target_impl");
}

static CHENG_MAYBE_UNUSED int driver_c_runtime_path_contains(const char* path, const char* needle) {
    return path != NULL && needle != NULL && strstr(path, needle) != NULL;
}

static int driver_c_runtime_resolve_self_path(char* out, size_t out_cap) {
    char raw[PATH_MAX];
    char* resolved = NULL;
    if (out == NULL || out_cap == 0) return 0;
    out[0] = '\0';
#if defined(__APPLE__)
    {
        uint32_t size = (uint32_t)sizeof(raw);
        if (_NSGetExecutablePath(raw, &size) == 0) {
            resolved = realpath(raw, out);
            if (resolved != NULL) return 1;
            if (strlen(raw) < out_cap) {
                snprintf(out, out_cap, "%s", raw);
                return 1;
            }
        }
    }
#elif !defined(_WIN32)
    {
        ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1);
        if (n > 0 && (size_t)n < sizeof(raw)) {
            raw[n] = '\0';
            resolved = realpath(raw, out);
            if (resolved != NULL) return 1;
            if ((size_t)n < out_cap) {
                snprintf(out, out_cap, "%s", raw);
                return 1;
            }
        }
    }
#endif
    return 0;
}

static int32_t driver_c_sidecar_builds_requested(void) {
    const char* preferEnv = getenv("BACKEND_UIR_PREFER_SIDECAR");
    if (preferEnv != NULL && preferEnv[0] != '\0') {
        if (preferEnv[0] == '0' && preferEnv[1] == '\0') return 0;
        return 1;
    }
    if (driver_c_env_bool("BACKEND_UIR_FORCE_SIDECAR", 0) != 0) {
        return 1;
    }
    cheng_driver_bool_dummy_fn preferSidecarFn =
        (cheng_driver_bool_dummy_fn)dlsym(RTLD_DEFAULT, "driver_export_prefer_sidecar_builds");
    if (preferSidecarFn != NULL) {
        int32_t prefer = preferSidecarFn(0);
        driver_c_diagf("[driver_c_prefer_sidecar_builds] export=%d\n", prefer);
        if (prefer != 0) return 1;
    }
    return 0;
}

static int32_t driver_c_prefer_sidecar_builds(void) {
    int32_t prefer = driver_c_sidecar_builds_requested();
    return prefer;
}

WEAK int32_t driver_c_build_emit_obj_default(const char* inputPath, const char* target,
                                             const char* outputPath) {
    int32_t preferSidecar = driver_c_prefer_sidecar_builds();
    cheng_build_emit_obj_stage1_target_fn buildEmitObjDirect =
        driver_c_resolve_build_emit_obj_stage1_target();
    if (inputPath == NULL || inputPath[0] == '\0') return -30;
    if (target == NULL || target[0] == '\0') return -31;
    if (outputPath == NULL || outputPath[0] == '\0') return -32;
    if (preferSidecar == 0 && buildEmitObjDirect != NULL &&
        driver_c_build_emit_obj_dispatch_depth == 0) {
        int32_t directRc;
        driver_c_build_emit_obj_dispatch_depth += 1;
        directRc = buildEmitObjDirect(inputPath, target, outputPath);
        driver_c_build_emit_obj_dispatch_depth -= 1;
        driver_c_diagf("[driver_c_build_emit_obj_default] direct_build_emit_rc=%d\n", directRc);
        if (directRc != 0) return directRc;
        return driver_c_finish_emit_obj(outputPath);
    }
    if (preferSidecar == 0 && buildEmitObjDirect != NULL &&
        driver_c_build_emit_obj_dispatch_depth != 0) {
        driver_c_diagf("[driver_c_build_emit_obj_default] direct_build_emit_reentry_skip depth=%d\n",
                       driver_c_build_emit_obj_dispatch_depth);
    }
    void* module = driver_c_build_module_stage1(inputPath, target);
    if (module == NULL) return -33;
    int32_t emitRc = driver_c_emit_obj_default(module, target, outputPath);
    if (emitRc != 0) return emitRc;
    return driver_c_finish_emit_obj(outputPath);
}

static CHENG_MAYBE_UNUSED int driver_c_scoped_sidecar_disable_begin(const char** oldRaw, int* hadOld) {
    const char* cur = getenv("BACKEND_UIR_SIDECAR_DISABLE");
    if (oldRaw != NULL) *oldRaw = cur;
    if (hadOld != NULL) *hadOld = (cur != NULL && cur[0] != '\0') ? 1 : 0;
    return setenv("BACKEND_UIR_SIDECAR_DISABLE", "1", 1);
}

static CHENG_MAYBE_UNUSED void driver_c_scoped_sidecar_disable_end(const char* oldRaw, int hadOld) {
    if (hadOld != 0 && oldRaw != NULL && oldRaw[0] != '\0') {
        setenv("BACKEND_UIR_SIDECAR_DISABLE", oldRaw, 1);
    } else {
        unsetenv("BACKEND_UIR_SIDECAR_DISABLE");
    }
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
    int32_t allowFacadeDispatch = (driver_c_build_module_dispatch_depth == 0);
    driver_c_diagf("[driver_c_build_module_stage1] input='%s' target='%s'\n",
                   inputPath != NULL ? inputPath : "", target != NULL ? target : "");
    if (inputPath == NULL || inputPath[0] == '\0') {
        driver_c_diagf("[driver_c_build_module_stage1] input_missing\n");
        return NULL;
    }
    if (target == NULL) target = "";
    int32_t preferSidecar = driver_c_prefer_sidecar_builds();
    if (driver_c_env_bool("BACKEND_UIR_SIDECAR_DISABLE", 0) != 0) {
        void* directModule = driver_c_build_module_stage1_direct(inputPath, target);
        driver_c_diagf("[driver_c_build_module_stage1] sidecar_disabled direct_module=%p\n",
                       directModule);
        if (directModule != NULL) return driver_c_module_wrap(directModule, CHENG_DRIVER_MODULE_KIND_RAW);
        driver_c_diagf("[driver_c_build_module_stage1] sidecar_disabled direct_failed\n");
        return NULL;
    }
    if (preferSidecar == 0) {
        cheng_build_module_stage1_fn buildModuleStage1 =
            driver_c_resolve_build_module_stage1_raw();
        if (buildModuleStage1 != NULL) {
            void* module = buildModuleStage1(inputPath, target);
            driver_c_diagf("[driver_c_build_module_stage1] weak_builder_module=%p\n", module);
            if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
        }
    }
    if (preferSidecar == 0 && allowFacadeDispatch != 0) {
        cheng_build_module_stage1_target_facade_fn buildModuleStage1TargetRetained =
            driver_c_resolve_build_module_stage1_target_retained();
        if (buildModuleStage1TargetRetained != NULL) {
            void* module;
            driver_c_build_module_dispatch_depth += 1;
            module = buildModuleStage1TargetRetained(inputPath, target);
            driver_c_build_module_dispatch_depth -= 1;
            driver_c_diagf("[driver_c_build_module_stage1] dlsym_retained_target_module=%p\n", module);
            if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RETAINED);
        }
        {
            cheng_build_module_stage1_target_facade_fn buildModuleStage1Target =
                driver_c_resolve_build_module_stage1_target_facade();
            if (buildModuleStage1Target != NULL) {
                void* module;
                driver_c_build_module_dispatch_depth += 1;
                module = buildModuleStage1Target(inputPath, target);
                driver_c_build_module_dispatch_depth -= 1;
                driver_c_diagf("[driver_c_build_module_stage1] dlsym_facade_target_module=%p\n", module);
                if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
            }
        }
        {
            cheng_build_module_stage1_facade_fn buildModuleStage1Retained =
                driver_c_resolve_build_module_stage1_retained();
            if (buildModuleStage1Retained != NULL) {
                void* module;
                driver_c_build_module_dispatch_depth += 1;
                module = buildModuleStage1Retained(inputPath);
                driver_c_build_module_dispatch_depth -= 1;
                driver_c_diagf("[driver_c_build_module_stage1] dlsym_retained_module=%p\n", module);
                if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RETAINED);
            }
        }
        {
            cheng_build_module_stage1_fn buildModuleStage1 =
                driver_c_resolve_build_module_stage1_raw();
            if (buildModuleStage1 != NULL) {
                void* module = buildModuleStage1(inputPath, target);
                driver_c_diagf("[driver_c_build_module_stage1] dlsym_builder_module=%p\n", module);
                if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
            }
        }
        {
            cheng_build_module_stage1_facade_fn buildModuleStage1Facade =
                driver_c_resolve_build_module_stage1_facade();
            if (buildModuleStage1Facade != NULL) {
                void* module;
                driver_c_build_module_dispatch_depth += 1;
                module = buildModuleStage1Facade(inputPath);
                driver_c_build_module_dispatch_depth -= 1;
                driver_c_diagf("[driver_c_build_module_stage1] dlsym_facade_module=%p\n", module);
                if (module != NULL) return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
            }
        }
    }
    if (preferSidecar == 0 && allowFacadeDispatch == 0) {
        driver_c_diagf("[driver_c_build_module_stage1] facade_reentry_skip depth=%d\n",
                       driver_c_build_module_dispatch_depth);
    }
    cheng_build_active_module_ptrs_fn buildActiveModulePtrsFn =
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
    int32_t preferSidecar = driver_c_prefer_sidecar_builds();
    int32_t sidecarDisabled = driver_c_env_bool("BACKEND_UIR_SIDECAR_DISABLE", 0);
    int32_t allowDirectExports = (preferSidecar == 0 || sidecarDisabled != 0);
    int32_t allowFacadeDispatch = (driver_c_build_module_dispatch_depth == 0);
    if (allowDirectExports != 0) {
        cheng_build_module_stage1_fn buildModuleStage1 =
            driver_c_resolve_build_module_stage1_raw();
        if (buildModuleStage1 != NULL) {
            void* module = buildModuleStage1(inputPath, target);
            driver_c_diagf("[driver_c_build_module_stage1_direct] weak_builder_module=%p\n", module);
            if (module != NULL) return module;
        }
    }
    if (allowDirectExports != 0 && allowFacadeDispatch != 0) {
        cheng_build_module_stage1_target_facade_fn buildModuleStage1TargetRetained =
            driver_c_resolve_build_module_stage1_target_retained();
        if (buildModuleStage1TargetRetained != NULL) {
            void* module;
            driver_c_build_module_dispatch_depth += 1;
            module = buildModuleStage1TargetRetained(inputPath, target);
            driver_c_build_module_dispatch_depth -= 1;
            driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_retained_target_module=%p\n",
                           module);
            if (module != NULL) return module;
        }
    }
    if (allowDirectExports != 0 && allowFacadeDispatch != 0) {
        cheng_build_module_stage1_target_facade_fn buildModuleStage1Target =
            driver_c_resolve_build_module_stage1_target_facade();
        if (buildModuleStage1Target != NULL) {
            void* module;
            driver_c_build_module_dispatch_depth += 1;
            module = buildModuleStage1Target(inputPath, target);
            driver_c_build_module_dispatch_depth -= 1;
            driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_facade_target_module=%p\n",
                           module);
            if (module != NULL) return module;
        }
    }
    if (allowDirectExports != 0 && allowFacadeDispatch != 0) {
        cheng_build_module_stage1_facade_fn buildModuleStage1Retained =
            driver_c_resolve_build_module_stage1_retained();
        if (buildModuleStage1Retained != NULL) {
            void* module;
            driver_c_build_module_dispatch_depth += 1;
            module = buildModuleStage1Retained(inputPath);
            driver_c_build_module_dispatch_depth -= 1;
            driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_retained_module=%p\n", module);
            if (module != NULL) return module;
        }
    }
    if (allowDirectExports != 0) {
        cheng_build_module_stage1_fn buildModuleStage1 =
            driver_c_resolve_build_module_stage1_raw();
        if (buildModuleStage1 != NULL) {
            void* module = buildModuleStage1(inputPath, target);
            driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_builder_module=%p\n", module);
            if (module != NULL) return module;
        }
    }
    if (allowDirectExports != 0 && allowFacadeDispatch != 0) {
        cheng_build_module_stage1_facade_fn buildModuleStage1Facade =
            driver_c_resolve_build_module_stage1_facade();
        if (buildModuleStage1Facade != NULL) {
            void* module;
            driver_c_build_module_dispatch_depth += 1;
            module = buildModuleStage1Facade(inputPath);
            driver_c_build_module_dispatch_depth -= 1;
            driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_facade_module=%p\n", module);
            if (module != NULL) return module;
        }
    }
    if (allowDirectExports != 0 && allowFacadeDispatch == 0) {
        driver_c_diagf("[driver_c_build_module_stage1_direct] facade_reentry_skip depth=%d\n",
                       driver_c_build_module_dispatch_depth);
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
static bool cheng_safe_raw_cstr_view(const char* s, const char** out_ptr, size_t* out_len);

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

WEAK int32_t libc_socket(int32_t domain, int32_t typ, int32_t protocol) {
    return socket(domain, typ, protocol);
}

WEAK int32_t libc_fcntl(int32_t fd, int32_t cmd, int32_t arg) {
    return fcntl(fd, cmd, arg);
}

WEAK int32_t libc_bind(int32_t fd, void* addr, int32_t len) {
    return bind(fd, (const struct sockaddr*)addr, (socklen_t)len);
}

WEAK int32_t libc_sendto(int32_t fd, void* buf, int32_t len, int32_t flags, void* addr, int32_t addrlen) {
    return (int32_t)sendto(fd, buf, (size_t)len, flags, (const struct sockaddr*)addr, (socklen_t)addrlen);
}

WEAK int32_t libc_recvfrom(int32_t fd, void* buf, int32_t len, int32_t flags, void* addr, int32_t* addrlen) {
    socklen_t raw_len = (addrlen != NULL && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
    int32_t rc = (int32_t)recvfrom(fd, buf, (size_t)len, flags, (struct sockaddr*)addr, addrlen != NULL ? &raw_len : NULL);
    if (addrlen != NULL) {
        *addrlen = (int32_t)raw_len;
    }
    return rc;
}

WEAK int32_t libc_getsockname(int32_t fd, void* addr, int32_t* addrlen) {
    socklen_t raw_len = (addrlen != NULL && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
    int32_t rc = getsockname(fd, (struct sockaddr*)addr, addrlen != NULL ? &raw_len : NULL);
    if (addrlen != NULL) {
        *addrlen = (int32_t)raw_len;
    }
    return rc;
}

WEAK int32_t libc_setsockopt(int32_t fd, int32_t level, int32_t optname, void* optval, int32_t optlen) {
    return setsockopt(fd, level, optname, optval, (socklen_t)optlen);
}

WEAK int32_t libc_inet_pton(int32_t family, const char* src, void* dst) {
    return inet_pton(family, src, dst);
}

WEAK char* libc_inet_ntop(int32_t family, void* src, char* dst, int32_t size) {
    return (char*)inet_ntop(family, src, dst, (socklen_t)size);
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

static CHENG_MAYBE_UNUSED ChengMemBlock* cheng_mem_find_block_any_scan(void* p) {
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
WEAK void cheng_v3_panic_cstring_and_exit(const char* text) {
    fprintf(stderr, "%s\n", (text != NULL && text[0] != '\0') ? text : "panic");
    fflush(stderr);
    cheng_dump_backtrace_if_enabled();
    cheng_exit(1);
}

WEAK void cheng_v3_native_dump_backtrace_and_exit(const char* reason, int32_t code) {
    const char* safe = (reason != NULL && reason[0] != '\0') ? reason : "panic";
    fflush(stderr);
    cheng_dump_backtrace_now(safe);
    cheng_exit(code != 0 ? code : 1);
}

void cheng_bounds_check(int32_t len, int32_t idx) {
    if (idx < 0 || idx >= len) {
        fprintf(stderr, "[cheng] bounds check failed: idx=%d len=%d\n", idx, len);
        fflush(stderr);
        cheng_dump_backtrace_now("bounds");
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
    int32_t actual_elem_size = cheng_seq_elem_size_compat_get(buffer, elem_size);
    return cheng_index_ptr(buffer, len, idx, actual_elem_size, __builtin_return_address(0));
}

void* cheng_seq_set(void* buffer, int32_t len, int32_t idx, int32_t elem_size) {
    int32_t actual_elem_size = cheng_seq_elem_size_compat_get(buffer, elem_size);
    return cheng_index_ptr(buffer, len, idx, actual_elem_size, __builtin_return_address(0));
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

WEAK void cheng_error_info_bridge_ok(void* out_raw) {
    if (!out_raw) {
        return;
    }
    ChengErrorInfoBridgeCompat* out = (ChengErrorInfoBridgeCompat*)out_raw;
    out->code = 0;
    out->msg = cheng_str_bridge_empty();
}

WEAK void cheng_error_info_bridge_new(void* out_raw, ChengStrBridge msg) {
    if (!out_raw) {
        return;
    }
    ChengErrorInfoBridgeCompat* out = (ChengErrorInfoBridgeCompat*)out_raw;
    out->code = 0;
    out->msg = msg;
}

WEAK void cheng_error_info_bridge_copy(void* out_raw, int32_t code, ChengStrBridge msg) {
    if (!out_raw) {
        return;
    }
    ChengErrorInfoBridgeCompat* out = (ChengErrorInfoBridgeCompat*)out_raw;
    out->code = code;
    out->msg = msg;
}

WEAK void cheng_error_info_bridge_copy_from(void* out_raw, ChengErrorInfoBridgeCompat src) {
    if (!out_raw) {
        return;
    }
    ChengErrorInfoBridgeCompat* out = (ChengErrorInfoBridgeCompat*)out_raw;
    *out = src;
}

WEAK int32_t cheng_error_info_bridge_code(ChengErrorInfoBridgeCompat src) {
    return src.code;
}

WEAK void cheng_error_info_bridge_msg_into(void* out_raw, ChengErrorInfoBridgeCompat src) {
    if (!out_raw) {
        return;
    }
    ChengStrBridge* out = (ChengStrBridge*)out_raw;
    *out = src.msg;
}

WEAK ChengStrBridge cheng_error_info_bridge_msg(ChengErrorInfoBridgeCompat src) {
    return src.msg;
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
    int32_t actual_elem_size = cheng_seq_elem_size_compat_get(seq_ptr, elem_size);
    if (!seq || elem_size <= 0) {
        return cheng_index_ptr(NULL, 0, idx, actual_elem_size, __builtin_return_address(0));
    }
    if (idx < 0) {
        return cheng_index_ptr(seq->buffer, seq->len, idx, actual_elem_size, __builtin_return_address(0));
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
        size_t bytes = (size_t)new_cap * (size_t)actual_elem_size;
        void* new_buf = realloc(seq->buffer, bytes);
        if (!new_buf) {
            return cheng_index_ptr(seq->buffer, seq->len, idx, actual_elem_size, __builtin_return_address(0));
        }

        if (new_cap > old_cap) {
            size_t old_bytes = (size_t)old_cap * (size_t)actual_elem_size;
            size_t grow_bytes = (size_t)(new_cap - old_cap) * (size_t)actual_elem_size;
            memset((uint8_t*)new_buf + old_bytes, 0, grow_bytes);
        }
        seq->buffer = new_buf;
        seq->cap = new_cap;
    }

    if (need > seq->len) {
        seq->len = need;
    }
    cheng_seq_elem_size_compat_register(seq_ptr, seq->buffer, actual_elem_size);
    return cheng_index_ptr(seq->buffer, seq->len, idx, actual_elem_size, __builtin_return_address(0));
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
static void* cheng_thread_runtime_handle CHENG_MAYBE_UNUSED = NULL;
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
#if defined(__APPLE__) && UINTPTR_MAX > 0xffffffffu && !CHENG_TARGET_APPLE_MOBILE
typedef struct {
    mach_vm_address_t addr;
    mach_vm_size_t size;
    int readonly;
    int valid;
} cheng_cstr_region_cache_entry;

static cheng_cstr_region_cache_entry cheng_cstr_region_cache[16];

static bool cheng_find_cached_region(uintptr_t raw, mach_vm_address_t* out_addr, mach_vm_size_t* out_size,
                                     int* out_readonly) {
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
    if (out_readonly != NULL) *out_readonly = entry->readonly;
    return true;
}

static void cheng_store_cached_region(uintptr_t raw, mach_vm_address_t addr, mach_vm_size_t size, int readonly) {
    size_t slot = (size_t)((raw >> 12) & 15u);
    cheng_cstr_region_cache[slot].addr = addr;
    cheng_cstr_region_cache[slot].size = size;
    cheng_cstr_region_cache[slot].readonly = readonly;
    cheng_cstr_region_cache[slot].valid = 1;
}
#endif

static bool cheng_safe_cstr_view_impl(const char* s, bool allow_bridge, const char** out_ptr, size_t* out_len) {
    if (out_ptr != NULL) *out_ptr = "";
    if (out_len != NULL) *out_len = 0u;
    if (s == NULL) {
        return true;
    }
    uintptr_t raw = (uintptr_t)s;
    if (allow_bridge && cheng_probably_valid_ptr(s)) {
        const ChengStrBridge* bridge = (const ChengStrBridge*)s;
        if (cheng_probably_valid_str_bridge(bridge)) {
            if (out_ptr != NULL) *out_ptr = bridge->ptr != NULL ? bridge->ptr : "";
            if (out_len != NULL) *out_len = bridge->ptr != NULL ? (size_t)bridge->len : 0u;
            return true;
        }
    }
    size_t known_len = 0u;
    if (cheng_strmeta_get(s, &known_len)) {
        if (out_ptr != NULL) *out_ptr = s;
        if (out_len != NULL) *out_len = known_len;
        return true;
    }
#if defined(__APPLE__) && UINTPTR_MAX > 0xffffffffu && !CHENG_TARGET_APPLE_MOBILE
    if (raw < (uintptr_t)0x1000ULL) {
        return false;
    }
    mach_vm_address_t region_addr = 0;
    mach_vm_size_t region_size = 0;
    int region_readonly = 0;
    if (!cheng_find_cached_region(raw, &region_addr, &region_size, &region_readonly)) {
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
        region_readonly = ((info.protection & VM_PROT_WRITE) == 0) ? 1 : 0;
        cheng_store_cached_region(raw, region_addr, region_size, region_readonly);
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
            if (region_readonly && i <= (size_t)INT32_MAX) {
                cheng_strmeta_put(s, (int32_t)i);
            }
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

static bool cheng_safe_cstr_view(const char* s, const char** out_ptr, size_t* out_len) {
    return cheng_safe_cstr_view_impl(s, true, out_ptr, out_len);
}

static bool cheng_safe_raw_cstr_view(const char* s, const char** out_ptr, size_t* out_len) {
    return cheng_safe_cstr_view_impl(s, false, out_ptr, out_len);
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
    if (!cheng_safe_raw_cstr_view(ptr, &safe, &n)) {
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

static bool cheng_str_bridge_view(ChengStrBridge bridge, const char** out_ptr, size_t* out_len) {
    if (out_ptr != NULL) *out_ptr = "";
    if (out_len != NULL) *out_len = 0u;
    if (cheng_probably_valid_str_bridge(&bridge)) {
        if (out_ptr != NULL) *out_ptr = bridge.ptr != NULL ? bridge.ptr : "";
        if (out_len != NULL) *out_len = bridge.ptr != NULL ? (size_t)bridge.len : 0u;
        return true;
    }
    return cheng_safe_raw_cstr_view(bridge.ptr, out_ptr, out_len);
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

WEAK int32_t cheng_cstrlen(const char* s) {
    return cheng_strlen((char*)s);
}

WEAK int32_t min(int32_t a, int32_t b) {
    return (a < b) ? a : b;
}

WEAK ChengStrBridge load(const ChengStrBridge* p) {
    ChengStrBridge out;
    memset(&out, 0, sizeof(out));
    if (p != NULL) {
        out = *p;
    }
    return out;
}

WEAK void cheng_str_bridge_load_into(void* out_raw, const ChengStrBridge* p) {
    ChengStrBridge* out = (ChengStrBridge*)out_raw;
    if (out == NULL) {
        return;
    }
    *out = load(p);
}

WEAK void store(ChengStrBridge* p, ChengStrBridge val) {
    if (p == NULL) {
        return;
    }
    *p = val;
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
    if (!cheng_safe_raw_cstr_view(s, &safe, &n)) {
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
    if (!cheng_safe_raw_cstr_view(s, &safe, &n)) {
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
WEAK ChengStrBridge driver_c_str_slice_bridge(ChengStrBridge s, int32_t start, int32_t count) {
    const char* safe = "";
    size_t n = 0;
    size_t s0 = 0;
    size_t need = 0;
    if (!cheng_str_bridge_view(s, &safe, &n)) {
        safe = "";
        n = 0;
    }
    if (count <= 0 || n == 0u) {
        return cheng_str_bridge_from_owned(cheng_copy_string_bytes("", 0u));
    }
    s0 = start < 0 ? 0u : (size_t)start;
    if (s0 >= n) {
        return cheng_str_bridge_from_owned(cheng_copy_string_bytes("", 0u));
    }
    need = (size_t)count;
    if (s0 + need > n) {
        need = n - s0;
    }
    return cheng_str_bridge_from_owned(cheng_copy_string_bytes(safe + s0, need));
}
WEAK char* driver_c_char_to_str(int32_t value) {
    unsigned char ch = (unsigned char)(value & 0xff);
    return cheng_copy_string_bytes((const char*)&ch, 1u);
}
WEAK ChengStrBridge driver_c_char_to_str_bridge(int32_t value) {
    return cheng_str_bridge_from_owned(driver_c_char_to_str(value));
}
WEAK ChengStrBridge driver_c_str_from_utf8_copy_bridge(const char* raw, int32_t n) {
    if (raw == NULL || n <= 0) {
        return cheng_str_bridge_from_owned(cheng_copy_string_bytes("", 0u));
    }
    return cheng_str_bridge_from_owned(cheng_copy_string_bytes(raw, (size_t)n));
}
WEAK ChengStrBridge driver_c_str_concat_bridge(ChengStrBridge a, ChengStrBridge b) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    char* out = NULL;
    if (!cheng_str_bridge_view(a, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_str_bridge_view(b, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    out = (char*)malloc(la + lb + 1u);
    if (out == NULL) {
        return cheng_str_bridge_from_owned(cheng_copy_string_bytes("", 0u));
    }
    if (la > 0) {
        memcpy(out, sa, la);
    }
    if (lb > 0) {
        memcpy(out + la, sb, lb);
    }
    out[la + lb] = '\0';
    return cheng_str_bridge_from_owned(out);
}

static int cheng_v3_ascii_space_char(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}
WEAK ChengStrBridge cheng_v3_str_strip_bridge(ChengStrBridge s) {
    const char* safe = "";
    size_t n = 0u;
    size_t start = 0u;
    size_t end = 0u;
    if (!cheng_str_bridge_view(s, &safe, &n) || n == 0u) {
        return driver_c_str_from_utf8_copy_bridge("", 0);
    }
    end = n;
    while (start < n && cheng_v3_ascii_space_char((unsigned char)safe[start])) {
        start += 1u;
    }
    while (end > start && cheng_v3_ascii_space_char((unsigned char)safe[end - 1u])) {
        end -= 1u;
    }
    return driver_c_str_from_utf8_copy_bridge(safe + start, (int32_t)(end - start));
}
WEAK int32_t cheng_v3_os_is_absolute_bridge(ChengStrBridge path) {
    const char* safe = "";
    size_t n = 0u;
    if (!cheng_str_bridge_view(path, &safe, &n) || n == 0u) {
        return 0;
    }
    if (safe[0] == '/' || safe[0] == '\\') {
        return 1;
    }
    if (n >= 2u && safe[1] == ':') {
        return 1;
    }
    return 0;
}
WEAK ChengStrBridge cheng_v3_os_join_path_bridge(ChengStrBridge left, ChengStrBridge right) {
    const char* left_ptr = "";
    const char* right_ptr = "";
    size_t left_len = 0u;
    size_t right_len = 0u;
    size_t total = 0u;
    int need_sep = 0;
    char* out = NULL;
    if (!cheng_str_bridge_view(left, &left_ptr, &left_len)) {
        left_ptr = "";
        left_len = 0u;
    }
    if (!cheng_str_bridge_view(right, &right_ptr, &right_len)) {
        right_ptr = "";
        right_len = 0u;
    }
    if (left_len == 0u) {
        return driver_c_str_from_utf8_copy_bridge(right_ptr, (int32_t)right_len);
    }
    if (right_len == 0u) {
        return driver_c_str_from_utf8_copy_bridge(left_ptr, (int32_t)left_len);
    }
    need_sep = left_ptr[left_len - 1u] != '/' &&
               left_ptr[left_len - 1u] != '\\' &&
               right_ptr[0] != '/' &&
               right_ptr[0] != '\\';
    total = left_len + right_len + (size_t)(need_sep ? 1 : 0);
    out = (char*)malloc(total + 1u);
    if (out == NULL) {
        return driver_c_str_from_utf8_copy_bridge("", 0);
    }
    memcpy(out, left_ptr, left_len);
    if (need_sep) {
        out[left_len] = '/';
    }
    memcpy(out + left_len + (size_t)(need_sep ? 1 : 0), right_ptr, right_len);
    out[total] = '\0';
    return cheng_str_bridge_from_owned(out);
}

#include "system_helpers_debug_profile.inc"

#include "system_helpers_debug_entry.inc"

WEAK void cheng_v3_register_line_map_from_argv0(const char* argv0) {
    v3_debug_register_line_map_from_argv0_impl(argv0);
}

WEAK void cheng_v3_native_register_line_map_from_argv0(const char* argv0) {
    cheng_v3_register_line_map_from_argv0(argv0);
}

static void cheng_dump_backtrace_now(const char* reason) {
    v3_debug_dump_backtrace_now_impl(reason);
}

static void cheng_v3_mkdir_all_raw(const char* path, int include_leaf) {
    v3_debug_mkdir_all_raw(path, include_leaf);
}

static char* cheng_v3_bridge_copy_cstring(ChengStrBridge text) {
    const char* safe = "";
    size_t n = 0u;
    char* out = NULL;
    if (!cheng_str_bridge_view(text, &safe, &n)) {
        safe = "";
        n = 0u;
    }
    out = (char*)malloc(n + 1u);
    if (out == NULL) {
        return NULL;
    }
    if (n > 0u) {
        memcpy(out, safe, n);
    }
    out[n] = '\0';
    return out;
}

typedef struct ChengV3Sha256Ctx {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} ChengV3Sha256Ctx;

static const uint32_t cheng_v3_sha256_table[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

#define CHENG_V3_SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CHENG_V3_SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define CHENG_V3_SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define CHENG_V3_SHA256_EP0(x) (CHENG_V3_SHA256_ROTR((x), 2) ^ CHENG_V3_SHA256_ROTR((x), 13) ^ CHENG_V3_SHA256_ROTR((x), 22))
#define CHENG_V3_SHA256_EP1(x) (CHENG_V3_SHA256_ROTR((x), 6) ^ CHENG_V3_SHA256_ROTR((x), 11) ^ CHENG_V3_SHA256_ROTR((x), 25))
#define CHENG_V3_SHA256_SIG0(x) (CHENG_V3_SHA256_ROTR((x), 7) ^ CHENG_V3_SHA256_ROTR((x), 18) ^ ((x) >> 3))
#define CHENG_V3_SHA256_SIG1(x) (CHENG_V3_SHA256_ROTR((x), 17) ^ CHENG_V3_SHA256_ROTR((x), 19) ^ ((x) >> 10))

static void cheng_v3_sha256_init(ChengV3Sha256Ctx* ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

static void cheng_v3_sha256_transform(ChengV3Sha256Ctx* ctx, const uint8_t data[64]) {
    uint32_t a = 0U;
    uint32_t b = 0U;
    uint32_t c = 0U;
    uint32_t d = 0U;
    uint32_t e = 0U;
    uint32_t f = 0U;
    uint32_t g = 0U;
    uint32_t h = 0U;
    uint32_t m[64];
    uint32_t t1 = 0U;
    uint32_t t2 = 0U;
    size_t i = 0u;
    for (i = 0u; i < 16u; ++i) {
        m[i] = ((uint32_t)data[i * 4u] << 24) |
               ((uint32_t)data[i * 4u + 1u] << 16) |
               ((uint32_t)data[i * 4u + 2u] << 8) |
               ((uint32_t)data[i * 4u + 3u]);
    }
    for (i = 16u; i < 64u; ++i) {
        m[i] = CHENG_V3_SHA256_SIG1(m[i - 2u]) + m[i - 7u] +
               CHENG_V3_SHA256_SIG0(m[i - 15u]) + m[i - 16u];
    }
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];
    for (i = 0u; i < 64u; ++i) {
        t1 = h + CHENG_V3_SHA256_EP1(e) + CHENG_V3_SHA256_CH(e, f, g) +
             cheng_v3_sha256_table[i] + m[i];
        t2 = CHENG_V3_SHA256_EP0(a) + CHENG_V3_SHA256_MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void cheng_v3_sha256_update(ChengV3Sha256Ctx* ctx, const uint8_t* data, size_t len) {
    size_t i = 0u;
    for (i = 0u; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64u) {
            cheng_v3_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512u;
            ctx->datalen = 0u;
        }
    }
}

static void cheng_v3_sha256_final(ChengV3Sha256Ctx* ctx, uint8_t hash[32]) {
    size_t i = (size_t)ctx->datalen;
    if (ctx->datalen < 56u) {
        ctx->data[i++] = 0x80u;
        while (i < 56u) {
            ctx->data[i++] = 0x00u;
        }
    } else {
        ctx->data[i++] = 0x80u;
        while (i < 64u) {
            ctx->data[i++] = 0x00u;
        }
        cheng_v3_sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56u);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    cheng_v3_sha256_transform(ctx, ctx->data);
    for (i = 0u; i < 4u; ++i) {
        hash[i] = (uint8_t)((ctx->state[0] >> (24u - (uint32_t)i * 8u)) & 0xffu);
        hash[i + 4u] = (uint8_t)((ctx->state[1] >> (24u - (uint32_t)i * 8u)) & 0xffu);
        hash[i + 8u] = (uint8_t)((ctx->state[2] >> (24u - (uint32_t)i * 8u)) & 0xffu);
        hash[i + 12u] = (uint8_t)((ctx->state[3] >> (24u - (uint32_t)i * 8u)) & 0xffu);
        hash[i + 16u] = (uint8_t)((ctx->state[4] >> (24u - (uint32_t)i * 8u)) & 0xffu);
        hash[i + 20u] = (uint8_t)((ctx->state[5] >> (24u - (uint32_t)i * 8u)) & 0xffu);
        hash[i + 24u] = (uint8_t)((ctx->state[6] >> (24u - (uint32_t)i * 8u)) & 0xffu);
        hash[i + 28u] = (uint8_t)((ctx->state[7] >> (24u - (uint32_t)i * 8u)) & 0xffu);
    }
}

WEAK ChengStrBridge cheng_v3_sha256_hex_bridge(ChengStrBridge text) {
    static const char digits[] = "0123456789abcdef";
    ChengV3Sha256Ctx ctx;
    uint8_t digest[32];
    char hex[64];
    int32_t i = 0;
    cheng_v3_sha256_init(&ctx);
    if (text.ptr != NULL && text.len > 0) {
        cheng_v3_sha256_update(&ctx, (const uint8_t*)text.ptr, (size_t)text.len);
    }
    cheng_v3_sha256_final(&ctx, digest);
    for (i = 0; i < 32; ++i) {
        uint8_t value = digest[i];
        hex[i * 2] = digits[(value >> 4) & 0x0fU];
        hex[i * 2 + 1] = digits[value & 0x0fU];
    }
    return driver_c_str_from_utf8_copy_bridge(hex, 64);
}

WEAK int64_t cheng_v3_sha256_word_bridge(ChengStrBridge text, int32_t word_index) {
    ChengV3Sha256Ctx ctx;
    uint8_t digest[32];
    uint64_t out = 0u;
    int32_t base = 0;
    int32_t i = 0;
    if (word_index < 0 || word_index >= 4) {
        return 0;
    }
    cheng_v3_sha256_init(&ctx);
    if (text.ptr != NULL && text.len > 0) {
        cheng_v3_sha256_update(&ctx, (const uint8_t*)text.ptr, (size_t)text.len);
    }
    cheng_v3_sha256_final(&ctx, digest);
    base = word_index * 8;
    for (i = 0; i < 8; ++i) {
        out = (out << 8) | (uint64_t)digest[base + i];
    }
    return (int64_t)out;
}
static char* cheng_v3_bridge_dup_cstring(const char* raw) {
    size_t n = 0u;
    char* out = NULL;
    if (raw == NULL) {
        raw = "";
    }
    n = strlen(raw);
    out = (char*)malloc(n + 1u);
    if (out == NULL) {
        return NULL;
    }
    if (n > 0u) {
        memcpy(out, raw, n);
    }
    out[n] = '\0';
    return out;
}
static int cheng_v3_bridge_starts_with(const char* text, const char* prefix) {
    size_t text_len = 0u;
    size_t prefix_len = 0u;
    if (text == NULL || prefix == NULL) {
        return 0;
    }
    text_len = strlen(text);
    prefix_len = strlen(prefix);
    if (prefix_len > text_len) {
        return 0;
    }
    return memcmp(text, prefix, prefix_len) == 0 ? 1 : 0;
}
static char* cheng_v3_bridge_concat2(const char* a, const char* b) {
    size_t na = 0u;
    size_t nb = 0u;
    char* out = NULL;
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    na = strlen(a);
    nb = strlen(b);
    out = (char*)malloc(na + nb + 1u);
    if (out == NULL) {
        return NULL;
    }
    if (na > 0u) {
        memcpy(out, a, na);
    }
    if (nb > 0u) {
        memcpy(out + na, b, nb);
    }
    out[na + nb] = '\0';
    return out;
}
static char* cheng_v3_bridge_concat3(const char* a, const char* b, const char* c) {
    char* ab = cheng_v3_bridge_concat2(a, b);
    char* out = NULL;
    if (ab == NULL) {
        return NULL;
    }
    out = cheng_v3_bridge_concat2(ab, c);
    free(ab);
    return out;
}
static char* cheng_v3_bridge_concat4(const char* a, const char* b, const char* c, const char* d) {
    char* abc = cheng_v3_bridge_concat3(a, b, c);
    char* out = NULL;
    if (abc == NULL) {
        return NULL;
    }
    out = cheng_v3_bridge_concat2(abc, d);
    free(abc);
    return out;
}
static const char* cheng_v3_bridge_last_path_name(const char* path) {
    const char* last = path;
    if (path == NULL) {
        return "";
    }
    for (const char* cur = path; *cur != '\0'; ++cur) {
        if (*cur == '/' || *cur == '\\') {
            last = cur + 1;
        }
    }
    return last;
}
static char* cheng_v3_bridge_file_stem_dup(const char* path) {
    const char* base = cheng_v3_bridge_last_path_name(path);
    size_t len = strlen(base);
    size_t dot = len;
    char* out = NULL;
    for (size_t i = 0u; i < len; ++i) {
        if (base[i] == '.') {
            dot = i;
        }
    }
    if (dot == 0u || dot == len) {
        return cheng_v3_bridge_dup_cstring(base);
    }
    out = (char*)malloc(dot + 1u);
    if (out == NULL) {
        return NULL;
    }
    if (dot > 0u) {
        memcpy(out, base, dot);
    }
    out[dot] = '\0';
    return out;
}
static char* cheng_v3_bridge_rel_without_ext_dup(const char* path) {
    size_t len = 0u;
    char* out = NULL;
    if (path == NULL) {
        return cheng_v3_bridge_dup_cstring("");
    }
    len = strlen(path);
    out = cheng_v3_bridge_dup_cstring(path);
    if (out == NULL) {
        return NULL;
    }
    for (size_t i = 0u; i < len; ++i) {
        if (out[i] == '\\') {
            out[i] = '/';
        }
    }
    if (len >= 6u && memcmp(out + len - 6u, ".cheng", 6u) == 0) {
        out[len - 6u] = '\0';
    }
    return out;
}
static char* cheng_v3_bridge_package_id_dup(const char* package_root) {
    const char* name = cheng_v3_bridge_last_path_name(package_root);
    if (name[0] == '\0') {
        return cheng_v3_bridge_dup_cstring("");
    }
    if (strcmp(name, "v3") == 0) {
        return cheng_v3_bridge_dup_cstring("cheng/v3");
    }
    return cheng_v3_bridge_concat2("cheng/", name);
}
static char* cheng_v3_bridge_module_to_source_dup(const char* workspace,
                                                  const char* package,
                                                  const char* module) {
    char* out = NULL;
    if (workspace == NULL || package == NULL || module == NULL) {
        return NULL;
    }
    if (module[0] == '\0') {
        return cheng_v3_bridge_dup_cstring("");
    }
    if (cheng_v3_bridge_starts_with(module, "std/") && strlen(module) > 4u) {
        return cheng_v3_bridge_concat4(workspace, "/src/std/", module + 4, ".cheng");
    }
    {
        char* package_id = cheng_v3_bridge_package_id_dup(package);
        size_t package_id_len = package_id != NULL ? strlen(package_id) : 0u;
        if (package_id != NULL &&
            package_id_len < strlen(module) &&
            memcmp(module, package_id, package_id_len) == 0 &&
            module[package_id_len] == '/') {
            out = cheng_v3_bridge_concat4(package, "/src/", module + package_id_len + 1u, ".cheng");
        }
        free(package_id);
    }
    if (out != NULL) {
        return out;
    }
    return cheng_v3_bridge_dup_cstring("");
}
static int cheng_v3_bridge_buf_append_n(char** buf,
                                        size_t* len,
                                        size_t* cap,
                                        const char* text,
                                        size_t n) {
    char* grown = NULL;
    size_t need = 0u;
    size_t next = 0u;
    if (buf == NULL || len == NULL || cap == NULL) {
        return 0;
    }
    if (text == NULL) {
        text = "";
        n = 0u;
    }
    need = *len + n + 1u;
    if (need > *cap) {
        next = *cap > 0u ? *cap : 64u;
        while (next < need) {
            next *= 2u;
        }
        grown = (char*)realloc(*buf, next);
        if (grown == NULL) {
            return 0;
        }
        *buf = grown;
        *cap = next;
    }
    if (n > 0u) {
        memcpy(*buf + *len, text, n);
        *len += n;
    }
    (*buf)[*len] = '\0';
    return 1;
}
static int cheng_v3_bridge_buf_append(char** buf,
                                      size_t* len,
                                      size_t* cap,
                                      const char* text) {
    size_t n = text != NULL ? strlen(text) : 0u;
    return cheng_v3_bridge_buf_append_n(buf, len, cap, text, n);
}
static int cheng_v3_bridge_is_inline_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}
static void cheng_v3_bridge_release_owned_ptr(const char* ptr) {
    if (ptr == NULL) {
        return;
    }
    if (cheng_mem_find_block_any((void*)ptr) != NULL) {
        cheng_free((void*)ptr);
        return;
    }
    free((void*)ptr);
}
static void cheng_v3_bridge_release_owned(ChengStrBridge value) {
    if ((value.flags & CHENG_STR_BRIDGE_FLAG_OWNED) != 0 && value.ptr != NULL) {
        cheng_v3_bridge_release_owned_ptr(value.ptr);
    }
}
static int cheng_v3_file_nonempty_exists_raw(const char* path) {
    FILE* f = NULL;
    long size = 0;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    size = ftell(f);
    fclose(f);
    return size > 0 ? 1 : 0;
}
static char* cheng_v3_read_text_file_raw_dup(const char* path) {
    FILE* f = NULL;
    long size = 0;
    size_t got = 0u;
    char* out = NULL;
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    out = (char*)malloc((size_t)size + 1u);
    if (out == NULL) {
        fclose(f);
        return NULL;
    }
    if (size > 0) {
        got = fread(out, 1u, (size_t)size, f);
        if (got != (size_t)size) {
            free(out);
            fclose(f);
            return NULL;
        }
    }
    out[size] = '\0';
    fclose(f);
    return out;
}
static char* cheng_v3_bridge_trim_dup(const char* raw) {
    const char* start = raw != NULL ? raw : "";
    const char* end = NULL;
    char* out = NULL;
    size_t len = 0u;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    end = start + strlen(start);
    while (end > start) {
        char ch = end[-1];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        end--;
    }
    len = (size_t)(end - start);
    out = (char*)malloc(len + 1u);
    if (out == NULL) {
        return NULL;
    }
    if (len > 0u) {
        memcpy(out, start, len);
    }
    out[len] = '\0';
    return out;
}
static char* cheng_v3_bridge_normalize_path_dup(const char* raw) {
    char* out = cheng_v3_bridge_trim_dup(raw);
    size_t len = 0u;
    if (out == NULL) {
        return NULL;
    }
    len = strlen(out);
    while (len > 1u) {
        char last = out[len - 1u];
        if (last != '/' && last != '\\') {
            break;
        }
        out[len - 1u] = '\0';
        len -= 1u;
    }
    return out;
}
static char* cheng_v3_bridge_workspace_root_dup(const char* package_root) {
    char* normalized = cheng_v3_bridge_normalize_path_dup(package_root);
    const char* base = NULL;
    char* out = NULL;
    if (normalized == NULL) {
        return NULL;
    }
    base = cheng_v3_bridge_last_path_name(normalized);
    if (strcmp(base, "v3") == 0) {
        const char* last = base - 1;
        if (last > normalized) {
            size_t parent_len = (size_t)(last - normalized);
            out = (char*)malloc(parent_len + 1u);
            if (out == NULL) {
                free(normalized);
                return NULL;
            }
            memcpy(out, normalized, parent_len);
            out[parent_len] = '\0';
            free(normalized);
            return out;
        }
    }
    return normalized;
}
static ChengStrBridge cheng_v3_bridge_owned_str(const char* raw) {
    char* dup = cheng_v3_bridge_dup_cstring(raw);
    if (dup == NULL) {
        return cheng_str_bridge_empty();
    }
    return cheng_str_bridge_from_owned(dup);
}
static int cheng_v3_seq_add_elem(ChengSeqHeader* seq, const void* elem, int32_t elem_size) {
    void* slot = NULL;
    if (seq == NULL || elem == NULL || elem_size <= 0) {
        return 0;
    }
    slot = cheng_seq_set_grow(seq, seq->len, elem_size);
    if (slot == NULL) {
        return 0;
    }
    memcpy(slot, elem, (size_t)elem_size);
    return 1;
}
static int cheng_v3_str_bridge_eq_cstr(const ChengStrBridge* value, const char* raw) {
    size_t len = 0u;
    if (value == NULL) {
        return 0;
    }
    if (raw == NULL) {
        raw = "";
    }
    len = strlen(raw);
    if (value->len != (int32_t)len) {
        return 0;
    }
    if (len == 0u) {
        return 1;
    }
    return value->ptr != NULL && memcmp(value->ptr, raw, len) == 0 ? 1 : 0;
}
static int cheng_v3_str_seq_contains_cstr(const ChengSeqHeader* seq, const char* raw) {
    const ChengStrBridge* items = NULL;
    int32_t i = 0;
    if (seq == NULL || seq->buffer == NULL) {
        return 0;
    }
    items = (const ChengStrBridge*)seq->buffer;
    for (i = 0; i < seq->len; ++i) {
        if (cheng_v3_str_bridge_eq_cstr(items + i, raw)) {
            return 1;
        }
    }
    return 0;
}
WEAK ChengStrBridge cheng_v3_parser_source_to_module_bridge(ChengStrBridge workspace_root,
                                                            ChengStrBridge package_root,
                                                            ChengStrBridge source_path);
WEAK ChengStrBridge cheng_v3_parser_module_to_source_bridge(ChengStrBridge workspace_root,
                                                            ChengStrBridge package_root,
                                                            ChengStrBridge module_path) {
    char* workspace = cheng_v3_bridge_copy_cstring(workspace_root);
    char* package = cheng_v3_bridge_copy_cstring(package_root);
    char* module = cheng_v3_bridge_copy_cstring(module_path);
    char* out = NULL;
    if (workspace == NULL || package == NULL || module == NULL) {
        free(workspace);
        free(package);
        free(module);
        return cheng_v3_bridge_owned_str("");
    }
    if (module[0] == '\0') {
        free(workspace);
        free(package);
        free(module);
        return cheng_v3_bridge_owned_str("");
    }
    out = cheng_v3_bridge_module_to_source_dup(workspace, package, module);
    free(workspace);
    free(package);
    free(module);
    if (out == NULL) {
        return cheng_v3_bridge_owned_str("");
    }
    return cheng_str_bridge_from_owned(out);
}
WEAK ChengStrBridge cheng_v3_parser_import_source_paths_bridge(ChengStrBridge workspace_root,
                                                               ChengStrBridge package_root,
                                                               ChengStrBridge text_bridge) {
    char* workspace = cheng_v3_bridge_copy_cstring(workspace_root);
    char* package = cheng_v3_bridge_copy_cstring(package_root);
    char* text = cheng_v3_bridge_copy_cstring(text_bridge);
    char* out = NULL;
    size_t out_len = 0u;
    size_t out_cap = 0u;
    int line_no = 1;
    if (workspace == NULL || package == NULL || text == NULL) {
        free(workspace);
        free(package);
        free(text);
        return cheng_v3_bridge_owned_str("");
    }
    {
        const char* cur = text;
        while (*cur != '\0') {
            const char* line_start = cur;
            const char* line_end = cur;
            const char* token_start = NULL;
            const char* token_end = NULL;
            while (*line_end != '\0' && *line_end != '\n') {
                line_end++;
            }
            while (line_start < line_end && cheng_v3_bridge_is_inline_space(*line_start)) {
                line_start++;
            }
            if ((size_t)(line_end - line_start) >= 7u && memcmp(line_start, "import ", 7u) == 0) {
                token_start = line_start + 7;
                while (token_start < line_end && cheng_v3_bridge_is_inline_space(*token_start)) {
                    token_start++;
                }
                token_end = token_start;
                while (token_end < line_end && !cheng_v3_bridge_is_inline_space(*token_end)) {
                    token_end++;
                }
                if (token_start == token_end) {
                    char errbuf[96];
                    snprintf(errbuf, sizeof(errbuf), "!error:v3 parser: malformed import at line %d", line_no);
                    free(out);
                    free(workspace);
                    free(package);
                    free(text);
                    return cheng_v3_bridge_owned_str(errbuf);
                }
                {
                    size_t token_len = (size_t)(token_end - token_start);
                    char* module = (char*)malloc(token_len + 1u);
                    char* source = NULL;
                    const char* emit = "-";
                    if (module == NULL) {
                        free(out);
                        free(workspace);
                        free(package);
                        free(text);
                        return cheng_v3_bridge_owned_str("");
                    }
                    memcpy(module, token_start, token_len);
                    module[token_len] = '\0';
                    source = cheng_v3_bridge_module_to_source_dup(workspace, package, module);
                    if (source != NULL && source[0] != '\0' && cheng_v3_file_nonempty_exists_raw(source)) {
                        emit = source;
                    }
                    if (!cheng_v3_bridge_buf_append(&out, &out_len, &out_cap, emit) ||
                        !cheng_v3_bridge_buf_append_n(&out, &out_len, &out_cap, "\n", 1u)) {
                        free(module);
                        free(source);
                        free(out);
                        free(workspace);
                        free(package);
                        free(text);
                        return cheng_v3_bridge_owned_str("");
                    }
                    free(module);
                    free(source);
                }
            }
            if (*line_end == '\n') {
                cur = line_end + 1;
                line_no += 1;
            } else {
                cur = line_end;
            }
        }
    }
    free(workspace);
    free(package);
    free(text);
    if (out == NULL) {
        return cheng_v3_bridge_owned_str("");
    }
    return cheng_str_bridge_from_owned(out);
}
typedef struct ChengV3ImportEdgeCompat {
    ChengStrBridge ownerModulePath;
    ChengStrBridge targetModulePath;
    ChengStrBridge targetSourcePath;
    uint8_t resolved;
    uint8_t _pad[7];
} ChengV3ImportEdgeCompat;

typedef struct ChengV3ParsedSourceStubCompat {
    ChengStrBridge workspaceRoot;
    ChengStrBridge packageRoot;
    ChengStrBridge packageId;
    ChengStrBridge entrySourcePath;
    ChengStrBridge sourcePath;
    ChengStrBridge moduleStem;
    ChengStrBridge syntaxTag;
    int32_t sourceKind;
    int32_t _pad0;
    ChengSeqHeader ownerModulePaths;
    ChengSeqHeader importEdges;
    ChengSeqHeader orderedClosureSourcePaths;
    uint8_t hasCompilerTopLevel;
    uint8_t mainFunctionPresent;
    uint8_t _pad1[2];
    int32_t unresolvedImportCount;
} ChengV3ParsedSourceStubCompat;

static void cheng_v3_parser_scan_feature_flags(const char* text,
                                               int* has_compiler_top_level,
                                               int* main_function_present) {
    const char* cur = text != NULL ? text : "";
    if (has_compiler_top_level != NULL) {
        *has_compiler_top_level = 0;
    }
    if (main_function_present != NULL) {
        *main_function_present = 0;
    }
    while (*cur != '\0') {
        const char* line_start = cur;
        const char* line_end = cur;
        while (*line_end != '\0' && *line_end != '\n') {
            line_end++;
        }
        while (line_start < line_end && cheng_v3_bridge_is_inline_space(*line_start)) {
            line_start++;
        }
        if (has_compiler_top_level != NULL) {
            if ((size_t)(line_end - line_start) >= 7u && memcmp(line_start, "import ", 7u) == 0) {
                *has_compiler_top_level = 1;
            } else if ((size_t)(line_end - line_start) >= 4u && memcmp(line_start, "type", 4u) == 0) {
                *has_compiler_top_level = 1;
            } else if ((size_t)(line_end - line_start) >= 3u && memcmp(line_start, "fn ", 3u) == 0) {
                *has_compiler_top_level = 1;
            } else if ((size_t)(line_end - line_start) >= 9u && memcmp(line_start, "iterator ", 9u) == 0) {
                *has_compiler_top_level = 1;
            } else if ((size_t)(line_end - line_start) >= 6u && memcmp(line_start, "const ", 6u) == 0) {
                *has_compiler_top_level = 1;
            } else if ((size_t)(line_end - line_start) >= 4u && memcmp(line_start, "let ", 4u) == 0) {
                *has_compiler_top_level = 1;
            } else if ((size_t)(line_end - line_start) >= 4u && memcmp(line_start, "var ", 4u) == 0) {
                *has_compiler_top_level = 1;
            }
        }
        if (main_function_present != NULL &&
            (size_t)(line_end - line_start) >= 8u &&
            memcmp(line_start, "fn main(", 8u) == 0) {
            *main_function_present = 1;
        }
        if (*line_end == '\n') {
            cur = line_end + 1;
        } else {
            cur = line_end;
        }
    }
}

static int cheng_v3_parser_collect_closure_stub(const char* workspace_root,
                                                const char* package_root,
                                                const char* source_path,
                                                const char* owner_module_path,
                                                ChengSeqHeader* ordered_out,
                                                ChengSeqHeader* import_edges_out,
                                                int32_t* unresolved_out,
                                                ChengStrBridge* err_out) {
    int32_t scan_pos = 0;
    if (ordered_out == NULL || import_edges_out == NULL || unresolved_out == NULL || err_out == NULL) {
        return 0;
    }
    ordered_out->len = 0;
    ordered_out->cap = 0;
    ordered_out->buffer = NULL;
    import_edges_out->len = 0;
    import_edges_out->cap = 0;
    import_edges_out->buffer = NULL;
    *unresolved_out = 0;
    *err_out = cheng_str_bridge_empty();
    if (!cheng_v3_seq_add_elem(ordered_out,
                               &(ChengStrBridge){ .ptr = NULL, .len = 0, .store_id = 0, .flags = 0 },
                               (int32_t)sizeof(ChengStrBridge))) {
        *err_out = cheng_v3_bridge_owned_str("v3 parser: native closure alloc failed");
        return 0;
    }
    ((ChengStrBridge*)ordered_out->buffer)[0] = cheng_v3_bridge_owned_str(source_path);
    while (scan_pos < ordered_out->len) {
        ChengStrBridge current = ((ChengStrBridge*)ordered_out->buffer)[scan_pos];
        char* text = NULL;
        ChengStrBridge import_text_bridge;
        char* import_text = NULL;
        const char* cur = NULL;
        int is_root = scan_pos == 0 ? 1 : 0;
        scan_pos += 1;
        text = cheng_v3_read_text_file_raw_dup(current.ptr);
        if (text == NULL || text[0] == '\0') {
            free(text);
            *err_out = cheng_v3_bridge_owned_str("v3 parser: missing source");
            return 0;
        }
        import_text_bridge = cheng_v3_parser_import_source_paths_bridge(
            cheng_str_bridge_from_ptr_flags(workspace_root, CHENG_STR_BRIDGE_FLAG_NONE),
            cheng_str_bridge_from_ptr_flags(package_root, CHENG_STR_BRIDGE_FLAG_NONE),
            cheng_str_bridge_from_ptr_flags(text, CHENG_STR_BRIDGE_FLAG_NONE));
        free(text);
        import_text = cheng_v3_bridge_copy_cstring(import_text_bridge);
        cheng_v3_bridge_release_owned(import_text_bridge);
        if (import_text == NULL) {
            *err_out = cheng_v3_bridge_owned_str("v3 parser: import scan alloc failed");
            return 0;
        }
        if (cheng_v3_bridge_starts_with(import_text, "!error:")) {
            *err_out = cheng_v3_bridge_owned_str(import_text + 7);
            free(import_text);
            return 0;
        }
        cur = import_text;
        while (*cur != '\0') {
            const char* line_start = cur;
            const char* line_end = cur;
            size_t line_len = 0u;
            while (*line_end != '\0' && *line_end != '\n') {
                line_end++;
            }
            line_len = (size_t)(line_end - line_start);
            if (line_len > 0u) {
                char* line = (char*)malloc(line_len + 1u);
                int resolved = 0;
                if (line == NULL) {
                    free(import_text);
                    *err_out = cheng_v3_bridge_owned_str("v3 parser: import line alloc failed");
                    return 0;
                }
                memcpy(line, line_start, line_len);
                line[line_len] = '\0';
                resolved = strcmp(line, "-") != 0;
                if (!resolved) {
                    *unresolved_out += 1;
                }
                if (is_root) {
                    ChengV3ImportEdgeCompat edge;
                    memset(&edge, 0, sizeof(edge));
                    edge.ownerModulePath = cheng_v3_bridge_owned_str(owner_module_path);
                    edge.targetModulePath = cheng_str_bridge_empty();
                    edge.targetSourcePath = resolved ? cheng_v3_bridge_owned_str(line) : cheng_str_bridge_empty();
                    edge.resolved = resolved ? 1u : 0u;
                    if (!cheng_v3_seq_add_elem(import_edges_out, &edge, (int32_t)sizeof(edge))) {
                        free(line);
                        free(import_text);
                        *err_out = cheng_v3_bridge_owned_str("v3 parser: import edge alloc failed");
                        return 0;
                    }
                }
                if (resolved && !cheng_v3_str_seq_contains_cstr(ordered_out, line)) {
                    ChengStrBridge next = cheng_v3_bridge_owned_str(line);
                    if (!cheng_v3_seq_add_elem(ordered_out, &next, (int32_t)sizeof(next))) {
                        free(line);
                        free(import_text);
                        *err_out = cheng_v3_bridge_owned_str("v3 parser: closure path alloc failed");
                        return 0;
                    }
                }
                free(line);
            }
            if (*line_end == '\n') {
                cur = line_end + 1;
            } else {
                cur = line_end;
            }
        }
        free(import_text);
    }
    return 1;
}

WEAK int32_t cheng_v3_parser_parse_source_stub_bridge(ChengStrBridge package_root_bridge,
                                                      ChengStrBridge source_path_bridge,
                                                      void* parsed_out_raw,
                                                      ChengStrBridge* err_out) {
    ChengV3ParsedSourceStubCompat* parsed = (ChengV3ParsedSourceStubCompat*)parsed_out_raw;
    char* package_root_raw = NULL;
    char* source_path_raw = NULL;
    char* package_root = NULL;
    char* source_path = NULL;
    char* workspace_root = NULL;
    char* module_stem = NULL;
    char* package_id = NULL;
    char* root_text = NULL;
    ChengStrBridge owner_module_bridge;
    char* owner_module_path = NULL;
    int has_compiler_top_level = 0;
    int main_function_present = 0;
    ChengSeqHeader owner_modules;
    ChengSeqHeader import_edges;
    ChengSeqHeader ordered;
    ChengStrBridge closure_err = cheng_str_bridge_empty();
    int32_t unresolved = 0;
    memset(&owner_modules, 0, sizeof(owner_modules));
    memset(&import_edges, 0, sizeof(import_edges));
    memset(&ordered, 0, sizeof(ordered));
    owner_module_bridge = cheng_str_bridge_empty();
    if (parsed == NULL || err_out == NULL) {
        return 0;
    }
    memset(parsed, 0, sizeof(*parsed));
    *err_out = cheng_str_bridge_empty();
    package_root_raw = cheng_v3_bridge_copy_cstring(package_root_bridge);
    source_path_raw = cheng_v3_bridge_copy_cstring(source_path_bridge);
    if (package_root_raw == NULL || source_path_raw == NULL) {
        free(package_root_raw);
        free(source_path_raw);
        *err_out = cheng_v3_bridge_owned_str("v3 parser: native path copy failed");
        return 0;
    }
    package_root = cheng_v3_bridge_normalize_path_dup(package_root_raw);
    source_path = cheng_v3_bridge_normalize_path_dup(source_path_raw);
    free(package_root_raw);
    free(source_path_raw);
    if (package_root == NULL) {
        *err_out = cheng_v3_bridge_owned_str("v3 parser: native package root alloc failed");
        return 0;
    }
    if (source_path == NULL) {
        free(package_root);
        *err_out = cheng_v3_bridge_owned_str("v3 parser: native source path alloc failed");
        return 0;
    }
    if (source_path[0] == '\0') {
        free(package_root);
        free(source_path);
        *err_out = cheng_v3_bridge_owned_str("v3 parser: missing source path");
        return 0;
    }
    if (!cheng_v3_file_nonempty_exists_raw(source_path)) {
        char* msg = cheng_v3_bridge_concat2("v3 parser: source file missing: ", source_path);
        free(package_root);
        free(source_path);
        *err_out = cheng_str_bridge_from_owned(msg != NULL ? msg : cheng_v3_bridge_dup_cstring("v3 parser: source file missing"));
        return 0;
    }
    workspace_root = cheng_v3_bridge_workspace_root_dup(package_root);
    module_stem = cheng_v3_bridge_file_stem_dup(source_path);
    package_id = cheng_v3_bridge_package_id_dup(package_root);
    root_text = cheng_v3_read_text_file_raw_dup(source_path);
    if (workspace_root == NULL || module_stem == NULL || package_id == NULL || root_text == NULL) {
        free(package_root);
        free(source_path);
        free(workspace_root);
        free(module_stem);
        free(package_id);
        free(root_text);
        *err_out = cheng_v3_bridge_owned_str("v3 parser: native parse alloc failed");
        return 0;
    }
    owner_module_bridge = cheng_v3_parser_source_to_module_bridge(
        cheng_str_bridge_from_ptr_flags(workspace_root, CHENG_STR_BRIDGE_FLAG_NONE),
        cheng_str_bridge_from_ptr_flags(package_root, CHENG_STR_BRIDGE_FLAG_NONE),
        cheng_str_bridge_from_ptr_flags(source_path, CHENG_STR_BRIDGE_FLAG_NONE));
    owner_module_path = cheng_v3_bridge_copy_cstring(owner_module_bridge);
    cheng_v3_bridge_release_owned(owner_module_bridge);
    if (owner_module_path == NULL) {
        free(package_root);
        free(source_path);
        free(workspace_root);
        free(module_stem);
        free(package_id);
        free(root_text);
        *err_out = cheng_v3_bridge_owned_str("v3 parser: owner module alloc failed");
        return 0;
    }
    cheng_v3_parser_scan_feature_flags(root_text, &has_compiler_top_level, &main_function_present);
    if (!cheng_v3_seq_add_elem(&owner_modules,
                               &(ChengStrBridge){ .ptr = NULL, .len = 0, .store_id = 0, .flags = 0 },
                               (int32_t)sizeof(ChengStrBridge))) {
        free(package_root);
        free(source_path);
        free(workspace_root);
        free(module_stem);
        free(package_id);
        free(root_text);
        free(owner_module_path);
        *err_out = cheng_v3_bridge_owned_str("v3 parser: owner module seq alloc failed");
        return 0;
    }
    ((ChengStrBridge*)owner_modules.buffer)[0] = cheng_v3_bridge_owned_str(owner_module_path);
    if (!cheng_v3_parser_collect_closure_stub(workspace_root,
                                              package_root,
                                              source_path,
                                              owner_module_path,
                                              &ordered,
                                              &import_edges,
                                              &unresolved,
                                              &closure_err)) {
        free(package_root);
        free(source_path);
        free(workspace_root);
        free(module_stem);
        free(package_id);
        free(root_text);
        free(owner_module_path);
        *err_out = closure_err;
        return 0;
    }
    parsed->workspaceRoot = cheng_v3_bridge_owned_str(workspace_root);
    parsed->packageRoot = cheng_v3_bridge_owned_str(package_root);
    parsed->packageId = cheng_v3_bridge_owned_str(package_id);
    parsed->entrySourcePath = cheng_v3_bridge_owned_str(source_path);
    parsed->sourcePath = cheng_v3_bridge_owned_str(source_path);
    parsed->moduleStem = cheng_v3_bridge_owned_str(module_stem);
    parsed->syntaxTag = cheng_v3_bridge_owned_str("v3_source_stub_v1");
    parsed->sourceKind = 0;
    parsed->ownerModulePaths = owner_modules;
    parsed->importEdges = import_edges;
    parsed->orderedClosureSourcePaths = ordered;
    parsed->hasCompilerTopLevel = has_compiler_top_level ? 1u : 0u;
    parsed->mainFunctionPresent = main_function_present ? 1u : 0u;
    parsed->unresolvedImportCount = unresolved;
    free(package_root);
    free(source_path);
    free(workspace_root);
    free(module_stem);
    free(package_id);
    free(root_text);
    free(owner_module_path);
    *err_out = cheng_str_bridge_empty();
    return 1;
}
WEAK ChengStrBridge cheng_v3_parser_source_to_module_bridge(ChengStrBridge workspace_root,
                                                            ChengStrBridge package_root,
                                                            ChengStrBridge source_path) {
    char* workspace = cheng_v3_bridge_copy_cstring(workspace_root);
    char* package = cheng_v3_bridge_copy_cstring(package_root);
    char* source = cheng_v3_bridge_copy_cstring(source_path);
    char* out = NULL;
    if (workspace == NULL || package == NULL || source == NULL) {
        free(workspace);
        free(package);
        free(source);
        return cheng_v3_bridge_owned_str("");
    }
    {
        char* package_prefix = cheng_v3_bridge_concat2(package, "/src/");
        if (package_prefix != NULL && cheng_v3_bridge_starts_with(source, package_prefix)) {
            char* rel = cheng_v3_bridge_rel_without_ext_dup(source + strlen(package_prefix));
            char* package_id = cheng_v3_bridge_package_id_dup(package);
            if (rel != NULL && package_id != NULL) {
                out = cheng_v3_bridge_concat3(package_id, "/", rel);
            }
            free(rel);
            free(package_id);
        }
        free(package_prefix);
    }
    if (out == NULL) {
        char* std_prefix = cheng_v3_bridge_concat2(workspace, "/src/std/");
        if (std_prefix != NULL && cheng_v3_bridge_starts_with(source, std_prefix)) {
            char* rel = cheng_v3_bridge_rel_without_ext_dup(source + strlen(std_prefix));
            if (rel != NULL) {
                out = cheng_v3_bridge_concat2("std/", rel);
            }
            free(rel);
        }
        free(std_prefix);
    }
    if (out == NULL) {
        out = cheng_v3_bridge_file_stem_dup(source);
    }
    free(workspace);
    free(package);
    free(source);
    if (out == NULL) {
        return cheng_v3_bridge_owned_str("");
    }
    return cheng_str_bridge_from_owned(out);
}
WEAK int32_t cheng_v3_os_file_exists_bridge(ChengStrBridge path) {
    char* raw = cheng_v3_bridge_copy_cstring(path);
    int32_t out = 0;
    if (raw == NULL) {
        return 0;
    }
    out = cheng_file_exists(raw) != 0 ? 1 : 0;
    free(raw);
    return out;
}
WEAK int32_t cheng_v3_os_dir_exists_bridge(ChengStrBridge path) {
    char* raw = cheng_v3_bridge_copy_cstring(path);
    int32_t out = 0;
    if (raw == NULL) {
        return 0;
    }
    out = cheng_dir_exists(raw) != 0 ? 1 : 0;
    free(raw);
    return out;
}
WEAK int64_t cheng_v3_os_file_size_bridge(ChengStrBridge path) {
    char* raw = cheng_v3_bridge_copy_cstring(path);
    int64_t out = 0;
    if (raw == NULL) {
        return 0;
    }
    out = cheng_file_size(raw);
    free(raw);
    return out;
}
WEAK ChengStrBridge cheng_v3_read_file_bridge(ChengStrBridge path) {
    char* raw = cheng_v3_bridge_copy_cstring(path);
    ChengStrBridge out = driver_c_str_from_utf8_copy_bridge("", 0);
    if (raw == NULL) {
        return out;
    }
    out = driver_c_read_file_all_bridge(raw);
    free(raw);
    return out;
}
static ChengStrBridge cheng_v3_tcp_bridge_error_result(const char* prefix) {
    char buffer[256];
    const char* safe_prefix = prefix != NULL ? prefix : "v3 libp2p tcp";
    const char* safe_text = strerror(errno);
    if (safe_text == NULL) {
        safe_text = "unknown";
    }
    snprintf(buffer, sizeof(buffer), "%s: %s (%d)", safe_prefix, safe_text, errno);
    return driver_c_str_from_utf8_copy_bridge(buffer, (int32_t)strlen(buffer));
}
static int cheng_v3_tcp_send_all_raw(int fd, const char* data, size_t len) {
    size_t sent = 0u;
    while (sent < len) {
        ssize_t rc = send(fd, data + sent, len - sent, 0);
        if (rc <= 0) {
            return 0;
        }
        sent += (size_t)rc;
    }
    return 1;
}
static int cheng_v3_tcp_recv_exact_raw(int fd, char* data, size_t len) {
    size_t got = 0u;
    while (got < len) {
        ssize_t rc = recv(fd, data + got, len - got, 0);
        if (rc <= 0) {
            return 0;
        }
        got += (size_t)rc;
    }
    return 1;
}
static void cheng_v3_tcp_write_u32be(unsigned char out[4], uint32_t value) {
    out[0] = (unsigned char)((value >> 24) & 0xffu);
    out[1] = (unsigned char)((value >> 16) & 0xffu);
    out[2] = (unsigned char)((value >> 8) & 0xffu);
    out[3] = (unsigned char)(value & 0xffu);
}
static uint32_t cheng_v3_tcp_read_u32be(const unsigned char in[4]) {
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}
static int cheng_v3_tcp_parse_ipv4_host_raw(const char* raw, uint32_t* out_addr_be) {
    struct in_addr addr;
    if (out_addr_be == NULL) {
        return 0;
    }
    if (raw == NULL || raw[0] == '\0' || strcmp(raw, "127.0.0.1") == 0 || strcmp(raw, "localhost") == 0) {
        *out_addr_be = htonl(INADDR_LOOPBACK);
        return 1;
    }
    if (inet_pton(AF_INET, raw, &addr) != 1) {
        return 0;
    }
    *out_addr_be = addr.s_addr;
    return 1;
}
static int cheng_v3_tcp_write_ready_port_raw(const char* ready_path, uint16_t port, ChengStrBridge* err_out) {
    FILE* f = NULL;
    char port_text[32];
    size_t want = 0u;
    size_t wrote = 0u;
    if (ready_path == NULL || ready_path[0] == '\0') {
        return 1;
    }
    f = fopen(ready_path, "wb");
    if (f == NULL) {
        if (err_out != NULL) {
            *err_out = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: open ready path");
        }
        return 0;
    }
    snprintf(port_text, sizeof(port_text), "%u\n", (unsigned)port);
    want = strlen(port_text);
    wrote = fwrite(port_text, 1u, want, f);
    if (wrote != want || fflush(f) != 0) {
        if (err_out != NULL) {
            *err_out = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: write ready path");
        }
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}
WEAK int32_t cheng_v3_tcp_loopback_payload_impl_bridge(ChengStrBridge protocol_text,
                                                       ChengStrBridge payload_text,
                                                       ChengStrBridge* response_text,
                                                       ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p tcp: win32 loopback payload bridge not implemented";
    if (response_text != NULL) {
        *response_text = driver_c_str_from_utf8_copy_bridge("", 0);
    }
    if (err_text != NULL) {
        *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    }
    (void)protocol_text;
    (void)payload_text;
    return 0;
#else
    const char* alloc_failed = "v3 libp2p tcp: alloc failed";
    const char* request_mismatch = "v3 libp2p tcp: multistream request mismatch";
    const char* response_mismatch = "v3 libp2p tcp: multistream response mismatch";
    const char* protocol = "";
    const char* payload = "";
    size_t protocol_len = 0u;
    size_t payload_len = 0u;
    char* server_protocol = NULL;
    char* client_protocol = NULL;
    char* server_payload = NULL;
    char* client_payload = NULL;
    int listener_fd = -1;
    int client_fd = -1;
    int server_fd = -1;
    struct sockaddr_in listener_addr;
    struct sockaddr_in client_addr;
    socklen_t listener_len = (socklen_t)sizeof(listener_addr);
    unsigned char frame_len_buf[4];
    uint32_t frame_len = 0u;
    ChengStrBridge ok = cheng_str_bridge_empty();
    ChengStrBridge err = ok;
    ChengStrBridge response = ok;
    if (response_text != NULL) {
        *response_text = ok;
    }
    if (err_text != NULL) {
        *err_text = ok;
    }
    if (!cheng_str_bridge_view(protocol_text, &protocol, &protocol_len)) {
        protocol = "";
        protocol_len = 0u;
    }
    if (!cheng_str_bridge_view(payload_text, &payload, &payload_len)) {
        payload = "";
        payload_len = 0u;
    }
    memset(&listener_addr, 0, sizeof(listener_addr));
    listener_addr.sin_family = AF_INET;
    listener_addr.sin_port = htons(0);
    listener_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: socket");
        goto cleanup;
    }
    {
        int reuse = 1;
        (void)setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, (socklen_t)sizeof(reuse));
    }
    if (bind(listener_fd, (const struct sockaddr*)&listener_addr, (socklen_t)sizeof(listener_addr)) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: bind");
        goto cleanup;
    }
    if (listen(listener_fd, 8) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: listen");
        goto cleanup;
    }
    if (getsockname(listener_fd, (struct sockaddr*)&listener_addr, &listener_len) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: getsockname");
        goto cleanup;
    }
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = listener_addr.sin_port;
    client_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: client socket");
        goto cleanup;
    }
    if (connect(client_fd, (const struct sockaddr*)&client_addr, (socklen_t)sizeof(client_addr)) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: connect");
        goto cleanup;
    }
    server_fd = accept(listener_fd, NULL, NULL);
    if (server_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: accept");
        goto cleanup;
    }
    if (protocol_len > 0u) {
        server_protocol = (char*)malloc(protocol_len);
        client_protocol = (char*)malloc(protocol_len);
        if (server_protocol == NULL || client_protocol == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_send_all_raw(client_fd, protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send request");
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(server_fd, server_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv request");
            goto cleanup;
        }
        if (memcmp(server_protocol, protocol, protocol_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(request_mismatch, (int32_t)strlen(request_mismatch));
            goto cleanup;
        }
        if (!cheng_v3_tcp_send_all_raw(server_fd, server_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send response");
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(client_fd, client_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv response");
            goto cleanup;
        }
        if (memcmp(client_protocol, protocol, protocol_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(response_mismatch, (int32_t)strlen(response_mismatch));
            goto cleanup;
        }
    }
    if (payload_len > 0xffffffffu) {
        const char* too_large = "v3 libp2p tcp: payload too large";
        err = driver_c_str_from_utf8_copy_bridge(too_large, (int32_t)strlen(too_large));
        goto cleanup;
    }
    cheng_v3_tcp_write_u32be(frame_len_buf, (uint32_t)payload_len);
    if (!cheng_v3_tcp_send_all_raw(client_fd, (const char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send frame len");
        goto cleanup;
    }
    if (payload_len > 0u && !cheng_v3_tcp_send_all_raw(client_fd, payload, payload_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send payload");
        goto cleanup;
    }
    if (!cheng_v3_tcp_recv_exact_raw(server_fd, (char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv frame len");
        goto cleanup;
    }
    frame_len = cheng_v3_tcp_read_u32be(frame_len_buf);
    if (frame_len != (uint32_t)payload_len) {
        const char* length_mismatch = "v3 libp2p tcp: payload length mismatch";
        err = driver_c_str_from_utf8_copy_bridge(length_mismatch, (int32_t)strlen(length_mismatch));
        goto cleanup;
    }
    if (frame_len > 0u) {
        server_payload = (char*)malloc((size_t)frame_len);
        client_payload = (char*)malloc((size_t)frame_len);
        if (server_payload == NULL || client_payload == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(server_fd, server_payload, (size_t)frame_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv payload");
            goto cleanup;
        }
        if (memcmp(server_payload, payload, (size_t)frame_len) != 0) {
            const char* payload_mismatch = "v3 libp2p tcp: payload mismatch";
            err = driver_c_str_from_utf8_copy_bridge(payload_mismatch, (int32_t)strlen(payload_mismatch));
            goto cleanup;
        }
    }
    cheng_v3_tcp_write_u32be(frame_len_buf, frame_len);
    if (!cheng_v3_tcp_send_all_raw(server_fd, (const char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send echo len");
        goto cleanup;
    }
    if (frame_len > 0u && !cheng_v3_tcp_send_all_raw(server_fd, server_payload, (size_t)frame_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send echo payload");
        goto cleanup;
    }
    if (!cheng_v3_tcp_recv_exact_raw(client_fd, (char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv echo len");
        goto cleanup;
    }
    frame_len = cheng_v3_tcp_read_u32be(frame_len_buf);
    if (frame_len > 0u) {
        if (!cheng_v3_tcp_recv_exact_raw(client_fd, client_payload, (size_t)frame_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv echo payload");
            goto cleanup;
        }
        if (memcmp(client_payload, payload, (size_t)frame_len) != 0) {
            const char* echo_payload_mismatch = "v3 libp2p tcp: echo payload mismatch";
            err = driver_c_str_from_utf8_copy_bridge(echo_payload_mismatch, (int32_t)strlen(echo_payload_mismatch));
            goto cleanup;
        }
    }
    response = driver_c_str_from_utf8_copy_bridge(client_payload != NULL ? client_payload : "", (int32_t)frame_len);
cleanup:
    if (response_text != NULL) {
        *response_text = response;
    } else {
        cheng_v3_bridge_release_owned(response);
    }
    if (err_text != NULL) {
        *err_text = err;
    } else {
        cheng_v3_bridge_release_owned(err);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(server_protocol);
    free(client_protocol);
    free(server_payload);
    free(client_payload);
    return err.ptr == NULL || err.len == 0 ? 1 : 0;
#endif
}
WEAK int32_t cheng_v3_tcp_loopback_request_response_impl_bridge(ChengStrBridge protocol_text,
                                                                ChengStrBridge request_text,
                                                                ChengStrBridge response_text,
                                                                ChengStrBridge* client_response_text,
                                                                ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p tcp: win32 loopback request-response bridge not implemented";
    if (client_response_text != NULL) {
        *client_response_text = driver_c_str_from_utf8_copy_bridge("", 0);
    }
    if (err_text != NULL) {
        *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    }
    (void)protocol_text;
    (void)request_text;
    (void)response_text;
    return 0;
#else
    const char* alloc_failed = "v3 libp2p tcp: alloc failed";
    const char* request_mismatch = "v3 libp2p tcp: multistream request mismatch";
    const char* response_mismatch = "v3 libp2p tcp: multistream response mismatch";
    const char* request_payload_mismatch = "v3 libp2p tcp: request payload mismatch";
    const char* request_length_mismatch = "v3 libp2p tcp: request payload length mismatch";
    const char* response_too_large = "v3 libp2p tcp: response payload too large";
    const char* protocol = "";
    const char* request = "";
    const char* response_payload = "";
    size_t protocol_len = 0u;
    size_t request_len = 0u;
    size_t response_len = 0u;
    char* server_protocol = NULL;
    char* client_protocol = NULL;
    char* server_request = NULL;
    char* client_response = NULL;
    int listener_fd = -1;
    int client_fd = -1;
    int server_fd = -1;
    struct sockaddr_in listener_addr;
    struct sockaddr_in client_addr;
    socklen_t listener_len = (socklen_t)sizeof(listener_addr);
    unsigned char frame_len_buf[4];
    uint32_t frame_len = 0u;
    ChengStrBridge err = cheng_str_bridge_empty();
    ChengStrBridge response = cheng_str_bridge_empty();
    if (client_response_text != NULL) {
        *client_response_text = response;
    }
    if (err_text != NULL) {
        *err_text = err;
    }
    if (!cheng_str_bridge_view(protocol_text, &protocol, &protocol_len)) {
        protocol = "";
        protocol_len = 0u;
    }
    if (!cheng_str_bridge_view(request_text, &request, &request_len)) {
        request = "";
        request_len = 0u;
    }
    if (!cheng_str_bridge_view(response_text, &response_payload, &response_len)) {
        response_payload = "";
        response_len = 0u;
    }
    if (response_len > 0xffffffffu) {
        err = driver_c_str_from_utf8_copy_bridge(response_too_large, (int32_t)strlen(response_too_large));
        goto cleanup;
    }
    memset(&listener_addr, 0, sizeof(listener_addr));
    listener_addr.sin_family = AF_INET;
    listener_addr.sin_port = htons(0);
    listener_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: socket");
        goto cleanup;
    }
    {
        int reuse = 1;
        (void)setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, (socklen_t)sizeof(reuse));
    }
    if (bind(listener_fd, (const struct sockaddr*)&listener_addr, (socklen_t)sizeof(listener_addr)) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: bind");
        goto cleanup;
    }
    if (listen(listener_fd, 8) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: listen");
        goto cleanup;
    }
    if (getsockname(listener_fd, (struct sockaddr*)&listener_addr, &listener_len) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: getsockname");
        goto cleanup;
    }
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = listener_addr.sin_port;
    client_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: client socket");
        goto cleanup;
    }
    if (connect(client_fd, (const struct sockaddr*)&client_addr, (socklen_t)sizeof(client_addr)) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: connect");
        goto cleanup;
    }
    server_fd = accept(listener_fd, NULL, NULL);
    if (server_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: accept");
        goto cleanup;
    }
    if (protocol_len > 0u) {
        server_protocol = (char*)malloc(protocol_len);
        client_protocol = (char*)malloc(protocol_len);
        if (server_protocol == NULL || client_protocol == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_send_all_raw(client_fd, protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send request");
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(server_fd, server_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv request");
            goto cleanup;
        }
        if (memcmp(server_protocol, protocol, protocol_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(request_mismatch, (int32_t)strlen(request_mismatch));
            goto cleanup;
        }
        if (!cheng_v3_tcp_send_all_raw(server_fd, protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send response");
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(client_fd, client_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv response");
            goto cleanup;
        }
        if (memcmp(client_protocol, protocol, protocol_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(response_mismatch, (int32_t)strlen(response_mismatch));
            goto cleanup;
        }
    }
    cheng_v3_tcp_write_u32be(frame_len_buf, (uint32_t)request_len);
    if (!cheng_v3_tcp_send_all_raw(client_fd, (const char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send request len");
        goto cleanup;
    }
    if (request_len > 0u && !cheng_v3_tcp_send_all_raw(client_fd, request, request_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send request payload");
        goto cleanup;
    }
    if (!cheng_v3_tcp_recv_exact_raw(server_fd, (char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv request len");
        goto cleanup;
    }
    frame_len = cheng_v3_tcp_read_u32be(frame_len_buf);
    if (frame_len != (uint32_t)request_len) {
        err = driver_c_str_from_utf8_copy_bridge(request_length_mismatch, (int32_t)strlen(request_length_mismatch));
        goto cleanup;
    }
    if (frame_len > 0u) {
        server_request = (char*)malloc((size_t)frame_len);
        if (server_request == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(server_fd, server_request, (size_t)frame_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv request payload");
            goto cleanup;
        }
        if (memcmp(server_request, request, (size_t)frame_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(request_payload_mismatch, (int32_t)strlen(request_payload_mismatch));
            goto cleanup;
        }
    }
    cheng_v3_tcp_write_u32be(frame_len_buf, (uint32_t)response_len);
    if (!cheng_v3_tcp_send_all_raw(server_fd, (const char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send response len");
        goto cleanup;
    }
    if (response_len > 0u && !cheng_v3_tcp_send_all_raw(server_fd, response_payload, response_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send response payload");
        goto cleanup;
    }
    if (!cheng_v3_tcp_recv_exact_raw(client_fd, (char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv response len");
        goto cleanup;
    }
    frame_len = cheng_v3_tcp_read_u32be(frame_len_buf);
    if (frame_len > 0u) {
        client_response = (char*)malloc((size_t)frame_len);
        if (client_response == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(client_fd, client_response, (size_t)frame_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv response payload");
            goto cleanup;
        }
    }
    response = driver_c_str_from_utf8_copy_bridge(client_response != NULL ? client_response : "",
                                                  (int32_t)frame_len);
cleanup:
    if (client_response_text != NULL) {
        *client_response_text = response;
    } else {
        cheng_v3_bridge_release_owned(response);
    }
    if (err_text != NULL) {
        *err_text = err;
    } else {
        cheng_v3_bridge_release_owned(err);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (client_fd >= 0) {
        close(client_fd);
    }
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(server_protocol);
    free(client_protocol);
    free(server_request);
    free(client_response);
    return err.ptr == NULL || err.len == 0 ? 1 : 0;
#endif
}
WEAK int32_t cheng_v3_webrtc_datachannel_request_response_bridge(ChengStrBridge protocol_text,
                                                                 ChengStrBridge signal_text,
                                                                 ChengStrBridge request_text,
                                                                 ChengStrBridge response_text,
                                                                 ChengStrBridge* client_response_text,
                                                                 ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p webrtc: win32 datachannel bridge not implemented";
    if (client_response_text != NULL) {
        *client_response_text = driver_c_str_from_utf8_copy_bridge("", 0);
    }
    if (err_text != NULL) {
        *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    }
    (void)protocol_text;
    (void)signal_text;
    (void)request_text;
    (void)response_text;
    return 0;
#else
    const char* alloc_failed = "v3 libp2p webrtc: alloc failed";
    const char* signal_missing = "v3 libp2p webrtc: signal transcript missing";
    const char* signal_length_mismatch = "v3 libp2p webrtc: signal transcript length mismatch";
    const char* signal_payload_mismatch = "v3 libp2p webrtc: signal transcript mismatch";
    const char* signal_ack_failed = "v3 libp2p webrtc: signal ack mismatch";
    const char* request_mismatch = "v3 libp2p webrtc: multistream request mismatch";
    const char* response_mismatch = "v3 libp2p webrtc: multistream response mismatch";
    const char* request_payload_mismatch = "v3 libp2p webrtc: request payload mismatch";
    const char* request_length_mismatch = "v3 libp2p webrtc: request payload length mismatch";
    const char* response_too_large = "v3 libp2p webrtc: response payload too large";
    const char* protocol = "";
    const char* signal_payload = "";
    const char* request = "";
    const char* response_payload = "";
    size_t protocol_len = 0u;
    size_t signal_len = 0u;
    size_t request_len = 0u;
    size_t response_len = 0u;
    char* server_protocol = NULL;
    char* client_protocol = NULL;
    char* server_signal = NULL;
    char* server_request = NULL;
    char* client_response = NULL;
    int control_fds[2] = {-1, -1};
    int data_fds[2] = {-1, -1};
    unsigned char frame_len_buf[4];
    uint32_t frame_len = 0u;
    char signal_ack = 0;
    ChengStrBridge err = cheng_str_bridge_empty();
    ChengStrBridge response = cheng_str_bridge_empty();
    if (client_response_text != NULL) {
        *client_response_text = response;
    }
    if (err_text != NULL) {
        *err_text = err;
    }
    if (!cheng_str_bridge_view(protocol_text, &protocol, &protocol_len)) {
        protocol = "";
        protocol_len = 0u;
    }
    if (!cheng_str_bridge_view(signal_text, &signal_payload, &signal_len)) {
        signal_payload = "";
        signal_len = 0u;
    }
    if (!cheng_str_bridge_view(request_text, &request, &request_len)) {
        request = "";
        request_len = 0u;
    }
    if (!cheng_str_bridge_view(response_text, &response_payload, &response_len)) {
        response_payload = "";
        response_len = 0u;
    }
    if (signal_len == 0u) {
        err = driver_c_str_from_utf8_copy_bridge(signal_missing, (int32_t)strlen(signal_missing));
        goto cleanup;
    }
    if (response_len > 0xffffffffu) {
        err = driver_c_str_from_utf8_copy_bridge(response_too_large, (int32_t)strlen(response_too_large));
        goto cleanup;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, control_fds) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: control socketpair");
        goto cleanup;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, data_fds) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: data socketpair");
        goto cleanup;
    }
    cheng_v3_tcp_write_u32be(frame_len_buf, (uint32_t)signal_len);
    if (!cheng_v3_tcp_send_all_raw(control_fds[0], (const char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send signal len");
        goto cleanup;
    }
    if (!cheng_v3_tcp_send_all_raw(control_fds[0], signal_payload, signal_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send signal payload");
        goto cleanup;
    }
    if (!cheng_v3_tcp_recv_exact_raw(control_fds[1], (char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv signal len");
        goto cleanup;
    }
    frame_len = cheng_v3_tcp_read_u32be(frame_len_buf);
    if (frame_len != (uint32_t)signal_len) {
        err = driver_c_str_from_utf8_copy_bridge(signal_length_mismatch, (int32_t)strlen(signal_length_mismatch));
        goto cleanup;
    }
    server_signal = (char*)malloc(signal_len);
    if (server_signal == NULL) {
        err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
        goto cleanup;
    }
    if (!cheng_v3_tcp_recv_exact_raw(control_fds[1], server_signal, signal_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv signal payload");
        goto cleanup;
    }
    if (memcmp(server_signal, signal_payload, signal_len) != 0) {
        err = driver_c_str_from_utf8_copy_bridge(signal_payload_mismatch, (int32_t)strlen(signal_payload_mismatch));
        goto cleanup;
    }
    signal_ack = 1;
    if (!cheng_v3_tcp_send_all_raw(control_fds[1], &signal_ack, 1u)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send signal ack");
        goto cleanup;
    }
    signal_ack = 0;
    if (!cheng_v3_tcp_recv_exact_raw(control_fds[0], &signal_ack, 1u)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv signal ack");
        goto cleanup;
    }
    if (signal_ack != 1) {
        err = driver_c_str_from_utf8_copy_bridge(signal_ack_failed, (int32_t)strlen(signal_ack_failed));
        goto cleanup;
    }
    if (protocol_len > 0u) {
        server_protocol = (char*)malloc(protocol_len);
        client_protocol = (char*)malloc(protocol_len);
        if (server_protocol == NULL || client_protocol == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_send_all_raw(data_fds[0], protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send request");
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(data_fds[1], server_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv request");
            goto cleanup;
        }
        if (memcmp(server_protocol, protocol, protocol_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(request_mismatch, (int32_t)strlen(request_mismatch));
            goto cleanup;
        }
        if (!cheng_v3_tcp_send_all_raw(data_fds[1], protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send response");
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(data_fds[0], client_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv response");
            goto cleanup;
        }
        if (memcmp(client_protocol, protocol, protocol_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(response_mismatch, (int32_t)strlen(response_mismatch));
            goto cleanup;
        }
    }
    cheng_v3_tcp_write_u32be(frame_len_buf, (uint32_t)request_len);
    if (!cheng_v3_tcp_send_all_raw(data_fds[0], (const char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send request len");
        goto cleanup;
    }
    if (request_len > 0u && !cheng_v3_tcp_send_all_raw(data_fds[0], request, request_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send request payload");
        goto cleanup;
    }
    if (!cheng_v3_tcp_recv_exact_raw(data_fds[1], (char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv request len");
        goto cleanup;
    }
    frame_len = cheng_v3_tcp_read_u32be(frame_len_buf);
    if (frame_len != (uint32_t)request_len) {
        err = driver_c_str_from_utf8_copy_bridge(request_length_mismatch, (int32_t)strlen(request_length_mismatch));
        goto cleanup;
    }
    if (frame_len > 0u) {
        server_request = (char*)malloc((size_t)frame_len);
        if (server_request == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(data_fds[1], server_request, (size_t)frame_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv request payload");
            goto cleanup;
        }
        if (memcmp(server_request, request, (size_t)frame_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(request_payload_mismatch, (int32_t)strlen(request_payload_mismatch));
            goto cleanup;
        }
    }
    cheng_v3_tcp_write_u32be(frame_len_buf, (uint32_t)response_len);
    if (!cheng_v3_tcp_send_all_raw(data_fds[1], (const char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send response len");
        goto cleanup;
    }
    if (response_len > 0u && !cheng_v3_tcp_send_all_raw(data_fds[1], response_payload, response_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: send response payload");
        goto cleanup;
    }
    if (!cheng_v3_tcp_recv_exact_raw(data_fds[0], (char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv response len");
        goto cleanup;
    }
    frame_len = cheng_v3_tcp_read_u32be(frame_len_buf);
    if (frame_len > 0u) {
        client_response = (char*)malloc((size_t)frame_len);
        if (client_response == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(data_fds[0], client_response, (size_t)frame_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p webrtc: recv response payload");
            goto cleanup;
        }
    }
    response = driver_c_str_from_utf8_copy_bridge(client_response != NULL ? client_response : "",
                                                  (int32_t)frame_len);
cleanup:
    if (client_response_text != NULL) {
        *client_response_text = response;
    } else {
        cheng_v3_bridge_release_owned(response);
    }
    if (err_text != NULL) {
        *err_text = err;
    } else {
        cheng_v3_bridge_release_owned(err);
    }
    if (control_fds[0] >= 0) {
        close(control_fds[0]);
    }
    if (control_fds[1] >= 0) {
        close(control_fds[1]);
    }
    if (data_fds[0] >= 0) {
        close(data_fds[0]);
    }
    if (data_fds[1] >= 0) {
        close(data_fds[1]);
    }
    free(server_protocol);
    free(client_protocol);
    free(server_signal);
    free(server_request);
    free(client_response);
    return err.ptr == NULL || err.len == 0 ? 1 : 0;
#endif
}

#if !defined(_WIN32)
static int cheng_v3_browser_bridge_file_exists(const char* path) {
    return path != NULL && access(path, F_OK) == 0;
}

static int cheng_v3_browser_bridge_write_file(const char* path, const char* data, size_t len) {
    FILE* f = NULL;
    size_t n = 0u;
    if (path == NULL || path[0] == '\0') return 0;
    f = fopen(path, "wb");
    if (f == NULL) return 0;
    if (len > 0u) {
        n = fwrite(data, 1u, len, f);
    }
    fclose(f);
    return n == len;
}

static int cheng_v3_browser_bridge_read_file(const char* path, char** out, size_t* out_len) {
    FILE* f = NULL;
    long size = 0;
    char* buf = NULL;
    size_t n = 0u;
    if (out != NULL) *out = NULL;
    if (out_len != NULL) *out_len = 0u;
    if (path == NULL || path[0] == '\0') return 0;
    f = fopen(path, "rb");
    if (f == NULL) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    if (size > 0) {
        buf = (char*)malloc((size_t)size);
        if (buf == NULL) {
            fclose(f);
            return 0;
        }
        n = fread(buf, 1u, (size_t)size, f);
        if (n != (size_t)size) {
            free(buf);
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    if (out != NULL) *out = buf;
    else free(buf);
    if (out_len != NULL) *out_len = (size_t)size;
    return 1;
}

static void cheng_v3_browser_bridge_cleanup_tmpdir(const char* tmp_dir) {
    char path[PATH_MAX];
    static const char* files[] = {
        "protocol.bin",
        "policy.bin",
        "label.txt",
        "request.bin",
        "response.bin",
        "signal.bin",
        "client_response.bin",
        "err.txt"
    };
    size_t i = 0u;
    if (tmp_dir == NULL || tmp_dir[0] == '\0') return;
    for (i = 0u; i < sizeof(files) / sizeof(files[0]); i += 1u) {
        snprintf(path, sizeof(path), "%s/%s", tmp_dir, files[i]);
        unlink(path);
    }
    rmdir(tmp_dir);
}

static int cheng_v3_browser_bridge_resolve_repo_root(char* out, size_t out_cap) {
    char cwd[PATH_MAX];
    char exe_path[PATH_MAX];
    char script_path[PATH_MAX];
    char* artifacts = NULL;
    if (out == NULL || out_cap == 0u) return 0;
    out[0] = '\0';
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        snprintf(script_path, sizeof(script_path), "%s/v3/tooling/browser_webrtc/run_browser_webrtc_bridge.mjs", cwd);
        if (cheng_v3_browser_bridge_file_exists(script_path)) {
            snprintf(out, out_cap, "%s", cwd);
            return 1;
        }
    }
    if (!driver_c_runtime_resolve_self_path(exe_path, sizeof(exe_path))) {
        return 0;
    }
    artifacts = strstr(exe_path, "/artifacts/");
    if (artifacts == NULL) {
        return 0;
    }
    *artifacts = '\0';
    snprintf(script_path, sizeof(script_path), "%s/v3/tooling/browser_webrtc/run_browser_webrtc_bridge.mjs", exe_path);
    if (!cheng_v3_browser_bridge_file_exists(script_path)) {
        return 0;
    }
    snprintf(out, out_cap, "%s", exe_path);
    return 1;
}

static int64_t cheng_v3_browser_bridge_spawn_node(const char* script_path, const char* tmp_dir) {
#if defined(__ANDROID__)
    (void)script_path;
    (void)tmp_dir;
    return -1;
#else
    posix_spawn_file_actions_t actions;
    pid_t pid = -1;
    int spawn_rc = 0;
    char* argv[4];
    argv[0] = (char*)"node";
    argv[1] = (char*)script_path;
    argv[2] = (char*)tmp_dir;
    argv[3] = NULL;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return -1;
    }
    if (posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) != 0 ||
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }
    spawn_rc = posix_spawnp(&pid, "node", &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_rc != 0) {
        return 127;
    }
    return cheng_exec_wait_status(pid, 30);
#endif
}

typedef struct ChengV3BrowserSession {
    int32_t read_fd;
    int32_t write_fd;
    int64_t pid;
    char* pending;
    size_t pending_len;
} ChengV3BrowserSession;

static int32_t cheng_v3_browser_session_active_count = 0;

static char* cheng_v3_browser_bridge_hex_encode(const char* data, size_t len) {
    static const char* hex = "0123456789abcdef";
    char* out = (char*)malloc((len * 2u) + 1u);
    size_t i = 0u;
    if (out == NULL) return NULL;
    for (i = 0u; i < len; i += 1u) {
        unsigned char byte = (unsigned char)data[i];
        out[i * 2u] = hex[(byte >> 4) & 0x0fu];
        out[(i * 2u) + 1u] = hex[byte & 0x0fu];
    }
    out[len * 2u] = '\0';
    return out;
}

static int cheng_v3_browser_bridge_hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return (int)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return 10 + (int)(ch - 'a');
    if (ch >= 'A' && ch <= 'F') return 10 + (int)(ch - 'A');
    return -1;
}

static int cheng_v3_browser_bridge_hex_decode_dup(const char* hex_text,
                                                  size_t hex_len,
                                                  char** out,
                                                  size_t* out_len) {
    size_t i = 0u;
    size_t bytes_len = 0u;
    char* bytes = NULL;
    if (out != NULL) *out = NULL;
    if (out_len != NULL) *out_len = 0u;
    if (hex_text == NULL) return 0;
    if ((hex_len % 2u) != 0u) return 0;
    bytes_len = hex_len / 2u;
    if (bytes_len == 0u) {
        if (out != NULL) *out = NULL;
        if (out_len != NULL) *out_len = 0u;
        return 1;
    }
    bytes = (char*)malloc(bytes_len);
    if (bytes == NULL) return 0;
    for (i = 0u; i < bytes_len; i += 1u) {
        int hi = cheng_v3_browser_bridge_hex_digit(hex_text[i * 2u]);
        int lo = cheng_v3_browser_bridge_hex_digit(hex_text[(i * 2u) + 1u]);
        if (hi < 0 || lo < 0) {
            free(bytes);
            return 0;
        }
        bytes[i] = (char)((hi << 4) | lo);
    }
    if (out != NULL) *out = bytes;
    else free(bytes);
    if (out_len != NULL) *out_len = bytes_len;
    return 1;
}

static int cheng_v3_browser_bridge_append_pending(ChengV3BrowserSession* session,
                                                  const char* chunk,
                                                  size_t chunk_len) {
    char* grown = NULL;
    if (session == NULL || chunk_len == 0u) return 1;
    grown = (char*)realloc(session->pending, session->pending_len + chunk_len + 1u);
    if (grown == NULL) return 0;
    memcpy(grown + session->pending_len, chunk, chunk_len);
    session->pending = grown;
    session->pending_len += chunk_len;
    session->pending[session->pending_len] = '\0';
    return 1;
}

static int cheng_v3_browser_bridge_write_all_fd(int32_t fd, const char* data, size_t len) {
    size_t off = 0u;
    while (off < len) {
        ssize_t rc = write(fd, data + off, len - off);
        if (rc > 0) {
            off += (size_t)rc;
            continue;
        }
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            if (poll(&pfd, 1, 100) > 0) {
                continue;
            }
        }
        return 0;
    }
    return 1;
}

static int cheng_v3_browser_bridge_read_line(ChengV3BrowserSession* session,
                                             char** out_line,
                                             int timeout_ms) {
    int waited = 0;
    if (out_line != NULL) *out_line = NULL;
    if (session == NULL || out_line == NULL) return 0;
    while (waited <= timeout_ms) {
        size_t i = 0u;
        for (i = 0u; i < session->pending_len; i += 1u) {
            if (session->pending[i] == '\n') {
                char* line = (char*)malloc(i + 1u);
                size_t remain = session->pending_len - (i + 1u);
                if (line == NULL) return 0;
                memcpy(line, session->pending, i);
                line[i] = '\0';
                if (remain > 0u) {
                    memmove(session->pending, session->pending + i + 1u, remain);
                }
                session->pending_len = remain;
                if (session->pending != NULL) {
                    session->pending[session->pending_len] = '\0';
                }
                *out_line = line;
                return 1;
            }
        }
        {
            int32_t eof = 0;
            char* chunk = cheng_fd_read_wait(session->read_fd, 4096, 50, &eof);
            size_t chunk_len = chunk != NULL ? strlen(chunk) : 0u;
            if (chunk_len > 0u) {
                int ok = cheng_v3_browser_bridge_append_pending(session, chunk, chunk_len);
                cheng_free(chunk);
                if (!ok) return 0;
                continue;
            }
            cheng_free(chunk);
            if (eof) return 0;
        }
        waited += 50;
    }
    return 0;
}

static int cheng_v3_browser_bridge_json_field_hex(const char* line,
                                                  const char* key,
                                                  char** out_hex) {
    char needle[64];
    const char* pos = NULL;
    const char* start = NULL;
    const char* end = NULL;
    size_t len = 0u;
    char* dup = NULL;
    if (out_hex != NULL) *out_hex = NULL;
    if (line == NULL || key == NULL || out_hex == NULL) return 0;
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    pos = strstr(line, needle);
    if (pos == NULL) return 0;
    start = pos + strlen(needle);
    end = strchr(start, '"');
    if (end == NULL) return 0;
    len = (size_t)(end - start);
    dup = (char*)malloc(len + 1u);
    if (dup == NULL) return 0;
    memcpy(dup, start, len);
    dup[len] = '\0';
    *out_hex = dup;
    return 1;
}

static int cheng_v3_browser_bridge_json_ok(const char* line, int* out_ok) {
    const char* pos = NULL;
    if (out_ok != NULL) *out_ok = 0;
    if (line == NULL || out_ok == NULL) return 0;
    pos = strstr(line, "\"ok\":");
    if (pos == NULL) return 0;
    pos += 5;
    if (strncmp(pos, "true", 4) == 0) {
        *out_ok = 1;
        return 1;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out_ok = 0;
        return 1;
    }
    return 0;
}

static void cheng_v3_browser_session_release(ChengV3BrowserSession* session) {
    if (session == NULL) return;
    if (session->write_fd >= 0) {
        close(session->write_fd);
        session->write_fd = -1;
    }
    if (session->read_fd >= 0) {
        close(session->read_fd);
        session->read_fd = -1;
    }
    if (session->pid > 0) {
        int status = 0;
        (void)waitpid((pid_t)session->pid, &status, 0);
        session->pid = -1;
    }
    free(session->pending);
    session->pending = NULL;
    session->pending_len = 0u;
    free(session);
    if (cheng_v3_browser_session_active_count > 0) {
        cheng_v3_browser_session_active_count -= 1;
    }
}

static int cheng_v3_browser_session_send_command(ChengV3BrowserSession* session,
                                                 const char* command_json,
                                                 char** out_line) {
    size_t json_len = 0u;
    if (session == NULL || command_json == NULL) return 0;
    json_len = strlen(command_json);
    if (!cheng_v3_browser_bridge_write_all_fd(session->write_fd, command_json, json_len)) {
        return 0;
    }
    if (!cheng_v3_browser_bridge_write_all_fd(session->write_fd, "\n", 1u)) {
        return 0;
    }
    return cheng_v3_browser_bridge_read_line(session, out_line, 10000);
}

static int cheng_v3_browser_session_spawn_handle(const char* repo_root,
                                                 ChengV3BrowserSession** out_session) {
    char command[PATH_MAX * 2];
    ChengV3BrowserSession* session = NULL;
    if (out_session != NULL) *out_session = NULL;
    if (repo_root == NULL || repo_root[0] == '\0') return 0;
    session = (ChengV3BrowserSession*)calloc(1u, sizeof(ChengV3BrowserSession));
    if (session == NULL) return 0;
    session->read_fd = -1;
    session->write_fd = -1;
    session->pid = -1;
    session->pending = NULL;
    session->pending_len = 0u;
    snprintf(command,
             sizeof(command),
             "node '%s/v3/tooling/browser_webrtc/run_browser_webrtc_bridge.mjs' --session",
             repo_root);
    if (!cheng_pipe_spawn(command, repo_root, &session->read_fd, &session->write_fd, &session->pid)) {
        free(session);
        return 0;
    }
    cheng_v3_browser_session_active_count += 1;
    *out_session = session;
    return 1;
}

static int cheng_v3_browser_session_send_open(ChengV3BrowserSession* session,
                                              const char* policy,
                                              size_t policy_len,
                                              const char* label,
                                              size_t label_len,
                                              ChengStrBridge* signal_out,
                                              ChengStrBridge* err_out) {
    char* policy_hex = NULL;
    char* label_hex = NULL;
    char* command = NULL;
    char* line = NULL;
    char* signal_hex = NULL;
    char* err_hex = NULL;
    char* signal_buf = NULL;
    size_t signal_len = 0u;
    int ok = 0;
    int rc = 0;
    if (signal_out != NULL) *signal_out = cheng_str_bridge_empty();
    if (err_out != NULL) *err_out = cheng_str_bridge_empty();
    policy_hex = cheng_v3_browser_bridge_hex_encode(policy, policy_len);
    label_hex = cheng_v3_browser_bridge_hex_encode(label, label_len);
    if (policy_hex == NULL || label_hex == NULL) goto cleanup;
    command = (char*)malloc(strlen(policy_hex) + strlen(label_hex) + 64u);
    if (command == NULL) goto cleanup;
    snprintf(command,
             strlen(policy_hex) + strlen(label_hex) + 64u,
             "{\"command\":\"open\",\"policyHex\":\"%s\",\"labelHex\":\"%s\"}",
             policy_hex,
             label_hex);
    if (!cheng_v3_browser_session_send_command(session, command, &line)) goto cleanup;
    if (!cheng_v3_browser_bridge_json_ok(line, &ok)) goto cleanup;
    if (!ok) {
        if (cheng_v3_browser_bridge_json_field_hex(line, "errHex", &err_hex) &&
            cheng_v3_browser_bridge_hex_decode_dup(err_hex, strlen(err_hex), &signal_buf, &signal_len)) {
            if (err_out != NULL) *err_out = driver_c_str_from_utf8_copy_bridge(signal_buf, (int32_t)signal_len);
            rc = 0;
            goto cleanup;
        }
        goto cleanup;
    }
    if (!cheng_v3_browser_bridge_json_field_hex(line, "signalHex", &signal_hex)) goto cleanup;
    if (!cheng_v3_browser_bridge_hex_decode_dup(signal_hex, strlen(signal_hex), &signal_buf, &signal_len)) goto cleanup;
    if (signal_out != NULL) *signal_out = driver_c_str_from_utf8_copy_bridge(signal_buf != NULL ? signal_buf : "", (int32_t)signal_len);
    rc = 1;
cleanup:
    free(policy_hex);
    free(label_hex);
    free(command);
    free(line);
    free(signal_hex);
    free(err_hex);
    free(signal_buf);
    return rc;
}

static int cheng_v3_browser_session_send_exchange(ChengV3BrowserSession* session,
                                                  const char* protocol,
                                                  size_t protocol_len,
                                                  const char* request,
                                                  size_t request_len,
                                                  const char* response_payload,
                                                  size_t response_len,
                                                  ChengStrBridge* response_out,
                                                  ChengStrBridge* err_out) {
    char* protocol_hex = NULL;
    char* request_hex = NULL;
    char* response_hex = NULL;
    char* command = NULL;
    char* line = NULL;
    char* reply_hex = NULL;
    char* err_hex = NULL;
    char* reply_buf = NULL;
    size_t reply_len = 0u;
    int ok = 0;
    int rc = 0;
    if (response_out != NULL) *response_out = cheng_str_bridge_empty();
    if (err_out != NULL) *err_out = cheng_str_bridge_empty();
    protocol_hex = cheng_v3_browser_bridge_hex_encode(protocol, protocol_len);
    request_hex = cheng_v3_browser_bridge_hex_encode(request, request_len);
    response_hex = cheng_v3_browser_bridge_hex_encode(response_payload, response_len);
    if (protocol_hex == NULL || request_hex == NULL || response_hex == NULL) goto cleanup;
    command = (char*)malloc(strlen(protocol_hex) + strlen(request_hex) + strlen(response_hex) + 96u);
    if (command == NULL) goto cleanup;
    snprintf(command,
             strlen(protocol_hex) + strlen(request_hex) + strlen(response_hex) + 96u,
             "{\"command\":\"exchange\",\"protocolHex\":\"%s\",\"requestHex\":\"%s\",\"responseHex\":\"%s\"}",
             protocol_hex,
             request_hex,
             response_hex);
    if (!cheng_v3_browser_session_send_command(session, command, &line)) goto cleanup;
    if (!cheng_v3_browser_bridge_json_ok(line, &ok)) goto cleanup;
    if (!ok) {
        if (cheng_v3_browser_bridge_json_field_hex(line, "errHex", &err_hex) &&
            cheng_v3_browser_bridge_hex_decode_dup(err_hex, strlen(err_hex), &reply_buf, &reply_len)) {
            if (err_out != NULL) *err_out = driver_c_str_from_utf8_copy_bridge(reply_buf, (int32_t)reply_len);
            rc = 0;
            goto cleanup;
        }
        goto cleanup;
    }
    if (!cheng_v3_browser_bridge_json_field_hex(line, "responseHex", &reply_hex)) goto cleanup;
    if (!cheng_v3_browser_bridge_hex_decode_dup(reply_hex, strlen(reply_hex), &reply_buf, &reply_len)) goto cleanup;
    if (response_out != NULL) *response_out = driver_c_str_from_utf8_copy_bridge(reply_buf != NULL ? reply_buf : "", (int32_t)reply_len);
    rc = 1;
cleanup:
    free(protocol_hex);
    free(request_hex);
    free(response_hex);
    free(command);
    free(line);
    free(reply_hex);
    free(err_hex);
    free(reply_buf);
    return rc;
}

static int cheng_v3_browser_session_send_close(ChengV3BrowserSession* session, ChengStrBridge* err_out) {
    char* line = NULL;
    char* err_hex = NULL;
    char* err_buf = NULL;
    size_t err_len = 0u;
    int ok = 0;
    if (err_out != NULL) *err_out = cheng_str_bridge_empty();
    if (!cheng_v3_browser_session_send_command(session, "{\"command\":\"close\"}", &line)) {
        return 0;
    }
    if (!cheng_v3_browser_bridge_json_ok(line, &ok)) {
        free(line);
        return 0;
    }
    if (!ok) {
        if (cheng_v3_browser_bridge_json_field_hex(line, "errHex", &err_hex) &&
            cheng_v3_browser_bridge_hex_decode_dup(err_hex, strlen(err_hex), &err_buf, &err_len)) {
            if (err_out != NULL) *err_out = driver_c_str_from_utf8_copy_bridge(err_buf, (int32_t)err_len);
        }
        free(line);
        free(err_hex);
        free(err_buf);
        return 0;
    }
    free(line);
    free(err_hex);
    free(err_buf);
    return 1;
}
#endif

WEAK uint64_t cheng_v3_webrtc_browser_session_open_bridge(ChengStrBridge policy_text,
                                                          ChengStrBridge label_text,
                                                          ChengStrBridge* signal_response_text,
                                                          ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p webrtc browser: win32 session bridge not implemented";
    if (signal_response_text != NULL) *signal_response_text = driver_c_str_from_utf8_copy_bridge("", 0);
    if (err_text != NULL) *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    (void)policy_text;
    (void)label_text;
    return 0;
#else
    const char* repo_root_missing = "v3 libp2p webrtc browser: repo root missing";
    const char* policy_missing = "v3 libp2p webrtc browser: policy payload missing";
    const char* spawn_failed = "v3 libp2p webrtc browser: session spawn failed";
    const char* open_failed = "v3 libp2p webrtc browser: session open failed";
    const char* policy = "";
    const char* label = "";
    size_t policy_len = 0u;
    size_t label_len = 0u;
    char repo_root[PATH_MAX];
    ChengV3BrowserSession* session = NULL;
    ChengStrBridge signal_out = cheng_str_bridge_empty();
    ChengStrBridge err = cheng_str_bridge_empty();
    if (signal_response_text != NULL) *signal_response_text = signal_out;
    if (err_text != NULL) *err_text = err;
    if (!cheng_str_bridge_view(policy_text, &policy, &policy_len)) {
        policy = "";
        policy_len = 0u;
    }
    if (!cheng_str_bridge_view(label_text, &label, &label_len)) {
        label = "";
        label_len = 0u;
    }
    if (policy_len == 0u) {
        err = driver_c_str_from_utf8_copy_bridge(policy_missing, (int32_t)strlen(policy_missing));
        goto cleanup;
    }
    if (!cheng_v3_browser_bridge_resolve_repo_root(repo_root, sizeof(repo_root))) {
        err = driver_c_str_from_utf8_copy_bridge(repo_root_missing, (int32_t)strlen(repo_root_missing));
        goto cleanup;
    }
    if (!cheng_v3_browser_session_spawn_handle(repo_root, &session)) {
        err = driver_c_str_from_utf8_copy_bridge(spawn_failed, (int32_t)strlen(spawn_failed));
        goto cleanup;
    }
    if (!cheng_v3_browser_session_send_open(session, policy, policy_len, label, label_len, &signal_out, &err)) {
        if (err.ptr == NULL || err.len == 0) {
            err = driver_c_str_from_utf8_copy_bridge(open_failed, (int32_t)strlen(open_failed));
        }
        goto cleanup;
    }
    if (signal_response_text != NULL) *signal_response_text = signal_out;
    if (err_text != NULL) *err_text = err;
    return cheng_ffi_handle_register_ptr(session);
cleanup:
    if (session != NULL) {
        cheng_v3_browser_session_release(session);
        session = NULL;
    }
    if (signal_response_text != NULL) *signal_response_text = signal_out;
    else cheng_v3_bridge_release_owned(signal_out);
    if (err_text != NULL) *err_text = err;
    else cheng_v3_bridge_release_owned(err);
    return 0;
#endif
}

WEAK int32_t cheng_v3_webrtc_browser_session_exchange_bridge(uint64_t handle,
                                                             ChengStrBridge protocol_text,
                                                             ChengStrBridge request_text,
                                                             ChengStrBridge response_text,
                                                             ChengStrBridge* client_response_text,
                                                             ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p webrtc browser: win32 session exchange not implemented";
    if (client_response_text != NULL) *client_response_text = driver_c_str_from_utf8_copy_bridge("", 0);
    if (err_text != NULL) *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    (void)handle;
    (void)protocol_text;
    (void)request_text;
    (void)response_text;
    return 0;
#else
    const char* exchange_failed = "v3 libp2p webrtc browser: session exchange failed";
    const char* protocol = "";
    const char* request = "";
    const char* response_payload = "";
    size_t protocol_len = 0u;
    size_t request_len = 0u;
    size_t response_len = 0u;
    ChengV3BrowserSession* session = (ChengV3BrowserSession*)cheng_ffi_handle_resolve_ptr(handle);
    ChengStrBridge response_out = cheng_str_bridge_empty();
    ChengStrBridge err = cheng_str_bridge_empty();
    if (client_response_text != NULL) *client_response_text = response_out;
    if (err_text != NULL) *err_text = err;
    if (!cheng_str_bridge_view(protocol_text, &protocol, &protocol_len)) {
        protocol = "";
        protocol_len = 0u;
    }
    if (!cheng_str_bridge_view(request_text, &request, &request_len)) {
        request = "";
        request_len = 0u;
    }
    if (!cheng_str_bridge_view(response_text, &response_payload, &response_len)) {
        response_payload = "";
        response_len = 0u;
    }
    if (!cheng_v3_browser_session_send_exchange(session,
                                                protocol,
                                                protocol_len,
                                                request,
                                                request_len,
                                                response_payload,
                                                response_len,
                                                &response_out,
                                                &err)) {
        if (err.ptr == NULL || err.len == 0) {
            err = driver_c_str_from_utf8_copy_bridge(exchange_failed, (int32_t)strlen(exchange_failed));
        }
    }
    if (client_response_text != NULL) *client_response_text = response_out;
    else cheng_v3_bridge_release_owned(response_out);
    if (err_text != NULL) *err_text = err;
    else cheng_v3_bridge_release_owned(err);
    return err.ptr == NULL || err.len == 0 ? 1 : 0;
#endif
}

WEAK int32_t cheng_v3_webrtc_browser_session_close_bridge(uint64_t handle,
                                                          ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p webrtc browser: win32 session close not implemented";
    if (err_text != NULL) *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    (void)handle;
    return 0;
#else
    ChengV3BrowserSession* session = (ChengV3BrowserSession*)cheng_ffi_handle_resolve_ptr(handle);
    ChengStrBridge err = cheng_str_bridge_empty();
    int ok = cheng_v3_browser_session_send_close(session, &err);
    if (cheng_ffi_handle_invalidate(handle) != 0) {
        ok = 0;
    }
    cheng_v3_browser_session_release(session);
    if (err_text != NULL) *err_text = err;
    else cheng_v3_bridge_release_owned(err);
    return ok && (err.ptr == NULL || err.len == 0) ? 1 : 0;
#endif
}

WEAK int32_t cheng_v3_webrtc_browser_session_active_count_bridge(void) {
    return cheng_v3_browser_session_active_count;
}

static void cheng_v3_bio_safe_copy(char* dst, size_t cap, const char* src) {
    if (dst == NULL || cap == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = '\0';
}

static int cheng_v3_bio_hex_nibble(char ch, uint8_t* out) {
    if (out == NULL) {
        return 0;
    }
    if (ch >= '0' && ch <= '9') {
        *out = (uint8_t)(ch - '0');
        return 1;
    }
    if (ch >= 'a' && ch <= 'f') {
        *out = (uint8_t)(10 + (ch - 'a'));
        return 1;
    }
    if (ch >= 'A' && ch <= 'F') {
        *out = (uint8_t)(10 + (ch - 'A'));
        return 1;
    }
    return 0;
}

static int cheng_v3_bio_wire_find_value(const char* wire,
                                        const char* key,
                                        const char** out_value,
                                        size_t* out_len) {
    if (wire == NULL || key == NULL || out_value == NULL || out_len == NULL) {
        return 0;
    }
    const size_t key_len = strlen(key);
    const char* p = wire;
    while (*p != '\0') {
        const char* line_end = strchr(p, '\n');
        if (line_end == NULL) {
            line_end = p + strlen(p);
        }
        const char* eq = memchr(p, '=', (size_t)(line_end - p));
        if (eq != NULL && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
            *out_value = eq + 1;
            *out_len = (size_t)(line_end - (eq + 1));
            return 1;
        }
        if (*line_end == '\0') {
            break;
        }
        p = line_end + 1;
    }
    return 0;
}

static int cheng_v3_bio_wire_copy_text(const char* wire, const char* key, char* out, size_t out_cap) {
    const char* value = NULL;
    size_t len = 0u;
    if (!cheng_v3_bio_wire_find_value(wire, key, &value, &len)) {
        cheng_v3_bio_safe_copy(out, out_cap, "");
        return 1;
    }
    if (out == NULL || out_cap == 0u || out_cap <= len) {
        return 0;
    }
    memcpy(out, value, len);
    out[len] = '\0';
    return 1;
}

static int cheng_v3_bio_wire_copy_hex_decoded(const char* wire, const char* key, char* out, size_t out_cap) {
    const char* value = NULL;
    size_t len = 0u;
    if (!cheng_v3_bio_wire_find_value(wire, key, &value, &len)) {
        cheng_v3_bio_safe_copy(out, out_cap, "");
        return 1;
    }
    if (out == NULL || out_cap == 0u || (len % 2u) != 0u) {
        return 0;
    }
    const size_t out_len = len / 2u;
    if (out_cap <= out_len) {
        return 0;
    }
    for (size_t i = 0u; i < out_len; i += 1u) {
        uint8_t hi = 0u;
        uint8_t lo = 0u;
        if (!cheng_v3_bio_hex_nibble(value[i * 2u], &hi) ||
            !cheng_v3_bio_hex_nibble(value[i * 2u + 1u], &lo)) {
            return 0;
        }
        out[i] = (char)((hi << 4u) | lo);
    }
    out[out_len] = '\0';
    return 1;
}

static int cheng_v3_bio_hex_encode_utf8(const char* src, char* out, size_t out_cap) {
    static const char* digits = "0123456789abcdef";
    if (out == NULL || out_cap == 0u) {
        return 0;
    }
    if (src == NULL || src[0] == '\0') {
        out[0] = '\0';
        return 1;
    }
    const size_t n = strlen(src);
    if (out_cap <= (n * 2u)) {
        return 0;
    }
    for (size_t i = 0u; i < n; i += 1u) {
        const uint8_t value = (uint8_t)src[i];
        out[i * 2u] = digits[value >> 4u];
        out[i * 2u + 1u] = digits[value & 0x0fu];
    }
    out[n * 2u] = '\0';
    return 1;
}

WEAK ChengStrBridge cheng_v3_mobile_biometric_fingerprint_authorize_bridge_native_impl(ChengStrBridge request_wire) {
    if (cheng_mobile_host_biometric_fingerprint_authorize_bridge == NULL) {
        char error_hex[1024];
        char response_raw[2048];
        error_hex[0] = '\0';
        (void)cheng_v3_bio_hex_encode_utf8("v3 biometric: mobile host bridge missing", error_hex, sizeof(error_hex));
        (void)snprintf(response_raw,
                       sizeof(response_raw),
                       "ok=0\nfeature32_hex=\ndevice_binding_seed_hex=\ndevice_label_hex=\nsensor_id_hex=\nhardware_attestation_hex=\nerror_hex=%s\n",
                       error_hex);
        return driver_c_str_from_utf8_copy_bridge(response_raw, (int32_t)strlen(response_raw));
    }

    const char* request_wire_raw = cheng_str_to_cstring_temp_bridge(request_wire);
    char request_id_raw[256];
    char purpose_raw[32];
    char did_text_raw[8192];
    char prompt_title_raw[512];
    char prompt_reason_raw[1024];
    char device_binding_seed_hint_raw[512];
    char device_label_hint_raw[512];
    request_id_raw[0] = '\0';
    purpose_raw[0] = '\0';
    did_text_raw[0] = '\0';
    prompt_title_raw[0] = '\0';
    prompt_reason_raw[0] = '\0';
    device_binding_seed_hint_raw[0] = '\0';
    device_label_hint_raw[0] = '\0';

    if (request_wire_raw == NULL ||
        !cheng_v3_bio_wire_copy_hex_decoded(request_wire_raw, "request_id", request_id_raw, sizeof(request_id_raw)) ||
        !cheng_v3_bio_wire_copy_text(request_wire_raw, "purpose", purpose_raw, sizeof(purpose_raw)) ||
        !cheng_v3_bio_wire_copy_hex_decoded(request_wire_raw, "did", did_text_raw, sizeof(did_text_raw)) ||
        !cheng_v3_bio_wire_copy_hex_decoded(request_wire_raw, "prompt_title", prompt_title_raw, sizeof(prompt_title_raw)) ||
        !cheng_v3_bio_wire_copy_hex_decoded(request_wire_raw, "prompt_reason", prompt_reason_raw, sizeof(prompt_reason_raw)) ||
        !cheng_v3_bio_wire_copy_hex_decoded(request_wire_raw, "device_binding_seed_hint", device_binding_seed_hint_raw, sizeof(device_binding_seed_hint_raw)) ||
        !cheng_v3_bio_wire_copy_hex_decoded(request_wire_raw, "device_label_hint", device_label_hint_raw, sizeof(device_label_hint_raw))) {
        char error_hex[1024];
        char response_raw[2048];
        error_hex[0] = '\0';
        (void)cheng_v3_bio_hex_encode_utf8("v3 biometric: invalid request wire", error_hex, sizeof(error_hex));
        (void)snprintf(response_raw,
                       sizeof(response_raw),
                       "ok=0\nfeature32_hex=\ndevice_binding_seed_hex=\ndevice_label_hex=\nsensor_id_hex=\nhardware_attestation_hex=\nerror_hex=%s\n",
                       error_hex);
        return driver_c_str_from_utf8_copy_bridge(response_raw, (int32_t)strlen(response_raw));
    }

    const int32_t purpose = (int32_t)strtol(purpose_raw, NULL, 10);
    char feature32_hex_raw[65];
    char device_binding_seed_raw[512];
    char device_label_raw[512];
    char sensor_id_raw[256];
    char hardware_attestation_raw[8192];
    char error_raw[512];
    feature32_hex_raw[0] = '\0';
    device_binding_seed_raw[0] = '\0';
    device_label_raw[0] = '\0';
    sensor_id_raw[0] = '\0';
    hardware_attestation_raw[0] = '\0';
    error_raw[0] = '\0';

    const int32_t ok = cheng_mobile_host_biometric_fingerprint_authorize_bridge(
        request_id_raw,
        purpose,
        did_text_raw,
        prompt_title_raw,
        prompt_reason_raw,
        device_binding_seed_hint_raw,
        device_label_hint_raw,
        feature32_hex_raw,
        (int32_t)sizeof(feature32_hex_raw),
        device_binding_seed_raw,
        (int32_t)sizeof(device_binding_seed_raw),
        device_label_raw,
        (int32_t)sizeof(device_label_raw),
        sensor_id_raw,
        (int32_t)sizeof(sensor_id_raw),
        hardware_attestation_raw,
        (int32_t)sizeof(hardware_attestation_raw),
        error_raw,
        (int32_t)sizeof(error_raw));

    if (ok == 0) {
        const char* err_raw_final = error_raw[0] != '\0' ? error_raw : "v3 biometric: host authorize failed";
        char error_hex[1024];
        char response_raw[2048];
        error_hex[0] = '\0';
        (void)cheng_v3_bio_hex_encode_utf8(err_raw_final, error_hex, sizeof(error_hex));
        (void)snprintf(response_raw,
                       sizeof(response_raw),
                       "ok=0\nfeature32_hex=\ndevice_binding_seed_hex=\ndevice_label_hex=\nsensor_id_hex=\nhardware_attestation_hex=\nerror_hex=%s\n",
                       error_hex);
        return driver_c_str_from_utf8_copy_bridge(response_raw, (int32_t)strlen(response_raw));
    }

    char device_binding_seed_hex[1024];
    char device_label_hex[1024];
    char sensor_id_hex[512];
    char hardware_attestation_hex[24576];
    char response_raw[32768];
    if (!cheng_v3_bio_hex_encode_utf8(device_binding_seed_raw, device_binding_seed_hex, sizeof(device_binding_seed_hex)) ||
        !cheng_v3_bio_hex_encode_utf8(device_label_raw, device_label_hex, sizeof(device_label_hex)) ||
        !cheng_v3_bio_hex_encode_utf8(sensor_id_raw, sensor_id_hex, sizeof(sensor_id_hex)) ||
        !cheng_v3_bio_hex_encode_utf8(hardware_attestation_raw, hardware_attestation_hex, sizeof(hardware_attestation_hex))) {
        char error_hex[1024];
        error_hex[0] = '\0';
        (void)cheng_v3_bio_hex_encode_utf8("v3 biometric: response wire overflow", error_hex, sizeof(error_hex));
        (void)snprintf(response_raw,
                       sizeof(response_raw),
                       "ok=0\nfeature32_hex=\ndevice_binding_seed_hex=\ndevice_label_hex=\nsensor_id_hex=\nhardware_attestation_hex=\nerror_hex=%s\n",
                       error_hex);
        return driver_c_str_from_utf8_copy_bridge(response_raw, (int32_t)strlen(response_raw));
    }

    response_raw[0] = '\0';
    (void)snprintf(response_raw,
                   sizeof(response_raw),
                   "ok=1\n"
                   "feature32_hex=%s\n"
                   "device_binding_seed_hex=%s\n"
                   "device_label_hex=%s\n"
                   "sensor_id_hex=%s\n"
                   "hardware_attestation_hex=%s\n"
                   "error_hex=\n",
                   feature32_hex_raw,
                   device_binding_seed_hex,
                   device_label_hex,
                   sensor_id_hex,
                   hardware_attestation_hex);
    return driver_c_str_from_utf8_copy_bridge(response_raw, (int32_t)strlen(response_raw));
}

typedef struct ChengV3TailnetProviderRuntime {
    int32_t provider_ready;
    int32_t proxy_ready;
    int32_t listener_needs_repair;
    int32_t derp_healthy;
    char startup_stage[64];
    char provider_kind[32];
    char control_endpoint[512];
    char relay_endpoint[512];
    char control_probe_endpoint[512];
    char relay_probe_endpoint[512];
    char derp_region_code[64];
    char derp_hostname[128];
    char last_error[256];
} ChengV3TailnetProviderRuntime;

static int32_t cheng_v3_tailnet_provider_active_count = 0;
static int32_t cheng_v3_tailnet_provider_command_count = 0;

static ChengV3TailnetProviderRuntime* cheng_v3_tailnet_provider_runtime_new(void) {
    ChengV3TailnetProviderRuntime* runtime = (ChengV3TailnetProviderRuntime*)calloc(1u, sizeof(ChengV3TailnetProviderRuntime));
    if (runtime == NULL) return NULL;
    snprintf(runtime->startup_stage, sizeof(runtime->startup_stage), "init");
    return runtime;
}

static void cheng_v3_tailnet_provider_copy_bridge_text(char* out,
                                                       size_t out_cap,
                                                       ChengStrBridge text) {
    const char* safe = "";
    size_t safe_len = 0u;
    if (out == NULL || out_cap == 0u) {
        return;
    }
    if (!cheng_str_bridge_view(text, &safe, &safe_len)) {
        safe = "";
        safe_len = 0u;
    }
    if (safe_len >= out_cap) {
        safe_len = out_cap - 1u;
    }
    if (safe_len > 0u) {
        memcpy(out, safe, safe_len);
    }
    out[safe_len] = '\0';
}

static void cheng_v3_tailnet_provider_set_stage(ChengV3TailnetProviderRuntime* runtime,
                                                const char* stage) {
    const char* safe = stage != NULL ? stage : "";
    if (runtime == NULL) {
        return;
    }
    snprintf(runtime->startup_stage, sizeof(runtime->startup_stage), "%s", safe);
}

static void cheng_v3_tailnet_provider_set_error(ChengV3TailnetProviderRuntime* runtime,
                                                const char* err) {
    const char* safe = err != NULL ? err : "";
    if (runtime == NULL) {
        return;
    }
    snprintf(runtime->last_error, sizeof(runtime->last_error), "%s", safe);
}

static int cheng_v3_tailnet_provider_has_prefix(const char* text,
                                                const char* prefix) {
    size_t text_len;
    size_t prefix_len;
    if (text == NULL || prefix == NULL) {
        return 0;
    }
    text_len = strlen(text);
    prefix_len = strlen(prefix);
    if (text_len < prefix_len) {
        return 0;
    }
    return memcmp(text, prefix, prefix_len) == 0 ? 1 : 0;
}

static char* cheng_v3_tailnet_provider_dup_range(const char* start,
                                                 size_t len) {
    char* out = (char*)malloc(len + 1u);
    if (out == NULL) {
        return NULL;
    }
    if (len > 0u) {
        memcpy(out, start, len);
    }
    out[len] = '\0';
    return out;
}

static int cheng_v3_tailnet_provider_http_split_url(const char* raw,
                                                    char** host_out,
                                                    char** port_out,
                                                    char** path_out) {
    const char* rest = NULL;
    const char* slash = NULL;
    const char* colon = NULL;
    char* authority = NULL;
    size_t authority_len = 0u;
    char* host = NULL;
    char* port = NULL;
    char* path = NULL;
    if (host_out == NULL || port_out == NULL || path_out == NULL) {
        return 0;
    }
    *host_out = NULL;
    *port_out = NULL;
    *path_out = NULL;
    if (raw == NULL || !cheng_v3_tailnet_provider_has_prefix(raw, "http://")) {
        return 0;
    }
    rest = raw + 7;
    if (rest[0] == '\0') {
        return 0;
    }
    slash = strchr(rest, '/');
    authority_len = slash != NULL ? (size_t)(slash - rest) : strlen(rest);
    authority = cheng_v3_tailnet_provider_dup_range(rest, authority_len);
    if (authority == NULL) {
        return 0;
    }
    if (authority[0] == '\0') {
        free(authority);
        return 0;
    }
    if (authority[0] == '[') {
        const char* close = strchr(authority, ']');
        if (close == NULL) {
            free(authority);
            return 0;
        }
        host = cheng_v3_tailnet_provider_dup_range(authority + 1,
                                                   (size_t)(close - authority - 1));
        if (close[1] == ':' && close[2] != '\0') {
            port = cheng_v3_bridge_dup_cstring(close + 2);
        }
    } else {
        colon = strrchr(authority, ':');
        if (colon != NULL && strchr(colon + 1, ':') == NULL) {
            host = cheng_v3_tailnet_provider_dup_range(authority,
                                                       (size_t)(colon - authority));
            port = cheng_v3_bridge_dup_cstring(colon + 1);
        } else {
            host = cheng_v3_bridge_dup_cstring(authority);
        }
    }
    if (host == NULL || host[0] == '\0') {
        free(authority);
        free(host);
        free(port);
        return 0;
    }
    if (port == NULL) {
        port = cheng_v3_bridge_dup_cstring("80");
    }
    if (port == NULL || port[0] == '\0') {
        free(authority);
        free(host);
        free(port);
        return 0;
    }
    if (slash != NULL) {
        path = cheng_v3_bridge_dup_cstring(slash);
    } else {
        path = cheng_v3_bridge_dup_cstring("/");
    }
    free(authority);
    if (path == NULL) {
        free(host);
        free(port);
        return 0;
    }
    *host_out = host;
    *port_out = port;
    *path_out = path;
    return 1;
}

static char* cheng_v3_tailnet_provider_http_get_dup(const char* url) {
    char* host = NULL;
    char* port = NULL;
    char* path = NULL;
    struct addrinfo hints;
    struct addrinfo* infos = NULL;
    struct addrinfo* info = NULL;
    int fd = -1;
    int status = 0;
    char request[1024];
    size_t used = 0u;
    size_t cap = 0u;
    char* buffer = NULL;
    char* body = NULL;
    char* header_end = NULL;
    memset(&hints, 0, sizeof(hints));
    if (!cheng_v3_tailnet_provider_http_split_url(url, &host, &port, &path)) {
        return NULL;
    }
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &infos) != 0) {
        free(host);
        free(port);
        free(path);
        return NULL;
    }
    for (info = infos; info != NULL; info = info->ai_next) {
        fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, info->ai_addr, info->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    if (fd < 0) {
        freeaddrinfo(infos);
        free(host);
        free(port);
        free(path);
        return NULL;
    }
    status = snprintf(request,
                      sizeof(request),
                      "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nAccept: application/json\r\n\r\n",
                      path,
                      host);
    if (status < 0 || (size_t)status >= sizeof(request) ||
        !cheng_v3_tcp_send_all_raw(fd, request, (size_t)status)) {
        close(fd);
        freeaddrinfo(infos);
        free(host);
        free(port);
        free(path);
        return NULL;
    }
    cap = 4096u;
    buffer = (char*)malloc(cap);
    if (buffer == NULL) {
        close(fd);
        freeaddrinfo(infos);
        free(host);
        free(port);
        free(path);
        return NULL;
    }
    while (1) {
        ssize_t rc;
        if (used + 2048u + 1u > cap) {
            size_t next_cap = cap * 2u;
            char* next = (char*)realloc(buffer, next_cap);
            if (next == NULL) {
                free(buffer);
                close(fd);
                freeaddrinfo(infos);
                free(host);
                free(port);
                free(path);
                return NULL;
            }
            buffer = next;
            cap = next_cap;
        }
        rc = recv(fd, buffer + used, cap - used - 1u, 0);
        if (rc < 0) {
            free(buffer);
            close(fd);
            freeaddrinfo(infos);
            free(host);
            free(port);
            free(path);
            return NULL;
        }
        if (rc == 0) {
            break;
        }
        used += (size_t)rc;
    }
    buffer[used] = '\0';
    close(fd);
    freeaddrinfo(infos);
    free(host);
    free(port);
    free(path);
    if (strncmp(buffer, "HTTP/", 5) != 0) {
        free(buffer);
        return NULL;
    }
    if (strstr(buffer, " 200 ") == NULL) {
        free(buffer);
        return NULL;
    }
    header_end = strstr(buffer, "\r\n\r\n");
    if (header_end != NULL) {
        header_end += 4;
    } else {
        header_end = strstr(buffer, "\n\n");
        if (header_end != NULL) {
            header_end += 2;
        }
    }
    if (header_end == NULL) {
        free(buffer);
        return NULL;
    }
    body = cheng_v3_bridge_dup_cstring(header_end);
    free(buffer);
    return body;
}

static char* cheng_v3_tailnet_provider_load_endpoint_dup(const char* endpoint) {
    char* clean = cheng_v3_bridge_trim_dup(endpoint);
    char* payload = NULL;
    if (clean == NULL || clean[0] == '\0') {
        free(clean);
        return NULL;
    }
    if (cheng_v3_tailnet_provider_has_prefix(clean, "file://")) {
        payload = cheng_v3_read_text_file_raw_dup(clean + 7);
    } else if (cheng_v3_tailnet_provider_has_prefix(clean, "http://")) {
        payload = cheng_v3_tailnet_provider_http_get_dup(clean);
    } else if (cheng_v3_tailnet_provider_has_prefix(clean, "https://")) {
        payload = NULL;
    } else {
        payload = cheng_v3_read_text_file_raw_dup(clean);
    }
    free(clean);
    return payload;
}

static const char* cheng_v3_tailnet_provider_json_value_after_key(const char* payload,
                                                                  const char* key) {
    char pattern[128];
    const char* hit = NULL;
    const char* value = NULL;
    int status;
    if (payload == NULL || key == NULL) {
        return NULL;
    }
    status = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (status < 0 || (size_t)status >= sizeof(pattern)) {
        return NULL;
    }
    hit = strstr(payload, pattern);
    if (hit == NULL) {
        return NULL;
    }
    value = hit + (size_t)status;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        value++;
    }
    if (*value != ':') {
        return NULL;
    }
    value++;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        value++;
    }
    return value;
}

static int cheng_v3_tailnet_provider_json_read_bool(const char* payload,
                                                    const char* key,
                                                    int32_t* out_value) {
    const char* value = cheng_v3_tailnet_provider_json_value_after_key(payload, key);
    if (value == NULL || out_value == NULL) {
        return 0;
    }
    if (strncmp(value, "true", 4) == 0 &&
        (value[4] == '\0' || value[4] == ',' || value[4] == '}' ||
         value[4] == '\r' || value[4] == '\n' || value[4] == ' ' || value[4] == '\t')) {
        *out_value = 1;
        return 1;
    }
    if (strncmp(value, "false", 5) == 0 &&
        (value[5] == '\0' || value[5] == ',' || value[5] == '}' ||
         value[5] == '\r' || value[5] == '\n' || value[5] == ' ' || value[5] == '\t')) {
        *out_value = 0;
        return 1;
    }
    return 0;
}

static int cheng_v3_tailnet_provider_json_read_string(const char* payload,
                                                      const char* key,
                                                      char* out,
                                                      size_t out_cap) {
    const char* value = cheng_v3_tailnet_provider_json_value_after_key(payload, key);
    size_t used = 0u;
    if (value == NULL || out == NULL || out_cap == 0u || *value != '"') {
        return 0;
    }
    value++;
    while (*value != '\0' && *value != '"') {
        char ch = *value++;
        if (ch == '\\' && *value != '\0') {
            ch = *value++;
        }
        if (used + 1u >= out_cap) {
            return 0;
        }
        out[used++] = ch;
    }
    if (*value != '"') {
        return 0;
    }
    out[used] = '\0';
    return 1;
}

static int cheng_v3_tailnet_provider_is_headscale(const ChengV3TailnetProviderRuntime* runtime) {
    if (runtime == NULL) {
        return 0;
    }
    return strcmp(runtime->provider_kind, "headscale") == 0 ? 1 : 0;
}

static int cheng_v3_tailnet_provider_sync_control(ChengV3TailnetProviderRuntime* runtime) {
    const char* endpoint = NULL;
    char* payload = NULL;
    int32_t provider_ready = 0;
    int32_t proxy_ready = 0;
    int32_t listener_needs_repair = 0;
    char startup_stage[64];
    memset(startup_stage, 0, sizeof(startup_stage));
    if (runtime == NULL) {
        return 0;
    }
    endpoint = runtime->control_probe_endpoint[0] != '\0'
                   ? runtime->control_probe_endpoint
                   : runtime->control_endpoint;
    payload = cheng_v3_tailnet_provider_load_endpoint_dup(endpoint);
    if (payload == NULL) {
        cheng_v3_tailnet_provider_set_error(runtime, "tailnet provider: control probe load failed");
        return 0;
    }
    if (!cheng_v3_tailnet_provider_json_read_bool(payload, "providerReady", &provider_ready) ||
        !cheng_v3_tailnet_provider_json_read_bool(payload, "proxyListenersReady", &proxy_ready) ||
        !cheng_v3_tailnet_provider_json_read_bool(payload, "listenerNeedsRepair", &listener_needs_repair) ||
        !cheng_v3_tailnet_provider_json_read_string(payload, "startupStage", startup_stage, sizeof(startup_stage))) {
        free(payload);
        cheng_v3_tailnet_provider_set_error(runtime, "tailnet provider: control probe parse failed");
        return 0;
    }
    free(payload);
    runtime->provider_ready = provider_ready;
    runtime->proxy_ready = proxy_ready;
    runtime->listener_needs_repair = listener_needs_repair;
    cheng_v3_tailnet_provider_set_stage(runtime, startup_stage);
    cheng_v3_tailnet_provider_set_error(runtime, "");
    return 1;
}

static int cheng_v3_tailnet_provider_sync_derp(ChengV3TailnetProviderRuntime* runtime) {
    const char* endpoint = NULL;
    char* payload = NULL;
    int32_t derp_healthy = 0;
    char startup_stage[64];
    memset(startup_stage, 0, sizeof(startup_stage));
    if (runtime == NULL) {
        return 0;
    }
    endpoint = runtime->relay_probe_endpoint[0] != '\0'
                   ? runtime->relay_probe_endpoint
                   : runtime->relay_endpoint;
    payload = cheng_v3_tailnet_provider_load_endpoint_dup(endpoint);
    if (payload == NULL) {
        cheng_v3_tailnet_provider_set_error(runtime, "tailnet provider: derp probe load failed");
        return 0;
    }
    if (!cheng_v3_tailnet_provider_json_read_bool(payload, "derpHealthy", &derp_healthy) ||
        !cheng_v3_tailnet_provider_json_read_string(payload, "startupStage", startup_stage, sizeof(startup_stage))) {
        free(payload);
        cheng_v3_tailnet_provider_set_error(runtime, "tailnet provider: derp probe parse failed");
        return 0;
    }
    free(payload);
    runtime->derp_healthy = derp_healthy;
    cheng_v3_tailnet_provider_set_stage(runtime, startup_stage);
    cheng_v3_tailnet_provider_set_error(runtime, "");
    return 1;
}

WEAK uint64_t cheng_v3_tailnet_provider_open_bridge(ChengStrBridge provider_kind_text,
                                                    ChengStrBridge control_endpoint_text,
                                                    ChengStrBridge relay_endpoint_text,
                                                    ChengStrBridge control_probe_endpoint_text,
                                                    ChengStrBridge relay_probe_endpoint_text,
                                                    ChengStrBridge derp_region_code_text,
                                                    ChengStrBridge derp_hostname_text) {
    ChengV3TailnetProviderRuntime* runtime = cheng_v3_tailnet_provider_runtime_new();
    if (runtime == NULL) return 0;
    cheng_v3_tailnet_provider_copy_bridge_text(runtime->provider_kind,
                                               sizeof(runtime->provider_kind),
                                               provider_kind_text);
    cheng_v3_tailnet_provider_copy_bridge_text(runtime->control_endpoint,
                                               sizeof(runtime->control_endpoint),
                                               control_endpoint_text);
    cheng_v3_tailnet_provider_copy_bridge_text(runtime->relay_endpoint,
                                               sizeof(runtime->relay_endpoint),
                                               relay_endpoint_text);
    cheng_v3_tailnet_provider_copy_bridge_text(runtime->control_probe_endpoint,
                                               sizeof(runtime->control_probe_endpoint),
                                               control_probe_endpoint_text);
    cheng_v3_tailnet_provider_copy_bridge_text(runtime->relay_probe_endpoint,
                                               sizeof(runtime->relay_probe_endpoint),
                                               relay_probe_endpoint_text);
    cheng_v3_tailnet_provider_copy_bridge_text(runtime->derp_region_code,
                                               sizeof(runtime->derp_region_code),
                                               derp_region_code_text);
    cheng_v3_tailnet_provider_copy_bridge_text(runtime->derp_hostname,
                                               sizeof(runtime->derp_hostname),
                                               derp_hostname_text);
    cheng_v3_tailnet_provider_active_count += 1;
    return cheng_ffi_handle_register_ptr(runtime);
}

WEAK int32_t cheng_v3_tailnet_provider_command_bridge(uint64_t handle,
                                                      ChengStrBridge command_text) {
    const char* command = "";
    size_t command_len = 0u;
    ChengV3TailnetProviderRuntime* runtime = (ChengV3TailnetProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    if (runtime == NULL) return -1;
    if (!cheng_str_bridge_view(command_text, &command, &command_len)) {
        command = "";
        command_len = 0u;
    }
    cheng_v3_tailnet_provider_command_count += 1;
    if (command_len == 14u && memcmp(command, "provider-start", 14u) == 0) {
        if (cheng_v3_tailnet_provider_is_headscale(runtime)) {
            return cheng_v3_tailnet_provider_sync_control(runtime) ? 0 : -1;
        }
        runtime->provider_ready = 1;
        runtime->proxy_ready = 0;
        runtime->listener_needs_repair = 0;
        cheng_v3_tailnet_provider_set_stage(runtime, "provider-started");
        cheng_v3_tailnet_provider_set_error(runtime, "");
        return 0;
    }
    if (command_len == 11u && memcmp(command, "proxy-ready", 11u) == 0) {
        if (cheng_v3_tailnet_provider_is_headscale(runtime)) {
            return cheng_v3_tailnet_provider_sync_control(runtime) ? 0 : -1;
        }
        runtime->provider_ready = 1;
        runtime->proxy_ready = 1;
        runtime->listener_needs_repair = 0;
        cheng_v3_tailnet_provider_set_stage(runtime, "proxy-ready");
        cheng_v3_tailnet_provider_set_error(runtime, "");
        return 0;
    }
    if (command_len == 13u && memcmp(command, "repair-needed", 13u) == 0) {
        runtime->provider_ready = 1;
        runtime->proxy_ready = 0;
        runtime->listener_needs_repair = 1;
        cheng_v3_tailnet_provider_set_stage(runtime, "repair-needed");
        cheng_v3_tailnet_provider_set_error(runtime, "");
        return 0;
    }
    if (command_len == 16u && memcmp(command, "repair-listeners", 16u) == 0) {
        if (cheng_v3_tailnet_provider_is_headscale(runtime)) {
            return cheng_v3_tailnet_provider_sync_control(runtime) ? 0 : -1;
        }
        runtime->provider_ready = 1;
        runtime->proxy_ready = 1;
        runtime->listener_needs_repair = 0;
        cheng_v3_tailnet_provider_set_stage(runtime, "repair-complete");
        cheng_v3_tailnet_provider_set_error(runtime, "");
        return 0;
    }
    if (command_len == 14u && memcmp(command, "derp-reconnect", 14u) == 0) {
        runtime->derp_healthy = 0;
        cheng_v3_tailnet_provider_set_stage(runtime, "derp-reconnect");
        cheng_v3_tailnet_provider_set_error(runtime, "");
        return 0;
    }
    if (command_len == 10u && memcmp(command, "derp-ready", 10u) == 0) {
        if (cheng_v3_tailnet_provider_is_headscale(runtime)) {
            return cheng_v3_tailnet_provider_sync_derp(runtime) ? 0 : -1;
        }
        runtime->derp_healthy = 1;
        cheng_v3_tailnet_provider_set_stage(runtime, "derp-ready");
        cheng_v3_tailnet_provider_set_error(runtime, "");
        return 0;
    }
    (void)command;
    (void)command_len;
    return -1;
}

WEAK int32_t cheng_v3_tailnet_provider_close_bridge(uint64_t handle) {
    ChengV3TailnetProviderRuntime* runtime = (ChengV3TailnetProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    if (runtime == NULL) return -1;
    if (cheng_ffi_handle_invalidate(handle) != 0) {
        return -1;
    }
    free(runtime);
    if (cheng_v3_tailnet_provider_active_count > 0) {
        cheng_v3_tailnet_provider_active_count -= 1;
    }
    return 0;
}

WEAK int32_t cheng_v3_tailnet_provider_active_count_bridge(void) {
    return cheng_v3_tailnet_provider_active_count;
}

WEAK int32_t cheng_v3_tailnet_provider_command_count_bridge(void) {
    return cheng_v3_tailnet_provider_command_count;
}

WEAK int32_t cheng_v3_tailnet_provider_probe_provider_ready_bridge(uint64_t handle) {
    ChengV3TailnetProviderRuntime* runtime = (ChengV3TailnetProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    if (runtime == NULL) return 0;
    return runtime->provider_ready != 0 ? 1 : 0;
}

WEAK int32_t cheng_v3_tailnet_provider_probe_proxy_ready_bridge(uint64_t handle) {
    ChengV3TailnetProviderRuntime* runtime = (ChengV3TailnetProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    if (runtime == NULL) return 0;
    return runtime->proxy_ready != 0 ? 1 : 0;
}

WEAK int32_t cheng_v3_tailnet_provider_probe_listener_needs_repair_bridge(uint64_t handle) {
    ChengV3TailnetProviderRuntime* runtime = (ChengV3TailnetProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    if (runtime == NULL) return 0;
    return runtime->listener_needs_repair != 0 ? 1 : 0;
}

WEAK int32_t cheng_v3_tailnet_provider_probe_derp_healthy_bridge(uint64_t handle) {
    ChengV3TailnetProviderRuntime* runtime = (ChengV3TailnetProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    if (runtime == NULL) return 0;
    return runtime->derp_healthy != 0 ? 1 : 0;
}

WEAK ChengStrBridge cheng_v3_tailnet_provider_probe_startup_stage_bridge(uint64_t handle) {
    ChengV3TailnetProviderRuntime* runtime = (ChengV3TailnetProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    if (runtime == NULL) {
        return cheng_v3_bridge_owned_str("");
    }
    return cheng_v3_bridge_owned_str(runtime->startup_stage);
}

typedef struct ChengV3RelayProviderRuntime {
    char service_kind[32];
    char endpoint[512];
    char region[64];
    char auth_scope[256];
    int32_t requires_lease;
    int32_t healthy;
    int32_t success_permille;
    int32_t load_permille;
    int32_t capacity_permille;
    char last_error[256];
} ChengV3RelayProviderRuntime;

static ChengV3RelayProviderRuntime* cheng_v3_relay_provider_runtime_new(void) {
    ChengV3RelayProviderRuntime* runtime = (ChengV3RelayProviderRuntime*)calloc(1u, sizeof(ChengV3RelayProviderRuntime));
    if (runtime == NULL) return NULL;
    runtime->healthy = 0;
    runtime->success_permille = 0;
    runtime->load_permille = 1000;
    runtime->capacity_permille = 0;
    runtime->last_error[0] = '\0';
    return runtime;
}

static void cheng_v3_relay_provider_copy_bridge_text(char* out,
                                                     size_t out_cap,
                                                     ChengStrBridge text) {
    const char* safe = "";
    size_t safe_len = 0u;
    if (out == NULL || out_cap == 0u) {
        return;
    }
    if (!cheng_str_bridge_view(text, &safe, &safe_len)) {
        safe = "";
        safe_len = 0u;
    }
    if (safe_len >= out_cap) {
        safe_len = out_cap - 1u;
    }
    if (safe_len > 0u) {
        memcpy(out, safe, safe_len);
    }
    out[safe_len] = '\0';
}

static void cheng_v3_relay_provider_set_error(ChengV3RelayProviderRuntime* runtime,
                                              const char* err) {
    const char* safe = err != NULL ? err : "";
    if (runtime == NULL) {
        return;
    }
    snprintf(runtime->last_error, sizeof(runtime->last_error), "%s", safe);
}

static int cheng_v3_relay_provider_text_contains(const char* text,
                                                 const char* token) {
    if (text == NULL || token == NULL || token[0] == '\0') {
        return 0;
    }
    return strstr(text, token) != NULL ? 1 : 0;
}

static void cheng_v3_relay_provider_refresh_health(ChengV3RelayProviderRuntime* runtime) {
    if (runtime == NULL) {
        return;
    }
    runtime->healthy = runtime->endpoint[0] != '\0' ? 1 : 0;
    runtime->success_permille = runtime->healthy ? 1000 : 0;
    if (strcmp(runtime->service_kind, "tsnet") == 0) {
        runtime->load_permille = 220;
        runtime->capacity_permille = 780;
    } else if (strcmp(runtime->service_kind, "turn") == 0) {
        runtime->load_permille = 140;
        runtime->capacity_permille = 860;
    } else {
        runtime->load_permille = 80;
        runtime->capacity_permille = 920;
    }
    if (cheng_v3_relay_provider_text_contains(runtime->endpoint, "busy")) {
        runtime->load_permille = 840;
        runtime->capacity_permille = 160;
    }
    if (cheng_v3_relay_provider_text_contains(runtime->endpoint, "unhealthy") ||
        cheng_v3_relay_provider_text_contains(runtime->endpoint, "down")) {
        runtime->healthy = 0;
        runtime->success_permille = 0;
        runtime->load_permille = 1000;
        runtime->capacity_permille = 0;
        cheng_v3_relay_provider_set_error(runtime, "relay provider: unhealthy");
        return;
    }
    cheng_v3_relay_provider_set_error(runtime, "");
}

WEAK uint64_t cheng_v3_relay_provider_open_bridge(ChengStrBridge service_kind_text,
                                                  ChengStrBridge endpoint_text,
                                                  ChengStrBridge region_text,
                                                  ChengStrBridge auth_scope_text,
                                                  int32_t requires_lease) {
    ChengV3RelayProviderRuntime* runtime = cheng_v3_relay_provider_runtime_new();
    if (runtime == NULL) return 0;
    cheng_v3_relay_provider_copy_bridge_text(runtime->service_kind,
                                             sizeof(runtime->service_kind),
                                             service_kind_text);
    cheng_v3_relay_provider_copy_bridge_text(runtime->endpoint,
                                             sizeof(runtime->endpoint),
                                             endpoint_text);
    cheng_v3_relay_provider_copy_bridge_text(runtime->region,
                                             sizeof(runtime->region),
                                             region_text);
    cheng_v3_relay_provider_copy_bridge_text(runtime->auth_scope,
                                             sizeof(runtime->auth_scope),
                                             auth_scope_text);
    runtime->requires_lease = requires_lease != 0 ? 1 : 0;
    cheng_v3_relay_provider_refresh_health(runtime);
    return cheng_ffi_handle_register_ptr(runtime);
}

WEAK ChengStrBridge cheng_v3_relay_provider_probe_bridge(uint64_t handle) {
    ChengV3RelayProviderRuntime* runtime = (ChengV3RelayProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    char payload[1024];
    if (runtime == NULL) {
        return cheng_v3_bridge_owned_str("ok=0\nhealthy=0\nsuccessPermille=0\nloadPermille=1000\ncapacityPermille=0\nsampledAtEpochSeconds=0\nerror=relay provider: missing handle\n");
    }
    cheng_v3_relay_provider_refresh_health(runtime);
    (void)snprintf(payload,
                   sizeof(payload),
                   "ok=1\nhealthy=%d\nsuccessPermille=%d\nloadPermille=%d\ncapacityPermille=%d\nsampledAtEpochSeconds=0\nerror=%s\n",
                   runtime->healthy != 0 ? 1 : 0,
                   runtime->success_permille,
                   runtime->load_permille,
                   runtime->capacity_permille,
                   runtime->last_error);
    return cheng_v3_bridge_owned_str(payload);
}

WEAK ChengStrBridge cheng_v3_relay_provider_issue_lease_bridge(uint64_t handle,
                                                               ChengStrBridge requester_peer_id_text,
                                                               ChengStrBridge requested_region_text,
                                                               ChengStrBridge nonce_hex_text,
                                                               int64_t issued_at_epoch_seconds,
                                                               int32_t ttl_seconds) {
    ChengV3RelayProviderRuntime* runtime = (ChengV3RelayProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    const char* requester = "";
    const char* requested_region = "";
    const char* nonce_hex = "";
    size_t requester_len = 0u;
    size_t requested_region_len = 0u;
    size_t nonce_hex_len = 0u;
    char username[256];
    char credential[320];
    char payload[1536];
    if (runtime == NULL) {
        return cheng_v3_bridge_owned_str("ok=0\nendpoint=\nregion=\nusername=\ncredential=\nexpiresAtEpochSeconds=0\nerror=relay provider: missing handle\n");
    }
    if (!cheng_str_bridge_view(requester_peer_id_text, &requester, &requester_len)) {
        requester = "";
        requester_len = 0u;
    }
    if (!cheng_str_bridge_view(requested_region_text, &requested_region, &requested_region_len)) {
        requested_region = "";
        requested_region_len = 0u;
    }
    if (!cheng_str_bridge_view(nonce_hex_text, &nonce_hex, &nonce_hex_len)) {
        nonce_hex = "";
        nonce_hex_len = 0u;
    }
    cheng_v3_relay_provider_refresh_health(runtime);
    if (requester_len == 0u) {
        return cheng_v3_bridge_owned_str("ok=0\nendpoint=\nregion=\nusername=\ncredential=\nexpiresAtEpochSeconds=0\nerror=relay provider: requester missing\n");
    }
    if (ttl_seconds <= 0) {
        return cheng_v3_bridge_owned_str("ok=0\nendpoint=\nregion=\nusername=\ncredential=\nexpiresAtEpochSeconds=0\nerror=relay provider: ttl invalid\n");
    }
    if (!runtime->healthy || runtime->endpoint[0] == '\0') {
        return cheng_v3_bridge_owned_str("ok=0\nendpoint=\nregion=\nusername=\ncredential=\nexpiresAtEpochSeconds=0\nerror=relay provider: unavailable\n");
    }
    if (cheng_v3_relay_provider_text_contains(runtime->endpoint, "faillease")) {
        return cheng_v3_bridge_owned_str("ok=0\nendpoint=\nregion=\nusername=\ncredential=\nexpiresAtEpochSeconds=0\nerror=relay provider: lease issuer failed\n");
    }
    username[0] = '\0';
    credential[0] = '\0';
    if (runtime->requires_lease || strcmp(runtime->service_kind, "turn") == 0 ||
        strcmp(runtime->service_kind, "tsnet") == 0) {
        (void)snprintf(username,
                       sizeof(username),
                       "%s:%s",
                       runtime->service_kind,
                       requester);
        (void)snprintf(credential,
                       sizeof(credential),
                       "%s|%lld|%d|%s",
                       requester,
                       (long long)issued_at_epoch_seconds,
                       ttl_seconds,
                       nonce_hex_len > 0u ? nonce_hex : "nonce");
    }
    (void)snprintf(payload,
                   sizeof(payload),
                   "ok=1\nendpoint=%s\nregion=%s\nusername=%s\ncredential=%s\nexpiresAtEpochSeconds=%lld\nerror=\n",
                   runtime->endpoint,
                   requested_region_len > 0u ? requested_region : runtime->region,
                   username,
                   credential,
                   (long long)(issued_at_epoch_seconds + (int64_t)ttl_seconds));
    return cheng_v3_bridge_owned_str(payload);
}

WEAK int32_t cheng_v3_relay_provider_close_bridge(uint64_t handle) {
    ChengV3RelayProviderRuntime* runtime = (ChengV3RelayProviderRuntime*)cheng_ffi_handle_resolve_ptr(handle);
    if (runtime == NULL) return -1;
    if (cheng_ffi_handle_invalidate(handle) != 0) {
        return -1;
    }
    free(runtime);
    return 0;
}

WEAK int32_t cheng_v3_webrtc_browser_request_response_bridge(ChengStrBridge protocol_text,
                                                             ChengStrBridge policy_text,
                                                             ChengStrBridge label_text,
                                                             ChengStrBridge request_text,
                                                             ChengStrBridge response_text,
                                                             ChengStrBridge* signal_response_text,
                                                             ChengStrBridge* client_response_text,
                                                             ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p webrtc browser: win32 bridge not implemented";
    if (signal_response_text != NULL) {
        *signal_response_text = driver_c_str_from_utf8_copy_bridge("", 0);
    }
    if (client_response_text != NULL) {
        *client_response_text = driver_c_str_from_utf8_copy_bridge("", 0);
    }
    if (err_text != NULL) {
        *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    }
    (void)protocol_text;
    (void)policy_text;
    (void)label_text;
    (void)request_text;
    (void)response_text;
    return 0;
#else
    const char* repo_root_missing = "v3 libp2p webrtc browser: repo root missing";
    const char* policy_missing = "v3 libp2p webrtc browser: policy payload missing";
    const char* tmpdir_failed = "v3 libp2p webrtc browser: mkdtemp failed";
    const char* file_write_failed = "v3 libp2p webrtc browser: temp file write failed";
    const char* signal_missing = "v3 libp2p webrtc browser: signal transcript missing";
    const char* response_missing = "v3 libp2p webrtc browser: client response missing";
    const char* child_failed = "v3 libp2p webrtc browser: node runner failed";
    const char* protocol = "";
    const char* policy = "";
    const char* label = "";
    const char* request = "";
    const char* response_payload = "";
    size_t protocol_len = 0u;
    size_t policy_len = 0u;
    size_t label_len = 0u;
    size_t request_len = 0u;
    size_t response_len = 0u;
    char repo_root[PATH_MAX];
    char script_path[PATH_MAX];
    char tmp_template[] = "/tmp/cheng_v3_browser_webrtc.XXXXXX";
    char protocol_path[PATH_MAX];
    char policy_path[PATH_MAX];
    char label_path[PATH_MAX];
    char request_path[PATH_MAX];
    char response_path[PATH_MAX];
    char signal_path[PATH_MAX];
    char client_response_path[PATH_MAX];
    char err_path[PATH_MAX];
    char* signal_buf = NULL;
    char* client_response_buf = NULL;
    char* child_err_buf = NULL;
    size_t signal_len = 0u;
    size_t client_response_len = 0u;
    size_t child_err_len = 0u;
    int64_t child_status = 0;
    ChengStrBridge err = cheng_str_bridge_empty();
    ChengStrBridge signal_out = cheng_str_bridge_empty();
    ChengStrBridge response_out = cheng_str_bridge_empty();
    if (signal_response_text != NULL) {
        *signal_response_text = signal_out;
    }
    if (client_response_text != NULL) {
        *client_response_text = response_out;
    }
    if (err_text != NULL) {
        *err_text = err;
    }
    if (!cheng_str_bridge_view(protocol_text, &protocol, &protocol_len)) {
        protocol = "";
        protocol_len = 0u;
    }
    if (!cheng_str_bridge_view(policy_text, &policy, &policy_len)) {
        policy = "";
        policy_len = 0u;
    }
    if (!cheng_str_bridge_view(label_text, &label, &label_len)) {
        label = "";
        label_len = 0u;
    }
    if (!cheng_str_bridge_view(request_text, &request, &request_len)) {
        request = "";
        request_len = 0u;
    }
    if (!cheng_str_bridge_view(response_text, &response_payload, &response_len)) {
        response_payload = "";
        response_len = 0u;
    }
    if (policy_len == 0u) {
        err = driver_c_str_from_utf8_copy_bridge(policy_missing, (int32_t)strlen(policy_missing));
        goto cleanup;
    }
    if (!cheng_v3_browser_bridge_resolve_repo_root(repo_root, sizeof(repo_root))) {
        err = driver_c_str_from_utf8_copy_bridge(repo_root_missing, (int32_t)strlen(repo_root_missing));
        goto cleanup;
    }
    snprintf(script_path, sizeof(script_path), "%s/v3/tooling/browser_webrtc/run_browser_webrtc_bridge.mjs", repo_root);
    if (mkdtemp(tmp_template) == NULL) {
        err = driver_c_str_from_utf8_copy_bridge(tmpdir_failed, (int32_t)strlen(tmpdir_failed));
        goto cleanup;
    }
    snprintf(protocol_path, sizeof(protocol_path), "%s/protocol.bin", tmp_template);
    snprintf(policy_path, sizeof(policy_path), "%s/policy.bin", tmp_template);
    snprintf(label_path, sizeof(label_path), "%s/label.txt", tmp_template);
    snprintf(request_path, sizeof(request_path), "%s/request.bin", tmp_template);
    snprintf(response_path, sizeof(response_path), "%s/response.bin", tmp_template);
    snprintf(signal_path, sizeof(signal_path), "%s/signal.bin", tmp_template);
    snprintf(client_response_path, sizeof(client_response_path), "%s/client_response.bin", tmp_template);
    snprintf(err_path, sizeof(err_path), "%s/err.txt", tmp_template);
    if (!cheng_v3_browser_bridge_write_file(protocol_path, protocol, protocol_len) ||
        !cheng_v3_browser_bridge_write_file(policy_path, policy, policy_len) ||
        !cheng_v3_browser_bridge_write_file(label_path, label, label_len) ||
        !cheng_v3_browser_bridge_write_file(request_path, request, request_len) ||
        !cheng_v3_browser_bridge_write_file(response_path, response_payload, response_len)) {
        err = driver_c_str_from_utf8_copy_bridge(file_write_failed, (int32_t)strlen(file_write_failed));
        goto cleanup;
    }
    child_status = cheng_v3_browser_bridge_spawn_node(script_path, tmp_template);
    (void)cheng_v3_browser_bridge_read_file(err_path, &child_err_buf, &child_err_len);
    if (child_status != 0) {
        if (child_err_buf != NULL && child_err_len > 0u) {
            err = driver_c_str_from_utf8_copy_bridge(child_err_buf, (int32_t)child_err_len);
        } else {
            char status_buf[96];
            snprintf(status_buf, sizeof(status_buf), "%s (%lld)", child_failed, (long long)child_status);
            err = driver_c_str_from_utf8_copy_bridge(status_buf, (int32_t)strlen(status_buf));
        }
        goto cleanup;
    }
    if (!cheng_v3_browser_bridge_read_file(signal_path, &signal_buf, &signal_len) || signal_len == 0u) {
        err = driver_c_str_from_utf8_copy_bridge(signal_missing, (int32_t)strlen(signal_missing));
        goto cleanup;
    }
    if (!cheng_v3_browser_bridge_read_file(client_response_path, &client_response_buf, &client_response_len)) {
        err = driver_c_str_from_utf8_copy_bridge(response_missing, (int32_t)strlen(response_missing));
        goto cleanup;
    }
    signal_out = driver_c_str_from_utf8_copy_bridge(signal_buf != NULL ? signal_buf : "", (int32_t)signal_len);
    response_out = driver_c_str_from_utf8_copy_bridge(client_response_buf != NULL ? client_response_buf : "", (int32_t)client_response_len);
cleanup:
    if (signal_response_text != NULL) {
        *signal_response_text = signal_out;
    } else {
        cheng_v3_bridge_release_owned(signal_out);
    }
    if (client_response_text != NULL) {
        *client_response_text = response_out;
    } else {
        cheng_v3_bridge_release_owned(response_out);
    }
    if (err_text != NULL) {
        *err_text = err;
    } else {
        cheng_v3_bridge_release_owned(err);
    }
    if (tmp_template[0] != '\0' && strchr(tmp_template, 'X') == NULL) {
        cheng_v3_browser_bridge_cleanup_tmpdir(tmp_template);
    }
    free(signal_buf);
    free(client_response_buf);
    free(child_err_buf);
    return err.ptr == NULL || err.len == 0 ? 1 : 0;
#endif
}
WEAK int32_t cheng_v3_tcp_serve_payload_once_impl_bridge(ChengStrBridge host_text,
                                                         int32_t port,
                                                         ChengStrBridge protocol_text,
                                                         ChengStrBridge payload_text,
                                                         ChengStrBridge ready_path_text,
                                                         ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p tcp: win32 serve payload bridge not implemented";
    if (err_text != NULL) {
        *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    }
    (void)host_text;
    (void)port;
    (void)protocol_text;
    (void)payload_text;
    (void)ready_path_text;
    return 0;
#else
    const char* alloc_failed = "v3 libp2p tcp: alloc failed";
    const char* request_mismatch = "v3 libp2p tcp: multistream request mismatch";
    const char* bad_host = "v3 libp2p tcp: invalid host";
    const char* bad_port = "v3 libp2p tcp: invalid port";
    const char* payload_too_large = "v3 libp2p tcp: payload too large";
    const char* protocol = "";
    const char* payload = "";
    size_t protocol_len = 0u;
    size_t payload_len = 0u;
    char* bind_host = NULL;
    char* ready_path = NULL;
    char* server_protocol = NULL;
    int listener_fd = -1;
    int server_fd = -1;
    struct sockaddr_in listener_addr;
    socklen_t listener_len = (socklen_t)sizeof(listener_addr);
    unsigned char frame_len_buf[4];
    ChengStrBridge err = cheng_str_bridge_empty();
    if (err_text != NULL) {
        *err_text = err;
    }
    bind_host = cheng_v3_bridge_copy_cstring(host_text);
    ready_path = cheng_v3_bridge_copy_cstring(ready_path_text);
    if (bind_host == NULL || ready_path == NULL) {
        err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
        goto cleanup;
    }
    if (port < 0 || port > 65535) {
        err = driver_c_str_from_utf8_copy_bridge(bad_port, (int32_t)strlen(bad_port));
        goto cleanup;
    }
    if (!cheng_str_bridge_view(protocol_text, &protocol, &protocol_len)) {
        protocol = "";
        protocol_len = 0u;
    }
    if (!cheng_str_bridge_view(payload_text, &payload, &payload_len)) {
        payload = "";
        payload_len = 0u;
    }
    if (payload_len > 0xffffffffu) {
        err = driver_c_str_from_utf8_copy_bridge(payload_too_large, (int32_t)strlen(payload_too_large));
        goto cleanup;
    }
    memset(&listener_addr, 0, sizeof(listener_addr));
    listener_addr.sin_family = AF_INET;
    listener_addr.sin_port = htons((uint16_t)port);
    if (!cheng_v3_tcp_parse_ipv4_host_raw(bind_host, &listener_addr.sin_addr.s_addr)) {
        err = driver_c_str_from_utf8_copy_bridge(bad_host, (int32_t)strlen(bad_host));
        goto cleanup;
    }
    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: socket");
        goto cleanup;
    }
    {
        int reuse = 1;
        (void)setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, (socklen_t)sizeof(reuse));
    }
    if (bind(listener_fd, (const struct sockaddr*)&listener_addr, (socklen_t)sizeof(listener_addr)) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: bind");
        goto cleanup;
    }
    if (listen(listener_fd, 8) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: listen");
        goto cleanup;
    }
    if (getsockname(listener_fd, (struct sockaddr*)&listener_addr, &listener_len) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: getsockname");
        goto cleanup;
    }
    if (!cheng_v3_tcp_write_ready_port_raw(ready_path,
                                           ntohs(listener_addr.sin_port),
                                           &err)) {
        goto cleanup;
    }
    fflush(stdout);
    server_fd = accept(listener_fd, NULL, NULL);
    if (server_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: accept");
        goto cleanup;
    }
    if (protocol_len > 0u) {
        server_protocol = (char*)malloc(protocol_len);
        if (server_protocol == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(server_fd, server_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv request");
            goto cleanup;
        }
        if (memcmp(server_protocol, protocol, protocol_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(request_mismatch, (int32_t)strlen(request_mismatch));
            goto cleanup;
        }
        if (!cheng_v3_tcp_send_all_raw(server_fd, protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send response");
            goto cleanup;
        }
    }
    cheng_v3_tcp_write_u32be(frame_len_buf, (uint32_t)payload_len);
    if (!cheng_v3_tcp_send_all_raw(server_fd, (const char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send frame len");
        goto cleanup;
    }
    if (payload_len > 0u && !cheng_v3_tcp_send_all_raw(server_fd, payload, payload_len)) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send payload");
        goto cleanup;
    }
cleanup:
    if (err_text != NULL) {
        *err_text = err;
    } else {
        cheng_v3_bridge_release_owned(err);
    }
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (listener_fd >= 0) {
        close(listener_fd);
    }
    free(bind_host);
    free(ready_path);
    free(server_protocol);
    return err.ptr == NULL || err.len == 0 ? 1 : 0;
#endif
}
WEAK int32_t cheng_v3_tcp_recv_payload_once_impl_bridge(ChengStrBridge host_text,
                                                        int32_t port,
                                                        ChengStrBridge protocol_text,
                                                        ChengStrBridge* response_text,
                                                        ChengStrBridge* err_text) {
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p tcp: win32 recv payload bridge not implemented";
    if (response_text != NULL) {
        *response_text = driver_c_str_from_utf8_copy_bridge("", 0);
    }
    if (err_text != NULL) {
        *err_text = driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
    }
    (void)host_text;
    (void)port;
    (void)protocol_text;
    return 0;
#else
    const char* alloc_failed = "v3 libp2p tcp: alloc failed";
    const char* response_mismatch = "v3 libp2p tcp: multistream response mismatch";
    const char* bad_host = "v3 libp2p tcp: invalid host";
    const char* bad_port = "v3 libp2p tcp: invalid port";
    const char* protocol = "";
    size_t protocol_len = 0u;
    char* connect_host = NULL;
    char* client_protocol = NULL;
    char* payload = NULL;
    int client_fd = -1;
    struct sockaddr_in client_addr;
    unsigned char frame_len_buf[4];
    uint32_t frame_len = 0u;
    ChengStrBridge err = cheng_str_bridge_empty();
    ChengStrBridge response = cheng_str_bridge_empty();
    if (response_text != NULL) {
        *response_text = response;
    }
    if (err_text != NULL) {
        *err_text = err;
    }
    connect_host = cheng_v3_bridge_copy_cstring(host_text);
    if (connect_host == NULL) {
        err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
        goto cleanup;
    }
    if (port < 0 || port > 65535) {
        err = driver_c_str_from_utf8_copy_bridge(bad_port, (int32_t)strlen(bad_port));
        goto cleanup;
    }
    if (!cheng_str_bridge_view(protocol_text, &protocol, &protocol_len)) {
        protocol = "";
        protocol_len = 0u;
    }
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons((uint16_t)port);
    if (!cheng_v3_tcp_parse_ipv4_host_raw(connect_host, &client_addr.sin_addr.s_addr)) {
        err = driver_c_str_from_utf8_copy_bridge(bad_host, (int32_t)strlen(bad_host));
        goto cleanup;
    }
    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: client socket");
        goto cleanup;
    }
    if (connect(client_fd, (const struct sockaddr*)&client_addr, (socklen_t)sizeof(client_addr)) != 0) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: connect");
        goto cleanup;
    }
    if (protocol_len > 0u) {
        client_protocol = (char*)malloc(protocol_len);
        if (client_protocol == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_send_all_raw(client_fd, protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: send request");
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(client_fd, client_protocol, protocol_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv response");
            goto cleanup;
        }
        if (memcmp(client_protocol, protocol, protocol_len) != 0) {
            err = driver_c_str_from_utf8_copy_bridge(response_mismatch, (int32_t)strlen(response_mismatch));
            goto cleanup;
        }
    }
    if (!cheng_v3_tcp_recv_exact_raw(client_fd, (char*)frame_len_buf, sizeof(frame_len_buf))) {
        err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv frame len");
        goto cleanup;
    }
    frame_len = cheng_v3_tcp_read_u32be(frame_len_buf);
    if (frame_len > 0u) {
        payload = (char*)malloc((size_t)frame_len);
        if (payload == NULL) {
            err = driver_c_str_from_utf8_copy_bridge(alloc_failed, (int32_t)strlen(alloc_failed));
            goto cleanup;
        }
        if (!cheng_v3_tcp_recv_exact_raw(client_fd, payload, (size_t)frame_len)) {
            err = cheng_v3_tcp_bridge_error_result("v3 libp2p tcp: recv payload");
            goto cleanup;
        }
    }
    response = driver_c_str_from_utf8_copy_bridge(payload != NULL ? payload : "", (int32_t)frame_len);
cleanup:
    if (response_text != NULL) {
        *response_text = response;
    } else {
        cheng_v3_bridge_release_owned(response);
    }
    if (err_text != NULL) {
        *err_text = err;
    } else {
        cheng_v3_bridge_release_owned(err);
    }
    if (client_fd >= 0) {
        close(client_fd);
    }
    free(connect_host);
    free(client_protocol);
    free(payload);
    return err.ptr == NULL || err.len == 0 ? 1 : 0;
#endif
}
WEAK ChengStrBridge cheng_v3_tcp_loopback_multistream_bridge(ChengStrBridge protocol_text) {
    ChengStrBridge ok = cheng_str_bridge_empty();
#if defined(_WIN32)
    const char* not_impl = "v3 libp2p tcp: win32 loopback bridge not implemented";
    (void)protocol_text;
    return driver_c_str_from_utf8_copy_bridge(not_impl, (int32_t)strlen(not_impl));
#else
    ChengStrBridge response = ok;
    ChengStrBridge err = ok;
    ChengStrBridge payload = ok;
    int32_t bridge_ok = cheng_v3_tcp_loopback_payload_impl_bridge(protocol_text, payload, &response, &err);
    cheng_v3_bridge_release_owned(response);
    if (bridge_ok) {
        return ok;
    }
    return err;
#endif
}
WEAK ChengStrBridge driver_c_get_current_dir_bridge(void) {
    const char* cwd = cheng_getcwd();
    if (cwd == NULL) {
        return driver_c_str_from_utf8_copy_bridge("", 0);
    }
    return driver_c_str_from_utf8_copy_bridge(cwd, (int32_t)strlen(cwd));
}
WEAK ChengStrBridge driver_c_join_path2_bridge(ChengStrBridge left, ChengStrBridge right) {
    return cheng_v3_os_join_path_bridge(left, right);
}
WEAK void driver_c_create_dir_all_bridge(ChengStrBridge path) {
    char* raw = cheng_v3_bridge_copy_cstring(path);
    if (raw == NULL) {
        fprintf(stderr, "[cheng] create_dir_all bridge alloc failed\n");
        abort();
    }
    cheng_v3_mkdir_all_raw(raw, 1);
    free(raw);
}
WEAK void driver_c_write_text_file_bridge(ChengStrBridge path, ChengStrBridge content) {
    char* raw = cheng_v3_bridge_copy_cstring(path);
    FILE* f = NULL;
    if (raw == NULL) {
        fprintf(stderr, "[cheng] write_text_file bridge alloc failed\n");
        abort();
    }
    if (raw[0] == '\0') {
        free(raw);
        return;
    }
    cheng_v3_mkdir_all_raw(raw, 0);
    f = fopen(raw, "wb");
    if (f == NULL) {
        fprintf(stderr, "[cheng] open failed: %s\n", raw);
        free(raw);
        abort();
    }
    if (content.ptr != NULL && content.len > 0) {
        if (fwrite(content.ptr, 1, (size_t)content.len, f) != (size_t)content.len) {
            fprintf(stderr, "[cheng] write failed: %s\n", raw);
            fclose(f);
            free(raw);
            abort();
        }
    }
    fclose(f);
    free(raw);
}
WEAK int32_t driver_c_str_get_at(const char* s, int32_t idx) {
    const char* safe = "";
    size_t n = 0;
    if (!cheng_safe_raw_cstr_view(s, &safe, &n)) {
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
    if (!cheng_safe_raw_cstr_view((const char*)s, NULL, &n)) {
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
    if (!cheng_safe_raw_cstr_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_raw_cstr_view(prefix, &sb, &lb)) {
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
    if (!cheng_safe_raw_cstr_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_raw_cstr_view(suffix, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    if (lb == 0) return 1;
    if (la < lb) return 0;
    return memcmp(sa + (la - lb), sb, lb) == 0 ? 1 : 0;
}
WEAK int32_t driver_c_str_has_prefix_bridge(ChengStrBridge s, ChengStrBridge prefix) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_str_bridge_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_str_bridge_view(prefix, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    if (lb == 0) return 1;
    if (la < lb) return 0;
    return memcmp(sa, sb, lb) == 0 ? 1 : 0;
}
WEAK int32_t driver_c_str_has_suffix_bridge(ChengStrBridge s, ChengStrBridge suffix) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_str_bridge_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_str_bridge_view(suffix, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    if (lb == 0) return 1;
    if (la < lb) return 0;
    return memcmp(sa + (la - lb), sb, lb) == 0 ? 1 : 0;
}
WEAK int32_t driver_c_str_contains_char(const char* s, int32_t value) {
    const char* safe = "";
    size_t n = 0;
    unsigned char target = (unsigned char)(value & 0xff);
    if (!cheng_safe_raw_cstr_view(s, &safe, &n)) {
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
    const char* safe = "";
    size_t n = 0;
    unsigned char target = (unsigned char)(value & 0xff);
    if (!cheng_str_bridge_view(s, &safe, &n)) {
        return 0;
    }
    for (size_t i = 0; i < n; ++i) {
        if ((unsigned char)safe[i] == target) return 1;
    }
    return 0;
}
WEAK int32_t driver_c_str_contains_str(const char* s, const char* sub) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_safe_raw_cstr_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_raw_cstr_view(sub, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    return cheng_v3_twoway_contains_bytes((const unsigned char*)sa,
                                          la,
                                          (const unsigned char*)sb,
                                          lb);
}
WEAK int32_t driver_c_str_contains_str_bridge(ChengStrBridge s, ChengStrBridge sub) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_str_bridge_view(s, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_str_bridge_view(sub, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    return cheng_v3_twoway_contains_bytes((const unsigned char*)sa,
                                          la,
                                          (const unsigned char*)sb,
                                          lb);
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

int32_t cheng_system_entropy_fill(void* dst, int32_t len) {
    if (dst == NULL || len <= 0) return 0;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(dst, (size_t)len);
    return 1;
#elif defined(__ANDROID__)
    uint8_t* cursor = (uint8_t*)dst;
    int32_t remaining = len;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return 0;
    }
    while (remaining > 0) {
        ssize_t got = read(fd, cursor, (size_t)remaining);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return 0;
        }
        if (got == 0) {
            close(fd);
            return 0;
        }
        cursor += (size_t)got;
        remaining -= (int32_t)got;
    }
    close(fd);
    return 1;
#elif defined(__linux__)
    uint8_t* cursor = (uint8_t*)dst;
    int32_t remaining = len;
    while (remaining > 0) {
        size_t chunk = remaining > 256 ? 256u : (size_t)remaining;
        if (getentropy(cursor, chunk) != 0) {
            return 0;
        }
        cursor += chunk;
        remaining -= (int32_t)chunk;
    }
    return 1;
#else
    return 0;
#endif
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

void* get_stdin(void) { return (void*)stdin; }
void* get_stdout(void) { return (void*)stdout; }
void* get_stderr(void) { return (void*)stderr; }

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

int64_t cheng_epoch_time_seconds(void) {
    return (int64_t)time(NULL);
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

typedef struct ChengV3HardwareAccelCache {
    int initialized;
    int32_t gpu_device_count;
    int32_t gpu_core_count;
    int32_t npu_device_count;
    int32_t npu_core_count;
} ChengV3HardwareAccelCache;

static ChengV3HardwareAccelCache cheng_v3_hardware_accel_cache = {0, 0, 0, 0, 0};

static int32_t cheng_v3_text_count_key(const char* text, const char* key) {
    int32_t count = 0;
    const char* cur = text;
    size_t key_len = (text && key) ? strlen(key) : 0u;
    if (text == NULL || key == NULL || key_len == 0u) return 0;
    while ((cur = strstr(cur, key)) != NULL) {
        count += 1;
        cur += key_len;
    }
    return count;
}

static int32_t cheng_v3_text_sum_json_int_string_field(const char* text, const char* key) {
    int32_t total = 0;
    const char* cur = text;
    size_t key_len = (text && key) ? strlen(key) : 0u;
    if (text == NULL || key == NULL || key_len == 0u) return 0;
    while ((cur = strstr(cur, key)) != NULL) {
        int32_t value = 0;
        int found_digit = 0;
        cur += key_len;
        while (*cur != '\0' && *cur != ':') cur++;
        if (*cur == ':') cur++;
        while (*cur != '\0' && *cur != '"' && !isdigit((unsigned char)*cur) && *cur != '-') cur++;
        if (*cur == '"') cur++;
        while (*cur != '\0' && isdigit((unsigned char)*cur)) {
            found_digit = 1;
            value = value * 10 + (int32_t)(*cur - '0');
            cur++;
        }
        if (found_digit) total += value;
    }
    return total;
}

static int32_t cheng_v3_system_profiler_gpu_device_count(void) {
    int64_t exit_code = -1;
    char* output = cheng_exec_cmd_ex("system_profiler SPDisplaysDataType -json -detailLevel mini",
                                     NULL,
                                     1,
                                     &exit_code);
    int32_t count = 0;
    if (output != NULL && exit_code == 0) {
        count = cheng_v3_text_count_key(output, "\"sppci_device_type\" : \"spdisplays_gpu\"");
        if (count <= 0) {
            count = cheng_v3_text_count_key(output, "\"sppci_cores\"");
        }
    }
    if (output != NULL) cheng_free(output);
    return count > 0 ? count : 0;
}

static int32_t cheng_v3_system_profiler_gpu_core_count(void) {
    int64_t exit_code = -1;
    char* output = cheng_exec_cmd_ex("system_profiler SPDisplaysDataType -json -detailLevel mini",
                                     NULL,
                                     1,
                                     &exit_code);
    int32_t count = 0;
    if (output != NULL && exit_code == 0) {
        count = cheng_v3_text_sum_json_int_string_field(output, "\"sppci_cores\"");
    }
    if (output != NULL) cheng_free(output);
    return count > 0 ? count : 0;
}

static void cheng_v3_probe_coreml_accelerators(ChengV3HardwareAccelCache* cache) {
#if defined(__APPLE__)
    void* coreml = dlopen("/System/Library/Frameworks/CoreML.framework/CoreML", RTLD_LAZY | RTLD_LOCAL);
    void* objc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_LAZY | RTLD_LOCAL);
    if (objc == NULL) objc = dlopen("/usr/lib/libobjc.dylib", RTLD_LAZY | RTLD_LOCAL);
    if (coreml == NULL || objc == NULL) {
        if (coreml != NULL) dlclose(coreml);
        if (objc != NULL) dlclose(objc);
        return;
    }
    typedef void* (*ChengMLAllComputeDevicesFn)(void);
    typedef void* (*ChengObjcMsgSendObjU64Fn)(void*, void*, uint64_t);
    typedef uint64_t (*ChengObjcMsgSendU64Fn)(void*, void*);
    typedef int64_t (*ChengObjcMsgSendI64Fn)(void*, void*);
    typedef const char* (*ChengObjectGetClassNameFn)(void*);
    typedef void* (*ChengAutoreleasePoolPushFn)(void);
    typedef void (*ChengAutoreleasePoolPopFn)(void*);
    typedef void* (*ChengSelRegisterNameFn)(const char*);

    ChengMLAllComputeDevicesFn all_devices =
        (ChengMLAllComputeDevicesFn)dlsym(coreml, "MLAllComputeDevices");
    ChengObjcMsgSendObjU64Fn msg_obj_u64 =
        (ChengObjcMsgSendObjU64Fn)dlsym(objc, "objc_msgSend");
    ChengObjcMsgSendU64Fn msg_u64 =
        (ChengObjcMsgSendU64Fn)dlsym(objc, "objc_msgSend");
    ChengObjcMsgSendI64Fn msg_i64 =
        (ChengObjcMsgSendI64Fn)dlsym(objc, "objc_msgSend");
    ChengObjectGetClassNameFn class_name =
        (ChengObjectGetClassNameFn)dlsym(objc, "object_getClassName");
    ChengAutoreleasePoolPushFn pool_push =
        (ChengAutoreleasePoolPushFn)dlsym(objc, "objc_autoreleasePoolPush");
    ChengAutoreleasePoolPopFn pool_pop =
        (ChengAutoreleasePoolPopFn)dlsym(objc, "objc_autoreleasePoolPop");
    ChengSelRegisterNameFn sel_register =
        (ChengSelRegisterNameFn)dlsym(objc, "sel_registerName");
    if (all_devices == NULL || msg_obj_u64 == NULL || msg_u64 == NULL ||
        msg_i64 == NULL || class_name == NULL || sel_register == NULL) {
        dlclose(coreml);
        dlclose(objc);
        return;
    }
    void* pool = pool_push ? pool_push() : NULL;
    void* sel_count = sel_register("count");
    void* sel_object_at_index = sel_register("objectAtIndex:");
    void* sel_total_core_count = sel_register("totalCoreCount");
    void* devices = all_devices();
    if (devices != NULL) {
        uint64_t device_count = msg_u64(devices, sel_count);
        uint64_t i = 0;
        for (i = 0; i < device_count; ++i) {
            void* device = msg_obj_u64(devices, sel_object_at_index, i);
            const char* name = device ? class_name(device) : NULL;
            if (name == NULL) continue;
            if (strcmp(name, "MLGPUComputeDevice") == 0) {
                cache->gpu_device_count += 1;
                continue;
            }
            if (strcmp(name, "MLNeuralEngineComputeDevice") == 0) {
                int64_t cores = msg_i64(device, sel_total_core_count);
                cache->npu_device_count += 1;
                if (cores > 0 && cores <= INT32_MAX) {
                    cache->npu_core_count += (int32_t)cores;
                }
            }
        }
    }
    if (pool_pop != NULL && pool != NULL) pool_pop(pool);
    dlclose(coreml);
    dlclose(objc);
#else
    (void)cache;
#endif
}

static void cheng_v3_probe_hardware_accel_once(void) {
    if (cheng_v3_hardware_accel_cache.initialized) return;
    cheng_v3_hardware_accel_cache.initialized = 1;
    cheng_v3_hardware_accel_cache.gpu_device_count = cheng_v3_system_profiler_gpu_device_count();
    cheng_v3_hardware_accel_cache.gpu_core_count = cheng_v3_system_profiler_gpu_core_count();
    cheng_v3_probe_coreml_accelerators(&cheng_v3_hardware_accel_cache);
}

int32_t cheng_v3_system_cpu_logical_cores_bridge(void) {
#if !defined(_WIN32) && !defined(__ANDROID__)
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 0 && ncpu <= INT32_MAX) return (int32_t)ncpu;
#endif
    return 0;
}

int64_t cheng_v3_system_memory_total_bytes_bridge(void) {
#if defined(__APPLE__)
    uint64_t mem = 0;
    size_t size = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &size, NULL, 0) == 0) {
        return (int64_t)mem;
    }
#endif
    return 0;
}

int64_t cheng_v3_system_memory_available_bytes_bridge(void) {
#if defined(__APPLE__)
    vm_statistics64_data_t vmstat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t page_size = 0;
    if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS) return 0;
    if (host_statistics64(mach_host_self(),
                          HOST_VM_INFO64,
                          (host_info64_t)&vmstat,
                          &count) != KERN_SUCCESS) {
        return 0;
    }
    uint64_t pages = (uint64_t)vmstat.free_count +
                     (uint64_t)vmstat.inactive_count +
                     (uint64_t)vmstat.speculative_count;
    return (int64_t)(pages * (uint64_t)page_size);
#else
    return 0;
#endif
}

int64_t cheng_v3_system_disk_total_bytes_bridge(void) {
#if !defined(_WIN32)
    struct statvfs fsinfo;
    if (statvfs("/", &fsinfo) == 0) {
        return (int64_t)((uint64_t)fsinfo.f_blocks * (uint64_t)fsinfo.f_frsize);
    }
#endif
    return 0;
}

int64_t cheng_v3_system_disk_available_bytes_bridge(void) {
#if !defined(_WIN32)
    struct statvfs fsinfo;
    if (statvfs("/", &fsinfo) == 0) {
        return (int64_t)((uint64_t)fsinfo.f_bavail * (uint64_t)fsinfo.f_frsize);
    }
#endif
    return 0;
}

int32_t cheng_v3_system_gpu_device_count_bridge(void) {
    cheng_v3_probe_hardware_accel_once();
    return cheng_v3_hardware_accel_cache.gpu_device_count;
}

int32_t cheng_v3_system_gpu_core_count_bridge(void) {
    cheng_v3_probe_hardware_accel_once();
    return cheng_v3_hardware_accel_cache.gpu_core_count;
}

int32_t cheng_v3_system_npu_device_count_bridge(void) {
    cheng_v3_probe_hardware_accel_once();
    return cheng_v3_hardware_accel_cache.npu_device_count;
}

int32_t cheng_v3_system_npu_core_count_bridge(void) {
    cheng_v3_probe_hardware_accel_once();
    return cheng_v3_hardware_accel_cache.npu_core_count;
}

#if !defined(_WIN32)
extern char** environ;
#endif

static const char* cheng_seq_string_item(ChengSeqHeader seq, int32_t idx) {
    if (idx < 0 || idx >= seq.len || seq.buffer == NULL) return "";
    {
        char** items = (char**)seq.buffer;
        const char* value = items[idx];
        return value ? value : "";
    }
}

static int cheng_env_key_eq(const char* entry, const char* key, size_t keyLen) {
    if (!entry || !key || keyLen == 0) return 0;
    if (strncmp(entry, key, keyLen) != 0) return 0;
    return entry[keyLen] == '=';
}

static char** cheng_exec_build_envp(ChengSeqHeader overrides) {
#if defined(_WIN32)
    (void)overrides;
    return NULL;
#else
    size_t baseCount = 0;
    size_t i = 0;
    size_t outCount = 0;
    char** out = NULL;
    if (environ) {
        while (environ[baseCount] != NULL) baseCount++;
    }
    out = (char**)malloc(sizeof(char*) * (baseCount + (size_t)(overrides.len > 0 ? overrides.len : 0) + 1u));
    if (!out) return NULL;
    for (i = 0; i < baseCount; ++i) out[i] = environ[i];
    outCount = baseCount;
    for (i = 0; i < (size_t)(overrides.len > 0 ? overrides.len : 0); ++i) {
        const char* raw = cheng_seq_string_item(overrides, (int32_t)i);
        const char* eq = raw ? strchr(raw, '=') : NULL;
        size_t keyLen = eq ? (size_t)(eq - raw) : 0u;
        size_t j = 0;
        int replaced = 0;
        if (!raw || !raw[0] || keyLen == 0u) continue;
        for (j = 0; j < outCount; ++j) {
            if (cheng_env_key_eq(out[j], raw, keyLen)) {
                out[j] = (char*)raw;
                replaced = 1;
                break;
            }
        }
        if (!replaced) {
            out[outCount++] = (char*)raw;
        }
    }
    out[outCount] = NULL;
    return out;
#endif
}

static char** cheng_exec_build_argv(const char* filePath, ChengSeqHeader argvSeq) {
    size_t extraCount = (size_t)(argvSeq.len > 0 ? argvSeq.len : 0);
    size_t i = 0;
    char** argv = (char**)malloc(sizeof(char*) * (extraCount + 2u));
    if (!argv) return NULL;
    argv[0] = (char*)(filePath ? filePath : "");
    for (i = 0; i < extraCount; ++i) {
        argv[i + 1u] = (char*)cheng_seq_string_item(argvSeq, (int32_t)i);
    }
    argv[extraCount + 1u] = NULL;
    return argv;
}

static void cheng_exec_free_argv_env(char** argv, char** envp) {
    if (argv) free(argv);
    if (envp) free(envp);
}

static int cheng_exec_load_argv_env(const char* filePath, void* argvSeqPtr, void* envOverridesSeqPtr,
                                    char*** argvOut, char*** envpOut) {
    ChengSeqHeader argvSeq;
    ChengSeqHeader envSeq;
    char** argv = NULL;
    char** envp = NULL;
    memset(&argvSeq, 0, sizeof(argvSeq));
    memset(&envSeq, 0, sizeof(envSeq));
    if (!filePath || !filePath[0] || !argvOut || !envpOut) return 0;
    if (argvSeqPtr) argvSeq = *(ChengSeqHeader*)argvSeqPtr;
    if (envOverridesSeqPtr) envSeq = *(ChengSeqHeader*)envOverridesSeqPtr;
    argv = cheng_exec_build_argv(filePath, argvSeq);
    if (!argv) return 0;
    envp = cheng_exec_build_envp(envSeq);
    *argvOut = argv;
    *envpOut = envp;
    return 1;
}

static int64_t cheng_exec_wait_status(pid_t pid, int32_t timeoutSec) {
#if defined(_WIN32)
    (void)pid;
    (void)timeoutSec;
    return -1;
#else
    int childStatus = 0;
    int timedOut = 0;
    int termSent = 0;
    int64_t termSentNs = 0;
    int64_t deadlineNs = 0;
    if (timeoutSec > 0) {
        deadlineNs = cheng_monotime_ns() + (int64_t)timeoutSec * 1000000000LL;
    }
    for (;;) {
#if defined(__APPLE__)
        int waitTimeoutMs = -1;
        int kq = -1;
        if (timeoutSec > 0 && deadlineNs > 0) {
            int64_t remainNs = deadlineNs - cheng_monotime_ns();
            if (remainNs <= 0) {
                waitTimeoutMs = 0;
            } else {
                waitTimeoutMs = (int)(remainNs / 1000000LL);
                if (waitTimeoutMs < 0) waitTimeoutMs = 0;
            }
        }
        kq = kqueue();
        if (kq >= 0) {
            struct kevent changeEv;
            struct kevent outEv;
            struct timespec waitTs;
            struct timespec* waitTsPtr = NULL;
            EV_SET(&changeEv, (uintptr_t)pid, EVFILT_PROC, EV_ADD | EV_ENABLE, NOTE_EXIT, 0, NULL);
            if (waitTimeoutMs >= 0) {
                waitTs.tv_sec = (time_t)(waitTimeoutMs / 1000);
                waitTs.tv_nsec = (long)((waitTimeoutMs % 1000) * 1000000L);
                waitTsPtr = &waitTs;
            }
            (void)kevent(kq, &changeEv, 1, &outEv, 1, waitTsPtr);
            close(kq);
        } else
#endif
        {
            if (timeoutSec > 0 && deadlineNs > 0) {
                int64_t remainNs = deadlineNs - cheng_monotime_ns();
                if (remainNs > 0) {
                    int waitUs = (int)(remainNs / 1000LL);
                    if (waitUs > 1000) waitUs = 1000;
                    if (waitUs > 0) usleep((useconds_t)waitUs);
                }
            }
        }
        if (timeoutSec > 0 && deadlineNs > 0) {
            int64_t nowNs = cheng_monotime_ns();
            if (nowNs >= deadlineNs) {
                if (!termSent) {
                    cheng_exec_kill_target(pid, SIGTERM);
                    termSent = 1;
                    termSentNs = nowNs;
                } else if (nowNs - termSentNs >= 50000000LL) {
                    cheng_exec_kill_target(pid, SIGKILL);
                }
                timedOut = 1;
            }
        }
        {
            pid_t waitRc = waitpid(pid, &childStatus, WNOHANG);
            if (waitRc == pid) {
                break;
            }
            if (waitRc < 0) {
                childStatus = 0;
                break;
            }
        }
    }
    if (timedOut) {
        return 124;
    }
    if (WIFEXITED(childStatus)) {
        return (int64_t)WEXITSTATUS(childStatus);
    }
    if (WIFSIGNALED(childStatus)) {
        return (int64_t)(128 + WTERMSIG(childStatus));
    }
    return (int64_t)childStatus;
#endif
}

static void cheng_exec_kill_target(pid_t pid, int sig) {
#if defined(_WIN32)
    (void)pid;
    (void)sig;
#else
    if (pid <= 0) return;
    if (kill(-pid, sig) != 0) {
        (void)kill(pid, sig);
    }
#endif
}

static pid_t cheng_exec_spawn_orphan_guard(pid_t targetPid) {
#if defined(_WIN32)
    (void)targetPid;
    return -1;
#else
    pid_t guardPid = -1;
    if (targetPid <= 0) return -1;
    guardPid = fork();
    if (guardPid != 0) {
        return guardPid;
    }
    for (;;) {
        if (getppid() == 1) {
            cheng_exec_kill_target(targetPid, SIGTERM);
            usleep(200000);
            cheng_exec_kill_target(targetPid, SIGKILL);
            _exit(0);
        }
        if (kill(targetPid, 0) != 0 && errno == ESRCH) {
            _exit(0);
        }
        usleep(200000);
    }
#endif
}

static void cheng_exec_stop_orphan_guard(pid_t guardPid) {
#if defined(_WIN32)
    (void)guardPid;
#else
    if (guardPid <= 0) return;
    (void)kill(guardPid, SIGTERM);
    (void)waitpid(guardPid, NULL, 0);
#endif
}

static void cheng_exec_spawn_detached_orphan_guard(pid_t wrapperPid, pid_t targetPid) {
#if defined(_WIN32)
    (void)wrapperPid;
    (void)targetPid;
#else
    pid_t guardPid = -1;
    if (wrapperPid <= 0 || targetPid <= 0) return;
    guardPid = fork();
    if (guardPid < 0) {
        return;
    }
    if (guardPid > 0) {
        (void)waitpid(guardPid, NULL, 0);
        return;
    }
    {
        pid_t workerPid = fork();
        if (workerPid < 0) {
            _exit(0);
        }
        if (workerPid > 0) {
            _exit(0);
        }
    }
    (void)setsid();
    for (;;) {
        if (kill(targetPid, 0) != 0 && errno == ESRCH) {
            _exit(0);
        }
        if (kill(wrapperPid, 0) != 0 && errno == ESRCH) {
            cheng_exec_kill_target(targetPid, SIGTERM);
            usleep(200000);
            cheng_exec_kill_target(targetPid, SIGKILL);
            _exit(0);
        }
        usleep(200000);
    }
#endif
}

static int cheng_exec_spawn_addchdir(posix_spawn_file_actions_t* actions, const char* workingDir) {
    if (!workingDir || !workingDir[0]) return 0;
#if defined(__APPLE__) && !CHENG_TARGET_APPLE_MOBILE
    return posix_spawn_file_actions_addchdir(actions, workingDir);
#else
    (void)actions;
    return -1;
#endif
}

static int cheng_exec_spawn_addchdir_supported(void) {
#if defined(__APPLE__) && !CHENG_TARGET_APPLE_MOBILE
    return 1;
#else
    return 0;
#endif
}

static int64_t cheng_exec_file_status_fork(const char* filePath, char** argv, char** envp,
                                           const char* workingDir, int32_t timeoutSec) {
    int devNullFd = -1;
    pid_t pid = -1;
    pid_t guardPid = -1;
    int64_t rc = -1;
    devNullFd = open("/dev/null", O_WRONLY);
    if (devNullFd < 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(devNullFd);
        return -1;
    }
    if (pid == 0) {
        (void)setpgid(0, 0);
        if (workingDir && workingDir[0]) chdir(workingDir);
        dup2(devNullFd, STDOUT_FILENO);
        dup2(devNullFd, STDERR_FILENO);
        close(devNullFd);
        execve(filePath, argv, envp ? envp : environ);
        _exit(127);
    }
    close(devNullFd);
    (void)setpgid(pid, pid);
    guardPid = cheng_exec_spawn_orphan_guard(pid);
    rc = cheng_exec_wait_status(pid, timeoutSec);
    cheng_exec_stop_orphan_guard(guardPid);
    return rc;
}

int64_t cheng_exec_file_status(const char* filePath, void* argvSeqPtr, void* envOverridesSeqPtr,
                               const char* workingDir, int32_t timeoutSec) {
#if defined(_WIN32)
    (void)filePath;
    (void)argvSeqPtr;
    (void)envOverridesSeqPtr;
    (void)workingDir;
    (void)timeoutSec;
    return -1;
#elif defined(__ANDROID__)
    char** argv = NULL;
    char** envp = NULL;
    int64_t rc = -1;
    if (!filePath || !filePath[0]) return -1;
    if (!cheng_exec_load_argv_env(filePath, argvSeqPtr, envOverridesSeqPtr, &argv, &envp)) {
        return -1;
    }
    rc = cheng_exec_file_status_fork(filePath, argv, envp, workingDir, timeoutSec);
    cheng_exec_free_argv_env(argv, envp);
    return rc;
#else
    char** argv = NULL;
    char** envp = NULL;
    pid_t pid = -1;
    int64_t rc = -1;
    posix_spawn_file_actions_t actions;
    int spawnRc = 0;
    if (!filePath || !filePath[0]) return -1;
    if (!cheng_exec_load_argv_env(filePath, argvSeqPtr, envOverridesSeqPtr, &argv, &envp)) {
        return -1;
    }
    if (workingDir && workingDir[0] && !cheng_exec_spawn_addchdir_supported()) {
        rc = cheng_exec_file_status_fork(filePath, argv, envp, workingDir, timeoutSec);
        cheng_exec_free_argv_env(argv, envp);
        return rc;
    }
    if (posix_spawn_file_actions_init(&actions) != 0) {
        cheng_exec_free_argv_env(argv, envp);
        return -1;
    }
    if (cheng_exec_spawn_addchdir(&actions, workingDir) != 0 ||
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) != 0 ||
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        cheng_exec_free_argv_env(argv, envp);
        return -1;
    }
    spawnRc = posix_spawn(&pid, filePath, &actions, NULL, argv, envp ? envp : environ);
    posix_spawn_file_actions_destroy(&actions);
    cheng_exec_free_argv_env(argv, envp);
    if (spawnRc != 0) {
        return 127;
    }
    (void)setpgid(pid, pid);
    {
        pid_t guardPid = cheng_exec_spawn_orphan_guard(pid);
        rc = cheng_exec_wait_status(pid, timeoutSec);
        cheng_exec_stop_orphan_guard(guardPid);
    }
    return rc;
#endif
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

int32_t cheng_exec_file_pipe_spawn(const char* filePath, void* argvSeqPtr, void* envOverridesSeqPtr,
                                   const char* workingDir, int32_t* outReadFd, int32_t* outWriteFd, int64_t* outPid) {
#if defined(_WIN32)
    (void)filePath;
    (void)argvSeqPtr;
    (void)envOverridesSeqPtr;
    (void)workingDir;
    if (outReadFd) *outReadFd = -1;
    if (outWriteFd) *outWriteFd = -1;
    if (outPid) *outPid = -1;
    return 0;
#else
    char** argv = NULL;
    char** envp = NULL;
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid = -1;
    if (outReadFd) *outReadFd = -1;
    if (outWriteFd) *outWriteFd = -1;
    if (outPid) *outPid = -1;
    if (!filePath || !filePath[0]) return 0;
    if (!cheng_exec_load_argv_env(filePath, argvSeqPtr, envOverridesSeqPtr, &argv, &envp)) {
        return 0;
    }
    if (pipe(in_pipe) != 0) {
        cheng_exec_free_argv_env(argv, envp);
        return 0;
    }
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        cheng_exec_free_argv_env(argv, envp);
        return 0;
    }
    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        cheng_exec_free_argv_env(argv, envp);
        return 0;
    }
    if (pid == 0) {
        (void)setpgid(0, 0);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (workingDir && workingDir[0]) chdir(workingDir);
        execve(filePath, argv, envp ? envp : environ);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    cheng_exec_free_argv_env(argv, envp);
    (void)setpgid(pid, pid);
    {
        int flags = fcntl(out_pipe[0], F_GETFL);
        if (flags != -1) {
            fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);
        }
        flags = fcntl(in_pipe[1], F_GETFL);
        if (flags != -1) {
            fcntl(in_pipe[1], F_SETFL, flags | O_NONBLOCK);
        }
    }
    cheng_exec_spawn_detached_orphan_guard(getpid(), pid);
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
    ssize_t n = write(fd, data, (size_t)len);
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

int32_t cheng_process_signal(int64_t pid, int32_t signalCode) {
#if defined(_WIN32)
    (void)pid;
    (void)signalCode;
    return -1;
#else
    if (pid <= 0 || signalCode <= 0) return -1;
    if (kill(-(pid_t)pid, signalCode) == 0) {
        return 0;
    }
    if (kill((pid_t)pid, signalCode) == 0) {
        return 0;
    }
    return -1;
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

static void cheng_ffi_handle_abort_invalid(const char* op, uint64_t handle, const char* detail) {
    const char* actual_op = (op != NULL && op[0] != '\0') ? op : "?";
    const char* actual_detail = (detail != NULL && detail[0] != '\0') ? detail : "invalid";
    fprintf(stderr,
            "[cheng] ffi handle invalid: op=%s detail=%s handle=%llu\n",
            actual_op,
            actual_detail,
            (unsigned long long)handle);
    cheng_dump_backtrace_now("ffi_handle");
    fflush(stderr);
    _Exit(86);
}

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
        cheng_ffi_handle_abort_invalid("resolve_ptr", handle, "decode");
    }
    ChengFfiHandleSlot* slot = &cheng_ffi_handle_slots[idx];
    if (slot->ptr == NULL) {
        cheng_ffi_handle_abort_invalid("resolve_ptr", handle, "released");
    }
    if (slot->generation != generation) {
        cheng_ffi_handle_abort_invalid("resolve_ptr", handle, "stale");
    }
    return slot->ptr;
}

int32_t cheng_ffi_handle_invalidate(uint64_t handle) {
    uint32_t idx = 0;
    uint32_t generation = 0;
    if (!cheng_ffi_handle_decode(handle, &idx, &generation)) {
        cheng_ffi_handle_abort_invalid("invalidate", handle, "decode");
    }
    ChengFfiHandleSlot* slot = &cheng_ffi_handle_slots[idx];
    if (slot->ptr == NULL) {
        cheng_ffi_handle_abort_invalid("invalidate", handle, "released");
    }
    if (slot->generation != generation) {
        cheng_ffi_handle_abort_invalid("invalidate", handle, "stale");
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
        cheng_ffi_handle_abort_invalid("get_i32", handle, "null-out");
    }
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    *out_value = *cell;
    return 0;
}

int32_t cheng_ffi_handle_add_i32(uint64_t handle, int32_t delta, int32_t* out_value) {
    if (out_value == NULL) {
        cheng_ffi_handle_abort_invalid("add_i32", handle, "null-out");
    }
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    *cell += delta;
    *out_value = *cell;
    return 0;
}

int32_t cheng_ffi_handle_release_i32(uint64_t handle) {
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    if (cheng_ffi_handle_invalidate(handle) != 0) {
        cheng_ffi_handle_abort_invalid("release_i32", handle, "invalidate");
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
