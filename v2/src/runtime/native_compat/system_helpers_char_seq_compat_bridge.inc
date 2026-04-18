#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "system_helpers.h"

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_CHAR_SEQ_COMPAT_WEAK __attribute__((weak))
#else
#define CHENG_CHAR_SEQ_COMPAT_WEAK
#endif

#define CHENG_SEQ_ELEM_SIZE_COMPAT_CAP 4096u

static uintptr_t cheng_seq_elem_size_compat_keys[CHENG_SEQ_ELEM_SIZE_COMPAT_CAP];
static int32_t cheng_seq_elem_size_compat_vals[CHENG_SEQ_ELEM_SIZE_COMPAT_CAP];

static uint32_t cheng_seq_elem_size_compat_slot(uintptr_t key) {
  return (uint32_t)((key >> 3u) & (CHENG_SEQ_ELEM_SIZE_COMPAT_CAP - 1u));
}

static void cheng_seq_elem_size_compat_put(void *key_ptr, int32_t elem_size) {
  uintptr_t key = (uintptr_t)key_ptr;
  uint32_t slot = 0;
  uint32_t scanned = 0;
  if (key == 0 || elem_size <= 0) {
    return;
  }
  slot = cheng_seq_elem_size_compat_slot(key);
  for (scanned = 0; scanned < CHENG_SEQ_ELEM_SIZE_COMPAT_CAP; ++scanned) {
    uintptr_t cur_key = cheng_seq_elem_size_compat_keys[slot];
    if (cur_key == 0 || cur_key == key) {
      cheng_seq_elem_size_compat_keys[slot] = key;
      cheng_seq_elem_size_compat_vals[slot] = elem_size;
      return;
    }
    slot = (slot + 1u) & (CHENG_SEQ_ELEM_SIZE_COMPAT_CAP - 1u);
  }
}

CHENG_CHAR_SEQ_COMPAT_WEAK int32_t cheng_seq_string_elem_bytes_compat(void) {
  return (int32_t)sizeof(ChengStrBridge);
}

CHENG_CHAR_SEQ_COMPAT_WEAK int32_t cheng_seq_string_init_cap(int32_t seq_len, int32_t seq_cap) {
  int32_t base_cap = seq_cap < seq_len ? seq_len : seq_cap;
  if (base_cap <= 0) {
    return 0;
  }
  if (base_cap < 4) {
    return 4;
  }
  return base_cap;
}

CHENG_CHAR_SEQ_COMPAT_WEAK void cheng_seq_string_register_compat(void *seq_ptr) {
  ChengSeqHeader *seq_hdr = (ChengSeqHeader *)seq_ptr;
  int32_t elem_size = cheng_seq_string_elem_bytes_compat();
  if (seq_hdr == NULL) {
    return;
  }
  cheng_seq_elem_size_compat_put(seq_ptr, elem_size);
  cheng_seq_elem_size_compat_put(seq_hdr->buffer, elem_size);
}

CHENG_CHAR_SEQ_COMPAT_WEAK void cheng_seq_string_buffer_register_compat(void *buffer) {
  cheng_seq_elem_size_compat_put(buffer, cheng_seq_string_elem_bytes_compat());
}

CHENG_CHAR_SEQ_COMPAT_WEAK void *cheng_seq_string_alloc_compat(int32_t buf_cap) {
  int32_t total_bytes = 0;
  void *raw_buffer = NULL;
  if (buf_cap <= 0) {
    return NULL;
  }
  total_bytes = buf_cap * cheng_seq_string_elem_bytes_compat();
  raw_buffer = realloc(NULL, (size_t)total_bytes);
  if (raw_buffer == NULL) {
    abort();
  }
  memset(raw_buffer, 0, (size_t)total_bytes);
  return raw_buffer;
}

CHENG_CHAR_SEQ_COMPAT_WEAK void cheng_seq_string_init_compat(void *seq_ptr,
                                                             int32_t seq_len,
                                                             int32_t seq_cap) {
  ChengSeqHeader *seq_hdr = (ChengSeqHeader *)seq_ptr;
  int32_t buf_cap = 0;
  void *raw_buffer = NULL;
  if (seq_hdr == NULL) {
    return;
  }
  buf_cap = cheng_seq_string_init_cap(seq_len, seq_cap);
  if (buf_cap > 0) {
    raw_buffer = cheng_seq_string_alloc_compat(buf_cap);
  }
  seq_hdr->buffer = raw_buffer;
  seq_hdr->cap = buf_cap;
  seq_hdr->len = seq_len;
  cheng_seq_string_register_compat(seq_ptr);
}

CHENG_CHAR_SEQ_COMPAT_WEAK int32_t driver_c_chr_i32_compat(int32_t value) {
  return (int32_t)((unsigned char)(value & 0xff));
}

CHENG_CHAR_SEQ_COMPAT_WEAK int32_t driver_c_ord_char_compat(int32_t value) {
  return (int32_t)((unsigned char)(value & 0xff));
}
