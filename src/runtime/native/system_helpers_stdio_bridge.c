#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "system_helpers.h"

extern char **environ;

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
