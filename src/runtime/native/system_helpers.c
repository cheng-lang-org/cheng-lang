#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#if !defined(_WIN32) && !defined(__ANDROID__)
#if defined(__has_include)
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define CHENG_HAS_EXECINFO 1
#endif
#endif
#endif

#ifndef CHENG_HAS_EXECINFO
#define CHENG_HAS_EXECINFO 0
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

typedef struct ChengSeqHeader {
    int32_t len;
    int32_t cap;
    void* buffer;
} ChengSeqHeader;

typedef void (*cheng_emit_obj_from_module_fn)(ChengSeqHeader* out, void* module, int32_t optLevel,
                                              const char* target, const char* objWriter,
                                              int32_t validateModule, int32_t uirSimdEnabled,
                                              int32_t uirSimdMaxWidth, const char* uirSimdPolicy);
typedef int32_t (*cheng_driver_emit_obj_default_fn)(void* module, const char* target,
                                                    const char* outPath);
typedef void* (*cheng_build_active_module_ptrs_fn)(void* inputRaw, void* targetRaw);
typedef void* (*cheng_build_module_stage1_fn)(const char* path, const char* target);

static int cheng_arg_is_help(const char* arg) {
    if (arg == NULL) return 0;
    return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

WEAK int32_t driver_c_backend_usage(void) {
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

WEAK void driver_c_boot_marker(int32_t code);
WEAK void* driver_c_build_module_stage1(const char* inputPath, const char* target);
WEAK int32_t driver_c_arg_count(void);
WEAK char* driver_c_arg_copy(int32_t i);
WEAK int32_t __cheng_rt_paramCount(void);
WEAK const char* __cheng_rt_paramStr(int32_t i);

int32_t cheng_strlen(char* s);
void* cheng_malloc(int32_t size);
int32_t cheng_file_exists(const char* path);
int64_t cheng_file_size(const char* path);
int32_t cheng_open_w_trunc(const char* path);
int32_t cheng_fd_write(int32_t fd, const char* data, int32_t len);
int32_t libc_close(int32_t fd);
WEAK int32_t driver_c_finish_single_link(const char* path, const char* objPath);
WEAK int32_t driver_c_finish_emit_obj(const char* path);
WEAK void* driver_c_build_module_stage1(const char* inputPath, const char* target);

#if defined(CHENG_BACKEND_DRIVER_ENTRY_SHIM)
extern int32_t driver_backendBuildActiveObjNativePtrs(void* inputRaw, void* targetRaw, void* outputRaw);
extern int32_t driver_backendBuildActiveExeNativePtrs(void* inputRaw, void* targetRaw, void* outputRaw, void* linkerRaw);
#endif

#if defined(CHENG_BACKEND_DRIVER_ENTRY_SHIM)
extern int32_t backendMain(void);
WEAK void __cheng_setCmdLine(int32_t argc, const char** argv);
WEAK int main(int argc, char** argv) {
    __cheng_setCmdLine((int32_t)argc, (const char**)argv);
    for (int i = 1; i < argc; ++i) {
        if (cheng_arg_is_help(argv[i])) {
            return driver_c_backend_usage();
        }
    }
    return backendMain();
}
#elif defined(CHENG_TOOLING_ENTRY_SHIM)
extern int32_t toolingMainWithCount(int32_t count);
WEAK void __cheng_setCmdLine(int32_t argc, const char** argv);
WEAK int main(int argc, char** argv) {
    int32_t count = 0;
    __cheng_setCmdLine((int32_t)argc, (const char**)argv);
    if (argc > 1 && argc <= 4096) {
        count = (int32_t)argc - 1;
    }
    return toolingMainWithCount(count);
}
#endif

/*
 * Command-line runtime bridge:
 * `std/cmdline` now reads argv from `__cheng_rt_paramCount/__cheng_rt_paramStr`.
 * Keep `paramCount/paramStr` as weak compatibility aliases for one migration cycle.
 */
static int32_t cheng_saved_argc = 0;
static const char** cheng_saved_argv = NULL;

WEAK void __cheng_setCmdLine(int32_t argc, const char** argv) {
    if (argc <= 0 || argc > 4096 || argv == NULL) {
        cheng_saved_argc = 0;
        cheng_saved_argv = NULL;
        return;
    }
    cheng_saved_argc = argc;
    cheng_saved_argv = argv;
}


WEAK int32_t __cheng_rt_paramCount(void) {
    if (cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL) {
        return cheng_saved_argc;
    }
#if defined(__APPLE__)
    int* argc_ptr = _NSGetArgc();
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

/*
 * Compatibility shim for legacy stage0 codegen that can externalize
 * cmdline-local helper calls.
 */
WEAK int32_t __cmdCountFromRuntime(void) {
    int32_t argc = __cheng_rt_paramCount();
    if (argc <= 0 || argc > 4096) {
        return 0;
    }
    return argc;
}

WEAK int32_t cmdCountFromRuntime(void) {
    return __cmdCountFromRuntime();
}

WEAK const char* __cheng_rt_paramStr(int32_t i) {
    if (i >= 0 && cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL && i < cheng_saved_argc) {
        const char* s = cheng_saved_argv[i];
        return s != NULL ? s : "";
    }
#if defined(__APPLE__)
    if (i < 0) {
        return "";
    }
    int* argc_ptr = _NSGetArgc();
    char*** argv_ptr = _NSGetArgv();
    if (argc_ptr == NULL || argv_ptr == NULL || *argv_ptr == NULL) {
        return "";
    }
    int argc = *argc_ptr;
    if (argc <= 0 || argc > 4096 || i >= argc) {
        return "";
    }
    char* s = (*argv_ptr)[i];
    return s != NULL ? s : "";
#else
    (void)i;
    return "";
#endif
}

WEAK char* __cheng_rt_paramStrCopy(int32_t i) {
    const char* s = __cheng_rt_paramStr(i);
    if (s == NULL) {
        s = "";
    }
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1u);
    if (out == NULL) {
        return (char*)"";
    }
    if (n > 0) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

WEAK char* __cheng_rt_programBaseNameCopy(void) {
    const char* s = __cheng_rt_paramStr(0);
    if (s == NULL) {
        s = "";
    }
    const char* base = s;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    size_t n = strlen(base);
    if (n > 3 && base[n - 3] == '.' && base[n - 2] == 's' && base[n - 1] == 'h') {
        n -= 3;
    }
    char* out = (char*)malloc(n + 1u);
    if (out == NULL) {
        return (char*)"";
    }
    if (n > 0) {
        memcpy(out, base, n);
    }
    out[n] = '\0';
    return out;
}

WEAK int32_t paramCount(void) {
    int32_t argc = __cheng_rt_paramCount();
    if (argc <= 0) {
        return 0;
    }
    return argc - 1;
}

WEAK const char* paramStr(int32_t i) {
    return __cheng_rt_paramStr(i);
}

#if !defined(_WIN32)
int32_t SystemFunction036(void* buf, int32_t len) {
    (void)buf;
    (void)len;
    return 0;
}
#endif

WEAK int32_t c_puts(char* text) {
    return puts(text != NULL ? text : "");
}

WEAK char* c_getenv(char* name) {
    if (name == NULL) {
        return NULL;
    }
    return getenv(name);
}

WEAK char* driver_c_getenv(const char* name) {
    if (name == NULL) {
        return NULL;
    }
    return getenv(name);
}

WEAK int32_t driver_c_env_bool(const char* name, int32_t defaultValue) {
    const char* raw = getenv(name != NULL ? name : "");
    if (raw == NULL || raw[0] == 0) {
        return defaultValue ? 1 : 0;
    }
    if (strcmp(raw, "1") == 0 || strcmp(raw, "true") == 0 || strcmp(raw, "TRUE") == 0 ||
        strcmp(raw, "yes") == 0 || strcmp(raw, "YES") == 0 || strcmp(raw, "on") == 0 ||
        strcmp(raw, "ON") == 0) {
        return 1;
    }
    if (strcmp(raw, "0") == 0 || strcmp(raw, "false") == 0 || strcmp(raw, "FALSE") == 0 ||
        strcmp(raw, "no") == 0 || strcmp(raw, "NO") == 0 || strcmp(raw, "off") == 0 ||
        strcmp(raw, "OFF") == 0) {
        return 0;
    }
    return defaultValue ? 1 : 0;
}

WEAK char* driver_c_getenv_copy(const char* name) {
    const char* raw = getenv(name != NULL ? name : "");
    if (raw == NULL) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    size_t len = strlen(raw);
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (out == NULL) return NULL;
    memcpy(out, raw, len);
    out[len] = 0;
    return out;
}

static char* driver_c_dup_cstr(const char* raw) {
    if (raw == NULL) {
        raw = "";
    }
    size_t len = strlen(raw);
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (out == NULL) return NULL;
    if (len > 0) {
        memcpy(out, raw, len);
    }
    out[len] = 0;
    return out;
}

static char* driver_c_cli_value_copy(const char* key) {
    if (key == NULL || key[0] == '\0') {
        return driver_c_dup_cstr("");
    }
    int32_t argc = driver_c_arg_count();
    size_t key_len = strlen(key);
    if (argc <= 0 || argc > 4096) {
        return driver_c_dup_cstr("");
    }
    for (int32_t i = 1; i <= argc; ++i) {
        const char* arg = driver_c_arg_copy(i);
        if (arg == NULL || arg[0] == '\0') {
            continue;
        }
        if (strcmp(arg, key) == 0) {
            if (i + 1 <= argc) {
                return driver_c_dup_cstr(driver_c_arg_copy(i + 1));
            }
            return driver_c_dup_cstr("");
        }
        if (strncmp(arg, key, key_len) == 0 && (arg[key_len] == ':' || arg[key_len] == '=')) {
            return driver_c_dup_cstr(arg + key_len + 1);
        }
    }
    return driver_c_dup_cstr("");
}

WEAK char* driver_c_cli_input_copy(void) {
    char* value = driver_c_cli_value_copy("--input");
    if (value != NULL && value[0] != '\0') {
        return value;
    }
    int32_t argc = driver_c_arg_count();
    if (argc > 0 && argc <= 4096) {
        for (int32_t i = 1; i <= argc; ++i) {
            const char* arg = driver_c_arg_copy(i);
            if (arg == NULL || arg[0] == '\0') {
                continue;
            }
            if (arg[0] != '-') {
                return driver_c_dup_cstr(arg);
            }
        }
    }
    return value;
}

WEAK char* driver_c_cli_output_copy(void) {
    return driver_c_cli_value_copy("--output");
}

WEAK char* driver_c_cli_target_copy(void) {
    return driver_c_cli_value_copy("--target");
}

WEAK char* driver_c_cli_linker_copy(void) {
    return driver_c_cli_value_copy("--linker");
}

WEAK char* driver_c_new_string(int32_t n) {
    if (n <= 0) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    char* out = (char*)cheng_malloc(n + 1);
    if (out == NULL) return NULL;
    memset(out, 0, (size_t)n + 1u);
    return out;
}

WEAK int32_t driver_c_argv_is_help(int32_t argc, void* argv_void) {
    char** argv = (char**)argv_void;
    if (argc != 2 || argv == NULL) return 0;
    const char* arg1 = argv[1];
    if (arg1 == NULL) return 0;
    return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
}

WEAK int32_t driver_c_cli_help_requested(void) {
    if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) {
        return driver_c_argv_is_help(cheng_saved_argc, (void*)cheng_saved_argv);
    }
    if (__cheng_rt_paramCount != NULL && __cheng_rt_paramStrCopy != NULL) {
        int32_t argc = __cheng_rt_paramCount();
        if (argc != 2) return 0;
        char* arg1 = __cheng_rt_paramStrCopy(1);
        if (arg1 == NULL) return 0;
        return (strcmp(arg1, "--help") == 0 || strcmp(arg1, "-h") == 0) ? 1 : 0;
    }
    return 0;
}

WEAK int32_t driver_c_env_nonempty(const char* name) {
    const char* raw = getenv(name != NULL ? name : "");
    return (raw != NULL && raw[0] != '\0') ? 1 : 0;
}

WEAK int32_t driver_c_env_eq(const char* name, const char* expected) {
    const char* raw = getenv(name != NULL ? name : "");
    if (raw == NULL || raw[0] == '\0' || expected == NULL) {
        return 0;
    }
    return strcmp(raw, expected) == 0 ? 1 : 0;
}

static const char* driver_c_host_target_default(void) {
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

WEAK char* driver_c_active_output_path(const char* inputPath) {
    const char* raw = getenv("BACKEND_OUTPUT");
    if (raw != NULL && raw[0] != '\0') {
        return driver_c_getenv_copy("BACKEND_OUTPUT");
    }
    return (char*)(inputPath != NULL ? inputPath : "");
}

WEAK char* driver_c_active_output_copy(void) {
    const char* raw = getenv("BACKEND_OUTPUT");
    if (raw != NULL && raw[0] != '\0') {
        return driver_c_getenv_copy("BACKEND_OUTPUT");
    }
    return (char*)"";
}

WEAK char* driver_c_active_input_path(void) {
    const char* raw = getenv("BACKEND_INPUT");
    if (raw != NULL && raw[0] != '\0') {
        return driver_c_getenv_copy("BACKEND_INPUT");
    }
    return (char*)"";
}

WEAK char* driver_c_active_target(void) {
    const char* raw = getenv("BACKEND_TARGET");
    if (raw == NULL || raw[0] == '\0' ||
        strcmp(raw, "auto") == 0 || strcmp(raw, "native") == 0 || strcmp(raw, "host") == 0) {
        return (char*)driver_c_host_target_default();
    }
    return driver_c_getenv_copy("BACKEND_TARGET");
}

WEAK char* driver_c_active_linker(void) {
    const char* raw = getenv("BACKEND_LINKER");
    if (raw == NULL || raw[0] == '\0') {
        return (char*)"system";
    }
    return driver_c_getenv_copy("BACKEND_LINKER");
}

static int32_t driver_c_target_is_darwin_alias(const char* raw) {
    if (raw == NULL || raw[0] == '\0') return 0;
    return strcmp(raw, "arm64-apple-darwin") == 0 ||
           strcmp(raw, "aarch64-apple-darwin") == 0 ||
           strcmp(raw, "arm64-darwin") == 0 ||
           strcmp(raw, "aarch64-darwin") == 0 ||
           strcmp(raw, "darwin_arm64") == 0 ||
           strcmp(raw, "darwin_aarch64") == 0;
}

static char* driver_c_resolve_input_path(void) {
    char* cli = driver_c_cli_input_copy();
    if (cli != NULL && cli[0] != '\0') return cli;
    return driver_c_active_input_path();
}

static char* driver_c_resolve_output_path(const char* input_path) {
    char* cli = driver_c_cli_output_copy();
    if (cli != NULL && cli[0] != '\0') return cli;
    return driver_c_active_output_path(input_path);
}

static char* driver_c_resolve_target(void) {
    char* cli = driver_c_cli_target_copy();
    if (cli != NULL && cli[0] != '\0') {
        if (strcmp(cli, "auto") == 0 || strcmp(cli, "native") == 0 || strcmp(cli, "host") == 0) {
            return (char*)driver_c_host_target_default();
        }
        if (driver_c_target_is_darwin_alias(cli)) return (char*)"arm64-apple-darwin";
        return cli;
    }
    {
        char* active = driver_c_active_target();
        if (active == NULL || active[0] == '\0') return (char*)driver_c_host_target_default();
        if (strcmp(active, "auto") == 0 || strcmp(active, "native") == 0 || strcmp(active, "host") == 0) {
            return (char*)driver_c_host_target_default();
        }
        if (driver_c_target_is_darwin_alias(active)) return (char*)"arm64-apple-darwin";
        return active;
    }
}

static char* driver_c_resolve_linker(void) {
    char* cli = driver_c_cli_linker_copy();
    if (cli != NULL && cli[0] != '\0') return cli;
    return driver_c_active_linker();
}

static int32_t driver_c_emit_is_obj_mode(void) {
    const char* raw = getenv("BACKEND_EMIT");
    if (raw == NULL || raw[0] == '\0' || strcmp(raw, "exe") == 0) return 0;
    if (strcmp(raw, "obj") == 0 && driver_c_env_bool("BACKEND_INTERNAL_ALLOW_EMIT_OBJ", 0) != 0) {
        return 1;
    }
    fputs("backend_driver: invalid emit mode (expected exe)\n", stderr);
    return -50;
}

static void driver_c_require_output_file_or_die(const char* path, const char* phase) {
    const char* phase_text = (phase != NULL && phase[0] != '\0') ? phase : "<unknown>";
    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "backend_driver: missing output path after %s\n", phase_text);
        exit(1);
    }
    if (cheng_file_exists((char*)path) == 0) {
        fprintf(stderr, "backend_driver: missing output after %s: %s\n", phase_text, path);
        exit(1);
    }
    if (cheng_file_size((char*)path) <= 0) {
        fprintf(stderr, "backend_driver: empty output after %s: %s\n", phase_text, path);
        exit(1);
    }
}

WEAK int32_t driver_c_finish_emit_obj(const char* path) {
    driver_c_require_output_file_or_die(path, "emit_obj");
    driver_c_boot_marker(5);
    return 0;
}

WEAK int32_t driver_c_write_exact_file(const char* path, void* buffer, int32_t len) {
    if (path == NULL || path[0] == '\0') return -1;
    if (buffer == NULL || len <= 0) return -2;
    int32_t fd = cheng_open_w_trunc(path);
    if (fd < 0) return -3;
    int32_t wrote = cheng_fd_write(fd, (const char*)buffer, len);
    (void)libc_close(fd);
    if (wrote != len) return -4;
    if (cheng_file_exists((char*)path) == 0) return -5;
    if (cheng_file_size((char*)path) != (int64_t)len) return -6;
    return 0;
}

WEAK int32_t driver_c_emit_obj_default(void* module, const char* target, const char* path) {
    cheng_driver_emit_obj_default_fn emitObjFile =
        (cheng_driver_emit_obj_default_fn)dlsym(RTLD_DEFAULT, "driver_emitObjFromModuleDefault");
    cheng_emit_obj_from_module_fn emitObj =
        (cheng_emit_obj_from_module_fn)dlsym(RTLD_DEFAULT, "uirEmitObjFromModuleOrPanic");
    ChengSeqHeader objBytes;
    memset(&objBytes, 0, sizeof(objBytes));
    if (module == NULL) return -11;
    if (path == NULL || path[0] == '\0') return -12;
    if (target == NULL) target = "";
    if (emitObjFile != NULL) return emitObjFile(module, target, path);
    if (emitObj == NULL) return -10;
    emitObj(&objBytes, module, 0, target, "", 0, 0, 0, "autovec");
    if (objBytes.len <= 0) return -13;
    if (objBytes.buffer == NULL) return -14;
    return driver_c_write_exact_file(path, objBytes.buffer, objBytes.len);
}

WEAK int32_t driver_c_link_tmp_obj_default(const char* outputPath, const char* objPath,
                                           const char* target, const char* linker) {
    const char* linkerText = (linker != NULL && linker[0] != '\0') ? linker : "system";
    const char* runtimeC = "/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c";
    const char* cflags = getenv("BACKEND_CFLAGS");
    const char* ldflags = getenv("BACKEND_LDFLAGS");
    const char* cflagsText = (cflags != NULL) ? cflags : "";
    const char* ldflagsText = (ldflags != NULL) ? ldflags : "";
    if (outputPath == NULL || outputPath[0] == '\0') return -20;
    if (objPath == NULL || objPath[0] == '\0') return -21;
    if (strcmp(linkerText, "system") != 0) return -22;
    (void)target;
    size_t need = strlen(objPath) + strlen(runtimeC) + strlen(outputPath) +
                  strlen(cflagsText) + strlen(ldflagsText) + 96u;
    char* cmd = (char*)malloc(need);
    if (cmd == NULL) return -23;
    snprintf(cmd, need, "cc %s '%s' '%s' %s -o '%s'",
             cflagsText, objPath, runtimeC, ldflagsText, outputPath);
    int rc = system(cmd);
    free(cmd);
    if (rc != 0) return -24;
    if (cheng_file_exists((char*)outputPath) == 0) return -25;
    if (cheng_file_size((char*)outputPath) <= 0) return -26;
    return 0;
}

WEAK int32_t driver_c_link_tmp_obj_system(const char* outputPath, const char* objPath,
                                          const char* target) {
    return driver_c_link_tmp_obj_default(outputPath, objPath, target, "system");
}

WEAK int32_t driver_c_build_emit_obj_default(const char* inputPath, const char* target,
                                             const char* outputPath) {
    if (inputPath == NULL || inputPath[0] == '\0') return -30;
    if (target == NULL || target[0] == '\0') return -31;
    if (outputPath == NULL || outputPath[0] == '\0') return -32;
    void* module = driver_c_build_module_stage1(inputPath, target);
    if (module == NULL) return -33;
    int32_t emitRc = driver_c_emit_obj_default(module, target, outputPath);
    if (emitRc != 0) return emitRc;
    return driver_c_finish_emit_obj(outputPath);
}

WEAK int32_t driver_c_build_active_obj_default(void) {
    driver_c_boot_marker(34);
    char* inputPath = driver_c_active_input_path();
    if (inputPath == NULL || inputPath[0] == '\0') return 2;
    char* target = driver_c_active_target();
    char* outputPath = driver_c_active_output_path(inputPath);
    driver_c_boot_marker(35);
    int32_t emitRc = driver_c_build_emit_obj_default(inputPath, target, outputPath);
    if (emitRc != 0) return emitRc;
    driver_c_boot_marker(36);
    return 0;
}

WEAK int32_t driver_c_build_link_exe_default(const char* inputPath, const char* target,
                                             const char* outputPath, const char* linker) {
    (void)linker;
    if (inputPath == NULL || inputPath[0] == '\0') return -40;
    if (target == NULL || target[0] == '\0') return -41;
    if (outputPath == NULL || outputPath[0] == '\0') return -42;
    const char* suffix = ".tmp.linkobj";
    size_t need = strlen(outputPath) + strlen(suffix) + 1u;
    char* objPath = (char*)malloc(need);
    if (objPath == NULL) return -43;
    snprintf(objPath, need, "%s%s", outputPath, suffix);
    int32_t emitRc = driver_c_build_emit_obj_default(inputPath, target, objPath);
    if (emitRc != 0) {
        free(objPath);
        return emitRc;
    }
    int32_t linkRc = driver_c_link_tmp_obj_system(outputPath, objPath, target);
    if (linkRc != 0) {
        free(objPath);
        return linkRc;
    }
    int32_t finishRc = driver_c_finish_single_link(outputPath, objPath);
    free(objPath);
    return finishRc;
}

WEAK void* driver_c_build_module_stage1(const char* inputPath, const char* target) {
    cheng_build_active_module_ptrs_fn buildActiveModulePtrs =
        (cheng_build_active_module_ptrs_fn)dlsym(RTLD_DEFAULT, "driver_buildActiveModulePtrs");
    cheng_build_module_stage1_fn buildModuleStage1 =
        (cheng_build_module_stage1_fn)dlsym(RTLD_DEFAULT, "uirCoreBuildModuleFromFileStage1OrPanic");
    if (inputPath == NULL || inputPath[0] == '\0') return NULL;
    if (target == NULL) target = "";
    if (buildModuleStage1 != NULL) return buildModuleStage1(inputPath, target);
    if (buildActiveModulePtrs != NULL) return buildActiveModulePtrs((void*)inputPath, (void*)target);
    return NULL;
}

WEAK int32_t driver_c_build_active_exe_default(void) {
    driver_c_boot_marker(37);
    char* inputPath = driver_c_active_input_path();
    if (inputPath == NULL || inputPath[0] == '\0') return 2;
    char* target = driver_c_active_target();
    char* outputPath = driver_c_active_output_path(inputPath);
    char* linker = driver_c_active_linker();
    driver_c_boot_marker(38);
    return driver_c_build_link_exe_default(inputPath, target, outputPath, linker);
}

WEAK int32_t driver_c_run_default(void) {
    int32_t emit_mode = driver_c_emit_is_obj_mode();
    if (emit_mode < 0) return emit_mode;
    char* input_path = driver_c_resolve_input_path();
    char* target = driver_c_resolve_target();
    char* output_path = driver_c_resolve_output_path(input_path);
    char* linker = driver_c_resolve_linker();
    if (input_path == NULL || input_path[0] == '\0') return 2;
    if (target == NULL || target[0] == '\0') return 2;
    if (output_path == NULL || output_path[0] == '\0') return 2;
    if (emit_mode != 0) return driver_c_build_emit_obj_default(input_path, target, output_path);
    return driver_c_build_link_exe_default(input_path, target, output_path, linker);
}

WEAK int32_t driver_c_finish_single_link(const char* path, const char* objPath) {
    driver_c_require_output_file_or_die(path, "single_link");
    if (driver_c_env_bool("BACKEND_KEEP_TMP_LINKOBJ", 0) == 0 &&
        objPath != NULL && objPath[0] != '\0') {
        unlink(objPath);
    }
    driver_c_boot_marker(8);
    return 0;
}

static int32_t driver_c_boot_marker_enabled(void) {
    const char* raw = getenv("BACKEND_DEBUG_BOOT");
    if (raw == NULL || raw[0] == '\0') {
        return 0;
    }
    if (raw[0] == '0' && raw[1] == '\0') {
        return 0;
    }
    return 1;
}

static void driver_c_boot_marker_write(const char* text, size_t len) {
    if (!driver_c_boot_marker_enabled()) {
        return;
    }
    if (text == NULL || len == 0) {
        return;
    }
    (void)write(2, text, len);
}

WEAK void driver_c_boot_marker(int32_t code) {
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

WEAK void driver_c_capture_cmdline(int32_t argc, void* argv_void) {
    cheng_saved_argc = argc;
    cheng_saved_argv = (const char**)argv_void;
}

WEAK int32_t driver_c_capture_cmdline_keep(int32_t argc, void* argv_void) {
    driver_c_capture_cmdline(argc, argv_void);
    return argc;
}

WEAK int32_t driver_c_arg_count(void) {
    if (cheng_saved_argc > 1 && cheng_saved_argv != NULL) return cheng_saved_argc - 1;
    int32_t argc = __cheng_rt_paramCount();
    if (argc > 1 && argc <= 257) return argc - 1;
    return 0;
}

WEAK char* driver_c_arg_copy(int32_t i) {
    if (cheng_saved_argv == NULL && i > 0) {
        char* rt = __cheng_rt_paramStrCopy(i);
        if (rt != NULL) return rt;
    }
    if (cheng_saved_argv == NULL || i <= 0 || i >= cheng_saved_argc) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    const char* raw = cheng_saved_argv[i];
    if (raw == NULL) {
        char* out = (char*)cheng_malloc(1);
        if (out != NULL) out[0] = 0;
        return out;
    }
    size_t len = strlen(raw);
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (out == NULL) return NULL;
    memcpy(out, raw, len);
    out[len] = 0;
    return out;
}

WEAK int32_t driver_c_help_requested(void) {
    if (cheng_saved_argv != NULL && cheng_saved_argc > 1 && cheng_saved_argc <= 4096) {
        for (int32_t i = 1; i < cheng_saved_argc; ++i) {
            const char* arg = cheng_saved_argv[i];
            if (arg == NULL) continue;
            if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
        }
        return 0;
    }
    int32_t argc = __cheng_rt_paramCount();
    if (argc > 1 && argc <= 4096) {
        for (int32_t i = 1; i < argc; ++i) {
            char* arg = __cheng_rt_paramStrCopy(i);
            if (arg == NULL) continue;
            if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) return 1;
        }
    }
    return 0;
}

WEAK int32_t c_strcmp(char* a, char* b) {
    return strcmp(a != NULL ? a : "", b != NULL ? b : "");
}

WEAK int32_t __cheng_str_eq(const char* a, const char* b) {
    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0 ? 1 : 0;
}

WEAK int32_t c_strlen(char* s) {
    return cheng_strlen(s);
}

WEAK void c_iometer_call(void* hook, int32_t op, int64_t bytes) {
    (void)hook;
    (void)op;
    (void)bytes;
}

WEAK int32_t libc_remove(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    return remove(path);
}

WEAK int32_t libc_open(const char* path, int32_t flags, int32_t mode) {
    if (path == NULL) {
        return -1;
    }
    return open(path, flags, mode);
}

WEAK int32_t cheng_open_w_trunc(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    return creat(path, 0644);
}

WEAK int32_t libc_close(int32_t fd) {
    if (fd < 0) {
        return -1;
    }
    return close(fd);
}

WEAK int64_t libc_write(int32_t fd, void* data, int64_t n) {
    if (fd < 0 || data == NULL || n <= 0) {
        return 0;
    }
    ssize_t wrote = write(fd, data, (size_t)n);
    if (wrote < 0) {
        return -1;
    }
    return (int64_t)wrote;
}

WEAK void zeroMem(void* p, int64_t n) {
    if (p == NULL || n <= 0) {
        return;
    }
    memset(p, 0, (size_t)n);
}

WEAK void* openImpl(char* filename, int32_t mode) {
    const char* path = filename != NULL ? filename : "";
    const char* openMode = "rb";
    if (mode == 1) {
        openMode = "wb";
    } else if (mode != 0) {
        openMode = "rb+";
    }
    return fopen(path, openMode);
}

WEAK uint64_t processOptionMask(int32_t opt) {
    if (opt < 0 || opt >= 63) {
        return 0;
    }
    return ((uint64_t)1) << (uint64_t)opt;
}

WEAK char* sliceStr(char* text, int32_t start, int32_t stop) {
    if (text == NULL) {
        return (char*)"";
    }
    int32_t n = cheng_strlen(text);
    if (n <= 0) {
        return (char*)"";
    }
    int32_t a = start < 0 ? 0 : start;
    int32_t b = stop;
    if (b >= n) {
        b = n - 1;
    }
    if (b < a) {
        return (char*)"";
    }
    int32_t span = b - a + 1;
    char* out = (char*)malloc((size_t)span + 1u);
    if (out == NULL) {
        return (char*)"";
    }
    memcpy(out, text + a, (size_t)span);
    out[span] = '\0';
    return out;
}

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef struct ChengMemScope ChengMemScope;

typedef struct ChengMemBlock {
    struct ChengMemBlock* prev;
    struct ChengMemBlock* next;
    ChengMemScope* scope;
    size_t size;
    int32_t rc;
} ChengMemBlock;

struct ChengMemScope {
    ChengMemScope* parent;
    ChengMemBlock* head;
};

static ChengMemScope cheng_global_scope = {NULL, NULL};
static ChengMemScope* cheng_scope_current = &cheng_global_scope;
static int64_t cheng_mm_retain_total = 0;
static int64_t cheng_mm_release_total = 0;
static int64_t cheng_mm_alloc_total = 0;
static int64_t cheng_mm_free_total = 0;
static int64_t cheng_mm_live_total = 0;
static int cheng_mm_diag = -1;
static int cheng_mm_ptrmap_enabled = -1;
static int cheng_mm_ptrmap_scan_enabled = -1;
static int cheng_mm_disabled = -1;
static int cheng_mm_atomic = -1;

static int cheng_mm_is_disabled(void) {
    if (cheng_mm_disabled >= 0) {
        return cheng_mm_disabled;
    }
    const char* env = getenv("MM");
    if (env == NULL || env[0] == '\0') {
        cheng_mm_disabled = 0;
        return cheng_mm_disabled;
    }
    if (env[0] == '0') {
        cheng_mm_disabled = 1;
        return cheng_mm_disabled;
    }
    if (strcmp(env, "off") == 0 || strcmp(env, "none") == 0 ||
        strcmp(env, "false") == 0 || strcmp(env, "no") == 0) {
        cheng_mm_disabled = 1;
        return cheng_mm_disabled;
    }
    cheng_mm_disabled = 0;
    return cheng_mm_disabled;
}

static int cheng_mm_atomic_enabled(void) {
    if (cheng_mm_atomic >= 0) {
        return cheng_mm_atomic;
    }
    const char* env = getenv("MM_ATOMIC");
    if (!env || env[0] == '\0') {
        cheng_mm_atomic = 1;
        return cheng_mm_atomic;
    }
    if (strcmp(env, "0") == 0 || strcmp(env, "false") == 0 || strcmp(env, "no") == 0) {
        cheng_mm_atomic = 0;
        return cheng_mm_atomic;
    }
    cheng_mm_atomic = 1;
    return cheng_mm_atomic;
}

typedef struct ChengPtrMap {
    uintptr_t* keys;
    ChengMemBlock** vals;
    size_t cap;
    size_t count;
    size_t tombs;
} ChengPtrMap;

static ChengPtrMap cheng_block_map = {NULL, NULL, 0, 0, 0};
static const uintptr_t cheng_ptrmap_tomb = 1;
static const size_t cheng_ptrmap_init_cap = 65536;

static inline uintptr_t cheng_ptr_hash(uintptr_t v) {
#if UINTPTR_MAX > 0xffffffffu
    // Fast pointer hash: ignore alignment bits, then mix with one multiply.
    // This is cheaper than a full finalizer but distributes sequential allocs well.
    v >>= 3;
    v ^= v >> 33;
    v *= (uintptr_t)0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    return v;
#else
    v >>= 2;
    v ^= v >> 16;
    v *= (uintptr_t)0x7feb352dU;
    v ^= v >> 15;
    return v;
#endif
}

static void cheng_ptrmap_init(size_t cap) {
    size_t n = 1;
    while (n < cap) {
        n <<= 1;
    }
    cheng_block_map.keys = (uintptr_t*)calloc(n, sizeof(uintptr_t));
    cheng_block_map.vals = (ChengMemBlock**)calloc(n, sizeof(ChengMemBlock*));
    if (!cheng_block_map.keys || !cheng_block_map.vals) {
        free(cheng_block_map.keys);
        free(cheng_block_map.vals);
        cheng_block_map.keys = NULL;
        cheng_block_map.vals = NULL;
        cheng_block_map.cap = 0;
        cheng_block_map.count = 0;
        cheng_block_map.tombs = 0;
        return;
    }
    cheng_block_map.cap = n;
    cheng_block_map.count = 0;
    cheng_block_map.tombs = 0;
}

static void cheng_ptrmap_grow(void) {
    size_t oldcap = cheng_block_map.cap;
    uintptr_t* oldkeys = cheng_block_map.keys;
    ChengMemBlock** oldvals = cheng_block_map.vals;
    size_t newcap = oldcap ? oldcap * 2 : cheng_ptrmap_init_cap;
    size_t n = 1;
    while (n < newcap) {
        n <<= 1;
    }
    uintptr_t* newkeys = (uintptr_t*)calloc(n, sizeof(uintptr_t));
    ChengMemBlock** newvals = (ChengMemBlock**)calloc(n, sizeof(ChengMemBlock*));
    if (!newkeys || !newvals) {
        free(newkeys);
        free(newvals);
        return;
    }
    cheng_block_map.keys = newkeys;
    cheng_block_map.vals = newvals;
    cheng_block_map.cap = n;
    cheng_block_map.count = 0;
    cheng_block_map.tombs = 0;
    if (oldkeys != NULL) {
        size_t mask = cheng_block_map.cap - 1;
        for (size_t i = 0; i < oldcap; i++) {
            uintptr_t k = oldkeys[i];
            if (k > cheng_ptrmap_tomb) {
                ChengMemBlock* val = oldvals[i];
                if (val != NULL) {
                    size_t idx = (size_t)(cheng_ptr_hash(k) & mask);
                    for (;;) {
                        uintptr_t cur = cheng_block_map.keys[idx];
                        if (cur == 0) {
                            cheng_block_map.keys[idx] = k;
                            cheng_block_map.vals[idx] = val;
                            cheng_block_map.count++;
                            break;
                        }
                        idx = (idx + 1) & mask;
                    }
                }
            }
        }
        free(oldkeys);
        free(oldvals);
    }
}

static ChengMemBlock* cheng_ptrmap_get(void* key) {
    if (key == NULL || cheng_block_map.cap == 0) {
        return NULL;
    }
    uintptr_t k = (uintptr_t)key;
    if (k == 0 || k == cheng_ptrmap_tomb) {
        return NULL;
    }
    size_t mask = cheng_block_map.cap - 1;
    size_t idx = (size_t)(cheng_ptr_hash(k) & mask);
    for (;;) {
        uintptr_t cur = cheng_block_map.keys[idx];
        if (cur == 0) {
            return NULL;
        }
        if (cur == k) {
            return cheng_block_map.vals[idx];
        }
        idx = (idx + 1) & mask;
    }
}

static void cheng_ptrmap_put(void* key, ChengMemBlock* val) {
    if (key == NULL) {
        return;
    }
    uintptr_t k = (uintptr_t)key;
    if (k == 0 || k == cheng_ptrmap_tomb) {
        return;
    }
    if (cheng_block_map.cap == 0) {
        cheng_ptrmap_init(cheng_ptrmap_init_cap);
        if (cheng_block_map.cap == 0) {
            return;
        }
    }
    if ((cheng_block_map.count + cheng_block_map.tombs + 1) * 10 >= cheng_block_map.cap * 7) {
        cheng_ptrmap_grow();
    }
    size_t mask = cheng_block_map.cap - 1;
    size_t idx = (size_t)(cheng_ptr_hash(k) & mask);
    size_t tomb = (size_t)-1;
    for (;;) {
        uintptr_t cur = cheng_block_map.keys[idx];
        if (cur == 0) {
            if (tomb != (size_t)-1) {
                idx = tomb;
                cheng_block_map.tombs--;
            }
            cheng_block_map.keys[idx] = k;
            cheng_block_map.vals[idx] = val;
            cheng_block_map.count++;
            return;
        }
        if (cur == cheng_ptrmap_tomb) {
            if (tomb == (size_t)-1) {
                tomb = idx;
            }
        } else if (cur == k) {
            cheng_block_map.vals[idx] = val;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

static void cheng_ptrmap_del(void* key) {
    if (key == NULL || cheng_block_map.cap == 0) {
        return;
    }
    uintptr_t k = (uintptr_t)key;
    if (k == 0 || k == cheng_ptrmap_tomb) {
        return;
    }
    size_t mask = cheng_block_map.cap - 1;
    size_t idx = (size_t)(cheng_ptr_hash(k) & mask);
    for (;;) {
        uintptr_t cur = cheng_block_map.keys[idx];
        if (cur == 0) {
            return;
        }
        if (cur == k) {
            cheng_block_map.keys[idx] = cheng_ptrmap_tomb;
            cheng_block_map.vals[idx] = NULL;
            if (cheng_block_map.count > 0) {
                cheng_block_map.count--;
            }
            cheng_block_map.tombs++;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

static int cheng_mm_diag_enabled(void) {
    if (cheng_mm_diag >= 0) {
        return cheng_mm_diag;
    }
    const char* env = getenv("MM_DIAG");
    if (env && env[0] != '\0' && env[0] != '0') {
        cheng_mm_diag = 1;
    } else {
        cheng_mm_diag = 0;
    }
    return cheng_mm_diag;
}

static int cheng_mm_ptrmap_check(void) {
    if (cheng_mm_ptrmap_enabled >= 0) {
        return cheng_mm_ptrmap_enabled;
    }
    const char* env = getenv("MM_PTRMAP");
    if (env && env[0] != '\0') {
        if (env[0] == '0' || env[0] == 'f' || env[0] == 'F' || env[0] == 'n' || env[0] == 'N') {
            cheng_mm_ptrmap_enabled = 0;
        } else {
            cheng_mm_ptrmap_enabled = 1;
        }
    } else {
        cheng_mm_ptrmap_enabled = 1;
    }
    return cheng_mm_ptrmap_enabled;
}

static int cheng_mm_ptrmap_scan_check(void) {
    if (cheng_mm_ptrmap_scan_enabled >= 0) {
        return cheng_mm_ptrmap_scan_enabled;
    }
    const char* env = getenv("MM_PTRMAP_SCAN");
    if (env && env[0] != '\0') {
        if (env[0] == '0' || env[0] == 'f' || env[0] == 'F' || env[0] == 'n' || env[0] == 'N') {
            cheng_mm_ptrmap_scan_enabled = 0;
        } else {
            cheng_mm_ptrmap_scan_enabled = 1;
        }
    } else {
        cheng_mm_ptrmap_scan_enabled = 0;
    }
    return cheng_mm_ptrmap_scan_enabled;
}

static ChengMemScope* cheng_mem_current(void) {
    return cheng_scope_current;
}

static ChengMemBlock* cheng_mem_find_block(ChengMemScope* scope, void* p) {
    if (scope == NULL || p == NULL) {
        return NULL;
    }
    ChengMemBlock* cur = scope->head;
    while (cur) {
        if ((void*)(cur + 1) == p) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static void cheng_mem_link(ChengMemScope* scope, ChengMemBlock* block) {
    block->scope = scope;
    block->prev = NULL;
    block->next = scope->head;
    if (scope->head) {
        scope->head->prev = block;
    }
    scope->head = block;
}

static void cheng_mem_unlink(ChengMemBlock* block) {
    if (block == NULL || block->scope == NULL) {
        return;
    }
    ChengMemScope* scope = block->scope;
    if (block->prev) {
        block->prev->next = block->next;
    } else if (scope->head == block) {
        scope->head = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    block->prev = NULL;
    block->next = NULL;
    block->scope = NULL;
}

static ChengMemBlock* cheng_mem_find_block_any(void* p) {
    if (p == NULL) {
        return NULL;
    }
    if (cheng_mm_ptrmap_check()) {
        ChengMemBlock* block = cheng_ptrmap_get(p);
        if (block != NULL || !cheng_mm_ptrmap_scan_check()) {
            return block;
        }
    }
    ChengMemScope* scope = cheng_mem_current();
    while (scope) {
        ChengMemBlock* block = cheng_mem_find_block(scope, p);
        if (block != NULL) {
            return block;
        }
        scope = scope->parent;
    }
    return NULL;
}

static ChengMemBlock* cheng_mem_find_block_any_scan(void* p) {
    if (p == NULL) {
        return NULL;
    }
    if (cheng_mm_ptrmap_check()) {
        ChengMemBlock* block = cheng_ptrmap_get(p);
        if (block != NULL) {
            return block;
        }
    }
    ChengMemScope* scope = cheng_mem_current();
    while (scope) {
        ChengMemBlock* block = cheng_mem_find_block(scope, p);
        if (block != NULL) {
            return block;
        }
        scope = scope->parent;
    }
    return NULL;
}

void* cheng_mem_scope_push(void) {
    ChengMemScope* scope = (ChengMemScope*)malloc(sizeof(ChengMemScope));
    if (!scope) {
        return NULL;
    }
    scope->parent = cheng_scope_current;
    scope->head = NULL;
    cheng_scope_current = scope;
    return scope;
}

void cheng_mem_scope_pop(void) {
    ChengMemScope* scope = cheng_scope_current;
    if (scope == NULL || scope == &cheng_global_scope) {
        return;
    }
    ChengMemBlock* cur = scope->head;
    while (cur) {
        ChengMemBlock* next = cur->next;
        void* payload = (void*)(cur + 1);
        cheng_ptrmap_del(payload);
        free(cur);
        cheng_mm_free_total += 1;
        if (cheng_mm_live_total > 0) {
            cheng_mm_live_total -= 1;
        }
        cur = next;
    }
    cheng_scope_current = scope->parent ? scope->parent : &cheng_global_scope;
    free(scope);
}

void cheng_mem_scope_escape(void* p) {
    if (p == NULL) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    ChengMemScope* scope = block->scope;
    if (scope == NULL || scope == &cheng_global_scope) {
        return;
    }
    ChengMemScope* target = scope->parent ? scope->parent : &cheng_global_scope;
    if (target == scope) {
        return;
    }
    cheng_mem_unlink(block);
    cheng_mem_link(target, block);
}

void cheng_mem_scope_escape_global(void* p) {
    if (p == NULL) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    ChengMemScope* scope = block->scope;
    if (scope == NULL || scope == &cheng_global_scope) {
        return;
    }
    cheng_mem_unlink(block);
    cheng_mem_link(&cheng_global_scope, block);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void* ptr_add(void* p, int32_t offset) {
    return (void*)((uint8_t*)p + offset);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void* rawmemAsVoid(void* p) {
    return p;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
uint64_t cheng_ptr_to_u64(void* p) {
    return (uint64_t)(uintptr_t)p;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int32_t cheng_ptr_size(void) {
    return (int32_t)sizeof(void*);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void store_int32(void* p, int32_t val) {
    *(int32_t*)p = val;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int32_t load_int32(void* p) {
    return *(int32_t*)p;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void store_bool(void* p, int8_t val) {
    *(int8_t*)p = val;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
int8_t load_bool(void* p) {
    return *(int8_t*)p;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void store_ptr(void* p, void* val) {
    *(void**)p = val;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void* load_ptr(void* p) {
    return *(void**)p;
}

int32_t xor_0(int32_t a, int32_t b) {
    return a ^ b;
}

int32_t shl_0(int32_t a, int32_t b) {
    return a << b;
}

int32_t shr_0(int32_t a, int32_t b) {
    return a >> b;
}

int32_t mod_0(int32_t a, int32_t b) {
    return a % b;
}

int32_t bitand_0(int32_t a, int32_t b) {
    return a & b;
}

int32_t bitor_0(int32_t a, int32_t b) { return a | b; }
int32_t bitnot_0(int32_t a) { return ~a; }
int32_t mul_0(int32_t a, int32_t b) { return a * b; }
int32_t div_0(int32_t a, int32_t b) { return a / b; }
bool not_0(bool a) { return !a; }
int32_t cheng_puts(const char* text) { return puts(text ? text : ""); }
void cheng_exit(int32_t code) { exit(code); }
void cheng_bounds_check(int32_t len, int32_t idx) {
    if (idx < 0 || idx >= len) {
        fprintf(stderr, "[cheng] bounds check failed: idx=%d len=%d\n", idx, len);
        const char* trace = getenv("BOUNDS_TRACE");
        if (trace != NULL && trace[0] != '\0' && trace[0] != '0') {
            void* caller = __builtin_return_address(0);
            Dl_info info;
            if (dladdr(caller, &info) != 0) {
                uintptr_t pc = (uintptr_t)caller;
                uintptr_t base = (uintptr_t)info.dli_fbase;
                uintptr_t off = pc >= base ? (pc - base) : 0;
                fprintf(
                    stderr,
                    "[cheng] bounds caller=%p module=%s symbol=%s offset=0x%zx\n",
                    caller,
                    info.dli_fname ? info.dli_fname : "?",
                    info.dli_sname ? info.dli_sname : "?",
                    (size_t)off
                );
            } else {
                fprintf(stderr, "[cheng] bounds caller=%p module=? symbol=? offset=0x0\n", caller);
            }
#if CHENG_HAS_EXECINFO
            void* frames[24];
            int count = backtrace(frames, 24);
            if (count > 0) {
                backtrace_symbols_fd(frames, count, 2);
            }
#endif
        }
#if defined(__ANDROID__)
        __android_log_print(
            ANDROID_LOG_ERROR,
            "ChengRuntime",
            "bounds check failed: idx=%d len=%d",
            idx,
            len
        );
#endif
        cheng_exit(1);
    }
}

static void cheng_log_bounds_site(int32_t len, int32_t idx, int32_t elem_size, void* caller) {
#if defined(__ANDROID__)
    Dl_info info;
    if (dladdr(caller, &info) != 0) {
        uintptr_t pc = (uintptr_t)caller;
        uintptr_t base = (uintptr_t)info.dli_fbase;
        uintptr_t offset = pc >= base ? (pc - base) : 0;
        __android_log_print(
            ANDROID_LOG_ERROR,
            "ChengRuntime",
            "bounds fail idx=%d len=%d elem=%d caller=%p module=%s symbol=%s offset=0x%zx",
            idx,
            len,
            elem_size,
            caller,
            info.dli_fname ? info.dli_fname : "?",
            info.dli_sname ? info.dli_sname : "?",
            (size_t)offset
        );
        return;
    }
    __android_log_print(
        ANDROID_LOG_ERROR,
        "ChengRuntime",
        "bounds fail idx=%d len=%d elem=%d caller=%p dladdr=none",
        idx,
        len,
        elem_size,
        caller
    );
#else
    (void)len;
    (void)idx;
    (void)elem_size;
    (void)caller;
#endif
}

static void* cheng_index_ptr(void* base, int32_t len, int32_t idx, int32_t elem_size, void* caller) {
    if (idx < 0 || idx >= len) {
        cheng_log_bounds_site(len, idx, elem_size, caller);
    }
    cheng_bounds_check(len, idx);
    if (!base || elem_size <= 0) {
        return base;
    }
    size_t offset = (size_t)((int64_t)idx * (int64_t)elem_size);
    return (void*)((uint8_t*)base + offset);
}

void* cheng_seq_get(void* buffer, int32_t len, int32_t idx, int32_t elem_size) {
    return cheng_index_ptr(buffer, len, idx, elem_size, __builtin_return_address(0));
}

void* cheng_seq_set(void* buffer, int32_t len, int32_t idx, int32_t elem_size) {
    return cheng_index_ptr(buffer, len, idx, elem_size, __builtin_return_address(0));
}

void* cheng_seq_set_grow(void* seq_ptr, int32_t idx, int32_t elem_size) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (!seq || elem_size <= 0) {
        return cheng_index_ptr(NULL, 0, idx, elem_size, __builtin_return_address(0));
    }
    if (idx < 0) {
        return cheng_index_ptr(seq->buffer, seq->len, idx, elem_size, __builtin_return_address(0));
    }

    int32_t need = idx + 1;
    if (need > seq->cap || seq->buffer == NULL) {
        int32_t new_cap = seq->cap;
        if (new_cap < 4) {
            new_cap = 4;
        }
        while (new_cap < need) {
            int32_t doubled = new_cap * 2;
            if (doubled <= 0) {
                new_cap = need;
                break;
            }
            new_cap = doubled;
        }

        int32_t old_cap = seq->cap;
        size_t bytes = (size_t)new_cap * (size_t)elem_size;
        void* new_buf = realloc(seq->buffer, bytes);
        if (!new_buf) {
            return cheng_index_ptr(seq->buffer, seq->len, idx, elem_size, __builtin_return_address(0));
        }

        if (new_cap > old_cap) {
            size_t old_bytes = (size_t)old_cap * (size_t)elem_size;
            size_t grow_bytes = (size_t)(new_cap - old_cap) * (size_t)elem_size;
            memset((uint8_t*)new_buf + old_bytes, 0, grow_bytes);
        }
        seq->buffer = new_buf;
        seq->cap = new_cap;
    }

    if (need > seq->len) {
        seq->len = need;
    }
    return cheng_index_ptr(seq->buffer, seq->len, idx, elem_size, __builtin_return_address(0));
}

static int32_t cheng_seq_next_cap(int32_t cur_cap, int32_t need) {
    if (need <= 0) {
        return need;
    }
    int32_t cap = cur_cap;
    if (cap < 4) {
        cap = 4;
    }
    while (cap < need) {
        int32_t doubled = cap * 2;
        if (doubled <= 0) {
            return need;
        }
        cap = doubled;
    }
    return cap;
}

static void cheng_seq_sanitize(ChengSeqHeader* seq) {
    if (seq == NULL) {
        return;
    }
    if (seq->cap < 0 || seq->cap > (1 << 27) || seq->len < 0 || seq->len > seq->cap) {
        seq->len = 0;
        seq->cap = 0;
        seq->buffer = NULL;
        return;
    }
    if (seq->cap == 0) {
        seq->buffer = NULL;
    }
    if (seq->buffer == NULL && seq->len != 0) {
        seq->len = 0;
    }
    if (seq->buffer != NULL) {
        uintptr_t p = (uintptr_t)seq->buffer;
        if (p < 4096u || p > 0x0000FFFFFFFFFFFFull) {
            seq->len = 0;
            seq->cap = 0;
            seq->buffer = NULL;
        }
    }
}

WEAK void reserve(void* seq_ptr, int32_t new_cap) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL || new_cap < 0) {
        return;
    }
    cheng_seq_sanitize(seq);
    if (new_cap == 0) {
        return;
    }
    if (seq->buffer != NULL && new_cap <= seq->cap) {
        return;
    }
    int32_t target = cheng_seq_next_cap(seq->cap, new_cap);
    if (target <= 0) {
        return;
    }
    const size_t slot_bytes = sizeof(void*) < 32u ? 32u : sizeof(void*);
    const size_t old_bytes = (size_t)(seq->cap > 0 ? seq->cap : 0) * slot_bytes;
    const size_t new_bytes = (size_t)target * slot_bytes;
    void* new_buf = realloc(seq->buffer, new_bytes);
    if (new_buf == NULL) {
        return;
    }
    if (new_bytes > old_bytes) {
        memset((char*)new_buf + old_bytes, 0, new_bytes - old_bytes);
    }
    seq->buffer = new_buf;
    seq->cap = target;
}

WEAK void reserve_ptr_void(void* seq_ptr, int32_t new_cap) {
    reserve(seq_ptr, new_cap);
}

WEAK void setLen(void* seq_ptr, int32_t new_len) {
    ChengSeqHeader* seq = (ChengSeqHeader*)seq_ptr;
    if (seq == NULL) {
        return;
    }
    cheng_seq_sanitize(seq);
    int32_t target = new_len;
    if (target < 0) {
        target = 0;
    }
    if (target > seq->cap) {
        reserve(seq_ptr, target);
    }
    seq->len = target;
}

void* cheng_slice_get(void* ptr, int32_t len, int32_t idx, int32_t elem_size) {
    return cheng_index_ptr(ptr, len, idx, elem_size, __builtin_return_address(0));
}

void* cheng_slice_set(void* ptr, int32_t len, int32_t idx, int32_t elem_size) {
    return cheng_index_ptr(ptr, len, idx, elem_size, __builtin_return_address(0));
}

typedef void (*ChengTaskFn)(void*);

typedef struct ChengTask {
    ChengTaskFn fn;
    void* ctx;
    struct ChengTask* next;
} ChengTask;

static ChengTask* cheng_sched_head = NULL;
static ChengTask* cheng_sched_tail = NULL;
static int32_t cheng_sched_count = 0;

void cheng_spawn(void* fn_ptr, void* ctx) {
    if (!fn_ptr) {
        return;
    }
    ChengTaskFn fn = (ChengTaskFn)fn_ptr;
    ChengTask* task = (ChengTask*)malloc(sizeof(ChengTask));
    if (!task) {
        fn(ctx);
        return;
    }
    task->fn = fn;
    task->ctx = ctx;
    task->next = NULL;
    if (cheng_sched_tail) {
        cheng_sched_tail->next = task;
    } else {
        cheng_sched_head = task;
    }
    cheng_sched_tail = task;
    cheng_sched_count += 1;
}

int32_t cheng_sched_pending(void) {
    return cheng_sched_count;
}

int32_t cheng_sched_run_once(void) {
    ChengTask* task = cheng_sched_head;
    if (!task) {
        return 0;
    }
    cheng_sched_head = task->next;
    if (!cheng_sched_head) {
        cheng_sched_tail = NULL;
    }
    if (cheng_sched_count > 0) {
        cheng_sched_count -= 1;
    }
    ChengTaskFn fn = task->fn;
    void* ctx = task->ctx;
    free(task);
    if (fn) {
        fn(ctx);
    }
    return 1;
}

void cheng_sched_run(void) {
    while (cheng_sched_run_once()) {
        ;
    }
}

typedef struct ChengAwaitI32 {
    int32_t status;
    int32_t value;
} ChengAwaitI32;

typedef struct ChengAwaitVoid {
    int32_t status;
} ChengAwaitVoid;

static ChengAwaitI32* cheng_async_make_i32(int32_t ready, int32_t value) {
    ChengAwaitI32* st = (ChengAwaitI32*)malloc(sizeof(ChengAwaitI32));
    if (!st) {
        return NULL;
    }
    st->status = ready;
    st->value = value;
    return st;
}

static ChengAwaitVoid* cheng_async_make_void(int32_t ready) {
    ChengAwaitVoid* st = (ChengAwaitVoid*)malloc(sizeof(ChengAwaitVoid));
    if (!st) {
        return NULL;
    }
    st->status = ready;
    return st;
}

ChengAwaitI32* cheng_async_pending_i32(void) {
    return cheng_async_make_i32(0, 0);
}

ChengAwaitI32* cheng_async_ready_i32(int32_t value) {
    return cheng_async_make_i32(1, value);
}

void cheng_async_set_i32(ChengAwaitI32* st, int32_t value) {
    if (!st) {
        return;
    }
    st->value = value;
    st->status = 1;
}

int32_t cheng_await_i32(ChengAwaitI32* st) {
    if (!st) {
        return 0;
    }
    while (st->status == 0) {
        if (!cheng_sched_run_once()) {
            continue;
        }
    }
    return st->value;
}

ChengAwaitVoid* cheng_async_pending_void(void) {
    return cheng_async_make_void(0);
}

ChengAwaitVoid* cheng_async_ready_void(void) {
    return cheng_async_make_void(1);
}

void cheng_async_set_void(ChengAwaitVoid* st) {
    if (!st) {
        return;
    }
    st->status = 1;
}

void cheng_await_void(ChengAwaitVoid* st) {
    if (!st) {
        return;
    }
    while (st->status == 0) {
        if (!cheng_sched_run_once()) {
            continue;
        }
    }
}

typedef struct ChengChanI32 {
    int32_t cap;
    int32_t count;
    int32_t head;
    int32_t tail;
    int32_t* buffer;
} ChengChanI32;

ChengChanI32* cheng_chan_i32_new(int32_t cap) {
    if (cap <= 0) {
        cap = 1;
    }
    ChengChanI32* ch = (ChengChanI32*)malloc(sizeof(ChengChanI32));
    if (!ch) {
        return NULL;
    }
    ch->cap = cap;
    ch->count = 0;
    ch->head = 0;
    ch->tail = 0;
    ch->buffer = (int32_t*)malloc(sizeof(int32_t) * (size_t)cap);
    if (!ch->buffer) {
        free(ch);
        return NULL;
    }
    return ch;
}

int32_t cheng_chan_i32_send(ChengChanI32* ch, int32_t value) {
    if (!ch) {
        return 0;
    }
    while (ch->count >= ch->cap) {
        if (!cheng_sched_run_once()) {
            return 0;
        }
    }
    ch->buffer[ch->tail] = value;
    ch->tail = (ch->tail + 1) % ch->cap;
    ch->count += 1;
    return 1;
}

int32_t cheng_chan_i32_recv(ChengChanI32* ch, int32_t* out) {
    if (!ch || !out) {
        return 0;
    }
    while (ch->count == 0) {
        if (!cheng_sched_run_once()) {
            return 0;
        }
    }
    *out = ch->buffer[ch->head];
    ch->head = (ch->head + 1) % ch->cap;
    ch->count -= 1;
    return 1;
}

void spawn(void* fn_ptr, void* ctx) {
    cheng_spawn(fn_ptr, ctx);
}

int32_t schedPending(void) {
    return cheng_sched_pending();
}

int32_t schedRunOnce(void) {
    return cheng_sched_run_once();
}

void schedRun(void) {
    cheng_sched_run();
}

void* asyncPendingI32(void) {
    return cheng_async_pending_i32();
}

void* asyncReadyI32(int32_t value) {
    return cheng_async_ready_i32(value);
}

void asyncSetI32(void* state, int32_t value) {
    cheng_async_set_i32((ChengAwaitI32*)state, value);
}

int32_t awaitI32(void* state) {
    return cheng_await_i32((ChengAwaitI32*)state);
}

void* asyncPendingVoid(void) {
    return cheng_async_pending_void();
}

void* asyncReadyVoid(void) {
    return cheng_async_ready_void();
}

void asyncSetVoid(void* state) {
    cheng_async_set_void((ChengAwaitVoid*)state);
}

void awaitVoid(void* state) {
    cheng_await_void((ChengAwaitVoid*)state);
}

void* chanI32New(int32_t cap) {
    return cheng_chan_i32_new(cap);
}

int32_t chanI32Send(void* ch, int32_t value) {
    return cheng_chan_i32_send((ChengChanI32*)ch, value);
}

int32_t chanI32Recv(void* ch, int32_t* out) {
    return cheng_chan_i32_recv((ChengChanI32*)ch, out);
}

#include <stdio.h>
#include <string.h>

void* cheng_malloc(int32_t size) {
    if (size <= 0) {
        size = 1;
    }
    ChengMemScope* scope = cheng_mem_current();
    size_t total = sizeof(ChengMemBlock) + (size_t)size;
    ChengMemBlock* block = (ChengMemBlock*)malloc(total);
    if (!block) {
        return NULL;
    }
    block->size = (size_t)size;
    block->rc = 1;
    cheng_mem_link(scope, block);
    void* out = (void*)(block + 1);
    cheng_ptrmap_put(out, block);
    cheng_mm_alloc_total += 1;
    cheng_mm_live_total += 1;
    return out;
}

void cheng_free(void* p) {
    if (!p) {
        return;
    }
    ChengMemBlock* block = ((ChengMemBlock*)p) - 1;
    cheng_mem_unlink(block);
    cheng_ptrmap_del(p);
    free(block);
    cheng_mm_free_total += 1;
    if (cheng_mm_live_total > 0) {
        cheng_mm_live_total -= 1;
    }
}

void cheng_mem_retain(void* p) {
    if (cheng_mm_is_disabled()) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    if (cheng_mm_atomic_enabled()) {
        (void)__atomic_fetch_add(&block->rc, 1, __ATOMIC_RELAXED);
    } else {
        if (block->rc < INT32_MAX) {
            block->rc += 1;
        }
    }
    cheng_mm_retain_total += 1;
    if (cheng_mm_diag_enabled()) {
        int32_t rc = cheng_mm_atomic_enabled()
                         ? __atomic_load_n(&block->rc, __ATOMIC_RELAXED)
                         : block->rc;
        fprintf(stderr, "[mm] retain p=%p rc=%d\n", p, rc);
    }
}

void cheng_mem_release(void* p) {
    if (cheng_mm_is_disabled()) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    int32_t next_rc = 0;
    if (cheng_mm_atomic_enabled()) {
        int32_t prev = __atomic_fetch_sub(&block->rc, 1, __ATOMIC_RELEASE);
        next_rc = prev - 1;
    } else {
        if (block->rc > 0) {
            block->rc -= 1;
        }
        next_rc = block->rc;
    }
    cheng_mm_release_total += 1;
    if (cheng_mm_diag_enabled()) {
        int32_t rc = cheng_mm_atomic_enabled()
                         ? __atomic_load_n(&block->rc, __ATOMIC_RELAXED)
                         : block->rc;
        fprintf(stderr, "[mm] release p=%p rc=%d\n", p, rc);
    }
    if (cheng_mm_atomic_enabled()) {
        if (next_rc <= 0) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            cheng_mem_unlink(block);
            block->rc = 0;
            cheng_ptrmap_del(p);
            free(block);
            cheng_mm_free_total += 1;
            if (cheng_mm_live_total > 0) {
                cheng_mm_live_total -= 1;
            }
        }
    } else if (block->rc <= 0) {
        cheng_mem_unlink(block);
        block->rc = 0;
        cheng_ptrmap_del(p);
        free(block);
        cheng_mm_free_total += 1;
        if (cheng_mm_live_total > 0) {
            cheng_mm_live_total -= 1;
        }
    }
}

int32_t cheng_mem_refcount(void* p) {
    if (cheng_mm_is_disabled()) {
        return 0;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return 0;
    }
    if (cheng_mm_atomic_enabled()) {
        return __atomic_load_n(&block->rc, __ATOMIC_RELAXED);
    }
    return block->rc;
}

void cheng_mem_retain_atomic(void* p) {
    if (cheng_mm_is_disabled()) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    (void)__atomic_fetch_add(&block->rc, 1, __ATOMIC_RELAXED);
    cheng_mm_retain_total += 1;
    if (cheng_mm_diag_enabled()) {
        int32_t rc = __atomic_load_n(&block->rc, __ATOMIC_RELAXED);
        fprintf(stderr, "[mm] retain_atomic p=%p rc=%d\n", p, rc);
    }
}

void cheng_mem_release_atomic(void* p) {
    if (cheng_mm_is_disabled()) {
        return;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return;
    }
    int32_t prev = __atomic_fetch_sub(&block->rc, 1, __ATOMIC_RELEASE);
    cheng_mm_release_total += 1;
    if (cheng_mm_diag_enabled()) {
        int32_t rc = prev - 1;
        fprintf(stderr, "[mm] release_atomic p=%p rc=%d\n", p, rc);
    }
    if (prev <= 1) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        cheng_mem_unlink(block);
        block->rc = 0;
        cheng_ptrmap_del(p);
        free(block);
        cheng_mm_free_total += 1;
        if (cheng_mm_live_total > 0) {
            cheng_mm_live_total -= 1;
        }
    }
}

int32_t cheng_mem_refcount_atomic(void* p) {
    if (cheng_mm_is_disabled()) {
        return 0;
    }
    ChengMemBlock* block = cheng_mem_find_block_any(p);
    if (!block) {
        return 0;
    }
    return __atomic_load_n(&block->rc, __ATOMIC_RELAXED);
}

WEAK void memRetain(void* p) { cheng_mem_retain(p); }
WEAK void memRelease(void* p) { cheng_mem_release(p); }
WEAK int32_t memRefcount(void* p) { return cheng_mem_refcount(p); }
WEAK void memScopeEscape(void* p) { cheng_mem_scope_escape(p); }
WEAK void memScopeEscapeGlobal(void* p) { cheng_mem_scope_escape_global(p); }
WEAK void memRetainAtomic(void* p) { cheng_mem_retain_atomic(p); }
WEAK void memReleaseAtomic(void* p) { cheng_mem_release_atomic(p); }
WEAK int32_t memRefcountAtomic(void* p) { return cheng_mem_refcount_atomic(p); }

int32_t cheng_atomic_cas_i32(int32_t* p, int32_t expect, int32_t desired) {
    if (!p) {
        return 0;
    }
    return __atomic_compare_exchange_n(p, &expect, desired, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 1 : 0;
}

void cheng_atomic_store_i32(int32_t* p, int32_t val) {
    if (!p) {
        return;
    }
    __atomic_store_n(p, val, __ATOMIC_RELEASE);
}

int32_t cheng_atomic_load_i32(int32_t* p) {
    if (!p) {
        return 0;
    }
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

int64_t cheng_mm_retain_count(void) { return cheng_mm_retain_total; }
int64_t cheng_mm_release_count(void) { return cheng_mm_release_total; }
int64_t cheng_mm_alloc_count(void) { return cheng_mm_alloc_total; }
int64_t cheng_mm_free_count(void) { return cheng_mm_free_total; }
int64_t cheng_mm_live_count(void) { return cheng_mm_live_total; }
void cheng_mm_diag_reset(void) {
    cheng_mm_retain_total = 0;
    cheng_mm_release_total = 0;
}

WEAK int64_t memRetainCount(void) { return cheng_mm_retain_count(); }
WEAK int64_t memReleaseCount(void) { return cheng_mm_release_count(); }
WEAK int64_t mmAllocCount(void) { return cheng_mm_alloc_count(); }
WEAK int64_t mmFreeCount(void) { return cheng_mm_free_count(); }
WEAK int64_t mmLiveCount(void) { return cheng_mm_live_count(); }
WEAK void mmDiagReset(void) { cheng_mm_diag_reset(); }

void* cheng_realloc(void* p, int32_t size) {
    if (!p) {
        return cheng_malloc(size);
    }
    if (size <= 0) {
        size = 1;
    }
    ChengMemBlock* block = ((ChengMemBlock*)p) - 1;
    // Copy-on-write for shared blocks (rc>1). In-place `realloc` would invalidate
    // other aliases and can lead to heap corruption when sequences are copied
    // and later grown.
    if (!cheng_mm_is_disabled()) {
        int32_t rc = cheng_mm_atomic_enabled() ? __atomic_load_n(&block->rc, __ATOMIC_RELAXED) : block->rc;
        if (rc > 1) {
            size_t total_new = sizeof(ChengMemBlock) + (size_t)size;
            ChengMemBlock* fresh = (ChengMemBlock*)malloc(total_new);
            if (!fresh) {
                return NULL;
            }
            fresh->size = (size_t)size;
            fresh->rc = 1;
            ChengMemScope* scope = block->scope ? block->scope : cheng_mem_current();
            cheng_mem_link(scope, fresh);
            void* out = (void*)(fresh + 1);
            size_t copy = block->size;
            if (copy > (size_t)size) {
                copy = (size_t)size;
            }
            if (copy > 0) {
                memcpy(out, p, copy);
            }
            cheng_ptrmap_put(out, fresh);
            if (cheng_mm_atomic_enabled()) {
                (void)__atomic_fetch_sub(&block->rc, 1, __ATOMIC_RELEASE);
            } else {
                if (block->rc > 0) {
                    block->rc -= 1;
                }
            }
            cheng_mm_alloc_total += 1;
            cheng_mm_live_total += 1;
            return out;
        }
    }
    size_t total = sizeof(ChengMemBlock) + (size_t)size;
    ChengMemBlock* resized = (ChengMemBlock*)realloc(block, total);
    if (!resized) {
        return NULL;
    }
    resized->size = (size_t)size;
    if (resized != block) {
        ChengMemScope* scope = resized->scope;
        if (scope) {
            if (resized->prev) {
                resized->prev->next = resized;
            } else if (scope->head == block) {
                scope->head = resized;
            }
            if (resized->next) {
                resized->next->prev = resized;
            }
        }
    }
    void* out = (void*)(resized + 1);
    if (out != p) {
        cheng_ptrmap_del(p);
        cheng_ptrmap_put(out, resized);
    }
    return out;
}
#if defined(__APPLE__) && UINTPTR_MAX > 0xffffffffu
typedef struct {
    mach_vm_address_t addr;
    mach_vm_size_t size;
    int valid;
} cheng_cstr_region_cache_entry;

static cheng_cstr_region_cache_entry cheng_cstr_region_cache[16];

static bool cheng_find_cached_region(uintptr_t raw, mach_vm_address_t* out_addr, mach_vm_size_t* out_size) {
    size_t slot = (size_t)((raw >> 12) & 15u);
    cheng_cstr_region_cache_entry* entry = &cheng_cstr_region_cache[slot];
    if (!entry->valid) {
        return false;
    }
    mach_vm_address_t start = entry->addr;
    mach_vm_address_t end = entry->addr + entry->size;
    if ((mach_vm_address_t)raw < start || (mach_vm_address_t)raw >= end) {
        return false;
    }
    if (out_addr != NULL) *out_addr = entry->addr;
    if (out_size != NULL) *out_size = entry->size;
    return true;
}

static void cheng_store_cached_region(uintptr_t raw, mach_vm_address_t addr, mach_vm_size_t size) {
    size_t slot = (size_t)((raw >> 12) & 15u);
    cheng_cstr_region_cache[slot].addr = addr;
    cheng_cstr_region_cache[slot].size = size;
    cheng_cstr_region_cache[slot].valid = 1;
}
#endif

static bool cheng_safe_cstr_view(const char* s, const char** out_ptr, size_t* out_len) {
    if (out_ptr != NULL) *out_ptr = "";
    if (out_len != NULL) *out_len = 0u;
    if (s == NULL) {
        return true;
    }
    uintptr_t raw = (uintptr_t)s;
#if defined(__APPLE__) && UINTPTR_MAX > 0xffffffffu
    bool in_image_or_stack =
        raw >= (uintptr_t)0x0000000100000000ULL &&
        raw <  (uintptr_t)0x0000000200000000ULL;
    bool in_malloc_zone =
        raw >= (uintptr_t)0x0000600000000000ULL &&
        raw <  (uintptr_t)0x0000700000000000ULL;
    if (!in_image_or_stack && !in_malloc_zone) {
        return false;
    }
    mach_vm_address_t region_addr = 0;
    mach_vm_size_t region_size = 0;
    if (!cheng_find_cached_region(raw, &region_addr, &region_size)) {
        region_addr = (mach_vm_address_t)raw;
        region_size = 0;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        vm_region_basic_info_data_64_t info;
        mach_port_t object_name = MACH_PORT_NULL;
        kern_return_t kr = mach_vm_region(mach_task_self(),
                                          &region_addr,
                                          &region_size,
                                          VM_REGION_BASIC_INFO_64,
                                          (vm_region_info_t)&info,
                                          &count,
                                          &object_name);
        if (kr != KERN_SUCCESS) {
            return false;
        }
        if ((mach_vm_address_t)raw < region_addr || (mach_vm_address_t)raw >= region_addr + region_size) {
            return false;
        }
        if ((info.protection & VM_PROT_READ) == 0) {
            return false;
        }
        cheng_store_cached_region(raw, region_addr, region_size);
    }
    size_t span = (size_t)((region_addr + region_size) - (mach_vm_address_t)raw);
    size_t limit = span;
    if (limit > (size_t)(1 << 20)) {
        limit = (size_t)(1 << 20);
    }
    for (size_t i = 0; i < limit; ++i) {
        if (s[i] == '\0') {
            if (out_ptr != NULL) *out_ptr = s;
            if (out_len != NULL) *out_len = i;
            return true;
        }
    }
    return false;
#endif
#if defined(__ANDROID__) && UINTPTR_MAX > 0xffffffffu
    if ((raw & (uintptr_t)0x1U) != (uintptr_t)0U) {
        raw -= (uintptr_t)0x1U;
    }
    /* Android crash guard: reject clearly invalid tagged/non-canonical pointers. */
    uint16_t hi16 = (uint16_t)(raw >> 48);
    bool hi_ok =
        hi16 == (uint16_t)0x0000U ||
        hi16 == (uint16_t)0xb400U ||
        hi16 == (uint16_t)0xb500U ||
        hi16 == (uint16_t)0xb600U ||
        hi16 == (uint16_t)0xb700U;
    bool suspicious_low_region = raw < (uintptr_t)0x7b10000000ULL;
    bool suspicious_page_base = (raw & (uintptr_t)0xffffffffULL) == (uintptr_t)0x00000000ULL;
    if (!hi_ok || suspicious_low_region || suspicious_page_base) {
        return false;
    }
#endif
    if (out_ptr != NULL) *out_ptr = (const char*)raw;
    if (out_len != NULL) *out_len = strlen((const char*)raw);
    return true;
}

static const char* cheng_safe_cstr(const char* s) {
    const char* out = "";
    if (!cheng_safe_cstr_view(s, &out, NULL)) {
        return "";
    }
    return out;
}

int32_t cheng_strlen(char* s) {
    size_t n = 0;
    if (!cheng_safe_cstr_view((const char*)s, NULL, &n)) {
        return 0;
    }
    return (n > (size_t)INT32_MAX) ? INT32_MAX : (int32_t)n;
}
char* cheng_str_concat(char* a, char* b) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_safe_cstr_view((const char*)a, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_cstr_view((const char*)b, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    size_t total = la + lb;
    if (total > (size_t)INT32_MAX - 1) {
        total = (size_t)INT32_MAX - 1;
    }
    char* out = (char*)cheng_malloc((int32_t)total + 1);
    if (!out) {
        return NULL;
    }
    if (la > 0) {
        memcpy(out, sa, la);
    }
    if (lb > 0) {
        memcpy(out + la, sb, lb);
    }
    out[total] = '\0';
    return out;
}
WEAK char* __cheng_str_concat(char* a, char* b) { return cheng_str_concat(a, b); }
WEAK char* __cheng_sym_2b(char* a, char* b) { return cheng_str_concat(a, b); }
void* cheng_memcpy(void* dest, void* src, int64_t n) { return memcpy(dest, src, (size_t)n); }
void* cheng_memset(void* dest, int32_t val, int64_t n) { return memset(dest, val, (size_t)n); }
void* cheng_memcpy_ffi(void* dest, void* src, int64_t n) { return cheng_memcpy(dest, src, n); }
void* cheng_memset_ffi(void* dest, int32_t val, int64_t n) { return cheng_memset(dest, val, n); }
__attribute__((weak)) void* alloc(int32_t size) { return cheng_malloc(size); }
__attribute__((weak)) void dealloc(void* p) { cheng_free(p); }
__attribute__((weak)) void copyMem(void* dest, void* src, int32_t size) { (void)cheng_memcpy(dest, src, (int64_t)size); }
__attribute__((weak)) void setMem(void* dest, int32_t val, int32_t size) { (void)cheng_memset(dest, val, (int64_t)size); }
int32_t cheng_memcmp(void* a, void* b, int64_t n) { return memcmp(a, b, (size_t)n); }
int32_t cheng_strcmp(const char* a, const char* b) {
    const char* sa = "";
    const char* sb = "";
    size_t la = 0;
    size_t lb = 0;
    if (!cheng_safe_cstr_view(a, &sa, &la)) {
        sa = "";
        la = 0;
    }
    if (!cheng_safe_cstr_view(b, &sb, &lb)) {
        sb = "";
        lb = 0;
    }
    size_t n = la < lb ? la : lb;
    int cmp = 0;
    if (n > 0) {
        cmp = memcmp(sa, sb, n);
    }
    if (cmp != 0) {
        return cmp;
    }
    if (la < lb) return -1;
    if (la > lb) return 1;
    return 0;
}
double cheng_bits_to_f32(int32_t bits) {
    union {
        uint32_t u;
        float f;
    } v;
    v.u = (uint32_t)bits;
    return (double)v.f;
}
int32_t cheng_f32_to_bits(double value) {
    union {
        uint32_t u;
        float f;
    } v;
    v.f = (float)value;
    return (int32_t)v.u;
}
int64_t cheng_f64_to_bits(double value) {
    union {
        uint64_t u;
        double f;
    } v;
    v.f = value;
    return (int64_t)v.u;
}
int64_t cheng_parse_f64_bits(const char* s) {
    if (!s) s = "0";
    char* end = NULL;
    double v = strtod(s, &end);
    (void)end;
    return cheng_f64_to_bits(v);
}
double cheng_bits_to_f64(int64_t bits) {
    union {
        uint64_t u;
        double f;
    } v;
    v.u = (uint64_t)bits;
    return v.f;
}
int64_t cheng_f64_add_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return cheng_f64_to_bits(a + b);
}
int64_t cheng_f64_sub_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return cheng_f64_to_bits(a - b);
}
int64_t cheng_f64_mul_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return cheng_f64_to_bits(a * b);
}
int64_t cheng_f64_div_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return cheng_f64_to_bits(a / b);
}
int64_t cheng_f64_neg_bits(int64_t a_bits) {
    double a = cheng_bits_to_f64(a_bits);
    return cheng_f64_to_bits(-a);
}
int64_t cheng_f64_lt_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return (a < b) ? 1 : 0;
}
int64_t cheng_f64_le_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return (a <= b) ? 1 : 0;
}
int64_t cheng_f64_gt_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return (a > b) ? 1 : 0;
}
int64_t cheng_f64_ge_bits(int64_t a_bits, int64_t b_bits) {
    double a = cheng_bits_to_f64(a_bits);
    double b = cheng_bits_to_f64(b_bits);
    return (a >= b) ? 1 : 0;
}
int64_t cheng_i64_to_f64_bits(int64_t x) {
    return cheng_f64_to_bits((double)x);
}
int64_t cheng_u64_to_f64_bits(uint64_t x) {
    return cheng_f64_to_bits((double)x);
}
int64_t cheng_f64_bits_to_i64(int64_t bits) {
    return (int64_t)cheng_bits_to_f64(bits);
}
uint64_t cheng_f64_bits_to_u64(int64_t bits) {
    return (uint64_t)cheng_bits_to_f64(bits);
}
int64_t cheng_f32_bits_to_f64_bits(int32_t bits) {
    return cheng_f64_to_bits(cheng_bits_to_f32(bits));
}
int32_t cheng_f64_bits_to_f32_bits(int64_t bits) {
    return cheng_f32_to_bits(cheng_bits_to_f64(bits));
}
int64_t cheng_f32_bits_to_i64(int32_t bits) {
    return (int64_t)cheng_bits_to_f32(bits);
}
uint64_t cheng_f32_bits_to_u64(int32_t bits) {
    return (uint64_t)cheng_bits_to_f32(bits);
}
int32_t cheng_jpeg_decode(const uint8_t* data, int32_t len, int32_t* out_w, int32_t* out_h, uint8_t** out_rgba) {
    if (!data || len <= 0 || !out_w || !out_h || !out_rgba) return 0;
    int w = 0, h = 0, comp = 0;
    unsigned char* rgba = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
    if (!rgba) return 0;
    *out_w = (int32_t)w;
    *out_h = (int32_t)h;
    *out_rgba = rgba;
    return 1;
}
void cheng_jpeg_free(void* p) {
    if (p) {
        stbi_image_free(p);
    }
}

void* cheng_fopen(const char* filename, const char* mode) {
    return (void*)fopen(filename, mode);
}

static FILE* cheng_safe_stream(void* stream) {
    if (stream == (void*)stdout || stream == (void*)stderr || stream == (void*)stdin) {
        return (FILE*)stream;
    }
    if (stream == NULL) return stdout;
    uintptr_t v = (uintptr_t)stream;
    if (v < 0x100000000ull) {
        return stdout;
    }
    return (FILE*)stream;
}

int32_t cheng_fclose(void* f) {
    FILE* stream = cheng_safe_stream(f);
    if (stream == stdout || stream == stderr || stream == stdin) return 0;
    return fclose(stream);
}
int32_t cheng_fread(void* ptr, int64_t size, int64_t n, void* stream) {
    FILE* f = cheng_safe_stream(stream);
    return (int32_t)fread(ptr, (size_t)size, (size_t)n, f);
}
int32_t cheng_fwrite(void* ptr, int64_t size, int64_t n, void* stream) {
    FILE* f = cheng_safe_stream(stream);
    return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, f);
}
int32_t cheng_fseek(void* stream, int64_t offset, int32_t whence) {
#if defined(_WIN32)
    return _fseeki64((FILE*)stream, offset, whence);
#else
    return fseeko((FILE*)stream, (off_t)offset, whence);
#endif
}
int64_t cheng_ftell(void* stream) {
#if defined(_WIN32)
    FILE* f = cheng_safe_stream(stream);
    return (int64_t)_ftelli64(f);
#else
    FILE* f = cheng_safe_stream(stream);
    return (int64_t)ftello(f);
#endif
}
int32_t cheng_fflush(void* stream) {
    FILE* f = cheng_safe_stream(stream);
    return fflush(f);
}
int32_t cheng_fgetc(void* stream) {
    FILE* f = cheng_safe_stream(stream);
    return fgetc(f);
}

void* get_stdin() { return (void*)stdin; }
void* get_stdout() { return (void*)stdout; }
void* get_stderr() { return (void*)stderr; }

/*
 * Stage0 compatibility shim:
 * older bootstrap outputs may keep a direct unresolved call to symbol "[]=".
 * Provide a benign fallback body to keep runtime symbol closure deterministic.
 */
int64_t cheng_index_set_compat(void) __asm__("_[]=");
int64_t cheng_index_set_compat(void) { return 0; }

// Backend fallback: provide `__addr` symbol (Mach-O uses leading underscore).
// The backend should lower `__addr(...)` as an intrinsic, but during bootstrap
// we keep this as a safe identity for pointer-sized values.
int64_t __addr(int64_t value) { return value; }
int64_t _addr(int64_t value) { return __addr(value); }

typedef void (*cheng_iometer_hook_t)(int32_t op, int64_t bytes);

void cheng_iometer_call(void* hook, int32_t op, int64_t bytes) {
    if (!hook) return;
    cheng_iometer_hook_t fn = (cheng_iometer_hook_t)hook;
    fn(op, bytes);
}

void* sys_memset(void* dest, int32_t val, int32_t n) {
    return memset(dest, val, n);
}

// ---- Minimal cross-platform std helpers ----

#include <time.h>

#if defined(_WIN32)
  #include <windows.h>
  #include <direct.h>   // _mkdir, _getcwd
  #include <sys/stat.h> // _stat
#else
  #if defined(__APPLE__)
    #include <mach/mach_time.h>
  #endif
  #include <dirent.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>   // getcwd
  #include <sys/syscall.h>
  #include <sys/stat.h> // stat, mkdir
  #include <sys/wait.h>
  #include <sys/ioctl.h>
  #include <poll.h>
#endif

int32_t cheng_file_exists(const char* path) {
    if (!path || !path[0]) return 0;
    FILE* f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

int32_t cheng_dir_exists(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (st.st_mode & _S_IFDIR) ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

int64_t cheng_file_mtime(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (int64_t)st.st_mtime;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int64_t)st.st_mtime;
#endif
}

int64_t cheng_file_size(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (int64_t)st.st_size;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (int64_t)st.st_size;
#endif
}

int32_t cheng_mkdir1(const char* path) {
    if (!path || !path[0]) return -1;
#if defined(_WIN32)
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

char* cheng_getcwd(void) {
    static char buf[4096];
#if defined(_WIN32)
    if (_getcwd(buf, (int)sizeof(buf)) == NULL) return "";
#else
    if (getcwd(buf, sizeof(buf)) == NULL) return "";
#endif
    buf[sizeof(buf) - 1] = 0;
    return buf;
}

double cheng_epoch_time(void) {
    return (double)time(NULL);
}

int64_t cheng_monotime_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) return 0;
    if (!QueryPerformanceCounter(&counter)) return 0;
    return (int64_t)((counter.QuadPart * 1000000000LL) / freq.QuadPart);
#elif defined(__APPLE__)
    static mach_timebase_info_data_t info;
    if (info.denom == 0) {
        (void)mach_timebase_info(&info);
    }
    uint64_t t = mach_absolute_time();
    __uint128_t ns = (__uint128_t)t * (uint64_t)info.numer;
    ns /= (uint64_t)info.denom;
    return (int64_t)ns;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
#endif
}

static char* cheng_strdup(const char* s) {
    if (!s) {
        char* out = (char*)cheng_malloc(1);
        if (out) out[0] = 0;
        return out;
    }
    size_t len = strlen(s);
    char* out = (char*)cheng_malloc((int32_t)len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = 0;
    return out;
}

static int cheng_buf_reserve(char** buf, size_t* cap, size_t need) {
    if (need <= *cap) return 1;
    size_t newCap = (*cap == 0) ? 256 : *cap;
    while (newCap < need) newCap *= 2;
    char* next = (char*)cheng_realloc(*buf, (int32_t)newCap);
    if (!next) return 0;
    *buf = next;
    *cap = newCap;
    return 1;
}

char* cheng_list_dir(const char* path) {
    if (!path || !path[0]) return cheng_strdup("");
    size_t cap = 256;
    size_t len = 0;
    char* out = (char*)cheng_malloc((int32_t)cap);
    if (!out) return NULL;
    out[0] = 0;
#if defined(_WIN32)
    size_t baseLen = strlen(path);
    int needsSep = (baseLen > 0 && path[baseLen - 1] != '\\' && path[baseLen - 1] != '/');
    size_t patLen = baseLen + (needsSep ? 1 : 0) + 2;
    char* pattern = (char*)malloc(patLen);
    if (!pattern) return out;
    size_t pos = 0;
    memcpy(pattern + pos, path, baseLen);
    pos += baseLen;
    if (needsSep) pattern[pos++] = '\\';
    pattern[pos++] = '*';
    pattern[pos] = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const char* name = fd.cFileName;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
        size_t nlen = strlen(name);
        size_t need = len + nlen + 2;
        if (!cheng_buf_reserve(&out, &cap, need)) break;
        memcpy(out + len, name, nlen);
        len += nlen;
        out[len++] = '\n';
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(path);
    if (!dir) return out;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) continue;
        size_t nlen = strlen(name);
        size_t need = len + nlen + 2;
        if (!cheng_buf_reserve(&out, &cap, need)) break;
        memcpy(out + len, name, nlen);
        len += nlen;
        out[len++] = '\n';
    }
    closedir(dir);
#endif
    if (len > 0 && out[len - 1] == '\n') len -= 1;
    if (!cheng_buf_reserve(&out, &cap, len + 1)) return out;
    out[len] = 0;
    return out;
}

static char g_cheng_empty_str[1] = {0};
static char* g_cheng_read_file_buf = NULL;
static size_t g_cheng_read_file_cap = 0;

static int cheng_read_file_reserve(size_t need) {
    if (need <= g_cheng_read_file_cap) return 1;
    size_t newCap = (g_cheng_read_file_cap == 0) ? 256 : g_cheng_read_file_cap;
    while (newCap < need) newCap *= 2;
    char* next = (char*)realloc(g_cheng_read_file_buf, newCap);
    if (!next) return 0;
    g_cheng_read_file_buf = next;
    g_cheng_read_file_cap = newCap;
    return 1;
}

char* cheng_read_file(const char* path) {
    if (!path || !path[0]) return g_cheng_empty_str;
    FILE* f = fopen(path, "rb");
    if (!f) return g_cheng_empty_str;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return g_cheng_empty_str;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return g_cheng_empty_str;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return g_cheng_empty_str;
    }
    size_t need = (size_t)size + 1;
    // Keep this buffer outside Cheng-managed memory: the returned pointer is treated as `str`
    // by the caller and may be released by ORC. Using malloc/realloc avoids dangling pointers
    // when the caller releases the temporary string.
    if (!cheng_read_file_reserve(need)) {
        fclose(f);
        return g_cheng_empty_str;
    }
    size_t n = fread(g_cheng_read_file_buf, 1, (size_t)size, f);
    g_cheng_read_file_buf[n] = 0;
    fclose(f);
    return g_cheng_read_file_buf;
}

int32_t cheng_write_file(const char* path, const char* content) {
    if (!path || !path[0]) return 0;
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    if (!content) content = "";
    size_t len = strlen(content);
    size_t n = 0;
    if (len > 0) {
        n = fwrite(content, 1, len, f);
    }
    fclose(f);
    return (n == len) ? 1 : 0;
}

char* cheng_exec_cmd_ex(const char* command, const char* workingDir, int32_t mergeStderr, int64_t* exitCode) {
    if (exitCode) *exitCode = -1;
    if (!command || !command[0]) return cheng_strdup("");
    const char* suffix = mergeStderr ? " 2>&1" : "";
    size_t cmdLen = strlen(command);
    size_t sufLen = strlen(suffix);
    char* cmd = (char*)malloc(cmdLen + sufLen + 1);
    if (!cmd) return cheng_strdup("");
    memcpy(cmd, command, cmdLen);
    memcpy(cmd + cmdLen, suffix, sufLen);
    cmd[cmdLen + sufLen] = 0;

    char oldCwd[4096];
    int hasOld = 0;
#if defined(_WIN32)
    if (workingDir && workingDir[0]) {
        if (_getcwd(oldCwd, (int)sizeof(oldCwd)) != NULL) hasOld = 1;
        _chdir(workingDir);
    }
    FILE* pipe = _popen(cmd, "r");
#else
    if (workingDir && workingDir[0]) {
        if (getcwd(oldCwd, sizeof(oldCwd)) != NULL) hasOld = 1;
        chdir(workingDir);
    }
    FILE* pipe = popen(cmd, "r");
#endif
    free(cmd);
    if (!pipe) {
        if (hasOld) {
#if defined(_WIN32)
            _chdir(oldCwd);
#else
            chdir(oldCwd);
#endif
        }
        return cheng_strdup("");
    }

    size_t cap = 1024;
    size_t len = 0;
    char* out = (char*)cheng_malloc((int32_t)cap);
    if (!out) {
        out = cheng_strdup("");
        cap = out ? 1 : 0;
    }
    char buffer[4096];
    while (out && !feof(pipe)) {
        size_t n = fread(buffer, 1, sizeof(buffer), pipe);
        if (n == 0) break;
        if (!cheng_buf_reserve(&out, &cap, len + n + 1)) break;
        memcpy(out + len, buffer, n);
        len += n;
    }
    if (out) {
        if (!cheng_buf_reserve(&out, &cap, len + 1)) {
            if (cap > len) out[len] = 0;
        } else {
            out[len] = 0;
        }
    }

#if defined(_WIN32)
    int status = _pclose(pipe);
    if (exitCode) *exitCode = (int64_t)status;
    if (hasOld) _chdir(oldCwd);
#else
    int status = pclose(pipe);
    if (exitCode) {
        if (WIFEXITED(status)) *exitCode = (int64_t)WEXITSTATUS(status);
        else *exitCode = (int64_t)status;
    }
    if (hasOld) chdir(oldCwd);
#endif
    if (!out) return cheng_strdup("");
    return out;
}

char* chengQ_execQ_cmdQ_ex_0(char* command, char* workingDir, int32_t mergeStderr, int64_t* exitCode) {
    return cheng_exec_cmd_ex(command, workingDir, mergeStderr, exitCode);
}

int32_t cheng_pty_is_supported(void) {
#if defined(_WIN32)
    return 0;
#else
    return 1;
#endif
}

int32_t cheng_pty_spawn(const char* command, const char* workingDir, int32_t* outMasterFd, int64_t* outPid) {
#if defined(_WIN32)
    if (outMasterFd) *outMasterFd = -1;
    if (outPid) *outPid = -1;
    return 0;
#else
    if (outMasterFd) *outMasterFd = -1;
    if (outPid) *outPid = -1;
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) return 0;
    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        close(master_fd);
        return 0;
    }
    char* slave_name = ptsname(master_fd);
    if (!slave_name) {
        close(master_fd);
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(master_fd);
        return 0;
    }
    if (pid == 0) {
        setsid();
        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) _exit(127);
#ifdef TIOCSCTTY
        ioctl(slave_fd, TIOCSCTTY, 0);
#endif
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        if (slave_fd > STDERR_FILENO) close(slave_fd);
        if (workingDir && workingDir[0]) chdir(workingDir);
        if (command && command[0]) {
            execl("/bin/sh", "sh", "-lc", command, (char*)NULL);
        } else {
            const char* shell = getenv("SHELL");
            if (!shell || !shell[0]) shell = "/bin/sh";
            execl(shell, shell, "-l", (char*)NULL);
        }
        _exit(127);
    }
    int flags = fcntl(master_fd, F_GETFL);
    if (flags != -1) {
        fcntl(master_fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    if (outMasterFd) *outMasterFd = master_fd;
    if (outPid) *outPid = (int64_t)pid;
    return 1;
#endif
}

int32_t cheng_pipe_spawn(const char* command, const char* workingDir, int32_t* outReadFd, int32_t* outWriteFd, int64_t* outPid) {
#if defined(_WIN32)
    if (outReadFd) *outReadFd = -1;
    if (outWriteFd) *outWriteFd = -1;
    if (outPid) *outPid = -1;
    return 0;
#else
    if (outReadFd) *outReadFd = -1;
    if (outWriteFd) *outWriteFd = -1;
    if (outPid) *outPid = -1;
    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) != 0) return 0;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return 0;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return 0;
    }
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (workingDir && workingDir[0]) chdir(workingDir);
        if (command && command[0]) {
            execl("/bin/sh", "sh", "-lc", command, (char*)NULL);
        } else {
            const char* shell = getenv("SHELL");
            if (!shell || !shell[0]) shell = "/bin/sh";
            execl(shell, shell, "-l", (char*)NULL);
        }
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    int flags = fcntl(out_pipe[0], F_GETFL);
    if (flags != -1) {
        fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);
    }
    flags = fcntl(in_pipe[1], F_GETFL);
    if (flags != -1) {
        fcntl(in_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }
    if (outReadFd) *outReadFd = out_pipe[0];
    if (outWriteFd) *outWriteFd = in_pipe[1];
    if (outPid) *outPid = (int64_t)pid;
    return 1;
#endif
}

char* cheng_pty_read(int32_t fd, int32_t maxBytes, int32_t* outEof) {
#if defined(_WIN32)
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#else
    if (outEof) *outEof = 0;
    if (fd < 0 || maxBytes <= 0) return cheng_strdup("");
    if (maxBytes > 65536) maxBytes = 65536;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, 0);
    if (rc <= 0 || (pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return cheng_strdup("");
    }
    char* buf = (char*)cheng_malloc(maxBytes + 1);
    if (!buf) return cheng_strdup("");
    ssize_t n = read(fd, buf, (size_t)maxBytes);
    if (n > 0) {
        buf[n] = 0;
        return buf;
    }
    cheng_free(buf);
    if (n == 0) {
        if (outEof) *outEof = 1;
        return cheng_strdup("");
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return cheng_strdup("");
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#endif
}

char* cheng_fd_read(int32_t fd, int32_t maxBytes, int32_t* outEof) {
#if defined(_WIN32)
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#else
    if (outEof) *outEof = 0;
    if (fd < 0 || maxBytes <= 0) return cheng_strdup("");
    if (maxBytes > 65536) maxBytes = 65536;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, 0);
    if (rc <= 0 || (pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return cheng_strdup("");
    }
    char* buf = (char*)cheng_malloc(maxBytes + 1);
    if (!buf) return cheng_strdup("");
    ssize_t n = read(fd, buf, (size_t)maxBytes);
    if (n > 0) {
        buf[n] = 0;
        return buf;
    }
    cheng_free(buf);
    if (n == 0) {
        if (outEof) *outEof = 1;
        return cheng_strdup("");
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return cheng_strdup("");
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#endif
}

char* cheng_fd_read_wait(int32_t fd, int32_t maxBytes, int32_t timeoutMs, int32_t* outEof) {
#if defined(_WIN32)
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#else
    if (outEof) *outEof = 0;
    if (fd < 0 || maxBytes <= 0) return cheng_strdup("");
    if (maxBytes > 65536) maxBytes = 65536;
    if (timeoutMs < 0) timeoutMs = 0;
    if (timeoutMs > 50) timeoutMs = 50;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int rc = poll(&pfd, 1, timeoutMs);
    if (rc <= 0 || (pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return cheng_strdup("");
    }
    char* buf = (char*)cheng_malloc(maxBytes + 1);
    if (!buf) return cheng_strdup("");
    ssize_t n = read(fd, buf, (size_t)maxBytes);
    if (n > 0) {
        buf[n] = 0;
        return buf;
    }
    cheng_free(buf);
    if (n == 0) {
        if (outEof) *outEof = 1;
        return cheng_strdup("");
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) return cheng_strdup("");
    if (outEof) *outEof = 1;
    return cheng_strdup("");
#endif
}

int32_t cheng_pty_write(int32_t fd, const char* data, int32_t len) {
#if defined(_WIN32)
    return -1;
#else
    if (fd < 0 || !data || len <= 0) return 0;
    ssize_t n = write(fd, data, (size_t)len);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int rc = poll(&pfd, 1, 100);
        if (rc > 0 && (pfd.revents & POLLOUT)) {
            n = write(fd, data, (size_t)len);
        }
    }
    if (n < 0) {
        static int pty_write_errs = 0;
        if (pty_write_errs < 8) {
            fprintf(stderr, "[pty] write failed fd=%d errno=%d (%s)\n", fd, errno, strerror(errno));
            pty_write_errs++;
        }
        return -1;
    }
    return (int32_t)n;
#endif
}

int32_t cheng_fd_write(int32_t fd, const char* data, int32_t len) {
#if defined(_WIN32)
    (void)fd;
    (void)data;
    (void)len;
    return -1;
#else
    if (fd < 0 || data == NULL || len <= 0) return 0;
    ssize_t n = (ssize_t)syscall(SYS_write, fd, data, (size_t)len);
    if (n < 0) return -1;
    return (int32_t)n;
#endif
}

int32_t cheng_pty_close(int32_t fd) {
#if defined(_WIN32)
    return -1;
#else
    if (fd < 0) return 0;
    return close(fd);
#endif
}

int32_t cheng_pty_wait(int64_t pid, int32_t* outExitCode) {
#if defined(_WIN32)
    if (outExitCode) *outExitCode = -1;
    return -1;
#else
    if (outExitCode) *outExitCode = -1;
    if (pid <= 0) return -1;
    int status = 0;
    pid_t rc = waitpid((pid_t)pid, &status, WNOHANG);
    if (rc == 0) return 0;
    if (rc < 0) return -1;
    if (outExitCode) {
        if (WIFEXITED(status)) *outExitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *outExitCode = 128 + WTERMSIG(status);
        else *outExitCode = status;
    }
    return 1;
#endif
}

int32_t cheng_tcp_listener(int32_t port, int32_t* outPort) {
#if defined(_WIN32)
    if (outPort) *outPort = -1;
    return -1;
#else
    if (getenv("CODEX_LOGIN_DEBUG")) {
        fprintf(stderr, "[login] tcp_listener port=%d\n", port);
    }
    if (outPort) *outPort = 0;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, (socklen_t)sizeof(yes)) != 0) {
        if (getenv("CODEX_LOGIN_DEBUG")) {
            fprintf(stderr, "[login] setsockopt SO_REUSEADDR failed errno=%d (%s)\n", errno, strerror(errno));
        }
    }
#ifdef SO_REUSEPORT
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, (socklen_t)sizeof(yes)) != 0) {
        if (getenv("CODEX_LOGIN_DEBUG")) {
            fprintf(stderr, "[login] setsockopt SO_REUSEPORT failed errno=%d (%s)\n", errno, strerror(errno));
        }
    }
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
#if defined(__APPLE__)
    addr.sin_len = (uint8_t)sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr)) != 0) {
        if (getenv("CODEX_LOGIN_DEBUG")) {
            fprintf(stderr, "[login] bind loopback failed errno=%d (%s)\n", errno, strerror(errno));
        }
        if (getenv("CODEX_LOGIN_ALLOW_ANY")) {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(fd, (struct sockaddr*)&addr, (socklen_t)sizeof(addr)) != 0) {
                if (getenv("CODEX_LOGIN_DEBUG")) {
                    fprintf(stderr, "[login] bind any failed errno=%d (%s)\n", errno, strerror(errno));
                }
                close(fd);
                return -1;
            }
        } else {
            close(fd);
            return -1;
        }
    }
    if (listen(fd, 64) != 0) {
        if (getenv("CODEX_LOGIN_DEBUG")) {
            fprintf(stderr, "[login] listen failed errno=%d (%s)\n", errno, strerror(errno));
        }
        close(fd);
        return -1;
    }
    if (outPort) {
        struct sockaddr_in bound;
        socklen_t len = (socklen_t)sizeof(bound);
        memset(&bound, 0, sizeof(bound));
        if (getsockname(fd, (struct sockaddr*)&bound, &len) == 0) {
            *outPort = (int32_t)ntohs(bound.sin_port);
        }
    }
    return fd;
