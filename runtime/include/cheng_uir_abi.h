#ifndef CHENG_UIR_ABI_H
#define CHENG_UIR_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * v1 bootstrap ABI:
 * - input/options are UTF-8 manifests encoded as `key=value\n`
 * - returned pointer owns a UTF-8 report buffer
 * - free the returned pointer with `cheng_uir_free_result_v1`
 */
void *cheng_uir_validate_v1(const char *input_utf8, int32_t input_len, int32_t *out_len);
void *cheng_uir_compile_v1(const char *input_utf8,
                           int32_t input_len,
                           const char *options_utf8,
                           int32_t options_len,
                           int32_t *out_len);

/*
 * v2 bootstrap ABI:
 * - manifest/options stay as UTF-8 `key=value\n`
 * - semantic tables and symbol list are passed as separate raw UTF-8 buffers
 * - avoids manifest inlining/escaping for large Rust semantic payloads
 */
void *cheng_uir_validate_v2(const char *input_utf8,
                            int32_t input_len,
                            const char *semantic_utf8,
                            int32_t semantic_len,
                            const char *symbols_utf8,
                            int32_t symbols_len,
                            int32_t *out_len);
void *cheng_uir_compile_v2(const char *input_utf8,
                           int32_t input_len,
                           const char *semantic_utf8,
                           int32_t semantic_len,
                           const char *symbols_utf8,
                           int32_t symbols_len,
                           const char *options_utf8,
                           int32_t options_len,
                           int32_t *out_len);
void cheng_uir_free_result_v1(void *result_ptr);

#ifdef __cplusplus
}
#endif

#endif
