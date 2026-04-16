#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if !defined(_WIN32)
#include <sys/ioctl.h>
#endif

#include "system_helpers.h"

#if !defined(_WIN32)
extern char **environ;
#endif

typedef struct ChengAbiSeqI32View {
    int32_t len;
    int32_t cap;
    int32_t *buffer;
} ChengAbiSeqI32View;

typedef struct ChengAbiPairI32 {
    int32_t a;
    int32_t b;
} ChengAbiPairI32;

typedef struct ChengFfiHandleSlot {
    void *ptr;
    uint32_t generation;
} ChengFfiHandleSlot;

static ChengFfiHandleSlot *cheng_ffi_handle_slots = NULL;
static uint32_t cheng_ffi_handle_slots_len = 0;
static uint32_t cheng_ffi_handle_slots_cap = 0;

static char *cheng_host_strdup(const char *s) {
    const char *safe = s != NULL ? s : "";
    size_t len = strlen(safe);
    char *out = (char *)cheng_malloc((int32_t)len + 1);
    if (out == NULL) {
        return NULL;
    }
    if (len > 0u) {
        memcpy(out, safe, len);
    }
    out[len] = '\0';
    return out;
}

static const char *cheng_seq_string_item(ChengSeqHeader seq, int32_t idx) {
    if (idx < 0 || idx >= seq.len || seq.buffer == NULL) {
        return "";
    }
    return ((const char *const *)seq.buffer)[idx] != NULL
        ? ((const char *const *)seq.buffer)[idx]
        : "";
}

static int cheng_env_key_eq(const char *entry, const char *key, size_t key_len) {
    if (entry == NULL || key == NULL || key_len == 0u) {
        return 0;
    }
    if (strncmp(entry, key, key_len) != 0) {
        return 0;
    }
    return entry[key_len] == '=';
}

static char **cheng_exec_build_envp(ChengSeqHeader overrides) {
#if defined(_WIN32)
    (void)overrides;
    return NULL;
#else
    size_t base_count = 0u;
    size_t i = 0u;
    size_t out_count = 0u;
    char **out = NULL;
    if (environ != NULL) {
        while (environ[base_count] != NULL) {
            base_count += 1u;
        }
    }
    out = (char **)malloc(sizeof(char *) * (base_count + (size_t)(overrides.len > 0 ? overrides.len : 0) + 1u));
    if (out == NULL) {
        return NULL;
    }
    for (i = 0u; i < base_count; i += 1u) {
        out[i] = environ[i];
    }
    out_count = base_count;
    for (i = 0u; i < (size_t)(overrides.len > 0 ? overrides.len : 0); i += 1u) {
        const char *raw = cheng_seq_string_item(overrides, (int32_t)i);
        const char *eq = raw != NULL ? strchr(raw, '=') : NULL;
        size_t key_len = eq != NULL ? (size_t)(eq - raw) : 0u;
        size_t j = 0u;
        int replaced = 0;
        if (raw == NULL || raw[0] == '\0' || key_len == 0u) {
            continue;
        }
        for (j = 0u; j < out_count; j += 1u) {
            if (cheng_env_key_eq(out[j], raw, key_len)) {
                out[j] = (char *)raw;
                replaced = 1;
                break;
            }
        }
        if (!replaced) {
            out[out_count++] = (char *)raw;
        }
    }
    out[out_count] = NULL;
    return out;
#endif
}

static char **cheng_exec_build_argv(const char *file_path, ChengSeqHeader argv_seq) {
    size_t extra_count = (size_t)(argv_seq.len > 0 ? argv_seq.len : 0);
    size_t i = 0u;
    char **argv = (char **)malloc(sizeof(char *) * (extra_count + 2u));
    if (argv == NULL) {
        return NULL;
    }
    argv[0] = (char *)(file_path != NULL ? file_path : "");
    for (i = 0u; i < extra_count; i += 1u) {
        argv[i + 1u] = (char *)cheng_seq_string_item(argv_seq, (int32_t)i);
    }
    argv[extra_count + 1u] = NULL;
    return argv;
}

