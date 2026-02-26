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
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
extern int *__error(void);
#endif

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

void c_exit(int32_t code) { exit(code); }

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

int32_t c_memcmp(void *a, void *b, int64_t n) {
  if (!a || !b || n <= 0) return 0;
  return memcmp(a, b, (size_t)n);
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

void *cheng_seq_get(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  if (!buffer || idx < 0 || idx >= len || elem_size <= 0) return NULL;
  return (void *)((char *)buffer + ((size_t)idx * (size_t)elem_size));
}

void *cheng_seq_set(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_get(buffer, len, idx, elem_size);
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

void *libc_fopen(char *filename, char *mode) { return fopen(filename, mode); }

int32_t libc_fclose(void *f) { return fclose((FILE *)f); }

int64_t libc_fread(void *ptr, int64_t size, int64_t n, void *stream) {
  return (int64_t)fread(ptr, (size_t)size, (size_t)n, (FILE *)stream);
}

int64_t libc_fwrite(void *ptr, int64_t size, int64_t n, void *stream) {
  return (int64_t)fwrite(ptr, (size_t)size, (size_t)n, (FILE *)stream);
}

int32_t libc_fflush(void *stream) { return fflush((FILE *)stream); }

int32_t libc_fgetc(void *stream) { return fgetc((FILE *)stream); }

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

void *load_ptr(void *p, int32_t off) {
  if (!p) return NULL;
  return *(void **)((char *)p + off);
}

void store_ptr(void *p, int32_t off, void *v) {
  if (!p) return;
  *(void **)((char *)p + off) = v;
}
