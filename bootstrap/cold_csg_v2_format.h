/* cold_csg_v2_format.h -- Binary CSG v2 facts format schema.
 *
 * Shared between writer (cold_emit_csg_v2_facts) and reader (cold_load_csg_v2_facts).
 * All integers are little-endian. Strings are u32 byte_len + utf8 bytes.
 * Arrays are u32 count + records.
 */

#ifndef COLD_CSG_V2_FORMAT_H
#define COLD_CSG_V2_FORMAT_H

#include <stdint.h>

/* ---- Header (64 bytes) ---- */
#define CSG_V2_MAGIC           "CHENGCSG"
#define CSG_V2_MAGIC_LEN       8
#define CSG_V2_VERSION         2
#define CSG_V2_HEADER_SIZE     64

/* Header field offsets */
#define CSG_V2_OFF_MAGIC        0
#define CSG_V2_OFF_VERSION      8
#define CSG_V2_OFF_TARGET       12
#define CSG_V2_TARGET_LEN       32
#define CSG_V2_OFF_ABI          44
#define CSG_V2_OFF_PTR_WIDTH    48
#define CSG_V2_OFF_ENDIAN       49
#define CSG_V2_OFF_RESERVED     50
#define CSG_V2_RESERVED_LEN     14

/* ABI values */
#define CSG_V2_ABI_SYSTEM       0
#define CSG_V2_ABI_COLD_NO_RUNTIME 1

/* Pointer width */
#define CSG_V2_PTR_WIDTH_64     8

/* Endianness */
#define CSG_V2_ENDIAN_LE        0

/* ---- Section Index (after header) ---- */
/* section_count(4) + N * (kind(4) + offset(8) + size(4)) */
#define CSG_V2_SECTION_ENTRY_SIZE  16  /* kind(4) + offset(8) + size(4) */
#define CSG_V2_SECTION_INDEX_OFFSET 64  /* section index starts right after header */

/* Reader error codes */
enum {
    CSG_V2_ERR_BAD_MAGIC               = -1,
    CSG_V2_ERR_BAD_VERSION             = -2,
    CSG_V2_ERR_BAD_TARGET              = -3,
    CSG_V2_ERR_TRUNCATED               = -4,
    CSG_V2_ERR_TOO_MANY_SECTIONS       = -5,
    CSG_V2_ERR_MISSING_FUNCTION_SECTION = -6,
    CSG_V2_ERR_SECTION_OVERLAP         = -7,
    CSG_V2_ERR_BUDGET_EXCEEDED         = -8,
};

/* Section kinds */
enum {
    CSG_V2_SEC_TYPE_BLOCK      = 0,
    CSG_V2_SEC_FUNCTION        = 1,
    CSG_V2_SEC_PROVIDER_ROOT   = 2,
    CSG_V2_SEC_EXTERNAL_SYMBOL = 3,
    CSG_V2_SEC_DATA_CONSTANT   = 4,
    CSG_V2_SEC_STRING_CONSTANT = 5,
    CSG_V2_SEC_RELOCATION      = 6,
};

/* ---- Function Section (kind=1) ---- */
/* Per-function fields:
 *   name:         u32 len + utf8
 *   param_count:  u32
 *   params:       param_count * (slot_kind(u32)+slot_size(u32)+param_slot(u32))
 *   return_kind:  u32
 *   frame_size:   u32
 *   op_count:     u32
 *   ops:          op_count * (kind(u32)+dst(u32)+a(u32)+b(u32)+c(u32)) = 20 bytes/op
 *   slot_count:   u32
 *   slots:        slot_count * (kind(u32)+offset(u32)+size(u32)) = 12 bytes/slot
 *   block_count:  u32
 *   blocks:       block_count * (op_start(u32)+op_count(u32)+term(u32)) = 12 bytes/block
 *   term_count:   u32
 *   terms:        term_count * (kind+value+case_start+case_count+true_blk+false_blk) = 24 bytes/term
 *   switch_count: u32
 *   switches:     switch_count * (tag(u32)+block(u32)+term(u32)) = 12 bytes/case
 *   call_count:   u32
 *   calls:        call_count * (target_fn+arg_start+arg_count+result_slot) = 16 bytes/call
 *   call_arg_cnt: u32
 *   call_args:    call_arg_cnt * (slot(u32)+offset(u32)) = 8 bytes/arg
 *   str_lit_cnt:  u32
 *   str_lits:     str_lit_cnt * u32 (string_id references)
 */
#define CSG_V2_OP_SIZE           20
#define CSG_V2_PARAM_SIZE        12
#define CSG_V2_SLOT_SIZE         12
#define CSG_V2_BLOCK_SIZE        12
#define CSG_V2_TERM_SIZE         24   /* 6 fields per term */
#define CSG_V2_SWITCH_CASE_SIZE  12
#define CSG_V2_CALL_SIZE         16
#define CSG_V2_CALL_ARG_SIZE     8

/* ---- String Section (kind=5) ---- */
/*   str_count:   u32
 *   for each:    id(u32) + len(u32) + utf8_bytes
 */

/* ---- Trailer (32 bytes) ---- */
#define CSG_V2_TRAILER_SIZE      32
#define CSG_V2_HASH_SIZE         28   /* zero-filled for Phase 0 */

/* ---- Maximum limits ---- */
#define CSG_V2_MAX_SECTIONS      10
#define CSG_V2_MAX_FUNCTIONS     65536

/* ---- Size budgets ---- */
#define CSG_V2_BUDGET_MAX_FACTS_BYTES    (2 * 1024 * 1024)  /* 2MB hard limit */
#define CSG_V2_BUDGET_MAX_FN_COUNT       65536
#define CSG_V2_BUDGET_MAX_OP_COUNT       1000000
#define CSG_V2_BUDGET_MAX_SLOT_COUNT     1000000

static const char *csg_v2_error_string(int err) {
    switch (err) {
    case CSG_V2_ERR_BAD_MAGIC:               return "bad magic";
    case CSG_V2_ERR_BAD_VERSION:             return "bad version";
    case CSG_V2_ERR_BAD_TARGET:              return "bad target";
    case CSG_V2_ERR_TRUNCATED:               return "truncated data";
    case CSG_V2_ERR_TOO_MANY_SECTIONS:       return "too many sections";
    case CSG_V2_ERR_MISSING_FUNCTION_SECTION: return "missing function section";
    case CSG_V2_ERR_SECTION_OVERLAP:         return "section overlap";
    case CSG_V2_ERR_BUDGET_EXCEEDED:         return "budget exceeded";
    default:                                 return "unknown";
    }
}

#endif /* COLD_CSG_V2_FORMAT_H */
