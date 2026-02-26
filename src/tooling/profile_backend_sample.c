#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_PS_ROWS 32768
#define MAX_LINE 1024

typedef struct {
  pid_t pid;
  pid_t ppid;
  char comm[256];
} ProcEntry;

typedef struct {
  int duration_sec;
  int interval_ms;
  int top_n;
  int kill_after_sample;
  int attach_timeout_sec;
  const char *preset;
  const char *out_path;
  const char *attach_substr;
  char *const *cmd_argv;
  const char *tag;
} Options;

static void usage(void) {
  fprintf(stderr,
          "Usage:\n"
          "  profile_backend_sample [--preset:<name>] [--duration:<sec>] [--interval-ms:<ms>]\n"
          "                         [--top:<n>] [--out:<path>] [--attach-substr:<name>]\n"
          "                         [--attach-timeout:<sec>] [--kill-after-sample]\n"
          "                         [--] [command ...]\n"
          "\n"
          "Presets:\n"
          "  selfhost-cold  env CLEAN_CHENG_LOCAL=0 SELF_OBJ_BOOTSTRAP_REUSE=0\n"
          "                 SELF_OBJ_BOOTSTRAP_SESSION=sample_cold\n"
          "                 sh src/tooling/tooling_exec.sh verify_backend_selfhost_bootstrap_self_obj\n"
          "\n"
          "  fullchain-cold env CLEAN_CHENG_LOCAL=0 FULLCHAIN_REUSE=0\n"
          "                 FULLCHAIN_TOOL_JOBS=3\n"
          "                 sh src/tooling/tooling_exec.sh verify_fullchain_bootstrap\n"
          "\n"
          "  closure-cold   env CLEAN_CHENG_LOCAL=0 BACKEND_PROD_SELFHOST_REUSE=0\n"
          "                 FULLCHAIN_REUSE=0 FULLCHAIN_TOOL_JOBS=3\n"
          "                 sh src/tooling/tooling_exec.sh backend_prod_closure --no-publish\n"
          "\n"
          "Notes:\n"
          "  - Requires macOS and /usr/bin/sample.\n"
          "  - Presets default to --attach-substr:cheng to target compiler child process.\n");
}

static int parse_positive_int(const char *text, int *out_value) {
  char *end = NULL;
  long parsed = strtol(text, &end, 10);
  if (text == NULL || text[0] == '\0' || end == NULL || *end != '\0') {
    return -1;
  }
  if (parsed <= 0 || parsed > 2147483647L) {
    return -1;
  }
  *out_value = (int)parsed;
  return 0;
}

static int starts_with(const char *text, const char *prefix) {
  size_t prefix_len = strlen(prefix);
  return strncmp(text, prefix, prefix_len) == 0;
}

static int ensure_dir_recursive(const char *dir_path) {
  char buffer[PATH_MAX];
  size_t len = strlen(dir_path);
  size_t i = 0;
  struct stat st;

  if (len == 0) {
    return 0;
  }
  if (len >= sizeof(buffer)) {
    return -1;
  }
  memcpy(buffer, dir_path, len + 1);
  if (buffer[len - 1] == '/') {
    buffer[len - 1] = '\0';
  }

  if (buffer[0] == '\0') {
    return 0;
  }

  for (i = 1; buffer[i] != '\0'; ++i) {
    if (buffer[i] == '/') {
      buffer[i] = '\0';
      if (buffer[0] != '\0') {
        if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
          return -1;
        }
      }
      buffer[i] = '/';
    }
  }

  if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
    return -1;
  }
  if (stat(buffer, &st) != 0 || !S_ISDIR(st.st_mode)) {
    return -1;
  }
  return 0;
}

static int ensure_parent_dir(const char *file_path) {
  char buffer[PATH_MAX];
  char *slash = NULL;

  if (strlen(file_path) >= sizeof(buffer)) {
    return -1;
  }
  strcpy(buffer, file_path);
  slash = strrchr(buffer, '/');
  if (slash == NULL) {
    return 0;
  }
  *slash = '\0';
  if (buffer[0] == '\0') {
    return 0;
  }
  return ensure_dir_recursive(buffer);
}

