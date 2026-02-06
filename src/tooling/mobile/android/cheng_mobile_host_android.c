#ifdef __ANDROID__

#include <android/log.h>
#include <android/native_window.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cheng_mobile_bridge.h"
#include "cheng_mobile_android_gl.h"
#include "cheng_mobile_host_android.h"
#include "cheng_mobile_host_core.h"

#define CHENG_LOG_TAG "ChengMobile"

static pthread_mutex_t cheng_mobile_surface_mutex = PTHREAD_MUTEX_INITIALIZER;

static ANativeWindow* cheng_mobile_window = NULL;
static ANativeWindow* cheng_mobile_retired_windows[8];
static int cheng_mobile_retired_count = 0;
static int cheng_mobile_window_width = 0;
static int cheng_mobile_window_height = 0;
static int cheng_mobile_gl_active = 0;
static ANativeWindow* cheng_mobile_gl_window = NULL;
static char* cheng_mobile_asset_root = NULL;
static int cheng_mobile_surface_dirty = 0;

static void cheng_mobile_retire_window_locked(ANativeWindow* window) {
  if (window == NULL) {
    return;
  }
  if (cheng_mobile_retired_count < (int)(sizeof(cheng_mobile_retired_windows) / sizeof(cheng_mobile_retired_windows[0]))) {
    cheng_mobile_retired_windows[cheng_mobile_retired_count++] = window;
    return;
  }
  // Too many outstanding surfaces; release defensively to avoid leaks.
  ANativeWindow_release(window);
}

static void cheng_mobile_release_retired_windows(void) {
  pthread_mutex_lock(&cheng_mobile_surface_mutex);
  for (int i = 0; i < cheng_mobile_retired_count; i++) {
    if (cheng_mobile_retired_windows[i] != NULL) {
      ANativeWindow_release(cheng_mobile_retired_windows[i]);
      cheng_mobile_retired_windows[i] = NULL;
    }
  }
  cheng_mobile_retired_count = 0;
  pthread_mutex_unlock(&cheng_mobile_surface_mutex);
}

static int cheng_mobile_min_int(int a, int b) {
  return a < b ? a : b;
}

static int cheng_mobile_bytes_per_pixel(int format) {
  switch (format) {
    case WINDOW_FORMAT_RGBA_8888:
    case WINDOW_FORMAT_RGBX_8888:
      return 4;
    case WINDOW_FORMAT_RGB_565:
      return 2;
    default:
      return 4;
  }
}

void cheng_mobile_host_android_set_surface(ANativeWindow* window, int width, int height) {
  pthread_mutex_lock(&cheng_mobile_surface_mutex);
  // The JNI layer transfers ownership of `window` (returned by ANativeWindow_fromSurface)
  // to this function. We must either store it or release it.
  int sameWindow = (window != NULL && window == cheng_mobile_window);
  if (sameWindow) {
    // Drop the extra reference from ANativeWindow_fromSurface; keep existing handle.
    if (window != NULL) {
      ANativeWindow_release(window);
    }
    cheng_mobile_window_width = width;
    cheng_mobile_window_height = height;
    cheng_mobile_surface_dirty = 1;
    pthread_mutex_unlock(&cheng_mobile_surface_mutex);
    return;
  }

  if (cheng_mobile_window != NULL) {
    cheng_mobile_retire_window_locked(cheng_mobile_window);
  }
  cheng_mobile_window = window;
  cheng_mobile_window_width = width;
  cheng_mobile_window_height = height;
  cheng_mobile_surface_dirty = 1;
  pthread_mutex_unlock(&cheng_mobile_surface_mutex);
}

int cheng_mobile_host_init(const ChengMobileConfig* cfg) {
  (void)cfg;
  cheng_mobile_host_core_reset();
  ChengMobileEvent ev = {0};
  ev.kind = CHENG_MRE_RUNTIME_STARTED;
  cheng_mobile_host_core_push(&ev);
  __android_log_print(ANDROID_LOG_INFO, CHENG_LOG_TAG, "host init");
  return 0;
}

