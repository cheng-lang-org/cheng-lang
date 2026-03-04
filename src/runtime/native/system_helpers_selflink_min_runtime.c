#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif

typedef struct ChengSeqHeader {
  int32_t len;
  int32_t cap;
  void *buffer;
} ChengSeqHeader;

static int32_t cheng_saved_argc = 0;
static const char **cheng_saved_argv = NULL;

/* Forward declarations used before definitions in this minimal runtime TU. */
int32_t cheng_strlen(char *s);

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

static void cheng_seq_sanitize(ChengSeqHeader *hdr) {
  if (hdr == NULL) return;
  if (hdr->cap < 0 || hdr->cap > (1 << 27) || hdr->len < 0 || hdr->len > hdr->cap) {
    hdr->len = 0;
    hdr->cap = 0;
    hdr->buffer = NULL;
    return;
  }
  if (hdr->cap == 0) hdr->buffer = NULL;
  if (hdr->buffer == NULL && hdr->len != 0) hdr->len = 0;
  if (hdr->buffer != NULL) {
    uintptr_t p = (uintptr_t)hdr->buffer;
    if (p < 4096u || p > 0x0000FFFFFFFFFFFFull) {
      hdr->len = 0;
      hdr->cap = 0;
      hdr->buffer = NULL;
    }
  }
}

void *cheng_malloc(int32_t size) {
  int64_t bytes = size <= 0 ? 1 : (int64_t)size;
  return malloc((size_t)bytes);
}

void *alloc(int32_t size) {
  return cheng_malloc(size);
}

void cheng_free(void *p) {
  free(p);
}

void dealloc(void *p) {
  cheng_free(p);
}

void *cheng_realloc(void *p, int32_t size) {
  int64_t bytes = size <= 0 ? 1 : (int64_t)size;
  return realloc(p, (size_t)bytes);
}

void *cheng_memcpy(void *dest, void *src, int64_t n) {
  if (!dest || !src || n <= 0) return dest;
  return memcpy(dest, src, (size_t)n);
}

void *cheng_memset(void *dest, int32_t val, int64_t n) {
  if (!dest || n <= 0) return dest;
  return memset(dest, val, (size_t)n);
}

void cheng_mem_retain(void *p) {
  (void)p;
}

void cheng_mem_release(void *p) {
  (void)p;
}

void memRetain(void *p) { cheng_mem_retain(p); }
void memRelease(void *p) { cheng_mem_release(p); }

void *cheng_seq_get(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  if (!buffer || idx < 0 || idx >= len || elem_size <= 0) return NULL;
  return (void *)((char *)buffer + ((size_t)idx * (size_t)elem_size));
}

void *cheng_seq_set(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_get(buffer, len, idx, elem_size);
}

int32_t cheng_strcmp(char *a, char *b) {
  // Keep runtime object self-link friendly on Mach-O: avoid cstring literals
  // that may introduce non-extern relocations in this minimal runtime path.
  if (a == NULL && b == NULL) return 0;
  if (a == NULL) return -1;
  if (b == NULL) return 1;
  return strcmp(a, b);
}

int32_t c_strcmp(char *a, char *b) {
  return cheng_strcmp(a, b);
}

void c_iometer_call(void *hook, int32_t op, int64_t bytes) {
  (void)hook;
  (void)op;
  (void)bytes;
}

int32_t c_puts(char *text) {
  return puts(text ? text : "");
}

char *c_getenv(char *name) {
  if (name == NULL) return NULL;
  return getenv(name);
}

int32_t libc_remove(char *path) {
  if (path == NULL) return -1;
  return remove(path);
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
  char *out = (char *)cheng_malloc(span + 1);
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
  char *out = (char *)cheng_malloc((int32_t)n + 1);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, buf, n);
  out[n] = '\0';
  return out;
}

int32_t cheng_strlen(char *s) {
  if (!s) return 0;
  size_t n = strlen(s);
  return n > (size_t)INT32_MAX ? INT32_MAX : (int32_t)n;
}

char *cheng_str_concat(char *a, char *b) {
  const char *sa = a ? a : "";
  const char *sb = b ? b : "";
  size_t la = strlen(sa);
  size_t lb = strlen(sb);
  size_t total = la + lb;
  if (total > (size_t)INT32_MAX - 1) {
    total = (size_t)INT32_MAX - 1;
  }
  char *out = (char *)cheng_malloc((int32_t)total + 1);
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
  return out;
}

char *__cheng_str_concat(char *a, char *b) { return cheng_str_concat(a, b); }

void *load_ptr(void *p, int32_t off) {
  if (!p) return NULL;
  return *(void **)((char *)p + off);
}

void store_ptr(void *p, int32_t off, void *v) {
  if (!p) return;
  *(void **)((char *)p + off) = v;
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
  void *new_buf = realloc(hdr->buffer, new_bytes);
  if (!new_buf) return;
  if (new_bytes > old_bytes) {
    memset((char *)new_buf + old_bytes, 0, new_bytes - old_bytes);
  }
  hdr->buffer = new_buf;
  hdr->cap = target_cap;
}

void setLen(void *seq, int32_t new_len) {
  if (!seq) return;
  ChengSeqHeader *hdr = (ChengSeqHeader *)seq;
  cheng_seq_sanitize(hdr);
  int32_t target = new_len < 0 ? 0 : new_len;
  if (target > hdr->cap) reserve(seq, target);
  hdr->len = target;
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

void *cheng_fopen(const char *filename, const char *mode) {
  return (void *)fopen(filename, mode);
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

int32_t cheng_fclose(void *f) {
  FILE *stream = cheng_safe_stream(f);
  if (stream == stdout || stream == stderr || stream == stdin) {
    return 0;
  }
  return fclose(stream);
}

int32_t cheng_fread(void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || size <= 0 || n <= 0) return 0;
  FILE *f = cheng_safe_stream(stream);
  return (int32_t)fread(ptr, (size_t)size, (size_t)n, f);
}

int32_t cheng_fwrite(void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || size <= 0 || n <= 0) return 0;
  FILE *f = cheng_safe_stream(stream);
  return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, f);
}

int32_t cheng_fflush(void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return fflush(f);
}

int32_t cheng_fgetc(void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return fgetc(f);
}

int32_t cheng_fd_write(int32_t fd, const char *data, int32_t len) {
  if (fd < 0 || data == NULL || len <= 0) return 0;
  ssize_t n = (ssize_t)syscall(SYS_write, fd, data, (size_t)len);
  if (n < 0) return -1;
  return (int32_t)n;
}

int32_t libc_open(const char *path, int32_t flags, int32_t mode) {
  if (path == NULL) return -1;
  return open(path, flags, mode);
}

int32_t cheng_open_w_trunc(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
  return creat(path, 0644);
}

int32_t libc_close(int32_t fd) {
  if (fd < 0) return -1;
  return close(fd);
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

void cheng_iometer_call(void *hook, int32_t op, int64_t bytes) {
  (void)hook;
  (void)op;
  (void)bytes;
}
