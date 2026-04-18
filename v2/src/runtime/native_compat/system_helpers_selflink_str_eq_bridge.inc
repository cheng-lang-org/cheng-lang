#include <stddef.h>
#include <string.h>

#include "system_helpers.h"

int32_t driver_c_str_eq_bridge(ChengStrBridge a, ChengStrBridge b) {
  if (a.len != b.len) {
    return 0;
  }
  if (a.len <= 0) {
    return 1;
  }
  if (a.ptr == NULL || b.ptr == NULL) {
    return 0;
  }
  return memcmp(a.ptr, b.ptr, (size_t)a.len) == 0 ? 1 : 0;
}
