#ifndef MOBILE_HOST_ANDROID_H
#define MOBILE_HOST_ANDROID_H

#ifdef __ANDROID__

#include <jni.h>
#include <android/native_activity.h>
#include <android/native_window.h>

struct android_app;

void cheng_mobile_host_android_set_surface(ANativeWindow* window, int width, int height);
void cheng_mobile_host_android_attach_app(struct android_app* app);
void cheng_mobile_host_android_pump_events(void);
void cheng_mobile_host_android_set_asset_root(const char* path);
void cheng_mobile_host_android_set_ime_visible(int visible);
void cheng_mobile_host_android_set_native_activity(ANativeActivity* activity);
void cheng_mobile_host_android_jni_register(JNIEnv* env);
int cheng_mobile_host_android_width(void);
int cheng_mobile_host_android_height(void);
int cheng_mobile_host_android_surface_generation(void);
void cheng_mobile_host_android_set_density_scale(double scale);
double cheng_mobile_host_android_density_scale(void);
int cheng_mobile_host_dp_to_px(double dp, double scale);

#endif

#endif
