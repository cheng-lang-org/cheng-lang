#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "system_helpers.h"
#if defined(__APPLE__)
#include <crt_externs.h>
#endif

static int32_t cheng_saved_argc = 0;
static const char **cheng_saved_argv = NULL;

static ChengStrBridge cheng_str_bridge_from_ptr_flags_local(const char *ptr, int32_t flags) {
  ChengStrBridge out;
  out.ptr = ptr != NULL ? ptr : "";
  out.len = (int32_t)strlen(out.ptr);
  out.store_id = 0;
  out.flags = flags;
  return out;
}

static ChengStrBridge cheng_str_bridge_from_owned_local(char *ptr) {
  return cheng_str_bridge_from_ptr_flags_local((const char *)ptr, CHENG_STR_BRIDGE_FLAG_OWNED);
}

void __cheng_setCmdLine(int32_t argc, const char **argv) {
  if (argc <= 0 || argc > 4096 || argv == NULL) {
    cheng_saved_argc = 0;
    cheng_saved_argv = NULL;
    return;
  }
  cheng_saved_argc = argc;
  cheng_saved_argv = argv;
}

int32_t __cheng_rt_paramCount(void) {
  if (cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL) {
    return cheng_saved_argc;
  }
#if defined(__APPLE__)
  int *argc_ptr = _NSGetArgc();
  if (argc_ptr == NULL) {
    return 0;
  }
  int argc = *argc_ptr;
  if (argc <= 0 || argc > 4096) {
    return 0;
  }
  return (int32_t)argc;
#else
  return 0;
#endif
}

const char * __cheng_rt_paramStr(int32_t i) {
  if (i >= 0 && cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL &&
      i < cheng_saved_argc) {
    const char *s = cheng_saved_argv[i];
    return s != NULL ? s : "";
  }
#if defined(__APPLE__)
  if (i < 0) {
    return "";
  }
  int *argc_ptr = _NSGetArgc();
  char ***argv_ptr = _NSGetArgv();
  if (argc_ptr == NULL || argv_ptr == NULL || *argv_ptr == NULL) {
    return "";
  }
  int argc = *argc_ptr;
  if (argc <= 0 || argc > 4096 || i >= argc) {
    return "";
  }
  char *s = (*argv_ptr)[i];
  return s != NULL ? s : "";
#else
  (void)i;
  return "";
#endif
}

char * __cheng_rt_paramStrCopy(int32_t i) {
  const char *s = __cheng_rt_paramStr(i);
  if (s == NULL) {
    s = "";
  }
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) {
    return (char *)"";
  }
  if (n > 0) {
    memcpy(out, s, n);
  }
  out[n] = '\0';
  return out;
}

void __cheng_rt_paramStrCopyBridgeInto(int32_t i, ChengStrBridge *out) {
  if (out == NULL) {
    return;
  }
  *out = cheng_str_bridge_from_owned_local(__cheng_rt_paramStrCopy(i));
}

int32_t driver_c_cli_param1_eq_bridge(ChengStrBridge expected) {
  const char *raw = "";
  const char *want = expected.ptr != NULL ? expected.ptr : "";
  size_t raw_len;
  if (__cheng_rt_paramCount() <= 1) {
    return 0;
  }
  raw = __cheng_rt_paramStr(1);
  if (raw == NULL) {
    raw = "";
  }
  if (expected.len < 0) {
    return 0;
  }
  raw_len = strlen(raw);
  if (raw_len != (size_t)expected.len) {
    return 0;
  }
  if (raw_len == 0) {
    return 1;
  }
  return memcmp(raw, want, raw_len) == 0 ? 1 : 0;
}

int32_t driver_c_cli_param1_eq_raw_bridge(const char *expected_ptr, int32_t expected_len) {
  const char *raw = "";
  const char *want = expected_ptr != NULL ? expected_ptr : "";
  size_t raw_len;
  if (__cheng_rt_paramCount() <= 1) {
    return 0;
  }
  raw = __cheng_rt_paramStr(1);
  if (raw == NULL) {
    raw = "";
  }
  if (expected_len < 0) {
    return 0;
  }
  raw_len = strlen(raw);
  if (raw_len != (size_t)expected_len) {
    return 0;
  }
  if (raw_len == 0) {
    return 1;
  }
  return memcmp(raw, want, raw_len) == 0 ? 1 : 0;
}

int32_t driver_c_str_eq_raw_bridge(ChengStrBridge actual, const char *expected_ptr, int32_t expected_len) {
  const char *lhs = actual.ptr != NULL ? actual.ptr : "";
  const char *rhs = expected_ptr != NULL ? expected_ptr : "";
  if (actual.len < 0 || expected_len < 0) {
    return 0;
  }
  if (actual.len != expected_len) {
    return 0;
  }
  if (actual.len == 0) {
    return 1;
  }
  return memcmp(lhs, rhs, (size_t)actual.len) == 0 ? 1 : 0;
}

static int driver_c_flag_key_matches(const char *raw, ChengStrBridge key) {
  size_t key_len;
  if (raw == NULL || key.ptr == NULL || key.len < 0) {
    return 0;
  }
  key_len = (size_t)key.len;
  return strlen(raw) == key_len && memcmp(raw, key.ptr, key_len) == 0;
}

static int driver_c_flag_inline_value(const char *raw, ChengStrBridge key, const char **out_value) {
  size_t key_len;
  if (out_value == NULL) {
    return 0;
  }
  *out_value = NULL;
  if (raw == NULL || key.ptr == NULL || key.len < 0) {
    return 0;
  }
  key_len = (size_t)key.len;
  if (strncmp(raw, key.ptr, key_len) != 0) {
    return 0;
  }
  if (raw[key_len] == ':' || raw[key_len] == '=') {
    *out_value = raw + key_len + 1u;
    return 1;
  }
  return 0;
}

ChengStrBridge driver_c_read_flag_or_default_bridge(ChengStrBridge key, ChengStrBridge default_value) {
  int32_t argc = __cheng_rt_paramCount();
  int32_t i = 0;
  if (argc <= 1) {
    return default_value;
  }
  for (i = 1; i < argc; ++i) {
    const char *raw = __cheng_rt_paramStr(i);
    const char *inline_value = NULL;
    if (raw == NULL) {
      continue;
    }
    if (driver_c_flag_inline_value(raw, key, &inline_value)) {
      return cheng_str_bridge_from_ptr_flags_local(inline_value, 0);
    }
    if (driver_c_flag_key_matches(raw, key)) {
      const char *next_raw = "";
      if (i + 1 >= argc) {
        return default_value;
      }
      next_raw = __cheng_rt_paramStr(i + 1);
      if (next_raw == NULL) {
        next_raw = "";
      }
      return cheng_str_bridge_from_ptr_flags_local(next_raw, 0);
    }
  }
  return default_value;
}

int32_t driver_c_read_int32_flag_or_default_bridge(ChengStrBridge key, int32_t default_value) {
  ChengStrBridge raw = driver_c_read_flag_or_default_bridge(key, (ChengStrBridge){0});
  char *end = NULL;
  long parsed = 0;
  if (raw.ptr == NULL || raw.len <= 0) {
    return default_value;
  }
  parsed = strtol(raw.ptr, &end, 10);
  if (end == raw.ptr || *end != '\0') {
    return default_value;
  }
  if (parsed < -2147483648L || parsed > 2147483647L) {
    return default_value;
  }
  return (int32_t)parsed;
}
