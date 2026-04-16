#ifndef MOBILE_BRIDGE_H
#define MOBILE_BRIDGE_H

#include <stdint.h>
#if !defined(__cplusplus)
#include <stdatomic.h>
#endif
#include "cheng_mobile_exports.h"

#if defined(__cplusplus)
typedef uint32_t ChengMobileAtomicU32;
#else
typedef _Atomic uint32_t ChengMobileAtomicU32;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CHENG_MOBILE_ABI_VERSION 4

enum ChengMobilePlatform {
  MOBILE_ANDROID = 0,
  MOBILE_IOS = 1,
  MOBILE_HARMONY = 2
};

enum ChengMobileEventKind {
  MRE_RUNTIME_STARTED = 0,
  MRE_WINDOW_OPENED = 1,
  MRE_FRAME_TICK = 2,
  MRE_RUNTIME_STOPPED = 3,
  MRE_LOG = 4,
  MRE_POINTER_DOWN = 5,
  MRE_POINTER_UP = 6,
  MRE_POINTER_MOVE = 7,
  MRE_POINTER_SCROLL = 8,
  MRE_KEY_DOWN = 9,
  MRE_KEY_UP = 10,
  MRE_TEXT_INPUT = 11
};

enum ChengMobileBusMessageType {
  CMB_MSG_REQUEST = 0,
  CMB_MSG_RESPONSE = 1,
  CMB_MSG_EVENT = 2,
  CMB_MSG_STREAM_CHUNK = 3
};

enum ChengMobileServiceKind {
  CMSK_NONE = 0,
  CMSK_CAMERA = 1,
  CMSK_LOCATION = 2,
  CMSK_NFC = 3,
  CMSK_SECURE_STORE = 4,
  CMSK_NETWORK_STATE = 5,
  CMSK_BIOMETRIC = 6,
  CMSK_AUDIO = 8,
  CMSK_SENSORS = 9,
  CMSK_BLE = 10,
  CMSK_HAPTICS = 11,
  CMSK_FLASHLIGHT = 12,
  CMSK_PLUGIN = 1000
};

enum ChengMobileErrorCode {
  CME_OK = 0,
  CME_UNSUPPORTED = -1,
  CME_INVALID_ARG = -2,
  CME_QUEUE_FULL = -3,
  CME_QUEUE_EMPTY = -4,
  CME_PAYLOAD_TOO_LARGE = -5,
  CME_NOT_FOUND = -6,
  CME_PERMISSION_DENIED = -7,
  CME_INTERNAL = -8
};

typedef uint64_t ChengMobileCapabilityFlags;

enum ChengMobileCapabilityBits {
  CMCAP_BUS_V2 = 1ull << 0,
  CMCAP_SHM = 1ull << 1,
  CMCAP_CAMERA = 1ull << 2,
  CMCAP_LOCATION = 1ull << 3,
  CMCAP_NFC = 1ull << 4,
  CMCAP_SECURE_STORE = 1ull << 5,
  CMCAP_NETWORK_STATE = 1ull << 6,
  CMCAP_PLUGIN_SYSTEM = 1ull << 7,
  CMCAP_PLATFORM_ANDROID = 1ull << 8,
  CMCAP_PLATFORM_IOS = 1ull << 9,
  CMCAP_PLATFORM_HARMONY = 1ull << 10,
  CMCAP_GPU_TEXTURE_HANDLE = 1ull << 11,
  CMCAP_RING_BUFFER_STREAM = 1ull << 12,
  CMCAP_BIOMETRIC_STRONG = 1ull << 13,
  CMCAP_NFC_APDU = 1ull << 14,
  CMCAP_SECURE_SIGN_USER_PRESENCE = 1ull << 15,
  CMCAP_AUDIO_DUPLEX = 1ull << 17,
  CMCAP_SENSORS = 1ull << 18,
  CMCAP_BLE_CENTRAL = 1ull << 19,
  CMCAP_HAPTICS = 1ull << 20,
  CMCAP_FLASHLIGHT = 1ull << 21
};

