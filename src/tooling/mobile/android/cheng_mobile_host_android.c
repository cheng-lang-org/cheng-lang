#ifdef __ANDROID__

#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <math.h>
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cheng_mobile_bridge.h"
#include "cheng_mobile_android_gl.h"
#include "cheng_mobile_host_android.h"
#include "cheng_mobile_host_core.h"

#define LOG_TAG "ChengMobile"

static ANativeWindow* cheng_mobile_window = NULL;
static int cheng_mobile_window_width = 0;
static int cheng_mobile_window_height = 0;
static int cheng_mobile_surface_generation = 0;
static int cheng_mobile_present_count = 0;
static int cheng_mobile_poll_count = 0;
static int cheng_mobile_poll_non_empty = 0;
static ChengMobileEvent cheng_mobile_cached_event;
static int cheng_mobile_cached_event_valid = 0;
static double cheng_mobile_density_scale = 0.0;

/* EGL/GL resources are owned by the Cheng runtime thread. */
static int cheng_mobile_gl_active = 0;
static int cheng_mobile_gl_pending_shutdown = 0;
static int cheng_mobile_gl_pending_resize = 0;
static int cheng_mobile_gl_try_init = 0;

static char* cheng_mobile_asset_root = NULL;
static ANativeActivity* cheng_mobile_activity = NULL;

static JavaVM* cheng_mobile_jvm = NULL;
static jclass cheng_mobile_cheng_native_class = NULL;
static jmethodID cheng_mobile_mid_set_ime_visible = NULL;

static pthread_mutex_t cheng_mobile_mutex = PTHREAD_MUTEX_INITIALIZER;

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

static void cheng_mobile_host_android_call_java_set_ime_visible(int visible) {
  JavaVM* jvm = NULL;
  jclass cls = NULL;
  jmethodID mid = NULL;

  pthread_mutex_lock(&cheng_mobile_mutex);
  jvm = cheng_mobile_jvm;
  cls = cheng_mobile_cheng_native_class;
  mid = cheng_mobile_mid_set_ime_visible;
  pthread_mutex_unlock(&cheng_mobile_mutex);

  if (jvm == NULL || cls == NULL || mid == NULL) {
    return;
  }

  JNIEnv* env = NULL;
  int attached = 0;
  if ((*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) {
      return;
    }
    attached = 1;
  }

  (*env)->CallStaticVoidMethod(env, cls, mid, visible ? JNI_TRUE : JNI_FALSE);
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
  }

  if (attached) {
    (void)(*jvm)->DetachCurrentThread(jvm);
  }
}

void cheng_mobile_host_android_set_native_activity(ANativeActivity* activity) {
  pthread_mutex_lock(&cheng_mobile_mutex);
  cheng_mobile_activity = activity;
  if (cheng_mobile_activity && cheng_mobile_activity->vm) {
    cheng_mobile_jvm = cheng_mobile_activity->vm;
  }
  pthread_mutex_unlock(&cheng_mobile_mutex);
}

void cheng_mobile_host_android_jni_register(JNIEnv* env) {
  if (env == NULL) {
    return;
  }
  pthread_mutex_lock(&cheng_mobile_mutex);
  if (cheng_mobile_jvm == NULL) {
    (void)(*env)->GetJavaVM(env, &cheng_mobile_jvm);
  }
  if (cheng_mobile_cheng_native_class == NULL) {
    jclass local = (*env)->FindClass(env, "com/cheng/mobile/ChengNative");
    if (local != NULL) {
      cheng_mobile_cheng_native_class = (jclass)(*env)->NewGlobalRef(env, local);
      (*env)->DeleteLocalRef(env, local);
      if (cheng_mobile_cheng_native_class != NULL) {
        cheng_mobile_mid_set_ime_visible =
            (*env)->GetStaticMethodID(env, cheng_mobile_cheng_native_class, "setImeVisible", "(Z)V");
      }
    }
    if ((*env)->ExceptionCheck(env)) {
      (*env)->ExceptionClear(env);
    }
  }
  pthread_mutex_unlock(&cheng_mobile_mutex);
}

