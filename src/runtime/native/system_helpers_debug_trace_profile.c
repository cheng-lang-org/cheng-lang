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
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <dlfcn.h>

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
#include <sys/ucontext.h>
#include <mach-o/dyld.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_V3_DEBUG_WEAK __attribute__((weak))
#else
#define CHENG_V3_DEBUG_WEAK
#endif

#if defined(__APPLE__) && !defined(RTLD_DEFAULT)
#define RTLD_DEFAULT ((void*)-2)
#endif

#define CHENG_CRASH_TRACE_RING 128
#define CHENG_V3_PROFILE_MAX_SAMPLES 65536
#define CHENG_V3_PROFILE_MAX_DEPTH 8

typedef struct ChengCrashTraceEntry {
    const char* tag;
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t d;
} ChengCrashTraceEntry;

typedef struct ChengV3EmbeddedLineMapEntry {
    const void* start_pc;
    const void* end_pc;
    const char* function_name;
    const char* source_path;
    int32_t signature_line;
    int32_t body_first_line;
    int32_t body_last_line;
    int32_t reserved;
} ChengV3EmbeddedLineMapEntry;

typedef const ChengV3EmbeddedLineMapEntry* (*ChengV3EmbeddedLineMapEntriesGetter)(void);
typedef uint64_t (*ChengV3EmbeddedLineMapCountGetter)(void);

typedef struct ChengV3LineMapEntry {
    char* symbol;
    char* function_name;
    char* source_path;
    int32_t signature_line;
    int32_t body_first_line;
    int32_t body_last_line;
} ChengV3LineMapEntry;

typedef struct ChengV3ProfileSample {
    uint8_t depth;
    uintptr_t pcs[CHENG_V3_PROFILE_MAX_DEPTH];
} ChengV3ProfileSample;

typedef struct ChengV3ProfileCount {
    char* key;
    uint32_t count;
} ChengV3ProfileCount;

static ChengCrashTraceEntry cheng_crash_trace_ring[CHENG_CRASH_TRACE_RING];
static volatile int32_t cheng_crash_trace_next = 0;
static volatile int32_t cheng_crash_trace_enabled_cache = -1;
static volatile int32_t cheng_crash_trace_installed = 0;
static volatile sig_atomic_t cheng_crash_trace_handling = 0;
static const char* cheng_crash_trace_phase = NULL;

static ChengV3EmbeddedLineMapEntriesGetter cheng_v3_embedded_line_map_entries_getter = NULL;
static ChengV3EmbeddedLineMapCountGetter cheng_v3_embedded_line_map_count_getter = NULL;
static volatile int32_t cheng_v3_embedded_line_map_getters_resolved = 0;

static ChengV3LineMapEntry* cheng_v3_line_map_entries = NULL;
static int32_t cheng_v3_line_map_len = 0;
static int32_t cheng_v3_line_map_cap = 0;
static int32_t cheng_v3_line_map_registered = 0;
static char cheng_v3_line_map_loaded_path[PATH_MAX];

static ChengV3ProfileSample* cheng_v3_profile_samples = NULL;
static int32_t cheng_v3_profile_capacity = 0;
static volatile sig_atomic_t cheng_v3_profile_sample_count = 0;
static volatile sig_atomic_t cheng_v3_profile_dropped_samples = 0;
static volatile sig_atomic_t cheng_v3_profile_initialized = 0;
static volatile sig_atomic_t cheng_v3_profile_enabled_cache = -1;
static volatile sig_atomic_t cheng_v3_profile_installed = 0;
static char cheng_v3_profile_out_path[PATH_MAX];

static int v3_debug_flag_enabled(const char* raw) {
    if (raw == NULL || raw[0] == '\0') return 0;
    if (raw[0] == '0' && raw[1] == '\0') return 0;
    return 1;
}

static bool v3_debug_probably_valid_ptr(const void* p) {
    uintptr_t raw = (uintptr_t)p;
    if (p == NULL) return false;
    if (raw < (uintptr_t)65536) return false;
    if ((raw & (uintptr_t)(sizeof(void*) - 1U)) != 0U) return false;
    return true;
}

static char* v3_debug_dup_bytes(const char* src, size_t len) {
    char* out = (char*)malloc(len + 1U);
    if (out == NULL) return NULL;
    if (src != NULL && len > 0U) {
        memcpy(out, src, len);
    }
    out[len] = '\0';
    return out;
}

static int v3_debug_dir_exists(const char* path) {
    struct stat st;
    if (path == NULL || path[0] == '\0') return 0;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int v3_debug_mkdir1(const char* path) {
    if (path == NULL || path[0] == '\0') return -1;
    return mkdir(path, 0755);
}

static int v3_debug_profile_env_enabled(void) {
    return v3_debug_flag_enabled(getenv("CHENG_V3_PROFILE_ENABLE"));
}

static int32_t v3_debug_profile_hz_value(void) {
    const char* raw = getenv("CHENG_V3_PROFILE_HZ");
    char* end = NULL;
    long value = 200;
    if (raw != NULL && raw[0] != '\0') {
        value = strtol(raw, &end, 10);
        if (end == raw) value = 200;
    }
    if (value < 10) value = 10;
    if (value > 2000) value = 2000;
    return (int32_t)value;
}

static int v3_debug_crash_trace_env_enabled(void) {
    const char* raw = getenv("CHENG_CRASH_TRACE");
    if (raw != NULL && raw[0] != '\0') return v3_debug_flag_enabled(raw);
    raw = getenv("BACKEND_CRASH_TRACE");
    if (raw != NULL && raw[0] != '\0') return v3_debug_flag_enabled(raw);
    raw = getenv("STAGE1_CRASH_TRACE");
    if (raw != NULL && raw[0] != '\0') return v3_debug_flag_enabled(raw);
    return 1;
}

static size_t v3_debug_cstrnlen(const char* s, size_t limit) {
    size_t n = 0U;
    if (s == NULL) return 0U;
    while (n < limit && s[n] != '\0') n += 1U;
    return n;
}

static void v3_debug_write_buf(const char* buf, size_t len) {
    size_t off = 0U;
    while (off < len) {
        ssize_t wrote = write(2, buf + off, len - off);
        if (wrote <= 0) return;
        off += (size_t)wrote;
    }
}

static void v3_debug_write_cstr(const char* s) {
    const char* text = s != NULL ? s : "?";
    v3_debug_write_buf(text, v3_debug_cstrnlen(text, 160U));
}

static void v3_debug_write_i32(int32_t value) {
    char buf[24];
    char* out = buf + sizeof(buf);
    uint32_t mag;
    *--out = '\0';
    if (value < 0) {
        mag = (uint32_t)(-(value + 1)) + 1U;
    } else {
        mag = (uint32_t)value;
    }
    do {
        *--out = (char)('0' + (mag % 10U));
        mag /= 10U;
    } while (mag != 0U);
    if (value < 0) {
        *--out = '-';
    }
    v3_debug_write_cstr(out);
}

static void v3_debug_write_hex_uintptr(uintptr_t value) {
    static const char hex[] = "0123456789abcdef";
    char buf[2 + sizeof(uintptr_t) * 2 + 1];
    size_t digits = sizeof(uintptr_t) * 2U;
    size_t i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0U; i < digits; ++i) {
        unsigned shift = (unsigned)((digits - 1U - i) * 4U);
        buf[2U + i] = hex[(value >> shift) & 0xfU];
    }
    buf[2U + digits] = '\0';
    v3_debug_write_buf(buf, 2U + digits);
}