#endif
}

int32_t cheng_errno(void) {
    return errno;
}

const char* cheng_strerror(int32_t err) {
    return strerror(err);
}

int64_t cheng_abi_sum9_i64(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                           int64_t f, int64_t g, int64_t h, int64_t i) {
    return a + b + c + d + e + f + g + h + i;
}

int32_t cheng_abi_sum9_i32(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
                           int32_t f, int32_t g, int32_t h, int32_t i) {
    return a + b + c + d + e + f + g + h + i;
}

int64_t cheng_abi_sum16_i64(int64_t a, int64_t b, int64_t c, int64_t d,
                            int64_t e, int64_t f, int64_t g, int64_t h,
                            int64_t i, int64_t j, int64_t k, int64_t l,
                            int64_t m, int64_t n, int64_t o, int64_t p) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
}

int32_t cheng_abi_sum16_i32(int32_t a, int32_t b, int32_t c, int32_t d,
                            int32_t e, int32_t f, int32_t g, int32_t h,
                            int32_t i, int32_t j, int32_t k, int32_t l,
                            int32_t m, int32_t n, int32_t o, int32_t p) {
    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p;
}

int64_t cheng_abi_mix_i32_i64(int32_t a, int64_t b, int32_t c, int64_t d) {
    return (int64_t)a + b + (int64_t)c + d;
}

