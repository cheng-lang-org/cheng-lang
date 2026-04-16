#ifdef __ANDROID__

#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
extern void cheng_app_on_key(uint64_t app_id, int key_code, int down, int repeat) __attribute__((weak));
extern void cheng_app_on_text_input(uint64_t app_id, const char* text_utf8) __attribute__((weak));
extern void cheng_app_on_ime(uint64_t app_id, int ime_visible, const char* composing_utf8, int cursor_start, int cursor_end) __attribute__((weak));
extern void cheng_app_on_resize(uint64_t app_id, int physical_w, int physical_h, float scale) __attribute__((weak));
extern void cheng_app_on_focus(uint64_t app_id, int focused) __attribute__((weak));
extern void cheng_app_pause(uint64_t app_id) __attribute__((weak));
extern void cheng_app_resume(uint64_t app_id) __attribute__((weak));

static int cheng_mobile_started = 0;
static pthread_t cheng_mobile_thread;
static uint64_t s_app_id = 0u;
static int s_frame_count = 0;
static int s_touch_count = 0;
static double s_density_scale = 1.0;
static JavaVM* s_bio_jvm = NULL;
static jclass s_bio_native_class = NULL;
static jmethodID s_bio_fingerprint_authorize_mid = NULL;
static int s_host_api_injected = 0;

static int cheng_mobile_has_app_abi_v2(void) {
  return cheng_app_init != NULL &&
         cheng_app_set_window != NULL &&
         cheng_app_tick != NULL &&
         cheng_app_on_touch != NULL &&
         cheng_app_pause != NULL &&
         cheng_app_resume != NULL;
}

static int cheng_mobile_has_app_input_abi(void) {
  return cheng_app_on_key != NULL &&
         cheng_app_on_text_input != NULL &&
         cheng_app_on_ime != NULL &&
         cheng_app_on_resize != NULL &&
         cheng_app_on_focus != NULL;
}

static void cheng_mobile_safe_copy(char* dst, int32_t cap, const char* src) {
  if (dst == NULL || cap <= 0) {
    return;
  }
  if (src == NULL) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, (size_t)cap - 1u);
  dst[cap - 1] = '\0';
}

static int cheng_mobile_hex_nibble(char ch, uint8_t* out) {
  if (out == NULL) {
    return 0;
  }
  if (ch >= '0' && ch <= '9') {
    *out = (uint8_t)(ch - '0');
    return 1;
  }
  if (ch >= 'a' && ch <= 'f') {
    *out = (uint8_t)(10 + (ch - 'a'));
    return 1;
  }
  if (ch >= 'A' && ch <= 'F') {
    *out = (uint8_t)(10 + (ch - 'A'));
    return 1;
  }
  return 0;
}

static int cheng_mobile_find_wire_field(const char* wire,
                                        const char* key,
                                        const char** out_value,
                                        size_t* out_len) {
  if (wire == NULL || key == NULL || out_value == NULL || out_len == NULL) {
    return 0;
  }
  size_t key_len = strlen(key);
  const char* p = wire;
  while (*p != '\0') {
    const char* line_end = strchr(p, '\n');
    if (line_end == NULL) {
      line_end = p + strlen(p);
    }
    const char* eq = memchr(p, '=', (size_t)(line_end - p));
    if (eq != NULL && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0) {
      *out_value = eq + 1;
      *out_len = (size_t)(line_end - (eq + 1));
      return 1;
    }
    if (*line_end == '\0') {
      break;
    }
    p = line_end + 1;
  }
  return 0;
}

static int cheng_mobile_copy_wire_text_field(const char* wire,
                                             const char* key,
                                             char* out,
                                             int32_t out_cap) {
  const char* value = NULL;
  size_t len = 0u;
  if (!cheng_mobile_find_wire_field(wire, key, &value, &len)) {
    cheng_mobile_safe_copy(out, out_cap, "");
    return 1;
  }
  if (out == NULL || out_cap <= 0) {
    return 0;
  }
  if ((size_t)out_cap <= len) {
    return 0;
  }
  memcpy(out, value, len);
  out[len] = '\0';
  return 1;
}