static void cheng_v3_try_resolve_embedded_line_map_getters(void) {
    if (__sync_lock_test_and_set(&cheng_v3_embedded_line_map_getters_resolved, 1) != 0) {
        return;
    }
    dlerror();
    cheng_v3_embedded_line_map_entries_getter =
        (ChengV3EmbeddedLineMapEntriesGetter)dlsym(RTLD_DEFAULT, "cheng_v3_embedded_line_map_entries_get");
    dlerror();
    cheng_v3_embedded_line_map_count_getter =
        (ChengV3EmbeddedLineMapCountGetter)dlsym(RTLD_DEFAULT, "cheng_v3_embedded_line_map_count_get");
}

static const ChengV3EmbeddedLineMapEntry* cheng_v3_embedded_line_map_find_pc_in_entries(
    const ChengV3EmbeddedLineMapEntry* entries,
    uint64_t count,
    uintptr_t pc
) {
    uint64_t i;
    if (entries == NULL || count == 0U || pc == 0U) return NULL;
    for (i = 0U; i < count; ++i) {
        uintptr_t start = (uintptr_t)entries[i].start_pc;
        uintptr_t end = (uintptr_t)entries[i].end_pc;
        if (start == 0U) continue;
        if (pc < start) continue;
        if (end != 0U && pc >= end) continue;
        return &entries[i];
    }
    return NULL;
}

static void v3_debug_dump_async_crash_trace(const char* reason, int32_t sig) {
    int32_t end;
    int32_t start;
    int32_t i;
    v3_debug_write_cstr("[cheng-crash-trace] reason=");
    v3_debug_write_cstr(reason);
    if (sig != 0) {
        v3_debug_write_cstr(" sig=");
        v3_debug_write_i32(sig);
    }
    if (cheng_crash_trace_phase != NULL) {
        v3_debug_write_cstr(" phase=");
        v3_debug_write_cstr(cheng_crash_trace_phase);
    }
    v3_debug_write_cstr("\n");
    end = cheng_crash_trace_next;
    start = end > CHENG_CRASH_TRACE_RING ? end - CHENG_CRASH_TRACE_RING : 0;
    for (i = start; i < end; ++i) {
        const ChengCrashTraceEntry* entry =
            &cheng_crash_trace_ring[(uint32_t)i % CHENG_CRASH_TRACE_RING];
        if (entry->tag == NULL) continue;
        v3_debug_write_cstr("[cheng-crash-trace] #");
        v3_debug_write_i32(i);
        v3_debug_write_cstr(" ");
        v3_debug_write_cstr(entry->tag);
        v3_debug_write_cstr(" a=");
        v3_debug_write_i32(entry->a);
        v3_debug_write_cstr(" b=");
        v3_debug_write_i32(entry->b);
        v3_debug_write_cstr(" c=");
        v3_debug_write_i32(entry->c);
        v3_debug_write_cstr(" d=");
        v3_debug_write_i32(entry->d);
        v3_debug_write_cstr("\n");
    }
}

static int32_t cheng_v3_line_span_start(int32_t signature_line, int32_t body_first_line) {
    return body_first_line > 0 ? body_first_line : signature_line;
}

static int32_t cheng_v3_line_span_end(int32_t line_start, int32_t body_last_line) {
    return body_last_line > 0 ? body_last_line : line_start;
}

static void v3_debug_async_write_line_span(const ChengV3EmbeddedLineMapEntry* entry) {
    int32_t line_start;
    int32_t line_end;
    if (entry == NULL) return;
    line_start = cheng_v3_line_span_start(entry->signature_line, entry->body_first_line);
    line_end = cheng_v3_line_span_end(line_start, entry->body_last_line);
    v3_debug_write_i32(line_start);
    if (line_end > line_start) {
        v3_debug_write_cstr("-");
        v3_debug_write_i32(line_end);
    }
}

static void v3_debug_async_write_source_frame(int32_t frame_index,
                                              const ChengV3EmbeddedLineMapEntry* entry,
                                              uintptr_t pc) {
    if (entry == NULL) return;
    v3_debug_write_cstr("[cheng-v3] #");
    v3_debug_write_i32(frame_index);
    v3_debug_write_cstr(" ");
    v3_debug_write_cstr(
        entry->function_name != NULL && entry->function_name[0] != '\0' ? entry->function_name : "?"
    );
    v3_debug_write_cstr(" ");
    v3_debug_write_cstr(
        entry->source_path != NULL && entry->source_path[0] != '\0' ? entry->source_path : "?"
    );
    v3_debug_write_cstr(":");
    v3_debug_async_write_line_span(entry);
    v3_debug_write_cstr(" pc=");
    v3_debug_write_hex_uintptr(pc);
    v3_debug_write_cstr("\n");
}

static void v3_debug_async_write_machine_frame(int32_t frame_index,
                                               uintptr_t pc,
                                               uintptr_t fp,
                                               uintptr_t sp,
                                               uintptr_t lr,
                                               const ChengV3EmbeddedLineMapEntry* entry) {
    uintptr_t offset = 0U;
    if (entry != NULL && entry->start_pc != NULL && pc >= (uintptr_t)entry->start_pc) {
        offset = pc - (uintptr_t)entry->start_pc;
    }
    v3_debug_write_cstr("[cheng-v3] m#");
    v3_debug_write_i32(frame_index);
    v3_debug_write_cstr(" pc=");
    v3_debug_write_hex_uintptr(pc);
    v3_debug_write_cstr(" fp=");
    v3_debug_write_hex_uintptr(fp);
    v3_debug_write_cstr(" sp=");
    if (sp != 0U) {
        v3_debug_write_hex_uintptr(sp);
    } else {
        v3_debug_write_cstr("?");
    }
    v3_debug_write_cstr(" lr=");
    if (lr != 0U) {
        v3_debug_write_hex_uintptr(lr);
    } else {
        v3_debug_write_cstr("?");
    }
    v3_debug_write_cstr(" off=");
    v3_debug_write_hex_uintptr(offset);
    if (entry != NULL) {
        v3_debug_write_cstr(" fn=");
        v3_debug_write_cstr(
            entry->function_name != NULL && entry->function_name[0] != '\0' ? entry->function_name : "?"
        );
        v3_debug_write_cstr(" src=");
        v3_debug_write_cstr(
            entry->source_path != NULL && entry->source_path[0] != '\0' ? entry->source_path : "?"
        );
        v3_debug_write_cstr(":");
        v3_debug_async_write_line_span(entry);
    }
    v3_debug_write_cstr("\n");
}

