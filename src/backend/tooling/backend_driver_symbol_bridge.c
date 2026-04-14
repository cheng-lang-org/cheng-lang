#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "../../runtime/native/cheng_sidecar_loader.h"
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__APPLE__)
#define CHENG_WEAK_IMPORT __attribute__((weak_import))
#else
#define CHENG_WEAK_IMPORT __attribute__((weak))
#endif

/* Runtime hooks provided by the backend runtime surface. */
extern int32_t cheng_file_exists(const char *path);
extern int64_t cheng_file_mtime(const char *path);
extern int64_t cheng_file_size(const char *path);
extern void cheng_register_string_meta(const char *ptr, int32_t len);
extern int32_t cheng_open_w_trunc(const char *path);
extern int32_t cheng_fd_write(int32_t fd, const char *data, int32_t len);
extern int32_t libc_close(int32_t fd);
extern void *cheng_malloc(int32_t size);
extern int32_t driver_backendBuildActiveObjNativePtrs(void *input_raw, void *target_raw, void *output_raw);
extern int32_t driver_backendBuildActiveExeNativePtrs(void *input_raw, void *target_raw, void *output_raw, void *linker_raw);
extern void cheng_iometer_call(void *hook, int32_t op, int64_t bytes);
extern void *cheng_jpeg_decode(void *data, int32_t len, void *out_w, void *out_h);
extern void cheng_jpeg_free(void *p);
extern int32_t driver_c_arg_count(void);
extern char *driver_c_arg_copy(int32_t i);
extern int32_t __cheng_rt_paramCount(void);
extern const char *__cheng_rt_paramStr(int32_t i);
extern char *__cheng_rt_paramStrCopy(int32_t i);
extern void __cheng_setCmdLine(int32_t argc, const char **argv);
extern int32_t backendMain(void) CHENG_WEAK_IMPORT;
#if !defined(CHENG_BACKEND_DRIVER_ENTRY_SHIM)
extern int32_t toolingMainWithCount(int32_t count) CHENG_WEAK_IMPORT;
#endif
typedef struct {
  int32_t len;
  int32_t cap;
  void *buffer;
} cheng_seq_u8_header;
__attribute__((weak)) void driver_c_capture_cmdline(int32_t argc, void *argv_void);
__attribute__((weak)) void driver_c_boot_marker(int32_t code);
int32_t driver_c_finish_single_link(const char *path, const char *obj_path);
int32_t driver_c_finish_emit_obj(const char *path);
void *driver_c_build_module_stage1(const char *input_path, const char *target);
void *driver_c_build_module_stage1_direct(const char *input_path, const char *target);
static void driver_c_maybe_inject_env_cli(int *argc_io, char ***argv_io);
static void driver_c_sync_cli_env(int argc, char **argv);
static int driver_c_has_flag_value(int argc, char **argv, const char *flag);

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
  void *buffer;
  int32_t len;
  int32_t cap;
} cheng_seq_i32;

typedef struct ChengStrBridge {
  const char *ptr;
  int32_t len;
  int32_t store_id;
  int32_t flags;
} ChengStrBridge;

enum {
  CHENG_STR_BRIDGE_FLAG_NONE = 0,
  CHENG_STR_BRIDGE_FLAG_OWNED = 1,
};

static ChengStrBridge driver_c_bridge_from_owned(char *ptr);

typedef struct {
  void *buffer;
  int32_t len;
  int32_t cap;
} cheng_seq_any;

typedef void (*cheng_emit_obj_from_module_fn)(cheng_seq_any *out, void *module, int32_t opt_level,
                                              const char *target, const char *obj_writer,
                                              int32_t validate_module, int32_t uir_simd_enabled,
                                              int32_t uir_simd_max_width,
                                              const char *uir_simd_policy);
typedef int32_t (*cheng_driver_emit_obj_default_fn)(void *module, const char *target,
                                                    const char *out_path);
typedef int32_t (*cheng_driver_bool_dummy_fn)(int32_t);
typedef int32_t (*cheng_tooling_main_with_count_fn)(int32_t);
typedef void *(*cheng_build_active_module_ptrs_fn)(void *input_raw, void *target_raw);
typedef void (*cheng_global_init_fn)(void);
typedef void *(*cheng_build_module_stage1_fn)(const char *path, const char *target);
typedef void *(*cheng_build_module_stage1_target_facade_fn)(const char *path,
                                                            const char *target);
typedef void *(*cheng_build_module_stage1_facade_fn)(const char *path);
typedef int32_t (*cheng_build_emit_obj_stage1_target_fn)(const char *path, const char *target,
                                                         const char *output_path);

extern void *driver_export_buildModuleFromFileStage1TargetRetained(const char *path,
                                                                   const char *target)
    CHENG_WEAK_IMPORT;
extern void *driver_buildModuleFromFileStage1TargetRetained(const char *path, const char *target)
    CHENG_WEAK_IMPORT;
extern void *driver_export_buildModuleFromFileStage1Retained(const char *path)
    CHENG_WEAK_IMPORT;
extern void *driver_buildModuleFromFileStage1Retained(const char *path) CHENG_WEAK_IMPORT;
extern void *driver_export_uirBuildModuleFromFileStage1TargetOrPanic(const char *path,
                                                                     const char *target)
    CHENG_WEAK_IMPORT;
extern void *uirBuildModuleFromFileStage1TargetOrPanic(const char *path, const char *target)
    CHENG_WEAK_IMPORT;
extern void *driver_export_uirBuildModuleFromFileStage1OrPanic(const char *path)
    CHENG_WEAK_IMPORT;
extern void *uirBuildModuleFromFileStage1OrPanic(const char *path) CHENG_WEAK_IMPORT;
extern int32_t driver_export_emit_obj_from_module_default_impl(void *module,
                                                               const char *target,
                                                               const char *path)
    CHENG_WEAK_IMPORT;
extern int32_t driver_emit_obj_from_module_default_impl(void *module, const char *target,
                                                        const char *path) CHENG_WEAK_IMPORT;
extern int32_t driver_export_build_emit_obj_from_file_stage1_target_impl(const char *path,
                                                                         const char *target,
                                                                         const char *output_path)
    CHENG_WEAK_IMPORT;
extern int32_t driver_build_emit_obj_from_file_stage1_target_impl(const char *path,
                                                                  const char *target,
                                                                  const char *output_path)
    CHENG_WEAK_IMPORT;
extern void *uirCoreBuildModuleFromFileStage1OrPanic(const char *path, const char *target)
    CHENG_WEAK_IMPORT;
extern void uirEmitObjFromModuleOrPanic(cheng_seq_any *out, void *module, int32_t opt_level,
                                        const char *target, const char *obj_writer,
                                        int32_t validate_module, int32_t uir_simd_enabled,
                                        int32_t uir_simd_max_width,
                                        const char *uir_simd_policy) CHENG_WEAK_IMPORT;

static cheng_build_module_stage1_target_facade_fn
driver_c_resolve_build_module_stage1_target_retained(void) {
  if (driver_export_buildModuleFromFileStage1TargetRetained != NULL) {
    return driver_export_buildModuleFromFileStage1TargetRetained;
  }
  if (driver_buildModuleFromFileStage1TargetRetained != NULL) {
    return driver_buildModuleFromFileStage1TargetRetained;
  }
  {
    cheng_build_module_stage1_target_facade_fn fn =
        (cheng_build_module_stage1_target_facade_fn)dlsym(
            RTLD_DEFAULT, "driver_export_buildModuleFromFileStage1TargetRetained");
    if (fn != NULL) return fn;
  }
  return (cheng_build_module_stage1_target_facade_fn)dlsym(
      RTLD_DEFAULT, "driver_buildModuleFromFileStage1TargetRetained");
}

static cheng_build_module_stage1_target_facade_fn
driver_c_resolve_build_module_stage1_target_facade(void) {
  if (driver_export_uirBuildModuleFromFileStage1TargetOrPanic != NULL) {
    return driver_export_uirBuildModuleFromFileStage1TargetOrPanic;
  }
  if (uirBuildModuleFromFileStage1TargetOrPanic != NULL) {
    return uirBuildModuleFromFileStage1TargetOrPanic;
  }
  {
    cheng_build_module_stage1_target_facade_fn fn =
        (cheng_build_module_stage1_target_facade_fn)dlsym(
            RTLD_DEFAULT, "driver_export_uirBuildModuleFromFileStage1TargetOrPanic");
    if (fn != NULL) return fn;
  }
  return (cheng_build_module_stage1_target_facade_fn)dlsym(
      RTLD_DEFAULT, "uirBuildModuleFromFileStage1TargetOrPanic");
}

static cheng_build_module_stage1_facade_fn driver_c_resolve_build_module_stage1_retained(void) {
  if (driver_export_buildModuleFromFileStage1Retained != NULL) {
    return driver_export_buildModuleFromFileStage1Retained;
  }
  if (driver_buildModuleFromFileStage1Retained != NULL) {
    return driver_buildModuleFromFileStage1Retained;
  }
  {
    cheng_build_module_stage1_facade_fn fn =
        (cheng_build_module_stage1_facade_fn)dlsym(
            RTLD_DEFAULT, "driver_export_buildModuleFromFileStage1Retained");
    if (fn != NULL) return fn;
  }
  return (cheng_build_module_stage1_facade_fn)dlsym(
      RTLD_DEFAULT, "driver_buildModuleFromFileStage1Retained");
}

static cheng_build_module_stage1_fn driver_c_resolve_build_module_stage1_raw(void) {
  if (uirCoreBuildModuleFromFileStage1OrPanic != NULL) {
    return uirCoreBuildModuleFromFileStage1OrPanic;
  }
  return (cheng_build_module_stage1_fn)dlsym(RTLD_DEFAULT, "uirCoreBuildModuleFromFileStage1OrPanic");
}

