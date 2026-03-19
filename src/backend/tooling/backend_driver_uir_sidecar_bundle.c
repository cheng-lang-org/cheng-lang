#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef struct DriverUirSidecarHandle {
  char *input_path;
  char *rewrite_path;
  char *target;
  char *compiler;
} DriverUirSidecarHandle;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int driver_sidecar_str_non_empty(const char *s) {
  return s != NULL && s[0] != '\0';
}

static int driver_sidecar_copy_path(char *out, size_t out_size, const char *src) {
  size_t n;
  if (out == NULL || out_size == 0 || src == NULL) return 0;
  n = strlen(src);
  if (n + 1 > out_size) return 0;
  memcpy(out, src, n + 1);
  return 1;
}

static int driver_sidecar_join_path(char *out, size_t out_size,
                                    const char *lhs, const char *rhs) {
  size_t lhs_len;
  size_t rhs_len;
  size_t need;
  if (out == NULL || out_size == 0 || lhs == NULL || rhs == NULL) return 0;
  lhs_len = strlen(lhs);
  rhs_len = strlen(rhs);
  need = lhs_len + rhs_len + 2;
  if (need > out_size) return 0;
  memcpy(out, lhs, lhs_len);
  if (lhs_len > 0 && lhs[lhs_len - 1] != '/') {
    out[lhs_len] = '/';
    lhs_len += 1;
  }
  memcpy(out + lhs_len, rhs, rhs_len);
  out[lhs_len + rhs_len] = '\0';
  return 1;
}

static int driver_sidecar_path_is_dir(const char *path) {
  struct stat st;
  if (!driver_sidecar_str_non_empty(path)) return 0;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode);
}

static void driver_sidecar_parent_dir(char *path) {
  size_t n;
  if (!driver_sidecar_str_non_empty(path)) return;
  n = strlen(path);
  while (n > 1 && path[n - 1] == '/') {
    path[n - 1] = '\0';
    n -= 1;
  }
  while (n > 0 && path[n - 1] != '/') {
    n -= 1;
  }
  if (n == 0) {
    path[0] = '\0';
    return;
  }
  while (n > 1 && path[n - 1] == '/') {
    n -= 1;
  }
  path[n] = '\0';
}

static int driver_sidecar_make_absolute(const char *raw, char *out, size_t out_size) {
  char cwd[PATH_MAX];
  if (!driver_sidecar_str_non_empty(raw) || out == NULL || out_size == 0) return 0;
  if (raw[0] == '/') return driver_sidecar_copy_path(out, out_size, raw);
  if (getcwd(cwd, sizeof(cwd)) == NULL) return 0;
  return driver_sidecar_join_path(out, out_size, cwd, raw);
}

static int driver_sidecar_has_repo_marker(const char *root) {
  char marker[PATH_MAX];
  if (!driver_sidecar_str_non_empty(root)) return 0;
  if (!driver_sidecar_join_path(marker, sizeof(marker), root, "src/tooling/cheng_tooling.cheng")) {
    return 0;
  }
  return access(marker, R_OK) == 0;
}

static int driver_sidecar_find_repo_root_from_path(const char *raw, char *out, size_t out_size) {
  char probe[PATH_MAX];
  int depth = 0;
  if (!driver_sidecar_make_absolute(raw, probe, sizeof(probe))) return 0;
  if (!driver_sidecar_path_is_dir(probe)) {
    driver_sidecar_parent_dir(probe);
  }
  while (driver_sidecar_str_non_empty(probe) && depth < 16) {
    if (driver_sidecar_has_repo_marker(probe)) {
      return driver_sidecar_copy_path(out, out_size, probe);
    }
    driver_sidecar_parent_dir(probe);
    depth += 1;
  }
  return 0;
}

static int driver_sidecar_resolve_repo_root(const char *input_path,
                                            const char *compiler_path,
                                            char *out,
                                            size_t out_size) {
  const char *env_root = getenv("BACKEND_UIR_SIDECAR_REPO_ROOT");
  char cwd[PATH_MAX];
  if (driver_sidecar_find_repo_root_from_path(env_root, out, out_size)) return 1;
  env_root = getenv("TOOLING_ROOT");
  if (driver_sidecar_find_repo_root_from_path(env_root, out, out_size)) return 1;
  if (driver_sidecar_find_repo_root_from_path(input_path, out, out_size)) return 1;
  if (driver_sidecar_find_repo_root_from_path(compiler_path, out, out_size)) return 1;
  env_root = getenv("BACKEND_UIR_SIDECAR_COMPILER");
  if (driver_sidecar_find_repo_root_from_path(env_root, out, out_size)) return 1;
  env_root = getenv("PWD");
  if (driver_sidecar_find_repo_root_from_path(env_root, out, out_size)) return 1;
  if (getcwd(cwd, sizeof(cwd)) != NULL &&
      driver_sidecar_find_repo_root_from_path(cwd, out, out_size)) {
    return 1;
  }
  return 0;
}

static int driver_sidecar_build_repo_path(const char *input_path,
                                          const char *compiler_path,
                                          const char *relative_path,
                                          char *out,
                                          size_t out_size) {
  char root[PATH_MAX];
  if (!driver_sidecar_resolve_repo_root(input_path, compiler_path, root, sizeof(root))) return 0;
  return driver_sidecar_join_path(out, out_size, root, relative_path);
}

static const char *driver_sidecar_default_stage0_for_input(const char *input_path,
                                                           const char *compiler_path) {
  static char path[PATH_MAX];
  if (!driver_sidecar_build_repo_path(input_path, compiler_path,
                                      "artifacts/backend_selfhost_self_obj/cheng_stage0_default",
                                      path, sizeof(path))) {
    return NULL;
  }
  return path;
}