static uintptr_t cheng_v3_signal_context_pc(void* signal_context) {
#if defined(__APPLE__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL || context->uc_mcontext == NULL) return 0U;
#if defined(__arm64__)
    return (uintptr_t)context->uc_mcontext->__ss.__pc;
#elif defined(__x86_64__)
    return (uintptr_t)context->uc_mcontext->__ss.__rip;
#elif defined(__i386__)
    return (uintptr_t)context->uc_mcontext->__ss.__eip;
#else
    return 0U;
#endif
#else
#if defined(__linux__) && defined(__aarch64__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL) return 0U;
    return (uintptr_t)context->uc_mcontext.pc;
#else
    (void)signal_context;
    return 0U;
#endif
#endif
}

static uintptr_t cheng_v3_signal_context_fp(void* signal_context) {
#if defined(__APPLE__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL || context->uc_mcontext == NULL) return 0U;
#if defined(__arm64__)
    return (uintptr_t)context->uc_mcontext->__ss.__fp;
#elif defined(__x86_64__)
    return (uintptr_t)context->uc_mcontext->__ss.__rbp;
#elif defined(__i386__)
    return (uintptr_t)context->uc_mcontext->__ss.__ebp;
#else
    return 0U;
#endif
#else
#if defined(__linux__) && defined(__aarch64__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL) return 0U;
    return (uintptr_t)context->uc_mcontext.regs[29];
#else
    (void)signal_context;
    return 0U;
#endif
#endif
}

static uintptr_t cheng_v3_signal_context_sp(void* signal_context) {
#if defined(__APPLE__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL || context->uc_mcontext == NULL) return 0U;
#if defined(__arm64__)
    return (uintptr_t)context->uc_mcontext->__ss.__sp;
#elif defined(__x86_64__)
    return (uintptr_t)context->uc_mcontext->__ss.__rsp;
#elif defined(__i386__)
    return (uintptr_t)context->uc_mcontext->__ss.__esp;
#else
    return 0U;
#endif
#else
#if defined(__linux__) && defined(__aarch64__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL) return 0U;
    return (uintptr_t)context->uc_mcontext.sp;
#else
    (void)signal_context;
    return 0U;
#endif
#endif
}

static uintptr_t cheng_v3_signal_context_lr(void* signal_context) {
#if defined(__APPLE__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL || context->uc_mcontext == NULL) return 0U;
#if defined(__arm64__)
    return (uintptr_t)context->uc_mcontext->__ss.__lr;
#else
    return 0U;
#endif
#else
#if defined(__linux__) && defined(__aarch64__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL) return 0U;
    return (uintptr_t)context->uc_mcontext.regs[30];
#else
    (void)signal_context;
    return 0U;
#endif
#endif
}

static void v3_debug_async_write_crash_header(const char* reason,
                                              const char* mode,
                                              int32_t sig,
                                              uintptr_t pc,
                                              uintptr_t fp,
                                              uintptr_t sp,
                                              uintptr_t lr,
                                              uintptr_t fault_addr) {
    v3_debug_write_cstr("[cheng-v3] v3_crash_report_v1\n");
    v3_debug_write_cstr("[cheng-v3] reason=");
    v3_debug_write_cstr(reason != NULL ? reason : "?");
    v3_debug_write_cstr(" mode=");
    v3_debug_write_cstr(mode != NULL ? mode : "?");
    v3_debug_write_cstr("\n");
    v3_debug_write_cstr("[cheng-v3] regs pc=");
    v3_debug_write_hex_uintptr(pc);
    v3_debug_write_cstr(" fp=");
    v3_debug_write_hex_uintptr(fp);
    v3_debug_write_cstr(" sp=");
    v3_debug_write_hex_uintptr(sp);
    v3_debug_write_cstr(" lr=");
    v3_debug_write_hex_uintptr(lr);
    if (sig != 0) {
        v3_debug_write_cstr(" sig=");
        v3_debug_write_i32(sig);
    }
    if (sig != 0 || fault_addr != 0U) {
        v3_debug_write_cstr(" fault=");
        v3_debug_write_hex_uintptr(fault_addr);
    }
    v3_debug_write_cstr("\n");
}

static int32_t v3_debug_dump_machine_signal_frames(const ChengV3EmbeddedLineMapEntry* entries,
                                                   uint64_t count,
                                                   uintptr_t pc,
                                                   uintptr_t fp,
                                                   uintptr_t sp,
                                                   uintptr_t lr) {
    int32_t depth = 0;
    int32_t emitted = 0;
    uintptr_t current_pc = pc;
    uintptr_t current_fp = fp;
    uintptr_t current_sp = sp;
    uintptr_t current_lr = lr;
    while (current_pc != 0U && depth < 48) {
        const ChengV3EmbeddedLineMapEntry* entry =
            cheng_v3_embedded_line_map_find_pc_in_entries(entries, count, current_pc);
#if (defined(__APPLE__) && (defined(__arm64__) || defined(__x86_64__) || defined(__i386__))) || \
    (defined(__linux__) && defined(__aarch64__))
        if (depth > 0 &&
            v3_debug_probably_valid_ptr((const void*)current_fp) &&
            (current_fp & (uintptr_t)(sizeof(uintptr_t) - 1U)) == 0U) {
            const uintptr_t* current_frame = (const uintptr_t*)current_fp;
            current_lr = current_frame[1];
        }
#endif
        v3_debug_async_write_machine_frame(depth,
                                           current_pc,
                                           current_fp,
                                           current_sp,
                                           current_lr,
                                           entry);
        emitted += 1;
#if (defined(__APPLE__) && (defined(__arm64__) || defined(__x86_64__) || defined(__i386__))) || \
    (defined(__linux__) && defined(__aarch64__))
        if (!v3_debug_probably_valid_ptr((const void*)current_fp) ||
            (current_fp & (uintptr_t)(sizeof(uintptr_t) - 1U)) != 0U) {
            break;
        }
        {
            const uintptr_t* frame = (const uintptr_t*)current_fp;
            uintptr_t next_fp = frame[0];
            uintptr_t return_pc = frame[1];
            if (next_fp <= current_fp || (next_fp - current_fp) > (uintptr_t)(8U * 1024U * 1024U)) {
                break;
            }
            if (return_pc == 0U) {
                break;
            }
#if defined(__arm64__) || defined(__aarch64__)
            current_pc = return_pc > 4U ? return_pc - 4U : return_pc;
#else
            current_pc = return_pc > 1U ? return_pc - 1U : return_pc;
#endif
            current_fp = next_fp;
            current_sp = 0U;
            current_lr = 0U;
        }
        depth += 1;
#else
        break;
#endif
    }
    return emitted;
}

