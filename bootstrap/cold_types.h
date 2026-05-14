/* cold_types.h -- shared type definitions for cold bootstrap compiler
 *
 * Shared by cheng_cold.c (backend) and cold_parser.c (frontend/parser).
 *
 * Build: #include "cold_types.h" (after standard headers)
 */
#ifndef COLD_TYPES_H
#define COLD_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <sys/mman.h>
#include <unistd.h>
#include <setjmp.h>

/* ================================================================
 * Span — defined early because extern globals use it
 * ================================================================ */
typedef struct Span {
    const uint8_t *ptr;
    int32_t len;
} Span;

#define COLD_MAX_VARIANT_FIELDS 8
#define COLD_MAX_OBJECT_FIELDS 64

/* ================================================================
 * Extern global state (defined in cheng_cold.c)
 * ================================================================ */
extern bool cold_diag_dump_per_fn;
extern bool cold_diag_dump_slots;
extern char ColdDieError[512];
extern char ColdDieReportPath[PATH_MAX];
extern char **ColdArgv0;
extern jmp_buf ColdErrorJumpBuf;
extern bool ColdErrorRecoveryEnabled;
extern jmp_buf ColdImportSegvJumpBuf;
extern bool ColdImportSegvActive;
extern int ColdImportSegvSaw;
extern bool ColdImportBodyCompilationActive;
extern Span cold_cached_minimal_macho;

/* ================================================================
 * Forward declarations
 * ================================================================ */
struct ArenaPage;
struct Arena;
struct ColdImportSource;

/* ================================================================
 * Arena
 * ================================================================ */
#define ARENA_PAGE 65536

typedef struct ArenaPage {
    struct ArenaPage *next;
    uint8_t *base;
    uint8_t *ptr;
    uint8_t *end;
} ArenaPage;

typedef struct Arena {
    ArenaPage *head;
    ArenaPage *current;
    size_t used;
    /* Phase tracking for 30-80ms architecture compliance */
    int32_t phase_count;
    size_t phase_start_used[8];
    uint64_t phase_start_us[8];
    const char *phase_name[8];
    int32_t phase_page_count[8];   /* pages at phase start */
} Arena;

/* ================================================================
 * Contract types
 * ================================================================ */
#define COLD_CONTRACT_MAX_FIELDS 128

typedef struct ContractField {
    Span key;
    Span value;
} ContractField;

typedef struct ColdContract {
    Span source;
    const char *source_path;
    ContractField fields[COLD_CONTRACT_MAX_FIELDS];
    int32_t count;
} ColdContract;

#define COLD_ENTRY_CHAIN_CAP 8
#define COLD_COMMAND_CASE_CAP 16
#define COLD_NAME_CAP 128
#define COLD_ENTRY_ERROR_CAP 256

typedef struct ColdEntryChainStep {
    char function[COLD_NAME_CAP];
    char kind[64];
    char target[COLD_NAME_CAP];
    char aux[192];
} ColdEntryChainStep;

typedef struct ColdDispatchCase {
    int32_t code;
    char target[COLD_NAME_CAP];
} ColdDispatchCase;

typedef struct ColdCommandCase {
    char text[64];
    int32_t code;
} ColdCommandCase;

