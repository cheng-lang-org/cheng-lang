#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <signal.h>
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
#include <malloc/malloc.h>
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif
#include "cheng_sidecar_loader.h"

#if defined(__APPLE__)
extern int *__error(void);
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_SHIM_WEAK __attribute__((weak))
#else
#define CHENG_SHIM_WEAK
#endif

typedef struct ChengSeqHeader {
  int32_t len;
  int32_t cap;
  void *buffer;
} ChengSeqHeader;

typedef struct ChengErrorInfoCompat {
  int32_t code;
  char *msg;
} ChengErrorInfoCompat;

void *cheng_malloc(int32_t size);
int32_t cheng_strlen(char *s);
void cheng_mem_retain(void *p);
void cheng_mem_release(void *p);
void *cheng_seq_set_grow(void *seq_ptr, int32_t idx, int32_t elem_size);
int32_t driver_c_str_has_prefix(const char *s, const char *prefix);
int32_t driver_c_str_has_suffix(const char *s, const char *suffix);
int32_t driver_c_str_contains_char(const char *s, int32_t value);
int32_t driver_c_str_contains_str(const char *s, const char *sub);
char *driver_c_char_to_str(int32_t value);

static bool cheng_probably_valid_ptr(const void *p) {
  uintptr_t raw = (uintptr_t)p;
  if (p == NULL) return false;
  if (raw < (uintptr_t)65536) return false;
  if ((raw & (uintptr_t)(sizeof(void *) - 1)) != 0) return false;
  return true;
}

int32_t astRefIsNil(void *v) {
  return v == NULL ? 1 : 0;
}

int32_t astRefNonNil(void *v) {
  return v != NULL ? 1 : 0;
}

int32_t rawmemIsNil(void *v) {
  return v == NULL ? 1 : 0;
}

char *cheng_rawmem_str_from_offset(char *s, int32_t off) {
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

void cheng_seq_delete_shift(void *buffer, int32_t at, int32_t len, int32_t elem_size) {
  if (buffer == NULL || elem_size <= 0 || at < 0 || at >= len) {
    return;
  }
  if (at + 1 >= len) {
    return;
  }
  char *base = (char *)buffer;
  size_t dst_off = (size_t)at * (size_t)elem_size;
  size_t src_off = (size_t)(at + 1) * (size_t)elem_size;
  size_t move_bytes = (size_t)(len - at - 1) * (size_t)elem_size;
  memmove(base + dst_off, base + src_off, move_bytes);
}

void cheng_seq_add_ptr_value(ChengSeqHeader *seq_hdr, void *val) {
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
    size_t old_bytes = seq_hdr->buffer != NULL ? (size_t)seq_hdr->cap * sizeof(void *) : 0;
    size_t new_bytes = (size_t)new_cap * sizeof(void *);
    void *new_buffer = realloc(seq_hdr->buffer, new_bytes);
    if (new_buffer == NULL) {
      abort();
    }
    seq_hdr->buffer = new_buffer;
    seq_hdr->cap = new_cap;
    if (new_bytes > old_bytes) {
      memset((char *)new_buffer + old_bytes, 0, new_bytes - old_bytes);
    }
  }
  ((void **)seq_hdr->buffer)[seq_hdr->len] = val;
  seq_hdr->len += 1;
}

int32_t cheng_seq_next_cap(int32_t cur_cap, int32_t need_len) {
  int32_t next_cap = cur_cap;
  if (next_cap < 4) {
    next_cap = 4;
  }
  while (next_cap < need_len) {
    int32_t doubled = next_cap * 2;
    if (doubled <= 0) {
      next_cap = need_len;
      break;
    }
    next_cap = doubled;
  }
  return next_cap;
}

void cheng_seq_zero_tail_raw(void *buffer, int32_t seq_cap, int32_t len, int32_t target, int32_t elem_size) {
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
  memset((uint8_t *)buffer + from_bytes, 0, (size_t)(to_bytes - from_bytes));
}

int32_t cheng_seq_header_len_get(void *seq_ptr) {
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  if (seq == NULL) return 0;
  return seq->len;
}

void cheng_seq_header_len_set(void *seq_ptr, int32_t value) {
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  if (seq == NULL) return;
  seq->len = value;
}

int32_t cheng_seq_header_cap_get(void *seq_ptr) {
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  if (seq == NULL) return 0;
  return seq->cap;
}

void cheng_seq_header_cap_set(void *seq_ptr, int32_t value) {
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  if (seq == NULL) return;
  seq->cap = value;
}

void *cheng_seq_header_buffer_get(void *seq_ptr) {
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  if (seq == NULL) return NULL;
  return seq->buffer;
}

void cheng_seq_header_buffer_set(void *seq_ptr, void *value) {
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  if (seq == NULL) return;
  seq->buffer = value;
}

void cheng_seq_grow_to_raw(void **p_buffer, int32_t *p_cap, int32_t min_cap, int32_t elem_size) {
  if (p_buffer == NULL || p_cap == NULL || min_cap <= 0 || elem_size <= 0) {
    return;
  }
  int32_t cur_cap = *p_cap;
  void *raw_buffer = *p_buffer;
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
  void *new_buffer = realloc(raw_buffer, new_bytes);
  if (new_buffer == NULL) {
    abort();
  }
  *p_buffer = new_buffer;
  *p_cap = new_cap;
  if (new_bytes > old_bytes) {
    memset((char *)new_buffer + old_bytes, 0, new_bytes - old_bytes);
  }
}

int32_t cheng_rawbytes_get_at(void *base, int32_t idx) {
  if (base == NULL || idx < 0) {
    return 0;
  }
  return (int32_t)((uint8_t *)base)[idx];
}

void cheng_rawbytes_set_at(void *base, int32_t idx, int32_t value) {
  if (base == NULL || idx < 0) {
    return;
  }
  ((uint8_t *)base)[idx] = (uint8_t)value;
}

void cheng_rawmem_write_i8(void *dst, int32_t idx, int8_t value) {
  if (dst == NULL || idx < 0) {
    return;
  }
  ((uint8_t *)dst)[idx] = (uint8_t)value;
}

void cheng_rawmem_write_char(void *dst, int32_t idx, int32_t value) {
  if (dst == NULL || idx < 0) {
    return;
  }
  ((uint8_t *)dst)[idx] = (uint8_t)value;
}