static int cheng_mobile_copy_wire_hex_field(const char* wire,
                                            const char* key,
                                            char* out,
                                            int32_t out_cap) {
  const char* value = NULL;
  size_t len = 0u;
  if (!cheng_mobile_find_wire_field(wire, key, &value, &len)) {
    cheng_mobile_safe_copy(out, out_cap, "");
    return 1;
  }
  if (out == NULL || out_cap <= 0) {
    return 0;
  }
  if ((len % 2u) != 0u) {
    return 0;
  }
  size_t out_len = len / 2u;
  if ((size_t)out_cap <= out_len) {
    return 0;
  }
  for (size_t i = 0; i < out_len; i += 1u) {
    uint8_t hi = 0u;
    uint8_t lo = 0u;
    if (!cheng_mobile_hex_nibble(value[i * 2u], &hi) ||
        !cheng_mobile_hex_nibble(value[i * 2u + 1u], &lo)) {
      return 0;
    }
    out[i] = (char)((hi << 4u) | lo);
  }
  out[out_len] = '\0';
  return 1;
}

static void cheng_mobile_android_biometric_jni_register(JNIEnv* env) {
  if (env == NULL) {
    return;
  }
  if (s_bio_jvm == NULL) {
    (void)(*env)->GetJavaVM(env, &s_bio_jvm);
  }
  if (s_bio_native_class == NULL) {
    jclass local = (*env)->FindClass(env, "com/cheng/mobile/ChengNative");
    if (local != NULL) {
      s_bio_native_class = (jclass)(*env)->NewGlobalRef(env, local);
      (*env)->DeleteLocalRef(env, local);
    }
    if ((*env)->ExceptionCheck(env)) {
      (*env)->ExceptionClear(env);
    }
  }
  if (s_bio_native_class != NULL && s_bio_fingerprint_authorize_mid == NULL) {
    s_bio_fingerprint_authorize_mid = (*env)->GetStaticMethodID(
        env,
        s_bio_native_class,
        "biometricFingerprintAuthorize",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if ((*env)->ExceptionCheck(env)) {
      (*env)->ExceptionClear(env);
      s_bio_fingerprint_authorize_mid = NULL;
    }
  }
}

static void cheng_mobile_android_log_print(int32_t level, const char* msg) {
  int prio = ANDROID_LOG_INFO;
  if (level <= 1) {
    prio = ANDROID_LOG_ERROR;
  } else if (level == 2) {
    prio = ANDROID_LOG_WARN;
  } else if (level >= 4) {
    prio = ANDROID_LOG_DEBUG;
  }
  __android_log_print(prio, LOG_TAG, "%s", msg != NULL ? msg : "");
}

static int32_t cheng_mobile_android_biometric_fingerprint_authorize(
    const char* request_id,
    int32_t purpose,
    const char* did_text,
    const char* prompt_title,
    const char* prompt_reason,
    const char* device_binding_seed_hint,
    const char* device_label_hint,
    char* out_feature32_hex,
    int32_t out_feature32_cap,
    char* out_device_binding_seed,
    int32_t out_device_binding_seed_cap,
    char* out_device_label,
    int32_t out_device_label_cap,
    char* out_sensor_id,
    int32_t out_sensor_id_cap,
    char* out_hardware_attestation,
    int32_t out_hardware_attestation_cap,
    char* out_error,
    int32_t out_error_cap) {
  cheng_mobile_safe_copy(out_feature32_hex, out_feature32_cap, "");
  cheng_mobile_safe_copy(out_device_binding_seed, out_device_binding_seed_cap, "");
  cheng_mobile_safe_copy(out_device_label, out_device_label_cap, "");
  cheng_mobile_safe_copy(out_sensor_id, out_sensor_id_cap, "");
  cheng_mobile_safe_copy(out_hardware_attestation, out_hardware_attestation_cap, "");
  cheng_mobile_safe_copy(out_error, out_error_cap, "");
  if (s_bio_jvm == NULL || s_bio_native_class == NULL || s_bio_fingerprint_authorize_mid == NULL) {
    cheng_mobile_safe_copy(out_error, out_error_cap, "activity_required");
    return 0;
  }

  JNIEnv* env = NULL;
  int attached = 0;
  if ((*s_bio_jvm)->GetEnv(s_bio_jvm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
    if ((*s_bio_jvm)->AttachCurrentThread(s_bio_jvm, &env, NULL) != JNI_OK) {
      cheng_mobile_safe_copy(out_error, out_error_cap, "biometric_not_available");
      return 0;
    }
    attached = 1;
  }

  jstring j_request_id = NULL;
  jstring j_did_text = NULL;
  jstring j_prompt_title = NULL;
  jstring j_prompt_reason = NULL;
  jstring j_seed_hint = NULL;
  jstring j_label_hint = NULL;
  j_request_id = (*env)->NewStringUTF(env, request_id != NULL ? request_id : "");
  j_did_text = (*env)->NewStringUTF(env, did_text != NULL ? did_text : "");
  j_prompt_title = (*env)->NewStringUTF(env, prompt_title != NULL ? prompt_title : "");
  j_prompt_reason = (*env)->NewStringUTF(env, prompt_reason != NULL ? prompt_reason : "");
  j_seed_hint = (*env)->NewStringUTF(env, device_binding_seed_hint != NULL ? device_binding_seed_hint : "");
  j_label_hint = (*env)->NewStringUTF(env, device_label_hint != NULL ? device_label_hint : "");
  if (j_request_id == NULL || j_did_text == NULL || j_prompt_title == NULL ||
      j_prompt_reason == NULL || j_seed_hint == NULL || j_label_hint == NULL) {
    cheng_mobile_safe_copy(out_error, out_error_cap, "biometric_not_available");
    goto cleanup;
  }

  jstring response = (jstring)(*env)->CallStaticObjectMethod(
      env,
      s_bio_native_class,
      s_bio_fingerprint_authorize_mid,
      j_request_id,
      (jint)purpose,
      j_did_text,
      j_prompt_title,
      j_prompt_reason,
      j_seed_hint,
      j_label_hint);
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    cheng_mobile_safe_copy(out_error, out_error_cap, "biometric_not_available");
    goto cleanup;
  }
  if (response == NULL) {
    cheng_mobile_safe_copy(out_error, out_error_cap, "biometric_not_available");
    goto cleanup;
  }

  const char* wire = (*env)->GetStringUTFChars(env, response, 0);
  if (wire == NULL) {
    cheng_mobile_safe_copy(out_error, out_error_cap, "biometric_not_available");
    (*env)->DeleteLocalRef(env, response);
    goto cleanup;
  }

  char ok_text[16];
  if (!cheng_mobile_copy_wire_text_field(wire, "ok", ok_text, (int32_t)sizeof(ok_text))) {
    cheng_mobile_safe_copy(out_error, out_error_cap, "v3 biometric: invalid android wire");
    (*env)->ReleaseStringUTFChars(env, response, wire);
    (*env)->DeleteLocalRef(env, response);
    goto cleanup;
  }
  int parse_ok = 1;
  if (strcmp(ok_text, "1") == 0) {
    parse_ok = cheng_mobile_copy_wire_text_field(wire, "feature32_hex", out_feature32_hex, out_feature32_cap) &&
               cheng_mobile_copy_wire_hex_field(wire, "device_binding_seed_hex", out_device_binding_seed, out_device_binding_seed_cap) &&
               cheng_mobile_copy_wire_hex_field(wire, "device_label_hex", out_device_label, out_device_label_cap) &&
               cheng_mobile_copy_wire_hex_field(wire, "sensor_id_hex", out_sensor_id, out_sensor_id_cap) &&
               cheng_mobile_copy_wire_hex_field(wire, "hardware_attestation_hex", out_hardware_attestation, out_hardware_attestation_cap);
    if (!parse_ok || out_feature32_hex == NULL || out_feature32_hex[0] == '\0') {
      cheng_mobile_safe_copy(out_error, out_error_cap, "v3 biometric: invalid android wire");
      (*env)->ReleaseStringUTFChars(env, response, wire);
      (*env)->DeleteLocalRef(env, response);
      goto cleanup;
    }
    (*env)->ReleaseStringUTFChars(env, response, wire);
    (*env)->DeleteLocalRef(env, response);
    if (attached) {
      (void)(*s_bio_jvm)->DetachCurrentThread(s_bio_jvm);
    }
    return 1;
  }

  if (!cheng_mobile_copy_wire_hex_field(wire, "error_hex", out_error, out_error_cap) ||
      out_error == NULL || out_error[0] == '\0') {
    cheng_mobile_safe_copy(out_error, out_error_cap, "biometric_not_available");
  }
  (*env)->ReleaseStringUTFChars(env, response, wire);
  (*env)->DeleteLocalRef(env, response);

cleanup:
  if (j_request_id != NULL) {
    (*env)->DeleteLocalRef(env, j_request_id);
  }
  if (j_did_text != NULL) {
    (*env)->DeleteLocalRef(env, j_did_text);
  }
  if (j_prompt_title != NULL) {
    (*env)->DeleteLocalRef(env, j_prompt_title);
  }
  if (j_prompt_reason != NULL) {
    (*env)->DeleteLocalRef(env, j_prompt_reason);
  }
  if (j_seed_hint != NULL) {
    (*env)->DeleteLocalRef(env, j_seed_hint);
  }
  if (j_label_hint != NULL) {
    (*env)->DeleteLocalRef(env, j_label_hint);
  }
  if (attached) {
    (void)(*s_bio_jvm)->DetachCurrentThread(s_bio_jvm);
  }
  return 0;
}

static void cheng_mobile_android_try_inject_host_api(void) {
  if (s_host_api_injected || !cheng_mobile_has_app_abi_v2()) {
    return;
  }
  ChengHostPlatformAPI api = {0};
  api.log_print = cheng_mobile_android_log_print;
  api.biometric_fingerprint_authorize = cheng_mobile_android_biometric_fingerprint_authorize;
  cheng_app_inject_host_api(&api);
  s_host_api_injected = 1;
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
  cheng_mobile_android_biometric_jni_register(env);
  if (cheng_mobile_started) {
    return;
  }
  if (cheng_mobile_has_app_abi_v2()) {
    cheng_mobile_android_try_inject_host_api();
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
  if (cheng_mobile_has_app_abi_v2() && cheng_mobile_has_app_input_abi() && s_app_id != 0u) {
    cheng_app_on_key(s_app_id, (int)keyCode, down ? 1 : 0, repeat ? 1 : 0);
  } else {
    cheng_mobile_host_emit_key((int)windowId, (int)keyCode, down ? 1 : 0, repeat ? 1 : 0);
  }
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
    if (cheng_mobile_has_app_abi_v2() && cheng_mobile_has_app_input_abi() && s_app_id != 0u) {
      cheng_app_on_text_input(s_app_id, utf);
    } else {
      cheng_mobile_host_emit_text((int)windowId, utf);
    }
    (*env)->ReleaseStringUTFChars(env, text, utf);
  }
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onIme(
    JNIEnv* env,
    jclass cls,
    jint windowId,
    jboolean visible,
    jstring composing,
    jint cursorStart,
    jint cursorEnd
) {
  (void)cls;
  (void)windowId;
  cheng_mobile_host_android_jni_register(env);
  const char* utf = NULL;
  if (composing != NULL) {
    utf = (*env)->GetStringUTFChars(env, composing, 0);
  }
  if (cheng_mobile_has_app_abi_v2() && cheng_mobile_has_app_input_abi() && s_app_id != 0u) {
    cheng_app_on_ime(s_app_id, visible ? 1 : 0, utf != NULL ? utf : "", (int)cursorStart, (int)cursorEnd);
  }
  if (utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, composing, utf);
  }
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onFrame(
    JNIEnv* env,
    jclass cls,
    jfloat deltaSeconds
) {
  (void)cls;
  s_frame_count += 1;
  if (s_frame_count <= 5 || (s_frame_count % 120) == 0) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "jni onFrame count=%d dt=%.4f", s_frame_count, (double)deltaSeconds);
  }
  cheng_mobile_host_android_jni_register(env);
  if (cheng_mobile_has_app_abi_v2() && s_app_id != 0u) {
    float safe_delta = deltaSeconds;
    if (!(safe_delta > 0.0f)) {
      safe_delta = 1.0f / 60.0f;
    }
    cheng_app_tick(s_app_id, safe_delta);
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
    cheng_mobile_android_try_inject_host_api();
    if (s_app_id == 0u) {
      s_app_id = cheng_app_init();
    }
    if (s_app_id != 0u) {
      cheng_app_set_window(s_app_id, (uint64_t)(uintptr_t)window, (int)width, (int)height, (float)s_density_scale);
      if (cheng_mobile_has_app_input_abi()) {
        cheng_app_on_resize(s_app_id, (int)width, (int)height, (float)s_density_scale);
      }
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

JNIEXPORT jint JNICALL Java_com_cheng_mobile_ChengNative_cborBuilderBegin(
    JNIEnv* env,
    jclass cls
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  return (jint)cheng_mobile_cbor_builder_begin();
}

JNIEXPORT jboolean JNICALL Java_com_cheng_mobile_ChengNative_cborBuilderPutText(
    JNIEnv* env,
    jclass cls,
    jint handle,
    jstring key,
    jstring value
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* key_utf = NULL;
  const char* value_utf = NULL;
  if (key != NULL) {
    key_utf = (*env)->GetStringUTFChars(env, key, 0);
  }
  if (value != NULL) {
    value_utf = (*env)->GetStringUTFChars(env, value, 0);
  }
  const int ok = cheng_mobile_cbor_builder_put_text((int)handle, key_utf, value_utf);
  if (key != NULL && key_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, key, key_utf);
  }
  if (value != NULL && value_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, value, value_utf);
  }
  return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_cheng_mobile_ChengNative_cborBuilderFinish(
    JNIEnv* env,
    jclass cls,
    jint handle
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* out = cheng_mobile_cbor_builder_finish((int)handle);
  return (*env)->NewStringUTF(env, out != NULL ? out : "");
}

JNIEXPORT jstring JNICALL Java_com_cheng_mobile_ChengNative_cborGetText(
    JNIEnv* env,
    jclass cls,
    jstring payload,
    jstring key
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* payload_utf = NULL;
  const char* key_utf = NULL;
  if (payload != NULL) {
    payload_utf = (*env)->GetStringUTFChars(env, payload, 0);
  }
  if (key != NULL) {
    key_utf = (*env)->GetStringUTFChars(env, key, 0);
  }
  const char* out = cheng_mobile_cbor_get_text(payload_utf, key_utf);
  if (payload != NULL && payload_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, payload, payload_utf);
  }
  if (key != NULL && key_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, key, key_utf);
  }
  return (*env)->NewStringUTF(env, out != NULL ? out : "");
}

