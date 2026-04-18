#include "cheng_mobile_host_api.h"
#include "cheng_mobile_host_core.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>

#ifdef __ANDROID__
#include <android/log.h>
#define HOST_API_TAG "ChengMobile"
int cheng_mobile_host_android_width(void);
int cheng_mobile_host_android_height(void);
double cheng_mobile_host_android_density_scale(void);
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_MOBILE_MAYBE_UNUSED __attribute__((unused))
#else
#define CHENG_MOBILE_MAYBE_UNUSED
#endif

#define BUS_QUEUE_CAP 256u
#define BUS_PAYLOAD_CAP 8192u
#define BUS_ENVELOPE_CAP 8192u
#define RUNTIME_ERROR_CAP 256
#define RUNTIME_LAUNCH_ARGS_CAP 4096
#define SHM_SLOT_CAP 128u
#define CBOR_BUILDER_SLOT_CAP 16
#define CBOR_BUILDER_ITEM_CAP 64
#define CBOR_BUILDER_KEY_CAP 96
#define CBOR_BUILDER_VALUE_CAP 3072

#ifdef __ANDROID__
static int s_emit_pointer_count = 0;
#endif
static pthread_mutex_t s_runtime_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_shm_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t s_bus_once = PTHREAD_ONCE_INIT;

/* MPMC bounded queue (Vyukov sequence ring). */
typedef struct {
  atomic_uint seq;
  ChengMobileBusMessage msg;
  uint32_t payloadSize;
  uint8_t payload[BUS_PAYLOAD_CAP];
} ChengBusSlot;

typedef struct {
  atomic_uint enqueuePos;
  atomic_uint dequeuePos;
  ChengBusSlot slots[BUS_QUEUE_CAP];
} ChengBusQueue;

static ChengBusQueue s_request_queue = {0};
static ChengBusQueue s_response_queue = {0};

typedef enum {
  CHENG_CBOR_ITEM_TEXT = 1,
  CHENG_CBOR_ITEM_BOOL = 2,
  CHENG_CBOR_ITEM_I64 = 3,
  CHENG_CBOR_ITEM_F64 = 4,
  CHENG_CBOR_ITEM_NULL = 5,
} ChengCborBuilderItemKind;

typedef struct {
  int inUse;
  int handle;
  int count;
  char key[CBOR_BUILDER_ITEM_CAP][CBOR_BUILDER_KEY_CAP];
  int kind[CBOR_BUILDER_ITEM_CAP];
  char text[CBOR_BUILDER_ITEM_CAP][CBOR_BUILDER_VALUE_CAP];
  int boolValue[CBOR_BUILDER_ITEM_CAP];
  int64_t i64Value[CBOR_BUILDER_ITEM_CAP];
  double f64Value[CBOR_BUILDER_ITEM_CAP];
} ChengCborBuilder;

typedef struct {
  int started;
  char lastError[RUNTIME_ERROR_CAP];
  char launchArgsKv[RUNTIME_LAUNCH_ARGS_CAP];
  char launchArgsJson[RUNTIME_LAUNCH_ARGS_CAP];
} ChengRuntimeState;

static ChengRuntimeState s_runtime = {
    .started = 0,
    .lastError = "",
    .launchArgsKv = "",
    .launchArgsJson = "",
};

static __thread ChengCborBuilder s_cbor_builder_slots[CBOR_BUILDER_SLOT_CAP];
static __thread int s_cbor_builder_next_handle = 1;

typedef struct {
  int valid;
  int decodeOk;
  char payload[BUS_ENVELOPE_CAP];
  uint8_t cbor[BUS_PAYLOAD_CAP];
  size_t cborLen;
} ChengCborDecodeCache;

static __thread ChengCborDecodeCache s_cbor_decode_cache = {0};

static void cheng_json_escape_copy(const char* src, char* dst, size_t cap) {
  if (dst == NULL || cap == 0u) {
    return;
  }
  dst[0] = '\0';
  if (src == NULL) {
    return;
  }
  size_t w = 0u;
  for (size_t i = 0u; src[i] != '\0'; i++) {
    unsigned char ch = (unsigned char)src[i];
    if (w + 1u >= cap) {
      break;
    }
    if (ch == '\"' || ch == '\\') {
      if (w + 2u >= cap) {
        break;
      }
      dst[w++] = '\\';
      dst[w++] = (char)ch;
      continue;
    }
    if (ch == '\n') {
      if (w + 2u >= cap) {
        break;
      }
      dst[w++] = '\\';
      dst[w++] = 'n';
      continue;
    }
    if (ch == '\r') {
      if (w + 2u >= cap) {
        break;
      }
      dst[w++] = '\\';
      dst[w++] = 'r';
      continue;
    }
    if (ch == '\t') {
      if (w + 2u >= cap) {
        break;
      }
      dst[w++] = '\\';
      dst[w++] = 't';
      continue;
    }
    if (ch < 0x20u) {
      if (w + 6u >= cap) {
        break;
      }
      (void)snprintf(dst + w, cap - w, "\\u%04x", (unsigned int)ch);
      w += 6u;
      continue;
    }
    dst[w++] = (char)ch;
  }
  dst[w] = '\0';
}

static int cheng_runtime_is_token_boundary_char(char c) {
  return c == ' ' || c == ';' || c == '\t' || c == '\n' || c == '\r';
}

static int cheng_runtime_reason_get_token(const char* reason, const char* key, char* out, size_t out_cap) {
  if (out == NULL || out_cap == 0u) return 0;
  out[0] = '\0';
  if (reason == NULL || key == NULL || key[0] == '\0') return 0;
  size_t key_len = strlen(key);
  const char* p = reason;
  while (p != NULL && *p != '\0') {
    const char* hit = strstr(p, key);
    if (hit == NULL) break;
    if (hit != reason) {
      char prev = hit[-1];
      if (!cheng_runtime_is_token_boundary_char(prev)) {
        p = hit + 1;
        continue;
      }
    }
    if (hit[key_len] != '=') {
      p = hit + 1;
      continue;
    }
    const char* value = hit + key_len + 1u;
    size_t n = 0u;
    while (value[n] != '\0' && value[n] != ' ' && value[n] != ';' && value[n] != '\t' && value[n] != '\n' &&
           value[n] != '\r') {
      n += 1u;
    }
    if (n == 0u) return 0;
    if (n >= out_cap) n = out_cap - 1u;
    memcpy(out, value, n);
    out[n] = '\0';
    return 1;
  }
  return 0;
}

static int cheng_runtime_token_to_u32(const char* token, uint32_t* out) {
  if (out == NULL || token == NULL || token[0] == '\0') return 0;
  char* end = NULL;
  unsigned long v = strtoul(token, &end, 10);
  if (end == token) return 0;
  *out = (uint32_t)v;
  return 1;
}

static int cheng_runtime_token_truthy(const char* token) {
  if (token == NULL || token[0] == '\0') return 0;
  if (strcmp(token, "1") == 0 || strcmp(token, "true") == 0 || strcmp(token, "TRUE") == 0) return 1;
  char* end = NULL;
  unsigned long v = strtoul(token, &end, 10);
  if (end != token && *end == '\0') {
    return v != 0ul ? 1 : 0;
  }
  return 0;
}

typedef struct {
  int inUse;
  uint32_t handle;
  uint8_t* bytes;
  uint64_t sizeBytes;
  uint32_t usageFlags;
  uint32_t mapCount;
} ChengShmSlot;

static ChengShmSlot s_shm_slots[SHM_SLOT_CAP];
static atomic_uint s_shm_next_handle = 1u;

#if !defined(__ANDROID__)
static pthread_mutex_t s_event_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_event_cache_valid = 0;
static ChengMobileEvent s_event_cache = {0};
static char s_event_cache_text[512];
static char s_event_cache_message[512];
#endif

static uint64_t cheng_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ull + (uint64_t)(tv.tv_usec / 1000);
}

static uint32_t cheng_hash_request_id(const char* text) {
  if (text == NULL || text[0] == '\0') {
    return 0u;
  }
  uint32_t h = 2166136261u;
  for (const unsigned char* p = (const unsigned char*)text; *p != '\0'; ++p) {
    h ^= (uint32_t)(*p);
    h *= 16777619u;
  }
  if (h == 0u) {
    h = 1u;
  }
  return h;
}

static CHENG_MOBILE_MAYBE_UNUSED int cheng_trimmed_copy(const char* src, char* out, size_t cap) {
  if (out == NULL || cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  if (src == NULL) {
    return 0;
  }
  size_t n = strlen(src);
  size_t s = 0u;
  while (s < n && isspace((unsigned char)src[s])) {
    s += 1u;
  }
  size_t e = n;
  while (e > s && isspace((unsigned char)src[e - 1u])) {
    e -= 1u;
  }
  if (e <= s) {
    return 0;
  }
  size_t len = e - s;
  if (len >= cap) {
    len = cap - 1u;
  }
  memcpy(out, src + s, len);
  out[len] = '\0';
  return 1;
}

static size_t cheng_base64url_encode(const uint8_t* src, size_t len, char* out, size_t cap) {
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  if (out == NULL || cap == 0u) {
    return 0u;
  }
  out[0] = '\0';
  if (src == NULL || len == 0u) {
    return 0u;
  }
  size_t w = 0u;
  size_t i = 0u;
  while (i + 2u < len) {
    if (w + 4u >= cap) {
      break;
    }
    uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1u] << 8) | (uint32_t)src[i + 2u];
    out[w++] = kAlphabet[(v >> 18) & 0x3Fu];
    out[w++] = kAlphabet[(v >> 12) & 0x3Fu];
    out[w++] = kAlphabet[(v >> 6) & 0x3Fu];
    out[w++] = kAlphabet[v & 0x3Fu];
    i += 3u;
  }
  size_t rem = len - i;
  if (rem == 1u && w + 2u < cap) {
    uint32_t v = ((uint32_t)src[i] << 16);
    out[w++] = kAlphabet[(v >> 18) & 0x3Fu];
    out[w++] = kAlphabet[(v >> 12) & 0x3Fu];
  } else if (rem == 2u && w + 3u < cap) {
    uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1u] << 8);
    out[w++] = kAlphabet[(v >> 18) & 0x3Fu];
    out[w++] = kAlphabet[(v >> 12) & 0x3Fu];
    out[w++] = kAlphabet[(v >> 6) & 0x3Fu];
  }
  out[w] = '\0';
  return w;
}

