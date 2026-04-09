#include "compiler_core_native_dispatch.h"

int runtime_compiler_core_tooling_argv_entry(int argc, char **argv) {
    return cheng_v2_native_tooling_argv_entry(argc, argv);
}

int runtime_compiler_core_program_argv_entry(int argc, char **argv) {
    return cheng_v2_native_program_argv_entry(argc, argv);
}
