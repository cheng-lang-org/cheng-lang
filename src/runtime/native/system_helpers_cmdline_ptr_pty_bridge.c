#include <stdint.h>

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