static int cheng_base64url_value(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
  if (c >= 'a' && c <= 'z') return 26 + (int)(c - 'a');
  if (c >= '0' && c <= '9') return 52 + (int)(c - '0');
  if (c == '-' || c == '+') return 62;
  if (c == '_' || c == '/') return 63;
  if (c == '=') return -2;
  return -1;
}

static size_t cheng_base64url_decode(const char* text, uint8_t* out, size_t cap) {
  if (text == NULL || out == NULL || cap == 0u) {
    return 0u;
  }
  size_t w = 0u;
  int quartet[4];
  int qn = 0;
  for (size_t i = 0u; text[i] != '\0'; ++i) {
    int v = cheng_base64url_value((unsigned char)text[i]);
    if (v == -1) {
      continue;
    }
    quartet[qn++] = v;
    if (qn == 4) {
      if (quartet[0] < 0 || quartet[1] < 0) {
        return w;
      }
      uint32_t word = ((uint32_t)quartet[0] << 18) | ((uint32_t)quartet[1] << 12);
      if (quartet[2] >= 0) {
        word |= (uint32_t)quartet[2] << 6;
      }
      if (quartet[3] >= 0) {
        word |= (uint32_t)quartet[3];
      }
      if (w < cap) out[w++] = (uint8_t)((word >> 16) & 0xFFu);
      if (quartet[2] >= 0 && w < cap) out[w++] = (uint8_t)((word >> 8) & 0xFFu);
      if (quartet[3] >= 0 && w < cap) out[w++] = (uint8_t)(word & 0xFFu);
      qn = 0;
    }
  }
  if (qn == 2) {
    uint32_t word = ((uint32_t)quartet[0] << 18) | ((uint32_t)quartet[1] << 12);
    if (w < cap) out[w++] = (uint8_t)((word >> 16) & 0xFFu);
  } else if (qn == 3) {
    uint32_t word = ((uint32_t)quartet[0] << 18) | ((uint32_t)quartet[1] << 12) | ((uint32_t)quartet[2] << 6);
    if (w < cap) out[w++] = (uint8_t)((word >> 16) & 0xFFu);
    if (w < cap) out[w++] = (uint8_t)((word >> 8) & 0xFFu);
  }
  return w;
}

static size_t cheng_cbor_write_type_len(uint8_t major, uint64_t value, uint8_t* out, size_t cap) {
  if (out == NULL || cap == 0u) {
    return 0u;
  }
  if (value <= 23u) {
    out[0] = (uint8_t)((major << 5) | (uint8_t)value);
    return 1u;
  }
  if (value <= 0xFFu && cap >= 2u) {
    out[0] = (uint8_t)((major << 5) | 24u);
    out[1] = (uint8_t)value;
    return 2u;
  }
  if (value <= 0xFFFFu && cap >= 3u) {
    out[0] = (uint8_t)((major << 5) | 25u);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)(value & 0xFFu);
    return 3u;
  }
  if (value <= 0xFFFFFFFFu && cap >= 5u) {
    out[0] = (uint8_t)((major << 5) | 26u);
    out[1] = (uint8_t)((value >> 24) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 8) & 0xFFu);
    out[4] = (uint8_t)(value & 0xFFu);
    return 5u;
  }
  if (cap < 9u) {
    return 0u;
  }
  out[0] = (uint8_t)((major << 5) | 27u);
  out[1] = (uint8_t)((value >> 56) & 0xFFu);
  out[2] = (uint8_t)((value >> 48) & 0xFFu);
  out[3] = (uint8_t)((value >> 40) & 0xFFu);
  out[4] = (uint8_t)((value >> 32) & 0xFFu);
  out[5] = (uint8_t)((value >> 24) & 0xFFu);
  out[6] = (uint8_t)((value >> 16) & 0xFFu);
  out[7] = (uint8_t)((value >> 8) & 0xFFu);
  out[8] = (uint8_t)(value & 0xFFu);
  return 9u;
}

static size_t cheng_cbor_write_text(const char* text, uint8_t* out, size_t cap) {
  if (out == NULL || cap == 0u) {
    return 0u;
  }
  const char* s = text ? text : "";
  size_t n = strlen(s);
  size_t head = cheng_cbor_write_type_len(3u, (uint64_t)n, out, cap);
  if (head == 0u || head + n > cap) {
    return 0u;
  }
  if (n > 0u) {
    memcpy(out + head, s, n);
  }
  return head + n;
}

static size_t cheng_cbor_write_int64(int64_t value, uint8_t* out, size_t cap) {
  if (value >= 0) {
    return cheng_cbor_write_type_len(0u, (uint64_t)value, out, cap);
  }
  uint64_t u = (uint64_t)(-1 - value);
  return cheng_cbor_write_type_len(1u, u, out, cap);
}

static size_t cheng_cbor_write_bool(int value, uint8_t* out, size_t cap) {
  if (out == NULL || cap < 1u) {
    return 0u;
  }
  out[0] = (uint8_t)(value ? 0xF5u : 0xF4u);
  return 1u;
}

static size_t cheng_cbor_write_f64(double value, uint8_t* out, size_t cap) {
  if (out == NULL || cap < 9u) {
    return 0u;
  }
  union {
    double d;
    uint64_t u;
  } cvt;
  cvt.d = value;
  out[0] = 0xFBu;
  out[1] = (uint8_t)((cvt.u >> 56) & 0xFFu);
  out[2] = (uint8_t)((cvt.u >> 48) & 0xFFu);
  out[3] = (uint8_t)((cvt.u >> 40) & 0xFFu);
  out[4] = (uint8_t)((cvt.u >> 32) & 0xFFu);
  out[5] = (uint8_t)((cvt.u >> 24) & 0xFFu);
  out[6] = (uint8_t)((cvt.u >> 16) & 0xFFu);
  out[7] = (uint8_t)((cvt.u >> 8) & 0xFFu);
  out[8] = (uint8_t)(cvt.u & 0xFFu);
  return 9u;
}

static size_t cheng_cbor_write_null(uint8_t* out, size_t cap) {
  if (out == NULL || cap < 1u) {
    return 0u;
  }
  out[0] = 0xF6u;
  return 1u;
}

