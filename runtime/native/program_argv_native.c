#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern int32_t cheng_program_argv_entry(int32_t argc, const char **argv);
extern void cheng_register_line_map_from_argv0(const char *argv0) __attribute__((weak));

typedef struct ChengSeqHeader {
  int32_t len;
  int32_t cap;
  void *buffer;
} ChengSeqHeader;

static char *cheng_exec_dup_empty_string(void) {
  char *out = (char *)malloc(1);
  if (out != NULL) {
    out[0] = '\0';
  }
  return out;
}

static const char *cheng_exec_seq_string_item(ChengSeqHeader seq, int32_t idx) {
  if (idx < 0 || idx >= seq.len || seq.buffer == NULL) {
    return "";
  }
  {
    char **items = (char **)seq.buffer;
    const char *value = items[idx];
    return value != NULL ? value : "";
  }
}

static char **cheng_exec_build_program_argv(const char *program_name, void *argv_seq_ptr) {
  ChengSeqHeader seq;
  size_t extra_count = 0U;
  size_t i = 0U;
  char **argv = NULL;
  memset(&seq, 0, sizeof(seq));
  if (argv_seq_ptr != NULL) {
    seq = *(ChengSeqHeader *)argv_seq_ptr;
  }
  extra_count = (size_t)(seq.len > 0 ? seq.len : 0);
  argv = (char **)malloc(sizeof(char *) * (extra_count + 2U));
  if (argv == NULL) {
    return NULL;
  }
  argv[0] = (char *)(program_name != NULL ? program_name : "");
  for (i = 0U; i < extra_count; i += 1U) {
    argv[i + 1U] = (char *)cheng_exec_seq_string_item(seq, (int32_t)i);
  }
  argv[extra_count + 1U] = NULL;
  return argv;
}

static char *cheng_exec_read_file_all(const char *path) {
  FILE *f = NULL;
  char *output = NULL;
  size_t used = 0U;
  size_t cap = 0U;
  f = fopen(path, "rb");
  if (f == NULL) {
    return cheng_exec_dup_empty_string();
  }
  for (;;) {
    char chunk[4096];
    size_t got = fread(chunk, 1U, sizeof(chunk), f);
    if (got > 0U) {
      size_t need = used + got + 1U;
      char *next = NULL;
      if (need > cap) {
        size_t next_cap = cap > 0U ? cap : 4096U;
        while (next_cap < need) {
          next_cap *= 2U;
        }
        next = (char *)realloc(output, next_cap);
        if (next == NULL) {
          free(output);
          fclose(f);
          return cheng_exec_dup_empty_string();
        }
        output = next;
        cap = next_cap;
      }
      memcpy(output + used, chunk, got);
      used += got;
      output[used] = '\0';
    }
    if (got < sizeof(chunk)) {
      break;
    }
  }
  fclose(f);
  if (output == NULL) {
    output = cheng_exec_dup_empty_string();
  }
  return output;
}

char *cheng_exec_program_capture_bridge(const char *programName,
                                           void *argvSeqPtr,
                                           const char *workingDir,
                                           int64_t *exitCode) {
  char capture_path[] = "/tmp/cheng_exec_capture.XXXXXX";
  char **argv = NULL;
  int capture_fd = -1;
  pid_t pid = -1;
  int wait_status = 0;
  char *output = NULL;
  if (exitCode != NULL) {
    *exitCode = -1;
  }
  if (programName == NULL || programName[0] == '\0') {
    return cheng_exec_dup_empty_string();
  }
  argv = cheng_exec_build_program_argv(programName, argvSeqPtr);
  if (argv == NULL) {
    return cheng_exec_dup_empty_string();
  }
  capture_fd = mkstemp(capture_path);
  if (capture_fd < 0) {
    free(argv);
    return cheng_exec_dup_empty_string();
  }
  pid = fork();
  if (pid < 0) {
    close(capture_fd);
    unlink(capture_path);
    free(argv);
    return cheng_exec_dup_empty_string();
  }
  if (pid == 0) {
    if (workingDir != NULL && workingDir[0] != '\0') {
      (void)chdir(workingDir);
    }
    (void)dup2(capture_fd, STDOUT_FILENO);
    (void)dup2(capture_fd, STDERR_FILENO);
    close(capture_fd);
    execvp(programName, argv);
    {
      const char *msg = strerror(errno);
      if (msg == NULL) {
        msg = "execvp failed";
      }
      dprintf(STDERR_FILENO, "%s\n", msg);
    }
    _exit(127);
  }
  close(capture_fd);
  capture_fd = -1;
  while (waitpid(pid, &wait_status, 0) < 0) {
    if (errno != EINTR) {
      wait_status = -1;
      break;
    }
  }
  if (exitCode != NULL) {
    if (wait_status == -1) {
      *exitCode = -1;
    } else if (WIFEXITED(wait_status)) {
      *exitCode = (int64_t)WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
      *exitCode = (int64_t)(128 + WTERMSIG(wait_status));
    } else {
      *exitCode = (int64_t)wait_status;
    }
  }
  free(argv);
  output = cheng_exec_read_file_all(capture_path);
  unlink(capture_path);
  return output;
}

