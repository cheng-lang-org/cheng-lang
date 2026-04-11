#include <time.h>
#include <stdint.h>
#include <errno.h>

// Keep self-link runtime closure complete even when the base runtime object
// is built from a minimal surface that omits cheng_epoch_time.
__attribute__((weak)) double cheng_epoch_time(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
    }
    return (double)time(NULL);
}

__attribute__((weak)) int64_t cheng_epoch_time_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (int64_t)ts.tv_sec;
    }
    return (int64_t)time(NULL);
}

__attribute__((weak)) int32_t cheng_errno(void) {
    return (int32_t)errno;
}
