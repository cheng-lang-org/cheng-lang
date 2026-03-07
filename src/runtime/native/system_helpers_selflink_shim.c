#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#endif

#if defined(__APPLE__)
extern int *__error(void);
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_SHIM_WEAK __attribute__((weak))
#else
#define CHENG_SHIM_WEAK
#endif

typedef struct ChengSeqHeader {
  int32_t len;
  int32_t cap;
  void *buffer;
} ChengSeqHeader;

extern void uirEmitObjFromModuleOrPanic(ChengSeqHeader *out, void *module, int32_t optLevel,
                                        const char *target, const char *objWriter,
                                        int32_t validateModule, int32_t uirSimdEnabled,
                                        int32_t uirSimdMaxWidth,
                                        const char *uirSimdPolicy) __attribute__((weak_import));

typedef void (*cheng_emit_obj_from_module_fn)(ChengSeqHeader *out, void *module, int32_t optLevel,
                                              const char *target, const char *objWriter,
                                              int32_t validateModule, int32_t uirSimdEnabled,
                                              int32_t uirSimdMaxWidth, const char *uirSimdPolicy);
typedef void *(*cheng_build_active_module_ptrs_fn)(void *inputRaw, void *targetRaw);
typedef void *(*cheng_build_module_stage1_fn)(const char *path, const char *target);

static int32_t cheng_saved_argc = 0;
static const char **cheng_saved_argv = NULL;
int32_t cheng_strlen(char *s);
int32_t cheng_file_exists(const char *path);
int64_t cheng_file_size(const char *path);
int32_t cheng_open_w_trunc(const char *path);
int32_t cheng_fd_write(int32_t fd, const char *data, int32_t len);
int32_t libc_close(int32_t fd);
int32_t driver_c_arg_count(void);
char *driver_c_arg_copy(int32_t i);
int32_t __cheng_rt_paramCount(void);
const char * __cheng_rt_paramStr(int32_t i);
char *__cheng_rt_paramStrCopy(int32_t i);
CHENG_SHIM_WEAK int32_t driver_c_finish_emit_obj(const char *path);
CHENG_SHIM_WEAK void *driver_c_build_module_stage1(const char *input_path, const char *target);