int cheng_mobile_host_open_window(const ChengMobileConfig* cfg) {
  int windowId = 1;
  ChengMobileEvent ev = {0};
  ev.kind = CHENG_MRE_WINDOW_OPENED;
  ev.windowId = windowId;
  ev.message = cfg ? cfg->title : "Cheng Mobile";
  cheng_mobile_host_core_push(&ev);
  __android_log_print(ANDROID_LOG_INFO, CHENG_LOG_TAG, "open window");
  return windowId;
}

int cheng_mobile_host_poll_event(ChengMobileEvent* outEvent) {
  cheng_mobile_host_android_pump_events();
  return cheng_mobile_host_core_pop(outEvent);
}

void cheng_mobile_host_present(const void* pixels, int width, int height, int strideBytes) {
  if (pixels == NULL) {
    return;
  }

  ANativeWindow* window = NULL;
  int windowWidth = 0;
  int windowHeight = 0;
  int surfaceDirty = 0;
  pthread_mutex_lock(&cheng_mobile_surface_mutex);
  window = cheng_mobile_window;
  windowWidth = cheng_mobile_window_width;
  windowHeight = cheng_mobile_window_height;
  surfaceDirty = cheng_mobile_surface_dirty;
  cheng_mobile_surface_dirty = 0;
  if (window != NULL) {
    ANativeWindow_acquire(window);
  }
  pthread_mutex_unlock(&cheng_mobile_surface_mutex);

  if (window == NULL) {
    if (cheng_mobile_gl_active) {
      cheng_mobile_android_gl_shutdown();
      cheng_mobile_gl_active = 0;
      cheng_mobile_gl_window = NULL;
      cheng_mobile_release_retired_windows();
    }
    return;
  }

  if (surfaceDirty && !cheng_mobile_gl_active) {
    cheng_mobile_release_retired_windows();
  }

  if (surfaceDirty && cheng_mobile_gl_active && cheng_mobile_gl_window != window) {
    cheng_mobile_android_gl_shutdown();
    cheng_mobile_gl_active = 0;
    cheng_mobile_gl_window = NULL;
    cheng_mobile_release_retired_windows();
  }

  if (surfaceDirty && cheng_mobile_gl_active) {
    cheng_mobile_android_gl_resize(windowWidth, windowHeight);
    cheng_mobile_release_retired_windows();
  }

  if (!cheng_mobile_gl_active && windowWidth > 0 && windowHeight > 0) {
    if (cheng_mobile_android_gl_init(window, windowWidth, windowHeight)) {
      cheng_mobile_gl_active = 1;
      cheng_mobile_gl_window = window;
      cheng_mobile_release_retired_windows();
    }
  }

  if (cheng_mobile_gl_active) {
    if (cheng_mobile_android_gl_present(pixels, width, height, strideBytes)) {
      ANativeWindow_release(window);
      return;
    }
    __android_log_print(ANDROID_LOG_WARN, CHENG_LOG_TAG, "gl present failed; fallback to CPU blit");
    cheng_mobile_android_gl_shutdown();
    cheng_mobile_gl_active = 0;
    cheng_mobile_gl_window = NULL;
    cheng_mobile_release_retired_windows();
    // Ensure a predictable buffer format for ANativeWindow_lock().
    if (windowWidth > 0 && windowHeight > 0) {
      ANativeWindow_setBuffersGeometry(window, windowWidth, windowHeight, WINDOW_FORMAT_RGBA_8888);
    }
  }

  int dstWidth = windowWidth > 0 ? windowWidth : width;
  int dstHeight = windowHeight > 0 ? windowHeight : height;
  if (dstWidth <= 0 || dstHeight <= 0) {
    ANativeWindow_release(window);
    return;
  }
  ANativeWindow_Buffer buffer;
  if (ANativeWindow_lock(window, &buffer, NULL) != 0) {
    ANativeWindow_release(window);
    return;
  }
  if (buffer.bits == NULL || buffer.width <= 0 || buffer.height <= 0 || buffer.stride <= 0) {
    ANativeWindow_unlockAndPost(window);
    ANativeWindow_release(window);
    return;
  }
  int bpp = cheng_mobile_bytes_per_pixel(buffer.format);
  if (bpp != 4 && bpp != 2) {
    ANativeWindow_unlockAndPost(window);
    ANativeWindow_release(window);
    return;
  }
  int copyWidth = cheng_mobile_min_int(width, buffer.width);
  int copyHeight = cheng_mobile_min_int(height, buffer.height);
  int maxSrcWidth = strideBytes / 4;
  if (maxSrcWidth > 0) {
    copyWidth = cheng_mobile_min_int(copyWidth, maxSrcWidth);
  }
  copyWidth = cheng_mobile_min_int(copyWidth, buffer.stride);
  const unsigned char* src = (const unsigned char*)pixels;
  unsigned char* dst = (unsigned char*)buffer.bits;
  int srcStride = strideBytes;
  int dstStride = buffer.stride * bpp;
  for (int y = 0; y < copyHeight; y++) {
    const uint32_t* srcRow = (const uint32_t*)(src + y * srcStride);
    unsigned char* dstRowBytes = (unsigned char*)(dst + y * dstStride);
    if (bpp == 4) {
      uint32_t* dstRow = (uint32_t*)dstRowBytes;
      for (int x = 0; x < copyWidth; x++) {
        uint32_t px = srcRow[x];
        /* Cheng pixels are BGRA (little-endian); Android RGBA_8888 expects RGBA. */
        uint32_t swapped = (px & 0xFF00FF00u) | ((px & 0x00FF0000u) >> 16) | ((px & 0x000000FFu) << 16);
        dstRow[x] = swapped;
      }
    } else {
      uint16_t* dstRow = (uint16_t*)dstRowBytes;
      for (int x = 0; x < copyWidth; x++) {
        uint32_t px = srcRow[x];
        uint32_t r = (px >> 16) & 0xFFu;
        uint32_t g = (px >> 8) & 0xFFu;
        uint32_t b = px & 0xFFu;
        uint16_t rgb565 = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        dstRow[x] = rgb565;
      }
    }
  }
  ANativeWindow_unlockAndPost(window);
  ANativeWindow_release(window);
}