static cheng_build_module_stage1_facade_fn driver_c_resolve_build_module_stage1_facade(void) {
  if (driver_export_uirBuildModuleFromFileStage1OrPanic != NULL) {
    return driver_export_uirBuildModuleFromFileStage1OrPanic;
  }
  if (uirBuildModuleFromFileStage1OrPanic != NULL) {
    return uirBuildModuleFromFileStage1OrPanic;
  }
  {
    cheng_build_module_stage1_facade_fn fn =
        (cheng_build_module_stage1_facade_fn)dlsym(
            RTLD_DEFAULT, "driver_export_uirBuildModuleFromFileStage1OrPanic");
    if (fn != NULL) return fn;
  }
  return (cheng_build_module_stage1_facade_fn)dlsym(
      RTLD_DEFAULT, "uirBuildModuleFromFileStage1OrPanic");
}

static cheng_driver_emit_obj_default_fn driver_c_resolve_emit_obj_default_impl(void) {
  if (driver_export_emit_obj_from_module_default_impl != NULL) {
    return driver_export_emit_obj_from_module_default_impl;
  }
  if (driver_emit_obj_from_module_default_impl != NULL) {
    return driver_emit_obj_from_module_default_impl;
  }
  {
    cheng_driver_emit_obj_default_fn fn =
        (cheng_driver_emit_obj_default_fn)dlsym(
            RTLD_DEFAULT, "driver_export_emit_obj_from_module_default_impl");
    if (fn != NULL) return fn;
  }
  return (cheng_driver_emit_obj_default_fn)dlsym(
      RTLD_DEFAULT, "driver_emit_obj_from_module_default_impl");
}

static cheng_build_emit_obj_stage1_target_fn
driver_c_resolve_build_emit_obj_stage1_target(void) {
  if (driver_export_build_emit_obj_from_file_stage1_target_impl != NULL) {
    return driver_export_build_emit_obj_from_file_stage1_target_impl;
  }
  if (driver_build_emit_obj_from_file_stage1_target_impl != NULL) {
    return driver_build_emit_obj_from_file_stage1_target_impl;
  }
  {
    cheng_build_emit_obj_stage1_target_fn fn =
        (cheng_build_emit_obj_stage1_target_fn)dlsym(
            RTLD_DEFAULT, "driver_export_build_emit_obj_from_file_stage1_target_impl");
    if (fn != NULL) return fn;
  }
  return (cheng_build_emit_obj_stage1_target_fn)dlsym(
      RTLD_DEFAULT, "driver_build_emit_obj_from_file_stage1_target_impl");
}

static cheng_emit_obj_from_module_fn driver_c_resolve_uir_emit_obj(void) {
  if (uirEmitObjFromModuleOrPanic != NULL) {
    return uirEmitObjFromModuleOrPanic;
  }
  return (cheng_emit_obj_from_module_fn)dlsym(RTLD_DEFAULT, "uirEmitObjFromModuleOrPanic");
}

typedef enum ChengDriverModuleKind {
  CHENG_DRIVER_MODULE_KIND_LEGACY_RAW = 0,
  CHENG_DRIVER_MODULE_KIND_RAW = 1,
  CHENG_DRIVER_MODULE_KIND_RETAINED = 2,
  CHENG_DRIVER_MODULE_KIND_SIDECAR = 3,
} ChengDriverModuleKind;

typedef struct ChengDriverModuleHandle {
  uint32_t magic;
  uint32_t kind;
  void *payload;
} ChengDriverModuleHandle;

enum {
  CHENG_DRIVER_MODULE_HANDLE_MAGIC = 0x43444d48u,
};

static ChengDriverModuleHandle *driver_c_module_handle_try(void *module) {
  ChengDriverModuleHandle *handle = (ChengDriverModuleHandle *)module;
  if (module == NULL) return NULL;
  if (handle->magic != CHENG_DRIVER_MODULE_HANDLE_MAGIC) return NULL;
  return handle;
}

static void *driver_c_module_wrap(void *payload, ChengDriverModuleKind kind) {
  ChengDriverModuleHandle *handle;
  if (payload == NULL) return NULL;
  if (driver_c_module_handle_try(payload) != NULL) return payload;
  handle = (ChengDriverModuleHandle *)malloc(sizeof(ChengDriverModuleHandle));
  if (handle == NULL) return payload;
  handle->magic = CHENG_DRIVER_MODULE_HANDLE_MAGIC;
  handle->kind = (uint32_t)kind;
  handle->payload = payload;
  return (void *)handle;
}

static ChengDriverModuleKind driver_c_module_kind(void *module) {
  ChengDriverModuleHandle *handle = driver_c_module_handle_try(module);
  if (handle == NULL) return CHENG_DRIVER_MODULE_KIND_LEGACY_RAW;
  return (ChengDriverModuleKind)handle->kind;
}

static void *driver_c_module_payload(void *module) {
  ChengDriverModuleHandle *handle = driver_c_module_handle_try(module);
  if (handle == NULL) return module;
  return handle->payload;
}

static void driver_c_module_dispose(void *module) {
  ChengDriverModuleHandle *handle = driver_c_module_handle_try(module);
  if (handle != NULL) free(handle);
}

static int driver_c_diag_enabled(void);
static void driver_c_diagf(const char *fmt, ...);

static int32_t driver_c_env_i32(const char *name, int32_t fallback) {
  char *end = NULL;
  long value;
  const char *raw = getenv(name);
  if (raw == NULL || raw[0] == '\0') return fallback;
  value = strtol(raw, &end, 10);
  if (end == raw) return fallback;
  return (int32_t)value;
}

static ChengSidecarBundleCache driver_c_sidecar_cache = {0};
static __thread int32_t driver_c_build_emit_obj_dispatch_depth = 0;
static __thread int32_t driver_c_build_module_dispatch_depth = 0;

static void driver_c_sidecar_after_open(void *handle, ChengSidecarDiagFn diag) {
  dlerror();
  cheng_global_init_fn global_init = (cheng_global_init_fn)dlsym(handle, "__cheng_global_init");
  const char *init_err = dlerror();
  if (diag != NULL) {
    diag("[driver_c_sidecar_bundle_handle] global_init=%p err='%s'\n",
         (void *)global_init, init_err != NULL ? init_err : "");
  }
  if (global_init != NULL) global_init();
}

static int driver_c_diag_enabled(void) {
  const char *raw = getenv("BACKEND_DEBUG_BOOT");
  if (raw == NULL || raw[0] == '\0') return 0;
  if (raw[0] == '0' && raw[1] == '\0') return 0;
  return 1;
}

static void driver_c_diagf(const char *fmt, ...) {
  if (!driver_c_diag_enabled()) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fflush(stderr);
}

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

static int driver_c_env_enabled_default_true(const char *name) {
  const char *raw = getenv(name);
  if (raw == NULL || raw[0] == '\0') return 1;
  if ((raw[0] == '0' && raw[1] == '\0') || strcmp(raw, "false") == 0 ||
      strcmp(raw, "FALSE") == 0 || strcmp(raw, "no") == 0 ||
      strcmp(raw, "NO") == 0) {
    return 0;
  }
  return 1;
}

static int driver_c_path_contains(const char *path, const char *needle) {
  if (path == NULL || needle == NULL || needle[0] == '\0') return 0;
  return strstr(path, needle) != NULL;
}

static unsigned long long driver_c_path_hash64(const char *text) {
  unsigned long long hash = 1469598103934665603ULL;
  if (text == NULL) return hash;
  while (*text != '\0') {
    hash ^= (unsigned long long)(unsigned char)(*text);
    hash *= 1099511628211ULL;
    text += 1;
  }
  return hash;
}

static int driver_c_mkdir_p(const char *path) {
  char buf[PATH_MAX];
  size_t n;
  char *p;
  struct stat st;
  if (path == NULL || path[0] == '\0') return 0;
  n = strlen(path);
  if (n >= sizeof(buf)) return 0;
  memcpy(buf, path, n + 1);
  for (p = buf + 1; *p != '\0'; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return 0;
    *p = '/';
  }
  if (mkdir(buf, 0755) != 0 && errno != EEXIST) return 0;
  if (stat(buf, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int driver_c_copy_executable_atomic(const char *src_path, const char *dst_path) {
  int src_fd = -1;
  int dst_fd = -1;
  ssize_t nread;
  char tmp_path[PATH_MAX];
  char buffer[32768];
  struct stat st;
  if (src_path == NULL || dst_path == NULL || src_path[0] == '\0' || dst_path[0] == '\0') return 0;
  if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", dst_path, (int)getpid()) <= 0) return 0;
  src_fd = open(src_path, O_RDONLY);
  if (src_fd < 0) goto fail;
  if (fstat(src_fd, &st) != 0) goto fail;
  dst_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (dst_fd < 0) goto fail;
  while ((nread = read(src_fd, buffer, sizeof(buffer))) > 0) {
    ssize_t off = 0;
    while (off < nread) {
      ssize_t nw = write(dst_fd, buffer + off, (size_t)(nread - off));
      if (nw < 0) goto fail;
      off += nw;
    }
  }
  if (nread < 0) goto fail;
  if (fchmod(dst_fd, st.st_mode & 0777 ? (st.st_mode & 0777) : 0755) != 0) goto fail;
  if (close(dst_fd) != 0) {
    dst_fd = -1;
    goto fail;
  }
  dst_fd = -1;
  if (close(src_fd) != 0) {
    src_fd = -1;
    goto fail;
  }
  src_fd = -1;
  if (rename(tmp_path, dst_path) != 0) goto fail;
  return 1;
fail:
  if (src_fd >= 0) close(src_fd);
  if (dst_fd >= 0) close(dst_fd);
  unlink(tmp_path);
  return 0;
}

static int driver_c_resolve_self_path(const char *argv0, char *out, size_t out_cap) {
  char raw[PATH_MAX];
  char *resolved;
  if (out == NULL || out_cap == 0) return 0;
  out[0] = '\0';
#if defined(__APPLE__)
  {
    uint32_t size = (uint32_t)sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) == 0) {
      resolved = realpath(raw, out);
      if (resolved != NULL) return 1;
      if (strlen(raw) < out_cap) {
        snprintf(out, out_cap, "%s", raw);
        return 1;
      }
    }
  }
#endif
#if defined(__linux__)
  {
    ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1u);
    if (n > 0 && (size_t)n < sizeof(raw)) {
      raw[n] = '\0';
      resolved = realpath(raw, out);
      if (resolved != NULL) return 1;
      if (strlen(raw) < out_cap) {
        snprintf(out, out_cap, "%s", raw);
        return 1;
      }
    }
  }
#endif
  if (argv0 != NULL && argv0[0] != '\0') {
    resolved = realpath(argv0, out);
    if (resolved != NULL) return 1;
    if (argv0[0] == '/' && strlen(argv0) < out_cap) {
      snprintf(out, out_cap, "%s", argv0);
      return 1;
    }
  }
  return 0;
}

