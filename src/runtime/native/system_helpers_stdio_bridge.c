#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#include "system_helpers.h"

extern char **environ;
extern int cheng_v2c_tooling_handle(int argc, char **argv) __attribute__((weak_import));
extern int cheng_v2c_tooling_is_command(const char *cmd) __attribute__((weak_import));

typedef struct DriverCSha256Ctx {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} DriverCSha256Ctx;

typedef struct DriverCNativeStageArtifacts {
  char *compiler_path;
  char *release_path;
  char *system_link_plan_path;
  char *system_link_exec_path;
  char *binary_path;
  char *binary_cid;
  char *output_file_cid;
  int32_t external_cc_provider_count;
  char *compiler_core_provider_source_kind;
  char *compiler_core_provider_compile_mode;
} DriverCNativeStageArtifacts;

static const uint32_t driver_c_sha256_table[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

#define DRIVER_C_SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define DRIVER_C_SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define DRIVER_C_SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define DRIVER_C_SHA256_EP0(x) (DRIVER_C_SHA256_ROTR((x), 2) ^ DRIVER_C_SHA256_ROTR((x), 13) ^ DRIVER_C_SHA256_ROTR((x), 22))
#define DRIVER_C_SHA256_EP1(x) (DRIVER_C_SHA256_ROTR((x), 6) ^ DRIVER_C_SHA256_ROTR((x), 11) ^ DRIVER_C_SHA256_ROTR((x), 25))
#define DRIVER_C_SHA256_SIG0(x) (DRIVER_C_SHA256_ROTR((x), 7) ^ DRIVER_C_SHA256_ROTR((x), 18) ^ ((x) >> 3))
#define DRIVER_C_SHA256_SIG1(x) (DRIVER_C_SHA256_ROTR((x), 17) ^ DRIVER_C_SHA256_ROTR((x), 19) ^ ((x) >> 10))

__attribute__((weak)) char *driver_c_read_file_all(const char *path);

static char *driver_c_bridge_to_cstring(ChengStrBridge value) {
  size_t n = 0;
  char *out = NULL;
  if (value.len > 0) {
    n = (size_t)value.len;
  }
  out = (char *)malloc(n + 1u);
  if (out == NULL) return NULL;
  if (n > 0 && value.ptr != NULL) {
    memcpy(out, value.ptr, n);
  }
  out[n] = '\0';
  return out;
}

static ChengStrBridge driver_c_owned_bridge_from_cstring(char *text) {
  ChengStrBridge out = {0};
  out.ptr = text;
  out.len = text != NULL ? (int32_t)strlen(text) : 0;
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_OWNED;
  return out;
}

static char *driver_c_dup_cstring(const char *text) {
  size_t n = 0;
  char *out = NULL;
  if (text == NULL) {
    text = "";
  }
  n = strlen(text);
  out = (char *)malloc(n + 1u);
  if (out == NULL) return NULL;
  memcpy(out, text, n);
  out[n] = '\0';
  return out;
}

static char *driver_c_dup_range(const char *start, const char *stop) {
  size_t n = 0;
  char *out = NULL;
  if (start == NULL || stop == NULL || stop < start) {
    return driver_c_dup_cstring("");
  }
  n = (size_t)(stop - start);
  out = (char *)malloc(n + 1u);
  if (out == NULL) return NULL;
  if (n > 0) {
    memcpy(out, start, n);
  }
  out[n] = '\0';
  return out;
}

static char *driver_c_find_line_value_dup(const char *text, const char *key) {
  const char *cursor = NULL;
  size_t key_len = 0;
  if (text == NULL || key == NULL || key[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  key_len = strlen(key);
  cursor = text;
  while (*cursor != '\0') {
    const char *line_start = cursor;
    const char *line_stop = cursor;
    while (*line_stop != '\0' && *line_stop != '\n') {
      line_stop = line_stop + 1;
    }
    if ((size_t)(line_stop - line_start) > key_len &&
        memcmp(line_start, key, key_len) == 0 &&
        line_start[key_len] == '=') {
      return driver_c_dup_range(line_start + key_len + 1, line_stop);
    }
    cursor = *line_stop == '\0' ? line_stop : line_stop + 1;
  }
  return driver_c_dup_cstring("");
}

static char *driver_c_provider_field_for_module_dup(const char *plan_text,
                                                    const char *module_name,
                                                    const char *field_name) {
  const char *cursor = NULL;
  const char *name_prefix = "provider_module.";
  size_t name_prefix_len = strlen(name_prefix);
  size_t module_name_len = 0;
  char key_buf[256];
  if (plan_text == NULL || module_name == NULL || field_name == NULL ||
      module_name[0] == '\0' || field_name[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  module_name_len = strlen(module_name);
  cursor = plan_text;
  while (*cursor != '\0') {
    const char *line_start = cursor;
    const char *line_stop = cursor;
    const char *suffix = NULL;
    const char *eq = NULL;
    int idx_len = 0;
    while (*line_stop != '\0' && *line_stop != '\n') {
      line_stop = line_stop + 1;
    }
    if ((size_t)(line_stop - line_start) > name_prefix_len &&
        memcmp(line_start, name_prefix, name_prefix_len) == 0) {
      suffix = line_start + name_prefix_len;
      while (suffix < line_stop && *suffix >= '0' && *suffix <= '9') {
        suffix = suffix + 1;
        idx_len = idx_len + 1;
      }
      if (idx_len > 0 &&
          suffix + 6 < line_stop &&
          memcmp(suffix, ".name=", 6u) == 0 &&
          (size_t)(line_stop - (suffix + 6)) == module_name_len &&
          memcmp(suffix + 6, module_name, module_name_len) == 0 &&
          name_prefix_len + (size_t)idx_len + 1u + strlen(field_name) + 1u < sizeof(key_buf)) {
        memcpy(key_buf, name_prefix, name_prefix_len);
        memcpy(key_buf + name_prefix_len, line_start + name_prefix_len, (size_t)idx_len);
        eq = key_buf + name_prefix_len + (size_t)idx_len;
        * (char *)eq = '.';
        strcpy((char *)eq + 1, field_name);
        strcat(key_buf, "=");
        return driver_c_find_line_value_dup(plan_text, key_buf);
      }
    }
    cursor = *line_stop == '\0' ? line_stop : line_stop + 1;
  }
  return driver_c_dup_cstring("");
}

static char *driver_c_absolute_path_dup(const char *path) {
  char *cwd = NULL;
  char *out = NULL;
  size_t cwd_len = 0;
  size_t path_len = 0;
  if (path == NULL || path[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  if (path[0] == '/') {
    return driver_c_dup_cstring(path);
  }
  cwd = getcwd(NULL, 0);
  if (cwd == NULL || cwd[0] == '\0') {
    free(cwd);
    return driver_c_dup_cstring(path);
  }
  cwd_len = strlen(cwd);
  path_len = strlen(path);
  out = (char *)malloc(cwd_len + 1u + path_len + 1u);
  if (out == NULL) {
    free(cwd);
    return NULL;
  }
  memcpy(out, cwd, cwd_len);
  out[cwd_len] = '/';
  memcpy(out + cwd_len + 1u, path, path_len);
  out[cwd_len + 1u + path_len] = '\0';
  free(cwd);
  return out;
}

static char *driver_c_join_path2_dup(const char *left, const char *right) {
  size_t left_len = 0;
  size_t right_len = 0;
  int need_sep = 0;
  char *out = NULL;
  if (left == NULL || left[0] == '\0') {
    return driver_c_dup_cstring(right);
  }
  if (right == NULL || right[0] == '\0') {
    return driver_c_dup_cstring(left);
  }
  left_len = strlen(left);
  right_len = strlen(right);
  need_sep = left[left_len - 1] != '/';
  out = (char *)malloc(left_len + (size_t)need_sep + right_len + 1u);
  if (out == NULL) return NULL;
  memcpy(out, left, left_len);
  if (need_sep) {
    out[left_len] = '/';
  }
  memcpy(out + left_len + (size_t)need_sep, right, right_len);
  out[left_len + (size_t)need_sep + right_len] = '\0';
  return out;
}

static void driver_c_die(const char *message) {
  fputs(message != NULL ? message : "driver_c fatal", stderr);
  fputc('\n', stderr);
  fflush(stderr);
  exit(1);
}

static void driver_c_die_errno(const char *message, const char *path) {
  if (message != NULL && message[0] != '\0') {
    fputs(message, stderr);
  } else {
    fputs("driver_c errno", stderr);
  }
  if (path != NULL && path[0] != '\0') {
    fputs(": ", stderr);
    fputs(path, stderr);
  }
  if (errno != 0) {
    fputs(": ", stderr);
    fputs(strerror(errno), stderr);
  }
  fputc('\n', stderr);
  fflush(stderr);
  exit(1);
}

__attribute__((weak)) int32_t driver_c_compiler_core_print_usage_bridge(void) {
  static const char *const lines[] = {
      "cheng_v2",
      "usage: cheng_v2 <command>",
      "",
      "commands:",
      "  help                         show this message",
      "  status                       print the current dev-track contract",
      "  <tooling-command>            forward to cheng_tooling_v2",
      "",
      "tooling commands:",
      "  stage-selfhost-host          build stage1 -> stage2 -> stage3 native closure",
      "  tooling-selfhost-host        run the native tooling selfhost orchestration",
      "  release-compile              emit a deterministic release artifact spec",
      "  lsmr-address                 emit a deterministic LSMR address",
      "  lsmr-route-plan              emit a deterministic canonical LSMR route plan",
      "  outline-parse                emit a compiler_core outline parse",
      "  machine-plan                 emit a deterministic machine plan",
      "  obj-image                    emit a deterministic object image",
      "  obj-file                     emit a deterministic object-file byte layout",
      "  system-link-plan             emit a deterministic native system-link plan",
      "  system-link-exec             materialize object files and invoke the deterministic system linker",
      "  resolve-manifest             resolve a deterministic source manifest",
      "  publish-source-manifest      emit a source manifest artifact",
      "  publish-rule-pack            emit a rule-pack artifact",
      "  publish-compiler-rule-pack   emit a compiler rule-pack artifact",
      "  verify-network-selfhost      verify manifest + topology + rule-pack closure",
  };
  size_t i;
  for (i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
    fputs(lines[i], stdout);
    fputc('\n', stdout);
  }
  return 0;
}

__attribute__((weak)) int32_t driver_c_compiler_core_print_status_bridge(void) {
  fputs("cheng_v2: dev-only entry\n", stdout);
  fputs("track=dev\n", stdout);
  fputs("execution=direct_exe\n", stdout);
  fputs("tooling_forwarded=1\n", stdout);
  fputs("tooling_entry=tooling/cheng_tooling_v2\n", stdout);
  fputs("parallel=function_task\n", stdout);
  fputs("soa_index_only=1\n", stdout);
  fputs("infra_surface=1\n", stdout);
  return 0;
}

static int driver_c_starts_with(const char *text, const char *prefix) {
  size_t n = 0;
  if (text == NULL || prefix == NULL) return 0;
  n = strlen(prefix);
  return strncmp(text, prefix, n) == 0;
}

static int driver_c_path_is_dir(const char *path) {
  struct stat st;
  if (path == NULL || path[0] == '\0') return 0;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int driver_c_path_is_file(const char *path) {
  struct stat st;
  if (path == NULL || path[0] == '\0') return 0;
  if (stat(path, &st) != 0) return 0;
  return S_ISREG(st.st_mode) ? 1 : 0;
}

static void driver_c_path_join_buf(char *out,
                                   size_t out_cap,
                                   const char *left,
                                   const char *right) {
  int n = 0;
  if (left == NULL || left[0] == '\0') {
    n = snprintf(out, out_cap, "%s", right != NULL ? right : "");
  } else if (right == NULL || right[0] == '\0') {
    n = snprintf(out, out_cap, "%s", left);
  } else if (left[strlen(left) - 1] == '/') {
    n = snprintf(out, out_cap, "%s%s", left, right);
  } else {
    n = snprintf(out, out_cap, "%s/%s", left, right);
  }
  if (n < 0 || (size_t)n >= out_cap) {
    driver_c_die("driver_c path too long");
  }
}

static void driver_c_parent_dir_inplace(char *path) {
  char *slash = NULL;
  if (path == NULL || path[0] == '\0') return;
  slash = strrchr(path, '/');
  if (slash == NULL) {
    path[0] = '\0';
    return;
  }
  if (slash == path) {
    path[1] = '\0';
    return;
  }
  *slash = '\0';
}

static int driver_c_path_has_prefix(const char *path, const char *prefix) {
  size_t prefix_len = 0;
  if (path == NULL || prefix == NULL) return 0;
  prefix_len = strlen(prefix);
  if (strncmp(path, prefix, prefix_len) != 0) return 0;
  return path[prefix_len] == '\0' || path[prefix_len] == '/';
}

static void driver_c_normalize_relpath_inplace(char *path) {
  char temp[PATH_MAX];
  char *segments[PATH_MAX / 2];
  size_t seg_count = 0;
  char *cursor = NULL;
  size_t i = 0;
  if (path == NULL || path[0] == '\0') return;
  if (strlen(path) >= sizeof(temp)) {
    driver_c_die("driver_c relative path too long");
  }
  memcpy(temp, path, strlen(path) + 1u);
  cursor = temp;
  while (*cursor != '\0') {
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') {
      cursor = cursor + 1;
    }
    if (*cursor == '/') {
      *cursor = '\0';
      cursor = cursor + 1;
    }
    if (start[0] == '\0' || strcmp(start, ".") == 0) {
      continue;
    }
    if (strcmp(start, "..") == 0) {
      if (seg_count > 0 && strcmp(segments[seg_count - 1], "..") != 0) {
        seg_count = seg_count - 1;
      } else {
        segments[seg_count++] = start;
      }
      continue;
    }
    segments[seg_count++] = start;
  }
  path[0] = '\0';
  for (i = 0; i < seg_count; ++i) {
    if (i > 0) {
      if (strlen(path) + 1u >= PATH_MAX) {
        driver_c_die("driver_c normalized relative path too long");
      }
      strcat(path, "/");
    }
    if (strlen(path) + strlen(segments[i]) >= PATH_MAX) {
      driver_c_die("driver_c normalized relative path too long");
    }
    strcat(path, segments[i]);
  }
}

static int driver_c_relative_to_prefix(const char *path,
                                       const char *prefix,
                                       char *out,
                                       size_t out_cap) {
  size_t prefix_len = 0;
  const char *rel = NULL;
  if (!driver_c_path_has_prefix(path, prefix)) return 0;
  prefix_len = strlen(prefix);
  rel = path + prefix_len;
  if (*rel == '/') rel = rel + 1;
  if (snprintf(out, out_cap, "%s", rel) >= (int)out_cap) {
    driver_c_die("driver_c relative path too long");
  }
  driver_c_normalize_relpath_inplace(out);
  return 1;
}

static int driver_c_find_repo_root_from(const char *start_path,
                                        char *out,
                                        size_t out_cap) {
  char probe[PATH_MAX];
  char marker[PATH_MAX];
  if (start_path == NULL || start_path[0] == '\0') return 0;
  if (realpath(start_path, probe) == NULL) return 0;
  if (!driver_c_path_is_dir(probe)) {
    driver_c_parent_dir_inplace(probe);
  }
  while (probe[0] != '\0') {
    driver_c_path_join_buf(marker, sizeof(marker), probe, "v2/cheng-package.toml");
    if (driver_c_path_is_file(marker)) {
      if (snprintf(out, out_cap, "%s", probe) >= (int)out_cap) {
        driver_c_die("driver_c repo root path too long");
      }
      return 1;
    }
    if (strcmp(probe, "/") == 0) break;
    driver_c_parent_dir_inplace(probe);
  }
  return 0;
}

static void driver_c_detect_repo_root(const char *hint_a,
                                      const char *hint_b,
                                      char *out,
                                      size_t out_cap) {
  char cwd[PATH_MAX];
  if (driver_c_find_repo_root_from(hint_a, out, out_cap)) return;
  if (driver_c_find_repo_root_from(hint_b, out, out_cap)) return;
  if (getcwd(cwd, sizeof(cwd)) != NULL && driver_c_find_repo_root_from(cwd, out, out_cap)) {
    return;
  }
  driver_c_die("driver_c failed to detect repo root");
}

static void driver_c_resolve_existing_input_path(const char *repo_root,
                                                 const char *raw,
                                                 char *abs_out,
                                                 size_t abs_out_cap) {
  char cwd[PATH_MAX];
  char probe[PATH_MAX];
  (void)abs_out_cap;
  if (raw == NULL || raw[0] == '\0') {
    driver_c_die("driver_c missing input path");
  }
  if (raw[0] == '/') {
    if (snprintf(probe, sizeof(probe), "%s", raw) >= (int)sizeof(probe)) {
      driver_c_die("driver_c input path too long");
    }
  } else if (driver_c_starts_with(raw, "./") || driver_c_starts_with(raw, "../")) {
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      driver_c_die_errno("driver_c getcwd failed", "");
    }
    driver_c_path_join_buf(probe, sizeof(probe), cwd, raw);
  } else {
    driver_c_path_join_buf(probe, sizeof(probe), repo_root, raw);
  }
  if (realpath(probe, abs_out) == NULL) {
    driver_c_die_errno("driver_c realpath failed", probe);
  }
}

static void driver_c_resolve_output_path(const char *repo_root,
                                         const char *raw,
                                         char *out,
                                         size_t out_cap) {
  char cwd[PATH_MAX];
  if (raw == NULL || raw[0] == '\0') {
    driver_c_die("driver_c missing output path");
  }
  if (raw[0] == '/') {
    if (snprintf(out, out_cap, "%s", raw) >= (int)out_cap) {
      driver_c_die("driver_c output path too long");
    }
    return;
  }
  if (driver_c_starts_with(raw, "./") || driver_c_starts_with(raw, "../")) {
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      driver_c_die_errno("driver_c getcwd failed", "");
    }
    driver_c_path_join_buf(out, out_cap, cwd, raw);
    return;
  }
  driver_c_path_join_buf(out, out_cap, repo_root, raw);
}

static const char *driver_c_cli_arg_raw(int32_t idx) {
  return __cheng_rt_paramStr(idx);
}

static int32_t driver_c_cli_argc(void) {
  return __cheng_rt_paramCount();
}

static const char *driver_c_program_name_raw(void);

static char **driver_c_cli_current_argv_dup(int *out_argc) {
  int32_t param_count = driver_c_cli_argc();
  int argc = (int)param_count;
  int32_t i = 0;
  char **argv;
  if (argc <= 0) {
    argc = 1;
  }
  argv = (char **)malloc(sizeof(char *) * (size_t)(argc + 1));
  if (argv == NULL) return NULL;
  if (param_count <= 0) {
    argv[0] = (char *)driver_c_program_name_raw();
    argv[1] = NULL;
    if (out_argc != NULL) *out_argc = 1;
    return argv;
  }
  for (i = 0; i < param_count; ++i) {
    argv[i] = (char *)driver_c_cli_arg_raw(i);
  }
  argv[argc] = NULL;
  if (out_argc != NULL) *out_argc = argc;
  return argv;
}

static char *driver_c_payload_label_dup(const char *payload) {
  const char *start = NULL;
  const char *stop = NULL;
  if (payload == NULL || payload[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  start = strstr(payload, "|label=");
  if (start != NULL) {
    start = start + 1;
  } else if (strncmp(payload, "label=", 6u) == 0) {
    start = payload;
  } else {
    return driver_c_dup_cstring("");
  }
  start = start + 6;
  stop = start;
  while (*stop != '\0' && *stop != '|') {
    stop = stop + 1;
  }
  return driver_c_dup_range(start, stop);
}

static int driver_c_flag_key_matches_raw(const char *raw, const char *key) {
  size_t key_len = 0;
  if (raw == NULL || key == NULL || key[0] == '\0') return 0;
  key_len = strlen(key);
  return strlen(raw) == key_len && memcmp(raw, key, key_len) == 0;
}

static int driver_c_flag_inline_value_raw(const char *raw,
                                          const char *key,
                                          const char **out_value) {
  size_t key_len = 0;
  if (out_value == NULL) return 0;
  *out_value = NULL;
  if (raw == NULL || key == NULL || key[0] == '\0') return 0;
  key_len = strlen(key);
  if (strncmp(raw, key, key_len) != 0) return 0;
  if (raw[key_len] == ':' || raw[key_len] == '=') {
    *out_value = raw + key_len + 1u;
    return 1;
  }
  return 0;
}

static char *driver_c_read_flag_dup_raw(const char *key, const char *default_value) {
  int32_t argc = driver_c_cli_argc();
  int32_t i = 0;
  if (argc <= 1) {
    return driver_c_dup_cstring(default_value);
  }
  for (i = 1; i < argc; ++i) {
    const char *raw = driver_c_cli_arg_raw(i);
    const char *inline_value = NULL;
    const char *next_raw = NULL;
    if (raw == NULL) continue;
    if (driver_c_flag_inline_value_raw(raw, key, &inline_value)) {
      return driver_c_dup_cstring(inline_value);
    }
    if (!driver_c_flag_key_matches_raw(raw, key)) continue;
    if (i + 1 >= argc) {
      return driver_c_dup_cstring(default_value);
    }
    next_raw = driver_c_cli_arg_raw(i + 1);
    return driver_c_dup_cstring(next_raw != NULL ? next_raw : "");
  }
  return driver_c_dup_cstring(default_value);
}

static int32_t driver_c_read_int32_flag_default_raw(const char *key, int32_t default_value) {
  char *raw = driver_c_read_flag_dup_raw(key, "");
  char *end = NULL;
  long parsed = 0;
  int32_t out = default_value;
  if (raw != NULL && raw[0] != '\0') {
    errno = 0;
    parsed = strtol(raw, &end, 10);
    if (errno == 0 && end != raw && end != NULL && *end == '\0' &&
        parsed >= -2147483648L && parsed <= 2147483647L) {
      out = (int32_t)parsed;
    }
  }
  free(raw);
  return out;
}

static void driver_c_sha256_init(DriverCSha256Ctx *ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667U;
  ctx->state[1] = 0xbb67ae85U;
  ctx->state[2] = 0x3c6ef372U;
  ctx->state[3] = 0xa54ff53aU;
  ctx->state[4] = 0x510e527fU;
  ctx->state[5] = 0x9b05688cU;
  ctx->state[6] = 0x1f83d9abU;
  ctx->state[7] = 0x5be0cd19U;
}

static void driver_c_sha256_transform(DriverCSha256Ctx *ctx, const uint8_t data[]) {
  uint32_t a, b, c, d, e, f, g, h;
  uint32_t m[64];
  uint32_t t1, t2;
  size_t i = 0;
  for (i = 0; i < 16; ++i) {
    m[i] = ((uint32_t)data[i * 4] << 24) |
           ((uint32_t)data[i * 4 + 1] << 16) |
           ((uint32_t)data[i * 4 + 2] << 8) |
           ((uint32_t)data[i * 4 + 3]);
  }
  for (i = 16; i < 64; ++i) {
    m[i] = DRIVER_C_SHA256_SIG1(m[i - 2]) + m[i - 7] +
           DRIVER_C_SHA256_SIG0(m[i - 15]) + m[i - 16];
  }
  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];
  for (i = 0; i < 64; ++i) {
    t1 = h + DRIVER_C_SHA256_EP1(e) + DRIVER_C_SHA256_CH(e, f, g) +
         driver_c_sha256_table[i] + m[i];
    t2 = DRIVER_C_SHA256_EP0(a) + DRIVER_C_SHA256_MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void driver_c_sha256_update(DriverCSha256Ctx *ctx, const uint8_t *data, size_t len) {
  size_t i = 0;
  for (i = 0; i < len; ++i) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == 64) {
      driver_c_sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

static void driver_c_sha256_final(DriverCSha256Ctx *ctx, uint8_t hash[32]) {
  size_t i = ctx->datalen;
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) ctx->data[i++] = 0x00;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64) ctx->data[i++] = 0x00;
    driver_c_sha256_transform(ctx, ctx->data);
    memset(ctx->data, 0, 56);
  }
  ctx->bitlen += (uint64_t)ctx->datalen * 8u;
  ctx->data[63] = (uint8_t)(ctx->bitlen);
  ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
  ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
  ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
  ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
  ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
  ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
  ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
  driver_c_sha256_transform(ctx, ctx->data);
  for (i = 0; i < 4; ++i) {
    hash[i] = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0xffU);
    hash[i + 4] = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0xffU);
    hash[i + 8] = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0xffU);
    hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0xffU);
    hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0xffU);
    hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0xffU);
    hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0xffU);
    hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0xffU);
  }
}