static void print_command(char *const *argv) {
  int index = 0;
  fputs("command:", stdout);
  while (argv[index] != NULL) {
    fputc(' ', stdout);
    fputs(argv[index], stdout);
    index += 1;
  }
  fputc('\n', stdout);
}

static int load_process_table(ProcEntry *entries, int max_entries) {
  FILE *stream = NULL;
  char line[MAX_LINE];
  int count = 0;

  stream = popen("ps -axo pid=,ppid=,comm=", "r");
  if (stream == NULL) {
    return 0;
  }

  while (fgets(line, sizeof(line), stream) != NULL) {
    char *cursor = line;
    char *end = NULL;
    long pid = 0;
    long ppid = 0;
    size_t comm_len = 0;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
      cursor += 1;
    }
    if (*cursor == '\0') {
      continue;
    }

    pid = strtol(cursor, &end, 10);
    if (end == cursor) {
      continue;
    }
    cursor = end;
    ppid = strtol(cursor, &end, 10);
    if (end == cursor) {
      continue;
    }
    cursor = end;
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
      cursor += 1;
    }
    if (*cursor == '\0') {
      continue;
    }
    comm_len = strcspn(cursor, "\r\n");
    if (count >= max_entries) {
      continue;
    }
    entries[count].pid = (pid_t)pid;
    entries[count].ppid = (pid_t)ppid;
    if (comm_len >= sizeof(entries[count].comm)) {
      comm_len = sizeof(entries[count].comm) - 1;
    }
    memcpy(entries[count].comm, cursor, comm_len);
    entries[count].comm[comm_len] = '\0';
    count += 1;
  }

  pclose(stream);
  return count;
}

static int find_entry_index_by_pid(const ProcEntry *entries, int count, pid_t pid) {
  int index = 0;
  for (index = 0; index < count; ++index) {
    if (entries[index].pid == pid) {
      return index;
    }
  }
  return -1;
}

static int descendant_depth(const ProcEntry *entries, int count, pid_t candidate_pid, pid_t root_pid) {
  pid_t current = candidate_pid;
  int depth = 0;
  int guard = 0;

  if (candidate_pid == root_pid) {
    return 0;
  }

  while (current > 1 && guard < 512) {
    int entry_index = find_entry_index_by_pid(entries, count, current);
    pid_t parent = 0;
    if (entry_index < 0) {
      return 0;
    }
    parent = entries[entry_index].ppid;
    depth += 1;
    if (parent == root_pid) {
      return depth;
    }
    if (parent <= 1 || parent == current) {
      return 0;
    }
    current = parent;
    guard += 1;
  }

  return 0;
}

static pid_t find_attach_pid(pid_t root_pid, const char *attach_substr, int *out_depth) {
  ProcEntry *entries = NULL;
  int count = 0;
  int index = 0;
  pid_t best_pid = 0;
  int best_depth = 0;
  int root_index = -1;

  entries = (ProcEntry *)calloc((size_t)MAX_PS_ROWS, sizeof(ProcEntry));
  if (entries == NULL) {
    return 0;
  }
  count = load_process_table(entries, MAX_PS_ROWS);
  root_index = find_entry_index_by_pid(entries, count, root_pid);
  if (root_index >= 0) {
    if (attach_substr == NULL || attach_substr[0] == '\0' || strstr(entries[root_index].comm, attach_substr) != NULL) {
      best_pid = root_pid;
      /* Depth is a score: 1=root, 2=direct child, 3+=deeper descendants. */
      best_depth = 1;
    }
  }
  for (index = 0; index < count; ++index) {
    int depth = 0;
    int depth_score = 0;
    if (entries[index].pid == root_pid) {
      continue;
    }
    depth = descendant_depth(entries, count, entries[index].pid, root_pid);
    if (depth <= 0) {
      continue;
    }
    if (attach_substr != NULL && strstr(entries[index].comm, attach_substr) == NULL) {
      continue;
    }
    depth_score = depth + 1;
    if (depth_score > best_depth || (depth_score == best_depth && entries[index].pid > best_pid)) {
      best_pid = entries[index].pid;
      best_depth = depth_score;
    }
  }

  free(entries);
  if (out_depth != NULL) {
    *out_depth = best_depth;
  }
  return best_pid;
}