static int driver_c_stage0_repo_root_from_path(const char *self_path, char *out, size_t out_cap) {
  const char *needle = NULL;
  const char *hit = NULL;
  size_t root_len;
  if (self_path == NULL || out == NULL || out_cap == 0) return 0;
  if ((hit = strstr(self_path, "/artifacts/backend_driver/cheng")) != NULL) {
    needle = hit;
  } else if ((hit = strstr(self_path, "/dist/releases/current/cheng")) != NULL) {
    needle = hit;
  } else if ((hit = strstr(self_path, "/artifacts/backend_selfhost_self_obj/cheng.stage2")) != NULL) {
    needle = hit;
  } else if ((hit = strstr(self_path, "/artifacts/backend_selfhost_self_obj/cheng.stage1")) != NULL) {
    needle = hit;
  }
  if (needle == NULL) return 0;
  root_len = (size_t)(needle - self_path);
  if (root_len == 0 || root_len >= out_cap) return 0;
  memcpy(out, self_path, root_len);
  out[root_len] = '\0';
  return 1;
}

static int driver_c_stage0_self_mirror_needed(const char *self_path) {
  if (self_path == NULL || self_path[0] == '\0') return 0;
  if (!driver_c_env_enabled_default_true("BACKEND_STAGE0_SELF_MIRROR")) return 0;
  if (driver_c_env_enabled_default_true("BACKEND_STAGE0_SELF_MIRROR_ACTIVE") &&
      getenv("BACKEND_STAGE0_SELF_MIRROR_ACTIVE") != NULL &&
      getenv("BACKEND_STAGE0_SELF_MIRROR_ACTIVE")[0] != '\0') {
    return 0;
  }
  if (driver_c_path_contains(self_path, "/chengcache/stage0_quarantine/cheng.")) return 0;
  if (driver_c_path_contains(self_path, "/artifacts/backend_driver/cheng")) return 1;
  if (driver_c_path_contains(self_path, "/dist/releases/current/cheng")) return 1;
  if (driver_c_path_contains(self_path, "/artifacts/backend_selfhost_self_obj/cheng.stage2")) return 1;
  if (driver_c_path_contains(self_path, "/artifacts/backend_selfhost_self_obj/cheng.stage1")) return 1;
  return 0;
}

static int driver_c_prepare_self_mirror_path(const char *self_path, char *out, size_t out_cap) {
  const char *env_qdir = getenv("CHENG_STAGE0_QUARANTINE_DIR");
  char repo_root[PATH_MAX];
  char qdir[PATH_MAX];
  const char *tag = "stage0";
  unsigned long long hash;
  if (out == NULL || out_cap == 0 || self_path == NULL || self_path[0] == '\0') return 0;
  out[0] = '\0';
  if (env_qdir != NULL && env_qdir[0] != '\0') {
    snprintf(qdir, sizeof(qdir), "%s", env_qdir);
  } else {
    if (!driver_c_stage0_repo_root_from_path(self_path, repo_root, sizeof(repo_root))) return 0;
    if (snprintf(qdir, sizeof(qdir), "%s/chengcache/stage0_quarantine", repo_root) <= 0) return 0;
  }
  if (!driver_c_mkdir_p(qdir)) return 0;
  if (driver_c_path_contains(self_path, "/artifacts/backend_driver/cheng")) tag = "driver";
  else if (driver_c_path_contains(self_path, "/dist/releases/current/cheng")) tag = "dist";
  else if (driver_c_path_contains(self_path, "/artifacts/backend_selfhost_self_obj/cheng.stage2")) tag = "stage2";
  else if (driver_c_path_contains(self_path, "/artifacts/backend_selfhost_self_obj/cheng.stage1")) tag = "stage1";
  hash = driver_c_path_hash64(self_path);
  if (snprintf(out, out_cap, "%s/cheng.direct.%s.%llx", qdir, tag, hash) <= 0) return 0;
  return 1;
}

static int driver_c_reexec_via_self_mirror(int argc, char **argv) {
  char self_path[PATH_MAX];
  char mirror_path[PATH_MAX];
  char **mirror_argv;
  int i;
  if (!driver_c_resolve_self_path((argv != NULL && argc > 0) ? argv[0] : NULL, self_path, sizeof(self_path))) {
    return -1;
  }
  if (!driver_c_stage0_self_mirror_needed(self_path)) return -1;
  if (!driver_c_prepare_self_mirror_path(self_path, mirror_path, sizeof(mirror_path))) {
    fprintf(stderr, "backend_driver: failed to prepare self mirror path for %s\n", self_path);
    return 125;
  }
  if (!driver_c_copy_executable_atomic(self_path, mirror_path)) {
    fprintf(stderr, "backend_driver: failed to mirror stage0 executable: %s -> %s\n",
            self_path, mirror_path);
    return 125;
  }
  if (setenv("BACKEND_STAGE0_SELF_MIRROR_ACTIVE", "1", 1) != 0) {
    fprintf(stderr, "backend_driver: failed to mark self mirror active\n");
    return 125;
  }
  if (setenv("BACKEND_STAGE0_SELF_SOURCE_PATH", self_path, 1) != 0) {
    fprintf(stderr, "backend_driver: failed to export self source path\n");
    return 125;
  }
  mirror_argv = (char **)malloc((size_t)(argc + 1) * sizeof(char *));
  if (mirror_argv == NULL) {
    fprintf(stderr, "backend_driver: failed to allocate mirrored argv\n");
    return 125;
  }
  mirror_argv[0] = mirror_path;
  for (i = 1; i < argc; ++i) mirror_argv[i] = argv[i];
  mirror_argv[argc] = NULL;
  execv(mirror_path, mirror_argv);
  fprintf(stderr, "backend_driver: failed to exec mirrored stage0 (%s): %s\n",
          mirror_path, strerror(errno));
  free(mirror_argv);
  return 126;
}

int32_t driver_c_backend_usage(void) {
  fputs("Usage:\n", stderr);
  fputs("  backend_driver [--input:<file>|<file>] [--output:<out>] [options]\n", stderr);
  fputs("\n", stderr);
  fputs("Core options:\n", stderr);
  fputs("  --emit:<exe|obj> --target:<triple|auto> --frontend:<stage1>\n", stderr);
  fputs("  --linker:<self|system> --obj-writer:<auto|elf|macho|coff>\n", stderr);
  fputs("  --build-track:<dev|release> --mm:<orc> --fn-sched:<ws>\n", stderr);
  fputs("  --whole-program --whole-program:0|1\n", stderr);
  fputs("  --opt-level:<N> --opt2 --no-opt2 --opt --no-opt\n", stderr);
  fputs("  --ldflags:<text>\n", stderr);
  fputs("  --jobs:<N> --fn-jobs:<N>\n", stderr);
  fputs("  --module-cache:<path>\n", stderr);
  fputs("  --multi-module-cache --multi-module-cache:0|1\n", stderr);
  fputs("  --module-cache-unstable-allow --module-cache-unstable-allow:0|1\n", stderr);
  fputs("  --multi --no-multi --multi-force --no-multi-force\n", stderr);
  fputs("  --incremental --no-incremental --allow-no-main\n", stderr);
  fputs("  --skip-global-init --runtime-obj:<path> --no-runtime-c\n", stderr);
  fputs("  --generic-mode:<dict> --generic-spec-budget:<N>\n", stderr);
  fputs("  --borrow-ir:<mir|stage1> --generic-lowering:<mir_dict>\n", stderr);
  fputs("  --android-api:<N> --compile-stamp-out:<path>\n", stderr);
  fputs("  --sidecar-mode:<cheng> --sidecar-bundle:<path>\n", stderr);
  fputs("  --sidecar-compiler:<path>\n", stderr);
  fputs("  --sidecar-child-mode:<cli|outer_cli> --sidecar-outer-compiler:<path>\n", stderr);
  fputs("  --profile --no-profile --uir-profile --no-uir-profile\n", stderr);
  fputs("  --uir-simd --no-uir-simd --uir-simd-max-width:<N> --uir-simd-policy:<name>\n", stderr);
  return 0;
}

__attribute__((weak)) int main(int argc, char **argv) {
  char **effective_argv = argv;
  int effective_argc = argc;
  {
    int mirror_rc = driver_c_reexec_via_self_mirror(argc, argv);
    if (mirror_rc >= 0) return mirror_rc;
  }
  if (backendMain != NULL) {
    driver_c_maybe_inject_env_cli(&effective_argc, &effective_argv);
    driver_c_sync_cli_env(effective_argc, effective_argv);
  }
  __cheng_setCmdLine((int32_t)effective_argc, (const char **)effective_argv);
  driver_c_capture_cmdline((int32_t)effective_argc, (void *)effective_argv);
#if !defined(CHENG_BACKEND_DRIVER_ENTRY_SHIM)
  cheng_tooling_main_with_count_fn tooling_main =
      (cheng_tooling_main_with_count_fn)dlsym(RTLD_DEFAULT, "toolingMainWithCount");
  if (tooling_main != NULL) {
    int32_t count = 0;
    if (effective_argc > 1 && effective_argc <= 4096) {
      count = (int32_t)effective_argc - 1;
    }
    return tooling_main(count);
  }
#endif
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

static int32_t driver_c_sidecar_builds_requested(void) {
  const char *prefer_env = getenv("BACKEND_UIR_PREFER_SIDECAR");
  if (prefer_env != NULL && prefer_env[0] != '\0') {
    if (prefer_env[0] == '0' && prefer_env[1] == '\0') return 0;
    return 1;
  }
  if (driver_c_env_bool("BACKEND_UIR_FORCE_SIDECAR", 0) != 0) {
    return 1;
  }
  {
    cheng_driver_bool_dummy_fn prefer_sidecar_fn =
        (cheng_driver_bool_dummy_fn)dlsym(RTLD_DEFAULT, "driver_export_prefer_sidecar_builds");
    if (prefer_sidecar_fn != NULL) {
      int32_t prefer = prefer_sidecar_fn(0);
      driver_c_diagf("[driver_c_prefer_sidecar_builds] export=%d\n", prefer);
      if (prefer != 0) return 1;
    }
  }
  return 0;
}

static int32_t driver_c_prefer_sidecar_builds(void) {
  return driver_c_sidecar_builds_requested();
}

__attribute__((weak)) int32_t driver_export_prefer_sidecar_builds(int32_t dummy) {
  (void)dummy;
  return 1;
}

__attribute__((weak)) char *driver_c_getenv_copy(const char *name) {
  const char *raw = getenv(name != NULL ? name : "");
  if (raw == NULL) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_register_string_meta(out, 0);
    }
    return out;
  }
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  memcpy(out, raw, len);
  out[len] = '\0';
  cheng_register_string_meta(out, (int32_t)len);
  return out;
}