void cheng_seq_grow_inst(void *seq_ptr, int32_t min_cap, int32_t elem_size) {
  if (seq_ptr == NULL || min_cap <= 0 || elem_size <= 0) {
    return;
  }
  ChengSeqHeader *seq_hdr = (ChengSeqHeader *)seq_ptr;
  int32_t buf_cap = seq_hdr->cap;
  void *raw_buffer = seq_hdr->buffer;
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

void *cheng_seq_string_alloc_compat(int32_t buf_cap) {
  if (buf_cap <= 0) {
    return NULL;
  }
  int32_t total_bytes = buf_cap * (int32_t)sizeof(void *);
  void *raw_buffer = realloc(NULL, (size_t)total_bytes);
  if (raw_buffer == NULL) {
    abort();
  }
  memset(raw_buffer, 0, (size_t)total_bytes);
  return raw_buffer;
}

void cheng_seq_string_init_compat(void *seq_ptr, int32_t seq_len, int32_t seq_cap) {
  if (seq_ptr == NULL) {
    return;
  }
  ChengSeqHeader *seq_hdr = (ChengSeqHeader *)seq_ptr;
  int32_t buf_cap = cheng_seq_string_init_cap(seq_len, seq_cap);
  void *raw_buffer = NULL;
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

void cheng_seq_free(void *seq_ptr) {
  if (seq_ptr == NULL) {
    return;
  }
  ChengSeqHeader *seq_hdr = (ChengSeqHeader *)seq_ptr;
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

char *cheng_seq_string_get_compat(ChengSeqHeader seq, int32_t at) {
  if (!cheng_probably_valid_ptr(seq.buffer) || at < 0 || seq.len < 0 || seq.cap < 0 || seq.len > seq.cap || at >= seq.len || at >= seq.cap) {
    return "";
  }
  char *value = ((char **)seq.buffer)[at];
  if (!cheng_probably_valid_ptr(value)) {
    return "";
  }
  return value;
}

void cheng_seq_string_add_compat(void *seq_ptr, const char *value) {
  if (seq_ptr == NULL) {
    return;
  }
  ChengSeqHeader *seq_hdr = (ChengSeqHeader *)seq_ptr;
  int32_t write_at = seq_hdr->len;
  char **write_ptr = (char **)cheng_seq_set_grow(seq_ptr, write_at, (int32_t)sizeof(void *));
  if (write_ptr != NULL) {
    *write_ptr = (char *)value;
  }
}

void *cheng_seq_slice_alloc(void *buffer, int32_t len, int32_t start_pos, int32_t stop_pos,
                            int32_t exclusive, int32_t elem_size,
                            int32_t *out_len, int32_t *out_cap) {
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
  void *out = cheng_malloc((int32_t)total);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, (char *)buffer + ((size_t)start * (size_t)elem_size), total);
  if (out_len != NULL) {
    *out_len = slice_len;
  }
  if (out_cap != NULL) {
    *out_cap = slice_len;
  }
  return out;
}

typedef struct ChengStrBridge {
  const char *ptr;
  int32_t len;
  int32_t store_id;
  int32_t flags;
} ChengStrBridge;

enum {
  CHENG_STR_BRIDGE_FLAG_NONE = 0,
  CHENG_STR_BRIDGE_FLAG_OWNED = 1,
};

extern void uirEmitObjFromModuleOrPanic(ChengSeqHeader *out, void *module, int32_t optLevel,
                                        const char *target, const char *objWriter,
                                        int32_t validateModule, int32_t uirSimdEnabled,
                                        int32_t uirSimdMaxWidth,
                                        const char *uirSimdPolicy) __attribute__((weak_import));

typedef void (*cheng_emit_obj_from_module_fn)(ChengSeqHeader *out, void *module, int32_t optLevel,
                                              const char *target, const char *objWriter,
                                              int32_t validateModule, int32_t uirSimdEnabled,
                                              int32_t uirSimdMaxWidth, const char *uirSimdPolicy);
typedef int32_t (*cheng_driver_emit_obj_default_fn)(void *module, const char *target,
                                                    const char *out_path);
typedef void *(*cheng_build_active_module_ptrs_fn)(void *inputRaw, void *targetRaw);
typedef void *(*cheng_build_module_stage1_fn)(const char *path, const char *target);

CHENG_SHIM_WEAK int32_t driver_emit_obj_from_module_default_impl(void *module, const char *target,
                                                                 const char *path);
CHENG_SHIM_WEAK void *driver_buildActiveModulePtrs(void *inputRaw, void *targetRaw);
CHENG_SHIM_WEAK void *uirCoreBuildModuleFromFileStage1OrPanic(const char *path,
                                                              const char *target);

typedef struct ChengStrMetaEntry {
  const char *ptr;
  int32_t len;
} ChengStrMetaEntry;

static int32_t cheng_saved_argc = 0;
static const char **cheng_saved_argv = NULL;
static ChengStrMetaEntry *cheng_strmeta_entries = NULL;
static int32_t cheng_strmeta_len = 0;
static int32_t cheng_strmeta_cap = 0;
static volatile int cheng_strmeta_lock = 0;

static int driver_c_diag_enabled(void);
static void driver_c_diagf(const char *fmt, ...);
typedef void (*cheng_global_init_fn)(void);
static int32_t driver_c_self_global_init_state = 0;

#if defined(__APPLE__)
static void *driver_c_lookup_symbol_macho_self(const char *symbol) {
  char symbol_buf[256];
  NSSymbol ns_symbol = NULL;
  const struct mach_header *header = _dyld_get_image_header(0);
  if (symbol == NULL || symbol[0] == '\0') return NULL;
  if (symbol[0] == '_') {
    snprintf(symbol_buf, sizeof(symbol_buf), "%s", symbol);
  } else {
    snprintf(symbol_buf, sizeof(symbol_buf), "_%s", symbol);
  }
  if (header != NULL) {
    ns_symbol = NSLookupSymbolInImage(
        header, symbol_buf,
        NSLOOKUPSYMBOLINIMAGE_OPTION_BIND |
            NSLOOKUPSYMBOLINIMAGE_OPTION_RETURN_ON_ERROR);
    if (ns_symbol != NULL) return NSAddressOfSymbol(ns_symbol);
  }
  if (NSIsSymbolNameDefined(symbol_buf)) {
    ns_symbol = NSLookupAndBindSymbol(symbol_buf);
    if (ns_symbol != NULL) return NSAddressOfSymbol(ns_symbol);
  }
  return NULL;
}
#endif

static void *driver_c_lookup_runtime_symbol(const char *symbol, const char *diag_tag) {
  void *fn = NULL;
  const char *err = NULL;
  if (symbol == NULL || symbol[0] == '\0') return NULL;
  dlerror();
  fn = dlsym(RTLD_DEFAULT, symbol);
  err = dlerror();
  if (fn == NULL) {
#if defined(__APPLE__)
    fn = driver_c_lookup_symbol_macho_self(symbol);
#endif
  }
  if (diag_tag != NULL) {
    driver_c_diagf("[%s] symbol='%s' fn=%p err='%s'\n",
                   diag_tag, symbol, fn, err != NULL ? err : "");
  }
  return fn;
}

static void driver_c_maybe_run_self_global_init(void) {
  if (__sync_bool_compare_and_swap(&driver_c_self_global_init_state, 2, 2)) return;
  if (!__sync_bool_compare_and_swap(&driver_c_self_global_init_state, 0, 1)) {
    while (__sync_bool_compare_and_swap(&driver_c_self_global_init_state, 2, 2) == 0) {
    }
    return;
  }
  cheng_global_init_fn global_init =
      (cheng_global_init_fn)driver_c_lookup_runtime_symbol("__cheng_global_init",
                                                           "driver_c_self_global_init");
  if (global_init != NULL) global_init();
  __sync_synchronize();
  driver_c_self_global_init_state = 2;
}

static int32_t driver_c_env_i32(const char *name, int32_t fallback) {
  char *end = NULL;
  long value;
  const char *raw = getenv(name);
  if (raw == NULL || raw[0] == '\0') return fallback;
  value = strtol(raw, &end, 10);
  if (end == raw) return fallback;
  return (int32_t)value;
}

#define CHENG_CRASH_TRACE_RING 128

typedef struct ChengCrashTraceEntry {
  const char *tag;
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
static const char *cheng_crash_trace_phase = NULL;

static int cheng_flag_enabled(const char *raw) {
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

static size_t cheng_crash_trace_cstrnlen(const char *s, size_t limit) {
  size_t n = 0;
  if (s == NULL) return 0;
  while (n < limit && s[n] != '\0') n++;
  return n;
}

static void cheng_crash_trace_write_buf(const char *buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t wrote = write(2, buf + off, len - off);
    if (wrote <= 0) return;
    off += (size_t)wrote;
  }
}

static void cheng_crash_trace_write_cstr(const char *s) {
  const char *text = (s != NULL) ? s : "?";
  cheng_crash_trace_write_buf(text, cheng_crash_trace_cstrnlen(text, 160));
}

static void cheng_crash_trace_write_i32(int32_t value) {
  char buf[24];
  char *out = buf + sizeof(buf);
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

static void cheng_crash_trace_dump_async(const char *reason, int32_t sig) {
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
    const ChengCrashTraceEntry *entry = &cheng_crash_trace_ring[(uint32_t)i % CHENG_CRASH_TRACE_RING];
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

CHENG_SHIM_WEAK int32_t cheng_crash_trace_enabled(void) {
  int32_t cached = cheng_crash_trace_enabled_cache;
  if (cached >= 0) return cached;
  cached = cheng_crash_trace_env_enabled() ? 1 : 0;
  cheng_crash_trace_enabled_cache = cached;
  if (cached) cheng_crash_trace_install_handlers();
  return cached;
}

CHENG_SHIM_WEAK void cheng_crash_trace_set_phase(const char *phase) {
  if (!cheng_crash_trace_enabled()) return;
  cheng_crash_trace_phase = phase;
}

CHENG_SHIM_WEAK void cheng_crash_trace_mark(const char *tag, int32_t a, int32_t b, int32_t c, int32_t d) {
  if (!cheng_crash_trace_enabled()) return;
  int32_t serial = __sync_fetch_and_add(&cheng_crash_trace_next, 1);
  ChengCrashTraceEntry *entry = &cheng_crash_trace_ring[(uint32_t)serial % CHENG_CRASH_TRACE_RING];
  entry->a = a;
  entry->b = b;
  entry->c = c;
  entry->d = d;
  __sync_synchronize();
  entry->tag = tag;
}

static void cheng_crash_trace_dump_now(const char *reason) {
  if (!cheng_crash_trace_enabled()) return;
  cheng_crash_trace_dump_async(reason, 0);
}

static ChengSidecarBundleCache cheng_sidecar_cache = {0};

static void driver_c_sidecar_after_open(void *handle, ChengSidecarDiagFn diag) {
  dlerror();
  cheng_global_init_fn global_init =
      (cheng_global_init_fn)dlsym(handle, "__cheng_global_init");
  const char *init_err = dlerror();
  if (diag != NULL) {
    diag("[driver_c_sidecar_bundle_handle] global_init=%p err='%s'\n",
         (void *)global_init, init_err != NULL ? init_err : "");
  }
  if (global_init != NULL) {
    global_init();
  }
}

static int driver_c_diag_enabled(void) {
  const char *raw = getenv("BACKEND_DEBUG_BOOT");
  if (raw == NULL || raw[0] == '\0') return 0;
  if (raw[0] == '0' && raw[1] == '\0') return 0;
  return 1;
}

static void driver_c_diagf(const char *fmt, ...) {
  if (!driver_c_diag_enabled()) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fflush(stderr);
}

static int cheng_backtrace_enabled(void) {
  const char *raw = getenv("CHENG_PANIC_BACKTRACE");
  if (raw == NULL || raw[0] == '\0') raw = getenv("BACKEND_DEBUG_BACKTRACE");
  if (raw == NULL || raw[0] == '\0') return 0;
  if (raw[0] == '0' && raw[1] == '\0') return 0;
  return 1;
}

void cheng_dump_backtrace_if_enabled(void) {
  cheng_crash_trace_dump_now("panic");
  if (!cheng_backtrace_enabled()) return;
#if CHENG_HAS_EXECINFO
  void *frames[48];
  int count = backtrace(frames, 48);
  if (count > 0) {
    backtrace_symbols_fd(frames, count, 2);
    return;
  }
#endif
  fprintf(stderr, "[cheng] backtrace unavailable\n");
  fflush(stderr);
}
int32_t cheng_strlen(char *s);
int32_t cheng_file_exists(const char *path);
int64_t cheng_file_size(const char *path);
int32_t cheng_open_w_trunc(const char *path);
int32_t cheng_fd_write(int32_t fd, const char *data, int32_t len);
int32_t libc_close(int32_t fd);
int32_t driver_c_arg_count(void);
char *driver_c_arg_copy(int32_t i);
int32_t __cheng_rt_paramCount(void);
const char * __cheng_rt_paramStr(int32_t i);
char *__cheng_rt_paramStrCopy(int32_t i);
CHENG_SHIM_WEAK int32_t driver_c_finish_emit_obj(const char *path);
CHENG_SHIM_WEAK int32_t driver_c_finish_single_link(const char *path, const char *obj_path);
CHENG_SHIM_WEAK void *driver_c_build_module_stage1(const char *input_path, const char *target);
CHENG_SHIM_WEAK void *driver_c_build_module_stage1_direct(const char *input_path, const char *target);
static const char *cheng_safe_cstr(const char *s);

static void cheng_strmeta_acquire(void) {
  while (__sync_lock_test_and_set(&cheng_strmeta_lock, 1) != 0) {
  }
}

static void cheng_strmeta_release(void) {
  __sync_lock_release(&cheng_strmeta_lock);
}

static void cheng_strmeta_put(const char *ptr, int32_t len) {
  if (ptr == NULL || len < 0) return;
  cheng_strmeta_acquire();
  if (cheng_strmeta_len >= cheng_strmeta_cap) {
    int32_t next_cap = cheng_strmeta_cap < 256 ? 256 : (cheng_strmeta_cap * 2);
    ChengStrMetaEntry *next_entries =
        (ChengStrMetaEntry *)realloc(cheng_strmeta_entries, (size_t)next_cap * sizeof(ChengStrMetaEntry));
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

CHENG_SHIM_WEAK void cheng_register_string_meta(const char *ptr, int32_t len) {
  cheng_strmeta_put(ptr, len);
}

static int cheng_strmeta_get(const char *ptr, size_t *out_len) {
  if (ptr == NULL) return 0;
  int found = 0;
  cheng_strmeta_acquire();
  for (int32_t i = cheng_strmeta_len - 1; i >= 0; --i) {
    if (cheng_strmeta_entries[i].ptr == ptr) {
      if (out_len != NULL) *out_len = (size_t)cheng_strmeta_entries[i].len;
      found = 1;
      break;
    }
  }
  cheng_strmeta_release();
  return found;
}

static ChengStrBridge cheng_str_bridge_empty(void) {
  ChengStrBridge out;
  out.ptr = "";
  out.len = 0;
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_NONE;
  return out;
}

static ChengStrBridge cheng_str_bridge_from_ptr_flags(const char *ptr, int32_t flags) {
  ChengStrBridge out = cheng_str_bridge_empty();
  const char *safe = cheng_safe_cstr(ptr);
  size_t n = strlen(safe);
  out.ptr = safe;
  out.len = n > (size_t)INT32_MAX ? INT32_MAX : (int32_t)n;
  out.flags = flags;
  return out;
}

static ChengStrBridge cheng_str_bridge_from_owned(char *ptr) {
  return cheng_str_bridge_from_ptr_flags((const char *)ptr, CHENG_STR_BRIDGE_FLAG_OWNED);
}

static int driver_c_arg_is_help(const char *arg) {
  if (arg == NULL) return 0;
  return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

CHENG_SHIM_WEAK int32_t driver_c_backend_usage(void) {
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

CHENG_SHIM_WEAK int32_t driver_c_write_stderr(const char *text) {
  if (text == NULL) return 0;
  size_t n = strlen(text);
  if (n == 0) return 0;
  size_t wrote = fwrite(text, 1, n, stderr);
  fflush(stderr);
  return wrote == n ? 0 : -1;
}

CHENG_SHIM_WEAK int32_t driver_c_write_stderr_line(const char *text) {
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

void driver_c_boot_marker(int32_t code);

#if defined(CHENG_BACKEND_DRIVER_ENTRY_SHIM)
extern int32_t driver_c_run_default(void);
void __cheng_setCmdLine(int32_t argc, const char **argv) CHENG_SHIM_WEAK;
CHENG_SHIM_WEAK int32_t backendMain(void) {
  return driver_c_run_default();
}
CHENG_SHIM_WEAK int main(int argc, char **argv) {
  __cheng_setCmdLine((int32_t)argc, (const char **)argv);
  for (int i = 1; i < argc; ++i) {
    if (driver_c_arg_is_help(argv[i])) {
      return driver_c_backend_usage();
    }
  }
  return driver_c_run_default();
}
#endif

static int32_t cheng_next_cap(int32_t cur_cap, int32_t need) {
  if (need <= 0) return 0;
  int32_t cap = cur_cap < 4 ? 4 : cur_cap;
  while (cap < need) {
    int32_t doubled = cap * 2;
    if (doubled <= 0) return need;
    cap = doubled;
  }
  return cap;
}

static int cheng_ptr_is_suspicious(void *p) {
  if (p == NULL) return 0;
  uintptr_t v = (uintptr_t)p;
  if (v < 4096u) return 1;
  if (v > 0x0000FFFFFFFFFFFFull) return 1;
  return 0;
}

/*
 * Self-link executables may pass stale/uninitialized pointers to realloc when
 * stage1 lowers seq growth through raw temporaries. Guard this path so we
 * fail soft (allocate fresh) instead of aborting in libmalloc.
 */
void *realloc(void *p, size_t size) {
  if (size == 0u) size = 1u;
  if (cheng_ptr_is_suspicious(p)) p = NULL;
#if defined(__APPLE__)
  malloc_zone_t *zone = malloc_default_zone();
  if (zone != NULL) {
    return malloc_zone_realloc(zone, p, size);
  }
#endif
  if (p == NULL) return malloc(size);
  void *out = malloc(size);
  if (out == NULL) return NULL;
#if defined(__APPLE__)
  size_t old_size = malloc_size(p);
  if (old_size > 0u) {
    size_t n = old_size < size ? old_size : size;
    memcpy(out, p, n);
  }
#endif
  free(p);
  return out;
}

static void cheng_seq_sanitize(ChengSeqHeader *hdr) {
  if (hdr == NULL) return;
  if (hdr->cap < 0 || hdr->cap > (1 << 27) || hdr->len < 0 || hdr->len > hdr->cap) {
    hdr->len = 0;
    hdr->cap = 0;
    hdr->buffer = NULL;
    return;
  }
  if (hdr->cap == 0) {
    hdr->buffer = NULL;
  }
  if (hdr->buffer == NULL && hdr->len != 0) {
    hdr->len = 0;
  }
  if (hdr->buffer != NULL) {
    uintptr_t p = (uintptr_t)hdr->buffer;
    if (p < 4096u || p > 0x0000FFFFFFFFFFFFull) {
      hdr->len = 0;
      hdr->cap = 0;
      hdr->buffer = NULL;
    }
  }
}

void *c_malloc(int64_t size) {
  if (size <= 0) size = 1;
  return malloc((size_t)size);
}

void *c_calloc(int64_t nmemb, int64_t size) {
  if (nmemb <= 0) nmemb = 1;
  if (size <= 0) size = 1;
  return calloc((size_t)nmemb, (size_t)size);
}

void *c_realloc(void *p, int64_t size) {
  if (size <= 0) size = 1;
  return realloc(p, (size_t)size);
}

void c_free(void *p) { free(p); }

int32_t c_puts_runtime(char *text) { return puts(text ? text : ""); }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((used, noinline))
#endif
int32_t c_puts(char *text) {
  return c_puts_runtime(text);
}

void c_exit(int32_t code) { exit(code); }

void *alloc(int64_t size) {
  return c_malloc(size);
}

void dealloc(void *p) {
  c_free(p);
}

void *c_memcpy(void *dest, void *src, int64_t n) {
  if (!dest || !src || n <= 0) return dest;
  return memcpy(dest, src, (size_t)n);
}

void *c_memset(void *dest, int32_t val, int64_t n) {
  if (!dest || n <= 0) return dest;
  return memset(dest, val, (size_t)n);
}

void *cheng_memcpy(void *dest, void *src, int64_t n) {
  return c_memcpy(dest, src, n);
}

void *cheng_memset(void *dest, int32_t val, int64_t n) {
  return c_memset(dest, val, n);
}

void *cheng_malloc(int32_t size) {
  return c_malloc((int64_t)size);
}

void cheng_free(void *p) {
  c_free(p);
}

void *cheng_realloc(void *p, int32_t size) {
  return c_realloc(p, (int64_t)size);
}

void cheng_mem_retain(void *p) {
  (void)p;
}

void cheng_mem_release(void *p) {
  (void)p;
}

void memRetain(void *p) { cheng_mem_retain(p); }
void memRelease(void *p) { cheng_mem_release(p); }

void zeroMem(void *p, int64_t n) {
  if (!p || n <= 0) return;
  memset(p, 0, (size_t)n);
}

int32_t c_memcmp(void *a, void *b, int64_t n) {
  if (!a || !b || n <= 0) return 0;
  return memcmp(a, b, (size_t)n);
}

CHENG_SHIM_WEAK int32_t c_strlen(char *s) {
  return cheng_strlen(s);
}

char *c_getenv(char *name) {
  if (!name) return NULL;
  return getenv(name);
}

char *driver_c_getenv(const char *name) {
  if (!name) return NULL;
  return getenv(name);
}

int32_t driver_c_env_bool(const char *name, int32_t defaultValue) {
  const char *raw = getenv(name != NULL ? name : "");
  if (raw == NULL || raw[0] == '\0') return defaultValue ? 1 : 0;
  if (strcmp(raw, "1") == 0 || strcmp(raw, "true") == 0 || strcmp(raw, "TRUE") == 0 ||
      strcmp(raw, "yes") == 0 || strcmp(raw, "YES") == 0 || strcmp(raw, "on") == 0 ||
      strcmp(raw, "ON") == 0) return 1;
  if (strcmp(raw, "0") == 0 || strcmp(raw, "false") == 0 || strcmp(raw, "FALSE") == 0 ||
      strcmp(raw, "no") == 0 || strcmp(raw, "NO") == 0 || strcmp(raw, "off") == 0 ||
      strcmp(raw, "OFF") == 0) return 0;
  return defaultValue ? 1 : 0;
}

static int32_t driver_c_env_present_nonempty(const char *name) {
  const char *raw = getenv(name != NULL ? name : "");
  return (raw != NULL && raw[0] != '\0') ? 1 : 0;
}

static int32_t driver_c_env_is_falsey_text(const char *raw) {
  if (raw == NULL || raw[0] == '\0') return 1;
  if ((raw[0] == '0' && raw[1] == '\0') || strcmp(raw, "false") == 0 ||
      strcmp(raw, "FALSE") == 0 || strcmp(raw, "no") == 0 || strcmp(raw, "NO") == 0) {
    return 1;
  }
  return 0;
}

static int32_t driver_c_reject_removed_stage1_skip_envs(void) {
  const char *fixed_sem_raw = NULL;
  const char *fixed_ownership_raw = NULL;
  if (driver_c_env_present_nonempty("STAGE1_SKIP_SEM")) {
    fprintf(stderr, "backend_driver: removed env STAGE1_SKIP_SEM, use STAGE1_SEM_FIXED_0=0\n");
    return 2;
  }
  if (driver_c_env_present_nonempty("STAGE1_SKIP_OWNERSHIP")) {
    fprintf(stderr, "backend_driver: removed env STAGE1_SKIP_OWNERSHIP, use STAGE1_OWNERSHIP_FIXED_0=0\n");
    return 2;
  }
  fixed_sem_raw = getenv("STAGE1_SEM_FIXED_0");
  if (fixed_sem_raw != NULL && fixed_sem_raw[0] != '\0' &&
      !driver_c_env_is_falsey_text(fixed_sem_raw)) {
    fprintf(stderr, "backend_driver: STAGE1_SEM_FIXED_0 is fixed=0\n");
    return 2;
  }
  fixed_ownership_raw = getenv("STAGE1_OWNERSHIP_FIXED_0");
  if (fixed_ownership_raw != NULL && fixed_ownership_raw[0] != '\0' &&
      !driver_c_env_is_falsey_text(fixed_ownership_raw)) {
    fprintf(stderr, "backend_driver: STAGE1_OWNERSHIP_FIXED_0 is fixed=0\n");
    return 2;
  }
  return 0;
}

char *driver_c_getenv_copy(const char *name) {
  const char *raw = getenv(name != NULL ? name : "");
  if (raw == NULL) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_strmeta_put(out, 0);
    }
    return out;
  }
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  memcpy(out, raw, len);
  out[len] = '\0';
  cheng_strmeta_put(out, (int32_t)len);
  return out;
}

CHENG_SHIM_WEAK ChengStrBridge driver_c_getenv_copy_bridge(const char *name) {
  return cheng_str_bridge_from_owned(driver_c_getenv_copy(name));
}

CHENG_SHIM_WEAK void driver_c_getenv_copy_bridge_into(const char *name, ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_getenv_copy_bridge(name);
}

static char *driver_c_dup_cstr(const char *raw) {
  if (raw == NULL) raw = "";
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  if (len > 0) memcpy(out, raw, len);
  out[len] = '\0';
  cheng_strmeta_put(out, (int32_t)len);
  return out;
}

CHENG_SHIM_WEAK char *driver_c_dup_string(const char *raw) {
  return driver_c_dup_cstr(raw);
}

static int32_t driver_c_str_has_suffix_text(const char *text, const char *suffix) {
  if (text == NULL || suffix == NULL) return 0;
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);
  if (suffix_len == 0) return 1;
  if (suffix_len > text_len) return 0;
  return memcmp(text + (text_len - suffix_len), suffix, suffix_len) == 0 ? 1 : 0;
}

static char *driver_c_default_output_path_copy(const char *input_path) {
  const char *input = input_path != NULL ? input_path : "";
  size_t input_len = strlen(input);
  const char *emit = getenv("BACKEND_EMIT");
  int32_t emit_obj = (emit != NULL && strcmp(emit, "obj") == 0) ? 1 : 0;
  if (input_len == 0) return driver_c_dup_cstr("");
  if (driver_c_str_has_suffix_text(input, ".cheng")) {
    size_t stem_len = input_len - 6u;
    const char *suffix = emit_obj ? ".o" : "";
    size_t suffix_len = strlen(suffix);
    char *out = (char *)cheng_malloc((int32_t)(stem_len + suffix_len) + 1);
    if (out == NULL) return NULL;
    if (stem_len > 0) memcpy(out, input, stem_len);
    if (suffix_len > 0) memcpy(out + stem_len, suffix, suffix_len);
    out[stem_len + suffix_len] = '\0';
    cheng_strmeta_put(out, (int32_t)(stem_len + suffix_len));
    return out;
  }
  {
    const char *suffix = emit_obj ? ".o" : ".out";
    size_t suffix_len = strlen(suffix);
    char *out = (char *)cheng_malloc((int32_t)(input_len + suffix_len) + 1);
    if (out == NULL) return NULL;
    memcpy(out, input, input_len);
    memcpy(out + input_len, suffix, suffix_len);
    out[input_len + suffix_len] = '\0';
    cheng_strmeta_put(out, (int32_t)(input_len + suffix_len));
    return out;
  }
}

static char *driver_c_cli_value_copy(const char *key) {
  if (key == NULL || key[0] == '\0') return driver_c_dup_cstr("");
  int32_t argc = driver_c_arg_count();
  size_t key_len = strlen(key);
  if (argc <= 0 || argc > 4096) return driver_c_dup_cstr("");
  for (int32_t i = 1; i <= argc; ++i) {
    const char *arg = driver_c_arg_copy(i);
    if (arg == NULL || arg[0] == '\0') continue;
    if (strcmp(arg, key) == 0) {
      if (i + 1 <= argc) return driver_c_dup_cstr(driver_c_arg_copy(i + 1));
      return driver_c_dup_cstr("");
    }
    if (strncmp(arg, key, key_len) == 0 && (arg[key_len] == ':' || arg[key_len] == '=')) {
      return driver_c_dup_cstr(arg + key_len + 1);
    }
  }
  return driver_c_dup_cstr("");
}

CHENG_SHIM_WEAK char *driver_c_cli_input_copy(void) {
  char *value = driver_c_cli_value_copy("--input");
  if (value != NULL && value[0] != '\0') return value;
  int32_t argc = driver_c_arg_count();
  if (argc > 0 && argc <= 4096) {
    for (int32_t i = 1; i <= argc; ++i) {
      const char *arg = driver_c_arg_copy(i);
      if (arg == NULL || arg[0] == '\0') continue;
      if (arg[0] != '-') return driver_c_dup_cstr(arg);
    }
  }
  return value;
}

CHENG_SHIM_WEAK ChengStrBridge driver_c_cli_input_copy_bridge(void) {
  return cheng_str_bridge_from_owned(driver_c_cli_input_copy());
}

CHENG_SHIM_WEAK void driver_c_cli_input_copy_bridge_into(ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_cli_input_copy_bridge();
}

CHENG_SHIM_WEAK char *driver_c_cli_output_copy(void) {
  return driver_c_cli_value_copy("--output");
}

CHENG_SHIM_WEAK ChengStrBridge driver_c_cli_output_copy_bridge(void) {
  return cheng_str_bridge_from_owned(driver_c_cli_output_copy());
}

CHENG_SHIM_WEAK void driver_c_cli_output_copy_bridge_into(ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_cli_output_copy_bridge();
}

CHENG_SHIM_WEAK char *driver_c_cli_target_copy(void) {
  return driver_c_cli_value_copy("--target");
}

CHENG_SHIM_WEAK ChengStrBridge driver_c_cli_target_copy_bridge(void) {
  return cheng_str_bridge_from_owned(driver_c_cli_target_copy());
}

CHENG_SHIM_WEAK void driver_c_cli_target_copy_bridge_into(ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_cli_target_copy_bridge();
}

CHENG_SHIM_WEAK char *driver_c_cli_linker_copy(void) {
  return driver_c_cli_value_copy("--linker");
}

CHENG_SHIM_WEAK ChengStrBridge driver_c_cli_linker_copy_bridge(void) {
  return cheng_str_bridge_from_owned(driver_c_cli_linker_copy());
}

CHENG_SHIM_WEAK void driver_c_cli_linker_copy_bridge_into(ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_cli_linker_copy_bridge();
}

char *driver_c_new_string(int32_t n) {
  if (n <= 0) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_strmeta_put(out, 0);
    }
    return out;
  }
  char *out = (char *)cheng_malloc(n + 1);
  if (out == NULL) return NULL;
  memset(out, 0, (size_t)n + 1u);
  cheng_strmeta_put(out, n);
  return out;
}

char *driver_c_new_string_copy_n(void *raw, int32_t n) {
  if (raw == NULL || n <= 0) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_strmeta_put(out, 0);
    }
    return out;
  }
  char *out = (char *)cheng_malloc(n + 1);
  if (out == NULL) return NULL;
  memcpy(out, raw, (size_t)n);
  out[n] = '\0';
  cheng_strmeta_put(out, n);
  return out;
}

char *driver_c_read_file_all(const char *path) {
  if (path == NULL || path[0] == '\0') {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  long size_long = ftell(f);
  if (size_long < 0) {
    fclose(f);
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  int32_t size = (int32_t)size_long;
  char *out = (char *)cheng_malloc(size + 1);
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
    char *empty = (char *)cheng_malloc(1);
    if (empty != NULL) empty[0] = '\0';
    return empty;
  }
  out[size] = '\0';
  cheng_strmeta_put(out, size);
  return out;
}

CHENG_SHIM_WEAK ChengStrBridge driver_c_read_file_all_bridge(const char *path) {
  return cheng_str_bridge_from_owned(driver_c_read_file_all(path));
}

CHENG_SHIM_WEAK void driver_c_read_file_all_bridge_into(const char *path, ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_read_file_all_bridge(path);
}

int32_t driver_c_argv_is_help(int32_t argc, void *argv_void) {
  char **argv = (char **)argv_void;
  if (argc != 2 || argv == NULL) return 0;
  const char *arg1 = argv[1];
  if (arg1 == NULL) return 0;
  return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
}

int32_t driver_c_cli_help_requested(void) {
  if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) {
    return driver_c_argv_is_help(cheng_saved_argc, (void *)cheng_saved_argv);
  }
  int32_t argc = __cheng_rt_paramCount();
  if (argc != 2) return 0;
  char *arg1 = __cheng_rt_paramStrCopy(1);
  if (arg1 == NULL) return 0;
  return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
}

int32_t driver_c_env_nonempty(const char *name) {
  const char *raw = getenv(name != NULL ? name : "");
  return (raw != NULL && raw[0] != '\0') ? 1 : 0;
}

int32_t driver_c_env_eq(const char *name, const char *expected) {
  const char *raw = getenv(name != NULL ? name : "");
  if (raw == NULL || raw[0] == '\0' || expected == NULL) return 0;
  return strcmp(raw, expected) == 0 ? 1 : 0;
}

static const char *driver_c_host_target_default(void) {
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

CHENG_SHIM_WEAK char *driver_c_active_output_path(const char *input_path) {
  const char *raw = getenv("BACKEND_OUTPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_OUTPUT");
  return driver_c_default_output_path_copy(input_path);
}

CHENG_SHIM_WEAK char *driver_c_active_output_copy(void) {
  const char *raw = getenv("BACKEND_OUTPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_OUTPUT");
  return (char *)"";
}

CHENG_SHIM_WEAK char *driver_c_active_input_path(void) {
  const char *raw = getenv("BACKEND_INPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_INPUT");
  return (char *)"";
}

CHENG_SHIM_WEAK char *driver_c_active_target(void) {
  const char *raw = getenv("BACKEND_TARGET");
  if (raw == NULL || raw[0] == '\0' ||
      strcmp(raw, "auto") == 0 || strcmp(raw, "native") == 0 || strcmp(raw, "host") == 0) {
    return (char *)driver_c_host_target_default();
  }
  return driver_c_getenv_copy("BACKEND_TARGET");
}

CHENG_SHIM_WEAK char *driver_c_active_linker(void) {
  const char *raw = getenv("BACKEND_LINKER");
  if (raw == NULL || raw[0] == '\0') return (char *)"system";
  return driver_c_getenv_copy("BACKEND_LINKER");
}

static int32_t driver_c_target_is_darwin_alias(const char *raw) {
  if (raw == NULL || raw[0] == '\0') return 0;
  return strcmp(raw, "arm64-apple-darwin") == 0 ||
         strcmp(raw, "aarch64-apple-darwin") == 0 ||
         strcmp(raw, "arm64-darwin") == 0 ||
         strcmp(raw, "aarch64-darwin") == 0 ||
         strcmp(raw, "darwin_arm64") == 0 ||
         strcmp(raw, "darwin_aarch64") == 0;
}

static char *driver_c_resolve_input_path(void) {
  char *cli = driver_c_cli_input_copy();
  if (cli != NULL && cli[0] != '\0') return cli;
  return driver_c_active_input_path();
}

static char *driver_c_resolve_output_path(const char *input_path) {
  char *cli = driver_c_cli_output_copy();
  if (cli != NULL && cli[0] != '\0') return cli;
  return driver_c_active_output_path(input_path);
}

static char *driver_c_resolve_target(void) {
  char *cli = driver_c_cli_target_copy();
  if (cli != NULL && cli[0] != '\0') {
    if (strcmp(cli, "auto") == 0 || strcmp(cli, "native") == 0 || strcmp(cli, "host") == 0) {
      return (char *)driver_c_host_target_default();
    }
    if (driver_c_target_is_darwin_alias(cli)) return (char *)"arm64-apple-darwin";
    return cli;
  }
  {
    char *active = driver_c_active_target();
    if (active == NULL || active[0] == '\0') return (char *)driver_c_host_target_default();
    if (strcmp(active, "auto") == 0 || strcmp(active, "native") == 0 || strcmp(active, "host") == 0) {
      return (char *)driver_c_host_target_default();
    }
    if (driver_c_target_is_darwin_alias(active)) return (char *)"arm64-apple-darwin";
    return active;
  }
}

static char *driver_c_resolve_linker(void) {
  char *cli = driver_c_cli_linker_copy();
  if (cli != NULL && cli[0] != '\0') return cli;
  return driver_c_active_linker();
}

static int32_t driver_c_emit_is_obj_mode(void) {
  const char *raw = getenv("BACKEND_EMIT");
  if (raw == NULL || raw[0] == '\0' || strcmp(raw, "exe") == 0) return 0;
  if (strcmp(raw, "obj") == 0 && driver_c_env_bool("BACKEND_INTERNAL_ALLOW_EMIT_OBJ", 0) != 0) {
    return 1;
  }
  fputs("backend_driver: invalid emit mode (expected exe)\n", stderr);
  return -50;
}

static void driver_c_require_output_file_or_die(const char *path, const char *phase) {
  const char *phase_text = (phase != NULL && phase[0] != '\0') ? phase : "<unknown>";
  if (path == NULL || path[0] == '\0') {
    fprintf(stderr, "backend_driver: missing output path after %s\n", phase_text);
    exit(1);
  }
  if (cheng_file_exists((char *)path) == 0) {
    fprintf(stderr, "backend_driver: missing output after %s: %s\n", phase_text, path);
    exit(1);
  }
  if (cheng_file_size((char *)path) <= 0) {
    fprintf(stderr, "backend_driver: empty output after %s: %s\n", phase_text, path);
    exit(1);
  }
}

CHENG_SHIM_WEAK int32_t driver_c_finish_emit_obj(const char *path) {
  driver_c_require_output_file_or_die(path, "emit_obj");
  driver_c_boot_marker(5);
  return 0;
}

int32_t driver_c_write_exact_file(const char *path, void *buffer, int32_t len) {
  if (path == NULL || path[0] == '\0') return -1;
  if (buffer == NULL || len <= 0) return -2;
  int32_t fd = cheng_open_w_trunc(path);
  if (fd < 0) return -3;
  int32_t wrote = cheng_fd_write(fd, (const char *)buffer, len);
  (void)libc_close(fd);
  if (wrote != len) return -4;
  if (cheng_file_exists(path) == 0) return -5;
  if (cheng_file_size(path) != (int64_t)len) return -6;
  return 0;
}

CHENG_SHIM_WEAK int32_t driver_c_emit_obj_default(void *module, const char *target, const char *path) {
  driver_c_maybe_run_self_global_init();
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
  if (driver_emit_obj_from_module_default_impl != NULL) {
    int32_t rc = driver_emit_obj_from_module_default_impl(module, target, path);
    driver_c_diagf("[driver_c_emit_obj_default] weak_emit_rc=%d\n", rc);
    if (rc == 0) return rc;
  }
  cheng_driver_emit_obj_default_fn emit_obj_sidecar_default =
      (cheng_driver_emit_obj_default_fn)driver_c_lookup_runtime_symbol(
          "driver_emit_obj_from_module_default_impl",
          "driver_c_emit_obj_default.emit_default");
  if (emit_obj_sidecar_default != NULL &&
      emit_obj_sidecar_default != driver_emit_obj_from_module_default_impl) {
    int32_t rc = emit_obj_sidecar_default(module, target, path);
    driver_c_diagf("[driver_c_emit_obj_default] dlsym_emit_rc=%d\n", rc);
    if (rc == 0) return rc;
  }
  {
    cheng_driver_emit_obj_default_fn sidecar_emit_fn =
        (cheng_driver_emit_obj_default_fn)cheng_sidecar_driver_emit_obj_symbol(
            &cheng_sidecar_cache, target, driver_c_diagf, driver_c_sidecar_after_open);
    if (sidecar_emit_fn != NULL) {
      int32_t rc = sidecar_emit_fn(module, target, path);
      driver_c_diagf("[driver_c_emit_obj_default] sidecar_rc=%d\n", rc);
      if (rc == 0) return rc;
    }
  }
  ChengSeqHeader obj;
  memset(&obj, 0, sizeof(obj));
  cheng_emit_obj_from_module_fn emit_obj = NULL;
  if (uirEmitObjFromModuleOrPanic != NULL) {
    emit_obj = (cheng_emit_obj_from_module_fn)uirEmitObjFromModuleOrPanic;
  }
  if (emit_obj == NULL) {
    emit_obj = (cheng_emit_obj_from_module_fn)driver_c_lookup_runtime_symbol(
        "uirEmitObjFromModuleOrPanic", "driver_c_emit_obj_default.uir_emit");
  }
  if (emit_obj == NULL) {
    driver_c_diagf("[driver_c_emit_obj_default] no_emit_symbol\n");
    return -10;
  }
  emit_obj(&obj, module, driver_c_env_i32("BACKEND_OPT_LEVEL", 0), target, "", 0, 0, 0,
           "autovec");
  driver_c_diagf("[driver_c_emit_obj_default] weak_uir_emit len=%d buffer=%p\n", obj.len, obj.buffer);
  if (obj.len <= 0) return -13;
  if (obj.buffer == NULL) return -14;
  {
    int32_t rc = driver_c_write_exact_file(path, obj.buffer, obj.len);
    driver_c_diagf("[driver_c_emit_obj_default] write_rc=%d\n", rc);
    return rc;
  }
}

CHENG_SHIM_WEAK int32_t driver_c_link_tmp_obj_default(const char *output_path, const char *obj_path,
                                                      const char *target, const char *linker) {
  const char *target_text = (target != NULL) ? target : "";
  const char *linker_text = (linker != NULL && linker[0] != '\0') ? linker : "system";
  const char *runtime_c = "/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c";
  const char *cflags = getenv("BACKEND_CFLAGS");
  const char *ldflags = getenv("BACKEND_LDFLAGS");
  const char *cflags_text = (cflags != NULL) ? cflags : "";
  const char *ldflags_text = (ldflags != NULL) ? ldflags : "";
  char full_ldflags[512];
  char extra_ldflags[96];
  full_ldflags[0] = '\0';
  extra_ldflags[0] = '\0';
  if (output_path == NULL || output_path[0] == '\0') return -20;
  if (obj_path == NULL || obj_path[0] == '\0') return -21;
  if (strcmp(linker_text, "system") != 0) return -22;
  const char *base = strrchr(obj_path, '/');
  const char *obj_name = (base != NULL) ? (base + 1) : obj_path;
  int use_driver_entry_shim = strstr(obj_name, "backend_driver") != NULL ||
                              driver_c_env_bool("BACKEND_FORCE_DRIVER_ENTRY_SHIM", 0) != 0;
  int needs_darwin_codesign = 0;
  if (strstr(target_text, "darwin") != NULL || strstr(target_text, "apple-darwin") != NULL) {
    needs_darwin_codesign = 1;
    if (strstr(ldflags_text, "-Wl,-no_adhoc_codesign") == NULL) {
      snprintf(extra_ldflags + strlen(extra_ldflags),
               sizeof(extra_ldflags) - strlen(extra_ldflags),
               " -Wl,-no_adhoc_codesign");
    }
    if (strstr(ldflags_text, "-Wl,-export_dynamic") == NULL) {
      snprintf(extra_ldflags + strlen(extra_ldflags),
               sizeof(extra_ldflags) - strlen(extra_ldflags),
               " -Wl,-export_dynamic");
    }
  }
  snprintf(full_ldflags, sizeof(full_ldflags), "%s%s", ldflags_text, extra_ldflags);
  size_t need = strlen(obj_path) + strlen(runtime_c) + strlen(output_path) +
                strlen(cflags_text) + strlen(ldflags_text) + strlen(extra_ldflags) + 128u;
  char *cmd = (char *)malloc(need);
  if (cmd == NULL) return -23;
  if (use_driver_entry_shim) {
    snprintf(cmd, need, "cc -DCHENG_BACKEND_DRIVER_ENTRY_SHIM %s '%s' '%s' %s -o '%s'",
             cflags_text, obj_path, runtime_c, full_ldflags, output_path);
  } else {
    snprintf(cmd, need, "cc %s '%s' '%s' %s -o '%s'",
             cflags_text, obj_path, runtime_c, full_ldflags, output_path);
  }
  if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
    fprintf(stderr, "[driver_c_link_tmp_obj_default] obj=%s out=%s target=%s sidecar=<bundle-only>\n",
            obj_path, output_path, target_text);
    fprintf(stderr, "[driver_c_link_tmp_obj_default] cmd=%s\n", cmd);
    fflush(stderr);
  }
  int rc = system(cmd);
  free(cmd);
  if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
    fprintf(stderr, "[driver_c_link_tmp_obj_default] system_rc=%d\n", rc);
    fflush(stderr);
  }
  if (rc != 0) return -24;
  if (cheng_file_exists(output_path) == 0) {
    if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
      fprintf(stderr, "[driver_c_link_tmp_obj_default] missing_output=%s\n", output_path);
      fflush(stderr);
    }
    return -25;
  }
  if (cheng_file_size(output_path) <= 0) {
    if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
      fprintf(stderr, "[driver_c_link_tmp_obj_default] empty_output=%s\n", output_path);
      fflush(stderr);
    }
    return -26;
  }
  if (needs_darwin_codesign) {
    size_t sign_need = strlen(output_path) * 2u + 128u;
    char *sign_cmd = (char *)malloc(sign_need);
    if (sign_cmd == NULL) return -27;
    snprintf(sign_cmd, sign_need,
             "codesign --force --sign - '%s' >/dev/null 2>&1 && "
             "codesign --verify --verbose=2 '%s' >/dev/null 2>&1",
             output_path, output_path);
    rc = system(sign_cmd);
    if (getenv("BACKEND_DEBUG_LINK_CMD") != NULL) {
      fprintf(stderr, "[driver_c_link_tmp_obj_default] codesign_rc=%d\n", rc);
      fflush(stderr);
    }
    free(sign_cmd);
    if (rc != 0) return -28;
  }
  return 0;
}

