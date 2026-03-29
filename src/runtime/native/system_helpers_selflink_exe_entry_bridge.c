#include "system_helpers.h"

/* Darwin self-link executables need a real process entry that forwards argv. */
extern int32_t runtime_compiler_core_argv_entry(int32_t argc, const char **argv);

int main(int argc, char **argv) {
  return runtime_compiler_core_argv_entry((int32_t)argc, (const char **)argv);
}