static int driver_c_arg_is_help(const char *arg) {
  if (arg == NULL) return 0;
  return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

CHENG_SHIM_WEAK int32_t driver_c_backend_usage(void) {
  fputs("Usage:\n", stderr);
  fputs("  backend_driver [--input:<file>|<file>] [--output:<out>] [options]\n", stderr);
  fputs("\n", stderr);
  fputs("Core options:\n", stderr);
  fputs("  --emit:<exe> --target:<triple|auto> --frontend:<stage1>\n", stderr);
  fputs("  --linker:<self|system> --obj-writer:<auto|elf|macho|coff>\n", stderr);
  fputs("  --opt-level:<N> --opt2 --no-opt2 --opt --no-opt\n", stderr);
  fputs("  --multi --no-multi --multi-force --no-multi-force\n", stderr);
  fputs("  --incremental --no-incremental --allow-no-main\n", stderr);
  fputs("  --skip-global-init --runtime-obj:<path> --runtime-c:<path> --no-runtime-c\n", stderr);
  fputs("  --generic-mode:<dict> --generic-spec-budget:<N>\n", stderr);
  fputs("  --borrow-ir:<mir|stage1> --generic-lowering:<mir_dict>\n", stderr);
  fputs("  --abi:<v2_noptr> --android-api:<N> --compile-stamp-out:<path>\n", stderr);
  fputs("  --profile --no-profile --uir-profile --no-uir-profile\n", stderr);
  fputs("  --uir-simd --no-uir-simd --uir-simd-max-width:<N> --uir-simd-policy:<name>\n", stderr);
  return 0;
}

void driver_c_boot_marker(int32_t code);

#if defined(CHENG_BACKEND_DRIVER_ENTRY_SHIM)
extern int32_t backendMain(void);
void __cheng_setCmdLine(int32_t argc, const char **argv) CHENG_SHIM_WEAK;
CHENG_SHIM_WEAK int main(int argc, char **argv) {
  __cheng_setCmdLine((int32_t)argc, (const char **)argv);
  for (int i = 1; i < argc; ++i) {
    if (driver_c_arg_is_help(argv[i])) {
      return driver_c_backend_usage();
    }
  }
  return backendMain();
}
#endif

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

static int cheng_ptr_is_suspicious(void *p) {
  if (p == NULL) return 0;
  uintptr_t v = (uintptr_t)p;
  if (v < 4096u) return 1;
  if (v > 0x0000FFFFFFFFFFFFull) return 1;
  return 0;
}

/*
 * Self-link executables may pass stale/uninitialized pointers to realloc when
 * stage1 lowers seq growth through raw temporaries. Guard this path so we
 * fail soft (allocate fresh) instead of aborting in libmalloc.
 */
void *realloc(void *p, size_t size) {
  if (size == 0u) size = 1u;
  if (cheng_ptr_is_suspicious(p)) p = NULL;
#if defined(__APPLE__)
  malloc_zone_t *zone = malloc_default_zone();
  if (zone != NULL) {
    return malloc_zone_realloc(zone, p, size);
  }
#endif
  if (p == NULL) return malloc(size);
  void *out = malloc(size);
  if (out == NULL) return NULL;
#if defined(__APPLE__)
  size_t old_size = malloc_size(p);
  if (old_size > 0u) {
    size_t n = old_size < size ? old_size : size;
    memcpy(out, p, n);
  }
#endif
  free(p);
  return out;
}

static void cheng_seq_sanitize(ChengSeqHeader *hdr) {
  if (hdr == NULL) return;
  if (hdr->cap < 0 || hdr->cap > (1 << 27) || hdr->len < 0 || hdr->len > hdr->cap) {
    hdr->len = 0;
    hdr->cap = 0;
    hdr->buffer = NULL;
    return;
  }
  if (hdr->cap == 0) {
    hdr->buffer = NULL;
  }
  if (hdr->buffer == NULL && hdr->len != 0) {
    hdr->len = 0;
  }
  if (hdr->buffer != NULL) {
    uintptr_t p = (uintptr_t)hdr->buffer;
    if (p < 4096u || p > 0x0000FFFFFFFFFFFFull) {
      hdr->len = 0;
      hdr->cap = 0;
      hdr->buffer = NULL;
    }
  }
}

void *c_malloc(int64_t size) {
  if (size <= 0) size = 1;
  return malloc((size_t)size);
}

void *c_calloc(int64_t nmemb, int64_t size) {
  if (nmemb <= 0) nmemb = 1;
  if (size <= 0) size = 1;
  return calloc((size_t)nmemb, (size_t)size);
}

void *c_realloc(void *p, int64_t size) {
  if (size <= 0) size = 1;
  return realloc(p, (size_t)size);
}

void c_free(void *p) { free(p); }

int32_t c_puts_runtime(char *text) { return puts(text ? text : ""); }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((used, noinline))
#endif
int32_t c_puts(char *text) {
  return c_puts_runtime(text);
}

void c_exit(int32_t code) { exit(code); }

void *alloc(int64_t size) {
  return c_malloc(size);
}

void dealloc(void *p) {
  c_free(p);
}

void *c_memcpy(void *dest, void *src, int64_t n) {
  if (!dest || !src || n <= 0) return dest;
  return memcpy(dest, src, (size_t)n);
}

void *c_memset(void *dest, int32_t val, int64_t n) {
  if (!dest || n <= 0) return dest;
  return memset(dest, val, (size_t)n);
}

void *cheng_memcpy(void *dest, void *src, int64_t n) {
  return c_memcpy(dest, src, n);
}

void *cheng_memset(void *dest, int32_t val, int64_t n) {
  return c_memset(dest, val, n);
}

void *cheng_malloc(int32_t size) {
  return c_malloc((int64_t)size);
}

void cheng_free(void *p) {
  c_free(p);
}

void *cheng_realloc(void *p, int32_t size) {
  return c_realloc(p, (int64_t)size);
}

void cheng_mem_retain(void *p) {
  (void)p;
}

void cheng_mem_release(void *p) {
  (void)p;
}

void memRetain(void *p) { cheng_mem_retain(p); }
void memRelease(void *p) { cheng_mem_release(p); }

void zeroMem(void *p, int64_t n) {
  if (!p || n <= 0) return;
  memset(p, 0, (size_t)n);
}

int32_t c_memcmp(void *a, void *b, int64_t n) {
  if (!a || !b || n <= 0) return 0;
  return memcmp(a, b, (size_t)n);
}

CHENG_SHIM_WEAK int32_t c_strlen(char *s) {
  return cheng_strlen(s);
}

char *c_getenv(char *name) {
  if (!name) return NULL;
  return getenv(name);
}

char *driver_c_getenv(const char *name) {
  if (!name) return NULL;
  return getenv(name);
}

int32_t driver_c_env_bool(const char *name, int32_t defaultValue) {
  const char *raw = getenv(name != NULL ? name : "");
  if (raw == NULL || raw[0] == '\0') return defaultValue ? 1 : 0;
  if (strcmp(raw, "1") == 0 || strcmp(raw, "true") == 0 || strcmp(raw, "TRUE") == 0 ||
      strcmp(raw, "yes") == 0 || strcmp(raw, "YES") == 0 || strcmp(raw, "on") == 0 ||
      strcmp(raw, "ON") == 0) return 1;
  if (strcmp(raw, "0") == 0 || strcmp(raw, "false") == 0 || strcmp(raw, "FALSE") == 0 ||
      strcmp(raw, "no") == 0 || strcmp(raw, "NO") == 0 || strcmp(raw, "off") == 0 ||
      strcmp(raw, "OFF") == 0) return 0;
  return defaultValue ? 1 : 0;
}

char *driver_c_getenv_copy(const char *name) {
  const char *raw = getenv(name != NULL ? name : "");
  if (raw == NULL) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  memcpy(out, raw, len);
  out[len] = '\0';
  return out;
}

static char *driver_c_dup_cstr(const char *raw) {
  if (raw == NULL) raw = "";
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  if (len > 0) memcpy(out, raw, len);
  out[len] = '\0';
  return out;
}

static char *driver_c_cli_value_copy(const char *key) {
  if (key == NULL || key[0] == '\0') return driver_c_dup_cstr("");
  int32_t argc = driver_c_arg_count();
  size_t key_len = strlen(key);
  if (argc <= 0 || argc > 4096) return driver_c_dup_cstr("");
  for (int32_t i = 1; i <= argc; ++i) {
    const char *arg = driver_c_arg_copy(i);
    if (arg == NULL || arg[0] == '\0') continue;
    if (strcmp(arg, key) == 0) {
      if (i + 1 <= argc) return driver_c_dup_cstr(driver_c_arg_copy(i + 1));
      return driver_c_dup_cstr("");
    }
    if (strncmp(arg, key, key_len) == 0 && (arg[key_len] == ':' || arg[key_len] == '=')) {
      return driver_c_dup_cstr(arg + key_len + 1);
    }
  }
  return driver_c_dup_cstr("");
}

CHENG_SHIM_WEAK char *driver_c_cli_input_copy(void) {
  char *value = driver_c_cli_value_copy("--input");
  if (value != NULL && value[0] != '\0') return value;
  int32_t argc = driver_c_arg_count();
  if (argc > 0 && argc <= 4096) {
    for (int32_t i = 1; i <= argc; ++i) {
      const char *arg = driver_c_arg_copy(i);
      if (arg == NULL || arg[0] == '\0') continue;
      if (arg[0] != '-') return driver_c_dup_cstr(arg);
    }
  }
  return value;
}

CHENG_SHIM_WEAK char *driver_c_cli_output_copy(void) {
  return driver_c_cli_value_copy("--output");
}

CHENG_SHIM_WEAK char *driver_c_cli_target_copy(void) {
  return driver_c_cli_value_copy("--target");
}

CHENG_SHIM_WEAK char *driver_c_cli_linker_copy(void) {
  return driver_c_cli_value_copy("--linker");
}

char *driver_c_new_string(int32_t n) {
  if (n <= 0) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  char *out = (char *)cheng_malloc(n + 1);
  if (out == NULL) return NULL;
  memset(out, 0, (size_t)n + 1u);
  return out;
}

int32_t driver_c_argv_is_help(int32_t argc, void *argv_void) {
  char **argv = (char **)argv_void;
  if (argc != 2 || argv == NULL) return 0;
  const char *arg1 = argv[1];
  if (arg1 == NULL) return 0;
  return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
}

int32_t driver_c_cli_help_requested(void) {
  if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) {
    return driver_c_argv_is_help(cheng_saved_argc, (void *)cheng_saved_argv);
  }
  if (__cheng_rt_paramCount != NULL && __cheng_rt_paramStrCopy != NULL) {
    int32_t argc = __cheng_rt_paramCount();
    if (argc != 2) return 0;
    char *arg1 = __cheng_rt_paramStrCopy(1);
    if (arg1 == NULL) return 0;
    return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
  }
  return 0;
}

int32_t driver_c_env_nonempty(const char *name) {
  const char *raw = getenv(name != NULL ? name : "");
  return (raw != NULL && raw[0] != '\0') ? 1 : 0;
}

int32_t driver_c_env_eq(const char *name, const char *expected) {
  const char *raw = getenv(name != NULL ? name : "");
  if (raw == NULL || raw[0] == '\0' || expected == NULL) return 0;
  return strcmp(raw, expected) == 0 ? 1 : 0;
}