typedef struct ColdManifestStats {
    int32_t source_count;
    int32_t missing_count;
    int32_t import_count;
    int32_t function_count;
    int32_t type_block_count;
    int32_t const_block_count;
    int32_t importc_count;
    int32_t declaration_count;
    int32_t function_symbol_count;
    int32_t function_symbol_error_count;
    int32_t entry_function_found;
    int32_t entry_function_line;
    int32_t entry_function_source_start;
    int32_t entry_function_source_len;
    int32_t entry_function_params_start;
    int32_t entry_function_params_len;
    int32_t entry_function_return_start;
    int32_t entry_function_return_len;
    int32_t entry_function_body_start;
    int32_t entry_function_body_len;
    int32_t entry_function_has_body;
    int32_t entry_semantics_ok;
    int32_t entry_chain_step_count;
    int32_t entry_dispatch_case_count;
    int32_t entry_dispatch_default;
    int32_t entry_command_case_count;
    int32_t entry_command_default;
    uint64_t total_bytes;
    uint64_t total_lines;
    uint64_t tree_hash;
    uint64_t declaration_hash;
    char first_missing[PATH_MAX];
    char entry_source[PATH_MAX];
    char entry_function[128];
    char entry_semantics_error[COLD_ENTRY_ERROR_CAP];
    ColdEntryChainStep entry_chain[COLD_ENTRY_CHAIN_CAP];
    ColdDispatchCase entry_dispatch_cases[COLD_COMMAND_CASE_CAP];
    ColdCommandCase entry_command_cases[COLD_COMMAND_CASE_CAP];
} ColdManifestStats;

typedef struct ColdSourceScanStats {
    int32_t import_count;
    int32_t function_count;
    int32_t type_block_count;
    int32_t const_block_count;
    int32_t importc_count;
    int32_t declaration_count;
    int32_t function_symbol_count;
    int32_t function_symbol_error_count;
    int32_t entry_function_found;
    int32_t entry_function_line;
    int32_t entry_function_source_start;
    int32_t entry_function_source_len;
    int32_t entry_function_params_start;
    int32_t entry_function_params_len;
    int32_t entry_function_return_start;
    int32_t entry_function_return_len;
    int32_t entry_function_body_start;
    int32_t entry_function_body_len;
    int32_t entry_function_has_body;
    uint64_t line_count;
    uint64_t byte_count;
    uint64_t declaration_hash;
} ColdSourceScanStats;

typedef struct ColdFunctionSymbol {
    Span name;
    Span params;
    Span return_type;
    Span body;
    Span source_span;
    int32_t line;
    int32_t has_body;
    Span generic_names[4];
    int32_t generic_count;
} ColdFunctionSymbol;

/* ================================================================
 * SoA BodyIR
 * ================================================================ */
