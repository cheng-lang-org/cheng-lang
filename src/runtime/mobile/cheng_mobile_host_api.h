#ifndef CHENG_MOBILE_HOST_API_H
#define CHENG_MOBILE_HOST_API_H

#include <stdint.h>

#include "cheng_mobile_bridge.h"

#if defined(__GNUC__) || defined(__clang__)
#define CHENG_MOBILE_DEPRECATED __attribute__((deprecated))
#else
#define CHENG_MOBILE_DEPRECATED
#endif

void cheng_mobile_host_emit_frame_tick(void);
void cheng_mobile_host_emit_log(const char* message);
void cheng_mobile_host_emit_pointer(int windowId, int kind, double x, double y, double dx, double dy, int button, int pointerId, int64_t timeMs);
void cheng_mobile_host_emit_key(int windowId, int keyCode, int down, int repeat);
void cheng_mobile_host_emit_text(int windowId, const char* text);

/* Async RPC message bus (Bus v2). */
int cheng_mobile_bus_send(const ChengMobileBusMessage* msg, const void* payload, uint32_t payloadSize);
int cheng_mobile_bus_recv(ChengMobileBusMessage* outMsg, void* payloadBuf, uint32_t payloadCap, uint32_t* outPayloadSize);

/* Native host side queue accessors. */
int cheng_mobile_host_bus_poll_request(ChengMobileBusMessage* outMsg, void* payloadBuf, uint32_t payloadCap, uint32_t* outPayloadSize);
int cheng_mobile_host_bus_push_response(const ChengMobileBusMessage* msg, const void* payload, uint32_t payloadSize);

/* Envelope helpers for hosts and Cheng FFI. */
int cheng_mobile_bus_send_envelope(int serviceKind, int methodKind, int flags, const char* requestId, const char* payloadCbor);
const char* cheng_mobile_bus_recv_envelope(void);
const char* cheng_mobile_host_bus_poll_request_envelope(void);
int cheng_mobile_host_bus_push_response_envelope(const char* responseEnvelope);
int cheng_mobile_cbor_builder_begin(void);
int cheng_mobile_cbor_builder_put_text(int handle, const char* key, const char* value);
int cheng_mobile_cbor_builder_put_bool(int handle, const char* key, int value);
int cheng_mobile_cbor_builder_put_i64(int handle, const char* key, int64_t value);
int cheng_mobile_cbor_builder_put_f64(int handle, const char* key, double value);
int cheng_mobile_cbor_builder_put_null(int handle, const char* key);
const char* cheng_mobile_cbor_builder_finish(int handle);
const char* cheng_mobile_cbor_get_text(const char* payloadCbor, const char* key);
int cheng_mobile_cbor_has_key(const char* payloadCbor, const char* key);
int cheng_mobile_cbor_is_valid_map(const char* payloadCbor);
int cheng_mobile_cbor_map_count(const char* payloadCbor);
const char* cheng_mobile_cbor_map_key_at(const char* payloadCbor, int index);
const char* cheng_mobile_cbor_map_value_text_at(const char* payloadCbor, int index);

/* Shared memory pool APIs. */
int cheng_mobile_shm_alloc(uint64_t sizeBytes, uint32_t usageFlags, ChengMobileSharedBufferDesc* outDesc);
int cheng_mobile_shm_free(uint32_t handle);
void* cheng_mobile_shm_map(uint32_t handle, uint64_t offsetBytes, uint64_t lengthBytes);
int cheng_mobile_shm_unmap(uint32_t handle);
uint64_t cheng_mobile_shm_size_bytes(uint32_t handle);

/* Cheng-friendly wrappers. */
int cheng_mobile_shm_alloc_handle(int64_t sizeBytes, int32_t usageFlags);
int64_t cheng_mobile_shm_size_bytes_i64(int32_t handle);
int64_t cheng_mobile_capability_query_i64(void);

/* Capability probe. */
uint64_t cheng_mobile_capability_query(void);

/* Runtime state for lightweight health checks and fallback decisions. */
void cheng_mobile_host_runtime_mark_started(void);
void cheng_mobile_host_runtime_mark_stopped(const char* reason);
const char* cheng_mobile_host_runtime_state_json(void);
void cheng_mobile_host_runtime_set_launch_args(const char* argsKv, const char* argsJson);
const char* cheng_mobile_host_runtime_launch_args_kv(void);
const char* cheng_mobile_host_runtime_launch_args_json(void);

#endif