static const char *driver_c_host_target_default(void) {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
  return "arm64-apple-darwin";
#elif defined(__APPLE__) && defined(__x86_64__)
  return "x86_64-apple-darwin";
#elif defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
  return "aarch64-unknown-linux-gnu";
#elif defined(__linux__) && defined(__x86_64__)
  return "x86_64-unknown-linux-gnu";
#else
  return "";
#endif
}

CHENG_SHIM_WEAK char *driver_c_active_output_path(const char *input_path) {
  const char *raw = getenv("BACKEND_OUTPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_OUTPUT");
  return (char *)(input_path != NULL ? input_path : "");
}

CHENG_SHIM_WEAK char *driver_c_active_output_copy(void) {
  const char *raw = getenv("BACKEND_OUTPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_OUTPUT");
  return (char *)"";
}

CHENG_SHIM_WEAK char *driver_c_active_input_path(void) {
  const char *raw = getenv("BACKEND_INPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_INPUT");
  return (char *)"";
}

CHENG_SHIM_WEAK char *driver_c_active_target(void) {
  const char *raw = getenv("BACKEND_TARGET");
  if (raw == NULL || raw[0] == '\0' ||
      strcmp(raw, "auto") == 0 || strcmp(raw, "native") == 0 || strcmp(raw, "host") == 0) {
    return (char *)driver_c_host_target_default();
  }
  return driver_c_getenv_copy("BACKEND_TARGET");
}

CHENG_SHIM_WEAK char *driver_c_active_linker(void) {
  const char *raw = getenv("BACKEND_LINKER");
  if (raw == NULL || raw[0] == '\0') return (char *)"system";
  return driver_c_getenv_copy("BACKEND_LINKER");
}

static int32_t driver_c_target_is_darwin_alias(const char *raw) {
  if (raw == NULL || raw[0] == '\0') return 0;
  return strcmp(raw, "arm64-apple-darwin") == 0 ||
         strcmp(raw, "aarch64-apple-darwin") == 0 ||
         strcmp(raw, "arm64-darwin") == 0 ||
         strcmp(raw, "aarch64-darwin") == 0 ||
         strcmp(raw, "darwin_arm64") == 0 ||
         strcmp(raw, "darwin_aarch64") == 0;
}

static char *driver_c_resolve_input_path(void) {
  char *cli = driver_c_cli_input_copy();
  if (cli != NULL && cli[0] != '\0') return cli;
  return driver_c_active_input_path();
}

static char *driver_c_resolve_output_path(const char *input_path) {
  char *cli = driver_c_cli_output_copy();
  if (cli != NULL && cli[0] != '\0') return cli;
  return driver_c_active_output_path(input_path);
}

static char *driver_c_resolve_target(void) {
  char *cli = driver_c_cli_target_copy();
  if (cli != NULL && cli[0] != '\0') {
    if (strcmp(cli, "auto") == 0 || strcmp(cli, "native") == 0 || strcmp(cli, "host") == 0) {
      return (char *)driver_c_host_target_default();
    }
    if (driver_c_target_is_darwin_alias(cli)) return (char *)"arm64-apple-darwin";
    return cli;
  }
  {
    char *active = driver_c_active_target();
    if (active == NULL || active[0] == '\0') return (char *)driver_c_host_target_default();
    if (strcmp(active, "auto") == 0 || strcmp(active, "native") == 0 || strcmp(active, "host") == 0) {
      return (char *)driver_c_host_target_default();
    }
    if (driver_c_target_is_darwin_alias(active)) return (char *)"arm64-apple-darwin";
    return active;
  }
}

static char *driver_c_resolve_linker(void) {
  char *cli = driver_c_cli_linker_copy();
  if (cli != NULL && cli[0] != '\0') return cli;
  return driver_c_active_linker();
}

static int32_t driver_c_emit_is_obj_mode(void) {
  const char *raw = getenv("BACKEND_EMIT");
  if (raw == NULL || raw[0] == '\0' || strcmp(raw, "exe") == 0) return 0;
  if (strcmp(raw, "obj") == 0 && driver_c_env_bool("BACKEND_INTERNAL_ALLOW_EMIT_OBJ", 0) != 0) {
    return 1;
  }
  fputs("backend_driver: invalid emit mode (expected exe)\n", stderr);
  return -50;
}

static void driver_c_require_output_file_or_die(const char *path, const char *phase) {
  const char *phase_text = (phase != NULL && phase[0] != '\0') ? phase : "<unknown>";
  if (path == NULL || path[0] == '\0') {
    fprintf(stderr, "backend_driver: missing output path after %s\n", phase_text);
    exit(1);
  }
  if (cheng_file_exists((char *)path) == 0) {
    fprintf(stderr, "backend_driver: missing output after %s: %s\n", phase_text, path);
    exit(1);
  }
  if (cheng_file_size((char *)path) <= 0) {
    fprintf(stderr, "backend_driver: empty output after %s: %s\n", phase_text, path);
    exit(1);
  }
}

CHENG_SHIM_WEAK int32_t driver_c_finish_emit_obj(const char *path) {
  driver_c_require_output_file_or_die(path, "emit_obj");
  driver_c_boot_marker(5);
  return 0;
}

int32_t driver_c_write_exact_file(const char *path, void *buffer, int32_t len) {
  if (path == NULL || path[0] == '\0') return -1;
  if (buffer == NULL || len <= 0) return -2;
  int32_t fd = cheng_open_w_trunc(path);
  if (fd < 0) return -3;
  int32_t wrote = cheng_fd_write(fd, (const char *)buffer, len);
  (void)libc_close(fd);
  if (wrote != len) return -4;
  if (cheng_file_exists(path) == 0) return -5;
  if (cheng_file_size(path) != (int64_t)len) return -6;
  return 0;
}

CHENG_SHIM_WEAK int32_t driver_c_emit_obj_default(void *module, const char *target, const char *path) {
  cheng_emit_obj_from_module_fn emit_obj =
      (cheng_emit_obj_from_module_fn)dlsym(RTLD_DEFAULT, "uirEmitObjFromModuleOrPanic");
  ChengSeqHeader obj;
  memset(&obj, 0, sizeof(obj));
  if (emit_obj == NULL) return -10;
  if (module == NULL) return -11;
  if (path == NULL || path[0] == '\0') return -12;
  if (target == NULL) target = "";
  emit_obj(&obj, module, 0, target, "", 0, 0, 0, "autovec");
  if (obj.len <= 0) return -13;
  if (obj.buffer == NULL) return -14;
  return driver_c_write_exact_file(path, obj.buffer, obj.len);
}

