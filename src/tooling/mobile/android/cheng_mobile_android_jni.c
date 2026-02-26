#ifdef __ANDROID__

#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "cheng_mobile_host_api.h"
#include "cheng_mobile_host_android.h"
#if defined(__has_include)
#  if __has_include("cheng_mobile_exports.h")
#    include "cheng_mobile_exports.h"
#  endif
#endif

#ifndef MOBILE_THREAD_STACK_BYTES
#define MOBILE_THREAD_STACK_BYTES (8u * 1024u * 1024u)
#endif

#define LOG_TAG "ChengMobile"

static int cheng_mobile_action_to_kind(int action) {
  switch (action) {
    case 0: return MRE_POINTER_DOWN;
    case 1: return MRE_POINTER_UP;
    case 2: return MRE_POINTER_MOVE;
    case 3: return MRE_POINTER_UP;
    case 5: return MRE_POINTER_DOWN;
    case 6: return MRE_POINTER_UP;
    case 8: return MRE_POINTER_SCROLL;
    default: return MRE_POINTER_MOVE;
  }
}

__attribute__((weak)) void cheng_mobile_app_main(void);
__attribute__((weak)) int main(int argc, char **argv);
extern uint64_t cheng_app_init(void) __attribute__((weak));
extern void cheng_app_set_window(uint64_t app_id, uint64_t window_id, int physical_w, int physical_h, float scale) __attribute__((weak));
extern void cheng_app_tick(uint64_t app_id, float delta_time) __attribute__((weak));
extern void cheng_app_on_touch(uint64_t app_id, int action, int pointer_id, float x, float y) __attribute__((weak));
extern void cheng_app_pause(uint64_t app_id) __attribute__((weak));
extern void cheng_app_resume(uint64_t app_id) __attribute__((weak));

static int cheng_mobile_started = 0;
static pthread_t cheng_mobile_thread;
static uint64_t s_app_id = 0u;
static int s_frame_count = 0;
static int s_touch_count = 0;
static double s_density_scale = 1.0;

static int cheng_mobile_has_app_abi_v2(void) {
  return cheng_app_init != NULL &&
         cheng_app_set_window != NULL &&
         cheng_app_tick != NULL &&
         cheng_app_on_touch != NULL &&
         cheng_app_pause != NULL &&
         cheng_app_resume != NULL;
}

static void* cheng_mobile_entry_thread(void* arg) {
  (void)arg;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "entry thread start app_main=%p main=%p", cheng_mobile_app_main, main);
  if (cheng_mobile_app_main) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "entry calling cheng_mobile_app_main");
    cheng_mobile_app_main();
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "entry cheng_mobile_app_main returned");
    return NULL;
  }
  if (main) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "entry calling main");
    (void)main(0, NULL);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "entry main returned");
    return NULL;
  }
  __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "entry no main symbol");
  return NULL;
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_start(
    JNIEnv* env,
    jclass cls
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  if (cheng_mobile_started) {
    return;
  }
  if (cheng_mobile_has_app_abi_v2()) {
    s_app_id = cheng_app_init();
    cheng_mobile_started = (s_app_id != 0u);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "start ABI v2 app_id=%llu", (unsigned long long)s_app_id);
    return;
  }

  cheng_mobile_started = 1;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  (void)pthread_attr_setstacksize(&attr, (size_t)MOBILE_THREAD_STACK_BYTES);

  int err = pthread_create(&cheng_mobile_thread, &attr, cheng_mobile_entry_thread, NULL);
  pthread_attr_destroy(&attr);
  if (err != 0) {
    cheng_mobile_started = 0;
    return;
  }
  pthread_detach(cheng_mobile_thread);
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onTouch(
    JNIEnv* env,
    jclass cls,
    jint windowId,
    jint action,
    jint pointerId,
    jlong timeMs,
    jfloat x,
    jfloat y,
    jfloat dx,
    jfloat dy,
    jint button
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  int kind = cheng_mobile_action_to_kind(action);
  if (s_touch_count < 64) {
    s_touch_count += 1;
    __android_log_print(
        ANDROID_LOG_INFO,
        LOG_TAG,
        "jni onTouch#%d action=%d kind=%d pid=%d x=%.1f y=%.1f dx=%.1f dy=%.1f",
        s_touch_count,
        (int)action,
        kind,
        (int)pointerId,
        (double)x,
        (double)y,
        (double)dx,
        (double)dy);
  }
  if (cheng_mobile_has_app_abi_v2() && s_app_id != 0u) {
    cheng_app_on_touch(s_app_id, (int)action, (int)pointerId, x, y);
  } else {
    cheng_mobile_host_emit_pointer((int)windowId, kind, x, y, dx, dy, (int)button, (int)pointerId, (int64_t)timeMs);
  }
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onKey(
    JNIEnv* env,
    jclass cls,
    jint windowId,
    jint keyCode,
    jboolean down,
    jboolean repeat
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  cheng_mobile_host_emit_key((int)windowId, (int)keyCode, down ? 1 : 0, repeat ? 1 : 0);
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onText(
    JNIEnv* env,
    jclass cls,
    jint windowId,
    jstring text
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  if (!text) {
    return;
  }
  const char* utf = (*env)->GetStringUTFChars(env, text, 0);
  if (utf) {
    cheng_mobile_host_emit_text((int)windowId, utf);
    (*env)->ReleaseStringUTFChars(env, text, utf);
  }
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onFrame(
    JNIEnv* env,
    jclass cls
) {
  (void)cls;
  s_frame_count += 1;
  if (s_frame_count <= 5 || (s_frame_count % 120) == 0) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "jni onFrame count=%d", s_frame_count);
  }
  cheng_mobile_host_android_jni_register(env);
  if (cheng_mobile_has_app_abi_v2() && s_app_id != 0u) {
    cheng_app_tick(s_app_id, 1.0f / 60.0f);
  } else {
    cheng_mobile_host_emit_frame_tick();
  }
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onSurface(
    JNIEnv* env,
    jclass cls,
    jobject surface,
    jint width,
    jint height
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  if (surface == NULL) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "jni onSurface null");
    cheng_mobile_host_android_set_surface(NULL, 0, 0);
    if (cheng_mobile_has_app_abi_v2() && s_app_id != 0u) {
      cheng_app_set_window(s_app_id, 0u, 0, 0, (float)s_density_scale);
    }
    return;
  }
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == NULL) {
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "jni onSurface: ANativeWindow_fromSurface failed");
    return;
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "jni onSurface size=%dx%d", (int)width, (int)height);
  cheng_mobile_host_android_set_surface(window, (int)width, (int)height);
  if (cheng_mobile_has_app_abi_v2()) {
    if (s_app_id == 0u) {
      s_app_id = cheng_app_init();
    }
    if (s_app_id != 0u) {
      cheng_app_set_window(s_app_id, (uint64_t)(uintptr_t)window, (int)width, (int)height, (float)s_density_scale);
    }
  }
  ANativeWindow_release(window);
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_setAssetRoot(
    JNIEnv* env,
    jclass cls,
    jstring path
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  if (path == NULL) {
    cheng_mobile_host_android_set_asset_root(NULL);
    return;
  }
  const char* utf = (*env)->GetStringUTFChars(env, path, 0);
  if (utf) {
    cheng_mobile_host_android_set_asset_root(utf);
    (*env)->ReleaseStringUTFChars(env, path, utf);
  }
}

