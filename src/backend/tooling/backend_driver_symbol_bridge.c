#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime hooks provided by system_helpers.c */
extern int32_t cheng_file_exists(const char *path);
extern int64_t cheng_file_mtime(const char *path);
extern int64_t cheng_file_size(const char *path);
extern int32_t cheng_open_w_trunc(const char *path);
extern int32_t cheng_fd_write(int32_t fd, const char *data, int32_t len);
extern int32_t libc_close(int32_t fd);
extern void *cheng_malloc(int32_t size);
extern int32_t driver_backendBuildActiveObjNativePtrs(void *input_raw, void *target_raw, void *output_raw);
extern int32_t driver_backendBuildActiveExeNativePtrs(void *input_raw, void *target_raw, void *output_raw, void *linker_raw);
extern void cheng_iometer_call(void *hook, int32_t op, int64_t bytes);
extern void *cheng_jpeg_decode(void *data, int32_t len, void *out_w, void *out_h);
extern void cheng_jpeg_free(void *p);
extern int32_t libc_fflush(void *stream);
extern int32_t driver_c_arg_count(void);
extern char *driver_c_arg_copy(int32_t i);
extern int32_t __cheng_rt_paramCount(void);
extern const char *__cheng_rt_paramStr(int32_t i);
extern char *__cheng_rt_paramStrCopy(int32_t i);
extern void __cheng_setCmdLine(int32_t argc, const char **argv);
extern int32_t backendMain(void) __attribute__((weak));
extern int32_t toolingMainWithCount(int32_t count) __attribute__((weak));
extern void *driver_buildActiveModulePtrs(void *input_raw, void *target_raw) __attribute__((weak));
extern void *uirCoreBuildModuleFromFileStage1OrPanic(const char *path, const char *target) __attribute__((weak));
typedef struct {
  int32_t len;
  int32_t cap;
  void *buffer;
} cheng_seq_u8_header;
extern void uirEmitObjFromModuleOrPanic(cheng_seq_u8_header *out, void *module, int32_t optLevel,
                                        const char *target, const char *objWriter,
                                        bool validateModule, bool uirSimdEnabled,
                                        int32_t uirSimdMaxWidth,
                                        const char *uirSimdPolicy) __attribute__((weak));
extern int32_t driver_emitObjFromModuleDefault(void *module, const char *target,
                                               const char *out_path) __attribute__((weak));
__attribute__((weak)) void driver_c_capture_cmdline(int32_t argc, void *argv_void);
__attribute__((weak)) void driver_c_boot_marker(int32_t code);
int32_t driver_c_finish_single_link(const char *path, const char *obj_path);
int32_t driver_c_finish_emit_obj(const char *path);
void *driver_c_build_module_stage1(const char *input_path, const char *target);
static void driver_c_maybe_inject_env_cli(int *argc_io, char ***argv_io);

typedef struct {
  void *buffer;
  int32_t len;
  int32_t cap;
} cheng_seq_i32;

typedef struct {
  void *buffer;
  int32_t len;
  int32_t cap;
} cheng_seq_any;

__attribute__((weak)) int32_t driver_puts(const char *text) {
  if (text == NULL) {
    return puts("[bridge] driver_puts: <null>") >= 0 ? 0 : -1;
  }
  return puts(text);
}

__attribute__((weak)) void driver_exit(int32_t code) { exit(code); }

