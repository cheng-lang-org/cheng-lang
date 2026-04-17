#include <stdio.h>

static int retired(void) {
    fputs("cheng_v2_bootstrap: retired\n", stderr);
    fputs("v2 bootstrap manifest/check-tree entry has been archived; use v3 instead.\n", stderr);
    return 1;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return retired();
}