static char *driver_c_sha256_hex_file_dup(const char *path) {
  static const char hex[] = "0123456789abcdef";
  DriverCSha256Ctx ctx;
  uint8_t digest[32];
  uint8_t buf[4096];
  char *out = NULL;
  FILE *f = NULL;
  size_t n = 0;
  size_t i = 0;
  if (path == NULL || path[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  f = fopen(path, "rb");
  if (f == NULL) {
    driver_c_die_errno("driver_c fopen failed", path);
  }
  driver_c_sha256_init(&ctx);
  for (;;) {
    n = fread(buf, 1, sizeof(buf), f);
    if (n > 0) {
      driver_c_sha256_update(&ctx, buf, n);
    }
    if (n < sizeof(buf)) {
      if (ferror(f)) {
        fclose(f);
        driver_c_die_errno("driver_c fread failed", path);
      }
      break;
    }
  }
  fclose(f);
  driver_c_sha256_final(&ctx, digest);
  out = (char *)malloc(65u);
  if (out == NULL) {
    driver_c_die("driver_c malloc failed");
  }
  for (i = 0; i < 32; ++i) {
    out[i * 2] = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 0x0fU];
  }
  out[64] = '\0';
  return out;
}

static const char *driver_c_program_name_raw(void) {
#if defined(__APPLE__)
  char ***argv_ptr = _NSGetArgv();
  if (argv_ptr == NULL || *argv_ptr == NULL || (*argv_ptr)[0] == NULL) {
    return "";
  }
  return (*argv_ptr)[0];
#else
  return "";
#endif
}

static void driver_c_mkdir_parents_if_needed(const char *path) {
  char *buf = NULL;
  char *slash = NULL;
  char *p = NULL;
  if (path == NULL || path[0] == '\0') return;
  buf = (char *)malloc(strlen(path) + 1u);
  if (buf == NULL) return;
  strcpy(buf, path);
  slash = strrchr(buf, '/');
  if (slash == NULL) {
    free(buf);
    return;
  }
  *slash = '\0';
  if (buf[0] == '\0') {
    free(buf);
    return;
  }
  for (p = buf + 1; *p != '\0'; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(buf, 0755);
    *p = '/';
  }
  mkdir(buf, 0755);
  free(buf);
}

static void driver_c_mkdir_all_if_needed(const char *path) {
  char *buf = NULL;
  char *p = NULL;
  if (path == NULL || path[0] == '\0') return;
  buf = driver_c_dup_cstring(path);
  if (buf == NULL) return;
  for (p = buf + 1; *p != '\0'; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(buf, 0755);
    *p = '/';
  }
  mkdir(buf, 0755);
  free(buf);
}

static int32_t driver_c_compare_files_bytes_impl(const char *left_path, const char *right_path) {
  FILE *left = NULL;
  FILE *right = NULL;
  unsigned char left_buf[4096];
  unsigned char right_buf[4096];
  int32_t same = 1;
  if (left_path == NULL || right_path == NULL || left_path[0] == '\0' || right_path[0] == '\0') {
    return 0;
  }
  left = fopen(left_path, "rb");
  if (left == NULL) return 0;
  right = fopen(right_path, "rb");
  if (right == NULL) {
    fclose(left);
    return 0;
  }
  for (;;) {
    size_t left_n = fread(left_buf, 1, sizeof(left_buf), left);
    size_t right_n = fread(right_buf, 1, sizeof(right_buf), right);
    if (left_n != right_n) {
      same = 0;
      break;
    }
    if (left_n == 0u) {
      break;
    }
    if (memcmp(left_buf, right_buf, left_n) != 0) {
      same = 0;
      break;
    }
  }
  fclose(left);
  fclose(right);
  return same;
}

static void driver_c_abort_with_label_output(const char *label, const char *output) {
  if (label != NULL && label[0] != '\0') {
    fputs(label, stderr);
  } else {
    fputs("driver_c bridge failure", stderr);
  }
  if (output != NULL && output[0] != '\0') {
    fputc('\n', stderr);
    fputs(output, stderr);
  }
  fputc('\n', stderr);
  fflush(stderr);
  exit(1);
}

static int64_t driver_c_monotime_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static const char *driver_c_seq_string_item(ChengSeqHeader seq, int32_t idx) {
  if (idx < 0 || idx >= seq.len || seq.buffer == NULL) return "";
  {
    char **items = (char **)seq.buffer;
    const char *value = items[idx];
    return value != NULL ? value : "";
  }
}

static char **driver_c_exec_build_argv(const char *file_path, ChengSeqHeader argv_seq) {
  size_t extra_count = (size_t)(argv_seq.len > 0 ? argv_seq.len : 0);
  size_t i = 0;
  char **argv = (char **)malloc(sizeof(char *) * (extra_count + 2u));
  if (argv == NULL) return NULL;
  argv[0] = (char *)(file_path != NULL ? file_path : "");
  for (i = 0; i < extra_count; ++i) {
    argv[i + 1u] = (char *)driver_c_seq_string_item(argv_seq, (int32_t)i);
  }
  argv[extra_count + 1u] = NULL;
  return argv;
}

static int64_t driver_c_wait_status_timeout(pid_t pid, int32_t timeout_sec) {
  int status = 0;
  int timed_out = 0;
  int term_sent = 0;
  int64_t term_sent_ns = 0;
  int64_t deadline_ns = 0;
  if (timeout_sec > 0) {
    deadline_ns = driver_c_monotime_ns() + (int64_t)timeout_sec * 1000000000LL;
  }
  for (;;) {
    pid_t wait_rc = waitpid(pid, &status, WNOHANG);
    if (wait_rc == pid) {
      break;
    }
    if (wait_rc < 0) {
      status = 0;
      break;
    }
    if (deadline_ns > 0) {
      int64_t now_ns = driver_c_monotime_ns();
      if (now_ns >= deadline_ns) {
        if (!term_sent) {
          kill(pid, SIGTERM);
          term_sent = 1;
          term_sent_ns = now_ns;
        } else if (now_ns - term_sent_ns >= 50000000LL) {
          kill(pid, SIGKILL);
        }
        timed_out = 1;
      }
    }
    usleep(1000);
  }
  if (timed_out) return 124;
  if (WIFEXITED(status)) return (int64_t)WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return (int64_t)(128 + WTERMSIG(status));
  return (int64_t)status;
}

static char *driver_c_exec_file_capture_minimal(const char *file_path,
                                                ChengSeqHeader argv_seq,
                                                const char *working_dir,
                                                int32_t timeout_sec,
                                                int64_t *exit_code) {
  char capture_path[] = "/tmp/cheng_exec_capture.XXXXXX";
  char **argv = NULL;
  int capture_fd = -1;
  pid_t pid = -1;
  char *out = NULL;
  if (exit_code != NULL) *exit_code = -1;
  if (file_path == NULL || file_path[0] == '\0') {
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  argv = driver_c_exec_build_argv(file_path, argv_seq);
  if (argv == NULL) {
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  capture_fd = mkstemp(capture_path);
  if (capture_fd < 0) {
    free(argv);
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  pid = fork();
  if (pid < 0) {
    close(capture_fd);
    unlink(capture_path);
    free(argv);
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  if (pid == 0) {
    if (working_dir != NULL && working_dir[0] != '\0') {
      chdir(working_dir);
    }
    dup2(capture_fd, STDOUT_FILENO);
    dup2(capture_fd, STDERR_FILENO);
    close(capture_fd);
    execve(file_path, argv, environ);
    {
      const char *msg = strerror(errno);
      if (msg == NULL) msg = "execve failed";
      dprintf(STDERR_FILENO, "%s\n", msg);
    }
    _exit(127);
  }
  close(capture_fd);
  capture_fd = -1;
  free(argv);
  if (exit_code != NULL) {
    *exit_code = driver_c_wait_status_timeout(pid, timeout_sec);
  } else {
    (void)driver_c_wait_status_timeout(pid, timeout_sec);
  }
  out = driver_c_read_file_all(capture_path);
  unlink(capture_path);
  if (out == NULL) {
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
  }
  return out;
}

__attribute__((weak)) void *cheng_fopen(const char *filename, const char *mode) {
  if (filename == NULL || mode == NULL) return NULL;
  return (void *)fopen(filename, mode);
}

__attribute__((weak)) void *get_stdin(void) { return (void *)stdin; }

__attribute__((weak)) void *get_stdout(void) { return (void *)stdout; }

__attribute__((weak)) void *get_stderr(void) { return (void *)stderr; }

__attribute__((weak)) int32_t cheng_fclose(void *f) {
  if (f == NULL) return -1;
  return (int32_t)fclose((FILE *)f);
}

__attribute__((weak)) int32_t cheng_fread(void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || stream == NULL || size <= 0 || n <= 0) return 0;
  return (int32_t)fread(ptr, (size_t)size, (size_t)n, (FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fwrite(const void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || stream == NULL || size <= 0 || n <= 0) return 0;
  return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, (FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fflush(void *stream) {
  if (stream == NULL) return -1;
  return (int32_t)fflush((FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fgetc(void *stream) {
  if (stream == NULL) return -1;
  return (int32_t)fgetc((FILE *)stream);
}

__attribute__((weak)) int32_t cheng_file_exists(const char *path) {
  struct stat st;
  if (path == NULL) return 0;
  return stat(path, &st) == 0 ? 1 : 0;
}

__attribute__((weak)) int64_t cheng_file_size(const char *path) {
  struct stat st;
  if (path == NULL) return -1;
  if (stat(path, &st) != 0) return -1;
  return (int64_t)st.st_size;
}

__attribute__((weak)) int32_t cheng_dir_exists(const char *path) {
  struct stat st;
  if (path == NULL) return 0;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

__attribute__((weak)) int32_t cheng_mkdir1(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
  return mkdir(path, 0755);
}

__attribute__((weak)) int64_t cheng_file_mtime(const char *path) {
  struct stat st;
  if (path == NULL) return -1;
  if (stat(path, &st) != 0) return -1;
  return (int64_t)st.st_mtime;
}

__attribute__((weak)) char *cheng_getcwd(void) {
  static char buf[4096];
  if (getcwd(buf, sizeof(buf)) == NULL) return "";
  buf[sizeof(buf) - 1] = 0;
  return buf;
}

__attribute__((weak)) int32_t cheng_rawbytes_get_at(void *base, int32_t idx) {
  if (base == NULL || idx < 0) return 0;
  return (int32_t)((uint8_t *)base)[idx];
}

__attribute__((weak)) void cheng_rawbytes_set_at(void *base, int32_t idx, int32_t value) {
  if (base == NULL || idx < 0) return;
  ((uint8_t *)base)[idx] = (uint8_t)value;
}

__attribute__((weak)) char *driver_c_read_file_all(const char *path) {
  FILE *f = NULL;
  char *out = NULL;
  long sizeLong = 0;
  size_t readCount = 0;
  if (path == NULL || path[0] == '\0') {
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  f = fopen(path, "rb");
  if (f == NULL) {
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  sizeLong = ftell(f);
  if (sizeLong < 0) {
    fclose(f);
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  out = (char *)malloc((size_t)sizeLong + 1);
  if (out == NULL) {
    fclose(f);
    return NULL;
  }
  if (sizeLong > 0) {
    readCount = fread(out, 1, (size_t)sizeLong, f);
    if (readCount != (size_t)sizeLong && ferror(f)) {
      fclose(f);
      free(out);
      out = (char *)malloc(1);
      if (out != NULL) out[0] = 0;
      return out;
    }
  }
  out[readCount] = 0;
  fclose(f);
  return out;
}

__attribute__((weak)) ChengStrBridge driver_c_exec_file_capture_or_panic_bridge(ChengStrBridge file_path,
                                                                                ChengSeqHeader argv,
                                                                                ChengStrBridge working_dir,
                                                                                ChengStrBridge label) {
  char *file_path_c = driver_c_bridge_to_cstring(file_path);
  char *working_dir_c = driver_c_bridge_to_cstring(working_dir);
  char *label_c = driver_c_bridge_to_cstring(label);
  int64_t exit_code = -1;
  char *output = NULL;
  ChengStrBridge out = {0};
  output = driver_c_exec_file_capture_minimal(file_path_c != NULL ? file_path_c : "",
                                              argv,
                                              working_dir_c != NULL ? working_dir_c : "",
                                              300,
                                              &exit_code);
  free(file_path_c);
  free(working_dir_c);
  if (exit_code != 0) {
    driver_c_abort_with_label_output(label_c, output);
  }
  free(label_c);
  out.ptr = output;
  out.len = output != NULL ? (int32_t)strlen(output) : 0;
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_OWNED;
  return out;
}

__attribute__((weak)) void driver_c_compare_text_files_or_panic_bridge(ChengStrBridge left_path,
                                                                       ChengStrBridge right_path,
                                                                       ChengStrBridge label) {
  char *left_path_c = driver_c_bridge_to_cstring(left_path);
  char *right_path_c = driver_c_bridge_to_cstring(right_path);
  char *label_c = driver_c_bridge_to_cstring(label);
  int32_t same = driver_c_compare_files_bytes_impl(left_path_c, right_path_c);
  free(left_path_c);
  free(right_path_c);
  if (!same) {
    driver_c_abort_with_label_output(label_c, "");
  }
  free(label_c);
}

__attribute__((weak)) ChengStrBridge driver_c_absolute_path_bridge(ChengStrBridge path) {
  char *path_c = driver_c_bridge_to_cstring(path);
  char *out = driver_c_absolute_path_dup(path_c);
  free(path_c);
  return driver_c_owned_bridge_from_cstring(out);
}

__attribute__((weak)) ChengStrBridge driver_c_program_absolute_path_bridge(void) {
  return driver_c_owned_bridge_from_cstring(driver_c_absolute_path_dup(driver_c_program_name_raw()));
}

__attribute__((weak)) ChengStrBridge driver_c_get_current_dir_bridge(void) {
  return driver_c_owned_bridge_from_cstring(getcwd(NULL, 0));
}

__attribute__((weak)) ChengStrBridge driver_c_join_path2_bridge(ChengStrBridge left, ChengStrBridge right) {
  char *left_c = driver_c_bridge_to_cstring(left);
  char *right_c = driver_c_bridge_to_cstring(right);
  char *out = driver_c_join_path2_dup(left_c, right_c);
  free(left_c);
  free(right_c);
  return driver_c_owned_bridge_from_cstring(out);
}

__attribute__((weak)) void driver_c_create_dir_all_bridge(ChengStrBridge path) {
  char *path_c = driver_c_bridge_to_cstring(path);
  driver_c_mkdir_all_if_needed(path_c);
  free(path_c);
}

__attribute__((weak)) void driver_c_write_text_file_bridge(ChengStrBridge path, ChengStrBridge content) {
  char *path_c = driver_c_bridge_to_cstring(path);
  FILE *f = NULL;
  if (path_c == NULL || path_c[0] == '\0') {
    free(path_c);
    return;
  }
  driver_c_mkdir_parents_if_needed(path_c);
  f = fopen(path_c, "wb");
  if (f != NULL) {
    if (content.ptr != NULL && content.len > 0) {
      fwrite(content.ptr, 1, (size_t)content.len, f);
    }
    fclose(f);
  }
  free(path_c);
}

__attribute__((weak)) int32_t driver_c_compare_text_files_bridge(ChengStrBridge left_path, ChengStrBridge right_path) {
  char *left_c = driver_c_bridge_to_cstring(left_path);
  char *right_c = driver_c_bridge_to_cstring(right_path);
  int32_t same = driver_c_compare_files_bytes_impl(left_c, right_c);
  free(left_c);
  free(right_c);
  return same;
}

__attribute__((weak)) int32_t driver_c_compare_binary_files_bridge(ChengStrBridge left_path, ChengStrBridge right_path) {
  char *left_c = driver_c_bridge_to_cstring(left_path);
  char *right_c = driver_c_bridge_to_cstring(right_path);
  int32_t same = driver_c_compare_files_bytes_impl(left_c, right_c);
  free(left_c);
  free(right_c);
  return same;
}

__attribute__((weak)) ChengStrBridge driver_c_extract_line_value_bridge(ChengStrBridge text, ChengStrBridge key) {
  char *text_c = driver_c_bridge_to_cstring(text);
  char *key_c = driver_c_bridge_to_cstring(key);
  char *value = driver_c_find_line_value_dup(text_c, key_c);
  free(text_c);
  free(key_c);
  return driver_c_owned_bridge_from_cstring(value);
}

__attribute__((weak)) int32_t driver_c_count_external_cc_providers_bridge(ChengStrBridge plan_text) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  const char *cursor = plan_c;
  const char *needle = "compile_mode=external_cc_obj";
  int32_t count = 0;
  if (cursor != NULL) {
    while ((cursor = strstr(cursor, needle)) != NULL) {
      count = count + 1;
      cursor = cursor + strlen(needle);
    }
  }
  free(plan_c);
  return count;
}

__attribute__((weak)) int32_t driver_c_parse_plan_int32_or_zero_bridge(ChengStrBridge plan_text,
                                                                       ChengStrBridge key) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  char *key_c = driver_c_bridge_to_cstring(key);
  char *value = driver_c_find_line_value_dup(plan_c, key_c);
  char *end_ptr = NULL;
  long parsed = 0;
  int32_t out = 0;
  if (value != NULL && value[0] != '\0') {
    errno = 0;
    parsed = strtol(value, &end_ptr, 10);
    if (errno == 0 && end_ptr != NULL && *end_ptr == '\0') {
      out = (int32_t)parsed;
    }
  }
  free(value);
  free(plan_c);
  free(key_c);
  return out;
}

__attribute__((weak)) ChengStrBridge driver_c_provider_field_for_module_from_plan_text_bridge(ChengStrBridge plan_text,
                                                                                               ChengStrBridge module_name,
                                                                                               ChengStrBridge field_name) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  char *module_name_c = driver_c_bridge_to_cstring(module_name);
  char *field_name_c = driver_c_bridge_to_cstring(field_name);
  char *value = driver_c_provider_field_for_module_dup(plan_c, module_name_c, field_name_c);
  free(plan_c);
  free(module_name_c);
  free(field_name_c);
  return driver_c_owned_bridge_from_cstring(value);
}

__attribute__((weak)) ChengStrBridge driver_c_compiler_core_provider_source_kind_bridge(ChengStrBridge plan_text) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  char *value = driver_c_provider_field_for_module_dup(plan_c,
                                                       "runtime/compiler_core_runtime_v2",
                                                       "source_kind");
  free(plan_c);
  return driver_c_owned_bridge_from_cstring(value);
}

__attribute__((weak)) ChengStrBridge driver_c_compiler_core_provider_compile_mode_bridge(ChengStrBridge plan_text) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  char *value = driver_c_provider_field_for_module_dup(plan_c,
                                                       "runtime/compiler_core_runtime_v2",
                                                       "compile_mode");
  free(plan_c);
  return driver_c_owned_bridge_from_cstring(value);
}

static int32_t driver_c_count_external_cc_providers_text(const char *plan_text) {
  const char *cursor = plan_text;
  const char *needle = "compile_mode=external_cc_obj";
  int32_t count = 0;
  if (cursor == NULL) return 0;
  while ((cursor = strstr(cursor, needle)) != NULL) {
    count = count + 1;
    cursor = cursor + strlen(needle);
  }
  return count;
}

static void driver_c_write_text_file_raw(const char *path, const char *content) {
  FILE *f = NULL;
  if (path == NULL || path[0] == '\0') {
    driver_c_die("driver_c missing output path");
  }
  driver_c_mkdir_parents_if_needed(path);
  f = fopen(path, "wb");
  if (f == NULL) {
    driver_c_die_errno("driver_c fopen failed", path);
  }
  if (content != NULL && content[0] != '\0') {
    fwrite(content, 1, strlen(content), f);
  }
  fclose(f);
}

static char *driver_c_run_command_capture_or_die(const char *file_path,
                                                 const char **argv_items,
                                                 int32_t argc,
                                                 const char *working_dir,
                                                 const char *label) {
  ChengSeqHeader argv = {0};
  int64_t exit_code = -1;
  char *output = NULL;
  argv.len = argc;
  argv.cap = argc;
  argv.buffer = (void *)argv_items;
  output = driver_c_exec_file_capture_minimal(file_path, argv, working_dir, 300, &exit_code);
  if (exit_code != 0) {
    driver_c_abort_with_label_output(label, output);
  }
  return output;
}

static void driver_c_compare_expected_text_or_die(const char *repo_root,
                                                  const char *expected_rel,
                                                  const char *actual_path,
                                                  const char *label) {
  char expected_abs[PATH_MAX];
  driver_c_resolve_existing_input_path(repo_root, expected_rel, expected_abs, sizeof(expected_abs));
  if (!driver_c_compare_files_bytes_impl(expected_abs, actual_path)) {
    driver_c_abort_with_label_output(label, "");
  }
}

static DriverCNativeStageArtifacts driver_c_build_stage_artifacts(const char *repo_root,
                                                                  const char *compiler_path,
                                                                  const char *source_path,
                                                                  const char *target_triple,
                                                                  const char *stage_dir,
                                                                  const char *binary_name) {
  DriverCNativeStageArtifacts out;
  char release_path[PATH_MAX];
  char system_link_plan_path[PATH_MAX];
  char system_link_exec_path[PATH_MAX];
  char binary_path[PATH_MAX];
  const char *release_argv[9];
  const char *plan_argv[9];
  const char *exec_argv[11];
  char *release_text = NULL;
  char *plan_text = NULL;
  char *exec_text = NULL;
  memset(&out, 0, sizeof(out));
  driver_c_path_join_buf(release_path, sizeof(release_path), stage_dir, "release.txt");
  driver_c_path_join_buf(system_link_plan_path, sizeof(system_link_plan_path), stage_dir, "system_link_plan.txt");
  driver_c_path_join_buf(system_link_exec_path, sizeof(system_link_exec_path), stage_dir, "system_link_exec.txt");
  driver_c_path_join_buf(binary_path, sizeof(binary_path), stage_dir, binary_name);
  driver_c_mkdir_all_if_needed(stage_dir);

  release_argv[0] = "release-compile";
  release_argv[1] = "--in";
  release_argv[2] = source_path;
  release_argv[3] = "--emit";
  release_argv[4] = "exe";
  release_argv[5] = "--target";
  release_argv[6] = target_triple;
  release_argv[7] = "--out";
  release_argv[8] = release_path;
  release_text = driver_c_run_command_capture_or_die(compiler_path,
                                                     release_argv,
                                                     9,
                                                     repo_root,
                                                     "stage-selfhost-host release-compile failed");
  driver_c_write_text_file_raw(release_path, release_text);
  free(release_text);

  plan_argv[0] = "system-link-plan";
  plan_argv[1] = "--in";
  plan_argv[2] = source_path;
  plan_argv[3] = "--emit";
  plan_argv[4] = "exe";
  plan_argv[5] = "--target";
  plan_argv[6] = target_triple;
  plan_argv[7] = "--out";
  plan_argv[8] = system_link_plan_path;
  plan_text = driver_c_run_command_capture_or_die(compiler_path,
                                                  plan_argv,
                                                  9,
                                                  repo_root,
                                                  "stage-selfhost-host system-link-plan failed");
  driver_c_write_text_file_raw(system_link_plan_path, plan_text);

  exec_argv[0] = "system-link-exec";
  exec_argv[1] = "--in";
  exec_argv[2] = source_path;
  exec_argv[3] = "--emit";
  exec_argv[4] = "exe";
  exec_argv[5] = "--target";
  exec_argv[6] = target_triple;
  exec_argv[7] = "--out";
  exec_argv[8] = binary_path;
  exec_argv[9] = "--report-out";
  exec_argv[10] = system_link_exec_path;
  exec_text = driver_c_run_command_capture_or_die(compiler_path,
                                                  exec_argv,
                                                  11,
                                                  repo_root,
                                                  "stage-selfhost-host system-link-exec failed");
  driver_c_write_text_file_raw(system_link_exec_path, exec_text);

  out.compiler_path = driver_c_dup_cstring(compiler_path);
  out.release_path = driver_c_dup_cstring(release_path);
  out.system_link_plan_path = driver_c_dup_cstring(system_link_plan_path);
  out.system_link_exec_path = driver_c_dup_cstring(system_link_exec_path);
  out.binary_path = driver_c_dup_cstring(binary_path);
  out.binary_cid = driver_c_sha256_hex_file_dup(binary_path);
  out.output_file_cid = driver_c_find_line_value_dup(exec_text, "output_file_cid");
  out.external_cc_provider_count = driver_c_count_external_cc_providers_text(plan_text);
  out.compiler_core_provider_source_kind =
      driver_c_provider_field_for_module_dup(plan_text,
                                             "runtime/compiler_core_runtime_v2",
                                             "source_kind");
  out.compiler_core_provider_compile_mode =
      driver_c_provider_field_for_module_dup(plan_text,
                                             "runtime/compiler_core_runtime_v2",
                                             "compile_mode");
  free(plan_text);
  free(exec_text);
  return out;
}

static void driver_c_free_stage_artifacts(DriverCNativeStageArtifacts *bundle) {
  if (bundle == NULL) return;
  free(bundle->compiler_path);
  free(bundle->release_path);
  free(bundle->system_link_plan_path);
  free(bundle->system_link_exec_path);
  free(bundle->binary_path);
  free(bundle->binary_cid);
  free(bundle->output_file_cid);
  free(bundle->compiler_core_provider_source_kind);
  free(bundle->compiler_core_provider_compile_mode);
  memset(bundle, 0, sizeof(*bundle));
}

__attribute__((weak)) int32_t driver_c_run_tooling_selfhost_host_bridge(void) {
  char repo_root[PATH_MAX];
  char compiler_abs[PATH_MAX];
  char out_dir[PATH_MAX];
  char source_manifest_stdout[PATH_MAX];
  char source_manifest_file[PATH_MAX];
  char rule_pack_stdout[PATH_MAX];
  char rule_pack_file[PATH_MAX];
  char compiler_rule_pack_stdout[PATH_MAX];
  char compiler_rule_pack_file[PATH_MAX];
  char release_stdout[PATH_MAX];
  char release_file[PATH_MAX];
  char network_selfhost_file[PATH_MAX];
  char *compiler_raw = NULL;
  char *out_raw = NULL;
  char *stdout_text = NULL;
  const char *root_raw = "v2/examples";
  const char *source_raw = "v2/examples/network_distribution_module.cheng";
  const char *target_raw = "arm64-apple-darwin";
  const char *source_manifest_argv[5];
  const char *rule_pack_argv[5];
  const char *compiler_rule_pack_argv[5];
  const char *release_argv[9];
  const char *network_selfhost_argv[5];
  driver_c_detect_repo_root(driver_c_program_name_raw(), cheng_getcwd(), repo_root, sizeof(repo_root));
  compiler_raw = driver_c_read_flag_dup_raw("--compiler", driver_c_program_name_raw());
  out_raw = driver_c_read_flag_dup_raw("--out-dir", "v2/artifacts/full_selfhost/tooling_selfhost");
  driver_c_resolve_existing_input_path(repo_root, compiler_raw, compiler_abs, sizeof(compiler_abs));
  driver_c_resolve_output_path(repo_root, out_raw, out_dir, sizeof(out_dir));
  driver_c_mkdir_all_if_needed(out_dir);
  driver_c_path_join_buf(source_manifest_stdout, sizeof(source_manifest_stdout), out_dir, "source_manifest.stdout");
  driver_c_path_join_buf(source_manifest_file, sizeof(source_manifest_file), out_dir, "source_manifest.txt");
  driver_c_path_join_buf(rule_pack_stdout, sizeof(rule_pack_stdout), out_dir, "rule_pack.stdout");
  driver_c_path_join_buf(rule_pack_file, sizeof(rule_pack_file), out_dir, "rule_pack.txt");
  driver_c_path_join_buf(compiler_rule_pack_stdout, sizeof(compiler_rule_pack_stdout), out_dir, "compiler_rule_pack.stdout");
  driver_c_path_join_buf(compiler_rule_pack_file, sizeof(compiler_rule_pack_file), out_dir, "compiler_rule_pack.txt");
  driver_c_path_join_buf(release_stdout, sizeof(release_stdout), out_dir, "release_artifact.stdout");
  driver_c_path_join_buf(release_file, sizeof(release_file), out_dir, "release_artifact.txt");
  driver_c_path_join_buf(network_selfhost_file, sizeof(network_selfhost_file), out_dir, "network_selfhost.txt");

  source_manifest_argv[0] = "publish-source-manifest";
  source_manifest_argv[1] = "--root";
  source_manifest_argv[2] = root_raw;
  source_manifest_argv[3] = "--out";
  source_manifest_argv[4] = source_manifest_file;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    source_manifest_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host publish-source-manifest failed");
  driver_c_write_text_file_raw(source_manifest_stdout, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_source_manifest.expected",
                                        source_manifest_stdout,
                                        "tooling-selfhost-host source manifest stdout mismatch");
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_source_manifest.expected",
                                        source_manifest_file,
                                        "tooling-selfhost-host source manifest file mismatch");

  rule_pack_argv[0] = "publish-rule-pack";
  rule_pack_argv[1] = "--in";
  rule_pack_argv[2] = source_raw;
  rule_pack_argv[3] = "--out";
  rule_pack_argv[4] = rule_pack_file;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    rule_pack_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host publish-rule-pack failed");
  driver_c_write_text_file_raw(rule_pack_stdout, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_rule_pack.expected",
                                        rule_pack_stdout,
                                        "tooling-selfhost-host rule pack stdout mismatch");
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_rule_pack.expected",
                                        rule_pack_file,
                                        "tooling-selfhost-host rule pack file mismatch");

  compiler_rule_pack_argv[0] = "publish-compiler-rule-pack";
  compiler_rule_pack_argv[1] = "--in";
  compiler_rule_pack_argv[2] = source_raw;
  compiler_rule_pack_argv[3] = "--out";
  compiler_rule_pack_argv[4] = compiler_rule_pack_file;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    compiler_rule_pack_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host publish-compiler-rule-pack failed");
  driver_c_write_text_file_raw(compiler_rule_pack_stdout, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_compiler_rule_pack.expected",
                                        compiler_rule_pack_stdout,
                                        "tooling-selfhost-host compiler rule pack stdout mismatch");
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_compiler_rule_pack.expected",
                                        compiler_rule_pack_file,
                                        "tooling-selfhost-host compiler rule pack file mismatch");

  release_argv[0] = "release-compile";
  release_argv[1] = "--in";
  release_argv[2] = source_raw;
  release_argv[3] = "--emit";
  release_argv[4] = "exe";
  release_argv[5] = "--target";
  release_argv[6] = target_raw;
  release_argv[7] = "--out";
  release_argv[8] = release_file;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    release_argv,
                                                    9,
                                                    repo_root,
                                                    "tooling-selfhost-host release-compile failed");
  driver_c_write_text_file_raw(release_stdout, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_release_artifact.expected",
                                        release_stdout,
                                        "tooling-selfhost-host release stdout mismatch");
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_release_artifact.expected",
                                        release_file,
                                        "tooling-selfhost-host release file mismatch");

  network_selfhost_argv[0] = "verify-network-selfhost";
  network_selfhost_argv[1] = "--root";
  network_selfhost_argv[2] = root_raw;
  network_selfhost_argv[3] = "--in";
  network_selfhost_argv[4] = source_raw;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    network_selfhost_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host verify-network-selfhost failed");
  driver_c_write_text_file_raw(network_selfhost_file, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/network_selfhost.expected",
                                        network_selfhost_file,
                                        "tooling-selfhost-host network selfhost mismatch");

  printf("tooling_selfhost_host_ok=1\n");
  printf("compiler=%s\n", compiler_abs);
  printf("source_manifest_equal=1\n");
  printf("rule_pack_equal=1\n");
  printf("compiler_rule_pack_equal=1\n");
  printf("release_compile_equal=1\n");
  printf("network_selfhost_equal=1\n");
  free(compiler_raw);
  free(out_raw);
  return 0;
}

__attribute__((weak)) int32_t driver_c_run_stage_selfhost_host_bridge(void) {
  char repo_root[PATH_MAX];
  char compiler_abs[PATH_MAX];
  char out_dir[PATH_MAX];
  char stages_dir[PATH_MAX];
  char stage1_dir[PATH_MAX];
  char stage2_dir[PATH_MAX];
  char stage3_dir[PATH_MAX];
  char stage2_tooling_dir[PATH_MAX];
  char stage2_tooling_report[PATH_MAX];
  char *compiler_raw = NULL;
  char *out_raw = NULL;
  char *target_raw = NULL;
  const char *source_raw = "v2/src/tooling/cheng_v2.cheng";
  DriverCNativeStageArtifacts stage1;
  DriverCNativeStageArtifacts stage2;
  DriverCNativeStageArtifacts stage3;
  const char *tooling_selfhost_argv[5];
  char *tooling_selfhost_text = NULL;
  char *tooling_selfhost_ok = NULL;
  int release_equal = 0;
  int plan_equal = 0;
  int exec_equal = 0;
  int binary_equal = 0;
  int output_cid_equal = 0;
  int stage2_tooling_ok = 0;
  int no_external_c_provider = 0;
  int ok = 0;
  memset(&stage1, 0, sizeof(stage1));
  memset(&stage2, 0, sizeof(stage2));
  memset(&stage3, 0, sizeof(stage3));
  driver_c_detect_repo_root(driver_c_program_name_raw(), cheng_getcwd(), repo_root, sizeof(repo_root));
  compiler_raw = driver_c_read_flag_dup_raw("--compiler", driver_c_program_name_raw());
  out_raw = driver_c_read_flag_dup_raw("--out-dir", "v2/artifacts/full_selfhost");
  target_raw = driver_c_read_flag_dup_raw("--target", "arm64-apple-darwin");
  driver_c_resolve_existing_input_path(repo_root, compiler_raw, compiler_abs, sizeof(compiler_abs));
  driver_c_resolve_output_path(repo_root, out_raw, out_dir, sizeof(out_dir));
  driver_c_path_join_buf(stages_dir, sizeof(stages_dir), out_dir, "stages");
  driver_c_path_join_buf(stage1_dir, sizeof(stage1_dir), stages_dir, "cheng_v2.stage1");
  driver_c_path_join_buf(stage2_dir, sizeof(stage2_dir), stages_dir, "cheng_v2.stage2");
  driver_c_path_join_buf(stage3_dir, sizeof(stage3_dir), stages_dir, "cheng_v2.stage3");
  driver_c_path_join_buf(stage2_tooling_dir, sizeof(stage2_tooling_dir), out_dir, "stage2_tooling_selfhost");
  driver_c_path_join_buf(stage2_tooling_report, sizeof(stage2_tooling_report), stage2_tooling_dir, "tooling_selfhost_host.txt");
  driver_c_mkdir_all_if_needed(stage1_dir);
  driver_c_mkdir_all_if_needed(stage2_dir);
  driver_c_mkdir_all_if_needed(stage3_dir);

  stage1 = driver_c_build_stage_artifacts(repo_root, compiler_abs, source_raw, target_raw, stage1_dir, "cheng_v2");
  stage2 = driver_c_build_stage_artifacts(repo_root, stage1.binary_path, source_raw, target_raw, stage2_dir, "cheng_v2");
  stage3 = driver_c_build_stage_artifacts(repo_root, stage2.binary_path, source_raw, target_raw, stage3_dir, "cheng_v2");

  release_equal = driver_c_compare_files_bytes_impl(stage2.release_path, stage3.release_path);
  plan_equal = driver_c_compare_files_bytes_impl(stage2.system_link_plan_path, stage3.system_link_plan_path);
  exec_equal = driver_c_compare_files_bytes_impl(stage2.system_link_exec_path, stage3.system_link_exec_path);
  binary_equal = driver_c_compare_files_bytes_impl(stage2.binary_path, stage3.binary_path);
  output_cid_equal = strcmp(stage2.output_file_cid != NULL ? stage2.output_file_cid : "",
                            stage3.output_file_cid != NULL ? stage3.output_file_cid : "") == 0;
  no_external_c_provider =
      stage1.external_cc_provider_count == 0 &&
      stage2.external_cc_provider_count == 0 &&
      stage3.external_cc_provider_count == 0 &&
      strcmp(stage2.compiler_core_provider_source_kind != NULL ? stage2.compiler_core_provider_source_kind : "",
             "cheng_module") == 0 &&
      strcmp(stage2.compiler_core_provider_compile_mode != NULL ? stage2.compiler_core_provider_compile_mode : "",
             "machine_obj") == 0;

  tooling_selfhost_argv[0] = "tooling-selfhost-host";
  tooling_selfhost_argv[1] = "--compiler";
  tooling_selfhost_argv[2] = stage2.binary_path;
  tooling_selfhost_argv[3] = "--out-dir";
  tooling_selfhost_argv[4] = stage2_tooling_dir;
  tooling_selfhost_text = driver_c_run_command_capture_or_die(stage2.binary_path,
                                                              tooling_selfhost_argv,
                                                              5,
                                                              repo_root,
                                                              "stage-selfhost-host stage2 tooling-selfhost-host failed");
  driver_c_write_text_file_raw(stage2_tooling_report, tooling_selfhost_text);
  tooling_selfhost_ok = driver_c_find_line_value_dup(tooling_selfhost_text, "tooling_selfhost_host_ok");
  stage2_tooling_ok = tooling_selfhost_ok != NULL && strcmp(tooling_selfhost_ok, "1") == 0;
  free(tooling_selfhost_ok);
  free(tooling_selfhost_text);

  ok = release_equal &&
       plan_equal &&
       exec_equal &&
       binary_equal &&
       output_cid_equal &&
       stage2_tooling_ok &&
       no_external_c_provider;
  printf("full_selfhost_ok=%d\n", ok ? 1 : 0);
  printf("compiler=%s\n", compiler_abs);
  printf("stage1_binary_cid=%s\n", stage1.binary_cid != NULL ? stage1.binary_cid : "");
  printf("stage2_binary_cid=%s\n", stage2.binary_cid != NULL ? stage2.binary_cid : "");
  printf("stage3_binary_cid=%s\n", stage3.binary_cid != NULL ? stage3.binary_cid : "");
  printf("stage2_stage3_release_equal=%d\n", release_equal);
  printf("stage2_stage3_system_link_plan_equal=%d\n", plan_equal);
  printf("stage2_stage3_system_link_exec_equal=%d\n", exec_equal);
  printf("stage2_stage3_binary_equal=%d\n", binary_equal);
  printf("stage2_stage3_output_file_cid_equal=%d\n", output_cid_equal);
  printf("stage2_tooling_selfhost_ok=%d\n", stage2_tooling_ok);
  printf("stage1_external_cc_provider_count=%d\n", stage1.external_cc_provider_count);
  printf("stage2_external_cc_provider_count=%d\n", stage2.external_cc_provider_count);
  printf("stage3_external_cc_provider_count=%d\n", stage3.external_cc_provider_count);
  printf("compiler_core_provider_source_kind=%s\n",
         stage2.compiler_core_provider_source_kind != NULL ? stage2.compiler_core_provider_source_kind : "");
  printf("compiler_core_provider_compile_mode=%s\n",
         stage2.compiler_core_provider_compile_mode != NULL ? stage2.compiler_core_provider_compile_mode : "");
  printf("compiler_core_dispatch_provider_removed=%d\n", no_external_c_provider ? 1 : 0);
  printf("emit_c_used_after_stage0=0\n");

  driver_c_free_stage_artifacts(&stage1);
  driver_c_free_stage_artifacts(&stage2);
  driver_c_free_stage_artifacts(&stage3);
  free(compiler_raw);
  free(out_raw);
  free(target_raw);
  return ok ? 0 : 1;
}

__attribute__((weak)) int32_t driver_c_compiler_core_local_payload_bridge(const char *payload) {
  int argc = 0;
  char **argv = NULL;
  char *label = driver_c_payload_label_dup(payload);
  const char *cmd = NULL;
  int32_t rc = 0;
  argv = driver_c_cli_current_argv_dup(&argc);
  if (argv == NULL) {
    free(label);
    driver_c_die("driver_c compiler_core local payload bridge: argv alloc failed");
  }
  cmd = argc > 1 ? argv[1] : "";
  if (cheng_v2c_tooling_handle == NULL || cheng_v2c_tooling_is_command == NULL) {
    free(argv);
    free(label);
    driver_c_die("driver_c compiler_core local payload bridge: bootstrap tooling handle unavailable");
  }
  if (cmd == NULL || !cheng_v2c_tooling_is_command(cmd)) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c compiler_core local payload bridge: unsupported label=%s cmd=%s",
             label != NULL ? label : "",
             cmd != NULL ? cmd : "");
    free(argv);
    free(label);
    driver_c_die(message);
  }
  rc = cheng_v2c_tooling_handle(argc, argv);
  free(argv);
  free(label);
  return rc;
}
