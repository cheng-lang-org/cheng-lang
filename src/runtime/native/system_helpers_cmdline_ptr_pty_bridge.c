#include <stdint.h>
#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_WEAK __attribute__((weak))
#else
#define CHENG_WEAK
#endif

int32_t __cheng_rt_paramCount(void);

CHENG_WEAK int32_t paramCount(void) {
  int32_t argc = __cheng_rt_paramCount();
  if (argc <= 0) {
    return 0;
  }
  return argc - 1;
}

CHENG_WEAK int32_t cheng_ptr_size(void) {
  return (int32_t)sizeof(void *);
}

CHENG_WEAK int32_t cheng_pty_is_supported(void) {
#if defined(_WIN32)
  return 0;
#else
  return 1;
#endif
}

CHENG_WEAK int32_t cheng_pty_wait(int64_t pid, int32_t *outExitCode) {
#if defined(_WIN32)
  if (outExitCode) {
    *outExitCode = -1;
  }
  return -1;
#else
  if (outExitCode) {
    *outExitCode = -1;
  }
  if (pid <= 0) {
    return -1;
  }
  int status = 0;
  pid_t rc = waitpid((pid_t)pid, &status, WNOHANG);
  if (rc == 0) {
    return 0;
  }
  if (rc < 0) {
    return -1;
  }
  if (outExitCode) {
    if (WIFEXITED(status)) {
      *outExitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      *outExitCode = 128 + WTERMSIG(status);
    } else {
      *outExitCode = status;
    }
  }
  return 1;
#endif
}