CHENG_SHIM_WEAK int32_t driver_c_link_tmp_obj_default(const char *output_path, const char *obj_path,
                                                      const char *target, const char *linker) {
  const char *linker_text = (linker != NULL && linker[0] != '\0') ? linker : "system";
  const char *runtime_c = "/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c";
  if (output_path == NULL || output_path[0] == '\0') return -20;
  if (obj_path == NULL || obj_path[0] == '\0') return -21;
  if (strcmp(linker_text, "system") != 0) return -22;
  (void)target;
  size_t need = strlen(obj_path) + strlen(runtime_c) + strlen(output_path) + 64u;
  char *cmd = (char *)malloc(need);
  if (cmd == NULL) return -23;
  snprintf(cmd, need, "cc '%s' '%s' -o '%s'", obj_path, runtime_c, output_path);
  int rc = system(cmd);
  free(cmd);
  if (rc != 0) return -24;
  if (cheng_file_exists(output_path) == 0) return -25;
  if (cheng_file_size(output_path) <= 0) return -26;
  return 0;
}

CHENG_SHIM_WEAK int32_t driver_c_link_tmp_obj_system(const char *output_path, const char *obj_path,
                                                     const char *target) {
  return driver_c_link_tmp_obj_default(output_path, obj_path, target, "system");
}

CHENG_SHIM_WEAK int32_t driver_c_build_emit_obj_default(const char *input_path, const char *target,
                                                        const char *output_path) {
  if (input_path == NULL || input_path[0] == '\0') return -30;
  if (target == NULL || target[0] == '\0') return -31;
  if (output_path == NULL || output_path[0] == '\0') return -32;
  void *module = driver_c_build_module_stage1(input_path, target);
  if (module == NULL) return -33;
  int32_t emit_rc = driver_c_emit_obj_default(module, target, output_path);
  if (emit_rc != 0) return emit_rc;
  return driver_c_finish_emit_obj(output_path);
}

CHENG_SHIM_WEAK int32_t driver_c_build_active_obj_default(void) {
  char *input_path = driver_c_active_input_path();
  if (input_path == NULL || input_path[0] == '\0') return 2;
  char *target = driver_c_active_target();
  char *output_path = driver_c_active_output_path(input_path);
  return driver_c_build_emit_obj_default(input_path, target, output_path);
}

CHENG_SHIM_WEAK int32_t driver_c_build_link_exe_default(const char *input_path, const char *target,
                                                        const char *output_path, const char *linker) {
  (void)linker;
  if (output_path == NULL || output_path[0] == '\0') return -40;
  const char *suffix = ".tmp.linkobj";
  size_t need = strlen(output_path) + strlen(suffix) + 1u;
  char *obj_path = (char *)malloc(need);
  if (obj_path == NULL) return -41;
  snprintf(obj_path, need, "%s%s", output_path, suffix);
  int32_t emit_rc = driver_c_build_emit_obj_default(input_path, target, obj_path);
  if (emit_rc != 0) {
    free(obj_path);
    return emit_rc;
  }
  int32_t link_rc = driver_c_link_tmp_obj_system(output_path, obj_path, target);
  if (link_rc != 0) {
    free(obj_path);
    return link_rc;
  }
  int32_t finish_rc = driver_c_finish_single_link(output_path, obj_path);
  free(obj_path);
  return finish_rc;
}

CHENG_SHIM_WEAK void *driver_c_build_module_stage1(const char *input_path, const char *target) {
  cheng_build_active_module_ptrs_fn build_active_module_ptrs =
      (cheng_build_active_module_ptrs_fn)dlsym(RTLD_DEFAULT, "driver_buildActiveModulePtrs");
  cheng_build_module_stage1_fn build_module_stage1 =
      (cheng_build_module_stage1_fn)dlsym(RTLD_DEFAULT, "uirCoreBuildModuleFromFileStage1OrPanic");
  if (input_path == NULL || input_path[0] == '\0') return NULL;
  if (target == NULL) target = "";
  if (build_active_module_ptrs != NULL) return build_active_module_ptrs((void *)input_path, (void *)target);
  if (build_module_stage1 == NULL) return NULL;
  return build_module_stage1(input_path, target);
}

CHENG_SHIM_WEAK int32_t driver_c_build_active_exe_default(void) {
  char *input_path = driver_c_active_input_path();
  if (input_path == NULL || input_path[0] == '\0') return 2;
  char *target = driver_c_active_target();
  char *output_path = driver_c_active_output_path(input_path);
  char *linker = driver_c_active_linker();
  return driver_c_build_link_exe_default(input_path, target, output_path, linker);
}

CHENG_SHIM_WEAK int32_t driver_c_run_default(void) {
  int32_t emit_mode = driver_c_emit_is_obj_mode();
  if (emit_mode < 0) return emit_mode;
  char *input_path = driver_c_resolve_input_path();
  char *target = driver_c_resolve_target();
  char *output_path = driver_c_resolve_output_path(input_path);
  char *linker = driver_c_resolve_linker();
  if (input_path == NULL || input_path[0] == '\0') return 2;
  if (target == NULL || target[0] == '\0') return 2;
  if (output_path == NULL || output_path[0] == '\0') return 2;
  if (emit_mode != 0) return driver_c_build_emit_obj_default(input_path, target, output_path);
  return driver_c_build_link_exe_default(input_path, target, output_path, linker);
}

CHENG_SHIM_WEAK int32_t driver_c_finish_single_link(const char *path, const char *obj_path) {
  driver_c_require_output_file_or_die(path, "single_link");
  if (driver_c_env_bool("BACKEND_KEEP_TMP_LINKOBJ", 0) == 0 &&
      obj_path != NULL && obj_path[0] != '\0') {
    unlink(obj_path);
  }
  driver_c_boot_marker(8);
  return 0;
}

static int32_t driver_c_boot_marker_enabled(void) {
  const char *raw = getenv("BACKEND_DEBUG_BOOT");
  if (raw == NULL || raw[0] == '\0') return 0;
  if (raw[0] == '0' && raw[1] == '\0') return 0;
  return 1;
}

static void driver_c_boot_marker_write(const char *text, size_t len) {
  if (!driver_c_boot_marker_enabled()) return;
  if (text == NULL || len == 0) return;
  (void)write(2, text, len);
}

