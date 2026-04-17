#include <stdio.h>
#include "cheng_v2c_tooling.h"

void cheng_v2c_tooling_print_usage(void) {
    puts("cheng_v2c: retired");
    puts("v2 bootstrap/tooling entry has been archived and is no longer supported.");
    puts("use the v3 compiler entry instead.");
}

int cheng_v2c_tooling_is_command(const char *cmd) {
    (void)cmd;
    return 0;
}

int cheng_v2c_tooling_handle(int argc, char **argv) {
    (void)argc;
    (void)argv;
    cheng_v2c_tooling_print_usage();
    return 1;
}