static void cheng_copy_cstr(const char* src, char* dst, size_t cap) {
  if (dst == NULL || cap == 0u) return;
  dst[0] = '\0';
  if (src == NULL) return;
  size_t n = strnlen(src, cap - 1u);
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void cheng_cbor_builder_reset_slot(ChengCborBuilder* slot) {
  if (slot == NULL) return;
  memset(slot, 0, sizeof(*slot));
}

static ChengCborBuilder* cheng_cbor_builder_alloc_slot(void) {
  for (int i = 0; i < CBOR_BUILDER_SLOT_CAP; ++i) {
    if (!s_cbor_builder_slots[i].inUse) {
      cheng_cbor_builder_reset_slot(&s_cbor_builder_slots[i]);
      s_cbor_builder_slots[i].inUse = 1;
      s_cbor_builder_slots[i].handle = s_cbor_builder_next_handle++;
      if (s_cbor_builder_slots[i].handle <= 0) {
        s_cbor_builder_slots[i].handle = 1;
        s_cbor_builder_next_handle = 2;
      }
      return &s_cbor_builder_slots[i];
    }
  }
  return NULL;
}

static ChengCborBuilder* cheng_cbor_builder_find_slot(int handle) {
  if (handle <= 0) return NULL;
  for (int i = 0; i < CBOR_BUILDER_SLOT_CAP; ++i) {
    if (s_cbor_builder_slots[i].inUse && s_cbor_builder_slots[i].handle == handle) {
      return &s_cbor_builder_slots[i];
    }
  }
  return NULL;
}

static int cheng_cbor_builder_put_item_common(ChengCborBuilder* slot, const char* key) {
  if (slot == NULL || key == NULL || key[0] == '\0') return -1;
  int idx = slot->count;
  if (idx < 0 || idx >= CBOR_BUILDER_ITEM_CAP) return -1;
  cheng_copy_cstr(key, slot->key[idx], sizeof(slot->key[idx]));
  if (slot->key[idx][0] == '\0') return -1;
  slot->count = idx + 1;
  return idx;
}

typedef struct {
  const uint8_t* bytes;
  size_t len;
  size_t pos;
  int depth;
} ChengCborReader;

static int cheng_cbor_read_u8(ChengCborReader* rd, uint8_t* out) {
  if (rd == NULL || out == NULL || rd->pos >= rd->len) {
    return 0;
  }
  *out = rd->bytes[rd->pos++];
  return 1;
}

static int cheng_cbor_read_arg(ChengCborReader* rd, uint8_t ai, uint64_t* out) {
  if (out == NULL) return 0;
  if (ai <= 23u) {
    *out = (uint64_t)ai;
    return 1;
  }
  uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, b7 = 0;
  if (ai == 24u) {
    if (!cheng_cbor_read_u8(rd, &b0)) return 0;
    *out = (uint64_t)b0;
    return 1;
  }
  if (ai == 25u) {
    if (!cheng_cbor_read_u8(rd, &b0) || !cheng_cbor_read_u8(rd, &b1)) return 0;
    *out = ((uint64_t)b0 << 8) | (uint64_t)b1;
    return 1;
  }
  if (ai == 26u) {
    if (!cheng_cbor_read_u8(rd, &b0) || !cheng_cbor_read_u8(rd, &b1) || !cheng_cbor_read_u8(rd, &b2) ||
        !cheng_cbor_read_u8(rd, &b3)) {
      return 0;
    }
    *out = ((uint64_t)b0 << 24) | ((uint64_t)b1 << 16) | ((uint64_t)b2 << 8) | (uint64_t)b3;
    return 1;
  }
  if (ai == 27u) {
    if (!cheng_cbor_read_u8(rd, &b0) || !cheng_cbor_read_u8(rd, &b1) || !cheng_cbor_read_u8(rd, &b2) ||
        !cheng_cbor_read_u8(rd, &b3) || !cheng_cbor_read_u8(rd, &b4) || !cheng_cbor_read_u8(rd, &b5) ||
        !cheng_cbor_read_u8(rd, &b6) || !cheng_cbor_read_u8(rd, &b7)) {
      return 0;
    }
    *out = ((uint64_t)b0 << 56) | ((uint64_t)b1 << 48) | ((uint64_t)b2 << 40) | ((uint64_t)b3 << 32) |
           ((uint64_t)b4 << 24) | ((uint64_t)b5 << 16) | ((uint64_t)b6 << 8) | (uint64_t)b7;
    return 1;
  }
  return 0;
}

static int cheng_cbor_skip_item_with_head(ChengCborReader* rd, uint8_t ib);

static int cheng_cbor_skip_item(ChengCborReader* rd) {
  uint8_t ib = 0;
  if (!cheng_cbor_read_u8(rd, &ib)) return 0;
  return cheng_cbor_skip_item_with_head(rd, ib);
}

static int cheng_cbor_skip_item_with_head(ChengCborReader* rd, uint8_t ib) {
  if (rd == NULL) return 0;
  uint8_t major = (uint8_t)(ib >> 5);
  uint8_t ai = (uint8_t)(ib & 0x1Fu);
  uint64_t n = 0;
  if (major == 0u || major == 1u) {
    return cheng_cbor_read_arg(rd, ai, &n);
  }
  if (major == 2u || major == 3u) {
    if (ai == 31u) {
      for (;;) {
        uint8_t b = 0;
        if (!cheng_cbor_read_u8(rd, &b)) return 0;
        if (b == 0xFFu) return 1;
        uint8_t cm = (uint8_t)(b >> 5);
        if (cm != major) return 0;
        if (!cheng_cbor_skip_item_with_head(rd, b)) return 0;
      }
    }
    if (!cheng_cbor_read_arg(rd, ai, &n)) return 0;
    if (rd->pos + (size_t)n > rd->len) return 0;
    rd->pos += (size_t)n;
    return 1;
  }
  if (major == 4u) {
    if (ai == 31u) {
      for (;;) {
        uint8_t b = 0;
        if (!cheng_cbor_read_u8(rd, &b)) return 0;
        if (b == 0xFFu) return 1;
        if (!cheng_cbor_skip_item_with_head(rd, b)) return 0;
      }
    }
    if (!cheng_cbor_read_arg(rd, ai, &n)) return 0;
    for (uint64_t i = 0; i < n; ++i) {
      if (!cheng_cbor_skip_item(rd)) return 0;
    }
    return 1;
  }
  if (major == 5u) {
    if (ai == 31u) {
      for (;;) {
        uint8_t b = 0;
        if (!cheng_cbor_read_u8(rd, &b)) return 0;
        if (b == 0xFFu) return 1;
        if (!cheng_cbor_skip_item_with_head(rd, b)) return 0;
        if (!cheng_cbor_skip_item(rd)) return 0;
      }
    }
    if (!cheng_cbor_read_arg(rd, ai, &n)) return 0;
    for (uint64_t i = 0; i < n; ++i) {
      if (!cheng_cbor_skip_item(rd)) return 0;
      if (!cheng_cbor_skip_item(rd)) return 0;
    }
    return 1;
  }
  if (major == 6u) {
    if (!cheng_cbor_read_arg(rd, ai, &n)) return 0;
    return cheng_cbor_skip_item(rd);
  }
  if (major == 7u) {
    if (ai <= 23u) return 1;
    if (ai == 24u) {
      uint8_t b = 0;
      return cheng_cbor_read_u8(rd, &b);
    }
    if (ai == 25u || ai == 26u || ai == 27u) {
      return cheng_cbor_read_arg(rd, ai, &n);
    }
    if (ai == 31u) return 1;
    return 0;
  }
  return 0;
}

static int cheng_cbor_item_to_text(ChengCborReader* rd, uint8_t ib, char* out, size_t cap) {
  if (out == NULL || cap == 0u || rd == NULL) {
    return 0;
  }
  out[0] = '\0';
  uint8_t major = (uint8_t)(ib >> 5);
  uint8_t ai = (uint8_t)(ib & 0x1Fu);
  uint64_t arg = 0;
  if (major == 0u || major == 1u) {
    if (!cheng_cbor_read_arg(rd, ai, &arg)) return 0;
    if (major == 0u) {
      snprintf(out, cap, "%llu", (unsigned long long)arg);
    } else {
      long long v = -(long long)(arg + 1u);
      snprintf(out, cap, "%lld", v);
    }
    return 1;
  }
  if (major == 2u || major == 3u) {
    if (ai == 31u) {
      size_t w = 0u;
      for (;;) {
        uint8_t b = 0;
        if (!cheng_cbor_read_u8(rd, &b)) return 0;
        if (b == 0xFFu) break;
        uint8_t cm = (uint8_t)(b >> 5);
        uint8_t cai = (uint8_t)(b & 0x1Fu);
        if (cm != major) return 0;
        uint64_t n = 0;
        if (!cheng_cbor_read_arg(rd, cai, &n)) return 0;
        if (rd->pos + (size_t)n > rd->len) return 0;
        if (major == 2u) {
          char enc[1024];
          size_t m = cheng_base64url_encode(rd->bytes + rd->pos, (size_t)n, enc, sizeof(enc));
          if (m == 0u && n != 0u) return 0;
          size_t room = cap - w;
          if (room <= 1u) return 0;
          int nn = snprintf(out + w, room, "%s", enc);
          if (nn <= 0 || (size_t)nn >= room) return 0;
          w += (size_t)nn;
        } else {
          if (w + (size_t)n + 1u > cap) return 0;
          memcpy(out + w, rd->bytes + rd->pos, (size_t)n);
          w += (size_t)n;
          out[w] = '\0';
        }
        rd->pos += (size_t)n;
      }
      return 1;
    }
    if (!cheng_cbor_read_arg(rd, ai, &arg)) return 0;
    if (rd->pos + (size_t)arg > rd->len) return 0;
    if (major == 2u) {
      size_t n = cheng_base64url_encode(rd->bytes + rd->pos, (size_t)arg, out, cap);
      if (n == 0u && arg != 0u) return 0;
      out[n] = '\0';
    } else {
      size_t n = (size_t)arg;
      if (n + 1u > cap) return 0;
      memcpy(out, rd->bytes + rd->pos, n);
      out[n] = '\0';
    }
    rd->pos += (size_t)arg;
    return 1;
  }
  if (major == 6u) {
    if (!cheng_cbor_read_arg(rd, ai, &arg)) return 0;
    (void)arg;
    uint8_t nested = 0;
    if (!cheng_cbor_read_u8(rd, &nested)) return 0;
    return cheng_cbor_item_to_text(rd, nested, out, cap);
  }
  if (major == 7u) {
    if (ai == 20u) {
      snprintf(out, cap, "0");
      return 1;
    }
    if (ai == 21u) {
      snprintf(out, cap, "1");
      return 1;
    }
    if (ai == 22u || ai == 23u) {
      out[0] = '\0';
      return 1;
    }
    if (ai == 24u) {
      uint8_t v = 0;
      if (!cheng_cbor_read_u8(rd, &v)) return 0;
      snprintf(out, cap, "%u", (unsigned)v);
      return 1;
    }
    if (ai == 25u || ai == 26u || ai == 27u) {
      uint64_t u = 0;
      if (!cheng_cbor_read_arg(rd, ai, &u)) return 0;
      double d = 0.0;
      if (ai == 25u) {
        /* half-float: keep integer-like fallback */
        d = (double)((int)u);
      } else if (ai == 26u) {
        union {
          uint32_t u32;
          float f32;
        } cvt;
        cvt.u32 = (uint32_t)u;
        d = (double)cvt.f32;
      } else {
        union {
          uint64_t u64;
          double f64;
        } cvt;
        cvt.u64 = u;
        d = cvt.f64;
      }
      snprintf(out, cap, "%.17g", d);
      return 1;
    }
  }
  return 0;
}

static int cheng_cbor_lookup_map_value_text(const uint8_t* bytes, size_t len, const char* key, char* out, size_t cap) {
  if (out == NULL || cap == 0u) return 0;
  out[0] = '\0';
  if (bytes == NULL || len == 0u || key == NULL || key[0] == '\0') return 0;

  ChengCborReader rd;
  rd.bytes = bytes;
  rd.len = len;
  rd.pos = 0u;
  rd.depth = 0;

  uint8_t ib = 0;
  if (!cheng_cbor_read_u8(&rd, &ib)) return 0;
  uint8_t major = (uint8_t)(ib >> 5);
  uint8_t ai = (uint8_t)(ib & 0x1Fu);
  if (major != 5u) return 0;

  if (ai == 31u) {
    for (;;) {
      if (rd.pos >= rd.len) return 0;
      uint8_t kb = rd.bytes[rd.pos];
      if (kb == 0xFFu) {
        rd.pos++;
        return 0;
      }
      if (!cheng_cbor_read_u8(&rd, &kb)) return 0;
      char parsed_key[CBOR_BUILDER_KEY_CAP];
      if (!cheng_cbor_item_to_text(&rd, kb, parsed_key, sizeof(parsed_key))) return 0;
      uint8_t vb = 0;
      if (!cheng_cbor_read_u8(&rd, &vb)) return 0;
      if (strcmp(parsed_key, key) == 0) {
        return cheng_cbor_item_to_text(&rd, vb, out, cap);
      }
      if (!cheng_cbor_skip_item_with_head(&rd, vb)) return 0;
    }
  }

  uint64_t count = 0;
  if (!cheng_cbor_read_arg(&rd, ai, &count)) return 0;
  for (uint64_t i = 0; i < count; ++i) {
    uint8_t kb = 0;
    if (!cheng_cbor_read_u8(&rd, &kb)) return 0;
    char parsed_key[CBOR_BUILDER_KEY_CAP];
    if (!cheng_cbor_item_to_text(&rd, kb, parsed_key, sizeof(parsed_key))) return 0;
    uint8_t vb = 0;
    if (!cheng_cbor_read_u8(&rd, &vb)) return 0;
    if (strcmp(parsed_key, key) == 0) {
      return cheng_cbor_item_to_text(&rd, vb, out, cap);
    }
    if (!cheng_cbor_skip_item_with_head(&rd, vb)) return 0;
  }
  return 0;
}

static int cheng_cbor_read_map_entry_text(
    ChengCborReader* rd,
    char* outKey,
    size_t keyCap,
    char* outValue,
    size_t valueCap) {
  if (rd == NULL || outKey == NULL || outValue == NULL || keyCap == 0u || valueCap == 0u) {
    return 0;
  }
  outKey[0] = '\0';
  outValue[0] = '\0';
  uint8_t kb = 0;
  if (!cheng_cbor_read_u8(rd, &kb)) {
    return 0;
  }
  if (!cheng_cbor_item_to_text(rd, kb, outKey, keyCap)) {
    return 0;
  }
  uint8_t vb = 0;
  if (!cheng_cbor_read_u8(rd, &vb)) {
    return 0;
  }
  size_t value_pos = rd->pos;
  if (!cheng_cbor_item_to_text(rd, vb, outValue, valueCap)) {
    rd->pos = value_pos;
    outValue[0] = '\0';
    if (!cheng_cbor_skip_item_with_head(rd, vb)) {
      return 0;
    }
  }
  return 1;
}

static int cheng_cbor_map_entry_at(
    const uint8_t* bytes,
    size_t len,
    int targetIndex,
    char* outKey,
    size_t keyCap,
    char* outValue,
    size_t valueCap,
    int* outCount) {
  if (bytes == NULL || len == 0u) {
    return 0;
  }
  if (targetIndex < 0) {
    if (outCount == NULL) {
      return 0;
    }
  } else if (outKey == NULL || outValue == NULL || keyCap == 0u || valueCap == 0u) {
    return 0;
  }
  if (outCount != NULL) {
    *outCount = 0;
  }

  ChengCborReader rd;
  rd.bytes = bytes;
  rd.len = len;
  rd.pos = 0u;
  rd.depth = 0;

  uint8_t ib = 0;
  if (!cheng_cbor_read_u8(&rd, &ib)) {
    return 0;
  }
  uint8_t major = (uint8_t)(ib >> 5);
  uint8_t ai = (uint8_t)(ib & 0x1Fu);
  if (major != 5u) {
    return 0;
  }

  int count = 0;
  if (ai == 31u) {
    for (;;) {
      if (rd.pos >= rd.len) {
        return 0;
      }
      uint8_t b = rd.bytes[rd.pos];
      if (b == 0xFFu) {
        rd.pos += 1u;
        if (outCount != NULL) {
          *outCount = count;
        }
        if (rd.pos != rd.len) {
          return 0;
        }
        return targetIndex < 0 ? 1 : 0;
      }
      char key[CBOR_BUILDER_KEY_CAP];
      char value[CBOR_BUILDER_VALUE_CAP];
      if (!cheng_cbor_read_map_entry_text(&rd, key, sizeof(key), value, sizeof(value))) {
        return 0;
      }
      if (targetIndex >= 0 && count == targetIndex) {
        cheng_copy_cstr(key, outKey, keyCap);
        cheng_copy_cstr(value, outValue, valueCap);
        if (outCount != NULL) {
          *outCount = count + 1;
        }
        return 1;
      }
      count += 1;
    }
  }

  uint64_t pair_count = 0u;
  if (!cheng_cbor_read_arg(&rd, ai, &pair_count)) {
    return 0;
  }
  if (pair_count > INT32_MAX) {
    return 0;
  }
  for (int i = 0; i < (int)pair_count; ++i) {
    char key[CBOR_BUILDER_KEY_CAP];
    char value[CBOR_BUILDER_VALUE_CAP];
    if (!cheng_cbor_read_map_entry_text(&rd, key, sizeof(key), value, sizeof(value))) {
      return 0;
    }
    if (targetIndex >= 0 && i == targetIndex) {
      cheng_copy_cstr(key, outKey, keyCap);
      cheng_copy_cstr(value, outValue, valueCap);
      if (outCount != NULL) {
        *outCount = i + 1;
      }
      return 1;
    }
    count += 1;
  }

  if (outCount != NULL) {
    *outCount = count;
  }
  if (rd.pos != rd.len) {
    return 0;
  }
  return targetIndex < 0 ? 1 : 0;
}

static CHENG_MOBILE_MAYBE_UNUSED int cheng_parse_i64_strict(const char* text, int64_t* out) {
  if (text == NULL || text[0] == '\0' || out == NULL) return 0;
  char* end = NULL;
  long long v = strtoll(text, &end, 10);
  if (end == text || *end != '\0') return 0;
  *out = (int64_t)v;
  return 1;
}

static size_t cheng_cbor_encode_builder_payload(const ChengCborBuilder* slot, uint8_t* out, size_t cap) {
  if (out == NULL || cap == 0u) return 0u;
  uint64_t count = 0u;
  if (slot != NULL && slot->count > 0) {
    count = (uint64_t)slot->count;
  }
  size_t w = cheng_cbor_write_type_len(5u, count, out, cap);
  if (w == 0u) return 0u;
  if (slot == NULL || slot->count <= 0) return w;

  for (int i = 0; i < slot->count; ++i) {
    size_t n = cheng_cbor_write_text(slot->key[i], out + w, cap - w);
    if (n == 0u) return 0u;
    w += n;
    switch (slot->kind[i]) {
      case CHENG_CBOR_ITEM_TEXT:
        n = cheng_cbor_write_text(slot->text[i], out + w, cap - w);
        break;
      case CHENG_CBOR_ITEM_BOOL:
        n = cheng_cbor_write_bool(slot->boolValue[i], out + w, cap - w);
        break;
      case CHENG_CBOR_ITEM_I64:
        n = cheng_cbor_write_int64(slot->i64Value[i], out + w, cap - w);
        break;
      case CHENG_CBOR_ITEM_F64:
        n = cheng_cbor_write_f64(slot->f64Value[i], out + w, cap - w);
        break;
      case CHENG_CBOR_ITEM_NULL:
        n = cheng_cbor_write_null(out + w, cap - w);
        break;
      default:
        return 0u;
    }
    if (n == 0u) return 0u;
    w += n;
  }
  return w;
}

static void cheng_bus_init_once(void) {
  memset(&s_request_queue, 0, sizeof(s_request_queue));
  memset(&s_response_queue, 0, sizeof(s_response_queue));

  for (uint32_t i = 0; i < BUS_QUEUE_CAP; i++) {
    atomic_init(&s_request_queue.slots[i].seq, i);
    atomic_init(&s_response_queue.slots[i].seq, i);
  }
  atomic_init(&s_request_queue.enqueuePos, 0u);
  atomic_init(&s_request_queue.dequeuePos, 0u);
  atomic_init(&s_response_queue.enqueuePos, 0u);
  atomic_init(&s_response_queue.dequeuePos, 0u);
}

static int cheng_bus_queue_push(ChengBusQueue* q, const ChengMobileBusMessage* msg, const void* payload, uint32_t payloadSize) {
  if (q == NULL || msg == NULL) {
    return CME_INVALID_ARG;
  }
  if (payloadSize > BUS_PAYLOAD_CAP) {
    return CME_PAYLOAD_TOO_LARGE;
  }

  ChengBusSlot* slot = NULL;
  uint32_t pos = 0u;

  for (;;) {
    pos = atomic_load_explicit(&q->enqueuePos, memory_order_relaxed);
    slot = &q->slots[pos % BUS_QUEUE_CAP];
    uint32_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
    intptr_t dif = (intptr_t)seq - (intptr_t)pos;
    if (dif == 0) {
      if (atomic_compare_exchange_weak_explicit(
              &q->enqueuePos,
              &pos,
              pos + 1u,
              memory_order_relaxed,
              memory_order_relaxed)) {
        break;
      }
    } else if (dif < 0) {
      return CME_QUEUE_FULL;
    }
  }

  slot->msg = *msg;
  slot->payloadSize = payloadSize;
  if (payload != NULL && payloadSize > 0) {
    memcpy(slot->payload, payload, payloadSize);
  }
  atomic_store_explicit(&slot->seq, pos + 1u, memory_order_release);
  return CME_OK;
}

static int cheng_bus_queue_pop(ChengBusQueue* q, ChengMobileBusMessage* outMsg, void* payloadBuf, uint32_t payloadCap, uint32_t* outPayloadSize) {
  if (q == NULL || outMsg == NULL) {
    return CME_INVALID_ARG;
  }

  ChengBusSlot* slot = NULL;
  uint32_t pos = 0u;

  for (;;) {
    pos = atomic_load_explicit(&q->dequeuePos, memory_order_relaxed);
    slot = &q->slots[pos % BUS_QUEUE_CAP];
    uint32_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
    intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1u);
    if (dif == 0) {
      if (atomic_compare_exchange_weak_explicit(
              &q->dequeuePos,
              &pos,
              pos + 1u,
              memory_order_relaxed,
              memory_order_relaxed)) {
        break;
      }
    } else if (dif < 0) {
      if (outPayloadSize != NULL) {
        *outPayloadSize = 0u;
      }
      return CME_QUEUE_EMPTY;
    }
  }

  const uint32_t payloadSize = slot->payloadSize;
  *outMsg = slot->msg;

  if (outPayloadSize != NULL) {
    *outPayloadSize = payloadSize;
  }

  if (payloadSize > 0u) {
    if (payloadBuf == NULL || payloadCap < payloadSize) {
      atomic_store_explicit(&slot->seq, pos + BUS_QUEUE_CAP, memory_order_release);
      return CME_PAYLOAD_TOO_LARGE;
    }
    memcpy(payloadBuf, slot->payload, payloadSize);
  }

  atomic_store_explicit(&slot->seq, pos + BUS_QUEUE_CAP, memory_order_release);
  return CME_OK;
}

