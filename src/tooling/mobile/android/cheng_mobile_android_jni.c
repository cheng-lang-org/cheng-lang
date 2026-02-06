#ifdef __ANDROID__

#include <jni.h>
#include <android/native_window_jni.h>
#include <pthread.h>
#include <stddef.h>
#include "cheng_mobile_host_api.h"
#include "cheng_mobile_host_android.h"

#ifndef CHENG_MOBILE_THREAD_STACK_BYTES
#define CHENG_MOBILE_THREAD_STACK_BYTES (8u * 1024u * 1024u)
#endif

static int cheng_mobile_action_to_kind(int action) {
  switch (action) {
    case 0: return CHENG_MRE_POINTER_DOWN;
    case 1: return CHENG_MRE_POINTER_UP;
    case 2: return CHENG_MRE_POINTER_MOVE;
    case 3: return CHENG_MRE_POINTER_UP;
    case 8: return CHENG_MRE_POINTER_SCROLL;
    default: return CHENG_MRE_POINTER_MOVE;
  }
}

__attribute__((weak)) void cheng_mobile_app_main(void);
__attribute__((weak)) int main(int argc, char **argv);

static int cheng_mobile_started = 0;
static pthread_t cheng_mobile_thread;

static void* cheng_mobile_entry_thread(void* arg) {
  (void)arg;
  if (cheng_mobile_app_main) {
    cheng_mobile_app_main();
    return NULL;
  }
  if (main) {
    (void)main(0, NULL);
  }
  return NULL;
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_start(
    JNIEnv* env,
    jclass cls
) {
  (void)env;
  (void)cls;
  if (cheng_mobile_started) {
    return;
  }
  cheng_mobile_started = 1;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  (void)pthread_attr_setstacksize(&attr, (size_t)CHENG_MOBILE_THREAD_STACK_BYTES);

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
    jfloat x,
    jfloat y,
    jfloat dx,
    jfloat dy,
    jint button
) {
  (void)env;
  (void)cls;
  int kind = cheng_mobile_action_to_kind(action);
  cheng_mobile_host_emit_pointer((int)windowId, kind, x, y, dx, dy, (int)button);
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onKey(
    JNIEnv* env,
    jclass cls,
    jint windowId,
    jint keyCode,
    jboolean down,
    jboolean repeat
) {
  (void)env;
  (void)cls;
  cheng_mobile_host_emit_key((int)windowId, (int)keyCode, down ? 1 : 0, repeat ? 1 : 0);
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onText(
    JNIEnv* env,
    jclass cls,
    jint windowId,
    jstring text
) {
  (void)cls;
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
  (void)env;
  (void)cls;
  cheng_mobile_host_emit_frame_tick();
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onSurface(
    JNIEnv* env,
    jclass cls,
    jobject surface,
    jint width,
    jint height
) {
  (void)cls;
  if (surface == NULL) {
    cheng_mobile_host_android_set_surface(NULL, 0, 0);
    return;
  }
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == NULL) {
    return;
  }
  cheng_mobile_host_android_set_surface(window, (int)width, (int)height);
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_setAssetRoot(
    JNIEnv* env,
    jclass cls,
    jstring path
) {
  (void)cls;
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

#endif