static int32_t v3_debug_dump_source_signal_frames(const ChengV3EmbeddedLineMapEntry* entries,
                                                  uint64_t count,
                                                  uintptr_t pc,
                                                  uintptr_t fp) {
    int32_t depth = 0;
    int32_t mapped = 0;
    uintptr_t current_pc = pc;
    uintptr_t current_fp = fp;
    while (current_pc != 0U && depth < 48) {
        const ChengV3EmbeddedLineMapEntry* entry =
            cheng_v3_embedded_line_map_find_pc_in_entries(entries, count, current_pc);
        if (entry != NULL) {
            v3_debug_async_write_source_frame(depth, entry, current_pc);
            mapped += 1;
        }
#if (defined(__APPLE__) && (defined(__arm64__) || defined(__x86_64__) || defined(__i386__))) || \
    (defined(__linux__) && defined(__aarch64__))
        if (!v3_debug_probably_valid_ptr((const void*)current_fp) ||
            (current_fp & (uintptr_t)(sizeof(uintptr_t) - 1U)) != 0U) {
            break;
        }
        {
            const uintptr_t* frame = (const uintptr_t*)current_fp;
            uintptr_t next_fp = frame[0];
            uintptr_t return_pc = frame[1];
            if (next_fp <= current_fp || (next_fp - current_fp) > (uintptr_t)(8U * 1024U * 1024U)) {
                break;
            }
            if (return_pc == 0U) {
                break;
            }
#if defined(__arm64__) || defined(__aarch64__)
            current_pc = return_pc > 4U ? return_pc - 4U : return_pc;
#else
            current_pc = return_pc > 1U ? return_pc - 1U : return_pc;
#endif
            current_fp = next_fp;
        }
        depth += 1;
#else
        break;
#endif
    }
    return mapped;
}

static void v3_debug_dump_source_signal_trace_if_available(const char* reason,
                                                           int32_t sig,
                                                           const siginfo_t* info,
                                                           void* signal_context) {
    const ChengV3EmbeddedLineMapEntry* entries = NULL;
    uint64_t count = 0U;
    uintptr_t pc = 0U;
    uintptr_t fp = 0U;
    uintptr_t sp = 0U;
    uintptr_t lr = 0U;
    int32_t mapped = 0;
    if (cheng_v3_embedded_line_map_entries_getter == NULL ||
        cheng_v3_embedded_line_map_count_getter == NULL) {
        return;
    }
    entries = cheng_v3_embedded_line_map_entries_getter();
    count = cheng_v3_embedded_line_map_count_getter();
    if (entries == NULL || count == 0U) return;
    pc = cheng_v3_signal_context_pc(signal_context);
    if (pc == 0U) return;
    fp = cheng_v3_signal_context_fp(signal_context);
    sp = cheng_v3_signal_context_sp(signal_context);
    lr = cheng_v3_signal_context_lr(signal_context);
    v3_debug_async_write_crash_header(reason,
                                      "signal",
                                      sig,
                                      pc,
                                      fp,
                                      sp,
                                      lr,
                                      info != NULL && info->si_addr != NULL ? (uintptr_t)info->si_addr : 0U);
    v3_debug_dump_machine_signal_frames(entries, count, pc, fp, sp, lr);
    mapped = v3_debug_dump_source_signal_frames(entries, count, pc, fp);
    if (mapped <= 0) {
        v3_debug_write_cstr("[cheng-v3] source_frames=0\n");
    }
}

static void v3_debug_dump_native_backtrace_if_enabled(void);

static void v3_debug_crash_trace_signal_handler(int sig, siginfo_t* info, void* signal_context) {
    if (cheng_crash_trace_handling) _exit(128 + sig);
    cheng_crash_trace_handling = 1;
    v3_debug_dump_source_signal_trace_if_available("signal", sig, info, signal_context);
    v3_debug_dump_async_crash_trace("signal", sig);
    v3_debug_dump_native_backtrace_if_enabled();
    _exit(128 + sig);
}

static void v3_debug_crash_trace_install_handlers(void) {
    if (__sync_lock_test_and_set(&cheng_crash_trace_installed, 1) != 0) return;
    cheng_v3_try_resolve_embedded_line_map_getters();
#if !defined(_WIN32)
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = v3_debug_crash_trace_signal_handler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGTRAP, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
#if defined(SIGSYS)
        sigaction(SIGSYS, &sa, NULL);
#endif
    }
#endif
}

CHENG_V3_DEBUG_WEAK int32_t cheng_crash_trace_enabled(void) {
    int32_t cached = cheng_crash_trace_enabled_cache;
    if (cached >= 0) return cached;
    cached = v3_debug_crash_trace_env_enabled() ? 1 : 0;
    cheng_crash_trace_enabled_cache = cached;
    if (cached) v3_debug_crash_trace_install_handlers();
    return cached;
}

CHENG_V3_DEBUG_WEAK void cheng_crash_trace_set_phase(const char* phase) {
    if (!cheng_crash_trace_enabled()) return;
    cheng_crash_trace_phase = phase;
}

CHENG_V3_DEBUG_WEAK void cheng_crash_trace_mark(const char* tag,
                                                int32_t a,
                                                int32_t b,
                                                int32_t c,
                                                int32_t d) {
    int32_t serial;
    ChengCrashTraceEntry* entry;
    if (!cheng_crash_trace_enabled()) return;
    serial = __sync_fetch_and_add(&cheng_crash_trace_next, 1);
    entry = &cheng_crash_trace_ring[(uint32_t)serial % CHENG_CRASH_TRACE_RING];
    entry->a = a;
    entry->b = b;
    entry->c = c;
    entry->d = d;
    __sync_synchronize();
    entry->tag = tag;
}

static void v3_debug_crash_trace_dump_now(const char* reason) {
    if (!cheng_crash_trace_enabled()) return;
    v3_debug_dump_async_crash_trace(reason, 0);
}

static const char* cheng_v3_symbol_normalize(const char* text) {
    if (text == NULL) return "";
    while (*text == '_') text += 1;
    return text;
}

static int cheng_v3_line_map_symbol_eq(const char* lhs, const char* rhs) {
    if (lhs == NULL || rhs == NULL) return 0;
    if (strcmp(lhs, rhs) == 0) return 1;
    return strcmp(cheng_v3_symbol_normalize(lhs), cheng_v3_symbol_normalize(rhs)) == 0;
}

static int32_t cheng_v3_parse_i32(const char* text) {
    char* end = NULL;
    long value;
    if (text == NULL || text[0] == '\0') return 0;
    value = strtol(text, &end, 10);
    if (end == text) return 0;
    return (int32_t)value;
}

static int cheng_v3_line_map_add_entry(const char* symbol,
                                       const char* function_name,
                                       const char* source_path,
                                       int32_t signature_line,
                                       int32_t body_first_line,
                                       int32_t body_last_line) {
    ChengV3LineMapEntry* grown = NULL;
    if (symbol == NULL || symbol[0] == '\0' || source_path == NULL || source_path[0] == '\0') {
        return 0;
    }
    if (cheng_v3_line_map_len >= cheng_v3_line_map_cap) {
        int32_t next_cap = cheng_v3_line_map_cap > 0 ? cheng_v3_line_map_cap * 2 : 32;
        grown = (ChengV3LineMapEntry*)realloc(cheng_v3_line_map_entries,
                                              (size_t)next_cap * sizeof(ChengV3LineMapEntry));
        if (grown == NULL) return 0;
        cheng_v3_line_map_entries = grown;
        cheng_v3_line_map_cap = next_cap;
    }
    cheng_v3_line_map_entries[cheng_v3_line_map_len].symbol =
        v3_debug_dup_bytes(symbol, strlen(symbol));
    cheng_v3_line_map_entries[cheng_v3_line_map_len].function_name =
        v3_debug_dup_bytes(function_name != NULL ? function_name : "",
                           strlen(function_name != NULL ? function_name : ""));
    cheng_v3_line_map_entries[cheng_v3_line_map_len].source_path =
        v3_debug_dup_bytes(source_path, strlen(source_path));
    if (cheng_v3_line_map_entries[cheng_v3_line_map_len].symbol == NULL ||
        cheng_v3_line_map_entries[cheng_v3_line_map_len].function_name == NULL ||
        cheng_v3_line_map_entries[cheng_v3_line_map_len].source_path == NULL) {
        return 0;
    }
    cheng_v3_line_map_entries[cheng_v3_line_map_len].signature_line = signature_line;
    cheng_v3_line_map_entries[cheng_v3_line_map_len].body_first_line = body_first_line;
    cheng_v3_line_map_entries[cheng_v3_line_map_len].body_last_line = body_last_line;
    cheng_v3_line_map_len += 1;
    return 1;
}

