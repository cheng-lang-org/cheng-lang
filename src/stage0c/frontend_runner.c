#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "core/cheng_core.h"

static int path_is_exec(const char *path) {
    return path != NULL && access(path, X_OK) == 0;
}

static int path_is_file(const char *path) {
    return path != NULL && access(path, R_OK) == 0;
}

static int file_mtime(const char *path, time_t *out) {
    struct stat st;
    if (path == NULL || out == NULL) {
        return -1;
    }
    if (stat(path, &st) != 0) {
        return -1;
    }
    *out = st.st_mtime;
    return 0;
}

static int read_file_text(const char *path, char *buf, size_t cap) {
    if (path == NULL || buf == NULL || cap == 0) {
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[n - 1] = '\0';
        n--;
    }
    return 0;
}

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1) {
        return 127;
    }
    if (WIFEXITED(rc)) {
        return WEXITSTATUS(rc);
    }
    return 1;
}

static int mkdir_if_missing(const char *path) {
    if (mkdir(path, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

static int path_ends_with(const char *path, const char *suffix) {
    size_t n = strlen(path);
    size_t m = strlen(suffix);
    if (m > n) {
        return 0;
    }
    return memcmp(path + (n - m), suffix, m) == 0;
}

static const char *arg_value(const char *arg, const char *prefix) {
    size_t n = strlen(prefix);
    if (strncmp(arg, prefix, n) != 0) {
        return NULL;
    }
    return arg + n;
}

static int env_is_truthy(const char *name) {
    const char *val = getenv(name);
    if (val == NULL || val[0] == '\0') {
        return 0;
    }
    if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0 || strcmp(val, "yes") == 0) {
        return 1;
    }
    return 0;
}

static int use_core_for_mode(const char *mode, const char *in_path) {
    if (mode && strcmp(mode, "sem") == 0) {
        return 1;
    }
    if (mode && strcmp(mode, "deps-list") == 0) {
        return 1;
    }
    if (mode && strcmp(mode, "c") == 0) {
        return 1;
    }
    if (mode && strcmp(mode, "asm") == 0) {
        return 1;
    }
    if (mode && strcmp(mode, "hrt") == 0) {
        return 1;
    }
    const char *val = getenv("CHENG_STAGE0C_CORE");
    if (val == NULL || val[0] == '\0') {
        val = "1";
    }
    if (strcmp(val, "0") == 0) {
        return 0;
    }
    if (strcmp(val, "1") == 0 || strcmp(val, "true") == 0 || strcmp(val, "yes") == 0) {
        return 1;
    }
    if (strcmp(val, "bootstrap") == 0 || strcmp(val, "auto") == 0) {
        return 1;
    }
    if (mode == NULL) {
        return 0;
    }
    return 1;
}

static int inc_has_header(const char *dir) {
    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/chengbase.h", dir);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        return 0;
    }
    return path_is_file(path);
}

static const char *pick_runtime_inc(void) {
    const char *env = getenv("CHENG_RT_INC");
    if (inc_has_header(env)) {
        return env;
    }
    env = getenv("CHENG_RUNTIME_INC");
    if (inc_has_header(env)) {
        return env;
    }
    if (inc_has_header("runtime/include")) {
        return "runtime/include";
    }
    return "runtime/include";
}

static const char *pick_stage1_cflags(void) {
    const char *flags = getenv("CHENG_STAGE1_CFLAGS");
    if (flags != NULL && flags[0] != '\0') {
        return flags;
    }
    flags = getenv("CFLAGS");
    if (flags != NULL && flags[0] != '\0') {
        return flags;
    }
    return "-O2";
}

static int stage1_runner_needs_rebuild(const char *cflags) {
    const char *force = getenv("CHENG_STAGE1_REBUILD");
    if (force != NULL && force[0] == '1') {
        return 1;
    }
    if (!path_is_exec("./stage1_runner")) {
        return 1;
    }
    time_t bin_ts = 0;
    time_t seed_ts = 0;
    time_t helper_ts = 0;
    if (file_mtime("./stage1_runner", &bin_ts) != 0) {
        return 1;
    }
    if (file_mtime("src/stage1/stage1_runner.seed.c", &seed_ts) == 0) {
        if (seed_ts > bin_ts) {
            return 1;
        }
    }
    if (file_mtime("src/runtime/native/system_helpers.c", &helper_ts) == 0) {
        if (helper_ts > bin_ts) {
            return 1;
        }
    }
    if (cflags != NULL && cflags[0] != '\0') {
        char buf[256];
        if (read_file_text("chengcache/stage1_runner.cflags", buf, sizeof(buf)) != 0) {
            return 1;
        }
        if (strcmp(buf, cflags) != 0) {
            return 1;
        }
    }
    return 0;
}

static int seed_is_stale(const char *seed_path) {
    time_t seed_ts = 0;
    if (file_mtime(seed_path, &seed_ts) != 0) {
        return 1;
    }
    const char *sources[] = {
        "src/stage1/frontend_bootstrap.cheng",
        "src/stage1/frontend_stage1.cheng",
        "src/stage1/c_codegen.cheng",
        "src/stage1/parser.cheng",
        "src/stage1/lexer.cheng",
        "src/stage1/ast.cheng",
        "src/stage1/semantics.cheng",
        "src/stage1/ownership.cheng",
        "src/stage1/monomorphize.cheng",
        "src/stage1/frontend_lib.cheng",
        NULL,
    };
    for (int i = 0; sources[i] != NULL; i++) {
        time_t src_ts = 0;
        if (file_mtime(sources[i], &src_ts) == 0) {
            if (src_ts > seed_ts) {
                return 1;
            }
        }
    }
    return 0;
}

static void write_stage1_cflags(const char *cflags) {
    if (cflags == NULL || cflags[0] == '\0') {
        return;
    }
    FILE *f = fopen("chengcache/stage1_runner.cflags", "w");
    if (!f) {
        return;
    }
    fputs(cflags, f);
    fputc('\n', f);
    fclose(f);
}

static int detect_repo_root(const char *argv0, char *out, size_t out_len) {
    char exe[PATH_MAX];
    if (argv0 != NULL && realpath(argv0, exe) != NULL) {
        if (path_ends_with(exe, "/bin/cheng_c") || path_ends_with(exe, "/bin/cheng")) {
            char *last = strrchr(exe, '/');
            if (last == NULL) {
                return -1;
            }
            *last = '\0';
            last = strrchr(exe, '/');
            if (last == NULL) {
                return -1;
            }
            *last = '\0';
            if (strlen(exe) + 1 > out_len) {
                return -1;
            }
            strncpy(out, exe, out_len - 1);
            out[out_len - 1] = '\0';
            return 0;
        }
    }
    if (getcwd(out, (int)out_len) == NULL) {
        return -1;
    }
    return 0;
}

static int ensure_stage1_runner(const char *cc) {
    const char *cflags = pick_stage1_cflags();
    int needs_rebuild = stage1_runner_needs_rebuild(cflags);
    if (!needs_rebuild) {
        return 0;
    }
    {
        const char *allow_stale = getenv("CHENG_STAGE0C_ALLOW_STALE_SEED");
        if (allow_stale == NULL || allow_stale[0] != '1') {
            const char *seed_path = "src/stage1/stage1_runner.seed.c";
            if (path_is_file(seed_path) && seed_is_stale(seed_path)) {
                fprintf(stderr,
                        "[Error] stage1 seed is older than stage1 sources.\n"
                        "        Update seed with stage0c:\n"
                        "        CHENG_BOOTSTRAP_UPDATE_SEED=1 "
                        "./src/tooling/bootstrap.sh\n"
                        "        (set CHENG_STAGE0C_ALLOW_STALE_SEED=1 to bypass)\n");
                return 2;
            }
        }
    }
    fprintf(stderr, "== stage0c: rebuild stage1_runner (CFLAGS=%s) ==\n", cflags);
    const char *seed_c_path = "src/stage1/stage1_runner.seed.c";
    const char *cache_c_path = "chengcache/stage1_runner.c";
    const char *runtime_inc = pick_runtime_inc();
    if (mkdir_if_missing("chengcache") != 0) {
        fprintf(stderr, "[Error] failed to create chengcache: %s\n", strerror(errno));
        return 2;
    }
    {
        char cmd[PATH_MAX * 2];
        const char *src_c = NULL;
        if (path_is_file(seed_c_path)) {
            src_c = seed_c_path;
        } else if (path_is_file(cache_c_path)) {
            src_c = cache_c_path;
        }
        if (src_c == NULL) {
            fprintf(stderr,
                    "[Error] missing seed C: %s\n"
                    "        (expected seed at %s or cached C at %s)\n",
                    seed_c_path,
                    seed_c_path,
                    cache_c_path);
            return 1;
        }
        snprintf(cmd, sizeof(cmd), "%s %s -I%s -Isrc/runtime/native -c %s -o chengcache/stage1_runner.o", cc, cflags, runtime_inc, src_c);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "[Error] failed: %s\n", cmd);
            return 1;
        }
        snprintf(cmd, sizeof(cmd), "%s %s -I%s -Isrc/runtime/native -c src/runtime/native/system_helpers.c -o chengcache/stage1_runner.system_helpers.o", cc, cflags, runtime_inc);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "[Error] failed: %s\n", cmd);
            return 1;
        }
        snprintf(cmd, sizeof(cmd), "%s chengcache/stage1_runner.o chengcache/stage1_runner.system_helpers.o -o stage1_runner", cc);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "[Error] failed: %s\n", cmd);
            return 1;
        }
    }
    if (!path_is_exec("./stage1_runner")) {
        fprintf(stderr, "[Error] stage1_runner build failed\n");
        return 1;
    }
    write_stage1_cflags(cflags);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  cheng --mode:deps-list --file:<input.cheng> [--out:<output.deps.list>]\n"
            "  cheng --mode:sem --file:<input.cheng> [--out:<output.txt>]\n"
            "  cheng --mode:c --file:<input.cheng> --out:<output.c>\n"
            "  cheng --mode:asm --file:<input.cheng> --out:<output.s>\n"
            "  cheng --mode:hrt --file:<input.cheng> --out:<output.s>\n"
            "\n"
            "Notes:\n"
            "  - This C stage0 driver currently supports --mode:deps-list, --mode:sem, --mode:c, --mode:asm, --mode:hrt.\n"
            "  - It delegates compilation to ./stage1_runner (builds from seed if missing).\n"
            "  - --mode:c/--mode:asm/--mode:hrt run in stage0c core only.\n");
}