static int driver_sidecar_find_latest_preserved_attempt(const char *input_path,
                                                        const char *compiler_path,
                                                        char *out,
                                                        size_t out_size) {
  char dir_path[PATH_MAX];
  DIR *dir;
  struct dirent *entry;
  struct stat st;
  time_t best_mtime = (time_t)0;
  int found = 0;
  if (!driver_sidecar_build_repo_path(input_path, compiler_path,
                                      "chengcache/backend_driver_build_tmp",
                                      dir_path, sizeof(dir_path))) {
    return 0;
  }
  dir = opendir(dir_path);
  if (dir == NULL) return 0;
  while ((entry = readdir(dir)) != NULL) {
    char candidate[PATH_MAX];
    if (strncmp(entry->d_name, "cheng.attempt.", 14) != 0) continue;
    if (!driver_sidecar_join_path(candidate, sizeof(candidate), dir_path, entry->d_name)) continue;
    if (access(candidate, X_OK) != 0) continue;
    if (stat(candidate, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if (!found || st.st_mtime > best_mtime) {
      if (!driver_sidecar_copy_path(out, out_size, candidate)) continue;
      best_mtime = st.st_mtime;
      found = 1;
    }
  }
  closedir(dir);
  return found;
}

static const char *driver_sidecar_default_preserved_attempt_for_input(const char *input_path,
                                                                      const char *compiler_path) {
  static char path[PATH_MAX];
  if (!driver_sidecar_find_latest_preserved_attempt(input_path, compiler_path,
                                                    path, sizeof(path))) {
    return NULL;
  }
  return path;
}

static const char *driver_sidecar_default_currentsrc_proof_stage0_for_input(
    const char *input_path, const char *compiler_path) {
  static char path[PATH_MAX];
  if (!driver_sidecar_build_repo_path(
          input_path, compiler_path,
          "artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof",
          path, sizeof(path))) {
    return NULL;
  }
  return path;
}

static const char *driver_sidecar_default_currentsrc_proof_outer_for_input(
    const char *input_path, const char *compiler_path) {
  static char path[PATH_MAX];
  if (!driver_sidecar_build_repo_path(
          input_path, compiler_path,
          "artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2",
          path, sizeof(path))) {
    return NULL;
  }
  return path;
}

static const char *driver_sidecar_default_dist_release_for_input(const char *input_path,
                                                                 const char *compiler_path) {
  static char path[PATH_MAX];
  if (!driver_sidecar_build_repo_path(input_path, compiler_path,
                                      "dist/releases/current/cheng",
                                      path, sizeof(path))) {
    return NULL;
  }
  return path;
}

static const char *driver_sidecar_default_preferred_release_for_input(
    const char *input_path, const char *compiler_path) {
  static char path[PATH_MAX];
  if (!driver_sidecar_build_repo_path(
          input_path, compiler_path,
          "dist/releases/2026-02-23T09_54_03Z_e84f22d_14/cheng",
          path, sizeof(path))) {
    return NULL;
  }
  return path;
}

static const char *driver_sidecar_default_canonical_for_input(const char *input_path,
                                                              const char *compiler_path) {
  static char path[PATH_MAX];
  if (!driver_sidecar_build_repo_path(input_path, compiler_path,
                                      "artifacts/backend_driver/cheng",
                                      path, sizeof(path))) {
    return NULL;
  }
  return path;
}

static const char *driver_sidecar_mode(void) {
  const char *mode = getenv("BACKEND_UIR_SIDECAR_MODE");
  if (mode == NULL || mode[0] == '\0') return "emergency_c";
  return mode;
}

static int driver_sidecar_mode_is_emergency_c(void) {
  return strcmp(driver_sidecar_mode(), "emergency_c") == 0;
}

static int driver_sidecar_debug_enabled(void) {
  const char *raw = getenv("BACKEND_DEBUG_BOOT");
  if (raw != NULL && raw[0] != '\0' && strcmp(raw, "0") != 0) return 1;
  raw = getenv("BACKEND_DEBUG_SIDECAR");
  if (raw != NULL && raw[0] != '\0' && strcmp(raw, "0") != 0) return 1;
  return 0;
}

static int driver_sidecar_keep_rewrite_enabled(void) {
  const char *raw = getenv("BACKEND_DEBUG_SIDECAR_KEEP_REWRITE");
  if (raw == NULL || raw[0] == '\0' || strcmp(raw, "0") == 0) return 0;
  return 1;
}

static const char *driver_sidecar_python_bin(void) {
  const char *raw = getenv("BACKEND_UIR_SIDECAR_PYTHON");
  if (raw == NULL || raw[0] == '\0') return "python3";
  return raw;
}

static int driver_sidecar_target_is_darwin(const char *target) {
  return target != NULL && strstr(target, "darwin") != NULL;
}

static int driver_sidecar_should_prefer_preserved_attempt(const char *input_path) {
  if (input_path == NULL || input_path[0] == '\0') return 0;
  return strstr(input_path, "/src/tooling/cheng_tooling.cheng") != NULL;
}

static int driver_sidecar_should_prefer_currentsrc_proof_child(const char *input_path) {
  if (input_path == NULL || input_path[0] == '\0') return 0;
  return strstr(input_path, "/src/backend/tooling/backend_driver_proof.cheng") != NULL;
}

static int driver_sidecar_compiler_requires_outer_cli_mode(const char *compiler) {
  if (compiler == NULL || compiler[0] == '\0') return 0;
  return strstr(
             compiler,
             "/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof") != NULL;
}

static int driver_sidecar_compiler_prefers_currentsrc_outer(const char *compiler) {
  return driver_sidecar_compiler_requires_outer_cli_mode(compiler);
}

static int driver_sidecar_compiler_is_currentsrc_proof_lineage(const char *compiler) {
  if (compiler == NULL || compiler[0] == '\0') return 0;
  if (strstr(
          compiler,
          "/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2") != NULL) {
    return 1;
  }
  return driver_sidecar_compiler_requires_outer_cli_mode(compiler);
}

static int driver_sidecar_compiler_supports_new_expr_assignments(const char *compiler) {
  if (compiler == NULL || compiler[0] == '\0') return 0;
  if (driver_sidecar_compiler_is_currentsrc_proof_lineage(compiler)) return 1;
  return 0;
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
  if (!driver_sidecar_keep_rewrite_enabled() &&
      h->rewrite_path != NULL && h->rewrite_path[0] != '\0') unlink(h->rewrite_path);
  free(h->input_path);
  free(h->rewrite_path);
  free(h->target);
  free(h->compiler);
  free(h);
}

typedef struct DriverSidecarStr {
  char *data;
  size_t len;
  size_t cap;
} DriverSidecarStr;

static void driver_sidecar_str_free(DriverSidecarStr *s) {
  if (s == NULL) return;
  free(s->data);
  s->data = NULL;
  s->len = 0;
  s->cap = 0;
}

static int driver_sidecar_str_reserve(DriverSidecarStr *s, size_t need) {
  size_t next_cap;
  char *next_data;
  if (s == NULL) return 0;
  if (need <= s->cap) return 1;
  next_cap = s->cap > 0 ? s->cap : 256;
  while (next_cap < need) {
    if (next_cap > ((size_t)-1) / 2) return 0;
    next_cap *= 2;
  }
  next_data = (char *)realloc(s->data, next_cap);
  if (next_data == NULL) return 0;
  s->data = next_data;
  s->cap = next_cap;
  return 1;
}

static int driver_sidecar_str_append_n(DriverSidecarStr *s, const char *data, size_t n) {
  if (s == NULL) return 0;
  if (n == 0) return 1;
  if (!driver_sidecar_str_reserve(s, s->len + n + 1)) return 0;
  memcpy(s->data + s->len, data, n);
  s->len += n;
  s->data[s->len] = '\0';
  return 1;
}

static int driver_sidecar_str_append(DriverSidecarStr *s, const char *data) {
  if (data == NULL) return 1;
  return driver_sidecar_str_append_n(s, data, strlen(data));
}

static int driver_sidecar_is_ident_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

static char *driver_sidecar_dup_n(const char *s, size_t n) {
  char *out;
  if (s == NULL) return NULL;
  out = (char *)malloc(n + 1);
  if (out == NULL) return NULL;
  if (n > 0) memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

static char *driver_sidecar_trim_dup_n(const char *s, size_t n) {
  size_t start = 0;
  size_t end = n;
  while (start < n && (s[start] == ' ' || s[start] == '\t')) start += 1;
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) end -= 1;
  return driver_sidecar_dup_n(s + start, end - start);
}

static int driver_sidecar_trim_eq_n(const char *a, size_t an, const char *b, size_t bn) {
  char *ta = driver_sidecar_trim_dup_n(a, an);
  char *tb = driver_sidecar_trim_dup_n(b, bn);
  int ok = 0;
  if (ta != NULL && tb != NULL && strcmp(ta, tb) == 0) ok = 1;
  free(ta);
  free(tb);
  return ok;
}

static char *driver_sidecar_replace_all(const char *src, const char *needle, const char *repl) {
  DriverSidecarStr out = {0};
  size_t needle_len;
  size_t repl_len;
  const char *p;
  const char *hit;
  if (src == NULL || needle == NULL || repl == NULL) return NULL;
  needle_len = strlen(needle);
  repl_len = strlen(repl);
  if (needle_len == 0) return driver_sidecar_dup(src);
  p = src;
  for (;;) {
    hit = strstr(p, needle);
    if (hit == NULL) {
      if (!driver_sidecar_str_append(&out, p)) {
        driver_sidecar_str_free(&out);
        return NULL;
      }
      break;
    }
    if (!driver_sidecar_str_append_n(&out, p, (size_t)(hit - p)) ||
        !driver_sidecar_str_append_n(&out, repl, repl_len)) {
      driver_sidecar_str_free(&out);
      return NULL;
    }
    p = hit + needle_len;
  }
  return out.data;
}

static char *driver_sidecar_sanitize_alias_suffix(const char *raw) {
  size_t i;
  size_t n;
  char *out;
  if (raw == NULL) return NULL;
  n = strlen(raw);
  out = (char *)malloc(n + 1);
  if (out == NULL) return NULL;
  for (i = 0; i < n; i += 1) {
    out[i] = driver_sidecar_is_ident_char(raw[i]) ? raw[i] : '_';
  }
  out[n] = '\0';
  return out;
}

static char *driver_sidecar_read_text(const char *path, size_t *len_out) {
  FILE *fp;
  long size_long;
  size_t size;
  char *buf;
  if (len_out != NULL) *len_out = 0;
  if (path == NULL || path[0] == '\0') return NULL;
  fp = fopen(path, "rb");
  if (fp == NULL) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  size_long = ftell(fp);
  if (size_long < 0) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  size = (size_t)size_long;
  buf = (char *)malloc(size + 1);
  if (buf == NULL) {
    fclose(fp);
    return NULL;
  }
  if (size > 0 && fread(buf, 1, size, fp) != size) {
    fclose(fp);
    free(buf);
    return NULL;
  }
  fclose(fp);
  buf[size] = '\0';
  if (len_out != NULL) *len_out = size;
  return buf;
}

static char *driver_sidecar_write_temp_text(const char *base_path, const char *text) {
  const char *slash;
  int fd;
  size_t len;
  size_t dir_len;
  const char *suffix = ".cheng";
  size_t suffix_len = 6;
  char *templ;
  char *path;
  if (base_path == NULL || base_path[0] == '\0' || text == NULL) return NULL;
  slash = strrchr(base_path, '/');
  dir_len = (slash != NULL) ? (size_t)(slash - base_path) : 0;
  templ = (char *)malloc(dir_len + sizeof("/cheng_sidecar_rewrite_XXXXXX") + suffix_len);
  if (templ == NULL) return NULL;
  if (dir_len > 0) memcpy(templ, base_path, dir_len);
  memcpy(templ + dir_len, "/cheng_sidecar_rewrite_XXXXXX", sizeof("/cheng_sidecar_rewrite_XXXXXX") - 1);
  memcpy(templ + dir_len + sizeof("/cheng_sidecar_rewrite_XXXXXX") - 1, suffix, suffix_len + 1);
  fd = mkstemps(templ, (int)suffix_len);
  if (fd < 0) {
    free(templ);
    return NULL;
  }
  len = strlen(text);
  if (len > 0 && write(fd, text, len) != (ssize_t)len) {
    close(fd);
    unlink(templ);
    free(templ);
    return NULL;
  }
  close(fd);
  path = driver_sidecar_dup(templ);
  if (path == NULL) unlink(templ);
  free(templ);
  return path;
}

static char *driver_sidecar_rewrite_new_expr_assignments(const char *src, int *changed_out) {
  DriverSidecarStr out = {0};
  const char *p;
  int changed = 0;
  int counter = 0;
  if (changed_out != NULL) *changed_out = 0;
  if (src == NULL) return NULL;
  p = src;
  while (*p != '\0') {
    const char *line = p;
    const char *nl = strchr(p, '\n');
    const char *line_end = nl != NULL ? nl : p + strlen(p);
    const char *cur = line;
    const char *after_kw;
    const char *colon;
    const char *eq;
    const char *new_pos;
    const char *open_paren;
    const char *close_paren;
    const char *name_start;
    const char *name_end;
    const char *type_start;
    const char *type_end;
    const char *arg_start;
    const char *arg_end;
    while (cur < line_end && (*cur == ' ' || *cur == '\t')) cur += 1;
    after_kw = NULL;
    if ((size_t)(line_end - cur) > 4 && memcmp(cur, "let ", 4) == 0) {
      after_kw = cur + 4;
    } else if ((size_t)(line_end - cur) > 4 && memcmp(cur, "var ", 4) == 0) {
      after_kw = cur + 4;
    }
    colon = after_kw != NULL ? memchr(after_kw, ':', (size_t)(line_end - after_kw)) : NULL;
    eq = colon != NULL ? memchr(colon, '=', (size_t)(line_end - colon)) : NULL;
    new_pos = eq != NULL ? strstr(eq, "new(") : NULL;
    open_paren = new_pos != NULL ? strchr(new_pos, '(') : NULL;
    close_paren = open_paren != NULL ? strchr(open_paren + 1, ')') : NULL;
    if (after_kw == NULL || colon == NULL || eq == NULL || new_pos == NULL ||
        open_paren == NULL || close_paren == NULL || close_paren > line_end) {
      if (!driver_sidecar_str_append_n(&out, line, (size_t)(line_end - line)) ||
          (nl != NULL && !driver_sidecar_str_append_n(&out, "\n", 1))) {
        driver_sidecar_str_free(&out);
        return NULL;
      }
      p = nl != NULL ? nl + 1 : line_end;
      continue;
    }
    name_start = after_kw;
    while (name_start < colon && (*name_start == ' ' || *name_start == '\t')) name_start += 1;
    name_end = colon;
    while (name_end > name_start && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end -= 1;
    type_start = colon + 1;
    while (type_start < eq && (*type_start == ' ' || *type_start == '\t')) type_start += 1;
    type_end = eq;
    while (type_end > type_start && (type_end[-1] == ' ' || type_end[-1] == '\t')) type_end -= 1;
    arg_start = open_paren + 1;
    while (arg_start < close_paren && (*arg_start == ' ' || *arg_start == '\t')) arg_start += 1;
    arg_end = close_paren;
    while (arg_end > arg_start && (arg_end[-1] == ' ' || arg_end[-1] == '\t')) arg_end -= 1;
    if (name_start >= name_end || type_start >= type_end || arg_start >= arg_end ||
        !driver_sidecar_trim_eq_n(type_start, (size_t)(type_end - type_start),
                                  arg_start, (size_t)(arg_end - arg_start))) {
      if (!driver_sidecar_str_append_n(&out, line, (size_t)(line_end - line)) ||
          (nl != NULL && !driver_sidecar_str_append_n(&out, "\n", 1))) {
        driver_sidecar_str_free(&out);
        return NULL;
      }
      p = nl != NULL ? nl + 1 : line_end;
      continue;
    }
    {
      char tmp_name[64];
      char *type_text = driver_sidecar_dup_n(type_start, (size_t)(type_end - type_start));
      char *name_text = driver_sidecar_dup_n(name_start, (size_t)(name_end - name_start));
      if (type_text == NULL || name_text == NULL) {
        free(type_text);
        free(name_text);
        driver_sidecar_str_free(&out);
        return NULL;
      }
      snprintf(tmp_name, sizeof(tmp_name), "__cheng_new_tmp_%d", counter++);
      if (!driver_sidecar_str_append_n(&out, line, (size_t)(cur - line)) ||
          !driver_sidecar_str_append(&out, "var ") ||
          !driver_sidecar_str_append(&out, tmp_name) ||
          !driver_sidecar_str_append(&out, ": ") ||
          !driver_sidecar_str_append(&out, type_text) ||
          !driver_sidecar_str_append(&out, "\n") ||
          !driver_sidecar_str_append_n(&out, line, (size_t)(cur - line)) ||
          !driver_sidecar_str_append(&out, "new ") ||
          !driver_sidecar_str_append(&out, tmp_name) ||
          !driver_sidecar_str_append(&out, "\n") ||
          !driver_sidecar_str_append_n(&out, line, (size_t)(cur - line)) ||
          !driver_sidecar_str_append_n(&out, cur, (size_t)(after_kw - cur)) ||
          !driver_sidecar_str_append(&out, name_text) ||
          !driver_sidecar_str_append(&out, ": ") ||
          !driver_sidecar_str_append(&out, type_text) ||
          !driver_sidecar_str_append(&out, " = ") ||
          !driver_sidecar_str_append(&out, tmp_name) ||
          (nl != NULL && !driver_sidecar_str_append(&out, "\n"))) {
        free(type_text);
        free(name_text);
        driver_sidecar_str_free(&out);
        return NULL;
      }
      free(type_text);
      free(name_text);
      changed = 1;
    }
    p = nl != NULL ? nl + 1 : line_end;
  }
  if (changed_out != NULL) *changed_out = changed;
  if (!changed) {
    driver_sidecar_str_free(&out);
    return driver_sidecar_dup(src);
  }
  return out.data;
}

static char *driver_sidecar_rewrite_generic_ref_types(const char *src, int *changed_out) {
  const char *p;
  if (changed_out != NULL) *changed_out = 0;
  if (src == NULL) return NULL;
  p = src;
  while (*p != '\0') {
    const char *line = p;
    const char *nl = strchr(p, '\n');
    const char *line_end = nl != NULL ? nl : p + strlen(p);
    const char *cur = line;
    const char *name_start;
    const char *name_end;
    const char *param_start;
    const char *param_end;
    const char *scan;
    const char *block_end;
    size_t indent;
    DriverSidecarStr injected = {0};
    char *name = NULL;
    char *param = NULL;
    char *with_defs = NULL;
    char *rewritten = NULL;
    int changed = 0;
    while (cur < line_end && (*cur == ' ' || *cur == '\t')) cur += 1;
    if (cur >= line_end || !((*cur >= 'A' && *cur <= 'Z') || (*cur >= 'a' && *cur <= 'z') || *cur == '_')) {
      p = nl != NULL ? nl + 1 : line_end;
      continue;
    }
    name_start = cur;
    while (cur < line_end && driver_sidecar_is_ident_char(*cur)) cur += 1;
    name_end = cur;
    if (cur >= line_end || *cur != '[') {
      p = nl != NULL ? nl + 1 : line_end;
      continue;
    }
    cur += 1;
    param_start = cur;
    while (cur < line_end && driver_sidecar_is_ident_char(*cur)) cur += 1;
    param_end = cur;
    if (param_start == param_end || cur >= line_end || *cur != ']') {
      p = nl != NULL ? nl + 1 : line_end;
      continue;
    }
    cur += 1;
    while (cur < line_end && (*cur == ' ' || *cur == '\t')) cur += 1;
    if (cur + 5 > line_end || *cur != '=' || strncmp(cur, "= ref", 5) != 0) {
      p = nl != NULL ? nl + 1 : line_end;
      continue;
    }
    name = driver_sidecar_dup_n(name_start, (size_t)(name_end - name_start));
    param = driver_sidecar_dup_n(param_start, (size_t)(param_end - param_start));
    if (name == NULL || param == NULL) {
      free(name);
      free(param);
      return NULL;
    }
    indent = (size_t)(name_start - line);
    block_end = nl != NULL ? nl + 1 : line_end;
    scan = block_end;
    while (*scan != '\0') {
      const char *field_line = scan;
      const char *field_nl = strchr(scan, '\n');
      const char *field_end = field_nl != NULL ? field_nl : scan + strlen(scan);
      const char *field_cur = field_line;
      size_t field_indent = 0;
      while (field_cur < field_end && (*field_cur == ' ' || *field_cur == '\t')) {
        field_cur += 1;
        field_indent += 1;
      }
      if (field_cur == field_end) {
        block_end = field_nl != NULL ? field_nl + 1 : field_end;
        scan = block_end;
        continue;
      }
      if (field_indent <= indent) break;
      block_end = field_nl != NULL ? field_nl + 1 : field_end;
      scan = block_end;
    }
    {
      const char *q = src;
      while ((q = strstr(q, name)) != NULL) {
        const char *after_name = q + strlen(name);
        const char *inner_start;
        const char *inner_end;
        char *concrete = NULL;
        char *alias_suffix = NULL;
        char *alias_name = NULL;
        char *needle = NULL;
        char *field_scan_text;
        if ((q > src && driver_sidecar_is_ident_char(q[-1])) || *after_name != '[') {
          q = q + 1;
          continue;
        }
        inner_start = after_name + 1;
        inner_end = strchr(inner_start, ']');
        if (inner_end == NULL) break;
        concrete = driver_sidecar_trim_dup_n(inner_start, (size_t)(inner_end - inner_start));
        if (concrete == NULL) {
          free(name);
          free(param);
          driver_sidecar_str_free(&injected);
          return NULL;
        }
        if (strcmp(concrete, param) == 0 || strchr(concrete, '[') != NULL || strchr(concrete, ']') != NULL ||
            strchr(concrete, ' ') != NULL || strchr(concrete, '\t') != NULL) {
          free(concrete);
          q = inner_end + 1;
          continue;
        }
        alias_suffix = driver_sidecar_sanitize_alias_suffix(concrete);
        if (alias_suffix == NULL) {
          free(concrete);
          free(name);
          free(param);
          driver_sidecar_str_free(&injected);
          return NULL;
        }
        alias_name = (char *)malloc(strlen(name) + 1 + strlen(alias_suffix) + 1);
        needle = (char *)malloc(strlen(name) + 1 + strlen(concrete) + 2);
        if (alias_name == NULL || needle == NULL) {
          free(concrete);
          free(alias_suffix);
          free(alias_name);
          free(needle);
          free(name);
          free(param);
          driver_sidecar_str_free(&injected);
          return NULL;
        }
        sprintf(alias_name, "%s_%s", name, alias_suffix);
        sprintf(needle, "%s[%s]", name, concrete);
        if (strstr(injected.data != NULL ? injected.data : "", alias_name) == NULL) {
          size_t i;
          if ((injected.len == 0 &&
               !driver_sidecar_str_append(&injected, "type\n")) ||
              !driver_sidecar_str_append_n(&injected, line, indent) ||
              !driver_sidecar_str_append(&injected, alias_name) ||
              !driver_sidecar_str_append(&injected, " = ref\n")) {
            free(concrete);
            free(alias_suffix);
            free(alias_name);
            free(needle);
            free(name);
            free(param);
            driver_sidecar_str_free(&injected);
            return NULL;
          }
          field_scan_text = driver_sidecar_dup_n((nl != NULL ? nl + 1 : line_end),
                                                 (size_t)(block_end - (nl != NULL ? nl + 1 : line_end)));
          if (field_scan_text == NULL) {
            free(concrete);
            free(alias_suffix);
            free(alias_name);
            free(needle);
            free(name);
            free(param);
            driver_sidecar_str_free(&injected);
            return NULL;
          }
          {
            char *fields_replaced = driver_sidecar_replace_all(field_scan_text, param, concrete);
            free(field_scan_text);
            if (fields_replaced == NULL ||
                !driver_sidecar_str_append(&injected, fields_replaced)) {
              free(fields_replaced);
              free(concrete);
              free(alias_suffix);
              free(alias_name);
              free(needle);
              free(name);
              free(param);
              driver_sidecar_str_free(&injected);
              return NULL;
            }
            free(fields_replaced);
          }
          for (i = 0; i < injected.len; i += 1) {
            /* no-op: keep compiler quiet on some older C modes about loop labels */
          }
        }
        free(alias_suffix);
        free(alias_name);
        free(needle);
        free(concrete);
        q = inner_end + 1;
      }
    }
    if (injected.len == 0) {
      free(name);
      free(param);
      driver_sidecar_str_free(&injected);
      p = nl != NULL ? nl + 1 : line_end;
      continue;
    }
    {
      DriverSidecarStr tmp = {0};
      if (!driver_sidecar_str_append_n(&tmp, src, (size_t)(block_end - src)) ||
          !driver_sidecar_str_append_n(&tmp, injected.data, injected.len) ||
          !driver_sidecar_str_append(&tmp, block_end)) {
        free(name);
        free(param);
        driver_sidecar_str_free(&injected);
        driver_sidecar_str_free(&tmp);
        return NULL;
      }
      with_defs = tmp.data;
    }
    {
      const char *q = injected.data;
      rewritten = with_defs;
      while (q != NULL && *q != '\0') {
        const char *alias_line_end = strchr(q, '\n');
        const char *alias_eq;
        if (alias_line_end == NULL) alias_line_end = q + strlen(q);
        alias_eq = strstr(q, " = ref");
        if (alias_eq != NULL && alias_eq < alias_line_end) {
          const char *alias_name_start = q;
          while (alias_name_start < alias_eq && (*alias_name_start == ' ' || *alias_name_start == '\t')) alias_name_start += 1;
          if (alias_name_start < alias_eq) {
            char *alias_name = driver_sidecar_dup_n(alias_name_start, (size_t)(alias_eq - alias_name_start));
            if (alias_name != NULL) {
              char *needle = NULL;
              char *next = NULL;
              size_t alias_len = strlen(alias_name);
              const char *us = strrchr(alias_name, '_');
              if (us != NULL && us > alias_name) {
                size_t base_len = (size_t)(us - alias_name);
                needle = (char *)malloc(base_len + 1 + strlen(us + 1) + 2);
                if (needle != NULL) {
                  sprintf(needle, "%.*s[%s]", (int)base_len, alias_name, us + 1);
                  next = driver_sidecar_replace_all(rewritten, needle, alias_name);
                }
              }
              if (next != NULL) {
                if (rewritten == with_defs) with_defs = NULL;
                free(rewritten);
                rewritten = next;
                changed = 1;
              }
              free(needle);
              (void)alias_len;
              free(alias_name);
            }
          }
        }
        q = (*alias_line_end == '\0') ? alias_line_end : alias_line_end + 1;
      }
    }
    free(with_defs);
    free(name);
    free(param);
    driver_sidecar_str_free(&injected);
    if (changed_out != NULL) *changed_out = changed;
    return rewritten;
  }
  return driver_sidecar_dup(src);
}

static char *driver_sidecar_prepare_rewritten_input(const char *input_path,
                                                    const char *compiler,
                                                    int *used_out) {
  char *text;
  char *tmp;
  char *next;
  int changed_generic = 0;
  int changed_new = 0;
  if (used_out != NULL) *used_out = 0;
  text = driver_sidecar_read_text(input_path, NULL);
  if (text == NULL) return NULL;
  next = driver_sidecar_rewrite_generic_ref_types(text, &changed_generic);
  free(text);
  if (next == NULL) return NULL;
  text = next;
  if (!driver_sidecar_compiler_supports_new_expr_assignments(compiler)) {
    next = driver_sidecar_rewrite_new_expr_assignments(text, &changed_new);
    free(text);
    if (next == NULL) return NULL;
    text = next;
  }
  if (!changed_generic && !changed_new) {
    if (used_out != NULL) *used_out = 0;
    free(text);
    return NULL;
  }
  tmp = driver_sidecar_write_temp_text(input_path, text);
  free(text);
  if (tmp != NULL && used_out != NULL) *used_out = 1;
  return tmp;
}

static const char *driver_sidecar_pick_compiler_for_input(const char *input_path) {
  const char *env_path = getenv("BACKEND_UIR_SIDECAR_COMPILER");
  const char *currentsrc_stage0;
  const char *preserved_attempt;
  const char *preferred_release_path;
  const char *stage0_path;
  const char *canonical_path;
  const char *dist_release_path;
  if (env_path != NULL && env_path[0] != '\0' && access(env_path, X_OK) == 0) {
    return env_path;
  }
  /*
   * Sidecar child compiles run with BACKEND_UIR_SIDECAR_DISABLE=1. For
   * tooling self-rebuilds, prefer preserved/default direct-path compilers over
   * the heavier current-source proof child. Keep proof-child preference only
   * for proof-surface inputs that actually require that lineage.
   */
  currentsrc_stage0 = driver_sidecar_default_currentsrc_proof_stage0_for_input(input_path, env_path);
  if (driver_sidecar_should_prefer_currentsrc_proof_child(input_path) &&
      currentsrc_stage0 != NULL && access(currentsrc_stage0, X_OK) == 0) {
    return currentsrc_stage0;
  }
  preserved_attempt = driver_sidecar_default_preserved_attempt_for_input(input_path, env_path);
  if (driver_sidecar_should_prefer_preserved_attempt(input_path) &&
      preserved_attempt != NULL && access(preserved_attempt, X_OK) == 0) {
    return preserved_attempt;
  }
  preferred_release_path =
      driver_sidecar_default_preferred_release_for_input(input_path, env_path);
  if (preferred_release_path != NULL && access(preferred_release_path, X_OK) == 0) {
    return preferred_release_path;
  }
  canonical_path = driver_sidecar_default_canonical_for_input(input_path, env_path);
  if (canonical_path != NULL && access(canonical_path, X_OK) == 0) {
    return canonical_path;
  }
  dist_release_path = driver_sidecar_default_dist_release_for_input(input_path, env_path);
  if (dist_release_path != NULL && access(dist_release_path, X_OK) == 0) {
    return dist_release_path;
  }
  stage0_path = driver_sidecar_default_stage0_for_input(input_path, env_path);
  if (stage0_path != NULL && access(stage0_path, X_OK) == 0) {
    return stage0_path;
  }
  if (preserved_attempt != NULL && access(preserved_attempt, X_OK) == 0) {
    return preserved_attempt;
  }
  return NULL;
}

static const char *driver_sidecar_pick_compiler(void) {
  return driver_sidecar_pick_compiler_for_input(NULL);
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

static DriverSidecarEnvOverride *driver_sidecar_find_override(DriverSidecarEnvOverride *overrides,
                                                              size_t override_count,
                                                              const char *name) {
  size_t i;
  if (overrides == NULL || name == NULL) return NULL;
  for (i = 0; i < override_count; i += 1) {
    if (strcmp(overrides[i].name, name) == 0) return &overrides[i];
  }
  return NULL;
}

static int driver_sidecar_set_override_value(DriverSidecarEnvOverride *overrides,
                                             size_t override_count,
                                             const char *name,
                                             const char *value) {
  DriverSidecarEnvOverride *slot =
      driver_sidecar_find_override(overrides, override_count, name);
  if (slot == NULL) return 0;
  slot->value = (value != NULL) ? value : "";
  return 1;
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
    if (overrides[i].name == NULL || overrides[i].value == NULL) {
      fprintf(stderr,
              "[backend_driver sidecar] invalid override[%zu] name=%p value=%p\n",
              i, (void *)overrides[i].name, (void *)overrides[i].value);
      size_t fi;
      for (fi = 0; fi < i; fi += 1) free(owned_pairs[fi]);
      free(owned_pairs);
      free(envp);
      return NULL;
    }
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

static int32_t driver_sidecar_patch_macho_cstrings(const DriverUirSidecarHandle *h,
                                                   const char *final_target,
                                                   const char *out_path) {
  pid_t pid;
  int status = 0;
  int spawn_rc;
  const char *script_path;
  const char *python_bin;
  char repo_root[PATH_MAX];
  char script_buf[1024];
  char *argv[9];
  if (h == NULL || out_path == NULL || out_path[0] == '\0') return 0;
  if (!driver_sidecar_target_is_darwin(final_target)) return 0;
  if (!driver_sidecar_resolve_repo_root(h->input_path, h->compiler, repo_root, sizeof(repo_root))) {
    fprintf(stderr, "[backend_driver sidecar] unable to resolve repo root for cstring patch\n");
    return 2;
  }
  script_path = getenv("BACKEND_UIR_SIDECAR_CSTRING_PATCHER");
  if (script_path == NULL || script_path[0] == '\0') {
    snprintf(script_buf, sizeof(script_buf), "%s/scripts/gen_cstring_compat_obj.py", repo_root);
    script_path = script_buf;
  }
  if (access(script_path, R_OK) != 0) {
    fprintf(stderr, "[backend_driver sidecar] missing cstring patcher: %s\n", script_path);
    return 2;
  }
  python_bin = driver_sidecar_python_bin();
  argv[0] = (char *)python_bin;
  argv[1] = (char *)script_path;
  argv[2] = "--repo-root";
  argv[3] = (char *)repo_root;
  argv[4] = "--patch-obj";
  argv[5] = (char *)out_path;
  argv[6] = "--extra-source";
  argv[7] = (char *)((h->input_path != NULL && h->input_path[0] != '\0') ? h->input_path : repo_root);
  argv[8] = NULL;
  spawn_rc = posix_spawnp(&pid, python_bin, NULL, NULL, argv, environ);
  if (spawn_rc != 0) {
    fprintf(stderr, "[backend_driver sidecar] cstring patch spawn failed: %s\n", strerror(spawn_rc));
    return 2;
  }
  for (;;) {
    pid_t waited = waitpid(pid, &status, 0);
    if (waited >= 0) break;
    if (errno == EINTR) continue;
    fprintf(stderr, "[backend_driver sidecar] cstring patch waitpid failed: %s\n", strerror(errno));
    return 2;
  }
  if (WIFEXITED(status)) {
    int32_t rc = (int32_t)WEXITSTATUS(status);
    if (rc != 0) {
      fprintf(stderr, "[backend_driver sidecar] cstring patch failed: rc=%d script=%s\n", rc, script_path);
    }
    return rc;
  }
  if (WIFSIGNALED(status)) {
    int32_t rc = (int32_t)(128 + WTERMSIG(status));
    fprintf(stderr, "[backend_driver sidecar] cstring patch signaled: rc=%d script=%s\n", rc, script_path);
    return rc;
  }
  return 2;
}

static void driver_sidecar_write_fallback_compile_stamp(const DriverUirSidecarHandle *h,
                                                        const char *final_target,
                                                        const char *compiler,
                                                        const char *generic_mode,
                                                        const char *generic_lowering) {
  const char *stamp_path = getenv("BACKEND_COMPILE_STAMP_OUT");
  const char *frontend = getenv("BACKEND_FRONTEND");
  const char *borrow_ir = getenv("BORROW_IR");
  const char *input_path = "";
  struct stat st;
  FILE *fp;
  if (stamp_path == NULL || stamp_path[0] == '\0') return;
  if (compiler == NULL || !driver_sidecar_compiler_is_currentsrc_proof_lineage(compiler)) return;
  if (stat(stamp_path, &st) == 0 && st.st_size > 0) return;
  if (h != NULL && h->input_path != NULL) input_path = h->input_path;
  if (frontend == NULL || frontend[0] == '\0') frontend = "stage1";
  if (borrow_ir == NULL) borrow_ir = "";
  if (generic_mode == NULL || generic_mode[0] == '\0') generic_mode = "dict";
  if (generic_lowering == NULL || generic_lowering[0] == '\0') generic_lowering = "mir_dict";
  fp = fopen(stamp_path, "w");
  if (fp == NULL) return;
  fprintf(fp, "target=%s\n", final_target != NULL ? final_target : "");
  fprintf(fp, "frontend=%s\n", frontend);
  fprintf(fp, "input=%s\n", input_path);
  fprintf(fp, "generic_mode=%s\n", generic_mode);
  fprintf(fp, "generic_spec_budget=0\n");
  fprintf(fp, "borrow_ir=%s\n", borrow_ir);
  fprintf(fp, "generic_lowering=%s\n", generic_lowering);
  fprintf(fp, "stage1_skip_ownership_effective=0\n");
  fprintf(fp, "stage1_skip_ownership_default=0\n");
  fprintf(fp, "uir_phase_contract_version=p4_phase_v1\n");
  fclose(fp);
}

static int32_t driver_sidecar_exec_obj_compile(const DriverUirSidecarHandle *h,
                                               const char *target,
                                               const char *out_path) {
  pid_t pid;
  int status = 0;
  int spawn_rc;
  const char *compiler;
  const char *final_target;
  const char *final_generic_mode;
  const char *final_generic_lowering;
  const char *final_jobs;
  const char *outer_sidecar_compiler = NULL;
  int outer_cli_mode = 0;
  char *argv[] = {NULL, NULL, NULL, NULL};
  char *output_arg = NULL;
  char **envp = NULL;
  char **owned_pairs = NULL;
  DriverSidecarEnvOverride overrides[] = {
      {"DYLD_INSERT_LIBRARIES", ""},
      {"DYLD_FORCE_FLAT_NAMESPACE", ""},
      {"BACKEND_UIR_SIDECAR_DISABLE", "1"},
      {"BACKEND_UIR_SIDECAR_OBJ", "/__cheng_sidecar_disabled__.o"},
      {"BACKEND_UIR_SIDECAR_BUNDLE", "/__cheng_sidecar_disabled__.bundle"},
      {"BACKEND_UIR_SIDECAR_COMPILER", ""},
      {"BACKEND_UIR_PREFER_SIDECAR", "0"},
      {"BACKEND_UIR_FORCE_SIDECAR", "0"},
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
      {"BACKEND_STAGE1_PARSE_MODE", NULL},
      {"BACKEND_FN_SCHED", NULL},
      {"BACKEND_DIRECT_EXE", NULL},
      {"BACKEND_LINKERLESS_INMEM", NULL},
      {"BACKEND_FAST_FALLBACK_ALLOW", "0"},
      {"BACKEND_MULTI", NULL},
      {"BACKEND_MULTI_FORCE", NULL},
      {"BACKEND_MULTI_MODULE_CACHE", "0"},
      {"BACKEND_MODULE_CACHE", ""},
      {"BACKEND_MODULE_CACHE_UNSTABLE_ALLOW", "0"},
      {"BACKEND_INCREMENTAL", NULL},
      {"BACKEND_JOBS", NULL},
      {"BACKEND_FN_JOBS", NULL},
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
      {"BACKEND_LINKER", NULL},
      {"BACKEND_NO_RUNTIME_C", "0"},
      {"BACKEND_INTERNAL_ALLOW_EMIT_OBJ", "1"},
      {"BACKEND_ALLOW_NO_MAIN", "1"},
      {"BACKEND_EMIT", NULL},
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
  if (driver_sidecar_debug_enabled()) {
    fprintf(stderr, "[backend_driver sidecar] selected_compiler='%s'\n", compiler);
  }
  final_target = (target != NULL && target[0] != '\0')
      ? target
      : ((h->target != NULL && h->target[0] != '\0') ? h->target : "arm64-apple-darwin");
  outer_cli_mode = driver_sidecar_compiler_requires_outer_cli_mode(compiler);
  if (outer_cli_mode) {
    const char *currentsrc_outer =
        driver_sidecar_default_currentsrc_proof_outer_for_input(h->input_path, compiler);
    const char *stage0_path = driver_sidecar_default_stage0_for_input(h->input_path, compiler);
    outer_sidecar_compiler = getenv("BACKEND_UIR_SIDECAR_COMPILER");
    if (outer_sidecar_compiler == NULL || outer_sidecar_compiler[0] == '\0' ||
        strcmp(outer_sidecar_compiler, compiler) == 0 ||
        driver_sidecar_compiler_requires_outer_cli_mode(outer_sidecar_compiler)) {
      if (driver_sidecar_compiler_prefers_currentsrc_outer(compiler) &&
          currentsrc_outer != NULL && access(currentsrc_outer, X_OK) == 0) {
        outer_sidecar_compiler = currentsrc_outer;
      } else {
        outer_sidecar_compiler = (stage0_path != NULL) ? stage0_path : compiler;
      }
    }
    if (!driver_sidecar_set_override_value(overrides, override_count,
                                           "BACKEND_UIR_SIDECAR_DISABLE", "") ||
        !driver_sidecar_set_override_value(overrides, override_count,
                                           "BACKEND_UIR_SIDECAR_OBJ", "") ||
        !driver_sidecar_set_override_value(overrides, override_count,
                                           "BACKEND_UIR_SIDECAR_BUNDLE", "") ||
        !driver_sidecar_set_override_value(overrides, override_count,
                                           "BACKEND_UIR_SIDECAR_COMPILER",
                                           outer_sidecar_compiler)) {
      fprintf(stderr, "[backend_driver sidecar] override map mismatch (outer cli)\n");
      return 2;
    }
  }
  if (!driver_sidecar_set_override_value(overrides, override_count,
                                         "BACKEND_STAGE1_PARSE_MODE",
                                         driver_sidecar_env_or_default("BACKEND_STAGE1_PARSE_MODE",
                                                                       "outline")) ||
      !driver_sidecar_set_override_value(overrides, override_count,
                                         "BACKEND_FN_SCHED",
                                         driver_sidecar_env_or_default("BACKEND_FN_SCHED", "ws")) ||
      !driver_sidecar_set_override_value(overrides, override_count,
                                         "BACKEND_DIRECT_EXE",
                                         driver_sidecar_env_or_default("BACKEND_DIRECT_EXE", "0")) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "BACKEND_LINKERLESS_INMEM",
          driver_sidecar_env_or_default("BACKEND_LINKERLESS_INMEM", "0")) ||
      !driver_sidecar_set_override_value(overrides, override_count,
                                         "BACKEND_MULTI",
                                         driver_sidecar_env_or_default("BACKEND_MULTI", "0")) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "BACKEND_MULTI_FORCE",
          driver_sidecar_env_or_default("BACKEND_MULTI_FORCE", "0")) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "BACKEND_INCREMENTAL",
          driver_sidecar_env_or_default("BACKEND_INCREMENTAL", "0"))) {
    fprintf(stderr, "[backend_driver sidecar] override map mismatch (core env)\n");
    return 2;
  }
  if (driver_sidecar_compiler_is_currentsrc_proof_lineage(compiler)) {
    final_jobs = driver_sidecar_env_or_default("BACKEND_JOBS", "");
  } else {
    final_jobs = driver_sidecar_env_or_default("BACKEND_JOBS", outer_cli_mode ? "0" : "1");
  }
  if (!driver_sidecar_set_override_value(overrides, override_count, "BACKEND_JOBS", final_jobs) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "BACKEND_FN_JOBS",
          driver_sidecar_env_or_default("BACKEND_FN_JOBS", final_jobs)) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "BACKEND_OPT_LEVEL",
          driver_sidecar_env_or_default("BACKEND_OPT_LEVEL", "0")) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "BACKEND_OPT",
          driver_sidecar_env_or_default("BACKEND_OPT", "0")) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "BACKEND_OPT2",
          driver_sidecar_env_or_default("BACKEND_OPT2", "0"))) {
    fprintf(stderr, "[backend_driver sidecar] override map mismatch (jobs/opt env)\n");
    return 2;
  }
  final_generic_mode = driver_sidecar_env_or_default("GENERIC_MODE", "dict");
  if (driver_sidecar_compiler_is_currentsrc_proof_lineage(compiler)) {
    final_generic_lowering = "mir_dict";
  } else {
    final_generic_lowering = driver_sidecar_env_or_default("GENERIC_LOWERING", "mir_dict");
  }
  if (!driver_sidecar_set_override_value(overrides, override_count, "GENERIC_MODE",
                                         final_generic_mode) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "GENERIC_SPEC_BUDGET",
          driver_sidecar_env_or_default("GENERIC_SPEC_BUDGET", "0")) ||
      !driver_sidecar_set_override_value(overrides, override_count, "GENERIC_LOWERING",
                                         final_generic_lowering) ||
      !driver_sidecar_set_override_value(overrides, override_count, "BACKEND_LINKER", "system") ||
      !driver_sidecar_set_override_value(overrides, override_count, "BACKEND_EMIT", "obj") ||
      !driver_sidecar_set_override_value(overrides, override_count, "BACKEND_TARGET",
                                         final_target) ||
      !driver_sidecar_set_override_value(
          overrides, override_count, "BACKEND_INPUT",
          (h->input_path != NULL) ? h->input_path : "") ||
      !driver_sidecar_set_override_value(overrides, override_count, "BACKEND_OUTPUT",
                                         out_path)) {
    fprintf(stderr, "[backend_driver sidecar] override map mismatch (io env)\n");
    return 2;
  }
  envp = driver_sidecar_build_envp(overrides, override_count, &owned_pairs);
  if (envp == NULL) {
    fprintf(stderr, "[backend_driver sidecar] envp build failed\n");
    return 2;
  }
  argv[0] = (char *)compiler;
  if (outer_cli_mode) {
    size_t output_need = strlen("--output:") + strlen(out_path) + 1u;
    output_arg = (char *)calloc(output_need, 1u);
    if (output_arg == NULL) {
      driver_sidecar_free_envp(envp, owned_pairs, override_count);
      fprintf(stderr, "[backend_driver sidecar] alloc failed for outer cli output arg\n");
      return 2;
    }
    snprintf(output_arg, output_need, "--output:%s", out_path);
    argv[1] = (char *)((h->input_path != NULL) ? h->input_path : "");
    argv[2] = output_arg;
    argv[3] = NULL;
  } else {
    argv[1] = NULL;
  }
  spawn_rc = posix_spawn(&pid, compiler, NULL, NULL, argv, envp);
  free(output_arg);
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
  if (WIFEXITED(status)) {
    int32_t rc = (int32_t)WEXITSTATUS(status);
    if (rc != 0) return rc;
    driver_sidecar_write_fallback_compile_stamp(
        h, final_target, compiler, final_generic_mode, final_generic_lowering);
    rc = driver_sidecar_patch_macho_cstrings(h, final_target, out_path);
    return rc;
  }
  if (WIFSIGNALED(status)) return (int32_t)(128 + WTERMSIG(status));
  return 2;
}