JNIEXPORT jboolean JNICALL Java_com_cheng_mobile_ChengNative_cborHasKey(
    JNIEnv* env,
    jclass cls,
    jstring payload,
    jstring key
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* payload_utf = NULL;
  const char* key_utf = NULL;
  if (payload != NULL) {
    payload_utf = (*env)->GetStringUTFChars(env, payload, 0);
  }
  if (key != NULL) {
    key_utf = (*env)->GetStringUTFChars(env, key, 0);
  }
  const int has_key = cheng_mobile_cbor_has_key(payload_utf, key_utf);
  if (payload != NULL && payload_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, payload, payload_utf);
  }
  if (key != NULL && key_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, key, key_utf);
  }
  return has_key ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_cheng_mobile_ChengNative_cborIsValidMap(
    JNIEnv* env,
    jclass cls,
    jstring payload
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* payload_utf = NULL;
  if (payload != NULL) {
    payload_utf = (*env)->GetStringUTFChars(env, payload, 0);
  }
  const int ok = cheng_mobile_cbor_is_valid_map(payload_utf);
  if (payload != NULL && payload_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, payload, payload_utf);
  }
  return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL Java_com_cheng_mobile_ChengNative_cborMapCount(
    JNIEnv* env,
    jclass cls,
    jstring payload
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* payload_utf = NULL;
  if (payload != NULL) {
    payload_utf = (*env)->GetStringUTFChars(env, payload, 0);
  }
  const int count = cheng_mobile_cbor_map_count(payload_utf);
  if (payload != NULL && payload_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, payload, payload_utf);
  }
  return (jint)count;
}