static int driver_c_arg_is_help(const char *arg) {
  if (arg == NULL) return 0;
  return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

int32_t driver_c_backend_usage(void) {
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

__attribute__((weak)) int main(int argc, char **argv) {
  char **effective_argv = argv;
  int effective_argc = argc;
  if (backendMain != NULL) {
    driver_c_maybe_inject_env_cli(&effective_argc, &effective_argv);
  }
  __cheng_setCmdLine((int32_t)effective_argc, (const char **)effective_argv);
  driver_c_capture_cmdline((int32_t)effective_argc, (void *)effective_argv);
  if (toolingMainWithCount != NULL) {
    int32_t count = 0;
    if (effective_argc > 1 && effective_argc <= 4096) {
      count = (int32_t)effective_argc - 1;
    }
    return toolingMainWithCount(count);
  }
  if (backendMain != NULL) {
    for (int i = 1; i < effective_argc; ++i) {
      if (driver_c_arg_is_help(effective_argv[i])) {
        return driver_c_backend_usage();
      }
    }
    return backendMain();
  }
  return 1;
}

__attribute__((weak)) void *driver_ptr_add(void *p, int32_t offset) {
  return (void *)((char *)p + offset);
}

__attribute__((weak)) void *ptr_add(void *p, int32_t offset) {
  return (void *)((char *)p + offset);
}

__attribute__((weak)) void *rawmemAsVoid(void *p) {
  return p;
}

void backend_driver_stage0_setindex_compat(char *s, int32_t idx, int32_t value)
    __asm__("_[]=");
__attribute__((weak)) void
backend_driver_stage0_setindex_compat(char *s, int32_t idx, int32_t value) {
  if (s == NULL || idx < 0) {
    return;
  }
  s[idx] = (char)value;
}

__attribute__((weak)) void *driver_memcpy(void *dest, void *src, int64_t n) {
  if (n <= 0) {
    return dest;
  }
  return memcpy(dest, src, (size_t)n);
}

__attribute__((weak)) void *driver_memset(void *dest, int32_t val, int64_t n) {
  if (n <= 0) {
    return dest;
  }
  return memset(dest, val, (size_t)n);
}

__attribute__((weak)) void *driver_c_alloc(int32_t size) { return cheng_malloc(size); }

__attribute__((weak)) int32_t driver_file_exists(const char *path) {
  return cheng_file_exists(path);
}

__attribute__((weak)) void c_iometer_call(void *hook, int32_t op, int64_t bytes) {
  cheng_iometer_call(hook, op, bytes);
}

__attribute__((weak)) int64_t driver_file_mtime(const char *path) {
  return cheng_file_mtime(path);
}

__attribute__((weak)) int64_t driver_file_size(const char *path) {
  return cheng_file_size(path);
}

__attribute__((weak)) int64_t driver_c_fork(void) { return (int64_t)fork(); }

__attribute__((weak)) int64_t driver_c_waitpid(int64_t pid, int32_t *status, int32_t options) {
  return (int64_t)waitpid((pid_t)pid, status, options);
}

__attribute__((weak)) double bitsToF32(int32_t bits) {
  union {
    uint32_t u;
    float f;
  } u;
  u.u = (uint32_t)bits;
  return (double)u.f;
}

__attribute__((weak)) int32_t f32ToBits(double value) {
  union {
    uint32_t u;
    float f;
  } u;
  u.f = (float)value;
  return (int32_t)u.u;
}

__attribute__((weak)) void *c_jpeg_decode(void *data, int32_t len, void *out_w, void *out_h) {
  return cheng_jpeg_decode(data, len, out_w, out_h);
}

__attribute__((weak)) void c_jpeg_free(void *p) { cheng_jpeg_free(p); }

__attribute__((weak)) char *libc_getenv(const char *name) { return getenv(name); }

__attribute__((weak)) char *driver_c_getenv(const char *name) { return getenv(name); }

__attribute__((weak)) int32_t driver_c_env_bool(const char *name, int32_t default_value) {
  const char *raw = getenv(name != NULL ? name : "");
  if (raw == NULL || raw[0] == '\0') return default_value ? 1 : 0;
  if (strcmp(raw, "1") == 0 || strcmp(raw, "true") == 0 || strcmp(raw, "TRUE") == 0 ||
      strcmp(raw, "yes") == 0 || strcmp(raw, "YES") == 0 || strcmp(raw, "on") == 0 ||
      strcmp(raw, "ON") == 0) return 1;
  if (strcmp(raw, "0") == 0 || strcmp(raw, "false") == 0 || strcmp(raw, "FALSE") == 0 ||
      strcmp(raw, "no") == 0 || strcmp(raw, "NO") == 0 || strcmp(raw, "off") == 0 ||
      strcmp(raw, "OFF") == 0) return 0;
  return default_value ? 1 : 0;
}

__attribute__((weak)) char *driver_c_getenv_copy(const char *name) {
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

__attribute__((weak)) char *driver_c_cli_input_copy(void) {
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

__attribute__((weak)) char *driver_c_cli_output_copy(void) {
  return driver_c_cli_value_copy("--output");
}

__attribute__((weak)) char *driver_c_cli_target_copy(void) {
  return driver_c_cli_value_copy("--target");
}

__attribute__((weak)) char *driver_c_cli_linker_copy(void) {
  return driver_c_cli_value_copy("--linker");
}

__attribute__((weak)) char *driver_c_new_string(int32_t n) {
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

static int32_t g_driver_cli_argc = 0;
static char **g_driver_cli_argv = NULL;

static char *driver_c_make_flag_value(const char *flag, const char *value) {
  size_t flag_len = (flag != NULL) ? strlen(flag) : 0u;
  size_t value_len = (value != NULL) ? strlen(value) : 0u;
  char *out = (char *)cheng_malloc((int32_t)(flag_len + value_len + 2u));
  if (out == NULL) return NULL;
  if (flag_len > 0) memcpy(out, flag, flag_len);
  out[flag_len] = ':';
  if (value_len > 0) memcpy(out + flag_len + 1u, value, value_len);
  out[flag_len + value_len + 1u] = '\0';
  return out;
}

static int driver_c_has_flag_value(int argc, char **argv, const char *flag) {
  if (flag == NULL || flag[0] == '\0') return 0;
  size_t n = strlen(flag);
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (arg == NULL) continue;
    if (strcmp(arg, flag) == 0) return 1;
    if (strncmp(arg, flag, n) == 0 && (arg[n] == ':' || arg[n] == '=')) return 1;
  }
  return 0;
}

static int driver_c_has_positional_input(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (arg == NULL || arg[0] == '\0') continue;
    if (arg[0] != '-') return 1;
  }
  return 0;
}

static void driver_c_maybe_inject_env_cli(int *argc_io, char ***argv_io) {
  int argc = (argc_io != NULL) ? *argc_io : 0;
  char **argv = (argv_io != NULL) ? *argv_io : NULL;
  if (argc <= 0 || argv == NULL) return;

  const char *env_input = getenv("BACKEND_INPUT");
  const char *env_output = getenv("BACKEND_OUTPUT");
  const char *env_target = getenv("BACKEND_TARGET");
  const char *env_linker = getenv("BACKEND_LINKER");

  int add_input = (env_input != NULL && env_input[0] != '\0' &&
                   !driver_c_has_flag_value(argc, argv, "--input") &&
                   !driver_c_has_positional_input(argc, argv)) ? 1 : 0;
  int add_output = (env_output != NULL && env_output[0] != '\0' &&
                    !driver_c_has_flag_value(argc, argv, "--output")) ? 1 : 0;
  int add_target = (env_target != NULL && env_target[0] != '\0' &&
                    !driver_c_has_flag_value(argc, argv, "--target")) ? 1 : 0;
  int add_linker = (env_linker != NULL && env_linker[0] != '\0' &&
                    !driver_c_has_flag_value(argc, argv, "--linker")) ? 1 : 0;

  int extra = add_input + add_output + add_target + add_linker;
  if (extra <= 0) return;

  char **out = (char **)cheng_malloc((int32_t)(argc + extra + 1) * (int32_t)sizeof(char *));
  if (out == NULL) return;
  for (int i = 0; i < argc; ++i) out[i] = argv[i];
  int w = argc;
  if (add_input) out[w++] = driver_c_make_flag_value("--input", env_input);
  if (add_output) out[w++] = driver_c_make_flag_value("--output", env_output);
  if (add_target) out[w++] = driver_c_make_flag_value("--target", env_target);
  if (add_linker) out[w++] = driver_c_make_flag_value("--linker", env_linker);
  out[w] = NULL;
  *argc_io = w;
  *argv_io = out;
}

__attribute__((weak)) void driver_c_capture_cmdline(int32_t argc, void *argv_void) {
  g_driver_cli_argc = argc;
  g_driver_cli_argv = (char **)argv_void;
}

__attribute__((weak)) int32_t driver_c_capture_cmdline_keep(int32_t argc, void *argv_void) {
  driver_c_capture_cmdline(argc, argv_void);
  return argc;
}

int32_t driver_c_arg_count(void) {
  if (g_driver_cli_argc > 1 && g_driver_cli_argv != NULL) return g_driver_cli_argc - 1;
  if (__cheng_rt_paramCount != NULL) {
    int32_t argc = __cheng_rt_paramCount();
    if (argc > 1 && argc <= 257) return argc - 1;
  }
  return 0;
}

char *driver_c_arg_copy(int32_t i) {
  if (g_driver_cli_argv == NULL && __cheng_rt_paramStrCopy != NULL && i > 0) {
    char *rt = __cheng_rt_paramStrCopy(i);
    if (rt != NULL) return rt;
  }
  if (g_driver_cli_argv == NULL || i <= 0 || i >= g_driver_cli_argc) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  const char *raw = g_driver_cli_argv[i];
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

int32_t driver_c_help_requested(void) {
  if (g_driver_cli_argv != NULL && g_driver_cli_argc > 1 && g_driver_cli_argc <= 4096) {
    for (int32_t i = 1; i < g_driver_cli_argc; ++i) {
      const char *arg = g_driver_cli_argv[i];
      if (arg == NULL) continue;
      if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
    }
    return 0;
  }
  if (__cheng_rt_paramCount != NULL && __cheng_rt_paramStrCopy != NULL) {
    int32_t argc = __cheng_rt_paramCount();
    if (argc > 1 && argc <= 4096) {
      for (int32_t i = 1; i < argc; ++i) {
        char *arg = __cheng_rt_paramStrCopy(i);
        if (arg == NULL) continue;
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
      }
    }
  }
  return 0;
}

__attribute__((weak)) int32_t driver_c_argv_is_help(int32_t argc, void *argv_void) {
  char **argv = (char **)argv_void;
  if (argc != 2 || argv == NULL) return 0;
  const char *arg1 = argv[1];
  if (arg1 == NULL) return 0;
  return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
}

__attribute__((weak)) int32_t driver_c_cli_help_requested(void) {
  if (g_driver_cli_argc > 1 && g_driver_cli_argv != NULL) {
    return driver_c_argv_is_help(g_driver_cli_argc, g_driver_cli_argv);
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

__attribute__((weak)) int32_t driver_c_env_nonempty(const char *name) {
  const char *raw = getenv(name != NULL ? name : "");
  return (raw != NULL && raw[0] != '\0') ? 1 : 0;
}

__attribute__((weak)) int32_t driver_c_env_eq(const char *name, const char *expected) {
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

char *driver_c_active_output_path(const char *input_path) {
  const char *raw = getenv("BACKEND_OUTPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_OUTPUT");
  return (char *)(input_path != NULL ? input_path : "");
}

char *driver_c_active_output_copy(void) {
  const char *raw = getenv("BACKEND_OUTPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_OUTPUT");
  return (char *)"";
}

char *driver_c_active_input_path(void) {
  const char *raw = getenv("BACKEND_INPUT");
  if (raw != NULL && raw[0] != '\0') return driver_c_getenv_copy("BACKEND_INPUT");
  return (char *)"";
}

char *driver_c_active_target(void) {
  const char *raw = getenv("BACKEND_TARGET");
  if (raw == NULL || raw[0] == '\0' ||
      strcmp(raw, "auto") == 0 || strcmp(raw, "native") == 0 || strcmp(raw, "host") == 0) {
    return (char *)driver_c_host_target_default();
  }
  return driver_c_getenv_copy("BACKEND_TARGET");
}

char *driver_c_active_linker(void) {
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
  if (cheng_file_exists(path) == 0) {
    fprintf(stderr, "backend_driver: missing output after %s: %s\n", phase_text, path);
    exit(1);
  }
  if (cheng_file_size(path) <= 0) {
    fprintf(stderr, "backend_driver: empty output after %s: %s\n", phase_text, path);
    exit(1);
  }
}

int32_t driver_c_finish_emit_obj(const char *path) {
  driver_c_require_output_file_or_die(path, "emit_obj");
  driver_c_boot_marker(5);
  return 0;
}

__attribute__((weak)) int32_t driver_c_write_exact_file(const char *path, void *buffer, int32_t len) {
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

int32_t driver_c_emit_obj_default(void *module, const char *target, const char *path) {
  if (driver_emitObjFromModuleDefault != NULL) {
    if (module == NULL) return -11;
    if (path == NULL || path[0] == '\0') return -12;
    if (target == NULL) target = "";
    return driver_emitObjFromModuleDefault(module, target, path);
  }
  cheng_seq_u8_header obj;
  memset(&obj, 0, sizeof(obj));
  if (uirEmitObjFromModuleOrPanic == NULL) return -10;
  if (module == NULL) return -11;
  if (path == NULL || path[0] == '\0') return -12;
  if (target == NULL) target = "";
  uirEmitObjFromModuleOrPanic(&obj, module, 0, target, "", false, false, 0, "autovec");
  if (obj.len <= 0) return -13;
  if (obj.buffer == NULL) return -14;
  return driver_c_write_exact_file(path, obj.buffer, obj.len);
}

int32_t driver_c_link_tmp_obj_default(const char *output_path, const char *obj_path,
                                      const char *target, const char *linker) {
  const char *target_text = (target != NULL) ? target : "";
  const char *linker_text = (linker != NULL && linker[0] != '\0') ? linker : "system";
  const char *runtime_c = "/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c";
  const char *cflags = getenv("BACKEND_CFLAGS");
  const char *ldflags = getenv("BACKEND_LDFLAGS");
  const char *cflags_text = (cflags != NULL) ? cflags : "";
  const char *ldflags_text = (ldflags != NULL) ? ldflags : "";
  if (output_path == NULL || output_path[0] == '\0') return -20;
  if (obj_path == NULL || obj_path[0] == '\0') return -21;
  if (strcmp(linker_text, "system") != 0) return -22;
  (void)target_text;
  size_t need = strlen(obj_path) + strlen(runtime_c) + strlen(output_path) +
                strlen(cflags_text) + strlen(ldflags_text) + 96u;
  char *cmd = (char *)malloc(need);
  if (cmd == NULL) return -23;
  snprintf(cmd, need, "cc %s '%s' '%s' %s -o '%s'",
           cflags_text, obj_path, runtime_c, ldflags_text, output_path);
  int rc = system(cmd);
  free(cmd);
  if (rc != 0) return -24;
  if (cheng_file_exists(output_path) == 0) return -25;
  if (cheng_file_size(output_path) <= 0) return -26;
  return 0;
}

int32_t driver_c_link_tmp_obj_system(const char *output_path, const char *obj_path,
                                     const char *target) {
  return driver_c_link_tmp_obj_default(output_path, obj_path, target, "system");
}

int32_t driver_c_build_emit_obj_default(const char *input_path, const char *target,
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

int32_t driver_c_build_active_obj_default(void) {
  char *input_path = driver_c_active_input_path();
  if (input_path == NULL || input_path[0] == '\0') return 2;
  char *target = driver_c_active_target();
  char *output_path = driver_c_active_output_path(input_path);
  return driver_c_build_emit_obj_default(input_path, target, output_path);
}

int32_t driver_c_build_link_exe_default(const char *input_path, const char *target,
                                        const char *output_path, const char *linker) {
  (void)linker;
  if (input_path == NULL || input_path[0] == '\0') return -40;
  if (target == NULL || target[0] == '\0') return -41;
  if (output_path == NULL || output_path[0] == '\0') return -42;
  const char *suffix = ".tmp.linkobj";
  size_t need = strlen(output_path) + strlen(suffix) + 1u;
  char *obj_path = (char *)malloc(need);
  if (obj_path == NULL) return -43;
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

void *driver_c_build_module_stage1(const char *input_path, const char *target) {
  if (input_path == NULL || input_path[0] == '\0') return NULL;
  if (target == NULL) target = "";
  if (uirCoreBuildModuleFromFileStage1OrPanic != NULL) {
    return uirCoreBuildModuleFromFileStage1OrPanic(input_path, target);
  }
  if (driver_buildActiveModulePtrs != NULL) {
    return driver_buildActiveModulePtrs((void *)input_path, (void *)target);
  }
  return NULL;
}

int32_t driver_c_build_active_exe_default(void) {
  char *input_path = driver_c_active_input_path();
  if (input_path == NULL || input_path[0] == '\0') return 2;
  char *target = driver_c_active_target();
  char *output_path = driver_c_active_output_path(input_path);
  char *linker = driver_c_active_linker();
  return driver_c_build_link_exe_default(input_path, target, output_path, linker);
}

__attribute__((weak)) int32_t driver_c_run_default(void) {
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

int32_t driver_c_finish_single_link(const char *path, const char *obj_path) {
  driver_c_require_output_file_or_die(path, "single_link");
  if (driver_c_env_bool("BACKEND_KEEP_TMP_LINKOBJ", 0) == 0 && obj_path != NULL && obj_path[0] != '\0') {
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

__attribute__((weak)) void driver_c_boot_marker(int32_t code) {
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

__attribute__((weak)) int32_t libc_remove(const char *path) { return remove(path); }

__attribute__((weak)) int32_t libc_rename(const char *oldpath, const char *newpath) {
  return rename(oldpath, newpath);
}

/*
 * Keep self-link runtime object symbol closure stable when composed from
 * backend_mm object + bridge objects (without full selflink shim).
 */
__attribute__((weak)) uint64_t processOptionMask(int32_t opt) {
  if (opt < 0 || opt >= 64) return 0ull;
  return 1ull << (uint32_t)opt;
}

__attribute__((weak)) char *sliceStr(char *text, int32_t start, int32_t stop) {
  if (text == NULL) return NULL;
  int32_t n = (int32_t)strlen(text);
  if (start < 0) start = 0;
  if (stop < start) stop = start;
  if (start > n) start = n;
  if (stop > n) stop = n;
  int32_t m = stop - start;
  char *out = (char *)malloc((size_t)m + 1u);
  if (out == NULL) return NULL;
  if (m > 0) memcpy(out, text + start, (size_t)m);
  out[m] = '\0';
  return out;
}

__attribute__((weak)) int64_t libc_write(int32_t fd, void *data, int64_t n) {
  if (fd < 0 || data == NULL || n <= 0) return 0;
  ssize_t wrote = write(fd, data, (size_t)n);
  if (wrote < 0) return -1;
  return (int64_t)wrote;
}

__attribute__((weak)) int32_t c_fflush(void *stream) { return libc_fflush(stream); }

/*
 * Stable raw byte writer for backend_driver object emission. This bypasses
 * stage0/stage2 variations in std/os write helpers and writes through libc.
 * Returns 1 on success, 0 on failure.
 */
__attribute__((weak)) int32_t backend_driver_write_file_bytes(const char *path,
                                                              const void *data,
                                                              int64_t len) {
  if (path == NULL || path[0] == '\0' || len < 0) {
    return 0;
  }
  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    return 0;
  }
  if (data != NULL && len > 0) {
    size_t want = (size_t)len;
    size_t wrote = fwrite(data, 1u, want, f);
    if (wrote != want) {
      fclose(f);
      return 0;
    }
  }
  fflush(f);
  if (fclose(f) != 0) {
    return 0;
  }
  return 1;
}

__attribute__((weak)) char *lower_c_getenv(const char *name) { return getenv(name); }

__attribute__((weak)) int32_t linkerCore_file_exists(const char *path) {
  return cheng_file_exists(path);
}

__attribute__((weak)) void machoSeqInitEmpty_int32(cheng_seq_i32 *items) {
  if (items == NULL) {
    return;
  }
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_MachoX64Reloc(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_MachoX64Sym(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_str(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_uint64(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

__attribute__((weak)) void mx64_seqInitEmpty_uint8(cheng_seq_any *items) {
  if (items == NULL) return;
  items->buffer = NULL;
  items->len = 0;
  items->cap = 0;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