CHENG_SHIM_WEAK int32_t driver_c_link_tmp_obj_system(const char *output_path, const char *obj_path,
                                                     const char *target) {
  return driver_c_link_tmp_obj_default(output_path, obj_path, target, "system");
}

CHENG_SHIM_WEAK int32_t driver_c_build_emit_obj_default(const char *input_path, const char *target,
                                                        const char *output_path) {
  if (input_path == NULL || input_path[0] == '\0') return -30;
  if (target == NULL || target[0] == '\0') return -31;
  if (output_path == NULL || output_path[0] == '\0') return -32;
  void *module = driver_c_build_module_stage1(input_path, target);
  if (module == NULL) return -33;
  int32_t emit_rc = driver_c_emit_obj_default(module, target, output_path);
  if (emit_rc != 0) return emit_rc;
  return driver_c_finish_emit_obj(output_path);
}

CHENG_SHIM_WEAK int32_t driver_c_build_active_obj_default(void) {
  char *input_path = driver_c_active_input_path();
  if (input_path == NULL || input_path[0] == '\0') return 2;
  char *target = driver_c_active_target();
  char *output_path = driver_c_active_output_path(input_path);
  return driver_c_build_emit_obj_default(input_path, target, output_path);
}

CHENG_SHIM_WEAK int32_t driver_c_build_link_exe_default(const char *input_path, const char *target,
                                                        const char *output_path, const char *linker) {
  (void)linker;
  if (output_path == NULL || output_path[0] == '\0') return -40;
  const char *suffix = ".tmp.linkobj";
  size_t need = strlen(output_path) + strlen(suffix) + 1u;
  char *obj_path = (char *)malloc(need);
  if (obj_path == NULL) return -41;
  snprintf(obj_path, need, "%s%s", output_path, suffix);
  int32_t emit_rc = driver_c_build_emit_obj_default(input_path, target, obj_path);
  if (emit_rc != 0) {
    free(obj_path);
    return emit_rc;
  }
  int32_t link_rc = driver_c_link_tmp_obj_system(output_path, obj_path, target);
  if (link_rc != 0) {
    free(obj_path);
    return link_rc;
  }
  int32_t finish_rc = driver_c_finish_single_link(output_path, obj_path);
  free(obj_path);
  return finish_rc;
}

