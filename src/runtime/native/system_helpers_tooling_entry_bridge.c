#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#include "system_helpers.h"

#if !defined(CHENG_TOOLING_ENTRY_NO_BOOTSTRAP)
extern int cheng_v2c_tooling_handle(int argc, char **argv) __attribute__((weak_import));
extern int cheng_v2c_tooling_is_command(const char *cmd) __attribute__((weak_import));
#endif

static void driver_c_tooling_entry_die(const char *message) {
  fputs(message != NULL ? message : "driver_c tooling entry fatal", stderr);
  fputc('\n', stderr);
  cheng_dump_backtrace_if_enabled();
  fflush(stderr);
  exit(1);
}

 #if !defined(CHENG_TOOLING_ENTRY_NO_BOOTSTRAP)
static char *driver_c_tooling_entry_dup_range(const char *start, const char *end) {
  size_t len = 0;
  char *out = NULL;
  if (start == NULL) {
    start = "";
  }
  if (end == NULL || end < start) {
    end = start + strlen(start);
  }
  len = (size_t)(end - start);
  out = (char *)malloc(len + 1u);
  if (out == NULL) {
    driver_c_tooling_entry_die("driver_c tooling entry: alloc failed");
  }
  if (len > 0u) {
    memcpy(out, start, len);
  }
  out[len] = '\0';
  return out;
}

static char *driver_c_tooling_entry_dup_cstring(const char *text) {
  if (text == NULL) {
    text = "";
  }
  return driver_c_tooling_entry_dup_range(text, text + strlen(text));
}

static const char *driver_c_tooling_entry_cli_arg_raw(int32_t idx) {
  return __cheng_rt_paramStr(idx);
}

static int32_t driver_c_tooling_entry_cli_argc(void) {
  return __cheng_rt_paramCount();
}

static const char *driver_c_tooling_entry_program_name_raw(void) {
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

static char **driver_c_tooling_entry_cli_current_argv_dup(int *out_argc) {
  int32_t param_count = driver_c_tooling_entry_cli_argc();
  int argc = (int)param_count;
  int32_t i = 0;
  char **argv = NULL;
  if (argc <= 0) {
    argc = 1;
  }
  argv = (char **)malloc(sizeof(char *) * (size_t)(argc + 1));
  if (argv == NULL) {
    return NULL;
  }
  if (param_count <= 0) {
    argv[0] = (char *)driver_c_tooling_entry_program_name_raw();
    argv[1] = NULL;
    if (out_argc != NULL) *out_argc = 1;
    return argv;
  }
  for (i = 0; i < param_count; ++i) {
    argv[i] = (char *)driver_c_tooling_entry_cli_arg_raw(i);
  }
  argv[argc] = NULL;
  if (out_argc != NULL) *out_argc = argc;
  return argv;
}

static char *driver_c_tooling_entry_payload_label_dup(const char *payload) {
  const char *start = NULL;
  const char *stop = NULL;
  if (payload == NULL || payload[0] == '\0') {
    return driver_c_tooling_entry_dup_cstring("");
  }
  start = strstr(payload, "|label=");
  if (start != NULL) {
    start = start + 1;
  } else if (strncmp(payload, "label=", 6u) == 0) {
    start = payload;
  } else {
    return driver_c_tooling_entry_dup_cstring("");
  }
  start = start + 6;
  stop = start;
  while (*stop != '\0' && *stop != '|') {
    stop = stop + 1;
  }
  return driver_c_tooling_entry_dup_range(start, stop);
}
#endif

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
      "  tooling-selfhost-check       verify tooling stage0 -> stage1 -> stage2 fixed-point closure",
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
  size_t i = 0;
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

__attribute__((weak)) int32_t driver_c_compiler_core_local_payload_bridge(const char *payload) {
#if defined(CHENG_TOOLING_ENTRY_NO_BOOTSTRAP)
  (void)payload;
  driver_c_tooling_entry_die("driver_c compiler_core local payload bridge: tooling bootstrap unavailable in program track");
  return 1;
#else
  int argc = 0;
  char **argv = NULL;
  char *label = driver_c_tooling_entry_payload_label_dup(payload);
  const char *cmd = NULL;
  int32_t rc = 0;
  char message[512];
  argv = driver_c_tooling_entry_cli_current_argv_dup(&argc);
  if (argv == NULL) {
    free(label);
    driver_c_tooling_entry_die("driver_c compiler_core local payload bridge: argv alloc failed");
  }
  cmd = argc > 1 ? argv[1] : "";
  if (cheng_v2c_tooling_handle == NULL || cheng_v2c_tooling_is_command == NULL) {
    free(argv);
    free(label);
    driver_c_tooling_entry_die("driver_c compiler_core local payload bridge: bootstrap tooling handle unavailable");
  }
  if (cmd == NULL || !cheng_v2c_tooling_is_command(cmd)) {
    snprintf(message,
             sizeof(message),
             "driver_c compiler_core local payload bridge: unsupported label=%s cmd=%s",
             label != NULL ? label : "",
             cmd != NULL ? cmd : "");
    free(argv);
    free(label);
    driver_c_tooling_entry_die(message);
  }
  rc = cheng_v2c_tooling_handle(argc, argv);
  free(argv);
  free(label);
  return rc;
#endif
}

__attribute__((weak)) int32_t driver_c_compiler_core_tooling_local_payload_bridge(const char *payload) {
  return driver_c_compiler_core_local_payload_bridge(payload);
}
