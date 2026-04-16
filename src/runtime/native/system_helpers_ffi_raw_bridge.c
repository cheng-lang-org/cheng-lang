#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdlib.h>

void *cheng_ffi_raw_new_i32(int32_t value) {
    int32_t *cell = (int32_t *)malloc(sizeof(int32_t));
    if (cell == NULL) {
        return NULL;
    }
    *cell = value;
    return (void *)cell;
}

int32_t cheng_ffi_raw_get_i32(void *p) {
    return p == NULL ? -1 : *(int32_t *)p;
}

int32_t cheng_ffi_raw_add_i32(void *p, int32_t delta) {
    int32_t *cell = (int32_t *)p;
    if (cell == NULL) {
        return -1;
    }
    *cell += delta;
    return *cell;
}

int32_t cheng_ffi_raw_release_i32(void *p) {
    if (p == NULL) {
        return -1;
    }
    free(p);
    return 0;
}