static void cheng_fill_bus_message_defaults(ChengMobileBusMessage* msg) {
  if (msg == NULL) {
    return;
  }
  if (msg->abiVersion == 0u) {
    msg->abiVersion = CHENG_MOBILE_ABI_VERSION;
  }
  if (msg->timeUnixMs == 0u) {
    msg->timeUnixMs = cheng_now_ms();
  }
}

static ChengShmSlot* cheng_shm_find_slot_locked(uint32_t handle) {
  for (uint32_t i = 0; i < SHM_SLOT_CAP; i++) {
    if (s_shm_slots[i].inUse && s_shm_slots[i].handle == handle) {
      return &s_shm_slots[i];
    }
  }
  return NULL;
}

static ChengShmSlot* cheng_shm_alloc_slot_locked(void) {
  for (uint32_t i = 0; i < SHM_SLOT_CAP; i++) {
    if (!s_shm_slots[i].inUse) {
      return &s_shm_slots[i];
    }
  }
  return NULL;
}

#if !defined(__ANDROID__)
static void cheng_event_cache_set(const ChengMobileEvent* ev) {
  pthread_mutex_lock(&s_event_cache_mutex);
  if (ev == NULL) {
    s_event_cache_valid = 0;
    memset(&s_event_cache, 0, sizeof(s_event_cache));
    s_event_cache_text[0] = '\0';
    s_event_cache_message[0] = '\0';
    pthread_mutex_unlock(&s_event_cache_mutex);
    return;
  }
  s_event_cache = *ev;
  if (ev->text != NULL) {
    size_t n = strnlen(ev->text, sizeof(s_event_cache_text) - 1u);
    memcpy(s_event_cache_text, ev->text, n);
    s_event_cache_text[n] = '\0';
    s_event_cache.text = s_event_cache_text;
  } else {
    s_event_cache_text[0] = '\0';
    s_event_cache.text = NULL;
  }
  if (ev->message != NULL) {
    size_t n = strnlen(ev->message, sizeof(s_event_cache_message) - 1u);
    memcpy(s_event_cache_message, ev->message, n);
    s_event_cache_message[n] = '\0';
    s_event_cache.message = s_event_cache_message;
  } else {
    s_event_cache_message[0] = '\0';
    s_event_cache.message = NULL;
  }
  s_event_cache_valid = 1;
  pthread_mutex_unlock(&s_event_cache_mutex);
}

