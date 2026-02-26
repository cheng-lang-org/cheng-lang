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
    return fputs("[bridge] driver_puts: <null>\n", stderr) >= 0 ? 0 : -1;
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

__attribute__((weak)) int32_t c_fflush(void *stream) { return libc_fflush(stream); }

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