__attribute__((weak)) ChengStrBridge driver_c_getenv_copy_bridge(const char *name) {
  return driver_c_bridge_from_owned(driver_c_getenv_copy(name));
}

__attribute__((weak)) void driver_c_getenv_copy_bridge_into(const char *name, ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_getenv_copy_bridge(name);
}

static char *driver_c_dup_cstr(const char *raw) {
  if (raw == NULL) raw = "";
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  if (len > 0) memcpy(out, raw, len);
  out[len] = '\0';
  cheng_register_string_meta(out, (int32_t)len);
  return out;
}

static ChengStrBridge driver_c_bridge_from_owned(char *ptr) {
  ChengStrBridge out;
  const char *safe = ptr != NULL ? ptr : "";
  out.ptr = safe;
  out.len = (int32_t)strlen(safe);
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_OWNED;
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

__attribute__((weak)) ChengStrBridge driver_c_cli_input_copy_bridge(void) {
  return driver_c_bridge_from_owned(driver_c_cli_input_copy());
}

__attribute__((weak)) void driver_c_cli_input_copy_bridge_into(ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_cli_input_copy_bridge();
}

__attribute__((weak)) char *driver_c_cli_output_copy(void) {
  return driver_c_cli_value_copy("--output");
}

__attribute__((weak)) ChengStrBridge driver_c_cli_output_copy_bridge(void) {
  return driver_c_bridge_from_owned(driver_c_cli_output_copy());
}

__attribute__((weak)) void driver_c_cli_output_copy_bridge_into(ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_cli_output_copy_bridge();
}

__attribute__((weak)) char *driver_c_cli_target_copy(void) {
  return driver_c_cli_value_copy("--target");
}

__attribute__((weak)) ChengStrBridge driver_c_cli_target_copy_bridge(void) {
  return driver_c_bridge_from_owned(driver_c_cli_target_copy());
}

__attribute__((weak)) void driver_c_cli_target_copy_bridge_into(ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_cli_target_copy_bridge();
}

__attribute__((weak)) char *driver_c_cli_linker_copy(void) {
  return driver_c_cli_value_copy("--linker");
}

__attribute__((weak)) ChengStrBridge driver_c_cli_linker_copy_bridge(void) {
  return driver_c_bridge_from_owned(driver_c_cli_linker_copy());
}

__attribute__((weak)) void driver_c_cli_linker_copy_bridge_into(ChengStrBridge *out) {
  if (out == NULL) return;
  *out = driver_c_cli_linker_copy_bridge();
}

__attribute__((weak)) char *driver_c_new_string(int32_t n) {
  if (n <= 0) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_register_string_meta(out, 0);
    }
    return out;
  }
  char *out = (char *)cheng_malloc(n + 1);
  if (out == NULL) return NULL;
  memset(out, 0, (size_t)n + 1u);
  cheng_register_string_meta(out, n);
  return out;
}

__attribute__((weak)) ChengStrBridge driver_c_str_from_utf8_copy_bridge(const char *raw, int32_t n) {
  if (raw == NULL || n <= 0) {
    return driver_c_bridge_from_owned(driver_c_new_string(0));
  }
  char *out = driver_c_new_string(n);
  if (out != NULL && n > 0) {
    memcpy(out, raw, (size_t)n);
  }
  return driver_c_bridge_from_owned(out);
}

__attribute__((weak)) ChengStrBridge __cheng_rt_paramStrCopyBridge(int32_t i) {
  return driver_c_bridge_from_owned(__cheng_rt_paramStrCopy(i));
}

__attribute__((weak)) void __cheng_rt_paramStrCopyBridgeInto(int32_t i, ChengStrBridge *out) {
  if (out == NULL) return;
  *out = __cheng_rt_paramStrCopyBridge(i);
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
  cheng_register_string_meta(out, (int32_t)(flag_len + value_len + 1u));
  return out;
}

static int driver_c_match_flag_value(const char *arg, const char *flag, const char **value_out) {
  size_t flag_len;
  if (value_out != NULL) *value_out = NULL;
  if (arg == NULL || flag == NULL || flag[0] == '\0') return 0;
  flag_len = strlen(flag);
  if (strcmp(arg, flag) == 0) return 1;
  if (strncmp(arg, flag, flag_len) == 0 && (arg[flag_len] == ':' || arg[flag_len] == '=')) {
    if (value_out != NULL) *value_out = arg + flag_len + 1u;
    return 1;
  }
  return 0;
}

static int driver_c_collect_flag_value(int argc, char **argv, const char *flag, const char **value_out) {
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    const char *inline_value = NULL;
    if (!driver_c_match_flag_value(arg, flag, &inline_value)) continue;
    if (inline_value != NULL) {
      if (value_out != NULL) *value_out = inline_value;
      return 1;
    }
    if (i + 1 < argc && argv[i + 1] != NULL) {
      if (value_out != NULL) *value_out = argv[i + 1];
      return 1;
    }
    if (value_out != NULL) *value_out = "";
    return 1;
  }
  return 0;
}

static int driver_c_collect_positional_input(int argc, char **argv, const char **value_out) {
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (arg == NULL || arg[0] == '\0') continue;
    if (arg[0] != '-') {
      if (value_out != NULL) *value_out = arg;
      return 1;
    }
  }
  return 0;
}

static void driver_c_sync_env_value_flag(int argc, char **argv,
                                         const char *flag,
                                         const char *env_name) {
  const char *value = NULL;
  if (!driver_c_collect_flag_value(argc, argv, flag, &value)) return;
  if (value == NULL || env_name == NULL || env_name[0] == '\0') return;
  setenv(env_name, value, 1);
}

static void driver_c_sync_env_bool_flag(int argc, char **argv,
                                        const char *positive_flag,
                                        const char *negative_flag,
                                        const char *env_name) {
  const char *value = NULL;
  int seen = 0;
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    const char *inline_value = NULL;
    if (driver_c_match_flag_value(arg, positive_flag, &inline_value)) {
      seen = 1;
      value = (inline_value != NULL) ? inline_value : "1";
      continue;
    }
    if (negative_flag != NULL && strcmp(arg != NULL ? arg : "", negative_flag) == 0) {
      seen = 1;
      value = "0";
      continue;
    }
  }
  if (!seen || value == NULL || env_name == NULL || env_name[0] == '\0') return;
  setenv(env_name, value, 1);
}

static void driver_c_sync_env_enable_flag(int argc, char **argv,
                                          const char *flag,
                                          const char *env_name) {
  if (!driver_c_has_flag_value(argc, argv, flag)) return;
  if (env_name == NULL || env_name[0] == '\0') return;
  setenv(env_name, "1", 1);
}