enum {
    BODY_OP_I32_CONST = 1,
    BODY_OP_LOAD_I32 = 2,
    BODY_OP_MAKE_VARIANT = 3,
    BODY_OP_TAG_LOAD = 4,
    BODY_OP_PAYLOAD_LOAD = 5,
    BODY_OP_CALL_I32 = 6,
    BODY_OP_COPY_I32 = 7,
    BODY_OP_I32_ADD = 8,
    BODY_OP_I32_SUB = 9,
    BODY_OP_STR_LITERAL = 10,
    BODY_OP_STR_LEN = 11,
    BODY_OP_CALL_COMPOSITE = 12,
    BODY_OP_COPY_COMPOSITE = 13,
    BODY_OP_UNWRAP_OR_RETURN = 14,
    BODY_OP_MAKE_COMPOSITE = 15,
    BODY_OP_MAKE_SEQ_I32 = 16,
    BODY_OP_SEQ_I32_INDEX = 17,
    BODY_OP_ARRAY_I32_INDEX_DYNAMIC = 18,
    BODY_OP_SEQ_I32_INDEX_DYNAMIC = 19,
    BODY_OP_I32_MUL = 20,
    BODY_OP_I32_ASR = 21,
    BODY_OP_I32_AND = 22,
    BODY_OP_STR_EQ = 23,
    BODY_OP_SEQ_I32_ADD = 24,
    BODY_OP_FIELD_REF = 25,
    BODY_OP_STR_REF_STORE = 26,
    BODY_OP_STR_CONCAT = 27,
    BODY_OP_I32_DIV = 28,
    BODY_OP_I32_TO_STR = 29,
    BODY_OP_PAYLOAD_STORE = 30,
    BODY_OP_STR_INDEX = 31,
    BODY_OP_SEQ_STR_INDEX_DYNAMIC = 32,
    BODY_OP_SEQ_STR_ADD = 33,
    BODY_OP_I32_CMP = 34,
    BODY_OP_I32_REF_LOAD = 35,
    BODY_OP_I32_REF_STORE = 36,
    BODY_OP_I32_OR = 37,
    BODY_OP_PTR_CONST = 38,
    BODY_OP_WRITE_LINE = 39,
    BODY_OP_WRITE_RAW = 115,
    BODY_OP_ARGC_LOAD = 40,
    BODY_OP_ARGV_STR = 41,
    BODY_OP_CWD_STR = 42,
    BODY_OP_PATH_JOIN = 43,
    BODY_OP_PATH_ABSOLUTE = 44,
    BODY_OP_PATH_PARENT = 45,
    BODY_OP_PATH_EXISTS = 46,
    BODY_OP_PATH_FILE_SIZE = 47,
    BODY_OP_PATH_WRITE_TEXT = 48,
    BODY_OP_TEXT_CONTAINS = 49,
    BODY_OP_MKDIR_ONE = 50,
    BODY_OP_ARRAY_I32_INDEX_STORE = 51,
    BODY_OP_SEQ_I32_INDEX_STORE = 52,
    BODY_OP_I32_MOD = 53,
    BODY_OP_I32_SHL = 54,
    BODY_OP_I32_XOR = 55,
    BODY_OP_TIME_NS = 56,
    BODY_OP_GETENV_STR = 57,
    BODY_OP_PARSE_INT = 58,
    BODY_OP_STR_JOIN = 59,
    BODY_OP_STR_SPLIT_CHAR = 60,
    BODY_OP_STR_STRIP = 61,
    BODY_OP_TEXT_SET_INIT = 62,
    BODY_OP_TEXT_SET_INSERT = 63,
    BODY_OP_GETRUSAGE = 64,
    BODY_OP_EXIT = 65,
    BODY_OP_STR_SLICE = 66,
    BODY_OP_READ_FLAG = 67,
    BODY_OP_SHELL_QUOTE = 68,
    BODY_OP_BRK = 69,
    BODY_OP_PATH_READ_TEXT = 70,
    BODY_OP_REMOVE_FILE = 71,
    BODY_OP_CHMOD_X = 72,
    BODY_OP_COLD_SELF_EXEC = 73,
    BODY_OP_I64_CONST = 74,
    BODY_OP_COPY_I64 = 75,
    BODY_OP_I64_FROM_I32 = 76,
    BODY_OP_I64_ADD = 77,
    BODY_OP_I64_SUB = 78,
    BODY_OP_I64_MUL = 79,
    BODY_OP_I64_DIV = 80,
    BODY_OP_I64_CMP = 81,
    BODY_OP_I64_TO_STR = 82,
    BODY_OP_OPEN = 83,
    BODY_OP_READ = 84,
    BODY_OP_CLOSE = 85,
    BODY_OP_MMAP = 86,
    BODY_OP_ATOMIC_LOAD_I32 = 87,
    BODY_OP_ATOMIC_STORE_I32 = 88,
    BODY_OP_ATOMIC_CAS_I32 = 89,
    BODY_OP_PTR_LOAD_I32 = 90,
    BODY_OP_PTR_STORE_I32 = 91,
    BODY_OP_PTR_LOAD_I64 = 92,
    BODY_OP_PTR_STORE_I64 = 93,
    BODY_OP_WRITE_BYTES = 94,
    BODY_OP_F32_CONST = 95,
    BODY_OP_F64_CONST = 96,
    BODY_OP_F32_ADD = 97,
    BODY_OP_F64_ADD = 98,
    BODY_OP_F32_SUB = 99,
    BODY_OP_F64_SUB = 100,
    BODY_OP_F32_MUL = 101,
    BODY_OP_F64_MUL = 102,
    BODY_OP_F32_DIV = 103,
    BODY_OP_F64_DIV = 104,
    BODY_OP_F32_CMP = 105,
    BODY_OP_F64_CMP = 106,
    BODY_OP_F32_NEG = 107,
    BODY_OP_F64_NEG = 108,
    BODY_OP_F32_FROM_I32 = 109,
    BODY_OP_I32_FROM_F32 = 110,
    BODY_OP_F64_FROM_I32 = 111,
    BODY_OP_I32_FROM_F64 = 112,
    BODY_OP_FN_ADDR = 113,
    BODY_OP_CALL_PTR = 114,
    BODY_OP_MAKE_SEQ_OPAQUE = 116,
    BODY_OP_SLOT_STORE_I32 = 117,
    BODY_OP_SLOT_STORE_I64 = 118,
    BODY_OP_EXEC_SHELL = 119,
    BODY_OP_SEQ_OPAQUE_INDEX_DYNAMIC = 120,
    BODY_OP_SEQ_OPAQUE_ADD = 121,
    BODY_OP_SEQ_OPAQUE_INDEX_STORE = 122,
    BODY_OP_SEQ_OPAQUE_REMOVE = 123,
    BODY_OP_I64_AND = 124,
    BODY_OP_I64_OR = 125,
    BODY_OP_I64_XOR = 126,
    BODY_OP_I64_SHL = 127,
    BODY_OP_I64_ASR = 128,
    BODY_OP_I32_FROM_I64 = 129,
    BODY_OP_ASSERT = 130,
    BODY_OP_STR_SELECT_NONEMPTY = 131,
    BODY_OP_CLOSURE_NEW = 132,
    BODY_OP_CLOSURE_CALL = 133,
    BODY_OP_PATH_WRITE_BYTES = 134,
    BODY_OP_PTR_ADD = 135,
    BODY_OP_COPY_RAW = 136,
    BODY_OP_PATH_IS_ABSOLUTE = 137,
    BODY_OP_THREAD_YIELD = 138,
    BODY_OP_SET_RAW = 139,
    BODY_OP_BYTES_ALLOC = 140,
};

