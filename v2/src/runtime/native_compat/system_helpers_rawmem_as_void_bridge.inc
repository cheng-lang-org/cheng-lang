#include "system_helpers.h"

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_RAWMEM_AS_VOID_WEAK __attribute__((weak))
#else
#define CHENG_RAWMEM_AS_VOID_WEAK
#endif

CHENG_RAWMEM_AS_VOID_WEAK void* rawmemAsVoid(void* p) {
  return p;
}