char *cheng_exec_cmd_ex(const char *command, const char *workingDir, int32_t mergeStderr, int64_t *exitCode) {
  char cwd_buf[4096];
  int have_cwd = 0;
  FILE *pipe = NULL;
  char *output = NULL;
  size_t used = 0;
  size_t cap = 0;
  int close_status = 0;
  const char *command_text = command != NULL ? command : "";
  char *shell_command = NULL;
  size_t shell_len = 0;
  if (exitCode != NULL) {
    *exitCode = -1;
  }
  if (workingDir != NULL && workingDir[0] != '\0') {
    if (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) {
      have_cwd = 1;
    }
    if (chdir(workingDir) != 0) {
      return cheng_exec_dup_empty_string();
    }
  }
  shell_len = strlen(command_text) + 8;
  shell_command = (char *)malloc(shell_len);
  if (shell_command == NULL) {
    if (have_cwd) {
      chdir(cwd_buf);
    }
    return cheng_exec_dup_empty_string();
  }
  snprintf(shell_command,
           shell_len,
           "%s%s",
           command_text,
           mergeStderr ? " 2>&1" : "");
  pipe = popen(shell_command, "r");
  free(shell_command);
  if (pipe == NULL) {
    if (have_cwd) {
      chdir(cwd_buf);
    }
    return cheng_exec_dup_empty_string();
  }
  for (;;) {
    char chunk[4096];
    size_t got = fread(chunk, 1U, sizeof(chunk), pipe);
    if (got > 0U) {
      size_t need = used + got + 1U;
      char *next;
      if (need > cap) {
        size_t next_cap = cap > 0U ? cap : 4096U;
        while (next_cap < need) {
          next_cap *= 2U;
        }
        next = (char *)realloc(output, next_cap);
        if (next == NULL) {
          free(output);
          pclose(pipe);
          if (have_cwd) {
            chdir(cwd_buf);
          }
          return cheng_exec_dup_empty_string();
        }
        output = next;
        cap = next_cap;
      }
      memcpy(output + used, chunk, got);
      used += got;
      output[used] = '\0';
    }
    if (got < sizeof(chunk)) {
      break;
    }
  }
  close_status = pclose(pipe);
  if (have_cwd) {
    chdir(cwd_buf);
  }
  if (output == NULL) {
    output = cheng_exec_dup_empty_string();
  }
  if (exitCode != NULL) {
    if (close_status == -1) {
      *exitCode = -1;
    } else if (WIFEXITED(close_status)) {
      *exitCode = (int64_t)WEXITSTATUS(close_status);
    } else if (WIFSIGNALED(close_status)) {
      *exitCode = (int64_t)(128 + WTERMSIG(close_status));
    } else {
      *exitCode = (int64_t)close_status;
    }
  }
  return output;
}

int main(int argc, char **argv) {
  if (cheng_register_line_map_from_argv0 != 0) {
    cheng_register_line_map_from_argv0((argc > 0 && argv != 0) ? argv[0] : 0);
  }
  return cheng_program_argv_entry((int32_t)argc, (const char **)argv);
}