static ChengMobileEvent cheng_event_cache_get(void) {
  ChengMobileEvent out;
  memset(&out, 0, sizeof(out));
  pthread_mutex_lock(&s_event_cache_mutex);
  if (s_event_cache_valid) {
    out = s_event_cache;
  }
  pthread_mutex_unlock(&s_event_cache_mutex);
  return out;
}
#endif

void cheng_mobile_host_emit_frame_tick(void) {
  ChengMobileEvent ev = {0};
  ev.kind = MRE_FRAME_TICK;
  cheng_mobile_host_core_push(&ev);
}

void cheng_mobile_host_emit_log(const char* message) {
  ChengMobileEvent ev = {0};
  ev.kind = MRE_LOG;
  ev.message = message;
  cheng_mobile_host_core_push(&ev);
}

void cheng_mobile_host_emit_pointer(int windowId, int kind, double x, double y, double dx, double dy, int button, int pointerId, int64_t timeMs) {
  ChengMobileEvent ev = {0};
  ev.kind = kind;
  ev.windowId = windowId;
  ev.pointerX = x;
  ev.pointerY = y;
  ev.pointerDeltaX = dx;
  ev.pointerDeltaY = dy;
  ev.pointerButton = button;
  ev.pointerId = pointerId;
  ev.eventTimeMs = timeMs;
#ifdef __ANDROID__
  if (s_emit_pointer_count < 64) {
    s_emit_pointer_count += 1;
    __android_log_print(
        ANDROID_LOG_INFO,
        HOST_API_TAG,
        "emit pointer#%d kind=%d pid=%d x=%.1f y=%.1f",
        s_emit_pointer_count,
        kind,
        pointerId,
        x,
        y);
  }
#endif
  cheng_mobile_host_core_push(&ev);
}

void cheng_mobile_host_emit_key(int windowId, int keyCode, int down, int repeat) {
  ChengMobileEvent ev = {0};
  ev.kind = down ? MRE_KEY_DOWN : MRE_KEY_UP;
  ev.windowId = windowId;
  ev.keyCode = keyCode;
  ev.keyRepeat = repeat;
  cheng_mobile_host_core_push(&ev);
}

void cheng_mobile_host_emit_text(int windowId, const char* text) {
  ChengMobileEvent ev = {0};
  ev.kind = MRE_TEXT_INPUT;
  ev.windowId = windowId;
  ev.text = text;
  cheng_mobile_host_core_push(&ev);
}

int cheng_mobile_bus_send(const ChengMobileBusMessage* msg, const void* payload, uint32_t payloadSize) {
  if (msg == NULL) {
    return 0;
  }
  pthread_once(&s_bus_once, cheng_bus_init_once);
  ChengMobileBusMessage copy = *msg;
  cheng_fill_bus_message_defaults(&copy);
  const int rc = cheng_bus_queue_push(&s_request_queue, &copy, payload, payloadSize);
  return rc == CME_OK ? 1 : 0;
}

int cheng_mobile_bus_recv(ChengMobileBusMessage* outMsg, void* payloadBuf, uint32_t payloadCap, uint32_t* outPayloadSize) {
  pthread_once(&s_bus_once, cheng_bus_init_once);
  const int rc = cheng_bus_queue_pop(&s_response_queue, outMsg, payloadBuf, payloadCap, outPayloadSize);
  if (rc == CME_QUEUE_EMPTY) {
    return 0;
  }
  if (rc == CME_OK) {
    return 1;
  }
  return rc;
}

int cheng_mobile_host_bus_poll_request(ChengMobileBusMessage* outMsg, void* payloadBuf, uint32_t payloadCap, uint32_t* outPayloadSize) {
  pthread_once(&s_bus_once, cheng_bus_init_once);
  const int rc = cheng_bus_queue_pop(&s_request_queue, outMsg, payloadBuf, payloadCap, outPayloadSize);
  if (rc == CME_QUEUE_EMPTY) {
    return 0;
  }
  if (rc == CME_OK) {
    return 1;
  }
  return rc;
}

int cheng_mobile_host_bus_push_response(const ChengMobileBusMessage* msg, const void* payload, uint32_t payloadSize) {
  if (msg == NULL) {
    return 0;
  }
  pthread_once(&s_bus_once, cheng_bus_init_once);
  ChengMobileBusMessage copy = *msg;
  cheng_fill_bus_message_defaults(&copy);
  const int rc = cheng_bus_queue_push(&s_response_queue, &copy, payload, payloadSize);
  return rc == CME_OK ? 1 : 0;
}

int cheng_mobile_bus_send_envelope(int serviceKind, int methodKind, int flags, const char* requestId, const char* payloadCbor) {
  const char* id = requestId ? requestId : "";
  const char* payload = payloadCbor ? payloadCbor : "";

  char envelope[BUS_ENVELOPE_CAP];
  int wrote = snprintf(envelope, sizeof(envelope), "%s|%d|%d|%d|%s", id, serviceKind, methodKind, flags, payload);
  if (wrote < 0 || (size_t)wrote >= sizeof(envelope)) {
    return 0;
  }

  ChengMobileBusMessage msg;
  memset(&msg, 0, sizeof(msg));
  msg.abiVersion = CHENG_MOBILE_ABI_VERSION;
  msg.messageType = CMB_MSG_REQUEST;
  msg.serviceKind = (uint32_t)(serviceKind < 0 ? 0 : serviceKind);
  msg.methodKind = (uint32_t)(methodKind < 0 ? 0 : methodKind);
  msg.flags = (uint32_t)(flags < 0 ? 0 : flags);
  msg.requestId = cheng_hash_request_id(id);
  msg.errorCode = CME_OK;
  msg.timeUnixMs = cheng_now_ms();

  return cheng_mobile_bus_send(&msg, envelope, (uint32_t)wrote);
}

const char* cheng_mobile_bus_recv_envelope(void) {
  static __thread char out[BUS_ENVELOPE_CAP];
  out[0] = '\0';

  ChengMobileBusMessage msg;
  uint32_t size = 0u;
  const int got = cheng_mobile_bus_recv(&msg, out, (uint32_t)(sizeof(out) - 1u), &size);
  if (got <= 0) {
    return out;
  }
  if (size >= sizeof(out)) {
    size = (uint32_t)sizeof(out) - 1u;
  }
  out[size] = '\0';
  (void)msg;
  return out;
}