static pid_t wait_attach_pid(pid_t root_pid, const Options *options) {
  int elapsed_ms = 0;
  int timeout_ms = options->attach_timeout_sec * 1000;
  const int step_ms = 100;
  pid_t found = 0;
  pid_t best_pid = 0;
  int best_depth = 0;
  int found_depth = 0;
  int first_found_ms = -1;
  const int observe_after_found_ms = 400;

  if (options->attach_substr == NULL || options->attach_substr[0] == '\0') {
    return root_pid;
  }

  while (elapsed_ms <= timeout_ms) {
    found_depth = 0;
    found = find_attach_pid(root_pid, options->attach_substr, &found_depth);
    if (found > 0) {
      best_pid = found;
      best_depth = found_depth;
      if (first_found_ms < 0) {
        first_found_ms = elapsed_ms;
      }
      /* If the target forks worker processes, prefer a deeper descendant. */
      if (best_depth >= 3) {
        return best_pid;
      }
      /* Don't lock onto the parent too early. Give workers time to spawn. */
      if (elapsed_ms - first_found_ms >= observe_after_found_ms) {
        return best_pid;
      }
    }
    if (kill(root_pid, 0) != 0) {
      break;
    }
    usleep((useconds_t)step_ms * 1000U);
    elapsed_ms += step_ms;
  }

  if (best_pid > 0) {
    return best_pid;
  }
  return root_pid;
}

static int run_sample_command(pid_t sample_pid, const Options *options) {
  char pid_text[32];
  char duration_text[32];
  char interval_text[32];
  char *sample_argv[8];
  pid_t child_pid = 0;
  int status = 0;

  snprintf(pid_text, sizeof(pid_text), "%d", (int)sample_pid);
  snprintf(duration_text, sizeof(duration_text), "%d", options->duration_sec);
  snprintf(interval_text, sizeof(interval_text), "%d", options->interval_ms);

  sample_argv[0] = "sample";
  sample_argv[1] = pid_text;
  sample_argv[2] = duration_text;
  sample_argv[3] = interval_text;
  sample_argv[4] = "-mayDie";
  sample_argv[5] = "-file";
  sample_argv[6] = (char *)options->out_path;
  sample_argv[7] = NULL;

  child_pid = fork();
  if (child_pid < 0) {
    return 1;
  }
  if (child_pid == 0) {
    execvp(sample_argv[0], sample_argv);
    _exit(127);
  }

  if (waitpid(child_pid, &status, 0) < 0) {
    return 1;
  }
  if (!WIFEXITED(status)) {
    return 1;
  }
  return WEXITSTATUS(status);
}

static void trim_newline(char *line) {
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
    line[len - 1] = '\0';
    len -= 1;
  }
}

static char *ltrim(char *line) {
  while (*line != '\0' && isspace((unsigned char)*line)) {
    line += 1;
  }
  return line;
}

static char *strip_call_graph_prefix(char *line) {
  char *cursor = ltrim(line);
  while (*cursor == '+' || *cursor == '!' || *cursor == ':' || *cursor == '|') {
    while (*cursor == '+' || *cursor == '!' || *cursor == ':' || *cursor == '|') {
      cursor += 1;
    }
    while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
      cursor += 1;
    }
  }
  return cursor;
}

static int is_noise_hotspot_frame(const char *text) {
  if (starts_with(text, "Thread_")) {
    return 1;
  }
  if (starts_with(text, "start  (in dyld)")) {
    return 1;
  }
  if (starts_with(text, "main  (in ")) {
    return 1;
  }
  return 0;
}

typedef struct {
  int count;
  char *text;
} HotspotFrame;

static int compare_hotspot_frame(const void *a, const void *b) {
  const HotspotFrame *left = (const HotspotFrame *)a;
  const HotspotFrame *right = (const HotspotFrame *)b;
  if (left->count != right->count) {
    return right->count - left->count;
  }
  if (left->text == NULL || right->text == NULL) {
    return 0;
  }
  return strcmp(left->text, right->text);
}