typedef struct ChengAbiSeqI32View {
    int32_t len;
    int32_t cap;
    int32_t* buffer;
} ChengAbiSeqI32View;

static int32_t cheng_abi_sum_seq_i32_raw(const int32_t* ptr, int32_t len) {
    if (ptr == NULL || len <= 0) {
        return 0;
    }
    int64_t sum = 0;
    for (int32_t i = 0; i < len; i += 1) {
        sum += (int64_t)ptr[i];
    }
    return (int32_t)sum;
}

int32_t cheng_abi_sum_seq_i32(uint64_t seqLike0, uint64_t seqLike1, uint64_t seqLike2) {
    /*
     * RPSPAR-03 slice bridge helper:
     * - pointer mode: arg0 is pointer to seq header (len/cap/buffer)
     * - packed mode:  arg0 packs len/cap (low/high 32b), arg1 is data pointer
     * - split mode:   arg0=len, arg1=cap, arg2=data pointer
     */
    if (seqLike0 <= (uint64_t)(1u << 20) &&
        seqLike1 <= (uint64_t)(1u << 20) &&
        seqLike2 > (uint64_t)4096) {
        return cheng_abi_sum_seq_i32_raw((const int32_t*)(uintptr_t)seqLike2, (int32_t)seqLike0);
    }
    uint32_t lo = (uint32_t)(seqLike0 & 0xffffffffu);
    uint32_t hi = (uint32_t)((seqLike0 >> 32) & 0xffffffffu);
    if (lo <= hi && hi <= (uint32_t)(1u << 20) && seqLike1 > (uint64_t)4096) {
        return cheng_abi_sum_seq_i32_raw((const int32_t*)(uintptr_t)seqLike1, (int32_t)lo);
    }
    if (seqLike0 == 0) {
        return 0;
    }
    const ChengAbiSeqI32View* seq = (const ChengAbiSeqI32View*)(uintptr_t)seqLike0;
    if (seq->len < 0 || seq->len > (1 << 26)) {
        return 0;
    }
    return cheng_abi_sum_seq_i32_raw((const int32_t*)seq->buffer, seq->len);
}

