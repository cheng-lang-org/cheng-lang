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

#include "system_helpers_debug_state.inc"

/* Keep the host-only base in a separate include so the live bridge file stays declarative. */
#include "system_helpers_debug_host_base.inc"

/* Split out crash/source-map/backtrace helpers to keep the live bridge small. */
#include "system_helpers_debug_trace.inc"

/* Split out sampling profiler helpers to isolate the remaining host-only profiler surface. */
#include "system_helpers_debug_profile.inc"

#include "system_helpers_debug_entry.inc"

CHENG_V3_DEBUG_WEAK void cheng_v3_native_register_line_map_from_argv0(const char* argv0) {
    v3_debug_register_line_map_from_argv0_impl(argv0);
}

CHENG_V3_DEBUG_WEAK void cheng_v3_native_dump_backtrace_and_exit(const char* reason, int32_t code) {
    const char* safe = (reason != NULL && reason[0] != '\0') ? reason : "panic";
    fflush(stderr);
    v3_debug_dump_backtrace_now_impl(safe);
    exit(code != 0 ? code : 1);
}