static int print_top_stack(const char *path, int top_n) {
  FILE *stream = fopen(path, "r");
  char line[MAX_LINE];
  int count = 0;
  int in_section = 0;
  if (stream == NULL) {
    return 0;
  }

  while (fgets(line, sizeof(line), stream) != NULL) {
    char *trimmed = NULL;
    trim_newline(line);
    if (!in_section) {
      if (starts_with(line, "Sort by top of stack, same collapsed")) {
        in_section = 1;
      }
      continue;
    }
    trimmed = ltrim(line);
    if (trimmed[0] == '\0') {
      break;
    }
    puts(trimmed);
    count += 1;
    if (count >= top_n) {
      break;
    }
  }

  fclose(stream);
  return count;
}

static int print_call_graph_fallback(const char *path, int top_n) {
  FILE *stream = fopen(path, "r");
  char line[MAX_LINE];
  HotspotFrame *frames = NULL;
  int frame_count = 0;
  int frame_capacity = 0;
  int in_section = 0;

  if (stream == NULL) {
    return 0;
  }

  while (fgets(line, sizeof(line), stream) != NULL) {
    char *cursor = NULL;
    char *end = NULL;
    long parsed = 0;
    trim_newline(line);
    if (!in_section) {
      if (starts_with(line, "Call graph:")) {
        in_section = 1;
      }
      continue;
    }
    if (starts_with(line, "Total number in stack")) {
      break;
    }
    cursor = strip_call_graph_prefix(line);
    if (!isdigit((unsigned char)cursor[0])) {
      continue;
    }
    parsed = strtol(cursor, &end, 10);
    if (end == cursor || parsed <= 0 || parsed > INT_MAX) {
      continue;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
      end += 1;
    }
    if (*end == '\0') {
      continue;
    }
    if (is_noise_hotspot_frame(end)) {
      continue;
    }
    if (frame_count >= frame_capacity) {
      int next_capacity = frame_capacity == 0 ? 256 : frame_capacity * 2;
      HotspotFrame *next = (HotspotFrame *)realloc(frames, sizeof(*frames) * next_capacity);
      if (next == NULL) {
        break;
      }
      frames = next;
      frame_capacity = next_capacity;
    }
    frames[frame_count].count = (int)parsed;
    frames[frame_count].text = strdup(end);
    if (frames[frame_count].text == NULL) {
      break;
    }
    frame_count += 1;
  }

  fclose(stream);
  if (frame_count == 0) {
    free(frames);
    return 0;
  }

  qsort(frames, (size_t)frame_count, sizeof(*frames), compare_hotspot_frame);
  int printed = 0;
  for (int index = 0; index < frame_count && printed < top_n; ++index) {
    if (frames[index].text == NULL) {
      continue;
    }
    printf("%d\t%s\n", frames[index].count, frames[index].text);
    printed += 1;
  }
  for (int index = 0; index < frame_count; ++index) {
    free(frames[index].text);
  }
  free(frames);
  return printed;
}

static const char *default_tag(const char *preset) {
  if (preset != NULL && preset[0] != '\0') {
    return preset;
  }
  return "command";
}

static int make_default_out_path(char *buffer, size_t size, const char *tag) {
  time_t now = time(NULL);
  struct tm local_tm;
  char stamp[32];
  if (localtime_r(&now, &local_tm) == NULL) {
    return -1;
  }
  if (strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local_tm) == 0) {
    return -1;
  }
  if (snprintf(buffer, size, "artifacts/profile/sample_%s_%s.txt", tag, stamp) >= (int)size) {
    return -1;
  }
  return 0;
}

static int pick_preset_command(const char *preset, char *const **out_argv, const char **out_default_attach) {
  static char *const selfhost[] = {
      "env",
      "CLEAN_CHENG_LOCAL=0",
      "SELF_OBJ_BOOTSTRAP_REUSE=0",
      "SELF_OBJ_BOOTSTRAP_SESSION=sample_cold",
      "sh",
      "src/tooling/verify_inline/verify_backend_selfhost_bootstrap_self_obj.inline",
      NULL,
  };
  static char *const fullchain[] = {
      "env",
      "CLEAN_CHENG_LOCAL=0",
      "FULLCHAIN_REUSE=0",
      "FULLCHAIN_TOOL_JOBS=3",
      "sh",
      "src/tooling/verify_inline/verify_fullchain_bootstrap.inline",
      NULL,
  };
  static char *const closure[] = {
      "env",
      "CLEAN_CHENG_LOCAL=0",
      "BACKEND_PROD_SELFHOST_REUSE=0",
      "FULLCHAIN_REUSE=0",
      "FULLCHAIN_TOOL_JOBS=3",
      "sh",
      "src/tooling/backend_prod_closure.sh",
      "--no-publish",
      NULL,
  };

  if (preset == NULL || preset[0] == '\0') {
    return -1;
  }
  if (strcmp(preset, "selfhost-cold") == 0) {
    *out_argv = selfhost;
    *out_default_attach = "cheng";
    return 0;
  }
  if (strcmp(preset, "fullchain-cold") == 0) {
    *out_argv = fullchain;
    *out_default_attach = "cheng";
    return 0;
  }
  if (strcmp(preset, "closure-cold") == 0) {
    *out_argv = closure;
    *out_default_attach = "cheng";
    return 0;
  }
  return -1;
}

