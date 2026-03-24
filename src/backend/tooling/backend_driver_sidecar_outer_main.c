#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void __cheng_setCmdLine(int32_t argc, const char **argv);
void driver_c_capture_cmdline(int32_t argc, void *argv_void);
int32_t driver_c_run_default(void);
int32_t driver_c_backend_usage(void);

static int driver_c_arg_is_help(const char *arg) {
  if (arg == NULL) return 0;
  return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

static int driver_c_collect_positional_input(int argc, char **argv, const char **out) {
  int i = 0;
  if (out == NULL) return 0;
  *out = NULL;
  for (i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (arg == NULL || arg[0] == '\0') continue;
    if (arg[0] == '-') continue;
    *out = arg;
    return 1;
  }
  return 0;
}

static void driver_c_sync_env_value_flag(int argc, char **argv,
                                         const char *flag,
                                         const char *env_name) {
  size_t flag_len = 0;
  int i = 0;
  if (flag == NULL || env_name == NULL || flag[0] == '\0' || env_name[0] == '\0') return;
  flag_len = strlen(flag);
  for (i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (arg == NULL) continue;
    if (strncmp(arg, flag, flag_len) == 0 && arg[flag_len] == ':') {
      setenv(env_name, arg + flag_len + 1, 1);
      return;
    }
  }
}

static void driver_c_sync_env_bool_flag(int argc, char **argv,
                                        const char *enable_flag,
                                        const char *disable_flag,
                                        const char *env_name) {
  int i = 0;
  if (env_name == NULL || env_name[0] == '\0') return;
  for (i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (arg == NULL) continue;
    if (enable_flag != NULL && strcmp(arg, enable_flag) == 0) {
      setenv(env_name, "1", 1);
      return;
    }
    if (disable_flag != NULL && strcmp(arg, disable_flag) == 0) {
      setenv(env_name, "0", 1);
      return;
    }
  }
}

static void driver_c_sync_env_enable_flag(int argc, char **argv,
                                          const char *flag,
                                          const char *env_name) {
  int i = 0;
  if (flag == NULL || env_name == NULL || flag[0] == '\0' || env_name[0] == '\0') return;
  for (i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (arg != NULL && strcmp(arg, flag) == 0) {
      setenv(env_name, "1", 1);
      return;
    }
  }
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

int main(int argc, char **argv) {
  int i = 0;
  driver_c_sync_cli_env(argc, argv);
  __cheng_setCmdLine((int32_t)argc, (const char **)argv);
  driver_c_capture_cmdline((int32_t)argc, (void *)argv);
  for (i = 1; i < argc; ++i) {
    if (driver_c_arg_is_help(argv[i])) {
      return (int)driver_c_backend_usage();
    }
  }
  return (int)driver_c_run_default();
}
