
#include <stdio.h>

// Declare the initialization function from test_io.c
// Generated C uses __attribute__((constructor)) for top level code,
// so it runs before main.
// We just need a main to satisfy linker.

extern void init(void); // Just in case we need to reference it, but likely not needed if constructor.

int main() {
    // printf("Running main...\n");
    return 0;
}
