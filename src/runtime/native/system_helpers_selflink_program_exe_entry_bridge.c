#include "system_helpers.h"

/* Darwin self-link executables for ordinary compiler_core programs forward argv to the program track. */
extern int32_t runtime_compiler_core_program_argv_entry(int32_t argc, const char **argv);

int main(int argc, char **argv) {
  return runtime_compiler_core_program_argv_entry((int32_t)argc, (const char **)argv);
}