const char* cheng_mobile_host_bus_poll_request_envelope(void) {
  static __thread char out[BUS_ENVELOPE_CAP];
  out[0] = '\0';

  ChengMobileBusMessage msg;
  uint32_t size = 0u;
  const int got = cheng_mobile_host_bus_poll_request(&msg, out, (uint32_t)(sizeof(out) - 1u), &size);
  if (got <= 0) {
    return out;
  }
  if (size >= sizeof(out)) {
    size = (uint32_t)sizeof(out) - 1u;
  }
  out[size] = '\0';
  (void)msg;
  return out;
}

int cheng_mobile_host_bus_push_response_envelope(const char* responseEnvelope) {
  const char* envelope = responseEnvelope ? responseEnvelope : "";

  char id[128];
  size_t idLen = 0;
  while (envelope[idLen] != '\0' && envelope[idLen] != '|' && idLen + 1 < sizeof(id)) {
    id[idLen] = envelope[idLen];
    idLen += 1;
  }
  id[idLen] = '\0';

  ChengMobileBusMessage msg;
  memset(&msg, 0, sizeof(msg));
  msg.abiVersion = CHENG_MOBILE_ABI_VERSION;
  msg.messageType = CMB_MSG_RESPONSE;
  msg.serviceKind = CMSK_NONE;
  msg.methodKind = 0u;
  msg.flags = 0u;
  msg.requestId = cheng_hash_request_id(id);
  msg.errorCode = CME_OK;
  msg.timeUnixMs = cheng_now_ms();

  const uint32_t size = (uint32_t)strnlen(envelope, BUS_PAYLOAD_CAP);
  return cheng_mobile_host_bus_push_response(&msg, envelope, size);
}

static int cheng_cbor_decode_cached(const char* payloadCbor, const uint8_t** outBytes, size_t* outLen) {
  if (outBytes == NULL || outLen == NULL) {
    return 0;
  }
  *outBytes = NULL;
  *outLen = 0u;
  const char* payload = payloadCbor ? payloadCbor : "";
  if (strncmp(payload, "cbor64:", 7) != 0) {
    return 0;
  }

  if (s_cbor_decode_cache.valid && strcmp(s_cbor_decode_cache.payload, payload) == 0) {
    if (!s_cbor_decode_cache.decodeOk) {
      return 0;
    }
    *outBytes = s_cbor_decode_cache.cbor;
    *outLen = s_cbor_decode_cache.cborLen;
    return 1;
  }

  const size_t payload_len = strnlen(payload, sizeof(s_cbor_decode_cache.payload));
  if (payload_len >= sizeof(s_cbor_decode_cache.payload)) {
    s_cbor_decode_cache.valid = 0;
    s_cbor_decode_cache.decodeOk = 0;
    return 0;
  }
  memcpy(s_cbor_decode_cache.payload, payload, payload_len);
  s_cbor_decode_cache.payload[payload_len] = '\0';
  s_cbor_decode_cache.valid = 1;

  const char* encoded = s_cbor_decode_cache.payload + 7;
  const size_t n = cheng_base64url_decode(encoded, s_cbor_decode_cache.cbor, sizeof(s_cbor_decode_cache.cbor));
  if (n == 0u && encoded[0] != '\0') {
    s_cbor_decode_cache.decodeOk = 0;
    s_cbor_decode_cache.cborLen = 0u;
    return 0;
  }

  s_cbor_decode_cache.decodeOk = 1;
  s_cbor_decode_cache.cborLen = n;
  *outBytes = s_cbor_decode_cache.cbor;
  *outLen = n;
  return 1;
}

int cheng_mobile_cbor_builder_begin(void) {
  ChengCborBuilder* slot = cheng_cbor_builder_alloc_slot();
  if (slot == NULL) return 0;
  return slot->handle;
}

int cheng_mobile_cbor_builder_put_text(int handle, const char* key, const char* value) {
  ChengCborBuilder* slot = cheng_cbor_builder_find_slot(handle);
  int idx = cheng_cbor_builder_put_item_common(slot, key);
  if (idx < 0) return 0;
  slot->kind[idx] = CHENG_CBOR_ITEM_TEXT;
  cheng_copy_cstr(value, slot->text[idx], sizeof(slot->text[idx]));
  return 1;
}

int cheng_mobile_cbor_builder_put_bool(int handle, const char* key, int value) {
  ChengCborBuilder* slot = cheng_cbor_builder_find_slot(handle);
  int idx = cheng_cbor_builder_put_item_common(slot, key);
  if (idx < 0) return 0;
  slot->kind[idx] = CHENG_CBOR_ITEM_BOOL;
  slot->boolValue[idx] = value ? 1 : 0;
  return 1;
}

int cheng_mobile_cbor_builder_put_i64(int handle, const char* key, int64_t value) {
  ChengCborBuilder* slot = cheng_cbor_builder_find_slot(handle);
  int idx = cheng_cbor_builder_put_item_common(slot, key);
  if (idx < 0) return 0;
  slot->kind[idx] = CHENG_CBOR_ITEM_I64;
  slot->i64Value[idx] = value;
  return 1;
}

int cheng_mobile_cbor_builder_put_f64(int handle, const char* key, double value) {
  ChengCborBuilder* slot = cheng_cbor_builder_find_slot(handle);
  int idx = cheng_cbor_builder_put_item_common(slot, key);
  if (idx < 0) return 0;
  slot->kind[idx] = CHENG_CBOR_ITEM_F64;
  slot->f64Value[idx] = value;
  return 1;
}

int cheng_mobile_cbor_builder_put_null(int handle, const char* key) {
  ChengCborBuilder* slot = cheng_cbor_builder_find_slot(handle);
  int idx = cheng_cbor_builder_put_item_common(slot, key);
  if (idx < 0) return 0;
  slot->kind[idx] = CHENG_CBOR_ITEM_NULL;
  return 1;
}

const char* cheng_mobile_cbor_builder_finish(int handle) {
  static __thread char out[BUS_ENVELOPE_CAP];
  out[0] = '\0';
  ChengCborBuilder* slot = cheng_cbor_builder_find_slot(handle);
  if (slot == NULL) return out;

  uint8_t cbor[BUS_PAYLOAD_CAP];
  const size_t cbor_len = cheng_cbor_encode_builder_payload(slot, cbor, sizeof(cbor));
  if (cbor_len == 0u) {
    cheng_cbor_builder_reset_slot(slot);
    return out;
  }
  char encoded[BUS_ENVELOPE_CAP];
  const size_t enc_len = cheng_base64url_encode(cbor, cbor_len, encoded, sizeof(encoded));
  if (enc_len == 0u && cbor_len != 0u) {
    cheng_cbor_builder_reset_slot(slot);
    return out;
  }
  if (enc_len + 7u >= sizeof(out)) {
    cheng_cbor_builder_reset_slot(slot);
    return out;
  }
  memcpy(out, "cbor64:", 7u);
  if (enc_len > 0u) {
    memcpy(out + 7u, encoded, enc_len);
  }
  out[7u + enc_len] = '\0';
  cheng_cbor_builder_reset_slot(slot);
  return out;
}

const char* cheng_mobile_cbor_get_text(const char* payloadCbor, const char* key) {
  static __thread char out[CBOR_BUILDER_VALUE_CAP];
  out[0] = '\0';
  const char* k = key ? key : "";
  if (k[0] == '\0') return out;
  const uint8_t* cbor = NULL;
  size_t n = 0u;
  if (!cheng_cbor_decode_cached(payloadCbor, &cbor, &n)) {
    return out;
  }
  (void)cheng_cbor_lookup_map_value_text(cbor, n, k, out, sizeof(out));
  return out;
}

int cheng_mobile_cbor_has_key(const char* payloadCbor, const char* key) {
  const char* k = key ? key : "";
  char tmp[CBOR_BUILDER_VALUE_CAP];
  tmp[0] = '\0';
  if (k[0] == '\0') return 0;
  const uint8_t* cbor = NULL;
  size_t n = 0u;
  if (!cheng_cbor_decode_cached(payloadCbor, &cbor, &n)) {
    return 0;
  }
  return cheng_cbor_lookup_map_value_text(cbor, n, k, tmp, sizeof(tmp));
}

int cheng_mobile_cbor_is_valid_map(const char* payloadCbor) {
  const uint8_t* cbor = NULL;
  size_t n = 0u;
  if (!cheng_cbor_decode_cached(payloadCbor, &cbor, &n)) {
    return 0;
  }
  int count = 0;
  return cheng_cbor_map_entry_at(cbor, n, -1, NULL, 0u, NULL, 0u, &count);
}

int cheng_mobile_cbor_map_count(const char* payloadCbor) {
  const uint8_t* cbor = NULL;
  size_t n = 0u;
  if (!cheng_cbor_decode_cached(payloadCbor, &cbor, &n)) {
    return -1;
  }
  int count = 0;
  if (!cheng_cbor_map_entry_at(cbor, n, -1, NULL, 0u, NULL, 0u, &count)) {
    return -1;
  }
  return count;
}

const char* cheng_mobile_cbor_map_key_at(const char* payloadCbor, int index) {
  static __thread char key[CBOR_BUILDER_KEY_CAP];
  static __thread char value[CBOR_BUILDER_VALUE_CAP];
  key[0] = '\0';
  value[0] = '\0';
  if (index < 0) {
    return key;
  }
  const uint8_t* cbor = NULL;
  size_t n = 0u;
  if (!cheng_cbor_decode_cached(payloadCbor, &cbor, &n)) {
    return key;
  }
  if (!cheng_cbor_map_entry_at(cbor, n, index, key, sizeof(key), value, sizeof(value), NULL)) {
    key[0] = '\0';
  }
  return key;
}