void driver_c_boot_marker(int32_t code) {
  switch (code) {
  case 1: driver_c_boot_marker_write("[boot]01\n", sizeof("[boot]01\n") - 1u); break;
  case 2: driver_c_boot_marker_write("[boot]02\n", sizeof("[boot]02\n") - 1u); break;
  case 3: driver_c_boot_marker_write("[boot]03\n", sizeof("[boot]03\n") - 1u); break;
  case 4: driver_c_boot_marker_write("[boot]04\n", sizeof("[boot]04\n") - 1u); break;
  case 5: driver_c_boot_marker_write("[boot]05\n", sizeof("[boot]05\n") - 1u); break;
  case 6: driver_c_boot_marker_write("[boot]06\n", sizeof("[boot]06\n") - 1u); break;
  case 7: driver_c_boot_marker_write("[boot]07\n", sizeof("[boot]07\n") - 1u); break;
  case 8: driver_c_boot_marker_write("[boot]08\n", sizeof("[boot]08\n") - 1u); break;
  case 9: driver_c_boot_marker_write("[boot]09\n", sizeof("[boot]09\n") - 1u); break;
  case 10: driver_c_boot_marker_write("[boot]10\n", sizeof("[boot]10\n") - 1u); break;
  case 11: driver_c_boot_marker_write("[boot]11\n", sizeof("[boot]11\n") - 1u); break;
  case 20: driver_c_boot_marker_write("[boot]20\n", sizeof("[boot]20\n") - 1u); break;
  case 21: driver_c_boot_marker_write("[boot]21\n", sizeof("[boot]21\n") - 1u); break;
  case 30: driver_c_boot_marker_write("[boot]30\n", sizeof("[boot]30\n") - 1u); break;
  case 31: driver_c_boot_marker_write("[boot]31\n", sizeof("[boot]31\n") - 1u); break;
  case 32: driver_c_boot_marker_write("[boot]32\n", sizeof("[boot]32\n") - 1u); break;
  case 33: driver_c_boot_marker_write("[boot]33\n", sizeof("[boot]33\n") - 1u); break;
  case 34: driver_c_boot_marker_write("[boot]34\n", sizeof("[boot]34\n") - 1u); break;
  case 35: driver_c_boot_marker_write("[boot]35\n", sizeof("[boot]35\n") - 1u); break;
  case 36: driver_c_boot_marker_write("[boot]36\n", sizeof("[boot]36\n") - 1u); break;
  case 37: driver_c_boot_marker_write("[boot]37\n", sizeof("[boot]37\n") - 1u); break;
  case 38: driver_c_boot_marker_write("[boot]38\n", sizeof("[boot]38\n") - 1u); break;
  default: break;
  }
}

void driver_c_capture_cmdline(int32_t argc, void *argv_void) {
  cheng_saved_argc = argc;
  cheng_saved_argv = (const char **)argv_void;
}

int32_t driver_c_capture_cmdline_keep(int32_t argc, void *argv_void) {
  driver_c_capture_cmdline(argc, argv_void);
  return argc;
}

CHENG_SHIM_WEAK int32_t driver_c_arg_count(void) {
  if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) return cheng_saved_argc - 1;
  int32_t argc = __cheng_rt_paramCount();
  if (argc > 1 && argc <= 257) return argc - 1;
  return 0;
}

CHENG_SHIM_WEAK char *driver_c_arg_copy(int32_t i) {
  if (cheng_saved_argv == NULL && i > 0) {
    char *rt = __cheng_rt_paramStrCopy(i);
    if (rt != NULL) return rt;
  }
  if (cheng_saved_argv == NULL || i <= 0 || i >= cheng_saved_argc) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  const char *raw = cheng_saved_argv[i];
  if (raw == NULL) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  memcpy(out, raw, len);
  out[len] = '\0';
  return out;
}

CHENG_SHIM_WEAK int32_t driver_c_help_requested(void) {
  if (cheng_saved_argv != NULL && cheng_saved_argc > 1 && cheng_saved_argc <= 4096) {
    for (int32_t i = 1; i < cheng_saved_argc; ++i) {
      const char *arg = cheng_saved_argv[i];
      if (arg == NULL) continue;
      if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
    }
    return 0;
  }
  int32_t argc = __cheng_rt_paramCount();
  if (argc > 1 && argc <= 4096) {
    for (int32_t i = 1; i < argc; ++i) {
      char *arg = __cheng_rt_paramStrCopy(i);
      if (arg == NULL) continue;
      if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
    }
  }
  return 0;
}

void cheng_bytes_copy(void *dst, void *src, int64_t n) {
  if (!dst || !src || n <= 0) return;
  memcpy(dst, src, (size_t)n);
}

void cheng_bytes_set(void *dst, int32_t value, int64_t n) {
  if (!dst || n <= 0) return;
  memset(dst, value, (size_t)n);
}

static const char *cheng_safe_cstr(const char *s) {
  if (s == NULL) return "";
  uintptr_t raw = (uintptr_t)s;
#if defined(__APPLE__) && UINTPTR_MAX > 0xffffffffu
  bool in_image_or_stack =
      raw >= (uintptr_t)0x0000000100000000ULL &&
      raw <  (uintptr_t)0x0000000200000000ULL;
  bool in_malloc_zone =
      raw >= (uintptr_t)0x0000600000000000ULL &&
      raw <  (uintptr_t)0x0000700000000000ULL;
  if (!in_image_or_stack && !in_malloc_zone) return "";
#endif
  return (const char *)raw;
}

int32_t cheng_strlen(char *s) {
  const char *safe = cheng_safe_cstr((const char *)s);
  size_t n = strlen(safe);
  return (n > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)n;
}

int32_t cheng_strcmp(char *a, char *b) {
  return strcmp(cheng_safe_cstr((const char *)a), cheng_safe_cstr((const char *)b));
}

int32_t c_strcmp(char *a, char *b) {
  return cheng_strcmp(a, b);
}

char *cheng_str_concat(char *a, char *b) {
  const char *sa = cheng_safe_cstr((const char *)a);
  const char *sb = cheng_safe_cstr((const char *)b);
  size_t la = strlen(sa);
  size_t lb = strlen(sb);
  size_t total = la + lb;
  if (total > (size_t)INT32_MAX - 1u) {
    total = (size_t)INT32_MAX - 1u;
  }
  char *out = (char *)malloc(total + 1u);
  if (out == NULL) return (char *)"";
  if (la > 0) memcpy(out, sa, la);
  if (lb > 0) memcpy(out + la, sb, lb);
  out[total] = '\0';
  return out;
}

char *__cheng_str_concat(char *a, char *b) { return cheng_str_concat(a, b); }
char *__cheng_sym_2b(char *a, char *b) { return cheng_str_concat(a, b); }

void c_iometer_call(void *hook, int32_t op, int64_t bytes) {
  (void)hook;
  (void)op;
  (void)bytes;
}

void cheng_iometer_call(void *hook, int32_t op, int64_t bytes) {
  (void)hook;
  (void)op;
  (void)bytes;
}

void *openImpl(char *filename, int32_t mode) {
  const char *path = filename != NULL ? filename : "";
  const char *openMode = "rb";
  if (mode == 1) {
    openMode = "wb";
  } else if (mode != 0) {
    openMode = "rb+";
  }
  return fopen(path, openMode);
}

uint64_t processOptionMask(int32_t opt) {
  if (opt < 0 || opt >= 63) return 0;
  return ((uint64_t)1) << (uint64_t)opt;
}