static void driver_c_sync_cli_env(int argc, char **argv) {
  const char *input_value = NULL;
  const char *env_input = NULL;
  if (argc <= 0 || argv == NULL) return;

  driver_c_sync_env_value_flag(argc, argv, "--input", "BACKEND_INPUT");
  env_input = getenv("BACKEND_INPUT");
  if ((env_input == NULL || env_input[0] == '\0') &&
      driver_c_collect_positional_input(argc, argv, &input_value) &&
      input_value != NULL && input_value[0] != '\0') {
    setenv("BACKEND_INPUT", input_value, 1);
  }
  driver_c_sync_env_value_flag(argc, argv, "--output", "BACKEND_OUTPUT");
  driver_c_sync_env_value_flag(argc, argv, "--target", "BACKEND_TARGET");
  driver_c_sync_env_value_flag(argc, argv, "--linker", "BACKEND_LINKER");
  driver_c_sync_env_value_flag(argc, argv, "--emit", "BACKEND_EMIT");
  driver_c_sync_env_value_flag(argc, argv, "--frontend", "BACKEND_FRONTEND");
  driver_c_sync_env_value_flag(argc, argv, "--build-track", "BACKEND_BUILD_TRACK");
  driver_c_sync_env_value_flag(argc, argv, "--mm", "MM");
  driver_c_sync_env_value_flag(argc, argv, "--fn-sched", "BACKEND_FN_SCHED");
  driver_c_sync_env_value_flag(argc, argv, "--opt-level", "BACKEND_OPT_LEVEL");
  driver_c_sync_env_value_flag(argc, argv, "--ldflags", "BACKEND_LDFLAGS");
  driver_c_sync_env_value_flag(argc, argv, "--runtime-obj", "BACKEND_RUNTIME_OBJ");
  driver_c_sync_env_value_flag(argc, argv, "--generic-mode", "GENERIC_MODE");
  driver_c_sync_env_value_flag(argc, argv, "--generic-spec-budget", "GENERIC_SPEC_BUDGET");
  driver_c_sync_env_value_flag(argc, argv, "--generic-lowering", "GENERIC_LOWERING");
  driver_c_sync_env_value_flag(argc, argv, "--jobs", "BACKEND_JOBS");
  driver_c_sync_env_value_flag(argc, argv, "--fn-jobs", "BACKEND_FN_JOBS");
  driver_c_sync_env_value_flag(argc, argv, "--module-cache", "BACKEND_MODULE_CACHE");
  driver_c_sync_env_value_flag(argc, argv, "--sidecar-mode", "BACKEND_UIR_SIDECAR_MODE");
  driver_c_sync_env_value_flag(argc, argv, "--sidecar-bundle", "BACKEND_UIR_SIDECAR_BUNDLE");
  driver_c_sync_env_value_flag(argc, argv, "--sidecar-compiler", "BACKEND_UIR_SIDECAR_COMPILER");
  driver_c_sync_env_value_flag(argc, argv, "--sidecar-child-mode", "BACKEND_UIR_SIDECAR_CHILD_MODE");
  driver_c_sync_env_value_flag(argc, argv, "--sidecar-outer-compiler", "BACKEND_UIR_SIDECAR_OUTER_COMPILER");
  driver_c_sync_env_value_flag(argc, argv, "--borrow-ir", "BORROW_IR");
  {
    const char *emit_mode = getenv("BACKEND_EMIT");
    if (emit_mode != NULL && strcmp(emit_mode, "obj") == 0) {
      setenv("BACKEND_INTERNAL_ALLOW_EMIT_OBJ", "1", 1);
    }
  }
  {
    const char *sidecar_mode = getenv("BACKEND_UIR_SIDECAR_MODE");
    if (sidecar_mode != NULL && strcmp(sidecar_mode, "cheng") == 0) {
      setenv("BACKEND_UIR_SIDECAR_DISABLE", "0", 1);
      setenv("BACKEND_UIR_PREFER_SIDECAR", "1", 1);
      setenv("BACKEND_UIR_FORCE_SIDECAR", "1", 1);
    }
  }

  driver_c_sync_env_bool_flag(argc, argv, "--opt", "--no-opt", "BACKEND_OPT");
  driver_c_sync_env_bool_flag(argc, argv, "--opt2", "--no-opt2", "BACKEND_OPT2");
  driver_c_sync_env_bool_flag(argc, argv, "--multi", "--no-multi", "BACKEND_MULTI");
  driver_c_sync_env_bool_flag(argc, argv, "--multi-force", "--no-multi-force", "BACKEND_MULTI_FORCE");
  driver_c_sync_env_bool_flag(argc, argv, "--incremental", "--no-incremental", "BACKEND_INCREMENTAL");
  driver_c_sync_env_bool_flag(argc, argv, "--whole-program", "--no-whole-program", "BACKEND_WHOLE_PROGRAM");
  driver_c_sync_env_bool_flag(argc, argv, "--multi-module-cache", "--no-multi-module-cache", "BACKEND_MULTI_MODULE_CACHE");
  driver_c_sync_env_bool_flag(argc, argv, "--module-cache-unstable-allow", "--no-module-cache-unstable-allow", "BACKEND_MODULE_CACHE_UNSTABLE_ALLOW");
  driver_c_sync_env_bool_flag(argc, argv, "--no-runtime-c", NULL, "BACKEND_NO_RUNTIME_C");

  driver_c_sync_env_enable_flag(argc, argv, "--allow-no-main", "BACKEND_ALLOW_NO_MAIN");
  driver_c_sync_env_enable_flag(argc, argv, "--skip-global-init", "BACKEND_SKIP_GLOBAL_INIT");
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
  int32_t argc = __cheng_rt_paramCount();
  if (argc > 1 && argc <= 257) return argc - 1;
  return 0;
}

char *driver_c_arg_copy(int32_t i) {
  if (g_driver_cli_argv == NULL && i > 0) {
    char *rt = __cheng_rt_paramStrCopy(i);
    if (rt != NULL) return rt;
  }
  if (g_driver_cli_argv == NULL || i <= 0 || i >= g_driver_cli_argc) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_register_string_meta(out, 0);
    }
    return out;
  }
  const char *raw = g_driver_cli_argv[i];
  if (raw == NULL) {
    char *out = (char *)cheng_malloc(1);
    if (out != NULL) {
      out[0] = '\0';
      cheng_register_string_meta(out, 0);
    }
    return out;
  }
  size_t len = strlen(raw);
  char *out = (char *)cheng_malloc((int32_t)len + 1);
  if (out == NULL) return NULL;
  memcpy(out, raw, len);
  out[len] = '\0';
  cheng_register_string_meta(out, (int32_t)len);
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
  int32_t argc = __cheng_rt_paramCount();
  if (argc != 2) return 0;
  char *arg1 = __cheng_rt_paramStrCopy(1);
  if (arg1 == NULL) return 0;
  return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
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

static int32_t driver_c_target_is_host(const char *target) {
  const char *host = driver_c_host_target_default();
  if (target == NULL || target[0] == '\0') return 0;
  if (host != NULL && host[0] != '\0' && strcmp(target, host) == 0) return 1;
  if ((strcmp(host, "arm64-apple-darwin") == 0 || strcmp(host, "aarch64-apple-darwin") == 0) &&
      driver_c_target_is_darwin_alias(target)) {
    return 1;
  }
  return 0;
}

static int32_t driver_c_path_has_suffix(const char *path, const char *suffix) {
  size_t path_len;
  size_t suffix_len;
  if (path == NULL || suffix == NULL) return 0;
  path_len = strlen(path);
  suffix_len = strlen(suffix);
  if (path_len < suffix_len) return 0;
  return strcmp(path + path_len - suffix_len, suffix) == 0 ? 1 : 0;
}

static int32_t driver_c_source_is_tooling_main(const char *input_path) {
  return driver_c_path_has_suffix(input_path, "/src/tooling/cheng_tooling.cheng");
}

static int32_t driver_c_tooling_launcher_enabled(void) {
  const char *raw = getenv("TOOLING_EMIT_SELFHOST_LAUNCHER");
  if (raw == NULL || raw[0] == '\0') return 1;
  return !(strcmp(raw, "0") == 0 || strcmp(raw, "false") == 0 || strcmp(raw, "FALSE") == 0 ||
           strcmp(raw, "no") == 0 || strcmp(raw, "NO") == 0 || strcmp(raw, "off") == 0 ||
           strcmp(raw, "OFF") == 0);
}

static int32_t driver_c_repo_root_from_tooling_input(const char *input_path,
                                                     char *out,
                                                     size_t out_size) {
  static const char *suffix = "/src/tooling/cheng_tooling.cheng";
  size_t n;
  if (out == NULL || out_size == 0) return 0;
  if (input_path == NULL || !driver_c_source_is_tooling_main(input_path)) return 0;
  n = strlen(input_path) - strlen(suffix);
  if (n == 0 || n + 1 > out_size) return 0;
  memcpy(out, input_path, n);
  out[n] = '\0';
  return 1;
}

static int driver_c_try_runtime_obj_path(const char *path, char *out, size_t out_cap) {
  size_t n;
  if (path == NULL || path[0] == '\0' || out == NULL || out_cap == 0) return 0;
  if (access(path, R_OK) != 0) return 0;
  n = strlen(path);
  if (n + 1u > out_cap) return 0;
  memcpy(out, path, n + 1u);
  return 1;
}

static int driver_c_try_runtime_obj_under_root(const char *root,
                                               const char *target,
                                               char *out,
                                               size_t out_cap) {
  static const char *target_templates[] = {
      "chengcache/runtime_selflink/system_helpers.backend.fullcompat.%s.o",
      "chengcache/runtime_selflink/system_helpers.backend.combined.%s.o",
      "artifacts/backend_mm/system_helpers.backend.combined.%s.o",
      "chengcache/system_helpers.backend.cheng.%s.o",
  };
  static const char *static_candidates[] = {
      "artifacts/backend_selfhost_self_obj/stage1.native.runtime.dedup.o",
      "artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.stage1shim.o",
      "chengcache/system_helpers.backend.cheng.o",
      "artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.o",
  };
  char rel[PATH_MAX];
  char candidate[PATH_MAX];
  size_t i;
  if (root == NULL || root[0] == '\0' || out == NULL || out_cap == 0) return 0;
  if (target != NULL && target[0] != '\0') {
    for (i = 0; i < sizeof(target_templates) / sizeof(target_templates[0]); ++i) {
      if (snprintf(rel, sizeof(rel), target_templates[i], target) <= 0) continue;
      if (snprintf(candidate, sizeof(candidate), "%s/%s", root, rel) <= 0) continue;
      if (driver_c_try_runtime_obj_path(candidate, out, out_cap)) return 1;
    }
  }
  for (i = 0; i < sizeof(static_candidates) / sizeof(static_candidates[0]); ++i) {
    if (snprintf(candidate, sizeof(candidate), "%s/%s", root, static_candidates[i]) <= 0) continue;
    if (driver_c_try_runtime_obj_path(candidate, out, out_cap)) return 1;
  }
  return 0;
}

static int driver_c_resolve_runtime_obj(const char *target, char *out, size_t out_cap) {
  char self_path[PATH_MAX];
  char root[PATH_MAX];
  const char *env_runtime_obj = getenv("BACKEND_RUNTIME_OBJ");
  const char *env_root = getenv("TOOLING_ROOT");
  const char *input_path = getenv("BACKEND_INPUT");
  const char *target_text = (target != NULL && target[0] != '\0') ? target : driver_c_host_target_default();
  if (driver_c_try_runtime_obj_path(env_runtime_obj, out, out_cap)) return 1;
  if (env_root != NULL && env_root[0] != '\0' &&
      driver_c_try_runtime_obj_under_root(env_root, target_text, out, out_cap)) {
    return 1;
  }
  if (driver_c_repo_root_from_tooling_input(input_path, root, sizeof(root)) &&
      driver_c_try_runtime_obj_under_root(root, target_text, out, out_cap)) {
    return 1;
  }
  if (driver_c_resolve_self_path(NULL, self_path, sizeof(self_path)) &&
      driver_c_stage0_repo_root_from_path(self_path, root, sizeof(root)) &&
      driver_c_try_runtime_obj_under_root(root, target_text, out, out_cap)) {
    return 1;
  }
  if (getcwd(root, sizeof(root)) != NULL &&
      driver_c_try_runtime_obj_under_root(root, target_text, out, out_cap)) {
    return 1;
  }
  return 0;
}

