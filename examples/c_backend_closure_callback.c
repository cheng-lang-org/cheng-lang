#include <stddef.h>
#include "chengbase.h"

typedef NI32 (*ChengCallback)(void*, NI32);

N_CDECL(NI32, call_cb)(ChengCallback cb, void* ctx, NI32 v) {
  if (cb == NIL) {
    return 0;
  }
  return cb(ctx, v);
}