char *sliceStr(char *text, int32_t start, int32_t stop) {
  if (text == NULL) return (char *)"";
  int32_t n = cheng_strlen(text);
  if (n <= 0) return (char *)"";
  int32_t a = start < 0 ? 0 : start;
  int32_t b = stop;
  if (b >= n) b = n - 1;
  if (b < a) return (char *)"";
  int32_t span = b - a + 1;
  char *out = (char *)malloc((size_t)span + 1u);
  if (out == NULL) return (char *)"";
  memcpy(out, text + a, (size_t)span);
  out[span] = '\0';
  return out;
}

int32_t cheng_dir_exists(const char *path) {
  if (path == NULL || path[0] == '\0') return 0;
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

int32_t cheng_mkdir1(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
#if defined(_WIN32)
  return _mkdir(path);
#else
  return mkdir(path, 0755);
#endif
}

char *cheng_getcwd(void) {
  static char buf[4096];
  if (getcwd(buf, sizeof(buf)) == NULL) {
    buf[0] = '\0';
  }
  size_t n = strlen(buf);
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, buf, n);
  out[n] = '\0';
  return out;
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
  if (argc_ptr == NULL) return 0;
  int argc = *argc_ptr;
  if (argc <= 0 || argc > 4096) return 0;
  return (int32_t)argc;
#else
  return 0;
#endif
}

int32_t __cmdCountFromRuntime(void) {
  int32_t argc = __cheng_rt_paramCount();
  if (argc <= 0 || argc > 4096) return 0;
  return argc;
}

int32_t cmdCountFromRuntime(void) {
  return __cmdCountFromRuntime();
}

const char * __cheng_rt_paramStr(int32_t i) {
  if (i >= 0 && cheng_saved_argc > 0 && cheng_saved_argc <= 4096 &&
      cheng_saved_argv != NULL && i < cheng_saved_argc) {
    const char *s = cheng_saved_argv[i];
    return s != NULL ? s : "";
  }
#if defined(__APPLE__)
  if (i < 0) return "";
  int *argc_ptr = _NSGetArgc();
  char ***argv_ptr = _NSGetArgv();
  if (argc_ptr == NULL || argv_ptr == NULL || *argv_ptr == NULL) return "";
  int argc = *argc_ptr;
  if (argc <= 0 || argc > 4096 || i >= argc) return "";
  char *s = (*argv_ptr)[i];
  return s != NULL ? s : "";
#else
  (void)i;
  return "";
#endif
}

