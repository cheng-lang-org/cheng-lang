#include <stdint.h>

extern int32_t cheng_v3_program_argv_entry(int32_t argc, const char **argv);
extern void cheng_v3_register_line_map_from_argv0(const char *argv0) __attribute__((weak));

int main(int argc, char **argv) {
  if (cheng_v3_register_line_map_from_argv0 != 0) {
    cheng_v3_register_line_map_from_argv0((argc > 0 && argv != 0) ? argv[0] : 0);
  }
  return cheng_v3_program_argv_entry((int32_t)argc, (const char **)argv);
}
