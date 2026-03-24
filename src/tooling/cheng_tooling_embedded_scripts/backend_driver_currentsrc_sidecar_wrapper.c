#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int file_exec_exists(const char *path) {
  return path != NULL && path[0] != '\0' && access(path, X_OK) == 0;
}

static int file_exists(const char *path) {
  return path != NULL && path[0] != '\0' && access(path, F_OK) == 0;
}

static void parent_dir_inplace(char *path) {
  size_t len = strlen(path);
  while (len > 0 && path[len - 1] == '/') {
    path[--len] = '\0';
  }
  while (len > 0 && path[len - 1] != '/') {
    path[--len] = '\0';
  }
  while (len > 1 && path[len - 1] == '/') {
    path[--len] = '\0';
  }
}

static void join_path4(char *out, size_t out_cap,
                       const char *a, const char *b, const char *c, const char *d) {
  snprintf(out, out_cap, "%s/%s/%s/%s", a, b, c, d);
}

static void join_path5(char *out, size_t out_cap, const char *a, const char *b,
                       const char *c, const char *d, const char *e) {
  snprintf(out, out_cap, "%s/%s/%s/%s/%s", a, b, c, d, e);
}

static int read_kv_field(const char *path, const char *key, char *out, size_t out_cap) {
  FILE *fp;
  char line[PATH_MAX * 2];
  size_t key_len;
  if (path == NULL || path[0] == '\0' || key == NULL || key[0] == '\0' || out == NULL ||
      out_cap == 0) {
    return 0;
  }
  fp = fopen(path, "r");
  if (fp == NULL) return 0;
  key_len = strlen(key);
  while (fgets(line, sizeof(line), fp) != NULL) {
    char *value;
    size_t len;
    if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') continue;
    value = line + key_len + 1;
    len = strlen(value);
    while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
      value[len - 1] = '\0';
      len -= 1;
    }
    snprintf(out, out_cap, "%s", value);
    fclose(fp);
    return out[0] != '\0';
  }
  fclose(fp);
  return 0;
}

static int path_is_under_root(const char *root, const char *path) {
  size_t root_len;
  if (root == NULL || root[0] == '\0' || path == NULL || path[0] == '\0') return 0;
  root_len = strlen(root);
  if (strncmp(path, root, root_len) != 0) return 0;
  return path[root_len] == '/' || path[root_len] == '\0';
}

static int resolve_snapshot_field(const char *root,
                                  const char *key,
                                  char *out,
                                  size_t out_cap) {
  char snapshot[PATH_MAX];
  char status[64];
  char mode[64];
  if (root == NULL || root[0] == '\0') return 0;
  join_path5(snapshot, sizeof(snapshot), root, "artifacts", "backend_sidecar_cheng_fresh",
             "verify_backend_sidecar_cheng_fresh.snapshot.env", "");
  if (snapshot[strlen(snapshot) - 1] == '/') {
    snapshot[strlen(snapshot) - 1] = '\0';
  }
  if (!file_exists(snapshot)) return 0;
  if (!read_kv_field(snapshot, "backend_sidecar_cheng_fresh_status", status, sizeof(status)) ||
      strcmp(status, "ok") != 0) {
    return 0;
  }
  if (!read_kv_field(snapshot, "backend_sidecar_cheng_fresh_mode", mode, sizeof(mode)) ||
      strcmp(mode, "cheng") != 0) {
    return 0;
  }
  if (!read_kv_field(snapshot, key, out, out_cap)) return 0;
  if (!path_is_under_root(root, out)) return 0;
  return 1;
}

static const char *host_target_suffix(void) {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
  return "arm64-apple-darwin";
#elif defined(__APPLE__) && defined(__x86_64__)
  return "x86_64-apple-darwin";
#elif defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
  return "aarch64-unknown-linux-gnu";
#elif defined(__linux__) && defined(__x86_64__)
  return "x86_64-unknown-linux-gnu";
#else
  return NULL;
#endif
}

static int run_wrapper(char **argv) {
  char exe_real[PATH_MAX];
  char cwd[PATH_MAX];
  char root[PATH_MAX];
  char root_probe[PATH_MAX];
  const char *env_driver = getenv("TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER");
  const char *real_driver = NULL;
  root[0] = '\0';

  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    join_path4(root_probe, sizeof(root_probe), cwd, "src", "tooling",
               "cheng_tooling_embedded_scripts");
    if (file_exists(root_probe)) {
      snprintf(root, sizeof(root), "%s", cwd);
    }
  }
  if (root[0] == '\0') {
    if (realpath(argv[0], exe_real) == NULL) {
      perror("backend_driver_currentsrc_sidecar_wrapper: realpath");
      return 1;
    }
    snprintf(root, sizeof(root), "%s", exe_real);
    parent_dir_inplace(root);
    parent_dir_inplace(root);
    parent_dir_inplace(root);
  }

  if (file_exec_exists(env_driver)) {
    real_driver = env_driver;
  }
  if (real_driver == NULL) {
    fprintf(stderr,
            "[backend_driver_currentsrc_sidecar_wrapper] missing explicit current-source real driver\n");
    return 1;
  }

  setenv("BACKEND_UIR_SIDECAR_REPO_ROOT", root, 1);

  argv[0] = (char *)real_driver;
  execv(real_driver, argv);
  perror("backend_driver_currentsrc_sidecar_wrapper: execv");
  return 1;
}

int backendMain(void) {
  return 0;
}

int main(int argc, char **argv) {
  (void)argc;
  return run_wrapper(argv);
}