static void cheng_exec_free_argv_env(char **argv, char **envp) {
    if (argv != NULL) {
        free(argv);
    }
    if (envp != NULL) {
        free(envp);
    }
}

static int cheng_exec_load_argv_env(const char *file_path,
                                    void *argv_seq_ptr,
                                    void *env_overrides_seq_ptr,
                                    char ***argv_out,
                                    char ***envp_out) {
    ChengSeqHeader argv_seq;
    ChengSeqHeader env_seq;
    char **argv = NULL;
    char **envp = NULL;
    memset(&argv_seq, 0, sizeof(argv_seq));
    memset(&env_seq, 0, sizeof(env_seq));
    if (file_path == NULL || file_path[0] == '\0' || argv_out == NULL || envp_out == NULL) {
        return 0;
    }
    if (argv_seq_ptr != NULL) {
        argv_seq = *(ChengSeqHeader *)argv_seq_ptr;
    }
    if (env_overrides_seq_ptr != NULL) {
        env_seq = *(ChengSeqHeader *)env_overrides_seq_ptr;
    }
    argv = cheng_exec_build_argv(file_path, argv_seq);
    if (argv == NULL) {
        return 0;
    }
    envp = cheng_exec_build_envp(env_seq);
    *argv_out = argv;
    *envp_out = envp;
    return 1;
}

static void cheng_exec_kill_target(pid_t pid, int sig) {
#if defined(_WIN32)
    (void)pid;
    (void)sig;
#else
    if (pid <= 0) {
        return;
    }
    if (kill(-pid, sig) != 0) {
        (void)kill(pid, sig);
    }
#endif
}

static void cheng_exec_spawn_detached_orphan_guard(pid_t wrapper_pid, pid_t target_pid) {
#if defined(_WIN32)
    (void)wrapper_pid;
    (void)target_pid;
#else
    pid_t guard_pid = -1;
    if (wrapper_pid <= 0 || target_pid <= 0) {
        return;
    }
    guard_pid = fork();
    if (guard_pid < 0) {
        return;
    }
    if (guard_pid > 0) {
        (void)waitpid(guard_pid, NULL, 0);
        return;
    }
    {
        pid_t worker_pid = fork();
        if (worker_pid < 0) {
            _exit(0);
        }
        if (worker_pid > 0) {
            _exit(0);
        }
    }
    (void)setsid();
    for (;;) {
        if (kill(target_pid, 0) != 0 && errno == ESRCH) {
            _exit(0);
        }
        if (kill(wrapper_pid, 0) != 0 && errno == ESRCH) {
            cheng_exec_kill_target(target_pid, SIGTERM);
            usleep(200000);
            cheng_exec_kill_target(target_pid, SIGKILL);
            _exit(0);
        }
        usleep(200000);
    }
#endif
}

static char *cheng_host_read_text(int32_t fd, int32_t max_bytes, int32_t timeout_ms, int32_t *out_eof) {
#if defined(_WIN32)
    if (out_eof != NULL) {
        *out_eof = 1;
    }
    return cheng_host_strdup("");
#else
    struct pollfd pfd;
    char *buf = NULL;
    ssize_t n = 0;
    if (out_eof != NULL) {
        *out_eof = 0;
    }
    if (fd < 0 || max_bytes <= 0) {
        return cheng_host_strdup("");
    }
    if (max_bytes > 65536) {
        max_bytes = 65536;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, timeout_ms) <= 0 || (pfd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
        return cheng_host_strdup("");
    }
    buf = (char *)cheng_malloc(max_bytes + 1);
    if (buf == NULL) {
        return cheng_host_strdup("");
    }
    n = read(fd, buf, (size_t)max_bytes);
    if (n > 0) {
        buf[n] = '\0';
        return buf;
    }
    cheng_free(buf);
    if (n == 0) {
        if (out_eof != NULL) {
            *out_eof = 1;
        }
        return cheng_host_strdup("");
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return cheng_host_strdup("");
    }
    if (out_eof != NULL) {
        *out_eof = 1;
    }
    return cheng_host_strdup("");
#endif
}