enum {
    BODY_TERM_RET = 1,
    BODY_TERM_BR = 2,
    BODY_TERM_CBR = 3,
    BODY_TERM_SWITCH = 4,
};

enum {
    COND_EQ = 0,
    COND_NE = 1,
    COND_GE = 10,
    COND_LT = 11,
    COND_GT = 12,
    COND_LE = 13,
};

enum {
    SLOT_I32 = 1,
    SLOT_VARIANT = 2,
    SLOT_STR = 3,
    SLOT_PTR = 4,
    SLOT_OBJECT = 5,
    SLOT_ARRAY_I32 = 6,
    SLOT_SEQ_I32 = 7,
    SLOT_SEQ_I32_REF = 8,
    SLOT_OBJECT_REF = 9,
    SLOT_SEQ_STR = 10,
    SLOT_STR_REF = 11,
    SLOT_SEQ_STR_REF = 12,
    SLOT_OPAQUE = 13,
    SLOT_OPAQUE_REF = 14,
    SLOT_SEQ_OPAQUE = 15,
    SLOT_I32_REF = 16,
    SLOT_I64 = 17,
    SLOT_I64_REF = 18,
    SLOT_F32 = 19,
    SLOT_F64 = 20,
    SLOT_SEQ_OPAQUE_REF = 21,
};

enum {
    COLD_STR_DATA_OFFSET = 0,
    COLD_STR_LEN_OFFSET = 8,
    COLD_STR_STORE_ID_OFFSET = 12,
    COLD_STR_FLAGS_OFFSET = 16,
    COLD_STR_SLOT_SIZE = 24,
};

#define COLD_MAX_I32_PARAMS 32