JNIEXPORT jstring JNICALL Java_com_cheng_mobile_ChengNative_cborMapKeyAt(
    JNIEnv* env,
    jclass cls,
    jstring payload,
    jint index
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* payload_utf = NULL;
  if (payload != NULL) {
    payload_utf = (*env)->GetStringUTFChars(env, payload, 0);
  }
  const char* out = cheng_mobile_cbor_map_key_at(payload_utf, (int)index);
  if (payload != NULL && payload_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, payload, payload_utf);
  }
  return (*env)->NewStringUTF(env, out != NULL ? out : "");
}

JNIEXPORT jstring JNICALL Java_com_cheng_mobile_ChengNative_cborMapValueAt(
    JNIEnv* env,
    jclass cls,
    jstring payload,
    jint index
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  const char* payload_utf = NULL;
  if (payload != NULL) {
    payload_utf = (*env)->GetStringUTFChars(env, payload, 0);
  }
  const char* out = cheng_mobile_cbor_map_value_text_at(payload_utf, (int)index);
  if (payload != NULL && payload_utf != NULL) {
    (*env)->ReleaseStringUTFChars(env, payload, payload_utf);
  }
  return (*env)->NewStringUTF(env, out != NULL ? out : "");
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
    int w = cheng_mobile_host_android_width();
    int h = cheng_mobile_host_android_height();
    cheng_app_set_window(s_app_id, 0u, w, h, (float)s_density_scale);
    if (cheng_mobile_has_app_input_abi()) {
      cheng_app_on_resize(s_app_id, w, h, (float)s_density_scale);
    }
  }
}

JNIEXPORT jlong JNICALL Java_com_cheng_mobile_ChengNative_shmSizeBytes(
    JNIEnv* env,
    jclass cls,
    jint handle
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  return (jlong)cheng_mobile_shm_size_bytes_i64((int32_t)handle);
}

JNIEXPORT void JNICALL Java_com_cheng_mobile_ChengNative_onPauseNative(
    JNIEnv* env,
    jclass cls
) {
  (void)cls;
  cheng_mobile_host_android_jni_register(env);
  if (cheng_mobile_has_app_abi_v2() && s_app_id != 0u) {
    cheng_app_pause(s_app_id);
    if (cheng_mobile_has_app_input_abi()) {
      cheng_app_on_focus(s_app_id, 0);
    }
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
    if (cheng_mobile_has_app_input_abi()) {
      cheng_app_on_focus(s_app_id, 1);
      cheng_app_on_resize(
          s_app_id,
          cheng_mobile_host_android_width(),
          cheng_mobile_host_android_height(),
          (float)s_density_scale);
    }
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