CHENG_SHIM_WEAK void *driver_c_build_module_stage1(const char *input_path, const char *target) {
  driver_c_maybe_run_self_global_init();
  driver_c_diagf("[driver_c_build_module_stage1] input='%s' target='%s'\n",
                 input_path != NULL ? input_path : "", target != NULL ? target : "");
  if (input_path == NULL || input_path[0] == '\0') {
    driver_c_diagf("[driver_c_build_module_stage1] input_missing\n");
    return NULL;
  }
  if (target == NULL) target = "";
  {
    cheng_build_module_stage1_fn build_module_stage1_target_retained =
        (cheng_build_module_stage1_fn)driver_c_lookup_runtime_symbol(
            "driver_export_buildModuleFromFileStage1TargetRetained",
            "driver_c_build_module_stage1.retained_target");
    if (build_module_stage1_target_retained != NULL) {
      void *module = build_module_stage1_target_retained(input_path, target);
      driver_c_diagf("[driver_c_build_module_stage1] dlsym_retained_target_module=%p\n", module);
      if (module != NULL) return module;
    }
  }
  if (driver_buildActiveModulePtrs != NULL) {
    void *module = driver_buildActiveModulePtrs((void *)input_path, (void *)target);
    driver_c_diagf("[driver_c_build_module_stage1] weak_sidecar_module=%p\n", module);
    if (module != NULL) return module;
  }
  if (uirCoreBuildModuleFromFileStage1OrPanic != NULL) {
    void *module = uirCoreBuildModuleFromFileStage1OrPanic(input_path, target);
    driver_c_diagf("[driver_c_build_module_stage1] weak_builder_module=%p\n", module);
    if (module != NULL) return module;
  }
  cheng_build_module_stage1_fn build_module_stage1 =
      (cheng_build_module_stage1_fn)driver_c_lookup_runtime_symbol(
          "uirCoreBuildModuleFromFileStage1OrPanic",
          "driver_c_build_module_stage1.builder");
  if (build_module_stage1 != NULL &&
      build_module_stage1 != (cheng_build_module_stage1_fn)uirCoreBuildModuleFromFileStage1OrPanic) {
    void *module = build_module_stage1(input_path, target);
    driver_c_diagf("[driver_c_build_module_stage1] dlsym_builder_module=%p\n", module);
    if (module != NULL) return module;
  }
  {
    cheng_build_active_module_ptrs_fn build_active_module_ptrs_fn =
        (cheng_build_active_module_ptrs_fn)driver_c_lookup_runtime_symbol(
            "driver_buildActiveModulePtrs",
            "driver_c_build_module_stage1.sidecar");
    if (build_active_module_ptrs_fn != NULL &&
        build_active_module_ptrs_fn != driver_buildActiveModulePtrs) {
      void *module = build_active_module_ptrs_fn((void *)input_path, (void *)target);
      driver_c_diagf("[driver_c_build_module_stage1] dlsym_sidecar_module=%p\n", module);
      if (module != NULL) return module;
    }
  }
  {
    cheng_build_active_module_ptrs_fn sidecar_build_fn =
        (cheng_build_active_module_ptrs_fn)cheng_sidecar_build_module_symbol(
            &cheng_sidecar_cache, target, driver_c_diagf, driver_c_sidecar_after_open);
    if (sidecar_build_fn != NULL) {
      void *module = sidecar_build_fn((void *)input_path, (void *)target);
      driver_c_diagf("[driver_c_build_module_stage1] sidecar_module=%p\n", module);
      if (module != NULL) return module;
    }
  }
  driver_c_diagf("[driver_c_build_module_stage1] no_module\n");
  return NULL;
}

