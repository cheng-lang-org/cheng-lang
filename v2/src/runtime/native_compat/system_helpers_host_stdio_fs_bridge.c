#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

__attribute__((weak)) void *cheng_fopen(const char *filename, const char *mode) {
  if (filename == NULL || mode == NULL) return NULL;
  return (void *)fopen(filename, mode);
}

__attribute__((weak)) void *get_stdin(void) { return (void *)stdin; }

__attribute__((weak)) void *get_stdout(void) { return (void *)stdout; }

__attribute__((weak)) void *get_stderr(void) { return (void *)stderr; }

__attribute__((weak)) int32_t cheng_fclose(void *f) {
  if (f == NULL) return -1;
  return (int32_t)fclose((FILE *)f);
}

__attribute__((weak)) int32_t cheng_fread(void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || stream == NULL || size <= 0 || n <= 0) return 0;
  return (int32_t)fread(ptr, (size_t)size, (size_t)n, (FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fwrite(const void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || stream == NULL || size <= 0 || n <= 0) return 0;
  return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, (FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fseek(void *stream, int64_t offset, int32_t whence) {
  if (stream == NULL) return -1;
  return (int32_t)fseeko((FILE *)stream, (off_t)offset, whence);
}

__attribute__((weak)) int64_t cheng_ftell(void *stream) {
  if (stream == NULL) return -1;
  return (int64_t)ftello((FILE *)stream);
}

__attribute__((weak)) int32_t cheng_system_entropy_fill(void *dst, int32_t len) {
  if (dst == NULL || len <= 0) return 0;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  arc4random_buf(dst, (size_t)len);
  return 1;
#elif defined(__linux__)
  uint8_t *cursor = (uint8_t *)dst;
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

__attribute__((weak)) int32_t cheng_fflush(void *stream) {
  if (stream == NULL) return -1;
  return (int32_t)fflush((FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fgetc(void *stream) {
  if (stream == NULL) return -1;
  return (int32_t)fgetc((FILE *)stream);
}

__attribute__((weak)) int32_t cheng_file_exists(const char *path) {
  struct stat st;
  if (path == NULL) return 0;
  return stat(path, &st) == 0 ? 1 : 0;
}

__attribute__((weak)) int64_t cheng_file_size(const char *path) {
  struct stat st;
  if (path == NULL) return -1;
  if (stat(path, &st) != 0) return -1;
  return (int64_t)st.st_size;
}

__attribute__((weak)) int32_t cheng_dir_exists(const char *path) {
  struct stat st;
  if (path == NULL) return 0;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

__attribute__((weak)) int32_t cheng_mkdir1(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
  return mkdir(path, 0755);
}

__attribute__((weak)) int64_t cheng_file_mtime(const char *path) {
  struct stat st;
  if (path == NULL) return -1;
  if (stat(path, &st) != 0) return -1;
  return (int64_t)st.st_mtime;
}

__attribute__((weak)) char *cheng_getcwd(void) {
  static char buf[4096];
  if (getcwd(buf, sizeof(buf)) == NULL) return "";
  buf[sizeof(buf) - 1] = 0;
  return buf;
}

__attribute__((weak)) int32_t cheng_rawbytes_get_at(void *base, int32_t idx) {
  if (base == NULL || idx < 0) return 0;
  return (int32_t)((uint8_t *)base)[idx];
}

__attribute__((weak)) void cheng_rawbytes_set_at(void *base, int32_t idx, int32_t value) {
  if (base == NULL || idx < 0) return;
  ((uint8_t *)base)[idx] = (uint8_t)value;
}

__attribute__((weak)) char *driver_c_read_file_all(const char *path) {
  FILE *f = NULL;
  char *out = NULL;
  long size_long = 0;
  size_t read_count = 0;
  if (path == NULL || path[0] == '\0') {
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = 0;
    return out;
  }
  f = fopen(path, "rb");
  if (f == NULL) {
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = 0;
    return out;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = 0;
    return out;
  }
  size_long = ftell(f);
  if (size_long < 0) {
    fclose(f);
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = 0;
    return out;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = 0;
    return out;
  }
  out = (char *)malloc((size_t)size_long + 1u);
  if (out == NULL) {
    fclose(f);
    return NULL;
  }
  if (size_long > 0) {
    read_count = fread(out, 1, (size_t)size_long, f);
    if (read_count != (size_t)size_long && ferror(f)) {
      fclose(f);
      free(out);
      out = (char *)malloc(1u);
      if (out != NULL) out[0] = 0;
      return out;
    }
  }
  out[read_count] = 0;
  fclose(f);
  return out;
}
