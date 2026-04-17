#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "system_helpers.h"

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_PTR_SEQ_BITS_WEAK __attribute__((weak))
#else
#define CHENG_PTR_SEQ_BITS_WEAK
#endif

/*
 * compiler-core support objects link a minimal native closure, so keep these
 * small runtime helpers available without pulling in the full legacy helper bundle.
 */
CHENG_PTR_SEQ_BITS_WEAK void *cheng_ptr_seq_get_compat(ChengSeqHeader seq, int32_t idx) {
  void *out = NULL;
  if (seq.buffer == NULL || idx < 0 || idx >= seq.len) {
    return NULL;
  }
  memcpy(&out, (char *)seq.buffer + ((size_t)idx * sizeof(void *)), sizeof(void *));
  return out;
}

CHENG_PTR_SEQ_BITS_WEAK void cheng_ptr_seq_set_compat(ChengSeqHeader *seq_ptr, int32_t idx, void *value) {
  if (seq_ptr == NULL || seq_ptr->buffer == NULL || idx < 0 || idx >= seq_ptr->len) {
    return;
  }
  memcpy((char *)seq_ptr->buffer + ((size_t)idx * sizeof(void *)), &value, sizeof(void *));
}

CHENG_PTR_SEQ_BITS_WEAK double cheng_bits_to_f32(int32_t bits) {
  union {
    uint32_t raw;
    float value;
  } cvt;
  cvt.raw = (uint32_t)bits;
  return (double)cvt.value;
}