__attribute__((visibility("default")))
void *driver_buildActiveModulePtrs(void *input_raw, void *target_raw) {
  DriverUirSidecarHandle *h;
  const char *compiler;
  const char *input_text = (const char *)input_raw;
  const char *target_text = (const char *)target_raw;
  int rewrite_used = 0;
  char *rewrite_path = NULL;
  if (!driver_sidecar_mode_is_emergency_c()) return NULL;
  if (input_text == NULL || input_text[0] == '\0') return NULL;
  compiler = driver_sidecar_pick_compiler_for_input(input_text);
  if (compiler == NULL) return NULL;
  if (driver_sidecar_debug_enabled()) {
    fprintf(stderr, "[backend_driver sidecar] picked_compiler='%s'\n", compiler);
  }
  h = (DriverUirSidecarHandle *)calloc(1, sizeof(DriverUirSidecarHandle));
  if (h == NULL) return NULL;
  rewrite_path = driver_sidecar_prepare_rewritten_input(input_text, compiler, &rewrite_used);
  if (rewrite_used && rewrite_path == NULL) {
    driver_sidecar_free_handle(h);
    return NULL;
  }
  if (rewrite_used) {
    h->rewrite_path = driver_sidecar_dup(rewrite_path);
    h->input_path = driver_sidecar_dup(rewrite_path);
    if (driver_sidecar_debug_enabled() || driver_sidecar_keep_rewrite_enabled()) {
      fprintf(stderr, "[backend_driver sidecar] rewrite_input='%s'\n", rewrite_path);
    }
    free(rewrite_path);
  } else {
    h->input_path = driver_sidecar_dup(input_text);
  }
  h->target = driver_sidecar_dup(target_text != NULL ? target_text : "");
  h->compiler = driver_sidecar_dup(compiler);
  if (h->input_path == NULL || h->compiler == NULL || (rewrite_used && h->rewrite_path == NULL)) {
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