static int cheng_v3_line_map_load_path(const char* path) {
    FILE* file = NULL;
    char* owned = NULL;
    char* cursor = NULL;
    char* line = NULL;
    long size = 0;
    int ok = 0;
    if (path == NULL || path[0] == '\0') return 0;
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    if (fseek(file, 0, SEEK_END) != 0) goto cleanup;
    size = ftell(file);
    if (size < 0) goto cleanup;
    if (fseek(file, 0, SEEK_SET) != 0) goto cleanup;
    owned = (char*)malloc((size_t)size + 1U);
    if (owned == NULL) goto cleanup;
    if (size > 0 && fread(owned, 1, (size_t)size, file) != (size_t)size) goto cleanup;
    owned[size] = '\0';
    cursor = owned;
    line = strsep(&cursor, "\n");
    if (line == NULL || strcmp(line, "v3_line_map_v1") != 0) goto cleanup;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        char* fields[7];
        size_t field_count = 0U;
        char* field_cursor = NULL;
        char* tab = NULL;
        if (strncmp(line, "entry\t", 6) != 0) continue;
        field_cursor = line + 6;
        fields[field_count++] = field_cursor;
        while (field_count < 7U && (tab = strchr(field_cursor, '\t')) != NULL) {
            *tab = '\0';
            field_cursor = tab + 1;
            fields[field_count++] = field_cursor;
        }
        if (field_count != 6U) goto cleanup;
        if (!cheng_v3_line_map_add_entry(fields[0],
                                         fields[1],
                                         fields[2],
                                         cheng_v3_parse_i32(fields[3]),
                                         cheng_v3_parse_i32(fields[4]),
                                         cheng_v3_parse_i32(fields[5]))) {
            goto cleanup;
        }
    }
    snprintf(cheng_v3_line_map_loaded_path, sizeof(cheng_v3_line_map_loaded_path), "%s", path);
    ok = cheng_v3_line_map_len > 0 ? 1 : 0;
cleanup:
    if (file != NULL) fclose(file);
    if (!ok) {
        cheng_v3_line_map_len = 0;
        cheng_v3_line_map_loaded_path[0] = '\0';
    }
    free(owned);
    return ok;
}

static const ChengV3LineMapEntry* cheng_v3_line_map_find(const char* symbol) {
    int32_t i;
    for (i = 0; i < cheng_v3_line_map_len; ++i) {
        if (cheng_v3_line_map_symbol_eq(cheng_v3_line_map_entries[i].symbol, symbol)) {
            return &cheng_v3_line_map_entries[i];
        }
    }
    return NULL;
}

static int v3_debug_runtime_resolve_self_path(char* out, size_t out_cap) {
    char raw[PATH_MAX];
    char* resolved = NULL;
    if (out == NULL || out_cap == 0U) return 0;
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
        ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1U);
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

static int v3_debug_backtrace_enabled(void) {
    const char* raw = getenv("CHENG_PANIC_BACKTRACE");
    if (raw == NULL || raw[0] == '\0') raw = getenv("BACKEND_DEBUG_BACKTRACE");
    if (raw == NULL || raw[0] == '\0') return 1;
    if (raw[0] == '0' && raw[1] == '\0') return 0;
    return 1;
}

static void v3_debug_dump_native_backtrace_if_enabled(void) {
    if (!v3_debug_backtrace_enabled()) return;
#if CHENG_HAS_EXECINFO
    {
        void* frames[48];
        int count = backtrace(frames, 48);
        if (count > 0) {
            backtrace_symbols_fd(frames, count, 2);
            return;
        }
    }
#endif
    v3_debug_write_cstr("[cheng] backtrace unavailable\n");
}