static int parse_args(int argc, char **argv, Options *options) {
  int index = 1;
  char *const *user_cmd = NULL;
  char *const *preset_cmd = NULL;
  const char *preset_default_attach = NULL;

  options->duration_sec = 12;
  options->interval_ms = 1;
  options->top_n = 12;
  options->kill_after_sample = 0;
  options->attach_timeout_sec = 10;
  options->preset = "";
  options->out_path = "";
  options->attach_substr = "";
  options->cmd_argv = NULL;
  options->tag = "command";

  while (index < argc) {
    const char *argument = argv[index];
    if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
      usage();
      exit(0);
    } else if (strcmp(argument, "--kill-after-sample") == 0) {
      options->kill_after_sample = 1;
      index += 1;
      continue;
    } else if (strcmp(argument, "--") == 0) {
      if (index + 1 < argc) {
        user_cmd = &argv[index + 1];
      }
      break;
    } else if (starts_with(argument, "--preset:")) {
      options->preset = argument + strlen("--preset:");
    } else if (strcmp(argument, "--preset") == 0 && index + 1 < argc) {
      options->preset = argv[++index];
    } else if (starts_with(argument, "--duration:")) {
      if (parse_positive_int(argument + strlen("--duration:"), &options->duration_sec) != 0) {
        return -1;
      }
    } else if (strcmp(argument, "--duration") == 0 && index + 1 < argc) {
      if (parse_positive_int(argv[++index], &options->duration_sec) != 0) {
        return -1;
      }
    } else if (starts_with(argument, "--interval-ms:")) {
      if (parse_positive_int(argument + strlen("--interval-ms:"), &options->interval_ms) != 0) {
        return -1;
      }
    } else if (strcmp(argument, "--interval-ms") == 0 && index + 1 < argc) {
      if (parse_positive_int(argv[++index], &options->interval_ms) != 0) {
        return -1;
      }
    } else if (starts_with(argument, "--top:")) {
      if (parse_positive_int(argument + strlen("--top:"), &options->top_n) != 0) {
        return -1;
      }
    } else if (strcmp(argument, "--top") == 0 && index + 1 < argc) {
      if (parse_positive_int(argv[++index], &options->top_n) != 0) {
        return -1;
      }
    } else if (starts_with(argument, "--out:")) {
      options->out_path = argument + strlen("--out:");
    } else if (strcmp(argument, "--out") == 0 && index + 1 < argc) {
      options->out_path = argv[++index];
    } else if (starts_with(argument, "--attach-substr:")) {
      options->attach_substr = argument + strlen("--attach-substr:");
    } else if (strcmp(argument, "--attach-substr") == 0 && index + 1 < argc) {
      options->attach_substr = argv[++index];
    } else if (starts_with(argument, "--attach-timeout:")) {
      if (parse_positive_int(argument + strlen("--attach-timeout:"), &options->attach_timeout_sec) != 0) {
        return -1;
      }
    } else if (strcmp(argument, "--attach-timeout") == 0 && index + 1 < argc) {
      if (parse_positive_int(argv[++index], &options->attach_timeout_sec) != 0) {
        return -1;
      }
    } else if (starts_with(argument, "--")) {
      return -1;
    } else {
      user_cmd = &argv[index];
      break;
    }
    index += 1;
  }

  if (user_cmd != NULL) {
    options->cmd_argv = user_cmd;
    options->tag = "command";
    return 0;
  }

  if (pick_preset_command(options->preset, &preset_cmd, &preset_default_attach) != 0) {
    return -1;
  }
  options->cmd_argv = preset_cmd;
  options->tag = default_tag(options->preset);
  if (options->attach_substr[0] == '\0' && preset_default_attach != NULL) {
    options->attach_substr = preset_default_attach;
  }
  return 0;
}