CHENG_SHIM_WEAK void *driver_c_build_module_stage1_direct(const char *input_path, const char *target) {
  driver_c_maybe_run_self_global_init();
  driver_c_diagf("[driver_c_build_module_stage1_direct] input='%s' target='%s'\n",
                 input_path != NULL ? input_path : "", target != NULL ? target : "");
  if (input_path == NULL || input_path[0] == '\0') {
    driver_c_diagf("[driver_c_build_module_stage1_direct] input_missing\n");
    return NULL;
  }
  if (target == NULL) target = "";
  {
    cheng_build_module_stage1_fn build_module_stage1_target_retained =
        (cheng_build_module_stage1_fn)driver_c_lookup_runtime_symbol(
            "driver_export_buildModuleFromFileStage1TargetRetained",
            "driver_c_build_module_stage1_direct.retained_target");
    if (build_module_stage1_target_retained != NULL) {
      void *module = build_module_stage1_target_retained(input_path, target);
      driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_retained_target_module=%p\n",
                     module);
      if (module != NULL) return module;
    }
  }
  if (uirCoreBuildModuleFromFileStage1OrPanic != NULL) {
    void *module = uirCoreBuildModuleFromFileStage1OrPanic(input_path, target);
    driver_c_diagf("[driver_c_build_module_stage1_direct] weak_builder_module=%p\n", module);
    if (module != NULL) return module;
  }
  cheng_build_module_stage1_fn build_module_stage1 =
      (cheng_build_module_stage1_fn)driver_c_lookup_runtime_symbol(
          "uirCoreBuildModuleFromFileStage1OrPanic",
          "driver_c_build_module_stage1_direct.builder");
  if (build_module_stage1 != NULL &&
      build_module_stage1 != (cheng_build_module_stage1_fn)uirCoreBuildModuleFromFileStage1OrPanic) {
    void *module = build_module_stage1(input_path, target);
    driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_builder_module=%p\n", module);
    if (module != NULL) return module;
  }
  driver_c_diagf("[driver_c_build_module_stage1_direct] no_module\n");
  return NULL;
}

CHENG_SHIM_WEAK int32_t driver_c_build_active_exe_default(void) {
  char *input_path = driver_c_active_input_path();
  if (input_path == NULL || input_path[0] == '\0') return 2;
  char *target = driver_c_active_target();
  char *output_path = driver_c_active_output_path(input_path);
  char *linker = driver_c_active_linker();
  return driver_c_build_link_exe_default(input_path, target, output_path, linker);
}

