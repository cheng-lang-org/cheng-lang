#include <stdio.h>

static int retired(void) {
    fputs("cheng_v2c: retired\n", stderr);
    fputs("v2 bootstrap/compiler entry has been archived; use v3 instead.\n", stderr);
    return 1;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return retired();
}
