#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct ChengSeqHeader {
  int32_t len;
  int32_t cap;
  void *buffer;
} ChengSeqHeader;

static int32_t cheng_next_cap(int32_t cur_cap, int32_t need) {
  if (need <= 0) return 0;
  int32_t cap = cur_cap < 4 ? 4 : cur_cap;
  while (cap < need) {
    int32_t doubled = cap * 2;
    if (doubled <= 0) return need;
    cap = doubled;
  }
  return cap;
}

void *cheng_malloc(int32_t size) {
  int64_t bytes = size <= 0 ? 1 : (int64_t)size;
  return malloc((size_t)bytes);
}

void cheng_free(void *p) {
  free(p);
}

void *cheng_realloc(void *p, int32_t size) {
  int64_t bytes = size <= 0 ? 1 : (int64_t)size;
  return realloc(p, (size_t)bytes);
}

void *cheng_memcpy(void *dest, void *src, int64_t n) {
  if (!dest || !src || n <= 0) return dest;
  return memcpy(dest, src, (size_t)n);
}

void *cheng_memset(void *dest, int32_t val, int64_t n) {
  if (!dest || n <= 0) return dest;
  return memset(dest, val, (size_t)n);
}

void cheng_mem_retain(void *p) {
  (void)p;
}

void cheng_mem_release(void *p) {
  (void)p;
}

void *cheng_seq_get(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  if (!buffer || idx < 0 || idx >= len || elem_size <= 0) return NULL;
  return (void *)((char *)buffer + ((size_t)idx * (size_t)elem_size));
}

void *cheng_seq_set(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_get(buffer, len, idx, elem_size);
}

int32_t cheng_strcmp(char *a, char *b) {
  // Keep runtime object self-link friendly on Mach-O: avoid cstring literals
  // that may introduce non-extern relocations in this minimal runtime path.
  if (a == NULL && b == NULL) return 0;
  if (a == NULL) return -1;
  if (b == NULL) return 1;
  return strcmp(a, b);
}

int32_t cheng_strlen(char *s) {
  if (!s) return 0;
  size_t n = strlen(s);
  return n > (size_t)INT32_MAX ? INT32_MAX : (int32_t)n;
}

void *load_ptr(void *p, int32_t off) {
  if (!p) return NULL;
  return *(void **)((char *)p + off);
}

void store_ptr(void *p, int32_t off, void *v) {
  if (!p) return;
  *(void **)((char *)p + off) = v;
}

void reserve(void *seq, int32_t new_cap) {
  if (!seq || new_cap <= 0) return;
  ChengSeqHeader *hdr = (ChengSeqHeader *)seq;
  if (hdr->buffer && new_cap <= hdr->cap) return;
  int32_t target_cap = cheng_next_cap(hdr->cap, new_cap);
  if (target_cap <= 0) return;
  void *new_buf = realloc(hdr->buffer, (size_t)target_cap);
  if (!new_buf) return;
  hdr->buffer = new_buf;
  hdr->cap = target_cap;
}

void setLen(void *seq, int32_t new_len) {
  if (!seq) return;
  ChengSeqHeader *hdr = (ChengSeqHeader *)seq;
  int32_t target = new_len < 0 ? 0 : new_len;
  if (target > hdr->cap) reserve(seq, target);
  hdr->len = target;
}

int64_t cheng_f32_bits_to_i64(int32_t bits) {
  union {
    uint32_t u32;
    float f32;
  } v;
  v.u32 = (uint32_t)bits;
  return (int64_t)v.f32;
}

int64_t cheng_f64_bits_to_i64(int64_t bits) {
  union {
    uint64_t u64;
    double f64;
  } v;
  v.u64 = (uint64_t)bits;
  return (int64_t)v.f64;
}