void cheng_mobile_host_shutdown(const char* reason) {
  (void)reason;
  cheng_mobile_android_gl_shutdown();
  cheng_mobile_gl_active = 0;
  cheng_mobile_gl_window = NULL;
  pthread_mutex_lock(&cheng_mobile_surface_mutex);
  if (cheng_mobile_window != NULL) {
    cheng_mobile_retire_window_locked(cheng_mobile_window);
    cheng_mobile_window = NULL;
  }
  pthread_mutex_unlock(&cheng_mobile_surface_mutex);
  cheng_mobile_release_retired_windows();
  cheng_mobile_window_width = 0;
  cheng_mobile_window_height = 0;
  if (cheng_mobile_asset_root != NULL) {
    free(cheng_mobile_asset_root);
    cheng_mobile_asset_root = NULL;
  }
  __android_log_print(ANDROID_LOG_INFO, CHENG_LOG_TAG, "host shutdown");
}

void cheng_mobile_host_android_set_asset_root(const char* path) {
  if (cheng_mobile_asset_root != NULL) {
    free(cheng_mobile_asset_root);
    cheng_mobile_asset_root = NULL;
  }
  if (path == NULL) {
    unsetenv("CHENG_ASSET_ROOT");
    return;
  }
  size_t len = strlen(path);
  char* copy = (char*)malloc(len + 1);
  if (copy == NULL) {
    return;
  }
  memcpy(copy, path, len);
  copy[len] = '\0';
  cheng_mobile_asset_root = copy;
  setenv("CHENG_ASSET_ROOT", copy, 1);
}

const char* cheng_mobile_host_default_resource_root(void) {
  return cheng_mobile_asset_root;
}

int cheng_mobile_host_android_width(void) {
  pthread_mutex_lock(&cheng_mobile_surface_mutex);
  int w = cheng_mobile_window_width;
  pthread_mutex_unlock(&cheng_mobile_surface_mutex);
  return w;
}

int cheng_mobile_host_android_height(void) {
  pthread_mutex_lock(&cheng_mobile_surface_mutex);
  int h = cheng_mobile_window_height;
  pthread_mutex_unlock(&cheng_mobile_surface_mutex);
  return h;
}

#endif