void cheng_mobile_host_android_set_ime_visible(int visible) {
  /* Preferred path: Kotlin host + View/InputConnection. */
  cheng_mobile_host_android_call_java_set_ime_visible(visible);

  /* NativeActivity fallback: can show/hide keyboard, but composition is limited. */
  pthread_mutex_lock(&cheng_mobile_mutex);
  ANativeActivity* activity = cheng_mobile_activity;
  pthread_mutex_unlock(&cheng_mobile_mutex);
  if (activity == NULL) {
    return;
  }
  if (visible) {
    ANativeActivity_showSoftInput(activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
  } else {
    ANativeActivity_hideSoftInput(activity, 0);
  }
}

void cheng_mobile_host_android_set_surface(ANativeWindow* window, int width, int height) {
  pthread_mutex_lock(&cheng_mobile_mutex);
  int sameWindow = (window != NULL && window == cheng_mobile_window);
  int resolvedWidth = width;
  int resolvedHeight = height;
  if (window != NULL) {
    if (resolvedWidth <= 0) {
      resolvedWidth = ANativeWindow_getWidth(window);
    }
    if (resolvedHeight <= 0) {
      resolvedHeight = ANativeWindow_getHeight(window);
    }
  }

  if (window == NULL) {
    if (cheng_mobile_window != NULL) {
      ANativeWindow_release(cheng_mobile_window);
      cheng_mobile_window = NULL;
    }
    cheng_mobile_window_width = 0;
    cheng_mobile_window_height = 0;
    cheng_mobile_gl_pending_shutdown = 1;
    cheng_mobile_gl_pending_resize = 0;
    cheng_mobile_gl_try_init = 0;
    cheng_mobile_surface_generation += 1;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "surface detached gen=%d", cheng_mobile_surface_generation);
    pthread_mutex_unlock(&cheng_mobile_mutex);
    return;
  }

  if (!sameWindow) {
    ANativeWindow_acquire(window);
    if (cheng_mobile_window != NULL) {
      ANativeWindow_release(cheng_mobile_window);
    }
    cheng_mobile_window = window;
    cheng_mobile_gl_pending_shutdown = 1;
    cheng_mobile_gl_try_init = 1;
  } else {
    if (cheng_mobile_window_width != resolvedWidth || cheng_mobile_window_height != resolvedHeight) {
      cheng_mobile_gl_pending_resize = 1;
      cheng_mobile_gl_try_init = 1;
    }
  }

  cheng_mobile_window_width = resolvedWidth;
  cheng_mobile_window_height = resolvedHeight;
  cheng_mobile_surface_generation += 1;
  __android_log_print(
      ANDROID_LOG_INFO,
      LOG_TAG,
      "surface attached gen=%d size=%dx%d same=%d",
      cheng_mobile_surface_generation,
      cheng_mobile_window_width,
      cheng_mobile_window_height,
      sameWindow ? 1 : 0
  );
  pthread_mutex_unlock(&cheng_mobile_mutex);
}

static void cheng_mobile_host_android_process_surface_changes(void) {
  ANativeWindow* window = NULL;
  int width = 0;
  int height = 0;
  int needShutdown = 0;
  int needResize = 0;
  int tryInit = 0;

  pthread_mutex_lock(&cheng_mobile_mutex);
  window = cheng_mobile_window;
  if (window != NULL) {
    ANativeWindow_acquire(window);
  }
  width = cheng_mobile_window_width;
  height = cheng_mobile_window_height;
  needShutdown = cheng_mobile_gl_pending_shutdown;
  needResize = cheng_mobile_gl_pending_resize;
  tryInit = cheng_mobile_gl_try_init;
  cheng_mobile_gl_pending_shutdown = 0;
  cheng_mobile_gl_pending_resize = 0;
  cheng_mobile_gl_try_init = 0;
  pthread_mutex_unlock(&cheng_mobile_mutex);

  if (needShutdown && cheng_mobile_gl_active) {
    cheng_mobile_android_gl_shutdown();
    cheng_mobile_gl_active = 0;
  }

  if (!cheng_mobile_gl_active && tryInit && window != NULL && width > 0 && height > 0) {
    if (cheng_mobile_android_gl_init(window, width, height)) {
      cheng_mobile_gl_active = 1;
    } else {
      ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888);
    }
  }

  if (cheng_mobile_gl_active && needResize && width > 0 && height > 0) {
    cheng_mobile_android_gl_resize(width, height);
  }

  if (window != NULL) {
    ANativeWindow_release(window);
  }
}

