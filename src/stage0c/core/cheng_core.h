#ifndef CHENG_STAGE0C_CORE_CHENG_CORE_H
#define CHENG_STAGE0C_CORE_CHENG_CORE_H

typedef struct ChengCoreOpts {
    const char *mode;
    const char *in_path;
    const char *out_path;
    int trace;
} ChengCoreOpts;

enum {
    CHENG_CORE_OK = 0,
    CHENG_CORE_ERR_USAGE = 2,
    CHENG_CORE_ERR_IO = 3,
    CHENG_CORE_ERR_NYI = 4
};

int cheng_core_compile(const ChengCoreOpts *opts);

#endif