void cheng_abi_store_i32(int64_t p, int32_t v) {
    if (p == 0) {
        return;
    }
    int32_t* pp = (int32_t*)(uintptr_t)p;
    *pp = v;
}

int32_t cheng_abi_load_i32(int64_t p) {
    if (p == 0) {
        return 0;
    }
    int32_t* pp = (int32_t*)(uintptr_t)p;
    return *pp;
}

typedef int32_t (*ChengAbiCbCtxI32)(int64_t ctx, int32_t a, int32_t b);

int32_t cheng_abi_call_cb_ctx_i32(void* fn_ptr, int64_t ctx, int32_t a, int32_t b) {
    if (!fn_ptr) {
        return 0;
    }
    ChengAbiCbCtxI32 fn = (ChengAbiCbCtxI32)fn_ptr;
    return fn(ctx, a, b);
}

int32_t cheng_abi_varargs_sum_i32(int32_t n, ...) {
    if (n <= 0) {
        return 0;
    }
    va_list ap;
    va_start(ap, n);
    int32_t sum = 0;
    for (int32_t i = 0; i < n; i += 1) {
        sum += (int32_t)va_arg(ap, int);
    }
    va_end(ap);
    return sum;
}

int32_t cheng_abi_varargs_sum10_i32_fixed(int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4,
                                         int32_t a5, int32_t a6, int32_t a7, int32_t a8, int32_t a9) {
    return cheng_abi_varargs_sum_i32(10, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

typedef struct ChengAbiPairI32 {
    int32_t a;
    int32_t b;
} ChengAbiPairI32;

ChengAbiPairI32 cheng_abi_ret_pair_i32(int32_t a, int32_t b) {
    ChengAbiPairI32 out;
    out.a = a;
    out.b = b;
    return out;
}

void cheng_abi_ret_pair_i32_out(int64_t outA, int64_t outB, int32_t a, int32_t b) {
    ChengAbiPairI32 r = cheng_abi_ret_pair_i32(a, b);
    if (outA != 0) {
        int32_t* pa = (int32_t*)(uintptr_t)outA;
        *pa = r.a;
    }
    if (outB != 0) {
        int32_t* pb = (int32_t*)(uintptr_t)outB;
        *pb = r.b;
    }
}

void cheng_abi_out_pair_i32(int64_t outA, int64_t outB, int32_t a, int32_t b) {
    if (outA != 0) {
        int32_t* pa = (int32_t*)(uintptr_t)outA;
        *pa = a;
    }
    if (outB != 0) {
        int32_t* pb = (int32_t*)(uintptr_t)outB;
        *pb = b;
    }
}

void cheng_abi_borrow_mut_pair_i32(int64_t pairPtr, int32_t da, int32_t db) {
    if (pairPtr == 0) {
        return;
    }
    ChengAbiPairI32* pair = (ChengAbiPairI32*)(uintptr_t)pairPtr;
    pair->a += da;
    pair->b += db;
}

typedef struct ChengFfiHandleSlot {
    void* ptr;
    uint32_t generation;
} ChengFfiHandleSlot;

static ChengFfiHandleSlot* cheng_ffi_handle_slots = NULL;
static uint32_t cheng_ffi_handle_slots_len = 0;
static uint32_t cheng_ffi_handle_slots_cap = 0;
static const int32_t cheng_ffi_handle_err_invalid = -1;

static int cheng_ffi_handle_ensure_capacity(uint32_t min_cap) {
    if (cheng_ffi_handle_slots_cap >= min_cap) {
        return 1;
    }
    uint32_t new_cap = cheng_ffi_handle_slots_cap;
    if (new_cap < 16) {
        new_cap = 16;
    }
    while (new_cap < min_cap) {
        if (new_cap > (UINT32_MAX / 2U)) {
            new_cap = min_cap;
            break;
        }
        new_cap *= 2U;
    }
    size_t bytes = (size_t)new_cap * sizeof(ChengFfiHandleSlot);
    if (new_cap != 0U && bytes / sizeof(ChengFfiHandleSlot) != (size_t)new_cap) {
        return 0;
    }
    ChengFfiHandleSlot* grown = (ChengFfiHandleSlot*)realloc(cheng_ffi_handle_slots, bytes);
    if (grown == NULL) {
        return 0;
    }
    for (uint32_t i = cheng_ffi_handle_slots_cap; i < new_cap; i += 1U) {
        grown[i].ptr = NULL;
        grown[i].generation = 1U;
    }
    cheng_ffi_handle_slots = grown;
    cheng_ffi_handle_slots_cap = new_cap;
    return 1;
}

static int cheng_ffi_handle_decode(uint64_t handle, uint32_t* out_idx, uint32_t* out_generation) {
    if (handle == 0ULL) {
        return 0;
    }
    uint32_t low = (uint32_t)(handle & 0xffffffffULL);
    uint32_t generation = (uint32_t)(handle >> 32U);
    if (low == 0U || generation == 0U) {
        return 0;
    }
    uint32_t idx = low - 1U;
    if (idx >= cheng_ffi_handle_slots_len) {
        return 0;
    }
    if (out_idx != NULL) {
        *out_idx = idx;
    }
    if (out_generation != NULL) {
        *out_generation = generation;
    }
    return 1;
}

uint64_t cheng_ffi_handle_register_ptr(void* ptr) {
    if (ptr == NULL) {
        return 0ULL;
    }
    uint32_t idx = UINT32_MAX;
    for (uint32_t i = 0; i < cheng_ffi_handle_slots_len; i += 1U) {
        if (cheng_ffi_handle_slots[i].ptr == NULL) {
            idx = i;
            break;
        }
    }
    if (idx == UINT32_MAX) {
        idx = cheng_ffi_handle_slots_len;
        if (!cheng_ffi_handle_ensure_capacity(idx + 1U)) {
            return 0ULL;
        }
        cheng_ffi_handle_slots_len = idx + 1U;
    }
    ChengFfiHandleSlot* slot = &cheng_ffi_handle_slots[idx];
    slot->ptr = ptr;
    if (slot->generation == 0U) {
        slot->generation = 1U;
    }
    return ((uint64_t)slot->generation << 32U) | (uint64_t)(idx + 1U);
}

void* cheng_ffi_handle_resolve_ptr(uint64_t handle) {
    uint32_t idx = 0;
    uint32_t generation = 0;
    if (!cheng_ffi_handle_decode(handle, &idx, &generation)) {
        return NULL;
    }
    ChengFfiHandleSlot* slot = &cheng_ffi_handle_slots[idx];
    if (slot->ptr == NULL || slot->generation != generation) {
        return NULL;
    }
    return slot->ptr;
}

int32_t cheng_ffi_handle_invalidate(uint64_t handle) {
    uint32_t idx = 0;
    uint32_t generation = 0;
    if (!cheng_ffi_handle_decode(handle, &idx, &generation)) {
        return cheng_ffi_handle_err_invalid;
    }
    ChengFfiHandleSlot* slot = &cheng_ffi_handle_slots[idx];
    if (slot->ptr == NULL || slot->generation != generation) {
        return cheng_ffi_handle_err_invalid;
    }
    slot->ptr = NULL;
    if (slot->generation == UINT32_MAX) {
        slot->generation = 1U;
    } else {
        slot->generation += 1U;
    }
    return 0;
}

uint64_t cheng_ffi_handle_new_i32(int32_t value) {
    int32_t* cell = (int32_t*)malloc(sizeof(int32_t));
    if (cell == NULL) {
        return 0ULL;
    }
    *cell = value;
    uint64_t handle = cheng_ffi_handle_register_ptr((void*)cell);
    if (handle == 0ULL) {
        free(cell);
    }
    return handle;
}

int32_t cheng_ffi_handle_get_i32(uint64_t handle, int32_t* out_value) {
    if (out_value == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    *out_value = *cell;
    return 0;
}

int32_t cheng_ffi_handle_add_i32(uint64_t handle, int32_t delta, int32_t* out_value) {
    if (out_value == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    *cell += delta;
    *out_value = *cell;
    return 0;
}

int32_t cheng_ffi_handle_release_i32(uint64_t handle) {
    int32_t* cell = (int32_t*)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return cheng_ffi_handle_err_invalid;
    }
    if (cheng_ffi_handle_invalidate(handle) != 0) {
        return cheng_ffi_handle_err_invalid;
    }
    free(cell);
    return 0;
}