static int cheng_host_str_bridge_view(ChengStrBridge bridge, const char **out_ptr, size_t *out_len) {
    const char *safe = bridge.ptr != NULL ? bridge.ptr : "";
    int32_t raw_len = bridge.len;
    if (raw_len < 0) {
        raw_len = 0;
    }
    if (out_ptr != NULL) {
        *out_ptr = safe;
    }
    if (out_len != NULL) {
        *out_len = (size_t)raw_len;
    }
    return 1;
}

static char *cheng_host_bridge_copy_cstring(ChengStrBridge text) {
    const char *safe = "";
    size_t n = 0u;
    char *out = NULL;
    (void)cheng_host_str_bridge_view(text, &safe, &n);
    out = (char *)malloc(n + 1u);
    if (out == NULL) {
        return NULL;
    }
    if (n > 0u) {
        memcpy(out, safe, n);
    }
    out[n] = '\0';
    return out;
}

static void cheng_host_mkdir_all_raw(const char *path, int include_leaf) {
#if defined(_WIN32)
    (void)path;
    (void)include_leaf;
#else
    char *raw = NULL;
    char *scan = NULL;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    raw = strdup(path);
    if (raw == NULL) {
        abort();
    }
    if (!include_leaf) {
        char *slash = strrchr(raw, '/');
        if (slash == NULL) {
            free(raw);
            return;
        }
        *slash = '\0';
        if (raw[0] == '\0') {
            free(raw);
            return;
        }
    }
    scan = raw;
    if (scan[0] == '/') {
        scan += 1;
    }
    while (*scan != '\0') {
        if (*scan == '/') {
            *scan = '\0';
            if (raw[0] != '\0' && mkdir(raw, 0777) != 0 && errno != EEXIST) {
                free(raw);
                abort();
            }
            *scan = '/';
        }
        scan += 1;
    }
    if (raw[0] != '\0' && mkdir(raw, 0777) != 0 && errno != EEXIST) {
        free(raw);
        abort();
    }
    free(raw);
#endif
}

int32_t cheng_pty_is_supported(void) {
#if defined(_WIN32)
    return 0;
#else
    return 1;
#endif
}

int32_t cheng_pty_spawn(const char *command, const char *working_dir, int32_t *out_master_fd, int64_t *out_pid) {
#if defined(_WIN32)
    if (out_master_fd != NULL) *out_master_fd = -1;
    if (out_pid != NULL) *out_pid = -1;
    (void)command;
    (void)working_dir;
    return 0;
#else
    int master_fd = -1;
    pid_t pid = -1;
    if (out_master_fd != NULL) *out_master_fd = -1;
    if (out_pid != NULL) *out_pid = -1;
    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        return 0;
    }
    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        close(master_fd);
        return 0;
    }
    {
        char *slave_name = ptsname(master_fd);
        if (slave_name == NULL) {
            close(master_fd);
            return 0;
        }
        pid = fork();
        if (pid < 0) {
            close(master_fd);
            return 0;
        }
        if (pid == 0) {
            int slave_fd = -1;
            (void)setsid();
            slave_fd = open(slave_name, O_RDWR);
            if (slave_fd < 0) {
                _exit(127);
            }
#ifdef TIOCSCTTY
            (void)ioctl(slave_fd, TIOCSCTTY, 0);
#endif
            (void)dup2(slave_fd, STDIN_FILENO);
            (void)dup2(slave_fd, STDOUT_FILENO);
            (void)dup2(slave_fd, STDERR_FILENO);
            if (slave_fd > STDERR_FILENO) {
                close(slave_fd);
            }
            if (working_dir != NULL && working_dir[0] != '\0') {
                (void)chdir(working_dir);
            }
            if (command != NULL && command[0] != '\0') {
                execl("/bin/sh", "sh", "-lc", command, (char *)NULL);
            } else {
                const char *shell = getenv("SHELL");
                if (shell == NULL || shell[0] == '\0') {
                    shell = "/bin/sh";
                }
                execl(shell, shell, "-l", (char *)NULL);
            }
            _exit(127);
        }
    }
    if (out_master_fd != NULL) *out_master_fd = master_fd;
    if (out_pid != NULL) *out_pid = (int64_t)pid;
    return 1;