typedef struct BodyIR {
    int32_t *op_kind;
    int32_t *op_dst;
    int32_t *op_a;
    int32_t *op_b;
    int32_t *op_c;
    int32_t op_count;
    int32_t op_cap;

    int32_t *term_kind;
    int32_t *term_value;
    int32_t *term_case_start;
    int32_t *term_case_count;
    int32_t *term_true_block;
    int32_t *term_false_block;
    int32_t term_count;
    int32_t term_cap;

    int32_t *block_op_start;
    int32_t *block_op_count;
    int32_t *block_term;
    int32_t block_count;
    int32_t block_cap;

    int32_t *slot_kind;
    int32_t *slot_offset;
    int32_t *slot_size;
    int32_t *slot_aux;
    Span *slot_type;
    int32_t *slot_no_alias;
    int32_t slot_count;
    int32_t slot_cap;
    int32_t frame_size;

    int32_t *switch_tag;
    int32_t *switch_block;
    int32_t *switch_term;
    int32_t switch_count;
    int32_t switch_cap;

    int32_t *call_arg_slot;
    int32_t *call_arg_offset;
    int32_t call_arg_count;
    int32_t call_arg_cap;

    Span *string_literal;
    int32_t string_literal_count;
    int32_t string_literal_cap;

    int32_t param_count;
    int32_t param_slot[COLD_MAX_I32_PARAMS];
    Span param_name[COLD_MAX_I32_PARAMS];
    int32_t return_kind;
    int32_t return_size;
    Span return_type;
    Span debug_name;
    int32_t sret_slot;
    bool has_fallback;

    Arena *arena;
} BodyIR;

/* ================================================================
 * Symbol table types
 * ================================================================ */
typedef struct Variant {
    Span name;
    int32_t tag;
    int32_t field_count;
    int32_t field_kind[COLD_MAX_VARIANT_FIELDS];
    int32_t field_size[COLD_MAX_VARIANT_FIELDS];
    int32_t field_offset[COLD_MAX_VARIANT_FIELDS];
    Span field_type[COLD_MAX_VARIANT_FIELDS];
} Variant;

typedef struct TypeDef {
    Span name;
    Variant *variants;
    int32_t variant_count;
    Span generic_names[COLD_MAX_VARIANT_FIELDS];
    int32_t generic_count;
    int32_t max_field_count;
    int32_t max_slot_size;
    bool is_enum;
} TypeDef;

typedef struct ObjectField {
    Span name;
    int32_t kind;
    int32_t offset;
    int32_t size;
    int32_t array_len;
    Span type_name;
} ObjectField;

typedef struct ObjectDef {
    Span name;
    ObjectField *fields;
    int32_t field_count;
    int32_t slot_size;
    Span generic_names[COLD_MAX_VARIANT_FIELDS];
    int32_t generic_count;
    bool is_ref;
} ObjectDef;

typedef struct FnDef {
    Span name;
    int32_t arity;
    int32_t param_kind[COLD_MAX_I32_PARAMS];
    int32_t param_size[COLD_MAX_I32_PARAMS];
    bool param_has_default[COLD_MAX_I32_PARAMS];
    int32_t param_default_value[COLD_MAX_I32_PARAMS];
    Span ret;
    bool is_external;
    Span generic_names[4];
    int32_t generic_count;
} FnDef;

typedef struct ConstDef {
    Span name;
    int32_t value;
    bool  is_str;
    Span  str_val;
} ConstDef;

typedef struct GlobalDef {
    Span name;
    int32_t kind;
    int32_t size;
    Span type_name;
    int32_t init_value;
} GlobalDef;

typedef struct Symbols {
    FnDef *functions;
    int32_t function_count;
    int32_t function_cap;
    TypeDef *types;
    int32_t type_count;
    int32_t type_cap;
    ObjectDef *objects;
    int32_t object_count;
    int32_t object_cap;
    ConstDef *consts;
    int32_t const_count;
    int32_t const_cap;
    GlobalDef *globals;
    int32_t global_count;
    int32_t global_cap;
    Arena *arena;
} Symbols;

typedef struct Local {
    Span name;
    int32_t slot;
    int32_t kind;
} Local;

