#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* cdrop_pick_delegate(void) {
    const char* env = getenv("CHENG_CDROP_DELEGATE");
    if (env != NULL && env[0] != '\0') {
        return env;
    }
    if (access("artifacts/backend_driver/cheng", X_OK) == 0) {
        return "artifacts/backend_driver/cheng";
    }
    if (access("dist/releases/current/cheng", X_OK) == 0) {
        return "dist/releases/current/cheng";
    }
    if (access("artifacts/backend_seed/cheng.stage2", X_OK) == 0) {
        return "artifacts/backend_seed/cheng.stage2";
    }
    return NULL;
}

int main(int argc, char** argv) {
    (void)argc;
    const char* delegate = cdrop_pick_delegate();
    if (delegate == NULL) {
        fprintf(stderr,
                "cheng_boot.c emergency seed: no delegate driver found. "
                "Set CHENG_CDROP_DELEGATE to a runnable cheng backend driver.\n");
        return 1;
    }

    if (argv != NULL && argv[0] != NULL) {
        argv[0] = (char*)delegate;
    }
    execv(delegate, argv);

    fprintf(stderr,
            "cheng_boot.c emergency seed: execv(%s) failed: %s\n",
            delegate,
            strerror(errno));
    return 127;
}
