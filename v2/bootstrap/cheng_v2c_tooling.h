#ifndef CHENG_V2C_TOOLING_H
#define CHENG_V2C_TOOLING_H

void cheng_v2c_tooling_print_usage(void);
int cheng_v2c_tooling_is_command(const char *cmd);
int cheng_v2c_tooling_handle(int argc, char **argv);

#endif
