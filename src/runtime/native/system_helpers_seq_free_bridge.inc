#include <stddef.h>
#include "system_helpers.h"

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_SEQ_FREE_WEAK __attribute__((weak))
#else
#define CHENG_SEQ_FREE_WEAK
#endif

/* compiler-core support objects do not link the full selflink runtime. */
CHENG_SEQ_FREE_WEAK void cheng_mem_release(void *p) {
  (void)p;
}

CHENG_SEQ_FREE_WEAK void cheng_seq_free(void *seq_ptr) {
  ChengSeqHeader *seq_hdr = (ChengSeqHeader *)seq_ptr;
  if (seq_hdr == NULL) {
    return;
  }
  if (seq_hdr->buffer != NULL) {
    cheng_mem_release(seq_hdr->buffer);
  }
  seq_hdr->buffer = NULL;
  seq_hdr->len = 0;
  seq_hdr->cap = 0;
}

CHENG_SEQ_FREE_WEAK void cheng_seq_free_compat(void *seq_ptr) {
  cheng_seq_free(seq_ptr);
}