static int32_t driver_c_write_executable_text(const char *out_path, const char *text) {
  FILE *fp;
  size_t want;
  size_t wrote;
  if (out_path == NULL || out_path[0] == '\0' || text == NULL) return -54;
  fp = fopen(out_path, "wb");
  if (fp == NULL) return -55;
  want = strlen(text);
  wrote = fwrite(text, 1, want, fp);
  if (wrote != want) {
    fclose(fp);
    return -56;
  }
  if (fclose(fp) != 0) return -57;
  if (chmod(out_path, 0755) != 0) return -58;
  return 0;
}

static int32_t driver_c_emit_tooling_launcher(const char *input_path, const char *output_path) {
  const char *env_root = getenv("TOOLING_ROOT");
  char root_buf[PATH_MAX];
  const char *root = NULL;
  size_t need;
  char *script;
  int32_t rc;
  if (env_root != NULL && env_root[0] != '\0') {
    root = env_root;
  } else if (driver_c_repo_root_from_tooling_input(input_path, root_buf, sizeof(root_buf))) {
    root = root_buf;
  } else {
    return -59;
  }
  need = strlen("#!/usr/bin/env sh\n:\nset -eu\n(set -o pipefail) 2>/dev/null && set -o pipefail\n\nroot=\"\"\nexport TOOLING_ROOT=\"$root\"\nexec sh \"$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh\" \"$@\"\n") +
         (strlen(root) * 3u) + 8u;
  script = (char *)calloc(need, 1u);
  if (script == NULL) return -60;
  snprintf(script, need,
           "#!/usr/bin/env sh\n"
           ":\n"
           "set -eu\n"
           "(set -o pipefail) 2>/dev/null && set -o pipefail\n"
           "\n"
           "root=\"%s\"\n"
           "export TOOLING_ROOT=\"$root\"\n"
           "exec sh \"$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh\" \"$@\"\n",
           root);
  rc = driver_c_write_executable_text(output_path, script);
  free(script);
  return rc;
}

static int32_t driver_c_should_emit_tooling_launcher(const char *input_path,
                                                     const char *target) {
  const char *build_track;
  if (!driver_c_tooling_launcher_enabled()) return 0;
  if (!driver_c_source_is_tooling_main(input_path)) return 0;
  if (!driver_c_target_is_host(target)) return 0;
  build_track = getenv("BACKEND_BUILD_TRACK");
  if (build_track != NULL && strcmp(build_track, "release") == 0) return 0;
  return 1;
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
  ChengDriverModuleKind module_kind;
  void *module_payload;
  int32_t rc = -10;
  driver_c_diagf("[driver_c_emit_obj_default] module=%p target='%s' path='%s'\n",
                 module, target != NULL ? target : "", path != NULL ? path : "");
  if (module == NULL) {
    driver_c_diagf("[driver_c_emit_obj_default] module_null\n");
    return -11;
  }
  if (path == NULL || path[0] == '\0') {
    driver_c_diagf("[driver_c_emit_obj_default] path_missing\n");
    return -12;
  }
  if (target == NULL) target = "";
  module_kind = driver_c_module_kind(module);
  module_payload = driver_c_module_payload(module);
  driver_c_diagf("[driver_c_emit_obj_default] module_kind=%d payload=%p\n",
                 (int32_t)module_kind, module_payload);
  if (module_payload == NULL) {
    driver_c_module_dispose(module);
    return -11;
  }
  if (module_kind == CHENG_DRIVER_MODULE_KIND_RETAINED) {
    cheng_driver_emit_obj_default_fn emit_obj_retained = driver_c_resolve_emit_obj_default_impl();
    if (emit_obj_retained != NULL) {
      rc = emit_obj_retained(module_payload, target, path);
      driver_c_diagf("[driver_c_emit_obj_default] retained_emit_rc=%d\n", rc);
    }
    driver_c_module_dispose(module);
    return rc;
  }
  if (module_kind == CHENG_DRIVER_MODULE_KIND_SIDECAR) {
    cheng_driver_emit_obj_default_fn sidecar_emit_fn =
        (cheng_driver_emit_obj_default_fn)cheng_sidecar_driver_emit_obj_symbol(
            &driver_c_sidecar_cache, target, driver_c_diagf,
            driver_c_sidecar_after_open);
    if (sidecar_emit_fn != NULL) {
      rc = sidecar_emit_fn(module_payload, target, path);
      driver_c_diagf("[driver_c_emit_obj_default] sidecar_rc=%d\n", rc);
    }
    driver_c_module_dispose(module);
    return rc;
  }
  {
    cheng_driver_emit_obj_default_fn emit_obj_raw = driver_c_resolve_emit_obj_default_impl();
    if (emit_obj_raw != NULL) {
      rc = emit_obj_raw(module_payload, target, path);
      driver_c_diagf("[driver_c_emit_obj_default] raw_emit_rc=%d\n", rc);
      if (rc == 0) {
        driver_c_module_dispose(module);
        return rc;
      }
    }
  }
  cheng_seq_u8_header obj;
  memset(&obj, 0, sizeof(obj));
  cheng_emit_obj_from_module_fn emit_obj = driver_c_resolve_uir_emit_obj();
  if (emit_obj == NULL) {
    driver_c_diagf("[driver_c_emit_obj_default] no_emit_symbol\n");
    driver_c_module_dispose(module);
    return -10;
  }
  emit_obj((cheng_seq_any *)&obj, module_payload, driver_c_env_i32("BACKEND_OPT_LEVEL", 0), target, "",
           false, false, 0, "autovec");
  driver_c_diagf("[driver_c_emit_obj_default] weak_uir_emit len=%d buffer=%p\n", obj.len, obj.buffer);
  if (obj.len <= 0) {
    driver_c_module_dispose(module);
    return -13;
  }
  if (obj.buffer == NULL) {
    driver_c_module_dispose(module);
    return -14;
  }
  {
    int32_t rc_write = driver_c_write_exact_file(path, obj.buffer, obj.len);
    driver_c_diagf("[driver_c_emit_obj_default] write_rc=%d\n", rc_write);
    driver_c_module_dispose(module);
    return rc_write;
  }
}

int32_t driver_c_link_tmp_obj_default(const char *output_path, const char *obj_path,
                                      const char *target, const char *linker) {
  const char *target_text = (target != NULL) ? target : "";
  const char *linker_text = (linker != NULL && linker[0] != '\0') ? linker : "system";
  char runtime_obj_buf[PATH_MAX];
  const char *runtime_input = NULL;
  const char *cflags = getenv("BACKEND_CFLAGS");
  const char *ldflags = getenv("BACKEND_LDFLAGS");
  const char *cflags_text = (cflags != NULL) ? cflags : "";
  const char *ldflags_text = (ldflags != NULL) ? ldflags : "";
  const char *fast_darwin_adhoc_raw = getenv("BACKEND_DARWIN_FAST_ADHOC");
  char full_ldflags[512];
  char extra_ldflags[96];
  full_ldflags[0] = '\0';
  extra_ldflags[0] = '\0';
  if (output_path == NULL || output_path[0] == '\0') return -20;
  if (obj_path == NULL || obj_path[0] == '\0') return -21;
  if (strcmp(linker_text, "system") != 0) return -22;
  runtime_obj_buf[0] = '\0';
  if (!driver_c_resolve_runtime_obj(target_text, runtime_obj_buf, sizeof(runtime_obj_buf))) {
    driver_c_diagf("[driver_c_link_tmp_obj_default] missing runtime_obj target='%s'\n", target_text);
    return -27;
  }
  runtime_input = runtime_obj_buf;
  const char *base = strrchr(obj_path, '/');
  const char *obj_name = (base != NULL) ? (base + 1) : obj_path;
  int use_driver_entry_shim = strstr(obj_name, "backend_driver") != NULL ||
                              driver_c_env_bool("BACKEND_FORCE_DRIVER_ENTRY_SHIM", 0) != 0;
  int needs_darwin_codesign = 0;
  int fast_darwin_adhoc =
      (fast_darwin_adhoc_raw != NULL && fast_darwin_adhoc_raw[0] != '\0')
          ? driver_c_env_bool("BACKEND_DARWIN_FAST_ADHOC", 0)
          : driver_c_env_bool("BACKEND_FAST_DEV_PROFILE", 0);
  if (strstr(target_text, "darwin") != NULL || strstr(target_text, "apple-darwin") != NULL) {
    if (strstr(ldflags_text, "-Wl,-no_adhoc_codesign") != NULL) fast_darwin_adhoc = 0;
    needs_darwin_codesign = fast_darwin_adhoc ? 0 : 1;
    if (!fast_darwin_adhoc && strstr(ldflags_text, "-Wl,-no_adhoc_codesign") == NULL) {
      snprintf(extra_ldflags + strlen(extra_ldflags),
               sizeof(extra_ldflags) - strlen(extra_ldflags),
               " -Wl,-no_adhoc_codesign");
    }
    if (strstr(ldflags_text, "-Wl,-export_dynamic") == NULL) {
      snprintf(extra_ldflags + strlen(extra_ldflags),
               sizeof(extra_ldflags) - strlen(extra_ldflags),
               " -Wl,-export_dynamic");
    }
  }
  snprintf(full_ldflags, sizeof(full_ldflags), "%s%s", ldflags_text, extra_ldflags);
  size_t need = strlen(obj_path) + strlen(runtime_input) + strlen(output_path) +
                strlen(cflags_text) + strlen(full_ldflags) + 128u;
  char *cmd = (char *)malloc(need);
  if (cmd == NULL) return -23;
  if (use_driver_entry_shim) {
    snprintf(cmd, need, "cc -DCHENG_BACKEND_DRIVER_ENTRY_SHIM %s '%s' '%s' %s -o '%s'",
             cflags_text, obj_path, runtime_input, full_ldflags, output_path);
  } else {
    snprintf(cmd, need, "cc %s '%s' '%s' %s -o '%s'",
             cflags_text, obj_path, runtime_input, full_ldflags, output_path);
  }
  int rc = system(cmd);
  free(cmd);
  if (rc != 0) return -24;
  if (cheng_file_exists(output_path) == 0) return -25;
  if (cheng_file_size(output_path) <= 0) return -26;
  if (needs_darwin_codesign) {
    size_t sign_need = strlen(output_path) * 2u + 128u;
    char *sign_cmd = (char *)malloc(sign_need);
    if (sign_cmd == NULL) return -27;
    snprintf(sign_cmd, sign_need,
             "codesign --force --sign - '%s' >/dev/null 2>&1 && "
             "codesign --verify --verbose=2 '%s' >/dev/null 2>&1",
             output_path, output_path);
    rc = system(sign_cmd);
    free(sign_cmd);
    if (rc != 0) return -28;
  }
  return 0;
}

