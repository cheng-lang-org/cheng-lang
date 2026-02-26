#include <stdint.h>
#include <stdlib.h>

typedef struct {
  void *buffer;
  int32_t len;
  int32_t cap;
} cheng_seq_u8;

extern void *uirBuildModuleFromFile(const char *path);
extern void *uirBuildModuleFromFileStage1(const char *path);
extern cheng_seq_u8 uirEmitObjFromModule(void *module, int32_t optLevel,
                                         const char *target,
                                         const char *objWriter,
                                         int32_t validateModule,
                                         int32_t uirSimdEnabled,
                                         int32_t uirSimdMaxWidth,
                                         const char *uirSimdPolicy);

__attribute__((weak)) void *uirBuildModuleFromFileOrPanic(const char *path) {
  void *module = uirBuildModuleFromFile(path);
  if (module == NULL) {
    abort();
  }
  return module;
}

__attribute__((weak)) void *uirBuildModuleFromFileStage1OrPanic(
    const char *path) {
  void *module = uirBuildModuleFromFileStage1(path);
  if (module == NULL) {
    abort();
  }
  return module;
}

__attribute__((weak)) cheng_seq_u8 uirEmitObjFromModuleOrPanic(
    void *module, int32_t optLevel, const char *target, const char *objWriter,
    int32_t validateModule, int32_t uirSimdEnabled, int32_t uirSimdMaxWidth,
    const char *uirSimdPolicy) {
  return uirEmitObjFromModule(module, optLevel, target, objWriter,
                              validateModule, uirSimdEnabled, uirSimdMaxWidth,
                              uirSimdPolicy);
}

void backend_driver_stage0_setindex_compat(char *s, int32_t idx, int32_t value)
    __asm__("_[]=");
__attribute__((weak)) void
backend_driver_stage0_setindex_compat(char *s, int32_t idx, int32_t value) {
  if (s == NULL || idx < 0) {
    return;
  }
  s[idx] = (char)value;
}
