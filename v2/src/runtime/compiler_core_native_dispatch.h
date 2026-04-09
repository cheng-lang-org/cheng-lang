#ifndef CHENG_V2_COMPILER_CORE_NATIVE_DISPATCH_H
#define CHENG_V2_COMPILER_CORE_NATIVE_DISPATCH_H

void cheng_v2_native_print_line(const char *text);
void cheng_v2_native_print_usage(void);
int cheng_v2_native_print_status(void);
int cheng_v2_native_tooling_argv_entry(int argc, char **argv);
int cheng_v2_native_program_argv_entry(int argc, char **argv);

#endif