const char* cheng_mobile_cbor_map_value_text_at(const char* payloadCbor, int index) {
  static __thread char key[CBOR_BUILDER_KEY_CAP];
  static __thread char value[CBOR_BUILDER_VALUE_CAP];
  key[0] = '\0';
  value[0] = '\0';
  if (index < 0) {
    return value;
  }
  const uint8_t* cbor = NULL;
  size_t n = 0u;
  if (!cheng_cbor_decode_cached(payloadCbor, &cbor, &n)) {
    return value;
  }
  if (!cheng_cbor_map_entry_at(cbor, n, index, key, sizeof(key), value, sizeof(value), NULL)) {
    value[0] = '\0';
  }
  return value;
}

int cheng_mobile_shm_alloc(uint64_t sizeBytes, uint32_t usageFlags, ChengMobileSharedBufferDesc* outDesc) {
  if (outDesc == NULL || sizeBytes == 0u) {
    return 0;
  }

  pthread_mutex_lock(&s_shm_mutex);
  ChengShmSlot* slot = cheng_shm_alloc_slot_locked();
  if (slot == NULL) {
    pthread_mutex_unlock(&s_shm_mutex);
    return 0;
  }

  uint8_t* bytes = (uint8_t*)malloc((size_t)sizeBytes);
  if (bytes == NULL) {
    pthread_mutex_unlock(&s_shm_mutex);
    return 0;
  }

  memset(bytes, 0, (size_t)sizeBytes);
  const uint32_t handle = atomic_fetch_add_explicit(&s_shm_next_handle, 1u, memory_order_relaxed);

  slot->inUse = 1;
  slot->handle = handle == 0u ? 1u : handle;
  slot->bytes = bytes;
  slot->sizeBytes = sizeBytes;
  slot->usageFlags = usageFlags;
  slot->mapCount = 0u;

  memset(outDesc, 0, sizeof(*outDesc));
  outDesc->handle = slot->handle;
  outDesc->usageFlags = usageFlags;
  outDesc->sizeBytes = sizeBytes;
  outDesc->strideBytes = 0u;
  outDesc->pixelFormat = 0u;

  pthread_mutex_unlock(&s_shm_mutex);
  return 1;
}

int cheng_mobile_shm_free(uint32_t handle) {
  pthread_mutex_lock(&s_shm_mutex);
  ChengShmSlot* slot = cheng_shm_find_slot_locked(handle);
  if (slot == NULL) {
    pthread_mutex_unlock(&s_shm_mutex);
    return 0;
  }
  if (slot->mapCount > 0u) {
    pthread_mutex_unlock(&s_shm_mutex);
    return 0;
  }

  free(slot->bytes);
  memset(slot, 0, sizeof(*slot));
  pthread_mutex_unlock(&s_shm_mutex);
  return 1;
}

void* cheng_mobile_shm_map(uint32_t handle, uint64_t offsetBytes, uint64_t lengthBytes) {
  pthread_mutex_lock(&s_shm_mutex);
  ChengShmSlot* slot = cheng_shm_find_slot_locked(handle);
  if (slot == NULL || slot->bytes == NULL) {
    pthread_mutex_unlock(&s_shm_mutex);
    return NULL;
  }
  if (offsetBytes > slot->sizeBytes) {
    pthread_mutex_unlock(&s_shm_mutex);
    return NULL;
  }
  if (lengthBytes > 0u && offsetBytes + lengthBytes > slot->sizeBytes) {
    pthread_mutex_unlock(&s_shm_mutex);
    return NULL;
  }
  slot->mapCount += 1u;
  uint8_t* ptr = slot->bytes + (size_t)offsetBytes;
  pthread_mutex_unlock(&s_shm_mutex);
  return ptr;
}

int cheng_mobile_shm_unmap(uint32_t handle) {
  pthread_mutex_lock(&s_shm_mutex);
  ChengShmSlot* slot = cheng_shm_find_slot_locked(handle);
  if (slot == NULL) {
    pthread_mutex_unlock(&s_shm_mutex);
    return 0;
  }
  if (slot->mapCount > 0u) {
    slot->mapCount -= 1u;
  }
  pthread_mutex_unlock(&s_shm_mutex);
  return 1;
}

uint64_t cheng_mobile_shm_size_bytes(uint32_t handle) {
  pthread_mutex_lock(&s_shm_mutex);
  ChengShmSlot* slot = cheng_shm_find_slot_locked(handle);
  uint64_t out = slot ? slot->sizeBytes : 0u;
  pthread_mutex_unlock(&s_shm_mutex);
  return out;
}

int cheng_mobile_shm_alloc_handle(int64_t sizeBytes, int32_t usageFlags) {
  if (sizeBytes <= 0) {
    return 0;
  }
  ChengMobileSharedBufferDesc desc;
  if (!cheng_mobile_shm_alloc((uint64_t)sizeBytes, (uint32_t)usageFlags, &desc)) {
    return 0;
  }
  return (int)desc.handle;
}

int64_t cheng_mobile_shm_size_bytes_i64(int32_t handle) {
  if (handle <= 0) {
    return (int64_t)0;
  }
  return (int64_t)cheng_mobile_shm_size_bytes((uint32_t)handle);
}

uint64_t cheng_mobile_capability_query(void) {
  uint64_t caps = 0u;
  caps |= CMCAP_BUS_V2;
  caps |= CMCAP_SHM;
  caps |= CMCAP_PLUGIN_SYSTEM;
  caps |= CMCAP_GPU_TEXTURE_HANDLE;
  caps |= CMCAP_RING_BUFFER_STREAM;
#ifdef __ANDROID__
  caps |= CMCAP_PLATFORM_ANDROID;
  caps |= CMCAP_CAMERA;
  caps |= CMCAP_LOCATION;
  caps |= CMCAP_NFC;
  caps |= CMCAP_SECURE_STORE;
  caps |= CMCAP_NETWORK_STATE;
  caps |= CMCAP_BIOMETRIC_STRONG;
  caps |= CMCAP_NFC_APDU;
  caps |= CMCAP_SECURE_SIGN_USER_PRESENCE;
  caps |= CMCAP_AUDIO_DUPLEX;
  caps |= CMCAP_SENSORS;
  caps |= CMCAP_BLE_CENTRAL;
  caps |= CMCAP_HAPTICS;
  caps |= CMCAP_FLASHLIGHT;
#elif defined(__APPLE__)
  caps |= CMCAP_PLATFORM_IOS;
  caps |= CMCAP_CAMERA;
  caps |= CMCAP_LOCATION;
  caps |= CMCAP_NFC;
  caps |= CMCAP_SECURE_STORE;
  caps |= CMCAP_NETWORK_STATE;
  caps |= CMCAP_BIOMETRIC_STRONG;
  caps |= CMCAP_NFC_APDU;
  caps |= CMCAP_SECURE_SIGN_USER_PRESENCE;
  caps |= CMCAP_AUDIO_DUPLEX;
  caps |= CMCAP_SENSORS;
  caps |= CMCAP_BLE_CENTRAL;
  caps |= CMCAP_HAPTICS;
  caps |= CMCAP_FLASHLIGHT;
#elif defined(__OHOS__) || defined(HARMONYOS) || defined(OHOS)
  caps |= CMCAP_PLATFORM_HARMONY;
#endif
  return caps;
}

int64_t cheng_mobile_capability_query_i64(void) {
  return (int64_t)cheng_mobile_capability_query();
}

void cheng_mobile_host_runtime_mark_started(void) {
  pthread_mutex_lock(&s_runtime_mutex);
  s_runtime.started = 1;
  /* Keep lastError payload for strict runtime probes (route/framehash traces). */
  pthread_mutex_unlock(&s_runtime_mutex);
}

void cheng_mobile_host_runtime_mark_stopped(const char* reason) {
  pthread_mutex_lock(&s_runtime_mutex);
  s_runtime.started = 0;
  if (reason != NULL && reason[0] != '\0') {
    size_t n = strnlen(reason, RUNTIME_ERROR_CAP - 1u);
    memcpy(s_runtime.lastError, reason, n);
    s_runtime.lastError[n] = '\0';
  } else {
    s_runtime.lastError[0] = '\0';
  }
  pthread_mutex_unlock(&s_runtime_mutex);
}

