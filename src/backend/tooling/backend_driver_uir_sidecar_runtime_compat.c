#include <stdint.h>

int32_t cheng_crash_trace_enabled(void) { return 0; }

void cheng_crash_trace_set_phase(const char *phase) { (void)phase; }

void cheng_crash_trace_mark(const char *tag, int32_t a, int32_t b, int32_t c, int32_t d) {
  (void)tag;
  (void)a;
  (void)b;
  (void)c;
  (void)d;
}

void cheng_dump_backtrace_if_enabled(void) {}

void stage1_memRelease(void *p) { (void)p; }

void trackLexString(void *p) { (void)p; }

extern void *backend_driver_c_sidecar_buildActiveModulePtrs(void *input_raw,
                                                            void *target_raw);
extern int32_t backend_driver_c_sidecar_emit_obj_from_module_default_impl(
    void *module_raw, const char *target_raw, const char *out_raw);

/*
 * The Cheng sidecar wrapper should export the canonical driver_* entrypoints,
 * but some stage0/object paths currently only materialize `_main` in the
 * wrapper object. Keep weak C aliases here so the linked bundle still exposes
 * the canonical symbols and forwards into the emergency-C helper object.
 * When the wrapper object does export strong driver_* symbols, they override
 * these weak aliases at link/load time.
 */
__attribute__((visibility("default"), weak))
void *driver_buildActiveModulePtrs(void *input_raw, void *target_raw) {
  return backend_driver_c_sidecar_buildActiveModulePtrs(input_raw, target_raw);
}

__attribute__((visibility("default"), weak))
int32_t driver_emit_obj_from_module_default_impl(void *module_raw,
                                                 const char *target_raw,
                                                 const char *out_raw) {
  return backend_driver_c_sidecar_emit_obj_from_module_default_impl(
      module_raw, target_raw, out_raw);
}
