#include <dirent.h>
#include <errno.h>
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
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#endif

#if defined(__APPLE__)
extern int *__error(void);
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_SHIM_WEAK __attribute__((weak))
#else
#define CHENG_SHIM_WEAK
#endif

static int32_t cheng_saved_argc = 0;
static const char **cheng_saved_argv = NULL;
int32_t cheng_strlen(char *s);
typedef struct ChengSeqHeader {
  int32_t len;
  int32_t cap;
  void *buffer;
} ChengSeqHeader;

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

void cheng_bytes_copy(void *dst, void *src, int64_t n) {
  if (!dst || !src || n <= 0) return;
  memcpy(dst, src, (size_t)n);
}

void cheng_bytes_set(void *dst, int32_t value, int64_t n) {
  if (!dst || n <= 0) return;
  memset(dst, value, (size_t)n);
}

int32_t cheng_strlen(char *s) {
  if (!s) return 0;
  size_t n = strlen(s);
  return (n > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)n;
}

int32_t cheng_strcmp(char *a, char *b) { return strcmp(a ? a : "", b ? b : ""); }

int32_t c_strcmp(char *a, char *b) {
  return cheng_strcmp(a, b);
}

char *cheng_str_concat(char *a, char *b) {
  const char *sa = a ? a : "";
  const char *sb = b ? b : "";
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
  return out;
}

char *__cheng_str_concat(char *a, char *b) { return cheng_str_concat(a, b); }

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

void *cheng_seq_get(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  if (!buffer || idx < 0 || idx >= len || elem_size <= 0) return NULL;
  return (void *)((char *)buffer + ((size_t)idx * (size_t)elem_size));
}

void *cheng_seq_set(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_get(buffer, len, idx, elem_size);
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
