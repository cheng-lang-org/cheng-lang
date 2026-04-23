#ifndef CHENG_RUNTIME_ABI_H
#define CHENG_RUNTIME_ABI_H

#include <stdint.h>

typedef struct ChengStrBridge {
    const char* ptr;
    int32_t len;
    int32_t store_id;
    int32_t flags;
} ChengStrBridge;

typedef struct ChengBytesBridge {
    void* data;
    int32_t len;
} ChengBytesBridge;

typedef struct ChengErrorInfoBridgeCompat {
    int32_t code;
    ChengStrBridge msg;
} ChengErrorInfoBridgeCompat;

typedef struct ChengSeqHeader {
    int32_t len;
    int32_t cap;
    void* buffer;
} ChengSeqHeader;

enum {
    CHENG_STR_BRIDGE_FLAG_NONE = 0,
    CHENG_STR_BRIDGE_FLAG_OWNED = 1,
};

char* driver_c_new_string_copy_n(void* raw, int32_t n);
ChengStrBridge driver_c_str_from_utf8_copy_bridge(const char* raw, int32_t n);

#endif