typedef struct ChengMobileSharedBufferDesc {
  uint32_t handle;
  uint32_t usageFlags;
  uint64_t sizeBytes;
  uint32_t width;
  uint32_t height;
  uint32_t strideBytes;
  uint32_t pixelFormat;
} ChengMobileSharedBufferDesc;

typedef struct ChengMobileBusMessage {
  uint32_t abiVersion;
  uint32_t messageType;
  uint32_t serviceKind;
  uint32_t methodKind;
  uint32_t flags;
  uint32_t requestId;
  int32_t errorCode;
  uint32_t reserved0;
  uint64_t timeUnixMs;
  ChengMobileSharedBufferDesc shared;
} ChengMobileBusMessage;

/* Layout must match MobileHostConfig in cheng/runtime/mobile_app.cheng. */
typedef struct ChengMobileConfig {
  int platform;
  const char* resourceRoot;
  const char* title;
  int width;
  int height;
  int highDpi;
} ChengMobileConfig;

typedef struct ChengMobileEvent {
  int kind;
  int windowId;
  double pointerX;
  double pointerY;
  double pointerDeltaX;
  double pointerDeltaY;
  int pointerButton;
  int pointerId;
  int64_t eventTimeMs;
  int keyCode;
  int keyRepeat;
  const char* text;
  const char* message;
} ChengMobileEvent;

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
  ChengMobileAtomicU32* write_idx;
  ChengMobileAtomicU32* read_idx;
} ChengInputRingBufferDesc;
#endif

#ifndef CHENG_MOBILE_EXPORTS_FUNCS_DECLARED
#define CHENG_MOBILE_EXPORTS_FUNCS_DECLARED
uint64_t cheng_app_init(void);
void cheng_app_set_window(uint64_t app_id, uint64_t window_id, int physical_w, int physical_h, float scale);
void cheng_app_tick(uint64_t app_id, float delta_time);
void cheng_app_on_touch(uint64_t app_id, int action, int pointer_id, float x, float y);
void cheng_app_pause(uint64_t app_id);
void cheng_app_resume(uint64_t app_id);
void cheng_app_inject_host_api(const ChengHostPlatformAPI* api);
const ChengInputRingBufferDesc* cheng_app_input_ring_desc(uint64_t app_id);
#endif

int cheng_mobile_host_init(const ChengMobileConfig* cfg);
int cheng_mobile_host_open_window(const ChengMobileConfig* cfg);
int cheng_mobile_host_poll_event(ChengMobileEvent* outEvent);
int cheng_mobile_host_poll_event_cached(void);
int cheng_mobile_host_cached_event_kind(void);
int cheng_mobile_host_cached_event_window_id(void);
double cheng_mobile_host_cached_event_pointer_x(void);
double cheng_mobile_host_cached_event_pointer_y(void);
double cheng_mobile_host_cached_event_pointer_delta_x(void);
double cheng_mobile_host_cached_event_pointer_delta_y(void);
int cheng_mobile_host_cached_event_pointer_x_i32(void);
int cheng_mobile_host_cached_event_pointer_y_i32(void);
int cheng_mobile_host_cached_event_pointer_delta_x_i32(void);
int cheng_mobile_host_cached_event_pointer_delta_y_i32(void);
int cheng_mobile_host_cached_event_pointer_button(void);
int cheng_mobile_host_cached_event_pointer_id(void);
int64_t cheng_mobile_host_cached_event_time_ms(void);
int cheng_mobile_host_cached_event_key_code(void);
int cheng_mobile_host_cached_event_key_repeat(void);
const char* cheng_mobile_host_cached_event_text(void);
const char* cheng_mobile_host_cached_event_message(void);
void cheng_mobile_host_present(const void* pixels, int width, int height, int strideBytes);
void cheng_mobile_host_shutdown(const char* reason);
const char* cheng_mobile_host_default_resource_root(void);

#ifdef __cplusplus
}
#endif

#endif