int main(int argc, char **argv) {
    const char *mode = NULL;
    const char *in_path = NULL;
    const char *out_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage();
            return 0;
        }
        const char *val = arg_value(arg, "--mode:");
        if (val != NULL) {
            mode = val;
            continue;
        }
        val = arg_value(arg, "--file:");
        if (val != NULL) {
            in_path = val;
            continue;
        }
        val = arg_value(arg, "--out:");
        if (val != NULL) {
            out_path = val;
            continue;
        }
    }
    if (mode == NULL || mode[0] == '\0') {
        mode = "c";
    }
    if (strcmp(mode, "deps") == 0) {
        fprintf(stderr, "[Error] stage0c --mode:deps removed; use --mode:deps-list instead\n");
        usage();
        return 2;
    }
    if (strcmp(mode, "deps-list") != 0 &&
        strcmp(mode, "sem") != 0 && strcmp(mode, "c") != 0 &&
        strcmp(mode, "asm") != 0 && strcmp(mode, "hrt") != 0) {
        fprintf(stderr, "[Error] stage0c only supports --mode:deps-list/--mode:sem/--mode:c/--mode:asm/--mode:hrt: %s\n", mode);
        usage();
        return 2;
    }

    char root[PATH_MAX];
    if (detect_repo_root(argv[0], root, sizeof(root)) != 0) {
        fprintf(stderr, "[Error] cannot determine repo root\n");
        return 2;
    }
    if (chdir(root) != 0) {
        fprintf(stderr, "[Error] cannot chdir to repo root: %s\n", strerror(errno));
        return 2;
    }

    if (use_core_for_mode(mode, in_path)) {
        ChengCoreOpts opts;
        opts.mode = mode;
        opts.in_path = in_path;
        opts.out_path = out_path;
        opts.trace = env_is_truthy("CHENG_STAGE0C_CORE_TRACE");
        int rc = cheng_core_compile(&opts);
        return rc;
    }

    const char *cc = getenv("CC");
    if (cc == NULL || cc[0] == '\0') {
        cc = "cc";
    }
    {
        int rc = ensure_stage1_runner(cc);
        if (rc != 0) {
            return rc;
        }
    }

    char **args = (char **)calloc((size_t)argc + 1, sizeof(char *));
    if (args == NULL) {
        fprintf(stderr, "[Error] out of memory\n");
        return 1;
    }
    args[0] = "./stage1_runner";
    for (int i = 1; i < argc; i++) {
        args[i] = argv[i];
    }
    args[argc] = NULL;
    execv(args[0], args);
    fprintf(stderr, "[Error] failed to exec stage1_runner: %s\n", strerror(errno));
    return 1;
}