int main(int argc, char **argv) {
  Options options;
  char default_out[PATH_MAX];
  struct utsname uts;
  int target_status = 0;
  int sample_status = 0;
  pid_t target_pid = 0;
  pid_t sample_pid = 0;
  int wait_status = 0;

  if (parse_args(argc, argv, &options) != 0 || options.cmd_argv == NULL || options.cmd_argv[0] == NULL) {
    usage();
    return 2;
  }

  if (strcmp(getenv("PROFILE_SAMPLE_SKIP_OS_CHECK") == NULL ? "" : getenv("PROFILE_SAMPLE_SKIP_OS_CHECK"), "1") != 0) {
    if (uname(&uts) != 0) {
      fprintf(stderr, "[Error] failed to detect host OS\n");
      return 2;
    }
    if (strcmp(uts.sysname, "Darwin") != 0) {
      fprintf(stderr, "[Error] profile_backend_sample requires Darwin/macOS (sample tool)\n");
      return 2;
    }
  }

  if (access("/usr/bin/sample", X_OK) != 0) {
    fprintf(stderr, "[Error] missing sample tool\n");
    return 2;
  }

  if (options.out_path == NULL || options.out_path[0] == '\0') {
    if (make_default_out_path(default_out, sizeof(default_out), options.tag) != 0) {
      fprintf(stderr, "[Error] failed to construct output path\n");
      return 2;
    }
    options.out_path = default_out;
  }
  if (ensure_parent_dir(options.out_path) != 0) {
    fprintf(stderr, "[Error] failed to create output directory for %s\n", options.out_path);
    return 2;
  }

  puts("== sample.target ==");
  print_command(options.cmd_argv);
  printf("duration: %ds interval: %dms\n", options.duration_sec, options.interval_ms);
  printf("output: %s\n", options.out_path);

  target_pid = fork();
  if (target_pid < 0) {
    fprintf(stderr, "[Error] failed to start target process\n");
    return 1;
  }
  if (target_pid == 0) {
    execvp(options.cmd_argv[0], options.cmd_argv);
    _exit(127);
  }

  usleep(200000U);
  if (kill(target_pid, 0) != 0) {
    fprintf(stderr, "[Error] target exited before sample started (pid=%d)\n", (int)target_pid);
    if (waitpid(target_pid, &wait_status, 0) >= 0) {
      if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
      }
    }
    return 1;
  }

  sample_pid = wait_attach_pid(target_pid, &options);
  if (sample_pid <= 0) {
    sample_pid = target_pid;
  }
  printf("attach_substr: %s\n", options.attach_substr[0] == '\0' ? "(none)" : options.attach_substr);
  printf("sample_pid: %d (root_pid=%d)\n", (int)sample_pid, (int)target_pid);

  sample_status = run_sample_command(sample_pid, &options);
  if (sample_status != 0) {
    fprintf(stderr, "[warn] sample exit status=%d\n", sample_status);
  }

  if (options.kill_after_sample) {
    kill(target_pid, SIGTERM);
  }

  if (waitpid(target_pid, &wait_status, 0) < 0) {
    target_status = 1;
  } else if (WIFEXITED(wait_status)) {
    target_status = WEXITSTATUS(wait_status);
  } else if (WIFSIGNALED(wait_status)) {
    target_status = 128 + WTERMSIG(wait_status);
  } else {
    target_status = 1;
  }

  printf("== sample.hotspots.top_stack (top=%d) ==\n", options.top_n);
  if (print_top_stack(options.out_path, options.top_n) == 0) {
    puts("(empty)");
  }
  printf("== sample.hotspots.call_graph (top=%d) ==\n", options.top_n);
  print_call_graph_fallback(options.out_path, options.top_n);

  printf("target_exit=%d sample_exit=%d\n", target_status, sample_status);
  if (options.kill_after_sample && target_status == (128 + SIGTERM)) {
    return sample_status;
  }
  if (target_status != 0) {
    return target_status;
  }
  return sample_status;
}
