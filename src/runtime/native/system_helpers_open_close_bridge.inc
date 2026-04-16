#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

__attribute__((weak)) int32_t libc_open(const char *path, int32_t flags, int32_t mode) {
  if (path == NULL) return -1;
  return open(path, flags, mode);
}

__attribute__((weak)) int32_t libc_close(int32_t fd) {
  if (fd < 0) return -1;
  return close(fd);
}

__attribute__((weak)) int32_t cheng_open_w_trunc(const char *path) {
  if (path == NULL) return -1;
  return creat(path, 0644);
}