CHENG_SHIM_WEAK int32_t driver_c_run_default(void) {
  int32_t env_gate_rc = driver_c_reject_removed_stage1_skip_envs();
  if (env_gate_rc != 0) return env_gate_rc;
  int32_t emit_mode = driver_c_emit_is_obj_mode();
  if (emit_mode < 0) return emit_mode;
  char *input_path = driver_c_resolve_input_path();
  char *target = driver_c_resolve_target();
  char *output_path = driver_c_resolve_output_path(input_path);
  char *linker = driver_c_resolve_linker();
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

CHENG_SHIM_WEAK int32_t driver_c_finish_single_link(const char *path, const char *obj_path) {
  driver_c_require_output_file_or_die(path, "single_link");
  if (driver_c_env_bool("BACKEND_KEEP_TMP_LINKOBJ", 0) == 0 &&
      obj_path != NULL && obj_path[0] != '\0') {
    unlink(obj_path);
  }
  driver_c_boot_marker(8);
  return 0;
}

static int32_t driver_c_boot_marker_enabled(void) {
  const char *raw = getenv("BACKEND_DEBUG_BOOT");
  if (raw == NULL || raw[0] == '\0') return 0;
  if (raw[0] == '0' && raw[1] == '\0') return 0;
  return 1;
}

static void driver_c_boot_marker_write(const char *text, size_t len) {
  if (!driver_c_boot_marker_enabled()) return;
  if (text == NULL || len == 0) return;
  (void)write(2, text, len);
}

void driver_c_boot_marker(int32_t code) {
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

void driver_c_capture_cmdline(int32_t argc, void *argv_void) {
  cheng_saved_argc = argc;
  cheng_saved_argv = (const char **)argv_void;
}

int32_t driver_c_capture_cmdline_keep(int32_t argc, void *argv_void) {
  driver_c_capture_cmdline(argc, argv_void);
  return argc;
}

CHENG_SHIM_WEAK int32_t driver_c_arg_count(void) {
  if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) return cheng_saved_argc - 1;
  int32_t argc = __cheng_rt_paramCount();
  if (argc > 1 && argc <= 257) return argc - 1;
  return 0;
}

CHENG_SHIM_WEAK char *driver_c_arg_copy(int32_t i) {
  if (cheng_saved_argv == NULL && i > 0) {
    char *rt = __cheng_rt_paramStrCopy(i);
    if (rt != NULL) return rt;
  }
  if (cheng_saved_argv == NULL || i <= 0 || i >= cheng_saved_argc) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  const char *raw = cheng_saved_argv[i];
  if (raw == NULL) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  memcpy(out, raw, len);
  out[len] = '\0';
  return out;
}

CHENG_SHIM_WEAK int32_t driver_c_help_requested(void) {
  if (cheng_saved_argv != NULL && cheng_saved_argc > 1 && cheng_saved_argc <= 4096) {
    for (int32_t i = 1; i < cheng_saved_argc; ++i) {
      const char *arg = cheng_saved_argv[i];
      if (arg == NULL) continue;
      if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
    }
    return 0;
  }
  int32_t argc = __cheng_rt_paramCount();
  if (argc > 1 && argc <= 4096) {
    for (int32_t i = 1; i < argc; ++i) {
      char *arg = __cheng_rt_paramStrCopy(i);
      if (arg == NULL) continue;
      if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
    }
  }
  return 0;
}

void cheng_bytes_copy(void *dst, void *src, int64_t n) {
  if (!dst || !src || n <= 0) return;
  memcpy(dst, src, (size_t)n);
}

void cheng_bytes_set(void *dst, int32_t value, int64_t n) {
  if (!dst || n <= 0) return;
  memset(dst, value, (size_t)n);
}

static const char *cheng_safe_cstr(const char *s) {
  if (s == NULL) return "";
  uintptr_t raw = (uintptr_t)s;
#if defined(__APPLE__) && UINTPTR_MAX > 0xffffffffu
  bool in_image_or_stack =
      raw >= (uintptr_t)0x0000000100000000ULL &&
      raw <  (uintptr_t)0x0000000200000000ULL;
  bool in_malloc_zone = malloc_zone_from_ptr((void *)raw) != NULL;
  if (!in_image_or_stack && !in_malloc_zone) return "";
#endif
  return (const char *)raw;
}

int32_t cheng_strlen(char *s) {
  size_t n = 0;
  if (cheng_strmeta_get((const char *)s, &n)) {
    return (n > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)n;
  }
  const char *safe = cheng_safe_cstr((const char *)s);
  n = strlen(safe);
  return (n > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)n;
}

int32_t cheng_strcmp(char *a, char *b) {
  return strcmp(cheng_safe_cstr((const char *)a), cheng_safe_cstr((const char *)b));
}

int32_t c_strcmp(char *a, char *b) {
  return cheng_strcmp(a, b);
}

int32_t __cheng_str_eq(const char *a, const char *b) {
  return strcmp(cheng_safe_cstr((const char *)a), cheng_safe_cstr((const char *)b)) == 0 ? 1 : 0;
}

char *cheng_str_concat(char *a, char *b) {
  const char *sa = cheng_safe_cstr((const char *)a);
  const char *sb = cheng_safe_cstr((const char *)b);
  size_t la = strlen(sa);
  size_t lb = strlen(sb);
  size_t total = la + lb;
  if (total > (size_t)INT32_MAX - 1u) {
    total = (size_t)INT32_MAX - 1u;
  }
  char *out = (char *)malloc(total + 1u);
  if (out == NULL) return (char *)"";
  if (la > 0) memcpy(out, sa, la);
  if (lb > 0) memcpy(out + la, sb, lb);
  out[total] = '\0';
  cheng_strmeta_put(out, (int32_t)total);
  return out;
}

static char *cheng_copy_string_bytes(const char *src, size_t len) {
  if (len > (size_t)INT32_MAX - 1u) {
    len = (size_t)INT32_MAX - 1u;
  }
  char *out = (char *)malloc(len + 1u);
  if (out == NULL) return (char *)"";
  if (src != NULL && len > 0) memcpy(out, src, len);
  out[len] = '\0';
  cheng_strmeta_put(out, (int32_t)len);
  return out;
}

char *__cheng_str_concat(char *a, char *b) { return cheng_str_concat(a, b); }
char *__cheng_sym_2b(char *a, char *b) { return cheng_str_concat(a, b); }
char *driver_c_str_clone(const char *s) {
  const char *safe = cheng_safe_cstr((const char *)s);
  return cheng_copy_string_bytes(safe, strlen(safe));
}
CHENG_SHIM_WEAK ChengStrBridge driver_c_str_clone_bridge(const char *s) {
  return cheng_str_bridge_from_owned(driver_c_str_clone(s));
}
char *driver_c_str_slice(const char *s, int32_t start, int32_t count) {
  const char *safe = cheng_safe_cstr((const char *)s);
  size_t n = strlen(safe);
  if (count <= 0) return cheng_copy_string_bytes("", 0u);
  size_t s0 = start < 0 ? 0u : (size_t)start;
  if (s0 >= n) return cheng_copy_string_bytes("", 0u);
  size_t need = (size_t)count;
  if (s0 + need > n) need = n - s0;
  return cheng_copy_string_bytes(safe + s0, need);
}
CHENG_SHIM_WEAK ChengStrBridge driver_c_str_slice_bridge(const char *s, int32_t start, int32_t count) {
  return cheng_str_bridge_from_owned(driver_c_str_slice(s, start, count));
}
char *driver_c_char_to_str(int32_t value) {
  unsigned char ch = (unsigned char)(value & 0xff);
  return cheng_copy_string_bytes((const char *)&ch, 1u);
}
CHENG_SHIM_WEAK ChengStrBridge driver_c_str_from_utf8_copy_bridge(const char *raw, int32_t n) {
  if (raw == NULL || n <= 0) return cheng_str_bridge_from_owned(cheng_copy_string_bytes("", 0u));
  return cheng_str_bridge_from_owned(cheng_copy_string_bytes(raw, (size_t)n));
}
int32_t driver_c_str_get_at(const char *s, int32_t idx) {
  const char *safe = cheng_safe_cstr((const char *)s);
  size_t n = strlen(safe);
  if (idx < 0 || (size_t)idx >= n) return 0;
  return (unsigned char)safe[idx];
}
CHENG_SHIM_WEAK int32_t driver_c_str_get_at_bridge(ChengStrBridge s, int32_t idx) {
  return driver_c_str_get_at(s.ptr, idx);
}
void driver_c_str_set_at(char *s, int32_t idx, int32_t value) {
  const char *safe = cheng_safe_cstr((const char *)s);
  size_t n = strlen(safe);
  if (idx < 0 || (size_t)idx >= n || s == NULL) return;
  s[idx] = (char)(value & 0xff);
}
CHENG_SHIM_WEAK void driver_c_str_set_at_bridge(ChengStrBridge *s, int32_t idx, int32_t value) {
  if (s == NULL) return;
  driver_c_str_set_at((char *)s->ptr, idx, value);
  *s = cheng_str_bridge_from_ptr_flags(s->ptr, s->flags);
}
int32_t driver_c_str_has_prefix(const char *s, const char *prefix) {
  const char *sa = cheng_safe_cstr((const char *)s);
  const char *sb = cheng_safe_cstr((const char *)prefix);
  size_t la = strlen(sa);
  size_t lb = strlen(sb);
  if (lb == 0) return 1;
  if (la < lb) return 0;
  return memcmp(sa, sb, lb) == 0 ? 1 : 0;
}
int32_t driver_c_str_has_suffix(const char *s, const char *suffix) {
  const char *sa = cheng_safe_cstr((const char *)s);
  const char *sb = cheng_safe_cstr((const char *)suffix);
  size_t la = strlen(sa);
  size_t lb = strlen(sb);
  if (lb == 0) return 1;
  if (la < lb) return 0;
  return memcmp(sa + (la - lb), sb, lb) == 0 ? 1 : 0;
}
CHENG_SHIM_WEAK int32_t driver_c_str_has_prefix_bridge(ChengStrBridge s, ChengStrBridge prefix) {
  return driver_c_str_has_prefix(s.ptr, prefix.ptr);
}
int32_t driver_c_str_contains_char(const char *s, int32_t value) {
  const char *safe = cheng_safe_cstr((const char *)s);
  size_t n = strlen(safe);
  unsigned char target = (unsigned char)(value & 0xff);
  for (size_t i = 0; i < n; ++i) {
    if ((unsigned char)safe[i] == target) return 1;
  }
  return 0;
}
int32_t driver_c_chr_i32(int32_t value) {
  return (int32_t)((unsigned char)(value & 0xff));
}
int32_t driver_c_ord_char(int32_t value) {
  return (int32_t)((unsigned char)(value & 0xff));
}
int32_t driver_c_chr_i32_compat(int32_t value) {
  return driver_c_chr_i32(value);
}
int32_t driver_c_ord_char_compat(int32_t value) {
  return driver_c_ord_char(value);
}
CHENG_SHIM_WEAK int32_t driver_c_str_contains_char_bridge(ChengStrBridge s, int32_t value) {
  return driver_c_str_contains_char(s.ptr, value);
}
int32_t driver_c_str_contains_str(const char *s, const char *sub) {
  const char *sa = cheng_safe_cstr((const char *)s);
  const char *sb = cheng_safe_cstr((const char *)sub);
  size_t la = strlen(sa);
  size_t lb = strlen(sb);
  if (lb == 0) return 1;
  if (lb > la) return 0;
  for (size_t i = 0; i + lb <= la; ++i) {
    if (memcmp(sa + i, sb, lb) == 0) return 1;
  }
  return 0;
}
CHENG_SHIM_WEAK int32_t driver_c_str_contains_str_bridge(ChengStrBridge s, ChengStrBridge sub) {
  return driver_c_str_contains_str(s.ptr, sub.ptr);
}
CHENG_SHIM_WEAK bool cheng_str_is_empty(const char *s) {
  return cheng_strlen((char *)s) == 0;
}
CHENG_SHIM_WEAK bool cheng_str_nonempty(const char *s) {
  return cheng_strlen((char *)s) != 0;
}
CHENG_SHIM_WEAK bool cheng_str_has_prefix_bool(const char *s, const char *prefix) {
  return driver_c_str_has_prefix(s, prefix) != 0;
}
CHENG_SHIM_WEAK bool cheng_str_contains_char_bool(const char *s, int32_t value) {
  return driver_c_str_contains_char(s, value) != 0;
}
CHENG_SHIM_WEAK bool cheng_str_contains_str_bool(const char *s, const char *sub) {
  return driver_c_str_contains_str(s, sub) != 0;
}
CHENG_SHIM_WEAK char *cheng_str_drop_prefix(const char *s, const char *prefix) {
  if (!cheng_str_has_prefix_bool(s, prefix)) return driver_c_str_clone(s);
  return driver_c_str_slice(s, cheng_strlen((char *)prefix), cheng_strlen((char *)s));
}
CHENG_SHIM_WEAK char *cheng_str_slice_bytes(const char *text, int32_t start, int32_t count) {
  return driver_c_str_slice(text, start, count);
}
CHENG_SHIM_WEAK void cheng_str_arc_retain(const char *s) {
  if (s != NULL) cheng_mem_retain((void *)s);
}
CHENG_SHIM_WEAK void cheng_str_arc_release(const char *s) {
  if (s != NULL) cheng_mem_release((void *)s);
}
CHENG_SHIM_WEAK char *cheng_str_from_bytes_compat(ChengSeqHeader data, int32_t len) {
  const char *raw = (const char *)data.buffer;
  if (raw == NULL || len <= 0 || data.len < len) return cheng_copy_string_bytes("", 0u);
  return cheng_copy_string_bytes(raw, (size_t)len);
}
CHENG_SHIM_WEAK char *cheng_char_to_str_compat(int32_t value) {
  return driver_c_char_to_str(value);
}
CHENG_SHIM_WEAK char *cheng_slice_string_compat(const char *s, int32_t start, int32_t stop, bool exclusive) {
  int32_t s0 = start < 0 ? 0 : start;
  int32_t e0 = exclusive ? (stop - 1) : stop;
  int32_t n;
  int32_t e;
  int32_t count;
  if (e0 < s0) return cheng_copy_string_bytes("", 0u);
  n = cheng_strlen((char *)s);
  if (s0 >= n) return cheng_copy_string_bytes("", 0u);
  e = e0 >= n ? (n - 1) : e0;
  count = e - s0 + 1;
  return driver_c_str_slice(s, s0, count);
}
CHENG_SHIM_WEAK bool cheng_str_contains_compat(const char *text, const char *sub) {
  return driver_c_str_contains_str(text, sub) != 0;
}
CHENG_SHIM_WEAK int32_t driver_c_str_eq_bridge(ChengStrBridge a, ChengStrBridge b) {
  if (a.len != b.len) return 0;
  if (a.len <= 0) return 1;
  if (a.ptr == NULL || b.ptr == NULL) return 0;
  return memcmp(a.ptr, b.ptr, (size_t)a.len) == 0 ? 1 : 0;
}
char *driver_c_i32_to_str(int32_t value) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%d", value);
  if (n < 0) n = 0;
  return cheng_copy_string_bytes(buf, (size_t)n);
}
char *driver_c_i64_to_str(int64_t value) {
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "%lld", (long long)value);
  if (n < 0) n = 0;
  return cheng_copy_string_bytes(buf, (size_t)n);
}
char *driver_c_u64_to_str(uint64_t value) {
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  if (n < 0) n = 0;
  return cheng_copy_string_bytes(buf, (size_t)n);
}

void c_iometer_call(void *hook, int32_t op, int64_t bytes) {
  (void)hook;
  (void)op;
  (void)bytes;
}

void cheng_iometer_call(void *hook, int32_t op, int64_t bytes) {
  (void)hook;
  (void)op;
  (void)bytes;
}

void *openImpl(char *filename, int32_t mode) {
  const char *path = filename != NULL ? filename : "";
  const char *openMode = "rb";
  if (mode == 1) {
    openMode = "wb";
  } else if (mode != 0) {
    openMode = "rb+";
  }
  return fopen(path, openMode);
}

uint64_t processOptionMask(int32_t opt) {
  if (opt < 0 || opt >= 63) return 0;
  return ((uint64_t)1) << (uint64_t)opt;
}

char *sliceStr(char *text, int32_t start, int32_t stop) {
  if (text == NULL) return (char *)"";
  int32_t n = cheng_strlen(text);
  if (n <= 0) return (char *)"";
  int32_t a = start < 0 ? 0 : start;
  int32_t b = stop;
  if (b >= n) b = n - 1;
  if (b < a) return (char *)"";
  int32_t span = b - a + 1;
  char *out = (char *)malloc((size_t)span + 1u);
  if (out == NULL) return (char *)"";
  memcpy(out, text + a, (size_t)span);
  out[span] = '\0';
  return out;
}

int32_t cheng_dir_exists(const char *path) {
  if (path == NULL || path[0] == '\0') return 0;
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

int32_t cheng_mkdir1(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
#if defined(_WIN32)
  return _mkdir(path);
#else
  return mkdir(path, 0755);
#endif
}

char *cheng_getcwd(void) {
  static char buf[4096];
  if (getcwd(buf, sizeof(buf)) == NULL) {
    buf[0] = '\0';
  }
  size_t n = strlen(buf);
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, buf, n);
  out[n] = '\0';
  return out;
}

void __cheng_setCmdLine(int32_t argc, const char **argv) {
  if (argc <= 0 || argc > 4096 || argv == NULL) {
    cheng_saved_argc = 0;
    cheng_saved_argv = NULL;
    return;
  }
  cheng_saved_argc = argc;
  cheng_saved_argv = argv;
}

int32_t __cheng_rt_paramCount(void) {
  if (cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL) {
    return cheng_saved_argc;
  }
#if defined(__APPLE__)
  int *argc_ptr = _NSGetArgc();
  if (argc_ptr == NULL) return 0;
  int argc = *argc_ptr;
  if (argc <= 0 || argc > 4096) return 0;
  return (int32_t)argc;
#else
  return 0;
#endif
}

int32_t __cmdCountFromRuntime(void) {
  int32_t argc = __cheng_rt_paramCount();
  if (argc <= 0 || argc > 4096) return 0;
  return argc;
}

int32_t cmdCountFromRuntime(void) {
  return __cmdCountFromRuntime();
}

const char * __cheng_rt_paramStr(int32_t i) {
  if (i >= 0 && cheng_saved_argc > 0 && cheng_saved_argc <= 4096 &&
      cheng_saved_argv != NULL && i < cheng_saved_argc) {
    const char *s = cheng_saved_argv[i];
    return s != NULL ? s : "";
  }
#if defined(__APPLE__)
  if (i < 0) return "";
  int *argc_ptr = _NSGetArgc();
  char ***argv_ptr = _NSGetArgv();
  if (argc_ptr == NULL || argv_ptr == NULL || *argv_ptr == NULL) return "";
  int argc = *argc_ptr;
  if (argc <= 0 || argc > 4096 || i >= argc) return "";
  char *s = (*argv_ptr)[i];
  return s != NULL ? s : "";
#else
  (void)i;
  return "";
#endif
}

