#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system_helpers.h"

static char *driver_c_machine_target_dup_cstring(const char *text) {
  size_t n = 0;
  char *out = NULL;
  if (text == NULL) {
    text = "";
  }
  n = strlen(text);
  out = (char *)malloc(n + 1u);
  if (out == NULL) return NULL;
  memcpy(out, text, n);
  out[n] = '\0';
  return out;
}

static ChengStrBridge driver_c_machine_target_owned_bridge_from_cstring(char *text) {
  ChengStrBridge out = {0};
  out.ptr = text;
  out.len = text != NULL ? (int32_t)strlen(text) : 0;
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_OWNED;
  return out;
}

static const char *driver_c_machine_target_text(ChengStrBridge target) {
  return target.ptr != NULL ? target.ptr : "";
}

static void driver_c_require_machine_target_bridge(ChengStrBridge target) {
  const char *raw = driver_c_machine_target_text(target);
  if (driver_c_str_eq_raw_bridge(target, "arm64-apple-darwin", 19)) {
    return;
  }
  fprintf(stderr, "v2 machine target: unsupported target %s\n", raw);
  abort();
}

static ChengStrBridge driver_c_machine_target_constant_bridge(const char *text) {
  return driver_c_machine_target_owned_bridge_from_cstring(
      driver_c_machine_target_dup_cstring(text));
}

ChengStrBridge driver_c_machine_target_architecture_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("aarch64");
}

ChengStrBridge driver_c_machine_target_obj_format_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("macho");
}

ChengStrBridge driver_c_machine_target_symbol_prefix_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("_");
}

ChengStrBridge driver_c_machine_target_text_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("__TEXT,__text");
}

ChengStrBridge driver_c_machine_target_cstring_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("__TEXT,__cstring");
}

ChengStrBridge driver_c_machine_target_symtab_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("__LINKEDIT,symtab");
}

ChengStrBridge driver_c_machine_target_strtab_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("__LINKEDIT,strtab");
}

ChengStrBridge driver_c_machine_target_reloc_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("__LINKEDIT,reloc");
}

ChengStrBridge driver_c_machine_target_call_relocation_kind_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("ARM64_RELOC_BRANCH26");
}

ChengStrBridge driver_c_machine_target_metadata_page_relocation_kind_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("ARM64_RELOC_PAGE21");
}

ChengStrBridge driver_c_machine_target_metadata_pageoff_relocation_kind_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("ARM64_RELOC_PAGEOFF12");
}

int32_t driver_c_machine_target_pointer_width_bits_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return 64;
}

int32_t driver_c_machine_target_stack_align_bytes_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return 16;
}

int32_t driver_c_machine_target_text_align_pow2_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return 2;
}

int32_t driver_c_machine_target_cstring_align_pow2_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return 0;
}

int32_t driver_c_machine_target_darwin_platform_id_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return 1;
}

ChengStrBridge driver_c_machine_target_darwin_platform_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("macos");
}

int32_t driver_c_machine_target_darwin_minos_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return 0x000d0000;
}

ChengStrBridge driver_c_machine_target_darwin_minos_text_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("13.0");
}

ChengStrBridge driver_c_machine_target_darwin_arch_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("arm64");
}

ChengStrBridge driver_c_machine_target_darwin_sdk_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_constant_bridge("macosx");
}