static void v3_debug_dump_machine_backtrace_if_available(const char* reason) {
#if CHENG_HAS_EXECINFO
    void* frames[48];
    int count = backtrace(frames, 48);
    int i;
    const ChengV3EmbeddedLineMapEntry* embedded_entries = NULL;
    uint64_t embedded_count = 0U;
    (void)reason;
    cheng_v3_try_resolve_embedded_line_map_getters();
    if (cheng_v3_embedded_line_map_entries_getter != NULL &&
        cheng_v3_embedded_line_map_count_getter != NULL) {
        embedded_entries = cheng_v3_embedded_line_map_entries_getter();
        embedded_count = cheng_v3_embedded_line_map_count_getter();
    }
    for (i = 0; i < count; ++i) {
        uintptr_t pc = (uintptr_t)frames[i];
        uintptr_t symbol_pc = 0U;
        uintptr_t offset = 0U;
        const char* symbol_name = "?";
        const char* source_path = NULL;
        const char* function_name = NULL;
        int32_t line_start = 0;
        int32_t line_end = 0;
        const ChengV3EmbeddedLineMapEntry* embedded_entry =
            cheng_v3_embedded_line_map_find_pc_in_entries(embedded_entries, embedded_count, pc);
        const ChengV3LineMapEntry* file_entry = NULL;
        Dl_info info;
        memset(&info, 0, sizeof(info));
        if (dladdr(frames[i], &info) != 0 && info.dli_sname != NULL) {
            symbol_name = info.dli_sname;
            symbol_pc = (uintptr_t)info.dli_saddr;
        }
        if (embedded_entry != NULL) {
            symbol_pc = (uintptr_t)embedded_entry->start_pc;
            function_name = embedded_entry->function_name;
            source_path = embedded_entry->source_path;
            line_start = cheng_v3_line_span_start(embedded_entry->signature_line,
                                                  embedded_entry->body_first_line);
            line_end = cheng_v3_line_span_end(line_start, embedded_entry->body_last_line);
        } else if (symbol_name != NULL && symbol_name[0] != '\0') {
            file_entry = cheng_v3_line_map_find(symbol_name);
            if (file_entry != NULL) {
                function_name = file_entry->function_name;
                source_path = file_entry->source_path;
                line_start = cheng_v3_line_span_start(file_entry->signature_line,
                                                      file_entry->body_first_line);
                line_end = cheng_v3_line_span_end(line_start, file_entry->body_last_line);
            }
        }
        if (symbol_pc != 0U && pc >= symbol_pc) {
            offset = pc - symbol_pc;
        }
        fprintf(stderr,
                "[cheng-v3] m#%d pc=0x%zx symbol=%s off=0x%zx",
                i,
                (size_t)pc,
                symbol_name != NULL && symbol_name[0] != '\0' ? symbol_name : "?",
                (size_t)offset);
        if (function_name != NULL && function_name[0] != '\0') {
            fprintf(stderr, " fn=%s", function_name);
        }
        if (source_path != NULL && source_path[0] != '\0') {
            fprintf(stderr, " src=%s:%d", source_path, line_start);
            if (line_end > line_start) {
                fprintf(stderr, "-%d", line_end);
            }
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
#else
    (void)reason;
#endif
}

static void v3_debug_dump_source_backtrace_if_available(const char* reason) {
    int mapped = 0;
    (void)reason;
    if (cheng_v3_line_map_len <= 0) return;
#if CHENG_HAS_EXECINFO
    {
        void* frames[48];
        int count = backtrace(frames, 48);
        int i;
        const ChengV3EmbeddedLineMapEntry* embedded_entries = NULL;
        uint64_t embedded_count = 0U;
        cheng_v3_try_resolve_embedded_line_map_getters();
        if (cheng_v3_embedded_line_map_entries_getter != NULL &&
            cheng_v3_embedded_line_map_count_getter != NULL) {
            embedded_entries = cheng_v3_embedded_line_map_entries_getter();
            embedded_count = cheng_v3_embedded_line_map_count_getter();
        }
        for (i = 0; i < count; ++i) {
            Dl_info info;
            const ChengV3EmbeddedLineMapEntry* embedded_entry;
            const ChengV3LineMapEntry* entry;
            const char* function_name = NULL;
            const char* source_path = NULL;
            uintptr_t pc;
            uintptr_t symbol_pc;
            uintptr_t offset;
            int32_t line_start;
            int32_t line_end;
            pc = (uintptr_t)frames[i];
            symbol_pc = 0U;
            offset = 0U;
            memset(&info, 0, sizeof(info));
            if (dladdr(frames[i], &info) != 0 && info.dli_sname != NULL) {
                symbol_pc = (uintptr_t)info.dli_saddr;
            }
            embedded_entry =
                cheng_v3_embedded_line_map_find_pc_in_entries(embedded_entries, embedded_count, pc);
            entry = NULL;
            if (embedded_entry != NULL) {
                symbol_pc = (uintptr_t)embedded_entry->start_pc;
                function_name = embedded_entry->function_name;
                source_path = embedded_entry->source_path;
                line_start = cheng_v3_line_span_start(embedded_entry->signature_line,
                                                      embedded_entry->body_first_line);
                line_end = cheng_v3_line_span_end(line_start, embedded_entry->body_last_line);
            } else {
                if (info.dli_sname == NULL) continue;
                entry = cheng_v3_line_map_find(info.dli_sname);
                if (entry == NULL) continue;
                function_name = entry->function_name;
                source_path = entry->source_path;
                line_start = cheng_v3_line_span_start(entry->signature_line,
                                                      entry->body_first_line);
                line_end = cheng_v3_line_span_end(line_start, entry->body_last_line);
            }
            offset = (symbol_pc != 0U && pc >= symbol_pc) ? (pc - symbol_pc) : 0U;
            fprintf(stderr,
                    "[cheng-v3] #%d %s %s:%d",
                    i,
                    function_name != NULL && function_name[0] != '\0' ?
                        function_name :
                        (info.dli_sname != NULL ? info.dli_sname : "?"),
                    source_path != NULL ? source_path : "?",
                    line_start);
            if (line_end > line_start) {
                fprintf(stderr, "-%d", line_end);
            }
            fprintf(stderr, " +0x%zx\n", (size_t)offset);
            mapped += 1;
        }
        if (mapped <= 0) {
            fprintf(stderr, "[cheng-v3] source_frames=0\n");
        }
        fflush(stderr);
    }
#else
    fprintf(stderr, "[cheng-v3] source_frames=unavailable\n");
    fflush(stderr);
#endif
}

static void v3_debug_dump_backtrace_now(const char* reason) {
    const char* actual_reason = reason != NULL && reason[0] != '\0' ? reason : "panic";
    uintptr_t pc = (uintptr_t)__builtin_return_address(0);
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    uintptr_t sp = (uintptr_t)&pc;
    uintptr_t lr = pc;
    v3_debug_crash_trace_dump_now(actual_reason);
    v3_debug_async_write_crash_header(actual_reason, "backtrace", 0, pc, fp, sp, lr, 0U);
    v3_debug_dump_machine_backtrace_if_available(actual_reason);
    v3_debug_dump_source_backtrace_if_available(actual_reason);
    v3_debug_dump_native_backtrace_if_enabled();
}

CHENG_V3_DEBUG_WEAK void cheng_dump_backtrace_if_enabled(void) {
    v3_debug_dump_backtrace_now("panic");
}

CHENG_V3_DEBUG_WEAK int32_t cheng_force_segv(void) {
#if defined(_WIN32)
    volatile int* p = (volatile int*)0;
    *p = 1;
#else
    raise(SIGSEGV);
#endif
    return 0;
}

static void v3_debug_path_mkdir_if_needed(const char* path) {
    if (path == NULL || path[0] == '\0' || v3_debug_dir_exists(path)) return;
    if (v3_debug_mkdir1(path) == 0) return;
    if (!v3_debug_dir_exists(path)) {
        fprintf(stderr, "[cheng] mkdir failed: %s\n", path);
        abort();
    }
}

static int v3_debug_path_is_sep(char ch) {
    return ch == '/' || ch == '\\';
}

static void v3_debug_mkdir_all_raw(const char* path, int include_leaf) {
    size_t len = 0U;
    char* scratch = NULL;
    if (path == NULL || path[0] == '\0') return;
    len = strlen(path);
    scratch = (char*)malloc(len + 1U);
    if (scratch == NULL) {
        fprintf(stderr, "[cheng] mkdir scratch alloc failed\n");
        abort();
    }
    memcpy(scratch, path, len + 1U);
    for (size_t i = 0U; i < len; ++i) {
        char ch = scratch[i];
        if (!v3_debug_path_is_sep(ch)) continue;
        if (i == 0U) continue;
        scratch[i] = '\0';
        v3_debug_path_mkdir_if_needed(scratch);
        scratch[i] = ch;
    }
    if (include_leaf) {
        v3_debug_path_mkdir_if_needed(scratch);
    }
    free(scratch);
}

static void v3_debug_profile_ensure_parent_dir(const char* path) {
    char parent[PATH_MAX];
    size_t len;
    const char* slash;
    if (path == NULL || path[0] == '\0') return;
    slash = strrchr(path, '/');
    if (slash == NULL) return;
    len = (size_t)(slash - path);
    if (len == 0U || len >= sizeof(parent)) return;
    memcpy(parent, path, len);
    parent[len] = '\0';
    v3_debug_mkdir_all_raw(parent, 1);
}

static uint8_t v3_debug_profile_capture_frames(void* signal_context,
                                               uintptr_t* pcs,
                                               uint8_t pcs_cap) {
    uintptr_t current_pc = cheng_v3_signal_context_pc(signal_context);
    uintptr_t current_fp = cheng_v3_signal_context_fp(signal_context);
    uint8_t depth = 0U;
    if (pcs == NULL || pcs_cap == 0U || current_pc == 0U) return 0U;
    while (current_pc != 0U && depth < pcs_cap) {
        pcs[depth++] = current_pc;
#if (defined(__APPLE__) && (defined(__arm64__) || defined(__x86_64__) || defined(__i386__))) || \
    (defined(__linux__) && defined(__aarch64__))
        if (!v3_debug_probably_valid_ptr((const void*)current_fp) ||
            (current_fp & (uintptr_t)(sizeof(uintptr_t) - 1U)) != 0U) {
            break;
        }
        {
            const uintptr_t* frame = (const uintptr_t*)current_fp;
            uintptr_t next_fp = frame[0];
            uintptr_t return_pc = frame[1];
            if (next_fp <= current_fp || (next_fp - current_fp) > (uintptr_t)(8U * 1024U * 1024U)) {
                break;
            }
            if (return_pc == 0U) {
                break;
            }
#if defined(__arm64__) || defined(__aarch64__)
            current_pc = return_pc > 4U ? return_pc - 4U : return_pc;
#else
            current_pc = return_pc > 1U ? return_pc - 1U : return_pc;
#endif
            current_fp = next_fp;
        }
#else
        break;
#endif
    }
    return depth;
}

static void v3_debug_profile_signal_handler(int sig, siginfo_t* info, void* signal_context) {
    int32_t index;
    (void)sig;
    (void)info;
    if (cheng_v3_profile_samples == NULL || cheng_v3_profile_capacity <= 0) return;
    index = __sync_fetch_and_add(&cheng_v3_profile_sample_count, 1);
    if (index < 0 || index >= cheng_v3_profile_capacity) {
        __sync_fetch_and_add(&cheng_v3_profile_dropped_samples, 1);
        return;
    }
    cheng_v3_profile_samples[index].depth =
        v3_debug_profile_capture_frames(signal_context,
                                        cheng_v3_profile_samples[index].pcs,
                                        CHENG_V3_PROFILE_MAX_DEPTH);
}

static int v3_debug_profile_count_cmp(const void* left, const void* right) {
    const ChengV3ProfileCount* a = (const ChengV3ProfileCount*)left;
    const ChengV3ProfileCount* b = (const ChengV3ProfileCount*)right;
    if (a->count > b->count) return -1;
    if (a->count < b->count) return 1;
    if (a->key == NULL && b->key == NULL) return 0;
    if (a->key == NULL) return 1;
    if (b->key == NULL) return -1;
    return strcmp(a->key, b->key);
}

static int v3_debug_profile_count_add(ChengV3ProfileCount** items,
                                      int32_t* len,
                                      int32_t* cap,
                                      const char* key) {
    ChengV3ProfileCount* grown = NULL;
    int32_t i;
    if (items == NULL || len == NULL || cap == NULL || key == NULL || key[0] == '\0') return 0;
    for (i = 0; i < *len; ++i) {
        if ((*items)[i].key != NULL && strcmp((*items)[i].key, key) == 0) {
            (*items)[i].count += 1U;
            return 1;
        }
    }
    if (*len >= *cap) {
        int32_t next_cap = *cap > 0 ? (*cap * 2) : 32;
        grown = (ChengV3ProfileCount*)realloc(*items, (size_t)next_cap * sizeof(ChengV3ProfileCount));
        if (grown == NULL) return 0;
        for (i = *cap; i < next_cap; i += 1) {
            grown[i].key = NULL;
            grown[i].count = 0U;
        }
        *items = grown;
        *cap = next_cap;
    }
    (*items)[*len].key = v3_debug_dup_bytes(key, strlen(key));
    if ((*items)[*len].key == NULL) return 0;
    (*items)[*len].count = 1U;
    *len += 1;
    return 1;
}

static void v3_debug_profile_counts_free(ChengV3ProfileCount* items, int32_t len) {
    int32_t i;
    if (items == NULL) return;
    for (i = 0; i < len; i += 1) {
        free(items[i].key);
    }
    free(items);
}

static void v3_debug_profile_labels_for_pc(uintptr_t pc,
                                           char* function_out,
                                           size_t function_cap,
                                           char* source_out,
                                           size_t source_cap) {
    const ChengV3EmbeddedLineMapEntry* embedded_entries = NULL;
    uint64_t embedded_count = 0U;
    const ChengV3EmbeddedLineMapEntry* embedded_entry = NULL;
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (function_cap > 0U) function_out[0] = '\0';
    if (source_cap > 0U) source_out[0] = '\0';
    cheng_v3_try_resolve_embedded_line_map_getters();
    if (cheng_v3_embedded_line_map_entries_getter != NULL &&
        cheng_v3_embedded_line_map_count_getter != NULL) {
        embedded_entries = cheng_v3_embedded_line_map_entries_getter();
        embedded_count = cheng_v3_embedded_line_map_count_getter();
        embedded_entry = cheng_v3_embedded_line_map_find_pc_in_entries(embedded_entries, embedded_count, pc);
    }
    if (embedded_entry != NULL) {
        snprintf(function_out,
                 function_cap,
                 "%s",
                 embedded_entry->function_name != NULL && embedded_entry->function_name[0] != '\0' ?
                     embedded_entry->function_name : "?");
        snprintf(source_out,
                 source_cap,
                 "%s:%d",
                 embedded_entry->source_path != NULL && embedded_entry->source_path[0] != '\0' ?
                     embedded_entry->source_path : "?",
                 cheng_v3_line_span_start(embedded_entry->signature_line,
                                          embedded_entry->body_first_line));
        return;
    }
    if (dladdr((void*)pc, &info) != 0 && info.dli_sname != NULL) {
        const ChengV3LineMapEntry* entry = cheng_v3_line_map_find(info.dli_sname);
        snprintf(function_out, function_cap, "%s", info.dli_sname);
        if (entry != NULL) {
            snprintf(source_out,
                     source_cap,
                     "%s:%d",
                     entry->source_path != NULL && entry->source_path[0] != '\0' ? entry->source_path : "?",
                     cheng_v3_line_span_start(entry->signature_line, entry->body_first_line));
        }
        return;
    }
    snprintf(function_out, function_cap, "pc_%zx", (size_t)pc);
    snprintf(source_out, source_cap, "pc_%zx", (size_t)pc);
}

static void v3_debug_profile_stop_timer(void) {
#if !defined(_WIN32)
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, NULL);
#endif
}

static void v3_debug_profile_write_report(void) {
    FILE* file = NULL;
    int32_t sample_count = cheng_v3_profile_sample_count;
    ChengV3ProfileCount* function_counts = NULL;
    ChengV3ProfileCount* source_counts = NULL;
    ChengV3ProfileCount* pc_counts = NULL;
    int32_t function_len = 0;
    int32_t source_len = 0;
    int32_t pc_len = 0;
    int32_t function_cap = 0;
    int32_t source_cap = 0;
    int32_t pc_cap = 0;
    int32_t i;
    if (!v3_debug_profile_env_enabled() || cheng_v3_profile_out_path[0] == '\0') return;
    v3_debug_profile_stop_timer();
    if (sample_count < 0) sample_count = 0;
    if (sample_count > cheng_v3_profile_capacity) sample_count = cheng_v3_profile_capacity;
    for (i = 0; i < sample_count; i += 1) {
        char function_name[PATH_MAX];
        char source_name[PATH_MAX];
        char pc_name[64];
        uintptr_t pc;
        if (cheng_v3_profile_samples == NULL || cheng_v3_profile_samples[i].depth == 0U) {
            continue;
        }
        pc = cheng_v3_profile_samples[i].pcs[0];
        v3_debug_profile_labels_for_pc(pc,
                                       function_name,
                                       sizeof(function_name),
                                       source_name,
                                       sizeof(source_name));
        snprintf(pc_name, sizeof(pc_name), "0x%zx", (size_t)pc);
        if (!v3_debug_profile_count_add(&function_counts, &function_len, &function_cap, function_name) ||
            !v3_debug_profile_count_add(&source_counts, &source_len, &source_cap, source_name) ||
            !v3_debug_profile_count_add(&pc_counts, &pc_len, &pc_cap, pc_name)) {
            v3_debug_profile_counts_free(function_counts, function_len);
            v3_debug_profile_counts_free(source_counts, source_len);
            v3_debug_profile_counts_free(pc_counts, pc_len);
            return;
        }
    }
    if (function_len > 1) {
        qsort(function_counts, (size_t)function_len, sizeof(ChengV3ProfileCount), v3_debug_profile_count_cmp);
    }
    if (source_len > 1) {
        qsort(source_counts, (size_t)source_len, sizeof(ChengV3ProfileCount), v3_debug_profile_count_cmp);
    }
    if (pc_len > 1) {
        qsort(pc_counts, (size_t)pc_len, sizeof(ChengV3ProfileCount), v3_debug_profile_count_cmp);
    }
    v3_debug_profile_ensure_parent_dir(cheng_v3_profile_out_path);
    file = fopen(cheng_v3_profile_out_path, "wb");
    if (file == NULL) {
        v3_debug_profile_counts_free(function_counts, function_len);
        v3_debug_profile_counts_free(source_counts, source_len);
        v3_debug_profile_counts_free(pc_counts, pc_len);
        return;
    }
    fprintf(file, "v3_profile_v1\n");
    fprintf(file, "total_samples=%d\n", sample_count);
    fprintf(file, "dropped_samples=%d\n", (int)cheng_v3_profile_dropped_samples);
    for (i = 0; i < function_len && i < 8; i += 1) {
        fprintf(file, "hot_function[%d]=%u|%s\n", i, function_counts[i].count,
                function_counts[i].key != NULL ? function_counts[i].key : "?");
    }
    for (i = 0; i < source_len && i < 8; i += 1) {
        fprintf(file, "hot_source[%d]=%u|%s\n", i, source_counts[i].count,
                source_counts[i].key != NULL ? source_counts[i].key : "?");
    }
    for (i = 0; i < pc_len && i < 8; i += 1) {
        fprintf(file, "hot_pc[%d]=%u|%s\n", i, pc_counts[i].count,
                pc_counts[i].key != NULL ? pc_counts[i].key : "?");
    }
    fclose(file);
    v3_debug_profile_counts_free(function_counts, function_len);
    v3_debug_profile_counts_free(source_counts, source_len);
    v3_debug_profile_counts_free(pc_counts, pc_len);
}

static void v3_debug_profile_maybe_init(const char* exe_path) {
#if defined(_WIN32)
    (void)exe_path;
    cheng_v3_profile_enabled_cache = 0;
    return;
#else
    struct sigaction sa;
    struct itimerval timer;
    int32_t hz;
    const char* out_env;
    if (cheng_v3_profile_initialized) return;
    if (!v3_debug_profile_env_enabled()) {
        cheng_v3_profile_enabled_cache = 0;
        return;
    }
#if !(defined(__APPLE__) && defined(__arm64__)) && !(defined(__linux__) && defined(__aarch64__))
    cheng_v3_profile_enabled_cache = 0;
    return;
#endif
    cheng_v3_profile_capacity = CHENG_V3_PROFILE_MAX_SAMPLES;
    cheng_v3_profile_samples = (ChengV3ProfileSample*)calloc((size_t)cheng_v3_profile_capacity,
                                                             sizeof(ChengV3ProfileSample));
    if (cheng_v3_profile_samples == NULL) {
        cheng_v3_profile_enabled_cache = 0;
        return;
    }
    out_env = getenv("CHENG_V3_PROFILE_OUT");
    if (out_env != NULL && out_env[0] != '\0') {
        snprintf(cheng_v3_profile_out_path, sizeof(cheng_v3_profile_out_path), "%s", out_env);
    } else if (exe_path != NULL && exe_path[0] != '\0') {
        snprintf(cheng_v3_profile_out_path, sizeof(cheng_v3_profile_out_path), "%s.v3.profile.txt", exe_path);
    } else {
        snprintf(cheng_v3_profile_out_path, sizeof(cheng_v3_profile_out_path), "cheng_v3.profile.txt");
    }
    if (__sync_lock_test_and_set(&cheng_v3_profile_installed, 1) == 0) {
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = v3_debug_profile_signal_handler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGALRM, &sa, NULL);
    }
    hz = v3_debug_profile_hz_value();
    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 1000000 / hz;
    if (timer.it_interval.tv_usec <= 0) {
        timer.it_interval.tv_usec = 1000;
    }
    timer.it_value = timer.it_interval;
    setitimer(ITIMER_REAL, &timer, NULL);
    atexit(v3_debug_profile_write_report);
    cheng_v3_profile_enabled_cache = 1;
    cheng_v3_profile_initialized = 1;
#endif
}