typedef struct Locals {
    Local *items;
    int32_t count;
    int32_t cap;
    Arena *arena;
} Locals;

/* ================================================================
 * Parser type
 * ================================================================ */
typedef struct ColdImportSource ColdImportSource;

typedef struct Parser {
    Span source;
    int32_t pos;
    Arena *arena;
    Symbols *symbols;
    bool import_mode;  /* when true, parse_fn skips symbols_add_fn */
    Span import_alias;
    struct ColdImportSource *import_sources;  /* for import alias resolution in bare names */
    int32_t import_source_count;
    BodyIR **function_bodies;   /* for storing closure/anonymous function bodies */
    int32_t function_body_cap;  /* capacity of function_bodies array */
    int32_t closure_count;      /* counter for generating unique closure names */
} Parser;

struct ColdImportSource {
    Span alias;
    char path[PATH_MAX];
};

/* ================================================================
 * Cold CSG types
 * ================================================================ */
typedef struct ColdCsgStmt {
    int32_t fn_index;
    int32_t indent;
    Span kind;
    Span a;
    Span b;
    Span c;
    Span d;
} ColdCsgStmt;

typedef struct ColdCsgFunction {
    Span name;
    Span params;
    Span ret;
    int32_t stmt_start;
    int32_t stmt_count;
    int32_t symbol_index;
} ColdCsgFunction;

typedef struct ColdCsg {
    ColdCsgFunction *functions;
    int32_t function_count;
    int32_t function_cap;
    ColdCsgStmt *stmts;
    int32_t stmt_count;
    int32_t stmt_cap;
    Span entry;
    Arena *arena;
    Symbols *symbols;
} ColdCsg;

typedef struct ColdCsgLower {
    ColdCsg *csg;
    int32_t fn_index;
    int32_t cursor;
    int32_t end;
    Parser owner;
} ColdCsgLower;

/* ================================================================
 * Loop context
 * ================================================================ */
#define COLD_LOOP_PATCH_CAP 128

typedef struct LoopCtx {
    int32_t break_terms[COLD_LOOP_PATCH_CAP];
    int32_t break_count;
    int32_t continue_terms[COLD_LOOP_PATCH_CAP];
    int32_t continue_count;
    struct LoopCtx *parent;
} LoopCtx;

/* ================================================================
 * String literal, match constants
 * ================================================================ */
#define COLD_MATCH_VARIANT_CAP 128
#define COLD_MATCH_FALLTHROUGH_CAP 128
#define COLD_CSG_MATCH_VARIANT_CAP 128
#define COLD_CSG_MATCH_FALLTHROUGH_CAP 128

/* ================================================================
 * Compile stats
 * ================================================================ */
typedef struct ColdCompileStats {
    int32_t function_count;
    int32_t type_count;
    int32_t op_count;
    int32_t block_count;
    int32_t switch_count;
    int32_t call_count;
    int32_t csg_lowering;
    int32_t csg_statement_count;
    int32_t code_words;
    int32_t param_count;
    int32_t abi_register_params;
    int32_t abi_stack_params;
    int32_t max_frame_size;
    char max_frame_function[COLD_NAME_CAP];
    int32_t after_csg_frame_size;
    int32_t after_source_bundle_frame_size;
    size_t arena_kb;
    uint64_t elapsed_us;
    uint64_t parse_us;
    uint64_t codegen_us;
    uint64_t emit_us;
    uint64_t facts_bytes;
    uint64_t facts_mmap_us;
    uint64_t facts_verify_us;
    uint64_t facts_decode_us;
    uint64_t facts_emit_obj_us;
    uint64_t facts_emit_exe_us;
    uint64_t facts_total_us;
    int32_t facts_function_count;
    int32_t facts_word_count;
    int32_t facts_reloc_count;
} ColdCompileStats;

/* ================================================================
 * ================================================================ */
extern BodyIR *cold_current_parsing_body;
/* ColdArgv0 is a char ** global set in main() for self-binary materialize */
extern char **ColdArgv0;