int cheng_mobile_host_init(const ChengMobileConfig* cfg) {
  (void)cfg;
  cheng_mobile_host_core_reset();
  ChengMobileEvent ev = {0};
  ev.kind = MRE_RUNTIME_STARTED;
  cheng_mobile_host_core_push(&ev);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "host init");
  return 0;
}

int cheng_mobile_host_open_window(const ChengMobileConfig* cfg) {
  int windowId = 1;
  ChengMobileEvent ev = {0};
  ev.kind = MRE_WINDOW_OPENED;
  ev.windowId = windowId;
  ev.message = cfg ? cfg->title : "Cheng Mobile";
  cheng_mobile_host_core_push(&ev);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "open window");
  return windowId;
}

int cheng_mobile_host_poll_event(ChengMobileEvent* outEvent) {
  cheng_mobile_poll_count += 1;
  cheng_mobile_host_android_pump_events();
  cheng_mobile_host_android_process_surface_changes();
  int got = cheng_mobile_host_core_pop(outEvent);
  if (got != 0) {
    cheng_mobile_poll_non_empty += 1;
    if (cheng_mobile_poll_non_empty <= 12 ||
        outEvent->kind == MRE_RUNTIME_STARTED ||
        outEvent->kind == MRE_WINDOW_OPENED ||
        outEvent->kind == MRE_RUNTIME_STOPPED ||
        outEvent->kind == MRE_POINTER_DOWN ||
        outEvent->kind == MRE_POINTER_UP ||
        outEvent->kind == MRE_POINTER_MOVE ||
        outEvent->kind == MRE_POINTER_SCROLL ||
        (outEvent->kind == MRE_FRAME_TICK && (cheng_mobile_poll_non_empty % 240) == 0)) {
      __android_log_print(
          ANDROID_LOG_INFO,
          LOG_TAG,
          "poll event kind=%d nonEmpty=%d polls=%d pid=%d x=%.1f y=%.1f",
          outEvent->kind,
          cheng_mobile_poll_non_empty,
          cheng_mobile_poll_count,
          outEvent->pointerId,
          outEvent->pointerX,
          outEvent->pointerY);
    }
  } else if (cheng_mobile_poll_count <= 8 || (cheng_mobile_poll_count % 2000) == 0) {
    __android_log_print(
        ANDROID_LOG_INFO,
        LOG_TAG,
        "poll empty polls=%d nonEmpty=%d",
        cheng_mobile_poll_count,
        cheng_mobile_poll_non_empty);
  }
  return got;
}

int cheng_mobile_host_poll_event_cached(void) {
  cheng_mobile_poll_count += 1;
  cheng_mobile_host_android_pump_events();
  cheng_mobile_host_android_process_surface_changes();
  int got = cheng_mobile_host_core_pop(&cheng_mobile_cached_event);
  cheng_mobile_cached_event_valid = got;
  if (got != 0) {
    cheng_mobile_poll_non_empty += 1;
    if (cheng_mobile_poll_non_empty <= 12 ||
        cheng_mobile_cached_event.kind == MRE_RUNTIME_STARTED ||
        cheng_mobile_cached_event.kind == MRE_WINDOW_OPENED ||
        cheng_mobile_cached_event.kind == MRE_RUNTIME_STOPPED ||
        cheng_mobile_cached_event.kind == MRE_POINTER_DOWN ||
        cheng_mobile_cached_event.kind == MRE_POINTER_UP ||
        cheng_mobile_cached_event.kind == MRE_POINTER_MOVE ||
        cheng_mobile_cached_event.kind == MRE_POINTER_SCROLL ||
        (cheng_mobile_cached_event.kind == MRE_FRAME_TICK && (cheng_mobile_poll_non_empty % 240) == 0)) {
      __android_log_print(
          ANDROID_LOG_INFO,
          LOG_TAG,
          "poll cached kind=%d nonEmpty=%d polls=%d pid=%d x=%.1f y=%.1f",
          cheng_mobile_cached_event.kind,
          cheng_mobile_poll_non_empty,
          cheng_mobile_poll_count,
          cheng_mobile_cached_event.pointerId,
          cheng_mobile_cached_event.pointerX,
          cheng_mobile_cached_event.pointerY);
    }
  } else if (cheng_mobile_poll_count <= 8 || (cheng_mobile_poll_count % 2000) == 0) {
    __android_log_print(
        ANDROID_LOG_INFO,
        LOG_TAG,
        "poll cached empty polls=%d nonEmpty=%d",
        cheng_mobile_poll_count,
        cheng_mobile_poll_non_empty);
  }
  return got;
}

