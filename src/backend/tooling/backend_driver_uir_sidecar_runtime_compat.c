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
