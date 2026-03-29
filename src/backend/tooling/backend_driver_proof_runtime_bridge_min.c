#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct ChengStrBridge {
  const char *ptr;
  int32_t len;
  int32_t store_id;
  int32_t flags;
} ChengStrBridge;

enum {
  CHENG_STR_BRIDGE_FLAG_NONE = 0,
  CHENG_STR_BRIDGE_FLAG_OWNED = 1,
};

static bool cheng_probably_valid_ptr(const void *p) {
  uintptr_t raw = (uintptr_t)p;
  if (p == NULL) return false;
  if (raw < (uintptr_t)65536) return false;
  if ((raw & (uintptr_t)(sizeof(void *) - 1)) != 0) return false;
  return true;
}

static bool cheng_probably_valid_str_bridge(const ChengStrBridge *s) {
  if (s == NULL) return false;
  if (s->len < 0 || s->len > (1 << 28)) return false;
  if ((s->flags & ~CHENG_STR_BRIDGE_FLAG_OWNED) != 0) return false;
  if (s->ptr == NULL) return s->len == 0;
  return cheng_probably_valid_ptr(s->ptr);
}

__attribute__((visibility("default")))
int32_t cheng_cstrlen(const char *s) {
  if (s == NULL) {
    return 0;
  }
  size_t n = strlen(s);
  return n > 2147483647u ? 2147483647 : (int32_t)n;
}

__attribute__((visibility("default")))
char *cheng_str_param_to_cstring_compat(void *raw) {
  uintptr_t raw_addr = (uintptr_t)raw;
  if (raw == NULL) return NULL;
  if (raw_addr < (uintptr_t)65536) return NULL;
  if ((raw_addr & (uintptr_t)(sizeof(void *) - 1)) != 0) return (char *)raw;
  if (!cheng_probably_valid_str_bridge((const ChengStrBridge *)raw)) return (char *)raw;
  return NULL;
}

__attribute__((visibility("default")))
ChengStrBridge load(const ChengStrBridge *p) {
  ChengStrBridge out;
  out.ptr = (const char *)0;
  out.len = 0;
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_NONE;
  if (p != (const ChengStrBridge *)0) {
    out = *p;
  }
  return out;
}