int cheng_mobile_host_cached_event_kind(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.kind : MRE_FRAME_TICK;
}

int cheng_mobile_host_cached_event_window_id(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.windowId : 0;
}

double cheng_mobile_host_cached_event_pointer_x(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.pointerX : 0.0;
}

double cheng_mobile_host_cached_event_pointer_y(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.pointerY : 0.0;
}

double cheng_mobile_host_cached_event_pointer_delta_x(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.pointerDeltaX : 0.0;
}

double cheng_mobile_host_cached_event_pointer_delta_y(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.pointerDeltaY : 0.0;
}

int cheng_mobile_host_cached_event_pointer_x_i32(void) {
  return cheng_mobile_cached_event_valid ? (int)llround(cheng_mobile_cached_event.pointerX) : 0;
}

int cheng_mobile_host_cached_event_pointer_y_i32(void) {
  return cheng_mobile_cached_event_valid ? (int)llround(cheng_mobile_cached_event.pointerY) : 0;
}

int cheng_mobile_host_cached_event_pointer_delta_x_i32(void) {
  return cheng_mobile_cached_event_valid ? (int)llround(cheng_mobile_cached_event.pointerDeltaX) : 0;
}

int cheng_mobile_host_cached_event_pointer_delta_y_i32(void) {
  return cheng_mobile_cached_event_valid ? (int)llround(cheng_mobile_cached_event.pointerDeltaY) : 0;
}

int cheng_mobile_host_cached_event_pointer_button(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.pointerButton : 0;
}

int cheng_mobile_host_cached_event_pointer_id(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.pointerId : 0;
}

int64_t cheng_mobile_host_cached_event_time_ms(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.eventTimeMs : 0;
}

int cheng_mobile_host_cached_event_key_code(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.keyCode : 0;
}

int cheng_mobile_host_cached_event_key_repeat(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.keyRepeat : 0;
}

const char* cheng_mobile_host_cached_event_text(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.text : NULL;
}

const char* cheng_mobile_host_cached_event_message(void) {
  return cheng_mobile_cached_event_valid ? cheng_mobile_cached_event.message : NULL;
}

void cheng_mobile_host_present(const void* pixels, int width, int height, int strideBytes) {
  if (pixels == NULL) {
    return;
  }

  /* Drive EGL init/shutdown/resize on the Cheng runtime thread. */
  cheng_mobile_host_android_process_surface_changes();

  ANativeWindow* window = NULL;
  int winW = 0;
  int winH = 0;
  pthread_mutex_lock(&cheng_mobile_mutex);
  window = cheng_mobile_window;
  if (window != NULL) {
    ANativeWindow_acquire(window);
  }
  winW = cheng_mobile_window_width;
  winH = cheng_mobile_window_height;
  pthread_mutex_unlock(&cheng_mobile_mutex);

  if (window == NULL) {
    if (cheng_mobile_present_count < 5) {
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "present skipped: no window");
      cheng_mobile_present_count += 1;
    }
    return;
  }

  if (cheng_mobile_gl_active) {
    if (cheng_mobile_present_count <= 5 || (cheng_mobile_present_count % 120) == 0) {
      __android_log_print(
          ANDROID_LOG_INFO,
          LOG_TAG,
          "present gl frame=%d src=%dx%d stride=%d win=%dx%d",
          cheng_mobile_present_count,
          width,
          height,
          strideBytes,
          winW,
          winH
      );
    }
    if (cheng_mobile_android_gl_present(pixels, width, height, strideBytes)) {
      cheng_mobile_present_count += 1;
      ANativeWindow_release(window);
      return;
    }
    cheng_mobile_android_gl_shutdown();
    cheng_mobile_gl_active = 0;
    if (winW > 0 && winH > 0) {
      ANativeWindow_setBuffersGeometry(window, winW, winH, WINDOW_FORMAT_RGBA_8888);
    }
  }

  ANativeWindow_Buffer buffer;
  if (ANativeWindow_lock(window, &buffer, NULL) != 0) {
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "ANativeWindow_lock failed");
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
  if (cheng_mobile_present_count <= 5 || (cheng_mobile_present_count % 120) == 0) {
    __android_log_print(
        ANDROID_LOG_INFO,
        LOG_TAG,
        "present sw frame=%d src=%dx%d stride=%d dst=%dx%d bufStride=%d fmt=%d",
        cheng_mobile_present_count,
        width,
        height,
        strideBytes,
        buffer.width,
        buffer.height,
        buffer.stride,
        buffer.format
    );
  }
  cheng_mobile_present_count += 1;
  ANativeWindow_release(window);
}

