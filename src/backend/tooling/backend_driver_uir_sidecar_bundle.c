#include <errno.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

typedef struct DriverUirSidecarHandle {
  char *input_path;
  char *target;
  char *compiler;
} DriverUirSidecarHandle;

static const char *driver_sidecar_default_stage0(void) {
  return "/Users/lbcheng/cheng-lang/artifacts/backend_selfhost_self_obj/cheng.stage2";
}

static const char *driver_sidecar_default_dist_release(void) {
  return "/Users/lbcheng/cheng-lang/dist/releases/current/cheng";
}

static const char *driver_sidecar_default_canonical(void) {
  return "/Users/lbcheng/cheng-lang/artifacts/backend_driver/cheng";
}

static const char *driver_sidecar_mode(void) {
  const char *mode = getenv("BACKEND_UIR_SIDECAR_MODE");
  if (mode == NULL || mode[0] == '\0') return "cheng";
  return mode;
}

static int driver_sidecar_mode_is_emergency_c(void) {
  return strcmp(driver_sidecar_mode(), "emergency_c") == 0;
}

static char *driver_sidecar_dup(const char *s) {
  size_t n;
  char *out;
  if (s == NULL) return NULL;
  n = strlen(s);
  out = (char *)malloc(n + 1);
  if (out == NULL) return NULL;
  if (n > 0) memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

static void driver_sidecar_free_handle(DriverUirSidecarHandle *h) {
  if (h == NULL) return;
  free(h->input_path);
  free(h->target);
  free(h->compiler);
  free(h);
}

static const char *driver_sidecar_pick_compiler(void) {
  const char *env_path = getenv("BACKEND_UIR_SIDECAR_COMPILER");
  if (env_path != NULL && env_path[0] != '\0' && access(env_path, X_OK) == 0) {
    return env_path;
  }
  if (access(driver_sidecar_default_canonical(), X_OK) == 0) {
    return driver_sidecar_default_canonical();
  }
  if (access(driver_sidecar_default_dist_release(), X_OK) == 0) {
    return driver_sidecar_default_dist_release();
  }
  if (access(driver_sidecar_default_stage0(), X_OK) == 0) {
    return driver_sidecar_default_stage0();
  }
  return NULL;
}

typedef struct DriverSidecarEnvOverride {
  const char *name;
  const char *value;
} DriverSidecarEnvOverride;

static int driver_sidecar_env_name_matches(const char *entry, const char *name) {
  size_t i = 0;
  if (entry == NULL || name == NULL) return 0;
  while (name[i] != '\0') {
    if (entry[i] != name[i]) return 0;
    i += 1;
  }
  return entry[i] == '=';
}

static char *driver_sidecar_env_pair(const char *name, const char *value) {
  size_t name_len;
  size_t value_len;
  char *out;
  if (name == NULL || value == NULL) return NULL;
  name_len = strlen(name);
  value_len = strlen(value);
  out = (char *)malloc(name_len + value_len + 2);
  if (out == NULL) return NULL;
  memcpy(out, name, name_len);
  out[name_len] = '=';
  memcpy(out + name_len + 1, value, value_len);
  out[name_len + value_len + 1] = '\0';
  return out;
}

static const char *driver_sidecar_env_or_default(const char *name, const char *fallback) {
  const char *raw = getenv(name);
  if (raw != NULL && raw[0] != '\0') return raw;
  return fallback;
}

static char **driver_sidecar_build_envp(const DriverSidecarEnvOverride *overrides,
                                        size_t override_count,
                                        char ***owned_pairs_out) {
  size_t env_count = 0;
  size_t i;
  size_t out_idx = 0;
  char **envp;
  char **owned_pairs;
  if (owned_pairs_out == NULL) return NULL;
  while (environ != NULL && environ[env_count] != NULL) {
    env_count += 1;
  }
  envp = (char **)calloc(env_count + override_count + 1, sizeof(char *));
  owned_pairs = (char **)calloc(override_count, sizeof(char *));
  if (envp == NULL || owned_pairs == NULL) {
    free(envp);
    free(owned_pairs);
    return NULL;
  }
  for (i = 0; i < env_count; i += 1) {
    size_t oi;
    int replaced = 0;
    for (oi = 0; oi < override_count; oi += 1) {
      if (driver_sidecar_env_name_matches(environ[i], overrides[oi].name)) {
        replaced = 1;
        break;
      }
    }
    if (!replaced) {
      envp[out_idx] = environ[i];
      out_idx += 1;
    }
  }
  for (i = 0; i < override_count; i += 1) {
    owned_pairs[i] = driver_sidecar_env_pair(overrides[i].name, overrides[i].value);
    if (owned_pairs[i] == NULL) {
      size_t fi;
      for (fi = 0; fi < i; fi += 1) free(owned_pairs[fi]);
      free(owned_pairs);
      free(envp);
      return NULL;
    }
    envp[out_idx] = owned_pairs[i];
    out_idx += 1;
  }
  envp[out_idx] = NULL;
  *owned_pairs_out = owned_pairs;
  return envp;
}

static void driver_sidecar_free_envp(char **envp, char **owned_pairs, size_t owned_count) {
  size_t i;
  if (owned_pairs != NULL) {
    for (i = 0; i < owned_count; i += 1) free(owned_pairs[i]);
  }
  free(owned_pairs);
  free(envp);
}

static int32_t driver_sidecar_exec_obj_compile(const DriverUirSidecarHandle *h,
                                               const char *target,
                                               const char *out_path) {
  pid_t pid;
  int status = 0;
  int spawn_rc;
  const char *compiler;
  const char *final_target;
  char *argv[] = {NULL, NULL};
  char **envp = NULL;
  char **owned_pairs = NULL;
  DriverSidecarEnvOverride overrides[] = {
      {"DYLD_INSERT_LIBRARIES", ""},
      {"DYLD_FORCE_FLAT_NAMESPACE", ""},
      {"BACKEND_UIR_SIDECAR_DISABLE", "1"},
      {"BACKEND_UIR_SIDECAR_OBJ", "/__cheng_sidecar_disabled__.o"},
      {"BACKEND_UIR_SIDECAR_BUNDLE", "/__cheng_sidecar_disabled__.bundle"},
      {"BACKEND_UIR_SIDECAR_COMPILER", ""},
      {"STAGE1_STD_NO_POINTERS", "0"},
      {"STAGE1_STD_NO_POINTERS_STRICT", "0"},
      {"STAGE1_NO_POINTERS_NON_C_ABI", "0"},
      {"STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL", "0"},
      {"MM", "orc"},
      {"CACHE", "0"},
      {"STAGE1_AUTO_SYSTEM", "0"},
      {"BACKEND_BUILD_TRACK", "dev"},
      {"BACKEND_ALLOW_DIRECT_DRIVER", "1"},
      {"WHOLE_PROGRAM", "1"},
      {"BACKEND_WHOLE_PROGRAM", "1"},
      {"whole_program", "1"},
      {"BACKEND_STAGE1_PARSE_MODE", "outline"},
      {"BACKEND_FN_SCHED", "ws"},
      {"BACKEND_DIRECT_EXE", "0"},
      {"BACKEND_LINKERLESS_INMEM", "0"},
      {"BACKEND_FAST_FALLBACK_ALLOW", "0"},
      {"BACKEND_MULTI", "0"},
      {"BACKEND_MULTI_FORCE", "0"},
      {"BACKEND_MULTI_MODULE_CACHE", "0"},
      {"BACKEND_MODULE_CACHE", ""},
      {"BACKEND_MODULE_CACHE_UNSTABLE_ALLOW", "0"},
      {"BACKEND_INCREMENTAL", "0"},
      {"BACKEND_JOBS", "1"},
      {"BACKEND_FN_JOBS", "1"},
      {"BACKEND_VALIDATE", "0"},
      {"BACKEND_OPT_LEVEL", NULL},
      {"BACKEND_OPT", NULL},
      {"BACKEND_OPT2", NULL},
      {"UIR_SIMD", "0"},
      {"STAGE1_SKIP_CPROFILE", "1"},
      {"STAGE1_PROFILE", "0"},
      {"GENERIC_MODE", "dict"},
      {"GENERIC_SPEC_BUDGET", "0"},
      {"GENERIC_LOWERING", "mir_dict"},
      {"BACKEND_STAGE1_DISABLE_DUP_SCAN", "0"},
      {"BACKEND_STAGE1_BUILDER", "stage1"},
      {"BACKEND_SKIP_GLOBAL_INIT", "0"},
      {"BACKEND_SKIP_UIR_FIXUPS", "0"},
      {"BACKEND_SKIP_UIR_NORMALIZE", "0"},
      {"BACKEND_LINKER", "system"},
      {"BACKEND_NO_RUNTIME_C", "0"},
      {"BACKEND_INTERNAL_ALLOW_EMIT_OBJ", "1"},
      {"BACKEND_ALLOW_NO_MAIN", "1"},
      {"BACKEND_EMIT", "obj"},
      {"BACKEND_TARGET", NULL},
      {"BACKEND_INPUT", NULL},
      {"BACKEND_OUTPUT", NULL},
  };
  size_t override_count = sizeof(overrides) / sizeof(overrides[0]);
  if (h == NULL || out_path == NULL || out_path[0] == '\0') {
    return 2;
  }
  compiler = (h->compiler != NULL && h->compiler[0] != '\0') ? h->compiler : driver_sidecar_pick_compiler();
  if (compiler == NULL || compiler[0] == '\0') {
    fprintf(stderr, "[backend_driver sidecar] no compiler available\n");
    return 2;
  }
  final_target = (target != NULL && target[0] != '\0')
      ? target
      : ((h->target != NULL && h->target[0] != '\0') ? h->target : "arm64-apple-darwin");
  overrides[32].value = driver_sidecar_env_or_default("BACKEND_OPT_LEVEL", "0");
  overrides[33].value = driver_sidecar_env_or_default("BACKEND_OPT", "0");
  overrides[34].value = driver_sidecar_env_or_default("BACKEND_OPT2", "0");
  overrides[override_count - 3].value = final_target;
  overrides[override_count - 2].value = (h->input_path != NULL) ? h->input_path : "";
  overrides[override_count - 1].value = out_path;
  envp = driver_sidecar_build_envp(overrides, override_count, &owned_pairs);
  if (envp == NULL) {
    fprintf(stderr, "[backend_driver sidecar] envp build failed\n");
    return 2;
  }
  argv[0] = (char *)compiler;
  argv[1] = NULL;
  spawn_rc = posix_spawn(&pid, compiler, NULL, NULL, argv, envp);
  driver_sidecar_free_envp(envp, owned_pairs, override_count);
  if (spawn_rc != 0) {
    fprintf(stderr, "[backend_driver sidecar] spawn failed: %s\n", strerror(spawn_rc));
    return 2;
  }
  for (;;) {
    pid_t waited = waitpid(pid, &status, 0);
    if (waited >= 0) break;
    if (errno == EINTR) continue;
    fprintf(stderr, "[backend_driver sidecar] waitpid failed: %s\n", strerror(errno));
    return 2;
  }
  if (WIFEXITED(status)) return (int32_t)WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return (int32_t)(128 + WTERMSIG(status));
  return 2;
}

__attribute__((visibility("default")))
void *driver_buildActiveModulePtrs(void *input_raw, void *target_raw) {
  DriverUirSidecarHandle *h;
  const char *compiler;
  const char *input_text = (const char *)input_raw;
  const char *target_text = (const char *)target_raw;
  if (!driver_sidecar_mode_is_emergency_c()) return NULL;
  if (input_text == NULL || input_text[0] == '\0') return NULL;
  compiler = driver_sidecar_pick_compiler();
  if (compiler == NULL) return NULL;
  h = (DriverUirSidecarHandle *)calloc(1, sizeof(DriverUirSidecarHandle));
  if (h == NULL) return NULL;
  h->input_path = driver_sidecar_dup(input_text);
  h->target = driver_sidecar_dup(target_text != NULL ? target_text : "");
  h->compiler = driver_sidecar_dup(compiler);
  if (h->input_path == NULL || h->compiler == NULL) {
    driver_sidecar_free_handle(h);
    return NULL;
  }
  return (void *)h;
}

__attribute__((visibility("default")))
int32_t driver_emit_obj_from_module_default_impl(void *module,
                                                 const char *target,
                                                 const char *out_path) {
  DriverUirSidecarHandle *h = (DriverUirSidecarHandle *)module;
  if (!driver_sidecar_mode_is_emergency_c()) return 2;
  int32_t rc = driver_sidecar_exec_obj_compile(h, target, out_path);
  driver_sidecar_free_handle(h);
  return rc;
}