static void v3_debug_register_line_map_from_argv0_impl(const char* argv0) {
    char exe_path[PATH_MAX];
    char map_path[PATH_MAX];
    if (cheng_v3_line_map_registered) return;
    cheng_v3_line_map_registered = 1;
    if (!v3_debug_runtime_resolve_self_path(exe_path, sizeof(exe_path))) {
        if (argv0 == NULL || argv0[0] == '\0') {
            v3_debug_crash_trace_install_handlers();
            return;
        }
        if (realpath(argv0, exe_path) == NULL) {
            snprintf(exe_path, sizeof(exe_path), "%s", argv0);
        }
    }
    snprintf(map_path, sizeof(map_path), "%s.v3.map", exe_path);
    cheng_v3_line_map_load_path(map_path);
    v3_debug_crash_trace_install_handlers();
    v3_debug_profile_maybe_init(exe_path);
}

CHENG_V3_DEBUG_WEAK void cheng_v3_native_register_line_map_from_argv0(const char* argv0) {
    v3_debug_register_line_map_from_argv0_impl(argv0);
}

CHENG_V3_DEBUG_WEAK void cheng_v3_native_dump_backtrace_and_exit(const char* reason, int32_t code) {
    const char* safe = (reason != NULL && reason[0] != '\0') ? reason : "panic";
    fflush(stderr);
    v3_debug_dump_backtrace_now(safe);
    exit(code != 0 ? code : 1);
}