void cheng_mobile_host_shutdown(const char* reason) {
  (void)reason;
  cheng_mobile_android_gl_shutdown();
  cheng_mobile_gl_active = 0;

  pthread_mutex_lock(&cheng_mobile_mutex);
  if (cheng_mobile_window != NULL) {
    ANativeWindow_release(cheng_mobile_window);
    cheng_mobile_window = NULL;
  }
  cheng_mobile_window_width = 0;
  cheng_mobile_window_height = 0;
  cheng_mobile_gl_pending_shutdown = 0;
  cheng_mobile_gl_pending_resize = 0;
  cheng_mobile_gl_try_init = 0;
  pthread_mutex_unlock(&cheng_mobile_mutex);

  if (cheng_mobile_asset_root != NULL) {
    free(cheng_mobile_asset_root);
    cheng_mobile_asset_root = NULL;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "host shutdown");
}

void cheng_mobile_host_android_set_asset_root(const char* path) {
  if (cheng_mobile_asset_root != NULL) {
    free(cheng_mobile_asset_root);
    cheng_mobile_asset_root = NULL;
  }
  if (path == NULL) {
    unsetenv("ASSET_ROOT");
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
  setenv("ASSET_ROOT", copy, 1);
}

const char* cheng_mobile_host_default_resource_root(void) {
  return cheng_mobile_asset_root;
}

int cheng_mobile_host_android_width(void) {
  pthread_mutex_lock(&cheng_mobile_mutex);
  int w = cheng_mobile_window_width;
  pthread_mutex_unlock(&cheng_mobile_mutex);
  return w;
}

int cheng_mobile_host_android_height(void) {
  pthread_mutex_lock(&cheng_mobile_mutex);
  int h = cheng_mobile_window_height;
  pthread_mutex_unlock(&cheng_mobile_mutex);
  return h;
}

int cheng_mobile_host_android_surface_generation(void) {
  pthread_mutex_lock(&cheng_mobile_mutex);
  int generation = cheng_mobile_surface_generation;
  pthread_mutex_unlock(&cheng_mobile_mutex);
  return generation;
}

void cheng_mobile_host_android_set_density_scale(double scale) {
  if (!(scale > 0.0)) {
    return;
  }
  if (scale < 0.5) {
    scale = 0.5;
  }
  if (scale > 6.0) {
    scale = 6.0;
  }
  pthread_mutex_lock(&cheng_mobile_mutex);
  cheng_mobile_density_scale = scale;
  pthread_mutex_unlock(&cheng_mobile_mutex);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "density scale set=%.3f", scale);
}

double cheng_mobile_host_android_density_scale(void) {
  pthread_mutex_lock(&cheng_mobile_mutex);
  double scale = cheng_mobile_density_scale;
  pthread_mutex_unlock(&cheng_mobile_mutex);
  return scale;
}

int cheng_mobile_host_dp_to_px(double dp, double scale) {
  double s = scale;
  if (!(s > 0.0)) {
    s = 1.0;
  }
  if (!(dp > 0.0)) {
    return 0;
  }
  double value = dp * s;
  if (!(value > 0.0)) {
    return 0;
  }
  int px = (int)llround(value);
  if (px <= 0) {
    px = 1;
  }
  return px;
}

void cheng_mobile_host_debug_state(const char* phase, int a, int b, int c, int d) {
  __android_log_print(
      ANDROID_LOG_INFO,
      LOG_TAG,
      "dbg %s a=%d b=%d c=%d d=%d",
      phase ? phase : "-",
      a,
      b,
      c,
      d);
}

#endif
