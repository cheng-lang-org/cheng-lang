#include <stdint.h>

extern void __cheng_setCmdLine(int32_t argc, const char **argv) __attribute__((weak));
extern void driver_c_capture_cmdline(int32_t argc, void *argv_void) __attribute__((weak));
extern int32_t backendMain(void);

int main(int argc, char **argv) {
  if (__cheng_setCmdLine != 0) {
    __cheng_setCmdLine((int32_t)argc, (const char **)argv);
  }
  if (driver_c_capture_cmdline != 0) {
    driver_c_capture_cmdline((int32_t)argc, (void *)argv);
  }
  return (int)backendMain();
}
