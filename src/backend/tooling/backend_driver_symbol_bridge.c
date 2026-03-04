#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime hooks provided by system_helpers.c */
extern int32_t cheng_file_exists(const char *path);
extern int64_t cheng_file_mtime(const char *path);
extern int64_t cheng_file_size(const char *path);
extern void *cheng_malloc(int32_t size);
extern void cheng_iometer_call(void *hook, int32_t op, int64_t bytes);
extern void *cheng_jpeg_decode(void *data, int32_t len, void *out_w, void *out_h);
extern void cheng_jpeg_free(void *p);
extern int32_t libc_fflush(void *stream);

typedef struct {
  void *buffer;
  int32_t len;
  int32_t cap;
} cheng_seq_i32;

typedef struct {
  void *buffer;
  int32_t len;
  int32_t cap;
} cheng_seq_any;

__attribute__((weak)) int32_t driver_puts(const char *text) {
  if (text == NULL) {
    return puts("[bridge] driver_puts: <null>") >= 0 ? 0 : -1;
  }
  return puts(text);
}

__attribute__((weak)) void driver_exit(int32_t code) { exit(code); }

__attribute__((weak)) void *driver_ptr_add(void *p, int32_t offset) {
  return (void *)((char *)p + offset);
}

__attribute__((weak)) void *ptr_add(void *p, int32_t offset) {
  return (void *)((char *)p + offset);
}

__attribute__((weak)) void *rawmemAsVoid(void *p) {
  return p;
}

void backend_driver_stage0_setindex_compat(char *s, int32_t idx, int32_t value)
    __asm__("_[]=");
__attribute__((weak)) void
backend_driver_stage0_setindex_compat(char *s, int32_t idx, int32_t value) {
  if (s == NULL || idx < 0) {
    return;
  }
  s[idx] = (char)value;
}

__attribute__((weak)) void *driver_memcpy(void *dest, void *src, int64_t n) {
  if (n <= 0) {
    return dest;
  }
  return memcpy(dest, src, (size_t)n);
}

__attribute__((weak)) void *driver_memset(void *dest, int32_t val, int64_t n) {
  if (n <= 0) {
    return dest;
  }
  return memset(dest, val, (size_t)n);
}

__attribute__((weak)) void *driver_c_alloc(int32_t size) { return cheng_malloc(size); }

__attribute__((weak)) int32_t driver_file_exists(const char *path) {
  return cheng_file_exists(path);
}

__attribute__((weak)) void c_iometer_call(void *hook, int32_t op, int64_t bytes) {
  cheng_iometer_call(hook, op, bytes);
}

__attribute__((weak)) int64_t driver_file_mtime(const char *path) {
  return cheng_file_mtime(path);
}

__attribute__((weak)) int64_t driver_file_size(const char *path) {
  return cheng_file_size(path);
}

__attribute__((weak)) int64_t driver_c_fork(void) { return (int64_t)fork(); }

__attribute__((weak)) int64_t driver_c_waitpid(int64_t pid, int32_t *status, int32_t options) {
  return (int64_t)waitpid((pid_t)pid, status, options);
}

__attribute__((weak)) double bitsToF32(int32_t bits) {
  union {
    uint32_t u;
    float f;
  } u;
  u.u = (uint32_t)bits;
  return (double)u.f;
}

__attribute__((weak)) int32_t f32ToBits(double value) {
  union {
    uint32_t u;
    float f;
  } u;
  u.f = (float)value;
  return (int32_t)u.u;
}

__attribute__((weak)) void *c_jpeg_decode(void *data, int32_t len, void *out_w, void *out_h) {
  return cheng_jpeg_decode(data, len, out_w, out_h);
}

__attribute__((weak)) void c_jpeg_free(void *p) { cheng_jpeg_free(p); }

__attribute__((weak)) char *libc_getenv(const char *name) { return getenv(name); }

__attribute__((weak)) char *driver_c_getenv(const char *name) { return getenv(name); }

__attribute__((weak)) int32_t libc_remove(const char *path) { return remove(path); }

__attribute__((weak)) int32_t libc_rename(const char *oldpath, const char *newpath) {
  return rename(oldpath, newpath);
}

/*
 * Keep self-link runtime object symbol closure stable when composed from
 * backend_mm object + bridge objects (without full selflink shim).
 */
__attribute__((weak)) uint64_t processOptionMask(int32_t opt) {
  if (opt < 0 || opt >= 64) return 0ull;
  return 1ull << (uint32_t)opt;
}

__attribute__((weak)) char *sliceStr(char *text, int32_t start, int32_t stop) {
  if (text == NULL) return NULL;
  int32_t n = (int32_t)strlen(text);
  if (start < 0) start = 0;
  if (stop < start) stop = start;
  if (start > n) start = n;
  if (stop > n) stop = n;
  int32_t m = stop - start;
  char *out = (char *)malloc((size_t)m + 1u);
  if (out == NULL) return NULL;
  if (m > 0) memcpy(out, text + start, (size_t)m);
  out[m] = '\0';
  return out;
}

__attribute__((weak)) int64_t libc_write(int32_t fd, void *data, int64_t n) {
  if (fd < 0 || data == NULL || n <= 0) return 0;
  ssize_t wrote = write(fd, data, (size_t)n);
  if (wrote < 0) return -1;
  return (int64_t)wrote;
}

__attribute__((weak)) int32_t c_fflush(void *stream) { return libc_fflush(stream); }

/*
 * Stable raw byte writer for backend_driver object emission. This bypasses
 * stage0/stage2 variations in std/os write helpers and writes through libc.
 * Returns 1 on success, 0 on failure.
 */
__attribute__((weak)) int32_t backend_driver_write_file_bytes(const char *path,
                                                              const void *data,
                                                              int64_t len) {
  if (path == NULL || path[0] == '\0' || len < 0) {
    return 0;
  }
  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    return 0;
  }
  if (data != NULL && len > 0) {
    size_t want = (size_t)len;
    size_t wrote = fwrite(data, 1u, want, f);
    if (wrote != want) {
      fclose(f);
      return 0;
    }
  }
  fflush(f);
  if (fclose(f) != 0) {
    return 0;
  }
  return 1;
}

__attribute__((weak)) char *lower_c_getenv(const char *name) { return getenv(name); }

__attribute__((weak)) int32_t linkerCore_file_exists(const char *path) {
  return cheng_file_exists(path);
}

__attribute__((weak)) void machoSeqInitEmpty_int32(cheng_seq_i32 *items) {
  if (items == NULL) {
    return;
  }
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_MachoX64Reloc(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_MachoX64Sym(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_str(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_uint64(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_uint8(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
