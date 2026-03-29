#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_EXPORT __attribute__((visibility("default")))
#else
#define CHENG_EXPORT
#endif

CHENG_EXPORT int32_t driver_export_prefer_sidecar_builds(int32_t dummy) {
  (void)dummy;
  return 1;
}
