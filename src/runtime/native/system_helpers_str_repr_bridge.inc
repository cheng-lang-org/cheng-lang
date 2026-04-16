#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system_helpers.h"

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_STR_REPR_WEAK __attribute__((weak))
#else
#define CHENG_STR_REPR_WEAK
#endif

static char *cheng_str_repr_copy_bytes(const char *src, size_t n) {
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) {
    return (char *)"";
  }
  if (n > 0 && src != NULL) {
    memcpy(out, src, n);
  }
  out[n] = '\0';
  return out;
}

static int cheng_str_repr_ensure(char **buf, size_t *cap, size_t need) {
  if (buf == NULL || cap == NULL) return 0;
  if (*buf == NULL || *cap == 0) {
    size_t init_cap = 64u;
    if (init_cap < need) init_cap = need;
    *buf = (char *)malloc(init_cap);
    if (*buf == NULL) {
      *cap = 0;
      return 0;
    }
    *cap = init_cap;
    return 1;
  }
  if (need <= *cap) return 1;
  size_t next = *cap;
  while (next < need) {
    size_t doubled = next * 2u;
    if (doubled <= next) {
      next = need;
      break;
    }
    next = doubled;
  }
  char *grown = (char *)realloc(*buf, next);
  if (grown == NULL) return 0;
  *buf = grown;
  *cap = next;
  return 1;
}

static int cheng_str_repr_append_bytes(char **buf,
                                       size_t *cap,
                                       size_t *len,
                                       const char *src,
                                       size_t n) {
  size_t need = 0;
  if (len == NULL) return 0;
  need = *len + n + 1u;
  if (!cheng_str_repr_ensure(buf, cap, need)) return 0;
  if (n > 0 && src != NULL) {
    memcpy((*buf) + *len, src, n);
    *len += n;
  }
  (*buf)[*len] = '\0';
  return 1;
}

static const char *cheng_str_repr_safe(const char *text) {
  return text != NULL ? text : "";
}

static char *cheng_str_repr_empty(void) {
  return cheng_str_repr_copy_bytes("", 0u);
}

CHENG_STR_REPR_WEAK char *driver_c_char_to_str(int32_t value) {
  unsigned char ch = (unsigned char)(value & 0xff);
  return cheng_str_repr_copy_bytes((const char *)&ch, 1u);
}

CHENG_STR_REPR_WEAK char *driver_c_bool_to_str(bool value) {
  return value ? cheng_str_repr_copy_bytes("true", 4u)
               : cheng_str_repr_copy_bytes("false", 5u);
}

CHENG_STR_REPR_WEAK char *driver_c_i32_to_str(int32_t value) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%d", value);
  if (n < 0) n = 0;
  return cheng_str_repr_copy_bytes(buf, (size_t)n);
}

CHENG_STR_REPR_WEAK char *driver_c_i64_to_str(int64_t value) {
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "%lld", (long long)value);
  if (n < 0) n = 0;
  return cheng_str_repr_copy_bytes(buf, (size_t)n);
}

CHENG_STR_REPR_WEAK char *driver_c_u64_to_str(uint64_t value) {
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  if (n < 0) n = 0;
  return cheng_str_repr_copy_bytes(buf, (size_t)n);
}

CHENG_STR_REPR_WEAK char *cheng_seq_string_repr_compat(ChengSeqHeader xs) {
  char **items = (char **)xs.buffer;
  size_t cap = 0;
  size_t len = 0;
  char *out = NULL;
  int32_t i = 0;
  if (!cheng_str_repr_append_bytes(&out, &cap, &len, "[", 1u)) {
    return cheng_str_repr_empty();
  }
  for (i = 0; i < xs.len; ++i) {
    const char *part = items != NULL ? cheng_str_repr_safe(items[i]) : "";
    if (i > 0 && !cheng_str_repr_append_bytes(&out, &cap, &len, ",", 1u)) {
      free(out);
      return cheng_str_repr_empty();
    }
    if (!cheng_str_repr_append_bytes(&out, &cap, &len, part, strlen(part))) {
      free(out);
      return cheng_str_repr_empty();
    }
  }
  if (!cheng_str_repr_append_bytes(&out, &cap, &len, "]", 1u)) {
    free(out);
    return cheng_str_repr_empty();
  }
  return out;
}

CHENG_STR_REPR_WEAK char *cheng_seq_bool_repr_compat(ChengSeqHeader xs) {
  const int8_t *items = (const int8_t *)xs.buffer;
  size_t cap = 0;
  size_t len = 0;
  char *out = NULL;
  int32_t i = 0;
  if (!cheng_str_repr_append_bytes(&out, &cap, &len, "[", 1u)) {
    return cheng_str_repr_empty();
  }
  for (i = 0; i < xs.len; ++i) {
    const char *part = (items != NULL && items[i] != 0) ? "true" : "false";
    if (i > 0 && !cheng_str_repr_append_bytes(&out, &cap, &len, ",", 1u)) {
      free(out);
      return cheng_str_repr_empty();
    }
    if (!cheng_str_repr_append_bytes(&out, &cap, &len, part, strlen(part))) {
      free(out);
      return cheng_str_repr_empty();
    }
  }
  if (!cheng_str_repr_append_bytes(&out, &cap, &len, "]", 1u)) {
    free(out);
    return cheng_str_repr_empty();
  }
  return out;
}

CHENG_STR_REPR_WEAK char *cheng_seq_i32_repr_compat(ChengSeqHeader xs) {
  const int32_t *items = (const int32_t *)xs.buffer;
  size_t cap = 0;
  size_t len = 0;
  char *out = NULL;
  int32_t i = 0;
  if (!cheng_str_repr_append_bytes(&out, &cap, &len, "[", 1u)) {
    return cheng_str_repr_empty();
  }
  for (i = 0; i < xs.len; ++i) {
    char *part = NULL;
    if (i > 0 && !cheng_str_repr_append_bytes(&out, &cap, &len, ",", 1u)) {
      free(out);
      return cheng_str_repr_empty();
    }
    part = driver_c_i32_to_str(items == NULL ? 0 : items[i]);
    if (!cheng_str_repr_append_bytes(&out, &cap, &len, cheng_str_repr_safe(part), strlen(cheng_str_repr_safe(part)))) {
      free(out);
      return cheng_str_repr_empty();
    }
  }
  if (!cheng_str_repr_append_bytes(&out, &cap, &len, "]", 1u)) {
    free(out);
    return cheng_str_repr_empty();
  }
  return out;
}

CHENG_STR_REPR_WEAK char *cheng_seq_i64_repr_compat(ChengSeqHeader xs) {
  const int64_t *items = (const int64_t *)xs.buffer;
  size_t cap = 0;
  size_t len = 0;
  char *out = NULL;
  int32_t i = 0;
  if (!cheng_str_repr_append_bytes(&out, &cap, &len, "[", 1u)) {
    return cheng_str_repr_empty();
  }
  for (i = 0; i < xs.len; ++i) {
    char *part = NULL;
    if (i > 0 && !cheng_str_repr_append_bytes(&out, &cap, &len, ",", 1u)) {
      free(out);
      return cheng_str_repr_empty();
    }
    part = driver_c_i64_to_str(items == NULL ? 0 : items[i]);
    if (!cheng_str_repr_append_bytes(&out, &cap, &len, cheng_str_repr_safe(part), strlen(cheng_str_repr_safe(part)))) {
      free(out);
      return cheng_str_repr_empty();
    }
  }
  if (!cheng_str_repr_append_bytes(&out, &cap, &len, "]", 1u)) {
    free(out);
    return cheng_str_repr_empty();
  }
  return out;
}