#endif
}

int32_t cheng_pipe_spawn(const char *command, const char *working_dir,
                         int32_t *out_read_fd, int32_t *out_write_fd, int64_t *out_pid) {
#if defined(_WIN32)
    if (out_read_fd != NULL) *out_read_fd = -1;
    if (out_write_fd != NULL) *out_write_fd = -1;
    if (out_pid != NULL) *out_pid = -1;
    (void)command;
    (void)working_dir;
    return 0;
#else
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid = -1;
    if (out_read_fd != NULL) *out_read_fd = -1;
    if (out_write_fd != NULL) *out_write_fd = -1;
    if (out_pid != NULL) *out_pid = -1;
    if (pipe(in_pipe) != 0) {
        return 0;
    }
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return 0;
    }
    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return 0;
    }
    if (pid == 0) {
        (void)dup2(in_pipe[0], STDIN_FILENO);
        (void)dup2(out_pipe[1], STDOUT_FILENO);
        (void)dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (working_dir != NULL && working_dir[0] != '\0') {
            (void)chdir(working_dir);
        }
        if (command != NULL && command[0] != '\0') {
            execl("/bin/sh", "sh", "-lc", command, (char *)NULL);
        } else {
            const char *shell = getenv("SHELL");
            if (shell == NULL || shell[0] == '\0') {
                shell = "/bin/sh";
            }
            execl(shell, shell, "-l", (char *)NULL);
        }
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    {
        int flags = fcntl(out_pipe[0], F_GETFL);
        if (flags != -1) {
            (void)fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);
        }
        flags = fcntl(in_pipe[1], F_GETFL);
        if (flags != -1) {
            (void)fcntl(in_pipe[1], F_SETFL, flags | O_NONBLOCK);
        }
    }
    if (out_read_fd != NULL) *out_read_fd = out_pipe[0];
    if (out_write_fd != NULL) *out_write_fd = in_pipe[1];
    if (out_pid != NULL) *out_pid = (int64_t)pid;
    return 1;
#endif
}

int32_t cheng_exec_file_pipe_spawn(const char *file_path, void *argv_seq_ptr, void *env_overrides_seq_ptr,
                                   const char *working_dir, int32_t *out_read_fd,
                                   int32_t *out_write_fd, int64_t *out_pid) {
#if defined(_WIN32)
    if (out_read_fd != NULL) *out_read_fd = -1;
    if (out_write_fd != NULL) *out_write_fd = -1;
    if (out_pid != NULL) *out_pid = -1;
    (void)file_path;
    (void)argv_seq_ptr;
    (void)env_overrides_seq_ptr;
    (void)working_dir;
    return 0;
#else
    char **argv = NULL;
    char **envp = NULL;
    int in_pipe[2];
    int out_pipe[2];
    pid_t pid = -1;
    if (out_read_fd != NULL) *out_read_fd = -1;
    if (out_write_fd != NULL) *out_write_fd = -1;
    if (out_pid != NULL) *out_pid = -1;
    if (file_path == NULL || file_path[0] == '\0') {
        return 0;
    }
    if (!cheng_exec_load_argv_env(file_path, argv_seq_ptr, env_overrides_seq_ptr, &argv, &envp)) {
        return 0;
    }
    if (pipe(in_pipe) != 0) {
        cheng_exec_free_argv_env(argv, envp);
        return 0;
    }
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        cheng_exec_free_argv_env(argv, envp);
        return 0;
    }
    pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        cheng_exec_free_argv_env(argv, envp);
        return 0;
    }
    if (pid == 0) {
        (void)setpgid(0, 0);
        (void)dup2(in_pipe[0], STDIN_FILENO);
        (void)dup2(out_pipe[1], STDOUT_FILENO);
        (void)dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        if (working_dir != NULL && working_dir[0] != '\0') {
            (void)chdir(working_dir);
        }
        execve(file_path, argv, envp != NULL ? envp : environ);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    cheng_exec_free_argv_env(argv, envp);
    (void)setpgid(pid, pid);
    {
        int flags = fcntl(out_pipe[0], F_GETFL);
        if (flags != -1) {
            (void)fcntl(out_pipe[0], F_SETFL, flags | O_NONBLOCK);
        }
        flags = fcntl(in_pipe[1], F_GETFL);
        if (flags != -1) {
            (void)fcntl(in_pipe[1], F_SETFL, flags | O_NONBLOCK);
        }
    }
    cheng_exec_spawn_detached_orphan_guard(getpid(), pid);
    if (out_read_fd != NULL) *out_read_fd = out_pipe[0];
    if (out_write_fd != NULL) *out_write_fd = in_pipe[1];
    if (out_pid != NULL) *out_pid = (int64_t)pid;
    return 1;
#endif
}

