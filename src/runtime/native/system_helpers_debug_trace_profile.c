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

typedef struct ChengV3ProfileSample {
    uint8_t depth;
    uintptr_t pcs[CHENG_V3_PROFILE_MAX_DEPTH];
} ChengV3ProfileSample;

static ChengCrashTraceEntry cheng_crash_trace_ring[CHENG_CRASH_TRACE_RING];
static volatile int32_t cheng_crash_trace_next = 0;
static volatile int32_t cheng_crash_trace_enabled_cache = -1;
static volatile int32_t cheng_crash_trace_installed = 0;
static volatile sig_atomic_t cheng_crash_trace_handling = 0;
static const char* cheng_crash_trace_phase = NULL;

static ChengV3EmbeddedLineMapEntriesGetter cheng_v3_embedded_line_map_entries_getter = NULL;
static ChengV3EmbeddedLineMapCountGetter cheng_v3_embedded_line_map_count_getter = NULL;
static volatile int32_t cheng_v3_embedded_line_map_getters_resolved = 0;

static int32_t cheng_v3_line_map_registered = 0;

static ChengV3ProfileSample* cheng_v3_profile_samples = NULL;
static int32_t cheng_v3_profile_capacity = 0;
static volatile sig_atomic_t cheng_v3_profile_sample_count = 0;
static volatile sig_atomic_t cheng_v3_profile_dropped_samples = 0;
static volatile sig_atomic_t cheng_v3_profile_initialized = 0;
static volatile sig_atomic_t cheng_v3_profile_enabled_cache = -1;
static volatile sig_atomic_t cheng_v3_profile_installed = 0;
static char cheng_v3_profile_raw_out_path[PATH_MAX];

/* Keep the host-only base in a separate include so the live bridge file stays declarative. */
#include "system_helpers_debug_host_base.inc"

/* Split out crash/source-map/backtrace helpers to keep the live bridge small. */
#include "system_helpers_debug_trace.inc"

/* Split out sampling profiler helpers to isolate the remaining host-only profiler surface. */
#include "system_helpers_debug_profile.inc"

static void v3_debug_register_line_map_from_argv0_impl(const char* argv0) {
    char exe_path[PATH_MAX];
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