const char* cheng_mobile_host_runtime_state_json(void) {
  static __thread char out[RUNTIME_ERROR_CAP + RUNTIME_LAUNCH_ARGS_CAP + 1024];
  int started = 0;
  char error[RUNTIME_ERROR_CAP];
  char launchKv[RUNTIME_LAUNCH_ARGS_CAP];
  char launchJson[RUNTIME_LAUNCH_ARGS_CAP];
  char sanitized[RUNTIME_ERROR_CAP];
  char sanitizedKv[RUNTIME_LAUNCH_ARGS_CAP];
  char sanitizedJson[RUNTIME_LAUNCH_ARGS_CAP];
  char routeState[128];
  char frameHash[64];
  char semanticAppliedHash[64];
  char token[64];
  char sanitizedRouteState[256];
  uint32_t surfaceWidth = 0u;
  uint32_t surfaceHeight = 0u;
  uint32_t surfaceScaleMilli = 0u;
  uint32_t semanticNodesLoaded = 0u;
  uint32_t semanticNodesAppliedCount = 0u;
  int renderReady = 0;
  error[0] = '\0';
  launchKv[0] = '\0';
  launchJson[0] = '\0';
  sanitized[0] = '\0';
  sanitizedKv[0] = '\0';
  sanitizedJson[0] = '\0';
  routeState[0] = '\0';
  frameHash[0] = '\0';
  semanticAppliedHash[0] = '\0';
  token[0] = '\0';
  sanitizedRouteState[0] = '\0';

  pthread_mutex_lock(&s_runtime_mutex);
  started = s_runtime.started;
  size_t n = strnlen(s_runtime.lastError, RUNTIME_ERROR_CAP - 1u);
  memcpy(error, s_runtime.lastError, n);
  error[n] = '\0';
  size_t kv_n = strnlen(s_runtime.launchArgsKv, RUNTIME_LAUNCH_ARGS_CAP - 1u);
  memcpy(launchKv, s_runtime.launchArgsKv, kv_n);
  launchKv[kv_n] = '\0';
  size_t js_n = strnlen(s_runtime.launchArgsJson, RUNTIME_LAUNCH_ARGS_CAP - 1u);
  memcpy(launchJson, s_runtime.launchArgsJson, js_n);
  launchJson[js_n] = '\0';
  pthread_mutex_unlock(&s_runtime_mutex);

  (void)n;
  (void)kv_n;
  (void)js_n;

  if (cheng_runtime_reason_get_token(error, "route", routeState, sizeof(routeState))) {
    /* routeState already set */
  }
  if (cheng_runtime_reason_get_token(error, "framehash", frameHash, sizeof(frameHash))) {
    /* frameHash already set */
  }
  if (cheng_runtime_reason_get_token(error, "sah", semanticAppliedHash, sizeof(semanticAppliedHash))) {
    /* semanticAppliedHash already set */
  }
  if (cheng_runtime_reason_get_token(error, "st", token, sizeof(token))) {
    semanticNodesLoaded = cheng_runtime_token_truthy(token) ? 1u : 0u;
  }
  if (cheng_runtime_reason_get_token(error, "sa", token, sizeof(token))) {
    uint32_t parsed = 0u;
    if (cheng_runtime_token_to_u32(token, &parsed)) semanticNodesAppliedCount = parsed;
  }
  if (cheng_runtime_reason_get_token(error, "sr", token, sizeof(token))) {
    renderReady = cheng_runtime_token_truthy(token) ? 1 : 0;
  }
  if (cheng_runtime_reason_get_token(error, "w", token, sizeof(token))) {
    uint32_t parsed = 0u;
    if (cheng_runtime_token_to_u32(token, &parsed)) surfaceWidth = parsed;
  }
  if (cheng_runtime_reason_get_token(error, "h", token, sizeof(token))) {
    uint32_t parsed = 0u;
    if (cheng_runtime_token_to_u32(token, &parsed)) surfaceHeight = parsed;
  }
  if (cheng_runtime_reason_get_token(error, "scm", token, sizeof(token))) {
    uint32_t parsed = 0u;
    if (cheng_runtime_token_to_u32(token, &parsed)) surfaceScaleMilli = parsed;
  }
#ifdef __ANDROID__
  if (surfaceWidth == 0u) {
    int w = cheng_mobile_host_android_width();
    if (w > 0) surfaceWidth = (uint32_t)w;
  }
  if (surfaceHeight == 0u) {
    int h = cheng_mobile_host_android_height();
    if (h > 0) surfaceHeight = (uint32_t)h;
  }
  if (surfaceScaleMilli == 0u) {
    double scale = cheng_mobile_host_android_density_scale();
    if (scale > 0.0) {
      uint32_t milli = (uint32_t)(scale * 1000.0 + 0.5);
      if (milli > 0u) surfaceScaleMilli = milli;
    }
  }
#endif

  cheng_json_escape_copy(error, sanitized, sizeof(sanitized));
  cheng_json_escape_copy(launchKv, sanitizedKv, sizeof(sanitizedKv));
  cheng_json_escape_copy(launchJson, sanitizedJson, sizeof(sanitizedJson));
  cheng_json_escape_copy(routeState, sanitizedRouteState, sizeof(sanitizedRouteState));

  if (error[0] == '\0') {
    snprintf(
        out,
        sizeof(out),
        "{\"abi_version\":%d,\"native_ready\":true,\"started\":%s,\"last_error\":\"\","
        "\"launch_args_kv\":\"%s\",\"launch_args_json\":\"%s\","
        "\"semantic_nodes_loaded\":%s,\"semantic_nodes_applied_count\":%u,"
        "\"semantic_nodes_applied_hash\":\"%s\",\"last_frame_hash\":\"%s\","
        "\"route_state\":\"%s\",\"render_ready\":%s,"
        "\"surface_width\":%u,\"surface_height\":%u,\"surface_scale_milli\":%u}",
        CHENG_MOBILE_ABI_VERSION,
        started ? "true" : "false",
        sanitizedKv,
        sanitizedJson,
        semanticNodesLoaded ? "true" : "false",
        semanticNodesAppliedCount,
        semanticAppliedHash,
        frameHash,
        sanitizedRouteState,
        renderReady ? "true" : "false",
        surfaceWidth,
        surfaceHeight,
        surfaceScaleMilli);
  } else {
    snprintf(
        out,
        sizeof(out),
        "{\"abi_version\":%d,\"native_ready\":true,\"started\":%s,\"last_error\":\"%s\","
        "\"launch_args_kv\":\"%s\",\"launch_args_json\":\"%s\","
        "\"semantic_nodes_loaded\":%s,\"semantic_nodes_applied_count\":%u,"
        "\"semantic_nodes_applied_hash\":\"%s\",\"last_frame_hash\":\"%s\","
        "\"route_state\":\"%s\",\"render_ready\":%s,"
        "\"surface_width\":%u,\"surface_height\":%u,\"surface_scale_milli\":%u}",
        CHENG_MOBILE_ABI_VERSION,
        started ? "true" : "false",
        sanitized,
        sanitizedKv,
        sanitizedJson,
        semanticNodesLoaded ? "true" : "false",
        semanticNodesAppliedCount,
        semanticAppliedHash,
        frameHash,
        sanitizedRouteState,
        renderReady ? "true" : "false",
        surfaceWidth,
        surfaceHeight,
        surfaceScaleMilli);
  }
  return out;
}

void cheng_mobile_host_runtime_set_launch_args(const char* argsKv, const char* argsJson) {
  pthread_mutex_lock(&s_runtime_mutex);
  if (argsKv != NULL && argsKv[0] != '\0') {
    size_t n = strnlen(argsKv, RUNTIME_LAUNCH_ARGS_CAP - 1u);
    memcpy(s_runtime.launchArgsKv, argsKv, n);
    s_runtime.launchArgsKv[n] = '\0';
  } else {
    s_runtime.launchArgsKv[0] = '\0';
  }
  if (argsJson != NULL && argsJson[0] != '\0') {
    size_t n = strnlen(argsJson, RUNTIME_LAUNCH_ARGS_CAP - 1u);
    memcpy(s_runtime.launchArgsJson, argsJson, n);
    s_runtime.launchArgsJson[n] = '\0';
  } else {
    s_runtime.launchArgsJson[0] = '\0';
  }
  pthread_mutex_unlock(&s_runtime_mutex);
}

const char* cheng_mobile_host_runtime_launch_args_kv(void) {
  static __thread char out[RUNTIME_LAUNCH_ARGS_CAP];
  pthread_mutex_lock(&s_runtime_mutex);
  size_t n = strnlen(s_runtime.launchArgsKv, RUNTIME_LAUNCH_ARGS_CAP - 1u);
  memcpy(out, s_runtime.launchArgsKv, n);
  out[n] = '\0';
  pthread_mutex_unlock(&s_runtime_mutex);
  return out;
}

const char* cheng_mobile_host_runtime_launch_args_json(void) {
  static __thread char out[RUNTIME_LAUNCH_ARGS_CAP];
  pthread_mutex_lock(&s_runtime_mutex);
  size_t n = strnlen(s_runtime.launchArgsJson, RUNTIME_LAUNCH_ARGS_CAP - 1u);
  memcpy(out, s_runtime.launchArgsJson, n);
  out[n] = '\0';
  pthread_mutex_unlock(&s_runtime_mutex);
  return out;
}

#if !defined(__ANDROID__)
int cheng_mobile_host_poll_event_cached(void) {
  ChengMobileEvent ev;
  if (!cheng_mobile_host_poll_event(&ev)) {
    cheng_event_cache_set(NULL);
    return 0;
  }
  cheng_event_cache_set(&ev);
  return 1;
}

int cheng_mobile_host_cached_event_kind(void) {
  return cheng_event_cache_get().kind;
}

int cheng_mobile_host_cached_event_window_id(void) {
  return cheng_event_cache_get().windowId;
}

double cheng_mobile_host_cached_event_pointer_x(void) {
  return cheng_event_cache_get().pointerX;
}

double cheng_mobile_host_cached_event_pointer_y(void) {
  return cheng_event_cache_get().pointerY;
}

double cheng_mobile_host_cached_event_pointer_delta_x(void) {
  return cheng_event_cache_get().pointerDeltaX;
}

double cheng_mobile_host_cached_event_pointer_delta_y(void) {
  return cheng_event_cache_get().pointerDeltaY;
}

int cheng_mobile_host_cached_event_pointer_x_i32(void) {
  return (int)cheng_event_cache_get().pointerX;
}

int cheng_mobile_host_cached_event_pointer_y_i32(void) {
  return (int)cheng_event_cache_get().pointerY;
}

int cheng_mobile_host_cached_event_pointer_delta_x_i32(void) {
  return (int)cheng_event_cache_get().pointerDeltaX;
}

int cheng_mobile_host_cached_event_pointer_delta_y_i32(void) {
  return (int)cheng_event_cache_get().pointerDeltaY;
}

int cheng_mobile_host_cached_event_pointer_button(void) {
  return cheng_event_cache_get().pointerButton;
}

int cheng_mobile_host_cached_event_pointer_id(void) {
  return cheng_event_cache_get().pointerId;
}

int64_t cheng_mobile_host_cached_event_time_ms(void) {
  return cheng_event_cache_get().eventTimeMs;
}

int cheng_mobile_host_cached_event_key_code(void) {
  return cheng_event_cache_get().keyCode;
}

int cheng_mobile_host_cached_event_key_repeat(void) {
  return cheng_event_cache_get().keyRepeat;
}

const char* cheng_mobile_host_cached_event_text(void) {
  return cheng_event_cache_get().text;
}

const char* cheng_mobile_host_cached_event_message(void) {
  return cheng_event_cache_get().message;
}
#endif
