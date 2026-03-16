#include <stdint.h>

void __cheng_setCmdLine(int32_t argc, const char **argv);
void driver_c_capture_cmdline(int32_t argc, void *argv_void);
int32_t driver_c_run_default(void);

int main(int argc, char **argv) {
  __cheng_setCmdLine((int32_t)argc, (const char **)argv);
  driver_c_capture_cmdline((int32_t)argc, (void *)argv);
  return (int)driver_c_run_default();
}