/* ================================================================
 * ================================================================ */

/* Arena */
void *arena_alloc(Arena *a, size_t size);

/* Die / error */
void die(const char *msg);
void cold_die_report_flush(void);

/* Span helpers */
Span source_open(const char *path);
Span span_sub(Span s, int32_t start, int32_t end);
bool span_eq(Span s, const char *text);
bool span_same(Span a, Span b);
bool span_is_i32(Span s);
int32_t span_i32(Span s);
int64_t span_i64(Span s);
Span span_trim(Span s);
void span_write(FILE *file, Span s);
bool cold_span_is_float(Span s);
double cold_span_f64(Span s);
Span cold_decode_string_content(Arena *arena, Span raw, bool fmt_literal);
int32_t cold_decode_char_literal(Span token);
int32_t cold_span_find_char(Span span, char needle);
int32_t cold_span_find_top_level_char(Span span, char needle);

/* Alignment */
int32_t align_i32(int32_t v, int32_t a);

/* Hash */
uint64_t cold_fnv1a64_update_cstr(uint64_t hash, const char *text);
uint64_t cold_fnv1a64_update(uint64_t hash, Span span);

/* Slot kind helpers */
int32_t cold_slot_kind_from_code(char code);
int32_t cold_slot_size_for_kind(int32_t kind);
int32_t cold_slot_align_for_kind(int32_t kind);
int32_t cold_slot_size_from_type_with_symbols(Symbols *symbols, Span type, int32_t kind);
int32_t cold_slot_kind_from_type_with_symbols(Symbols *symbols, Span type);
Span cold_type_strip_var(Span type, bool *is_var);
bool cold_parse_i32_seq_type(Span type);
bool cold_parse_str_seq_type(Span type);
bool cold_type_has_qualified_name(Span type);
bool cold_span_starts_with(Span span, const char *prefix);
int32_t cold_arg_reg_count(int32_t kind, int32_t size);
int32_t cold_param_size_from_type(Symbols *symbols, Span type, int32_t kind);

/* BodyIR functions */
BodyIR *body_new(Arena *arena);
int32_t body_slot(BodyIR *body, int32_t kind, int32_t size);
void body_slot_set_type(BodyIR *body, int32_t slot, Span type);
void body_slot_set_array_len(BodyIR *body, int32_t slot, int32_t len);
int32_t body_op3(BodyIR *body, int32_t kind, int32_t dst,
                 int32_t a, int32_t b, int32_t c);
int32_t body_op(BodyIR *body, int32_t kind, int32_t dst, int32_t a, int32_t b);
int32_t body_term(BodyIR *body, int32_t kind, int32_t value,
                  int32_t case_start, int32_t case_count,
                  int32_t true_block, int32_t false_block);
int32_t body_block(BodyIR *body);
void body_end_block(BodyIR *body, int32_t block, int32_t term);
void body_reopen_block(BodyIR *body, int32_t block);
int32_t body_switch_case(BodyIR *body, int32_t owner_term, int32_t tag, int32_t block);
int32_t body_call_arg(BodyIR *body, int32_t slot);
int32_t body_call_arg_with_offset(BodyIR *body, int32_t slot, int32_t offset);
int32_t body_string_literal(BodyIR *body, Span literal);
Span body_get_str_literal_span(BodyIR *body, int32_t index);
int32_t body_slot_for_object_field(BodyIR *body, ObjectField *field);
int32_t cold_span_find_top_level_char(Span span, char needle);

/* Symbols functions */
Symbols *symbols_new(Arena *arena);
int32_t symbols_add_fn(Symbols *symbols, Span name, int32_t arity,
                       const int32_t *param_kinds,
                       const int32_t *param_sizes,
                       Span ret);
int32_t symbols_find_fn(Symbols *symbols, Span name, int32_t arity,
                        const int32_t *param_kinds,
                        const int32_t *param_sizes,
                        Span ret);