char *cheng_pty_read(int32_t fd, int32_t max_bytes, int32_t *out_eof) {
    return cheng_host_read_text(fd, max_bytes, 0, out_eof);
}

char *cheng_fd_read(int32_t fd, int32_t max_bytes, int32_t *out_eof) {
    return cheng_host_read_text(fd, max_bytes, 0, out_eof);
}

char *cheng_fd_read_wait(int32_t fd, int32_t max_bytes, int32_t timeout_ms, int32_t *out_eof) {
    if (timeout_ms > 50) {
        timeout_ms = 50;
    }
    return cheng_host_read_text(fd, max_bytes, timeout_ms, out_eof);
}

int32_t cheng_pty_write(int32_t fd, const char *data, int32_t len) {
#if defined(_WIN32)
    (void)fd;
    (void)data;
    (void)len;
    return -1;
#else
    ssize_t n = 0;
    if (fd < 0 || data == NULL || len <= 0) {
        return 0;
    }
    n = write(fd, data, (size_t)len);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLOUT) != 0) {
            n = write(fd, data, (size_t)len);
        }
    }
    return n < 0 ? -1 : (int32_t)n;
#endif
}

int32_t cheng_pty_close(int32_t fd) {
#if defined(_WIN32)
    (void)fd;
    return -1;
#else
    if (fd < 0) {
        return 0;
    }
    return close(fd);
#endif
}

int32_t cheng_pty_wait(int64_t pid, int32_t *out_exit_code) {
#if defined(_WIN32)
    if (out_exit_code != NULL) *out_exit_code = -1;
    (void)pid;
    return -1;
#else
    int status = 0;
    pid_t rc = 0;
    if (out_exit_code != NULL) *out_exit_code = -1;
    if (pid <= 0) {
        return -1;
    }
    rc = waitpid((pid_t)pid, &status, WNOHANG);
    if (rc == 0) {
        return 0;
    }
    if (rc < 0) {
        return -1;
    }
    if (out_exit_code != NULL) {
        if (WIFEXITED(status)) *out_exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *out_exit_code = 128 + WTERMSIG(status);
        else *out_exit_code = status;
    }
    return 1;
#endif
}

int32_t cheng_process_signal(int64_t pid, int32_t signal_code) {
#if defined(_WIN32)
    (void)pid;
    (void)signal_code;
    return -1;
#else
    if (pid <= 0 || signal_code <= 0) {
        return -1;
    }
    if (kill(-(pid_t)pid, signal_code) == 0) {
        return 0;
    }
    if (kill((pid_t)pid, signal_code) == 0) {
        return 0;
    }
    return -1;
#endif
}

static int32_t cheng_abi_sum_seq_i32_raw(const int32_t *ptr, int32_t len) {
    int64_t sum = 0;
    int32_t i = 0;
    if (ptr == NULL || len <= 0) {
        return 0;
    }
    for (i = 0; i < len; i += 1) {
        sum += (int64_t)ptr[i];
    }
    return (int32_t)sum;
}

int32_t cheng_abi_sum_ptr_len_i32(const int32_t *ptr, int32_t len) {
    return cheng_abi_sum_seq_i32_raw(ptr, len);
}

