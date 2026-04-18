#include "system_helpers.h"

extern int32_t runtime_compiler_core_tooling_argv_entry(int32_t argc, const char **argv);
extern int32_t runtime_compiler_core_program_argv_entry(int32_t argc, const char **argv);

int main(int argc, char **argv) {
#if defined(CHENG_SELFLINK_PROGRAM_ENTRY)
  return runtime_compiler_core_program_argv_entry((int32_t)argc, (const char **)argv);
#else
  return runtime_compiler_core_tooling_argv_entry((int32_t)argc, (const char **)argv);
#endif
}
