#include <stdint.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <unistd.h>

__attribute__((weak)) int32_t cheng_fd_write(int32_t fd, const char* data, int32_t len) {
  if (fd < 0 || data == NULL || len <= 0) return 0;
  ssize_t n = (ssize_t)syscall(SYS_write, fd, data, (size_t)len);
  if (n < 0) return -1;
  return (int32_t)n;
}