char * __cheng_rt_paramStrCopy(int32_t i) {
  const char *s = __cheng_rt_paramStr(i);
  if (s == NULL) s = "";
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

CHENG_SHIM_WEAK ChengStrBridge __cheng_rt_paramStrCopyBridge(int32_t i) {
  return cheng_str_bridge_from_owned(__cheng_rt_paramStrCopy(i));
}

CHENG_SHIM_WEAK void __cheng_rt_paramStrCopyBridgeInto(int32_t i, ChengStrBridge *out) {
  if (out == NULL) return;
  *out = __cheng_rt_paramStrCopyBridge(i);
}

char * __cheng_rt_programBaseNameCopy(void) {
  const char *s = __cheng_rt_paramStr(0);
  if (s == NULL) s = "";
  const char *base = s;
  for (const char *p = s; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  size_t n = strlen(base);
  if (n > 3 && base[n - 3] == '.' && base[n - 2] == 's' && base[n - 1] == 'h') {
    n -= 3;
  }
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, base, n);
  out[n] = '\0';
  return out;
}

void *cheng_seq_get(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  if (!buffer || idx < 0 || idx >= len || elem_size <= 0) return NULL;
  return (void *)((char *)buffer + ((size_t)idx * (size_t)elem_size));
}

void *cheng_seq_set(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_get(buffer, len, idx, elem_size);
}

void *cheng_ptr_seq_get_value(ChengSeqHeader seq, int32_t idx) {
  void *slot = cheng_seq_get(seq.buffer, seq.len, idx, (int32_t)sizeof(void *));
  void *out = NULL;
  if (!slot) return NULL;
  memcpy(&out, slot, sizeof(void *));
  return out;
}

void *cheng_ptr_seq_get_compat(ChengSeqHeader seq, int32_t idx) {
  return cheng_ptr_seq_get_value(seq, idx);
}

void cheng_ptr_seq_set_compat(ChengSeqHeader *seq_ptr, int32_t idx, void *value) {
  if (!seq_ptr) return;
  void *slot = cheng_seq_set(seq_ptr->buffer, seq_ptr->len, idx, (int32_t)sizeof(void *));
  if (!slot) return;
  memcpy(slot, &value, sizeof(void *));
}

void cheng_error_info_init_compat(ChengErrorInfoCompat *out, int32_t code, char *msg) {
  if (!out) return;
  out->code = code;
  out->msg = msg;
}

int32_t cheng_error_info_code_compat(ChengErrorInfoCompat e) {
  return e.code;
}

char *cheng_error_info_msg_compat(ChengErrorInfoCompat e) {
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
  char *label;
  int32_t cond;
} ChengMachineInst;

enum {
  CHENG_MACHINE_INST_MAGIC = 0x434d494eu,
};

static char *cheng_machine_inst_dup_label(const char *s) {
  size_t len = 0u;
  char *out = NULL;
  if (s == NULL || s[0] == '\0') {
    out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_strmeta_put(out, 0);
    }
    return out;
  }
  len = strlen(s);
  out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  memcpy(out, s, len);
  out[len] = '\0';
  cheng_strmeta_put(out, (int32_t)len);
  return out;
}

static char *cheng_machine_inst_dup_label_n(const char *s, int32_t len_in) {
  char *out = NULL;
  int32_t len = len_in > 0 ? len_in : 0;
  if (s == NULL || len <= 0) {
    out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_strmeta_put(out, 0);
    }
    return out;
  }
  out = (char *)cheng_malloc(len + 1);
  if (out == NULL) return NULL;
  memcpy(out, s, (size_t)len);
  out[len] = '\0';
  cheng_strmeta_put(out, len);
  return out;
}

static ChengMachineInst *cheng_machine_inst_try(void *inst) {
  ChengMachineInst *raw = (ChengMachineInst *)inst;
  if (raw == NULL) return NULL;
  if (raw->magic != CHENG_MACHINE_INST_MAGIC) return NULL;
  return raw;
}

void *cheng_func_ptr_shadow_recover(uint64_t p) {
  uintptr_t ptr_val = (uintptr_t)p;
  if (ptr_val == 0) return NULL;
  if ((p >> 32) != 0) return (void *)ptr_val;
  uintptr_t low_key = ptr_val & UINT32_C(0xffffffff);
  if (low_key == 0) return NULL;
  uint32_t slot = cheng_func_ptr_shadow_slot(low_key);
  void *found = NULL;
  for (uint32_t scanned = 0; scanned < CHENG_FUNC_PTR_SHADOW_CAP; ++scanned) {
    uintptr_t cur_key = cheng_func_ptr_shadow_keys[slot];
    if (cur_key == 0) return found;
    if (cur_key == low_key) {
      uintptr_t cur_val = cheng_func_ptr_shadow_vals[slot];
      if (cur_val != 0) found = (void *)cur_val;
    }
    slot = (slot + 1u) & (CHENG_FUNC_PTR_SHADOW_CAP - 1u);
  }
  return found;
}