JNIEXPORT jstring JNICALL Java_com_cheng_mobile_ChengNative_pollBusRequestEnvelope(
    JNIEnv* env,
    jclass cls
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* raw = cheng_mobile_host_bus_poll_request_envelope();
  if (raw == NULL) {
    raw = "";
  }
  return (*env)->NewStringUTF(env, raw);
}

JNIEXPORT jint JNICALL Java_com_cheng_mobile_ChengNative_pushBusResponseEnvelope(
    JNIEnv* env,
    jclass cls,
    jstring response
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  if (response == NULL) {
    return 0;
  }
  const char* utf = (*env)->GetStringUTFChars(env, response, 0);
  if (utf == NULL) {
    return 0;
  }
  const int ok = cheng_mobile_host_bus_push_response_envelope(utf);
  (*env)->ReleaseStringUTFChars(env, response, utf);
  return ok;
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_setDensityScale(
    JNIEnv* env,
    jclass cls,
    jfloat scale
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  s_density_scale = (double)scale;
  cheng_mobile_host_android_set_density_scale((double)scale);
  if (cheng_mobile_has_app_abi_v2() && s_app_id != 0u) {
    cheng_app_set_window(
        s_app_id,
        0u,
        cheng_mobile_host_android_width(),
        cheng_mobile_host_android_height(),
        (float)s_density_scale);
  }
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onPauseNative(
    JNIEnv* env,
    jclass cls
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  if (cheng_mobile_has_app_abi_v2() && s_app_id != 0u) {
    cheng_app_pause(s_app_id);
  }
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onResumeNative(
    JNIEnv* env,
    jclass cls
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  if (cheng_mobile_has_app_abi_v2() && s_app_id != 0u) {
    cheng_app_resume(s_app_id);
    cheng_app_set_window(
        s_app_id,
        0u,
        cheng_mobile_host_android_width(),
        cheng_mobile_host_android_height(),
        (float)s_density_scale);
  }
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_setLaunchArgs(
    JNIEnv* env,
    jclass cls,
    jstring argsKv,
    jstring argsJson
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* kv = NULL;
  const char* js = NULL;
  if (argsKv != NULL) {
    kv = (*env)->GetStringUTFChars(env, argsKv, 0);
  }
  if (argsJson != NULL) {
    js = (*env)->GetStringUTFChars(env, argsJson, 0);
  }
  cheng_mobile_host_runtime_set_launch_args(kv, js);
  if (kv != NULL) {
    (*env)->ReleaseStringUTFChars(env, argsKv, kv);
  }
  if (js != NULL) {
    (*env)->ReleaseStringUTFChars(env, argsJson, js);
  }
}

JNIEXPORT jstring JNICALL Java_com_cheng_mobile_ChengNative_runtimeStateJson(
    JNIEnv* env,
    jclass cls
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* raw = cheng_mobile_host_runtime_state_json();
  if (raw == NULL || raw[0] == '\0') {
    raw = "{\"native_ready\":false}";
  }
  return (*env)->NewStringUTF(env, raw);
}

#endif