int32_t driver_c_link_tmp_obj_system(const char *output_path, const char *obj_path,
                                     const char *target) {
  return driver_c_link_tmp_obj_default(output_path, obj_path, target, "system");
}

int32_t driver_c_build_emit_obj_default(const char *input_path, const char *target,
                                        const char *output_path) {
  int32_t prefer_sidecar = driver_c_prefer_sidecar_builds();
  driver_c_boot_marker(40);
  if (input_path == NULL || input_path[0] == '\0') return -30;
  if (target == NULL || target[0] == '\0') return -31;
  if (output_path == NULL || output_path[0] == '\0') return -32;
  cheng_build_emit_obj_stage1_target_fn build_emit_obj_direct =
      driver_c_resolve_build_emit_obj_stage1_target();
  if (prefer_sidecar == 0 && build_emit_obj_direct != NULL &&
      driver_c_build_emit_obj_dispatch_depth == 0) {
    driver_c_build_emit_obj_dispatch_depth += 1;
    int32_t direct_rc = build_emit_obj_direct(input_path, target, output_path);
    driver_c_build_emit_obj_dispatch_depth -= 1;
    driver_c_diagf("[driver_c_build_emit_obj_default] direct_build_emit_rc=%d\n", direct_rc);
    if (direct_rc != 0) return direct_rc;
    driver_c_boot_marker(44);
    return driver_c_finish_emit_obj(output_path);
  }
  if (prefer_sidecar == 0 && build_emit_obj_direct != NULL &&
      driver_c_build_emit_obj_dispatch_depth != 0) {
    driver_c_diagf("[driver_c_build_emit_obj_default] direct_build_emit_reentry_skip depth=%d\n",
                   driver_c_build_emit_obj_dispatch_depth);
  }
  driver_c_boot_marker(41);
  void *module = driver_c_build_module_stage1(input_path, target);
  if (module == NULL) {
    driver_c_boot_marker(42);
    driver_c_diagf("[driver_c_build_emit_obj_default] module_build_null rc=-33 input='%s' target='%s'\n",
                   input_path != NULL ? input_path : "", target != NULL ? target : "");
    return -33;
  }
  driver_c_boot_marker(43);
  int32_t emit_rc = driver_c_emit_obj_default(module, target, output_path);
  if (emit_rc != 0) return emit_rc;
  driver_c_boot_marker(44);
  return driver_c_finish_emit_obj(output_path);
}

static int driver_c_scoped_sidecar_disable_begin(const char **old_raw, int *had_old) {
  const char *cur = getenv("BACKEND_UIR_SIDECAR_DISABLE");
  if (old_raw != NULL) *old_raw = cur;
  if (had_old != NULL) *had_old = (cur != NULL && cur[0] != '\0') ? 1 : 0;
  return setenv("BACKEND_UIR_SIDECAR_DISABLE", "1", 1);
}

static void driver_c_scoped_sidecar_disable_end(const char *old_raw, int had_old) {
  if (had_old != 0 && old_raw != NULL && old_raw[0] != '\0') {
    setenv("BACKEND_UIR_SIDECAR_DISABLE", old_raw, 1);
  } else {
    unsetenv("BACKEND_UIR_SIDECAR_DISABLE");
  }
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
  driver_c_boot_marker(50);
  if (input_path == NULL || input_path[0] == '\0') return -40;
  if (target == NULL || target[0] == '\0') return -41;
  if (output_path == NULL || output_path[0] == '\0') return -42;
  if (driver_c_should_emit_tooling_launcher(input_path, target)) {
    int32_t rc = driver_c_emit_tooling_launcher(input_path, output_path);
    driver_c_diagf("[driver_c_build_link_exe_default] tooling_launcher_rc=%d input='%s' output='%s'\n",
                   rc, input_path, output_path);
    return rc;
  }
  const char *suffix = ".tmp.linkobj";
  size_t need = strlen(output_path) + strlen(suffix) + 1u;
  char *obj_path = (char *)malloc(need);
  if (obj_path == NULL) return -43;
  snprintf(obj_path, need, "%s%s", output_path, suffix);
  driver_c_boot_marker(51);
  int32_t emit_rc = driver_c_build_emit_obj_default(input_path, target, obj_path);
  if (emit_rc != 0) {
    free(obj_path);
    return emit_rc;
  }
  driver_c_boot_marker(52);
  int32_t link_rc = driver_c_link_tmp_obj_system(output_path, obj_path, target);
  if (link_rc != 0) {
    free(obj_path);
    return link_rc;
  }
  driver_c_boot_marker(53);
  int32_t finish_rc = driver_c_finish_single_link(output_path, obj_path);
  free(obj_path);
  return finish_rc;
}

void *driver_c_build_module_stage1(const char *input_path, const char *target) {
  int32_t prefer_sidecar = driver_c_prefer_sidecar_builds();
  int32_t allow_facade_dispatch = (driver_c_build_module_dispatch_depth == 0);
  driver_c_boot_marker(60);
  driver_c_diagf("[driver_c_build_module_stage1] input='%s' target='%s'\n",
                 input_path != NULL ? input_path : "", target != NULL ? target : "");
  if (input_path == NULL || input_path[0] == '\0') {
    driver_c_boot_marker(61);
    driver_c_diagf("[driver_c_build_module_stage1] input_missing\n");
    return NULL;
  }
  if (target == NULL) target = "";
  if (driver_c_env_bool("BACKEND_UIR_SIDECAR_DISABLE", 0) != 0) {
    void *direct_module = driver_c_build_module_stage1_direct(input_path, target);
    driver_c_diagf("[driver_c_build_module_stage1] sidecar_disabled direct_module=%p\n",
                   direct_module);
    if (direct_module != NULL) {
      driver_c_boot_marker(63);
      return driver_c_module_wrap(direct_module, CHENG_DRIVER_MODULE_KIND_RAW);
    }
    driver_c_diagf("[driver_c_build_module_stage1] sidecar_disabled direct_failed\n");
    return NULL;
  }
  if (prefer_sidecar == 0) {
    {
      driver_c_boot_marker(62);
      if (uirCoreBuildModuleFromFileStage1OrPanic != NULL) {
        void *module = uirCoreBuildModuleFromFileStage1OrPanic(input_path, target);
        driver_c_diagf("[driver_c_build_module_stage1] weak_builder_module=%p\n", module);
        if (module != NULL) {
          driver_c_boot_marker(63);
          return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
        }
      }
    }
  }
  if (prefer_sidecar == 0 && allow_facade_dispatch != 0) {
    {
      driver_c_boot_marker(62);
      cheng_build_module_stage1_target_facade_fn build_module_stage1_target_retained =
          driver_c_resolve_build_module_stage1_target_retained();
      if (build_module_stage1_target_retained != NULL) {
        driver_c_build_module_dispatch_depth += 1;
        void *module = build_module_stage1_target_retained(input_path, target);
        driver_c_build_module_dispatch_depth -= 1;
        driver_c_diagf("[driver_c_build_module_stage1] dlsym_retained_target_module=%p\n", module);
        if (module != NULL) {
          driver_c_boot_marker(63);
          return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RETAINED);
        }
      }
    }
    {
      driver_c_boot_marker(62);
      cheng_build_module_stage1_target_facade_fn build_module_stage1_target =
          driver_c_resolve_build_module_stage1_target_facade();
      if (build_module_stage1_target != NULL) {
        driver_c_build_module_dispatch_depth += 1;
        void *module = build_module_stage1_target(input_path, target);
        driver_c_build_module_dispatch_depth -= 1;
        driver_c_diagf("[driver_c_build_module_stage1] dlsym_facade_target_module=%p\n", module);
        if (module != NULL) {
          driver_c_boot_marker(63);
          return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
        }
      }
    }
    {
      driver_c_boot_marker(62);
      cheng_build_module_stage1_facade_fn build_module_stage1_retained =
          driver_c_resolve_build_module_stage1_retained();
      if (build_module_stage1_retained != NULL) {
        driver_c_build_module_dispatch_depth += 1;
        void *module = build_module_stage1_retained(input_path);
        driver_c_build_module_dispatch_depth -= 1;
        driver_c_diagf("[driver_c_build_module_stage1] dlsym_retained_module=%p\n", module);
        if (module != NULL) {
          driver_c_boot_marker(63);
          return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RETAINED);
        }
      }
    }
    {
      driver_c_boot_marker(62);
      cheng_build_module_stage1_fn build_module_stage1 =
          driver_c_resolve_build_module_stage1_raw();
      if (build_module_stage1 != NULL) {
        void *module = build_module_stage1(input_path, target);
        driver_c_diagf("[driver_c_build_module_stage1] dlsym_builder_module=%p\n", module);
        if (module != NULL) {
          driver_c_boot_marker(63);
          return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
        }
      }
    }
    {
      driver_c_boot_marker(62);
      cheng_build_module_stage1_facade_fn build_module_stage1_facade =
          driver_c_resolve_build_module_stage1_facade();
      if (build_module_stage1_facade != NULL) {
        driver_c_build_module_dispatch_depth += 1;
        void *module = build_module_stage1_facade(input_path);
        driver_c_build_module_dispatch_depth -= 1;
        driver_c_diagf("[driver_c_build_module_stage1] dlsym_facade_module=%p\n", module);
        if (module != NULL) {
          driver_c_boot_marker(63);
          return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_RAW);
        }
      }
    }
  }
  if (prefer_sidecar == 0 && allow_facade_dispatch == 0) {
    driver_c_diagf("[driver_c_build_module_stage1] facade_reentry_skip depth=%d\n",
                   driver_c_build_module_dispatch_depth);
  }
  {
    driver_c_boot_marker(66);
    cheng_build_active_module_ptrs_fn sidecar_build_fn =
        (cheng_build_active_module_ptrs_fn)cheng_sidecar_build_module_symbol(
            &driver_c_sidecar_cache, target, driver_c_diagf,
            driver_c_sidecar_after_open);
    if (sidecar_build_fn != NULL) {
      void *module = sidecar_build_fn((void *)input_path, (void *)target);
      driver_c_diagf("[driver_c_build_module_stage1] sidecar_module=%p\n", module);
      if (module != NULL) {
        driver_c_boot_marker(67);
        return driver_c_module_wrap(module, CHENG_DRIVER_MODULE_KIND_SIDECAR);
      }
    }
  }
  driver_c_boot_marker(68);
  driver_c_diagf("[driver_c_build_module_stage1] no_module\n");
  return NULL;
}