char * __cheng_rt_paramStrCopy(int32_t i) {
  const char *s = __cheng_rt_paramStr(i);
  if (s == NULL) s = "";
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

char * __cheng_rt_programBaseNameCopy(void) {
  const char *s = __cheng_rt_paramStr(0);
  if (s == NULL) s = "";
  const char *base = s;
  for (const char *p = s; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  size_t n = strlen(base);
  if (n > 3 && base[n - 3] == '.' && base[n - 2] == 's' && base[n - 1] == 'h') {
    n -= 3;
  }
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, base, n);
  out[n] = '\0';
  return out;
}

void *cheng_seq_get(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  if (!buffer || idx < 0 || idx >= len || elem_size <= 0) return NULL;
  return (void *)((char *)buffer + ((size_t)idx * (size_t)elem_size));
}

void *cheng_seq_set(void *buffer, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_get(buffer, len, idx, elem_size);
}

void *cheng_seq_set_grow(void *seq_ptr, int32_t idx, int32_t elem_size) {
  if (!seq_ptr || elem_size <= 0) return cheng_seq_get(NULL, 0, idx, elem_size);
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  cheng_seq_sanitize(seq);
  if (idx < 0) return cheng_seq_get(seq->buffer, seq->len, idx, elem_size);
  int32_t need = idx + 1;
  if (need > seq->cap || seq->buffer == NULL) {
    int32_t new_cap = cheng_next_cap(seq->cap, need);
    if (new_cap <= 0) return cheng_seq_get(seq->buffer, seq->len, idx, elem_size);
    size_t old_bytes = (size_t)(seq->cap > 0 ? seq->cap : 0) * (size_t)elem_size;
    size_t new_bytes = (size_t)new_cap * (size_t)elem_size;
    void *new_buf = c_realloc(seq->buffer, (int64_t)new_bytes);
    if (!new_buf) return cheng_seq_get(seq->buffer, seq->len, idx, elem_size);
    if (new_bytes > old_bytes) {
      memset((char *)new_buf + old_bytes, 0, new_bytes - old_bytes);
    }
    seq->buffer = new_buf;
    seq->cap = new_cap;
  }
  if (need > seq->len) seq->len = need;
  return cheng_seq_get(seq->buffer, seq->len, idx, elem_size);
}

void reserve(void *seq, int32_t new_cap) {
  if (!seq || new_cap <= 0) return;
  ChengSeqHeader *hdr = (ChengSeqHeader *)seq;
  cheng_seq_sanitize(hdr);
  if (hdr->buffer && new_cap <= hdr->cap) return;
  int32_t target_cap = cheng_next_cap(hdr->cap, new_cap);
  if (target_cap <= 0) return;
  const size_t slot_bytes = sizeof(void *) < 32u ? 32u : sizeof(void *);
  const size_t old_bytes = (size_t)(hdr->cap > 0 ? hdr->cap : 0) * slot_bytes;
  const size_t new_bytes = (size_t)target_cap * slot_bytes;
  void *new_buf = c_realloc(hdr->buffer, (int64_t)new_bytes);
  if (!new_buf) return;
  if (new_bytes > old_bytes) {
    memset((char *)new_buf + old_bytes, 0, new_bytes - old_bytes);
  }
  hdr->buffer = new_buf;
  hdr->cap = target_cap;
}

void reserve_ptr_void(void *seq, int32_t new_cap) {
  reserve(seq, new_cap);
}

void setLen(void *seq, int32_t new_len) {
  if (!seq) return;
  ChengSeqHeader *hdr = (ChengSeqHeader *)seq;
  cheng_seq_sanitize(hdr);
  int32_t target = new_len < 0 ? 0 : new_len;
  if (target > hdr->cap) reserve(seq, target);
  hdr->len = target;
}

void *cheng_slice_get(void *ptr, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_get(ptr, len, idx, elem_size);
}

void *cheng_slice_set(void *ptr, int32_t len, int32_t idx, int32_t elem_size) {
  return cheng_seq_set(ptr, len, idx, elem_size);
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

int32_t cheng_file_exists(const char *path) {
  if (path == NULL || path[0] == '\0') return 0;
  struct stat st;
  return stat(path, &st) == 0 ? 1 : 0;
}

int64_t cheng_file_mtime(const char *path) {
  if (path == NULL || path[0] == '\0') return 0;
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  return (int64_t)st.st_mtime;
}

int64_t cheng_file_size(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
  struct stat st;
  if (stat(path, &st) != 0) return -1;
  return (int64_t)st.st_size;
}

int32_t cheng_jpeg_decode(const uint8_t *data, int32_t len, int32_t *out_w, int32_t *out_h, uint8_t **out_rgba) {
  (void)data;
  (void)len;
  if (out_w) *out_w = 0;
  if (out_h) *out_h = 0;
  if (out_rgba) *out_rgba = NULL;
  return 0;
}

void cheng_jpeg_free(void *p) {
  free(p);
}

int32_t libc_socket(int32_t domain, int32_t typ, int32_t protocol) {
  return socket(domain, typ, protocol);
}

int32_t libc_bind(int32_t fd, void *addr, int32_t len) {
  return bind(fd, (const struct sockaddr *)addr, (socklen_t)len);
}

int32_t libc_sendto(int32_t fd, void *buf, int32_t len, int32_t flags, void *addr, int32_t addrlen) {
  return (int32_t)sendto(fd, buf, (size_t)len, flags, (const struct sockaddr *)addr, (socklen_t)addrlen);
}

int32_t libc_send(int32_t fd, void *buf, int32_t len, int32_t flags) {
  return (int32_t)send(fd, buf, (size_t)len, flags);
}

int32_t libc_recvfrom(int32_t fd, void *buf, int32_t len, int32_t flags, void *addr, int32_t *addrlen) {
  socklen_t al = (addrlen && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
  int32_t rc = (int32_t)recvfrom(fd, buf, (size_t)len, flags, (struct sockaddr *)addr, addrlen ? &al : NULL);
  if (addrlen) *addrlen = (int32_t)al;
  return rc;
}

int32_t libc_recv(int32_t fd, void *buf, int32_t len, int32_t flags) {
  return (int32_t)recv(fd, buf, (size_t)len, flags);
}

int32_t libc_sendmsg(int32_t fd, void *msg, int32_t flags) {
  return (int32_t)sendmsg(fd, (const struct msghdr *)msg, flags);
}

int32_t libc_recvmsg(int32_t fd, void *msg, int32_t flags) {
  return (int32_t)recvmsg(fd, (struct msghdr *)msg, flags);
}

int32_t libc_getsockname(int32_t fd, void *addr, int32_t *addrlen) {
  socklen_t al = (addrlen && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
  int32_t rc = getsockname(fd, (struct sockaddr *)addr, addrlen ? &al : NULL);
  if (addrlen) *addrlen = (int32_t)al;
  return rc;
}

int32_t libc_getpeername(int32_t fd, void *addr, int32_t *addrlen) {
  socklen_t al = (addrlen && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
  int32_t rc = getpeername(fd, (struct sockaddr *)addr, addrlen ? &al : NULL);
  if (addrlen) *addrlen = (int32_t)al;
  return rc;
}

int32_t libc_listen(int32_t fd, int32_t backlog) { return listen(fd, backlog); }

int32_t libc_connect(int32_t fd, void *addr, int32_t len) {
  return connect(fd, (const struct sockaddr *)addr, (socklen_t)len);
}

int32_t libc_accept(int32_t fd, void *addr, int32_t *addrlen) {
  socklen_t al = (addrlen && *addrlen > 0) ? (socklen_t)(*addrlen) : (socklen_t)0;
  int32_t rc = accept(fd, (struct sockaddr *)addr, addrlen ? &al : NULL);
  if (addrlen) *addrlen = (int32_t)al;
  return rc;
}

int32_t libc_getsockopt(int32_t fd, int32_t level, int32_t optname, void *optval, int32_t *optlen) {
  socklen_t ol = (optlen && *optlen > 0) ? (socklen_t)(*optlen) : (socklen_t)0;
  int32_t rc = getsockopt(fd, level, optname, optval, optlen ? &ol : NULL);
  if (optlen) *optlen = (int32_t)ol;
  return rc;
}

int32_t libc_setsockopt(int32_t fd, int32_t level, int32_t optname, void *optval, int32_t optlen) {
  return setsockopt(fd, level, optname, optval, (socklen_t)optlen);
}

int32_t libc_socketpair(int32_t domain, int32_t typ, int32_t protocol, int32_t *sv) {
  return socketpair(domain, typ, protocol, sv);
}

int32_t libc_close(int32_t fd) { return close(fd); }

int32_t libc_shutdown(int32_t fd, int32_t how) { return shutdown(fd, how); }

char *libc_strerror(int32_t err) { return strerror(err); }

int32_t *libc_errno_ptr(void) {
#if defined(__APPLE__)
  return __error();
#else
  return &errno;
#endif
}

static FILE *cheng_safe_stream(void *stream) {
  if (stream == (void *)stdout || stream == (void *)stderr || stream == (void *)stdin) {
    return (FILE *)stream;
  }
  if (stream == NULL) return stdout;
  uintptr_t v = (uintptr_t)stream;
  if (v < 0x100000000ull) {
    return stdout;
  }
  return (FILE *)stream;
}

void *libc_fopen(char *filename, char *mode) { return fopen(filename, mode); }

int32_t libc_fclose(void *f) {
  FILE *stream = cheng_safe_stream(f);
  if (stream == stdout || stream == stderr || stream == stdin) return 0;
  return fclose(stream);
}

int64_t libc_fread(void *ptr, int64_t size, int64_t n, void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return (int64_t)fread(ptr, (size_t)size, (size_t)n, f);
}

int64_t libc_fwrite(void *ptr, int64_t size, int64_t n, void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return (int64_t)fwrite(ptr, (size_t)size, (size_t)n, f);
}

int32_t libc_fflush(void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return fflush(f);
}

int32_t libc_fgetc(void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return fgetc(f);
}

void *libc_fdopen(int32_t fd, char *mode) { return fdopen(fd, mode); }

int32_t libc_fseeko(void *stream, int64_t offset, int32_t whence) {
  return fseeko((FILE *)stream, (off_t)offset, whence);
}

int64_t libc_ftello(void *stream) { return (int64_t)ftello((FILE *)stream); }

int32_t libc_mkdir(char *path, int32_t mode) { return mkdir(path, (mode_t)mode); }

char *libc_getcwd(char *buf, int64_t size) { return getcwd(buf, (size_t)size); }

void *libc_opendir(char *path) { return opendir(path); }

void *libc_readdir(void *dir) { return readdir((DIR *)dir); }

int32_t libc_closedir(void *dir) { return closedir((DIR *)dir); }

void *libc_popen(char *command, char *mode) { return popen(command, mode); }

int32_t libc_pclose(void *stream) { return pclose((FILE *)stream); }

int64_t libc_time(void *out) { return (int64_t)time((time_t *)out); }

int32_t libc_clock_gettime(int32_t clock_id, void *out) {
  return clock_gettime(clock_id, (struct timespec *)out);
}

int32_t libc_stat(char *path, void *out) { return stat(path, (struct stat *)out); }

double cheng_bits_to_f32(int32_t bits) {
  union {
    uint32_t u32;
    float f32;
  } v;
  v.u32 = (uint32_t)bits;
  return (double)v.f32;
}

int32_t cheng_f32_to_bits(double value) {
  union {
    uint32_t u32;
    float f32;
  } v;
  v.f32 = (float)value;
  return (int32_t)v.u32;
}

int32_t cheng_fclose(void *f) {
  FILE *stream = cheng_safe_stream(f);
  if (stream == stdout || stream == stderr || stream == stdin) return 0;
  return fclose(stream);
}

int32_t cheng_fwrite(void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || size <= 0 || n <= 0) return 0;
  FILE *f = cheng_safe_stream(stream);
  return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, f);
}

int32_t cheng_fd_write(int32_t fd, const char *data, int32_t len) {
  if (fd < 0 || data == NULL || len <= 0) return 0;
  ssize_t n = (ssize_t)syscall(SYS_write, fd, data, (size_t)len);
  if (n < 0) return -1;
  return (int32_t)n;
}

int32_t cheng_fflush(void *stream) {
  FILE *f = cheng_safe_stream(stream);
  return fflush(f);
}

int32_t libc_open(const char *path, int32_t flags, int32_t mode) {
  if (path == NULL) return -1;
  return open(path, flags, mode);
}

int32_t cheng_open_w_trunc(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
  return creat(path, 0644);
}

int64_t libc_write(int32_t fd, void *data, int64_t n) {
  if (fd < 0 || data == NULL || n <= 0) return 0;
  ssize_t wrote = write(fd, data, (size_t)n);
  if (wrote < 0) return -1;
  return (int64_t)wrote;
}

void *get_stdin(void) {
  return (void *)stdin;
}

void *get_stdout(void) {
  return (void *)stdout;
}

void *get_stderr(void) {
  return (void *)stderr;
}

/*
 * Stage0 compatibility shim:
 * older bootstrap outputs may keep a direct unresolved call to symbol "[]=".
 * Provide a benign fallback body to keep runtime symbol closure deterministic.
 */
int64_t cheng_index_set_compat(void) __asm__("_[]=");
int64_t cheng_index_set_compat(void) { return 0; }

int64_t cheng_monotime_ns(void) {
#if defined(_WIN32)
  return (int64_t)clock();
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
#endif
}

static char *cheng_strdup_or_empty(const char *s) {
  if (s == NULL) s = "";
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) return (char *)"";
  if (n > 0) memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

static int cheng_buf_ensure(char **buf, size_t *cap, size_t need) {
  if (buf == NULL || cap == NULL) return 0;
  if (*buf == NULL || *cap == 0) {
    size_t init_cap = 256u;
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

static int cheng_buf_append(char **buf, size_t *cap, size_t *len, const char *src, size_t n) {
  if (len == NULL) return 0;
  size_t need = *len + n + 1u;
  if (!cheng_buf_ensure(buf, cap, need)) return 0;
  if (n > 0 && src != NULL) {
    memcpy((*buf) + *len, src, n);
    *len += n;
  }
  (*buf)[*len] = '\0';
  return 1;
}

char *cheng_list_dir(const char *path) {
  const char *dirPath = (path != NULL && path[0] != '\0') ? path : ".";
  DIR *dir = opendir(dirPath);
  if (dir == NULL) return cheng_strdup_or_empty("");
  char *out = NULL;
  size_t cap = 0;
  size_t len = 0;
  struct dirent *ent = NULL;
  while ((ent = readdir(dir)) != NULL) {
    const char *name = ent->d_name;
    if (name == NULL) continue;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    size_t n = strlen(name);
    if (!cheng_buf_append(&out, &cap, &len, name, n)) {
      closedir(dir);
      free(out);
      return cheng_strdup_or_empty("");
    }
    if (!cheng_buf_append(&out, &cap, &len, "\n", 1u)) {
      closedir(dir);
      free(out);
      return cheng_strdup_or_empty("");
    }
  }
  closedir(dir);
  if (out == NULL) return cheng_strdup_or_empty("");
  return out;
}

char *cheng_exec_cmd_ex(const char *command, const char *workingDir, int32_t mergeStderr, int64_t *exitCode) {
  if (exitCode != NULL) *exitCode = -1;
  if (command == NULL || command[0] == '\0') {
    return cheng_strdup_or_empty("");
  }

  char *shell = NULL;
  size_t shellCap = 0;
  size_t shellLen = 0;
  if (workingDir != NULL && workingDir[0] != '\0') {
    if (!cheng_buf_append(&shell, &shellCap, &shellLen, "cd ", 3u) ||
        !cheng_buf_append(&shell, &shellCap, &shellLen, "'", 1u)) {
      free(shell);
      return cheng_strdup_or_empty("");
    }
    for (size_t i = 0; workingDir[i] != '\0'; ++i) {
      const char c = workingDir[i];
      if (c == '\'') {
        if (!cheng_buf_append(&shell, &shellCap, &shellLen, "'\\''", 4u)) {
          free(shell);
          return cheng_strdup_or_empty("");
        }
      } else {
        if (!cheng_buf_append(&shell, &shellCap, &shellLen, &c, 1u)) {
          free(shell);
          return cheng_strdup_or_empty("");
        }
      }
    }
    if (!cheng_buf_append(&shell, &shellCap, &shellLen, "' && ", 6u)) {
      free(shell);
      return cheng_strdup_or_empty("");
    }
  }
  if (!cheng_buf_append(&shell, &shellCap, &shellLen, command, strlen(command))) {
    free(shell);
    return cheng_strdup_or_empty("");
  }
  if (mergeStderr != 0) {
    if (!cheng_buf_append(&shell, &shellCap, &shellLen, " 2>&1", 6u)) {
      free(shell);
      return cheng_strdup_or_empty("");
    }
  }

  FILE *fp = popen(shell, "r");
  free(shell);
  if (fp == NULL) {
    return cheng_strdup_or_empty("");
  }
  char *out = NULL;
  size_t cap = 0;
  size_t len = 0;
  char buf[4096];
  for (;;) {
    size_t n = fread(buf, 1u, sizeof(buf), fp);
    if (n > 0) {
      if (!cheng_buf_append(&out, &cap, &len, buf, n)) {
        free(out);
        out = NULL;
        break;
      }
    }
    if (n < sizeof(buf)) {
      if (feof(fp)) break;
      if (ferror(fp)) break;
    }
  }
  int status = pclose(fp);
  if (exitCode != NULL) {
#if defined(_WIN32)
    *exitCode = (int64_t)status;
#else
    if (WIFEXITED(status)) {
      *exitCode = (int64_t)WEXITSTATUS(status);
    } else {
      *exitCode = (int64_t)status;
    }
#endif
  }
  if (out == NULL) return cheng_strdup_or_empty("");
  return out;
}

void *load_ptr(void *p, int32_t off) {
  if (!p) return NULL;
  return *(void **)((char *)p + off);
}

void store_ptr(void *p, int32_t off, void *v) {
  if (!p) return;
  *(void **)((char *)p + off) = v;
}
