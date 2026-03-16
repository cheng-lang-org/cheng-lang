#ifndef CHENG_MOBILE_EXPORTS_H
#define CHENG_MOBILE_EXPORTS_H

#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CHENG_MOBILE_ABI_VERSION
#define CHENG_MOBILE_ABI_VERSION 2
#endif

#ifndef CHENG_MOBILE_CAP_RENDER_SEMANTIC
#define CHENG_MOBILE_CAP_RENDER_SEMANTIC (1ull << 0)
#endif
#ifndef CHENG_MOBILE_CAP_SIDE_EFFECT_BRIDGE
#define CHENG_MOBILE_CAP_SIDE_EFFECT_BRIDGE (1ull << 1)
#endif
#ifndef CHENG_MOBILE_CAP_FRAME_CAPTURE
#define CHENG_MOBILE_CAP_FRAME_CAPTURE (1ull << 2)
#endif
#ifndef CHENG_MOBILE_CAP_IME_INPUT
#define CHENG_MOBILE_CAP_IME_INPUT (1ull << 3)
#endif

#ifndef CHENG_MOBILE_EXPORTS_SHARED_TYPES
#define CHENG_MOBILE_EXPORTS_SHARED_TYPES
typedef struct ChengHostPlatformAPI {
  uint64_t (*read_asset)(int32_t asset_id, int32_t* out_len);
  void (*log_print)(int32_t level, const char* msg);
  int32_t (*request_permission)(int32_t permission_id);
  int32_t (*clipboard_write_text)(const char* text);
  int32_t (*clipboard_read_text)(char* buf, int32_t cap);
  int32_t (*geo_get_current)(double* out_lat, double* out_lon);
  int64_t (*time_now_ms)(void);
} ChengHostPlatformAPI;

typedef struct ChengInputRingBufferDesc {
  uint8_t* base;
  uint32_t capacity;
  _Atomic uint32_t* write_idx;
  _Atomic uint32_t* read_idx;
} ChengInputRingBufferDesc;
#endif

#ifndef CHENG_MOBILE_EXPORTS_FUNCS_DECLARED
#define CHENG_MOBILE_EXPORTS_FUNCS_DECLARED
uint64_t cheng_app_init(void);
void cheng_app_set_window(uint64_t app_id, uint64_t window_id, int physical_w, int physical_h, float scale);
void cheng_app_tick(uint64_t app_id, float delta_time);
void cheng_app_on_touch(uint64_t app_id, int action, int pointer_id, float x, float y);
void cheng_app_on_key(uint64_t app_id, int key_code, int down, int repeat);
void cheng_app_on_text_input(uint64_t app_id, const char* text_utf8);
void cheng_app_on_ime(uint64_t app_id, int ime_visible, const char* composing_utf8, int cursor_start, int cursor_end);
void cheng_app_on_resize(uint64_t app_id, int physical_w, int physical_h, float scale);
void cheng_app_on_focus(uint64_t app_id, int focused);
uint64_t cheng_app_capabilities(void);
void cheng_app_pause(uint64_t app_id);
void cheng_app_resume(uint64_t app_id);
void cheng_app_inject_host_api(const ChengHostPlatformAPI* api);
const ChengInputRingBufferDesc* cheng_app_input_ring_desc(uint64_t app_id);
int32_t cheng_app_pull_side_effect(uint64_t app_id, char* out_buf, int32_t out_cap);
int32_t cheng_app_push_side_effect_result(uint64_t app_id, const char* envelope_utf8);
uint64_t cheng_app_capture_frame_hash(uint64_t app_id);
int32_t cheng_app_capture_frame_rgba(uint64_t app_id, uint8_t* out_rgba, int32_t out_bytes);
#endif

#ifdef __cplusplus
}
#endif

#endif