int32_t cheng_abi_sum_seq_i32(uint64_t seq_like0, uint64_t seq_like1, uint64_t seq_like2) {
    if (seq_like0 <= (uint64_t)(1u << 20) &&
        seq_like1 <= (uint64_t)(1u << 20) &&
        seq_like2 > (uint64_t)4096) {
        return cheng_abi_sum_seq_i32_raw((const int32_t *)(uintptr_t)seq_like2, (int32_t)seq_like0);
    }
    {
        uint32_t lo = (uint32_t)(seq_like0 & 0xffffffffu);
        uint32_t hi = (uint32_t)((seq_like0 >> 32u) & 0xffffffffu);
        if (lo <= hi && hi <= (uint32_t)(1u << 20) && seq_like1 > (uint64_t)4096) {
            return cheng_abi_sum_seq_i32_raw((const int32_t *)(uintptr_t)seq_like1, (int32_t)lo);
        }
    }
    if (seq_like0 == 0u) {
        return 0;
    }
    {
        const ChengAbiSeqI32View *seq = (const ChengAbiSeqI32View *)(uintptr_t)seq_like0;
        if (seq->len < 0 || seq->len > (1 << 26)) {
            return 0;
        }
        return cheng_abi_sum_seq_i32_raw((const int32_t *)seq->buffer, seq->len);
    }
}

void cheng_abi_borrow_mut_pair_i32(int64_t pair_ptr, int32_t da, int32_t db) {
    ChengAbiPairI32 *pair = NULL;
    if (pair_ptr == 0) {
        return;
    }
    pair = (ChengAbiPairI32 *)(uintptr_t)pair_ptr;
    pair->a += da;
    pair->b += db;
}

static int cheng_ffi_handle_ensure_capacity(uint32_t min_cap) {
    uint32_t new_cap = 0;
    size_t bytes = 0u;
    ChengFfiHandleSlot *grown = NULL;
    uint32_t i = 0;
    if (cheng_ffi_handle_slots_cap >= min_cap) {
        return 1;
    }
    new_cap = cheng_ffi_handle_slots_cap;
    if (new_cap < 16u) {
        new_cap = 16u;
    }
    while (new_cap < min_cap) {
        if (new_cap > (UINT32_MAX / 2u)) {
            new_cap = min_cap;
            break;
        }
        new_cap *= 2u;
    }
    bytes = (size_t)new_cap * sizeof(ChengFfiHandleSlot);
    if (new_cap != 0u && bytes / sizeof(ChengFfiHandleSlot) != (size_t)new_cap) {
        return 0;
    }
    grown = (ChengFfiHandleSlot *)realloc(cheng_ffi_handle_slots, bytes);
    if (grown == NULL) {
        return 0;
    }
    for (i = cheng_ffi_handle_slots_cap; i < new_cap; i += 1u) {
        grown[i].ptr = NULL;
        grown[i].generation = 1u;
    }
    cheng_ffi_handle_slots = grown;
    cheng_ffi_handle_slots_cap = new_cap;
    return 1;
}