void cheng_func_ptr_shadow_remember(void *p) {
  uintptr_t ptr_val = (uintptr_t)p;
  if (ptr_val == 0) return;
  uintptr_t low_key = ptr_val & UINT32_C(0xffffffff);
  if (low_key == 0) return;
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

void *cheng_machine_inst_new(int32_t op, int32_t rd, int32_t rn, int32_t rm,
                             int32_t ra, int64_t imm, const char *label, int32_t cond) {
  ChengMachineInst *inst = (ChengMachineInst *)cheng_malloc((int32_t)sizeof(ChengMachineInst));
  if (inst == NULL) return NULL;
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
  cheng_func_ptr_shadow_remember((void *)inst);
  return (void *)inst;
}

void *cheng_machine_inst_new_n(int32_t op, int32_t rd, int32_t rn, int32_t rm,
                               int32_t ra, int64_t imm, const char *label, int32_t label_len,
                               int32_t cond) {
  ChengMachineInst *inst = (ChengMachineInst *)cheng_malloc((int32_t)sizeof(ChengMachineInst));
  if (inst == NULL) return NULL;
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
  cheng_func_ptr_shadow_remember((void *)inst);
  return (void *)inst;
}

void *cheng_machine_inst_clone(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  if (raw == NULL) return NULL;
  return cheng_machine_inst_new(raw->op, raw->rd, raw->rn, raw->rm,
                                raw->ra, raw->imm, raw->label, raw->cond);
}

int32_t cheng_machine_inst_valid(void *inst) {
  return cheng_machine_inst_try(inst) != NULL ? 1 : 0;
}

int32_t cheng_machine_inst_op(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  return raw != NULL ? raw->op : 0;
}

int32_t cheng_machine_inst_rd(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  return raw != NULL ? raw->rd : 0;
}

int32_t cheng_machine_inst_rn(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  return raw != NULL ? raw->rn : 0;
}

int32_t cheng_machine_inst_rm(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  return raw != NULL ? raw->rm : 0;
}

int32_t cheng_machine_inst_ra(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  return raw != NULL ? raw->ra : 0;
}

int64_t cheng_machine_inst_imm(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  return raw != NULL ? raw->imm : 0;
}

char *cheng_machine_inst_label(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  if (raw == NULL || raw->label == NULL) return "";
  return raw->label;
}

int32_t cheng_machine_inst_cond(void *inst) {
  ChengMachineInst *raw = cheng_machine_inst_try(inst);
  return raw != NULL ? raw->cond : 0;
}

void *cheng_seq_set_grow(void *seq_ptr, int32_t idx, int32_t elem_size) {
  if (!seq_ptr || elem_size <= 0) return cheng_seq_get(NULL, 0, idx, elem_size);
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  cheng_seq_sanitize(seq);
  if (idx < 0) return cheng_seq_get(seq->buffer, seq->len, idx, elem_size);
  int32_t need = idx + 1;
  if (need > seq->cap || seq->buffer == NULL) {
    int32_t new_cap = cheng_next_cap(seq->cap, need);
    if (new_cap <= 0) return cheng_seq_get(seq->buffer, seq->len, idx, elem_size);
    size_t old_bytes = (size_t)(seq->cap > 0 ? seq->cap : 0) * (size_t)elem_size;
    size_t new_bytes = (size_t)new_cap * (size_t)elem_size;
    void *new_buf = c_realloc(seq->buffer, (int64_t)new_bytes);
    if (!new_buf) return cheng_seq_get(seq->buffer, seq->len, idx, elem_size);
    if (new_bytes > old_bytes) {
      memset((char *)new_buf + old_bytes, 0, new_bytes - old_bytes);
    }
    seq->buffer = new_buf;
    seq->cap = new_cap;
  }
  if (need > seq->len) seq->len = need;
  return cheng_seq_get(seq->buffer, seq->len, idx, elem_size);
}

void reserve(void *seq, int32_t new_cap) {
  if (!seq || new_cap <= 0) return;
  ChengSeqHeader *hdr = (ChengSeqHeader *)seq;
  cheng_seq_sanitize(hdr);
  if (hdr->buffer && new_cap <= hdr->cap) return;
  int32_t target_cap = cheng_next_cap(hdr->cap, new_cap);
  if (target_cap <= 0) return;
  const size_t slot_bytes = sizeof(void *) < 32u ? 32u : sizeof(void *);
  const size_t old_bytes = (size_t)(hdr->cap > 0 ? hdr->cap : 0) * slot_bytes;
  const size_t new_bytes = (size_t)target_cap * slot_bytes;
  void *new_buf = c_realloc(hdr->buffer, (int64_t)new_bytes);
  if (!new_buf) return;
  if (new_bytes > old_bytes) {
    memset((char *)new_buf + old_bytes, 0, new_bytes - old_bytes);
  }
  hdr->buffer = new_buf;
  hdr->cap = target_cap;
}

void reserve_ptr_void(void *seq, int32_t new_cap) {
  reserve(seq, new_cap);
}

void setLen(void *seq, int32_t new_len) {
  if (!seq) return;
  ChengSeqHeader *hdr = (ChengSeqHeader *)seq;
  cheng_seq_sanitize(hdr);
  int32_t target = new_len < 0 ? 0 : new_len;
  if (target > hdr->cap) reserve(seq, target);
  hdr->len = target;
}

void *cheng_slice_get(void *ptr, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_get(ptr, len, idx, elem_size);
}

void *cheng_slice_set(void *ptr, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_set(ptr, len, idx, elem_size);
}

int64_t cheng_f32_bits_to_i64(int32_t bits) {
  union {
    uint32_t u32;
    float f32;
  } v;
  v.u32 = (uint32_t)bits;
  return (int64_t)v.f32;
}

int64_t cheng_f64_bits_to_i64(int64_t bits) {
  union {
    uint64_t u64;
    double f64;
  } v;
  v.u64 = (uint64_t)bits;
  return (int64_t)v.f64;
}

int32_t cheng_file_exists(const char *path) {
  if (path == NULL || path[0] == '\0') return 0;
  struct stat st;
  return stat(path, &st) == 0 ? 1 : 0;
}

int64_t cheng_file_mtime(const char *path) {
  if (path == NULL || path[0] == '\0') return 0;
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  return (int64_t)st.st_mtime;
}

int64_t cheng_file_size(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  return (int64_t)st.st_size;
}

int32_t cheng_jpeg_decode(const uint8_t *data, int32_t len, int32_t *out_w, int32_t *out_h, uint8_t **out_rgba) {
  (void)data;
  (void)len;
  if (out_w) *out_w = 0;
  if (out_h) *out_h = 0;
  if (out_rgba) *out_rgba = NULL;
  return 0;
}

void cheng_jpeg_free(void *p) {
  free(p);
}

int32_t libc_socket(int32_t domain, int32_t typ, int32_t protocol) {
  return socket(domain, typ, protocol);
}

int32_t libc_bind(int32_t fd, void *addr, int32_t len) {
  return bind(fd, (const struct sockaddr *)addr, (socklen_t)len);
}

int32_t libc_sendto(int32_t fd, void *buf, int32_t len, int32_t flags, void *addr, int32_t addrlen) {
  return (int32_t)sendto(fd, buf, (size_t)len, flags, (const struct sockaddr *)addr, (socklen_t)addrlen);
}

int32_t libc_send(int32_t fd, void *buf, int32_t len, int32_t flags) {
  return (int32_t)send(fd, buf, (size_t)len, flags);
}

int32_t libc_recvfrom(int32_t fd, void *buf, int32_t len, int32_t flags, void *addr, int32_t *addrlen) {
  socklen_t al = (addrlen && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
  int32_t rc = (int32_t)recvfrom(fd, buf, (size_t)len, flags, (struct sockaddr *)addr, addrlen ? &al : NULL);
  if (addrlen) *addrlen = (int32_t)al;
  return rc;
}

int32_t libc_recv(int32_t fd, void *buf, int32_t len, int32_t flags) {
  return (int32_t)recv(fd, buf, (size_t)len, flags);
}

int32_t libc_sendmsg(int32_t fd, void *msg, int32_t flags) {
  return (int32_t)sendmsg(fd, (const struct msghdr *)msg, flags);
}

int32_t libc_recvmsg(int32_t fd, void *msg, int32_t flags) {
  return (int32_t)recvmsg(fd, (struct msghdr *)msg, flags);
}

int32_t libc_getsockname(int32_t fd, void *addr, int32_t *addrlen) {
  socklen_t al = (addrlen && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
  int32_t rc = getsockname(fd, (struct sockaddr *)addr, addrlen ? &al : NULL);
  if (addrlen) *addrlen = (int32_t)al;
  return rc;
}

int32_t libc_getpeername(int32_t fd, void *addr, int32_t *addrlen) {
  socklen_t al = (addrlen && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
  int32_t rc = getpeername(fd, (struct sockaddr *)addr, addrlen ? &al : NULL);
  if (addrlen) *addrlen = (int32_t)al;
  return rc;
}

int32_t libc_listen(int32_t fd, int32_t backlog) { return listen(fd, backlog); }

int32_t libc_connect(int32_t fd, void *addr, int32_t len) {
  return connect(fd, (const struct sockaddr *)addr, (socklen_t)len);
}

int32_t libc_accept(int32_t fd, void *addr, int32_t *addrlen) {
  socklen_t al = (addrlen && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
  int32_t rc = accept(fd, (struct sockaddr *)addr, addrlen ? &al : NULL);
  if (addrlen) *addrlen = (int32_t)al;
  return rc;
}

int32_t libc_getsockopt(int32_t fd, int32_t level, int32_t optname, void *optval, int32_t *optlen) {
  socklen_t ol = (optlen && *optlen > 0) ? (socklen_t)(*optlen) : (socklen_t)0;
  int32_t rc = getsockopt(fd, level, optname, optval, optlen ? &ol : NULL);
  if (optlen) *optlen = (int32_t)ol;
  return rc;
}

int32_t libc_setsockopt(int32_t fd, int32_t level, int32_t optname, void *optval, int32_t optlen) {
  return setsockopt(fd, level, optname, optval, (socklen_t)optlen);
}

int32_t libc_socketpair(int32_t domain, int32_t typ, int32_t protocol, int32_t *sv) {
  return socketpair(domain, typ, protocol, sv);
}

int32_t libc_close(int32_t fd) { return close(fd); }

int32_t libc_shutdown(int32_t fd, int32_t how) { return shutdown(fd, how); }

char *libc_strerror(int32_t err) { return strerror(err); }

int32_t *libc_errno_ptr(void) {
#if defined(__APPLE__)
  return __error();
#else
  return &errno;
#endif
}

static FILE *cheng_safe_stream(void *stream) {
  if (stream == (void *)stdout || stream == (void *)stderr || stream == (void *)stdin) {
    return (FILE *)stream;
  }
  if (stream == NULL) return stdout;
  uintptr_t v = (uintptr_t)stream;
  if (v < 0x100000000ull) {
    return stdout;
  }
  return (FILE *)stream;
}

void *libc_fopen(char *filename, char *mode) { return fopen(filename, mode); }

int32_t libc_fclose(void *f) {
  FILE *stream = cheng_safe_stream(f);
  if (stream == stdout || stream == stderr || stream == stdin) return 0;
  return fclose(stream);
}

int64_t libc_fread(void *ptr, int64_t size, int64_t n, void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return (int64_t)fread(ptr, (size_t)size, (size_t)n, f);
}

int64_t libc_fwrite(void *ptr, int64_t size, int64_t n, void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return (int64_t)fwrite(ptr, (size_t)size, (size_t)n, f);
}

int32_t libc_fflush(void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return fflush(f);
}

int32_t libc_fgetc(void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return fgetc(f);
}

void *libc_fdopen(int32_t fd, char *mode) { return fdopen(fd, mode); }

int32_t libc_fseeko(void *stream, int64_t offset, int32_t whence) {
  return fseeko((FILE *)stream, (off_t)offset, whence);
}

int64_t libc_ftello(void *stream) { return (int64_t)ftello((FILE *)stream); }

int32_t libc_mkdir(char *path, int32_t mode) { return mkdir(path, (mode_t)mode); }

char *libc_getcwd(char *buf, int64_t size) { return getcwd(buf, (size_t)size); }

void *libc_opendir(char *path) { return opendir(path); }

void *libc_readdir(void *dir) { return readdir((DIR *)dir); }

int32_t libc_closedir(void *dir) { return closedir((DIR *)dir); }

void *libc_popen(char *command, char *mode) { return popen(command, mode); }

int32_t libc_pclose(void *stream) { return pclose((FILE *)stream); }

int64_t libc_time(void *out) { return (int64_t)time((time_t *)out); }

int32_t libc_clock_gettime(int32_t clock_id, void *out) {
  return clock_gettime(clock_id, (struct timespec *)out);
}

int32_t libc_stat(char *path, void *out) { return stat(path, (struct stat *)out); }

double cheng_bits_to_f32(int32_t bits) {
  union {
    uint32_t u32;
    float f32;
  } v;
  v.u32 = (uint32_t)bits;
  return (double)v.f32;
}

int32_t cheng_f32_to_bits(double value) {
  union {
    uint32_t u32;
    float f32;
  } v;
  v.f32 = (float)value;
  return (int32_t)v.u32;
}

int32_t cheng_f64_bits_is_nan(int64_t bits) {
  uint64_t ubits = (uint64_t)bits;
  uint64_t exp_raw = (ubits >> 52u) & 0x7ffu;
  uint64_t frac = ubits & 0x000fffffffffffffull;
  if (exp_raw != 0x7ffu) return 0;
  return frac != 0 ? 1 : 0;
}

int32_t cheng_f64_bits_is_zero(int64_t bits) {
  uint64_t mag = ((uint64_t)bits) & 0x7fffffffffffffffull;
  return mag == 0 ? 1 : 0;
}

uint64_t cheng_f64_bits_order(int64_t bits) {
  uint64_t ubits = (uint64_t)bits;
  uint64_t sign_mask = 1ull << 63u;
  if ((ubits & sign_mask) != 0) return ~ubits;
  return ubits ^ sign_mask;
}

char *driver_c_bool_to_str(int32_t value) {
  if (value) return cheng_copy_string_bytes("true", 4u);
  return cheng_copy_string_bytes("false", 5u);
}

int32_t driver_c_bool_identity(int32_t value) {
  return value ? 1 : 0;
}

int32_t cheng_fclose(void *f) {
  FILE *stream = cheng_safe_stream(f);
  if (stream == stdout || stream == stderr || stream == stdin) return 0;
  return fclose(stream);
}

int32_t cheng_fwrite(void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || size <= 0 || n <= 0) return 0;
  FILE *f = cheng_safe_stream(stream);
  return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, f);
}

int32_t cheng_fd_write(int32_t fd, const char *data, int32_t len) {
  if (fd < 0 || data == NULL || len <= 0) return 0;
  ssize_t n = (ssize_t)syscall(SYS_write, fd, data, (size_t)len);
  if (n < 0) return -1;
  return (int32_t)n;
}

int32_t cheng_fflush(void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return fflush(f);
}

int32_t libc_open(const char *path, int32_t flags, int32_t mode) {
  if (path == NULL) return -1;
  return open(path, flags, mode);
}

int32_t cheng_open_w_trunc(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
  return creat(path, 0644);
}

int64_t libc_write(int32_t fd, void *data, int64_t n) {
  if (fd < 0 || data == NULL || n <= 0) return 0;
  ssize_t wrote = write(fd, data, (size_t)n);
  if (wrote < 0) return -1;
  return (int64_t)wrote;
}

void *get_stdin(void) {
  return (void *)stdin;
}

void *get_stdout(void) {
  return (void *)stdout;
}

void *get_stderr(void) {
  return (void *)stderr;
}

/*
 * Stage0 compatibility shim:
 * older bootstrap outputs may keep a direct unresolved call to symbol "[]=".
 * Provide a benign fallback body to keep runtime symbol closure deterministic.
 */
int64_t cheng_index_set_compat(void) __asm__("_[]=");
int64_t cheng_index_set_compat(void) { return 0; }

int64_t cheng_monotime_ns(void) {
#if defined(_WIN32)
  return (int64_t)clock();
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
#endif
}

static char *cheng_strdup_or_empty(const char *s) {
  if (s == NULL) s = "";
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

static int cheng_buf_ensure(char **buf, size_t *cap, size_t need) {
  if (buf == NULL || cap == NULL) return 0;
  if (*buf == NULL || *cap == 0) {
    size_t init_cap = 256u;
    if (init_cap < need) init_cap = need;
    *buf = (char *)malloc(init_cap);
    if (*buf == NULL) {
      *cap = 0;
      return 0;
    }
    *cap = init_cap;
    return 1;
  }
  if (need <= *cap) return 1;
  size_t next = *cap;
  while (next < need) {
    size_t doubled = next * 2u;
    if (doubled <= next) {
      next = need;
      break;
    }
    next = doubled;
  }
  char *grown = (char *)realloc(*buf, next);
  if (grown == NULL) return 0;
  *buf = grown;
  *cap = next;
  return 1;
}

static int cheng_buf_append(char **buf, size_t *cap, size_t *len, const char *src, size_t n) {
  if (len == NULL) return 0;
  size_t need = *len + n + 1u;
  if (!cheng_buf_ensure(buf, cap, need)) return 0;
  if (n > 0 && src != NULL) {
    memcpy((*buf) + *len, src, n);
    *len += n;
  }
  (*buf)[*len] = '\0';
  return 1;
}

char *cheng_list_dir(const char *path) {
  const char *dirPath = (path != NULL && path[0] != '\0') ? path : ".";
  DIR *dir = opendir(dirPath);
  if (dir == NULL) return cheng_strdup_or_empty("");
  char *out = NULL;
  size_t cap = 0;
  size_t len = 0;
  struct dirent *ent = NULL;
  while ((ent = readdir(dir)) != NULL) {
    const char *name = ent->d_name;
    if (name == NULL) continue;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    size_t n = strlen(name);
    if (!cheng_buf_append(&out, &cap, &len, name, n)) {
      closedir(dir);
      free(out);
      return cheng_strdup_or_empty("");
    }
    if (!cheng_buf_append(&out, &cap, &len, "\n", 1u)) {
      closedir(dir);
      free(out);
      return cheng_strdup_or_empty("");
    }
  }
  closedir(dir);
  if (out == NULL) return cheng_strdup_or_empty("");
  return out;
}

char *cheng_exec_cmd_ex(const char *command, const char *workingDir, int32_t mergeStderr, int64_t *exitCode) {
  if (exitCode != NULL) *exitCode = -1;
  if (command == NULL || command[0] == '\0') {
    return cheng_strdup_or_empty("");
  }

  char *shell = NULL;
  size_t shellCap = 0;
  size_t shellLen = 0;
  if (workingDir != NULL && workingDir[0] != '\0') {
    if (!cheng_buf_append(&shell, &shellCap, &shellLen, "cd ", 3u) ||
        !cheng_buf_append(&shell, &shellCap, &shellLen, "'", 1u)) {
      free(shell);
      return cheng_strdup_or_empty("");
    }
    for (size_t i = 0; workingDir[i] != '\0'; ++i) {
      const char c = workingDir[i];
      if (c == '\'') {
        if (!cheng_buf_append(&shell, &shellCap, &shellLen, "'\\''", 4u)) {
          free(shell);
          return cheng_strdup_or_empty("");
        }
      } else {
        if (!cheng_buf_append(&shell, &shellCap, &shellLen, &c, 1u)) {
          free(shell);
          return cheng_strdup_or_empty("");
        }
      }
    }
    if (!cheng_buf_append(&shell, &shellCap, &shellLen, "' && ", 6u)) {
      free(shell);
      return cheng_strdup_or_empty("");
    }
  }
  if (!cheng_buf_append(&shell, &shellCap, &shellLen, command, strlen(command))) {
    free(shell);
    return cheng_strdup_or_empty("");
  }
  if (mergeStderr != 0) {
    if (!cheng_buf_append(&shell, &shellCap, &shellLen, " 2>&1", 6u)) {
      free(shell);
      return cheng_strdup_or_empty("");
    }
  }

  FILE *fp = popen(shell, "r");
  free(shell);
  if (fp == NULL) {
    return cheng_strdup_or_empty("");
  }
  char *out = NULL;
  size_t cap = 0;
  size_t len = 0;
  char buf[4096];
  for (;;) {
    size_t n = fread(buf, 1u, sizeof(buf), fp);
    if (n > 0) {
      if (!cheng_buf_append(&out, &cap, &len, buf, n)) {
        free(out);
        out = NULL;
        break;
      }
    }
    if (n < sizeof(buf)) {
      if (feof(fp)) break;
      if (ferror(fp)) break;
    }
  }
  int status = pclose(fp);
  if (exitCode != NULL) {
#if defined(_WIN32)
    *exitCode = (int64_t)status;
#else
    if (WIFEXITED(status)) {
      *exitCode = (int64_t)WEXITSTATUS(status);
    } else {
      *exitCode = (int64_t)status;
    }
#endif
  }
  if (out == NULL) return cheng_strdup_or_empty("");
  return out;
}

void *load_ptr(void *p, int32_t off) {
  if (!p) return NULL;
  return *(void **)((char *)p + off);
}

void store_ptr(void *p, int32_t off, void *v) {
  if (!p) return;
  *(void **)((char *)p + off) = v;
}

int32_t cheng_thread_spawn(void *fn_ptr, void *ctx) {
  (void)ctx;
  return fn_ptr != NULL ? 1 : 0;
}

int32_t cheng_thread_spawn_i32(void *fn_ptr, int32_t ctx) {
  (void)fn_ptr;
  (void)ctx;
  return 0;
}

int32_t cheng_thread_parallelism(void) {
  return 1;
}

void cheng_thread_yield(void) {
}

static int32_t cheng_thread_local_i32_slots[4];

int32_t cheng_thread_local_i32_get(int32_t slot) {
  if (slot < 0 || slot >= 4) return 0;
  return cheng_thread_local_i32_slots[slot];
}

void cheng_thread_local_i32_set(int32_t slot, int32_t value) {
  if (slot < 0 || slot >= 4) return;
  cheng_thread_local_i32_slots[slot] = value;
}