void *driver_c_build_module_stage1_direct(const char *input_path, const char *target) {
  int32_t prefer_sidecar = driver_c_prefer_sidecar_builds();
  int32_t sidecar_disabled = driver_c_env_bool("BACKEND_UIR_SIDECAR_DISABLE", 0);
  int32_t allow_direct_exports = (prefer_sidecar == 0 || sidecar_disabled != 0);
  int32_t allow_facade_dispatch = (driver_c_build_module_dispatch_depth == 0);
  driver_c_diagf("[driver_c_build_module_stage1_direct] input='%s' target='%s'\n",
                 input_path != NULL ? input_path : "", target != NULL ? target : "");
  if (input_path == NULL || input_path[0] == '\0') {
    driver_c_diagf("[driver_c_build_module_stage1_direct] input_missing\n");
    return NULL;
  }
  if (target == NULL) target = "";
  if (allow_direct_exports != 0) {
    if (uirCoreBuildModuleFromFileStage1OrPanic != NULL) {
      void *module = uirCoreBuildModuleFromFileStage1OrPanic(input_path, target);
      driver_c_diagf("[driver_c_build_module_stage1_direct] weak_builder_module=%p\n", module);
      if (module != NULL) return module;
    }
  }
  if (allow_direct_exports != 0 && allow_facade_dispatch != 0) {
    cheng_build_module_stage1_target_facade_fn build_module_stage1_target_retained =
        driver_c_resolve_build_module_stage1_target_retained();
    if (build_module_stage1_target_retained != NULL) {
      driver_c_build_module_dispatch_depth += 1;
      void *module = build_module_stage1_target_retained(input_path, target);
      driver_c_build_module_dispatch_depth -= 1;
      driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_retained_target_module=%p\n",
                     module);
      if (module != NULL) return module;
    }
  }
  if (allow_direct_exports != 0 && allow_facade_dispatch != 0) {
    cheng_build_module_stage1_target_facade_fn build_module_stage1_target =
        driver_c_resolve_build_module_stage1_target_facade();
    if (build_module_stage1_target != NULL) {
      driver_c_build_module_dispatch_depth += 1;
      void *module = build_module_stage1_target(input_path, target);
      driver_c_build_module_dispatch_depth -= 1;
      driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_facade_target_module=%p\n",
                     module);
      if (module != NULL) return module;
    }
  }
  if (allow_direct_exports != 0 && allow_facade_dispatch != 0) {
    cheng_build_module_stage1_facade_fn build_module_stage1_retained =
        driver_c_resolve_build_module_stage1_retained();
    if (build_module_stage1_retained != NULL) {
      driver_c_build_module_dispatch_depth += 1;
      void *module = build_module_stage1_retained(input_path);
      driver_c_build_module_dispatch_depth -= 1;
      driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_retained_module=%p\n", module);
      if (module != NULL) return module;
    }
  }
  if (allow_direct_exports != 0) {
    cheng_build_module_stage1_fn build_module_stage1 =
        driver_c_resolve_build_module_stage1_raw();
    if (build_module_stage1 != NULL) {
      void *module = build_module_stage1(input_path, target);
      driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_builder_module=%p\n", module);
      if (module != NULL) return module;
    }
  }
  if (allow_direct_exports != 0 && allow_facade_dispatch != 0) {
    cheng_build_module_stage1_facade_fn build_module_stage1_facade =
        driver_c_resolve_build_module_stage1_facade();
    if (build_module_stage1_facade != NULL) {
      driver_c_build_module_dispatch_depth += 1;
      void *module = build_module_stage1_facade(input_path);
      driver_c_build_module_dispatch_depth -= 1;
      driver_c_diagf("[driver_c_build_module_stage1_direct] dlsym_facade_module=%p\n", module);
      if (module != NULL) return module;
    }
  }
  if (allow_direct_exports != 0 && allow_facade_dispatch == 0) {
    driver_c_diagf("[driver_c_build_module_stage1_direct] facade_reentry_skip depth=%d\n",
                   driver_c_build_module_dispatch_depth);
  }
  driver_c_diagf("[driver_c_build_module_stage1_direct] no_module\n");
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
  driver_c_boot_marker(70);
  int32_t emit_mode = driver_c_emit_is_obj_mode();
  if (emit_mode < 0) return emit_mode;
  driver_c_boot_marker(71);
  char *input_path = driver_c_resolve_input_path();
  char *target = driver_c_resolve_target();
  char *output_path = driver_c_resolve_output_path(input_path);
  char *linker = driver_c_resolve_linker();
  driver_c_diagf("[driver_c_run_default] emit=%d input='%s' target='%s' output='%s' linker='%s'\n",
                 emit_mode,
                 input_path != NULL ? input_path : "",
                 target != NULL ? target : "",
                 output_path != NULL ? output_path : "",
                 linker != NULL ? linker : "");
  driver_c_boot_marker(72);
  if (input_path == NULL || input_path[0] == '\0') return 2;
  if (target == NULL || target[0] == '\0') return 2;
  if (output_path == NULL || output_path[0] == '\0') return 2;
  if (emit_mode != 0) {
    driver_c_boot_marker(73);
    int32_t rc = driver_c_build_emit_obj_default(input_path, target, output_path);
    driver_c_diagf("[driver_c_run_default] emit_obj_rc=%d\n", rc);
    return rc;
  }
  {
    driver_c_boot_marker(74);
    int32_t rc = driver_c_build_link_exe_default(input_path, target, output_path, linker);
    driver_c_diagf("[driver_c_run_default] link_exe_rc=%d\n", rc);
    return rc;
  }
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
  case 40: driver_c_boot_marker_write("[phase]emit_obj.enter\n", sizeof("[phase]emit_obj.enter\n") - 1u); break;
  case 41: driver_c_boot_marker_write("[phase]emit_obj.build_module\n", sizeof("[phase]emit_obj.build_module\n") - 1u); break;
  case 42: driver_c_boot_marker_write("[phase]emit_obj.module_null\n", sizeof("[phase]emit_obj.module_null\n") - 1u); break;
  case 43: driver_c_boot_marker_write("[phase]emit_obj.module_ok\n", sizeof("[phase]emit_obj.module_ok\n") - 1u); break;
  case 44: driver_c_boot_marker_write("[phase]emit_obj.finish\n", sizeof("[phase]emit_obj.finish\n") - 1u); break;
  case 50: driver_c_boot_marker_write("[phase]link_exe.enter\n", sizeof("[phase]link_exe.enter\n") - 1u); break;
  case 51: driver_c_boot_marker_write("[phase]link_exe.emit_tmp_obj\n", sizeof("[phase]link_exe.emit_tmp_obj\n") - 1u); break;
  case 52: driver_c_boot_marker_write("[phase]link_exe.system_link\n", sizeof("[phase]link_exe.system_link\n") - 1u); break;
  case 53: driver_c_boot_marker_write("[phase]link_exe.finish\n", sizeof("[phase]link_exe.finish\n") - 1u); break;
  case 60: driver_c_boot_marker_write("[phase]build_module.enter\n", sizeof("[phase]build_module.enter\n") - 1u); break;
  case 61: driver_c_boot_marker_write("[phase]build_module.input_missing\n", sizeof("[phase]build_module.input_missing\n") - 1u); break;
  case 62: driver_c_boot_marker_write("[phase]build_module.try_builder\n", sizeof("[phase]build_module.try_builder\n") - 1u); break;
  case 63: driver_c_boot_marker_write("[phase]build_module.builder_ok\n", sizeof("[phase]build_module.builder_ok\n") - 1u); break;
  case 64: driver_c_boot_marker_write("[phase]build_module.try_sidecar_symbol\n", sizeof("[phase]build_module.try_sidecar_symbol\n") - 1u); break;
  case 65: driver_c_boot_marker_write("[phase]build_module.sidecar_symbol_ok\n", sizeof("[phase]build_module.sidecar_symbol_ok\n") - 1u); break;
  case 66: driver_c_boot_marker_write("[phase]build_module.try_sidecar_bundle\n", sizeof("[phase]build_module.try_sidecar_bundle\n") - 1u); break;
  case 67: driver_c_boot_marker_write("[phase]build_module.sidecar_bundle_ok\n", sizeof("[phase]build_module.sidecar_bundle_ok\n") - 1u); break;
  case 68: driver_c_boot_marker_write("[phase]build_module.no_module\n", sizeof("[phase]build_module.no_module\n") - 1u); break;
  case 70: driver_c_boot_marker_write("[phase]run_default.enter\n", sizeof("[phase]run_default.enter\n") - 1u); break;
  case 71: driver_c_boot_marker_write("[phase]run_default.resolve_paths\n", sizeof("[phase]run_default.resolve_paths\n") - 1u); break;
  case 72: driver_c_boot_marker_write("[phase]run_default.dispatch\n", sizeof("[phase]run_default.dispatch\n") - 1u); break;
  case 73: driver_c_boot_marker_write("[phase]run_default.obj_mode\n", sizeof("[phase]run_default.obj_mode\n") - 1u); break;
  case 74: driver_c_boot_marker_write("[phase]run_default.exe_mode\n", sizeof("[phase]run_default.exe_mode\n") - 1u); break;
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

__attribute__((weak)) int32_t c_fflush(void *stream) {
  FILE *f = (stream != NULL) ? (FILE *)stream : stdout;
  return fflush(f);
}

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