FnDef *symbols_get_fn(Symbols *symbols, int32_t index);
TypeDef *symbols_add_type(Symbols *symbols, Span name, int32_t variant_count);
TypeDef *symbols_find_type(Symbols *symbols, Span name);
ObjectDef *symbols_add_object(Symbols *symbols, Span name, int32_t field_count);
ObjectDef *symbols_find_object(Symbols *symbols, Span name);
ConstDef *symbols_add_const(Symbols *symbols, Span name, int32_t value);
ConstDef *symbols_find_const(Symbols *symbols, Span name);
GlobalDef *symbols_find_global(Symbols *symbols, Span name);
void symbols_add_global(Symbols *symbols, Span name, int32_t kind,
                        int32_t size, Span type_name, int32_t init_value);
ObjectDef *symbols_resolve_object(Symbols *symbols, Span name);
void symbols_refine_object_layouts(Symbols *symbols);

/* External helpers used by parser */
int32_t cold_make_i32_const_slot(BodyIR *body, int32_t value);
int32_t cold_make_str_literal_cstr_slot(BodyIR *body, const char *text);
int32_t cold_seq_opaque_element_size_for_slot(Symbols *symbols, BodyIR *body, int32_t slot);
Variant *type_find_variant(TypeDef *type, Span name);
int32_t cold_split_top_level_commas(Span text, Span *parts, int32_t cap);
void object_finalize_layout(ObjectDef *object);
int32_t cold_make_zero_const_slot(BodyIR *body);

/* Cold CSG helpers used by parser */
int32_t cold_slot_size_from_type_with_symbols(Symbols *symbols, Span type, int32_t kind);
int32_t cold_slot_kind_from_type_with_symbols(Symbols *symbols, Span type);

/* Object/string/cold helpers */
bool cold_ident_char(uint8_t c);
int32_t cold_require_str_value(BodyIR *body, int32_t slot, const char *context);
ObjectField *cold_required_object_field(ObjectDef *object, const char *name);
Span cold_cstr_span(const char *text);

/* Source scanning helpers called from cold_parser.c */
int32_t cold_line_indent_width(Span line);
bool cold_line_top_level(Span line);
bool cold_line_has_triple_quote(Span line);
void symbols_add_str_const(Symbols *symbols, Span name, Span str_val);
bool cold_parse_function_symbol_at(Span source, int32_t fn_start, int32_t line_no,
                                    ColdFunctionSymbol *symbol);
bool cold_type_block_looks_like_object(Span body);
int32_t cold_line_end_from(Span source, int32_t pos);
ObjectField *object_find_field(ObjectDef *object, Span name);
int32_t symbols_variant_slot_size(Symbols *symbols, Variant *variant);
int32_t cold_return_kind_from_span(Symbols *symbols, Span ret);
int32_t cold_return_slot_size(Symbols *symbols, Span ret, int32_t kind);
Local *locals_find(Locals *locals, Span name);

/* Type/object helper functions (defined in cheng_cold.c, called from cold_parser.c) */
bool cold_parse_opaque_seq_type(Span type);
bool cold_parse_i32_array_type(Span type, int32_t *len_out);
bool cold_span_is_simple_ident(Span span);
bool cold_type_parse_generic_instance(Span type, Span *base, Span *args);
bool cold_type_is_generic_placeholder(Span type, Span *generic_names,
                                      int32_t generic_count);
Variant *symbols_find_variant(Symbols *symbols, Span name);
TypeDef *symbols_find_variant_type(Symbols *symbols, Variant *variant);
void variant_finalize_layout(TypeDef *type, Variant *variant);
int32_t symbols_type_slot_size(TypeDef *type);
void object_finalize_fields(ObjectDef *object);
int32_t symbols_object_slot_size(ObjectDef *object);
TypeDef *symbols_resolve_type(Symbols *symbols, Span type_name);

/* Parser */


#endif /* COLD_TYPES_H */
