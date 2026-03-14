#ifndef CHENG_SIDECAR_LOADER_H
#define CHENG_SIDECAR_LOADER_H

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef CHENG_SIDECAR_BUNDLE_PATH_MAX
#define CHENG_SIDECAR_BUNDLE_PATH_MAX 1024
#endif

#ifndef CHENG_SIDECAR_DEFAULT_BUNDLE_FMT
#define CHENG_SIDECAR_DEFAULT_BUNDLE_FMT \
  "/Users/lbcheng/cheng-lang/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.%s.bundle"
#endif

typedef struct ChengSidecarBundleCache {
  void *handle;
  char path[CHENG_SIDECAR_BUNDLE_PATH_MAX];
} ChengSidecarBundleCache;

typedef void (*ChengSidecarDiagFn)(const char *fmt, ...);
typedef void (*ChengSidecarAfterOpenFn)(void *handle, ChengSidecarDiagFn diag);

static inline int cheng_sidecar_env_truthy(const char *raw) {
  if (raw == NULL || raw[0] == '\0') return 0;
  if (raw[0] == '0' && raw[1] == '\0') return 0;
  return 1;
}

static inline int cheng_sidecar_bundle_path(char *out, size_t out_size,
                                            const char *target_text,
                                            ChengSidecarDiagFn diag) {
  const char *bundle_env = getenv("BACKEND_UIR_SIDECAR_BUNDLE");
  const char *target = target_text != NULL ? target_text : "";
  if (out == NULL || out_size == 0) return 0;
  out[0] = '\0';
  if (bundle_env != NULL && bundle_env[0] != '\0') {
    snprintf(out, out_size, "%s", bundle_env);
  } else if (target[0] != '\0') {
    snprintf(out, out_size, CHENG_SIDECAR_DEFAULT_BUNDLE_FMT, target);
  } else {
    if (diag != NULL) diag("[driver_c_sidecar_bundle_handle] no_target_or_bundle\n");
    return 0;
  }
  if (diag != NULL) {
    diag("[driver_c_sidecar_bundle_handle] env='%s' target='%s' path='%s'\n",
         bundle_env != NULL ? bundle_env : "", target, out);
  }
  return out[0] != '\0';
}

static inline void *cheng_sidecar_bundle_handle_cached(
    ChengSidecarBundleCache *cache, const char *target_text,
    ChengSidecarDiagFn diag, ChengSidecarAfterOpenFn after_open) {
  const char *disabled = getenv("BACKEND_UIR_SIDECAR_DISABLE");
  char bundle_path[CHENG_SIDECAR_BUNDLE_PATH_MAX];
  if (cache == NULL) return NULL;
  if (cheng_sidecar_env_truthy(disabled)) {
    if (diag != NULL) diag("[driver_c_sidecar_bundle_handle] disabled='%s'\n", disabled);
    return NULL;
  }
  if (!cheng_sidecar_bundle_path(bundle_path, sizeof(bundle_path), target_text, diag)) {
    return NULL;
  }
  if (access(bundle_path, F_OK) != 0) {
    if (diag != NULL) diag("[driver_c_sidecar_bundle_handle] missing_path='%s'\n", bundle_path);
    return NULL;
  }
  if (cache->handle != NULL && strcmp(cache->path, bundle_path) == 0) {
    if (diag != NULL) {
      diag("[driver_c_sidecar_bundle_handle] cache_hit path='%s' handle=%p\n",
           bundle_path, cache->handle);
    }
    return cache->handle;
  }
  dlerror();
  void *handle = dlopen(bundle_path, RTLD_NOW | RTLD_LOCAL);
  if (handle == NULL) {
    const char *err = dlerror();
    if (diag != NULL) {
      diag("[driver_c_sidecar_bundle_handle] dlopen_failed path='%s' err='%s'\n",
           bundle_path, err != NULL ? err : "");
    }
    return NULL;
  }
  if (diag != NULL) {
    diag("[driver_c_sidecar_bundle_handle] dlopen_ok path='%s' handle=%p\n",
         bundle_path, handle);
  }
  if (after_open != NULL) after_open(handle, diag);
  snprintf(cache->path, sizeof(cache->path), "%s", bundle_path);
  cache->handle = handle;
  return cache->handle;
}

static inline void *cheng_sidecar_lookup_symbol(void *handle, const char *symbol,
                                                ChengSidecarDiagFn diag,
                                                const char *diag_tag) {
  void *fn;
  const char *err;
  if (handle == NULL || symbol == NULL || symbol[0] == '\0') return NULL;
  dlerror();
  fn = dlsym(handle, symbol);
  err = dlerror();
  if (diag != NULL && diag_tag != NULL) {
    diag("[%s] handle=%p symbol='%s' fn=%p err='%s'\n",
         diag_tag, handle, symbol, fn, err != NULL ? err : "");
  }
  return fn;
}

static inline void *cheng_sidecar_driver_emit_obj_symbol(
    ChengSidecarBundleCache *cache, const char *target_text,
    ChengSidecarDiagFn diag, ChengSidecarAfterOpenFn after_open) {
  void *handle =
      cheng_sidecar_bundle_handle_cached(cache, target_text, diag, after_open);
  return cheng_sidecar_lookup_symbol(
      handle, "driver_emit_obj_from_module_default_impl", NULL, NULL);
}

static inline void *cheng_sidecar_build_module_symbol(
    ChengSidecarBundleCache *cache, const char *target_text,
    ChengSidecarDiagFn diag, ChengSidecarAfterOpenFn after_open) {
  void *handle =
      cheng_sidecar_bundle_handle_cached(cache, target_text, diag, after_open);
  return cheng_sidecar_lookup_symbol(handle, "driver_buildActiveModulePtrs",
                                     diag, "driver_c_sidecar_build_module_fn");
}

#endif