static int cheng_ffi_handle_decode(uint64_t handle, uint32_t *out_idx, uint32_t *out_generation) {
    uint32_t low = 0u;
    uint32_t generation = 0u;
    uint32_t idx = 0u;
    if (handle == 0u) {
        return 0;
    }
    low = (uint32_t)(handle & 0xffffffffu);
    generation = (uint32_t)(handle >> 32u);
    if (low == 0u || generation == 0u) {
        return 0;
    }
    idx = low - 1u;
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

uint64_t cheng_ffi_handle_register_ptr(void *ptr) {
    uint32_t idx = UINT32_MAX;
    uint32_t i = 0u;
    ChengFfiHandleSlot *slot = NULL;
    if (ptr == NULL) {
        return 0u;
    }
    for (i = 0u; i < cheng_ffi_handle_slots_len; i += 1u) {
        if (cheng_ffi_handle_slots[i].ptr == NULL) {
            idx = i;
            break;
        }
    }
    if (idx == UINT32_MAX) {
        idx = cheng_ffi_handle_slots_len;
        if (!cheng_ffi_handle_ensure_capacity(idx + 1u)) {
            return 0u;
        }
        cheng_ffi_handle_slots_len = idx + 1u;
    }
    slot = &cheng_ffi_handle_slots[idx];
    slot->ptr = ptr;
    if (slot->generation == 0u) {
        slot->generation = 1u;
    }
    return ((uint64_t)slot->generation << 32u) | (uint64_t)(idx + 1u);
}

void *cheng_ffi_handle_resolve_ptr(uint64_t handle) {
    uint32_t idx = 0u;
    uint32_t generation = 0u;
    ChengFfiHandleSlot *slot = NULL;
    if (!cheng_ffi_handle_decode(handle, &idx, &generation)) {
        return NULL;
    }
    slot = &cheng_ffi_handle_slots[idx];
    if (slot->ptr == NULL || slot->generation != generation) {
        return NULL;
    }
    return slot->ptr;
}

int32_t cheng_ffi_handle_invalidate(uint64_t handle) {
    uint32_t idx = 0u;
    uint32_t generation = 0u;
    ChengFfiHandleSlot *slot = NULL;
    if (!cheng_ffi_handle_decode(handle, &idx, &generation)) {
        return -1;
    }
    slot = &cheng_ffi_handle_slots[idx];
    if (slot->ptr == NULL || slot->generation != generation) {
        return -1;
    }
    slot->ptr = NULL;
    slot->generation = slot->generation == UINT32_MAX ? 1u : slot->generation + 1u;
    return 0;
}

uint64_t cheng_ffi_handle_new_i32(int32_t value) {
    int32_t *cell = (int32_t *)malloc(sizeof(int32_t));
    uint64_t handle = 0u;
    if (cell == NULL) {
        return 0u;
    }
    *cell = value;
    handle = cheng_ffi_handle_register_ptr((void *)cell);
    if (handle == 0u) {
        free(cell);
    }
    return handle;
}

int32_t cheng_ffi_handle_get_i32(uint64_t handle, int32_t *out_value) {
    int32_t *cell = NULL;
    if (out_value == NULL) {
        return -1;
    }
    cell = (int32_t *)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return -1;
    }
    *out_value = *cell;
    return 0;
}

int32_t cheng_ffi_handle_add_i32(uint64_t handle, int32_t delta, int32_t *out_value) {
    int32_t *cell = NULL;
    if (out_value == NULL) {
        return -1;
    }
    cell = (int32_t *)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return -1;
    }
    *cell += delta;
    *out_value = *cell;
    return 0;
}

int32_t cheng_ffi_handle_release_i32(uint64_t handle) {
    int32_t *cell = (int32_t *)cheng_ffi_handle_resolve_ptr(handle);
    if (cell == NULL) {
        return -1;
    }
    if (cheng_ffi_handle_invalidate(handle) != 0) {
        return -1;
    }
    free(cell);
    return 0;
}

void *cheng_ffi_raw_new_i32(int32_t value) {
    int32_t *cell = (int32_t *)malloc(sizeof(int32_t));
    if (cell == NULL) {
        return NULL;
    }
    *cell = value;
    return (void *)cell;
}

int32_t cheng_ffi_raw_get_i32(void *p) {
    return p == NULL ? -1 : *(int32_t *)p;
}

int32_t cheng_ffi_raw_add_i32(void *p, int32_t delta) {
    int32_t *cell = (int32_t *)p;
    if (cell == NULL) {
        return -1;
    }
    *cell += delta;
    return *cell;
}

int32_t cheng_ffi_raw_release_i32(void *p) {
    if (p == NULL) {
        return -1;
    }
    free(p);
    return 0;
}

void driver_c_write_text_file_bridge(ChengStrBridge path, ChengStrBridge content) {
    char *raw = cheng_host_bridge_copy_cstring(path);
    const char *payload = "";
    size_t payload_len = 0u;
    FILE *f = NULL;
    if (raw == NULL) {
        abort();
    }
    if (raw[0] == '\0') {
        free(raw);
        return;
    }
    (void)cheng_host_str_bridge_view(content, &payload, &payload_len);
    cheng_host_mkdir_all_raw(raw, 0);
    f = fopen(raw, "wb");
    if (f == NULL) {
        free(raw);
        abort();
    }
    if (payload_len > 0u && fwrite(payload, 1, payload_len, f) != payload_len) {
        fclose(f);
        free(raw);
        abort();
    }
    fclose(f);
    free(raw);
}
