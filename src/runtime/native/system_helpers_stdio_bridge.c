#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#include "system_helpers.h"

extern char **environ;
extern int cheng_v2c_tooling_handle(int argc, char **argv) __attribute__((weak_import));
extern int cheng_v2c_tooling_is_command(const char *cmd) __attribute__((weak_import));

typedef struct DriverCSha256Ctx {
  uint8_t data[64];
  uint32_t datalen;
  uint64_t bitlen;
  uint32_t state[8];
} DriverCSha256Ctx;

typedef struct DriverCNativeStageArtifacts {
  char *compiler_path;
  char *release_path;
  char *system_link_plan_path;
  char *system_link_exec_path;
  char *binary_path;
  char *binary_cid;
  char *output_file_cid;
  int32_t external_cc_provider_count;
  char *compiler_core_provider_source_kind;
  char *compiler_core_provider_compile_mode;
} DriverCNativeStageArtifacts;

typedef enum DriverCProgValueKind {
  DRIVER_C_PROG_VALUE_NIL = 0,
  DRIVER_C_PROG_VALUE_BOOL = 1,
  DRIVER_C_PROG_VALUE_I32 = 2,
  DRIVER_C_PROG_VALUE_I64 = 3,
  DRIVER_C_PROG_VALUE_U32 = 4,
  DRIVER_C_PROG_VALUE_U64 = 5,
  DRIVER_C_PROG_VALUE_F64 = 6,
  DRIVER_C_PROG_VALUE_STR = 7,
  DRIVER_C_PROG_VALUE_BYTES = 8,
  DRIVER_C_PROG_VALUE_ARRAY = 9,
  DRIVER_C_PROG_VALUE_RECORD = 10,
  DRIVER_C_PROG_VALUE_ROUTINE = 11,
  DRIVER_C_PROG_VALUE_MODULE = 12,
  DRIVER_C_PROG_VALUE_TYPE_TOKEN = 13,
  DRIVER_C_PROG_VALUE_REF = 14,
  DRIVER_C_PROG_VALUE_PTR = 15,
  DRIVER_C_PROG_VALUE_ZERO_PLAN = 16
} DriverCProgValueKind;

typedef enum DriverCProgTopLevelTag {
  DRIVER_C_PROG_TOP_UNKNOWN = 0,
  DRIVER_C_PROG_TOP_IMPORT = 1,
  DRIVER_C_PROG_TOP_TYPE = 2,
  DRIVER_C_PROG_TOP_VAR = 3,
  DRIVER_C_PROG_TOP_CONST = 4,
  DRIVER_C_PROG_TOP_FN = 5,
  DRIVER_C_PROG_TOP_ITERATOR = 6,
  DRIVER_C_PROG_TOP_IMPORTC_FN = 7,
  DRIVER_C_PROG_TOP_BUILTIN = 8
} DriverCProgTopLevelTag;

typedef enum DriverCProgOpTag {
  DRIVER_C_PROG_OP_UNKNOWN = 0,
  DRIVER_C_PROG_OP_LOCAL_DECL,
  DRIVER_C_PROG_OP_LOAD_BOOL,
  DRIVER_C_PROG_OP_LOAD_NIL,
  DRIVER_C_PROG_OP_LOAD_STR,
  DRIVER_C_PROG_OP_LOAD_INT32,
  DRIVER_C_PROG_OP_LOAD_INT64,
  DRIVER_C_PROG_OP_LOAD_FLOAT64,
  DRIVER_C_PROG_OP_LOAD_PARAM,
  DRIVER_C_PROG_OP_LOAD_LOCAL,
  DRIVER_C_PROG_OP_ADDR_OF_PARAM,
  DRIVER_C_PROG_OP_ADDR_OF_LOCAL,
  DRIVER_C_PROG_OP_ADDR_OF_NAME,
  DRIVER_C_PROG_OP_ADDR_OF_FIELD,
  DRIVER_C_PROG_OP_ADDR_OF_INDEX,
  DRIVER_C_PROG_OP_LOAD_IMPORT,
  DRIVER_C_PROG_OP_LOAD_ROUTINE,
  DRIVER_C_PROG_OP_LOAD_GLOBAL,
  DRIVER_C_PROG_OP_LOAD_NAME,
  DRIVER_C_PROG_OP_LOAD_FIELD,
  DRIVER_C_PROG_OP_LOAD_INDEX,
  DRIVER_C_PROG_OP_CALL,
  DRIVER_C_PROG_OP_BINARY_OP,
  DRIVER_C_PROG_OP_UNARY_NOT,
  DRIVER_C_PROG_OP_UNARY_NEG,
  DRIVER_C_PROG_OP_MAKE_ARRAY,
  DRIVER_C_PROG_OP_MAKE_TUPLE,
  DRIVER_C_PROG_OP_MAKE_RECORD,
  DRIVER_C_PROG_OP_MATERIALIZE,
  DRIVER_C_PROG_OP_JUMP_IF_FALSE,
  DRIVER_C_PROG_OP_JUMP,
  DRIVER_C_PROG_OP_LABEL,
  DRIVER_C_PROG_OP_PHI_MERGE,
  DRIVER_C_PROG_OP_RETURN_VALUE,
  DRIVER_C_PROG_OP_RETURN_VOID,
  DRIVER_C_PROG_OP_STORE_LOCAL,
  DRIVER_C_PROG_OP_STORE_PARAM,
  DRIVER_C_PROG_OP_STORE_NAME,
  DRIVER_C_PROG_OP_STORE_LOCAL_FIELD,
  DRIVER_C_PROG_OP_STORE_PARAM_FIELD,
  DRIVER_C_PROG_OP_STORE_FIELD,
  DRIVER_C_PROG_OP_STORE_INDEX,
  DRIVER_C_PROG_OP_DROP_VALUE,
  DRIVER_C_PROG_OP_FOR_RANGE_INIT,
  DRIVER_C_PROG_OP_FOR_RANGE_CHECK,
  DRIVER_C_PROG_OP_FOR_RANGE_STEP,
  DRIVER_C_PROG_OP_CAST_INT32,
  DRIVER_C_PROG_OP_CAST_INT64,
  DRIVER_C_PROG_OP_CAST_TYPE,
  DRIVER_C_PROG_OP_NEW_REF
} DriverCProgOpTag;

typedef enum DriverCProgBuiltinTag {
  DRIVER_C_PROG_BUILTIN_NONE = 0,
  DRIVER_C_PROG_BUILTIN_PARAM_COUNT,
  DRIVER_C_PROG_BUILTIN_PARAM_STR,
  DRIVER_C_PROG_BUILTIN_READ_FLAG_VALUE,
  DRIVER_C_PROG_BUILTIN_PARSE_INT32,
  DRIVER_C_PROG_BUILTIN_PARSE_INT64,
  DRIVER_C_PROG_BUILTIN_INT_TO_STR,
  DRIVER_C_PROG_BUILTIN_UINT64_TO_STR,
  DRIVER_C_PROG_BUILTIN_INT64_TO_STR,
  DRIVER_C_PROG_BUILTIN_LEN,
  DRIVER_C_PROG_BUILTIN_SLICE_BYTES,
  DRIVER_C_PROG_BUILTIN_FIND,
  DRIVER_C_PROG_BUILTIN_STARTS_WITH,
  DRIVER_C_PROG_BUILTIN_STR_CONTAINS,
  DRIVER_C_PROG_BUILTIN_STRIP,
  DRIVER_C_PROG_BUILTIN_SPLIT,
  DRIVER_C_PROG_BUILTIN_CHAR_TO_STR,
  DRIVER_C_PROG_BUILTIN_PARENT_DIR,
  DRIVER_C_PROG_BUILTIN_ABSOLUTE_PATH,
  DRIVER_C_PROG_BUILTIN_JOIN_PATH,
  DRIVER_C_PROG_BUILTIN_FILE_EXISTS,
  DRIVER_C_PROG_BUILTIN_DIR_EXISTS,
  DRIVER_C_PROG_BUILTIN_CREATE_DIR,
  DRIVER_C_PROG_BUILTIN_READ_FILE,
  DRIVER_C_PROG_BUILTIN_WRITE_FILE,
  DRIVER_C_PROG_BUILTIN_CHAIN_APPEND_LINE,
  DRIVER_C_PROG_BUILTIN_ECHO,
  DRIVER_C_PROG_BUILTIN_EMPTY_BYTES,
  DRIVER_C_PROG_BUILTIN_BYTES_ALLOC,
  DRIVER_C_PROG_BUILTIN_BYTES_LEN,
  DRIVER_C_PROG_BUILTIN_BYTES_FROM_STRING,
  DRIVER_C_PROG_BUILTIN_BYTES_TO_STRING,
  DRIVER_C_PROG_BUILTIN_BYTES_GET,
  DRIVER_C_PROG_BUILTIN_BYTES_SET,
  DRIVER_C_PROG_BUILTIN_BYTES_SLICE,
  DRIVER_C_PROG_BUILTIN_BYTES_TO_HEX,
  DRIVER_C_PROG_BUILTIN_SHA256_DIGEST,
  DRIVER_C_PROG_BUILTIN_K256,
  DRIVER_C_PROG_BUILTIN_K512,
  DRIVER_C_PROG_BUILTIN_ALLOC,
  DRIVER_C_PROG_BUILTIN_DEALLOC,
  DRIVER_C_PROG_BUILTIN_REALLOC,
  DRIVER_C_PROG_BUILTIN_SET_LEN,
  DRIVER_C_PROG_BUILTIN_RESERVE,
  DRIVER_C_PROG_BUILTIN_ADD,
  DRIVER_C_PROG_BUILTIN_SIZEOF,
  DRIVER_C_PROG_BUILTIN_INIT_MSQUIC_TRANSPORT,
  DRIVER_C_PROG_BUILTIN_CMDLINE_PARAM_STR_COPY_INTO_BRIDGE,
  DRIVER_C_PROG_BUILTIN_INT_TO_STR_RAW,
  DRIVER_C_PROG_BUILTIN_UINT64_TO_STR_RAW,
  DRIVER_C_PROG_BUILTIN_INT64_TO_STR_RAW
} DriverCProgBuiltinTag;

typedef struct DriverCProgValue DriverCProgValue;
typedef struct DriverCProgItem DriverCProgItem;
typedef struct DriverCProgArray DriverCProgArray;
typedef struct DriverCProgRecord DriverCProgRecord;
typedef struct DriverCProgZeroPlan DriverCProgZeroPlan;

enum {
  DRIVER_C_PROG_VALUE_FLAG_EPHEMERAL_AGGREGATE = 1
};

#define DRIVER_C_PROG_ARRAY_INLINE_CAP 8
#define DRIVER_C_PROG_RECORD_INLINE_CAP 8
#define DRIVER_C_PROG_RECORD_LOOKUP_INLINE_CAP 16

typedef struct DriverCProgBytes {
  uint8_t *data;
  int32_t len;
} DriverCProgBytes;

typedef struct DriverCProgStrSeqSlotToken {
  uint32_t magic;
  DriverCProgValue *slot;
} DriverCProgStrSeqSlotToken;

typedef struct DriverCProgRoutine {
  int32_t item_id;
  const char *label;
  const char *owner_module;
  const char *top_level_kind;
  int32_t top_level_tag;
  int32_t builtin_tag;
  const char *importc_symbol;
  const char **type_arg_texts;
  int32_t type_arg_count;
} DriverCProgRoutine;

typedef struct DriverCProgModuleToken {
  int32_t import_item_id;
  const char *alias;
  const char *target_module;
} DriverCProgModuleToken;

typedef struct DriverCProgRef {
  DriverCProgValue *slot;
} DriverCProgRef;

struct DriverCProgValue {
  DriverCProgValueKind kind;
  uint32_t flags;
  union {
    int32_t b;
    int32_t i32;
    int64_t i64;
    uint32_t u32;
    uint64_t u64;
    double f64;
    ChengStrBridge str;
    void *ptr;
    DriverCProgBytes bytes;
    DriverCProgArray *array;
    DriverCProgRecord *record;
    DriverCProgRoutine routine;
    DriverCProgModuleToken module;
    const char *type_text;
    DriverCProgRef ref;
    DriverCProgZeroPlan *zero_plan;
  } as;
};

struct DriverCProgArray {
  int32_t len;
  int32_t cap;
  const char *elem_type_text;
  DriverCProgValue *items;
  struct DriverCProgArray *next_registered;
  DriverCProgValue inline_items[DRIVER_C_PROG_ARRAY_INLINE_CAP];
};

struct DriverCProgRecord {
  int32_t len;
  int32_t cap;
  const char *type_text;
  char **names;
  DriverCProgValue *values;
  int32_t *lookup_indices;
  int32_t lookup_cap;
  int32_t names_shared;
  int32_t lookup_shared;
  char *inline_names[DRIVER_C_PROG_RECORD_INLINE_CAP];
  DriverCProgValue inline_values[DRIVER_C_PROG_RECORD_INLINE_CAP];
  int32_t inline_lookup_indices[DRIVER_C_PROG_RECORD_LOOKUP_INLINE_CAP];
};

typedef struct DriverCProgOp {
  char *kind;
  char *primary;
  char *secondary;
  char **secondary_lines;
  int32_t secondary_line_count;
  char *cached_record_type;
  struct DriverCProgTypeDecl *cached_record_decl;
  int32_t kind_tag;
  int32_t arg0;
  int32_t arg1;
  int32_t arg2;
} DriverCProgOp;

typedef struct DriverCProgTypeDecl {
  struct DriverCProgItem *item;
  const char *owner_module;
  char *decl_name;
  char *alias_target;
  char **param_names;
  int32_t param_count;
  int32_t param_cap;
  char **field_names;
  char **field_types;
  int32_t field_count;
  int32_t field_cap;
  int32_t *field_lookup_indices;
  int32_t field_lookup_cap;
  int32_t first_enum_ordinal;
  int32_t has_first_enum_ordinal;
} DriverCProgTypeDecl;

typedef enum DriverCProgZeroPlanKind {
  DRIVER_C_PROG_ZERO_PLAN_NIL = 0,
  DRIVER_C_PROG_ZERO_PLAN_BOOL,
  DRIVER_C_PROG_ZERO_PLAN_I32,
  DRIVER_C_PROG_ZERO_PLAN_U32,
  DRIVER_C_PROG_ZERO_PLAN_I64,
  DRIVER_C_PROG_ZERO_PLAN_U64,
  DRIVER_C_PROG_ZERO_PLAN_F64,
  DRIVER_C_PROG_ZERO_PLAN_STR,
  DRIVER_C_PROG_ZERO_PLAN_PTR,
  DRIVER_C_PROG_ZERO_PLAN_BYTES,
  DRIVER_C_PROG_ZERO_PLAN_ENUM,
  DRIVER_C_PROG_ZERO_PLAN_DYNAMIC_ARRAY,
  DRIVER_C_PROG_ZERO_PLAN_FIXED_ARRAY,
  DRIVER_C_PROG_ZERO_PLAN_RECORD
} DriverCProgZeroPlanKind;

struct DriverCProgZeroPlan {
  int32_t kind;
  char *type_text;
  char *elem_type_text;
  int32_t fixed_len;
  int32_t enum_ordinal;
  char **field_names;
  DriverCProgZeroPlan **field_plans;
  int32_t field_count;
  int32_t *field_lookup_indices;
  int32_t field_lookup_cap;
};

typedef struct DriverCProgItem {
  int32_t item_id;
  char *label;
  char *canonical_op;
  char *top_level_kind;
  char *owner_module;
  char *source_path;
  char *import_path;
  char *import_target_module;
  char *import_alias;
  char *import_visible_name;
  char *import_category;
  char *importc_symbol;
  char *return_type;
  char *body_text;
  char *payload_text;
  int32_t ast_ready;
  int32_t top_level_tag;
  int32_t param_count;
  int32_t local_count;
  int32_t stmt_count;
  int32_t expr_count;
  int32_t signature_line_count;
  char **signature_lines;
  char **type_param_names;
  int32_t type_param_count;
  int32_t type_param_cap;
  char **param_type_texts;
  char **local_type_texts;
  int32_t *param_is_var;
  int32_t exec_op_count;
  DriverCProgOp *ops;
} DriverCProgItem;

typedef struct DriverCProgZeroPlanCacheEntry {
  const char *owner_module;
  const char *inst_owner_module;
  char *type_text;
  DriverCProgZeroPlan *plan;
  int32_t used;
} DriverCProgZeroPlanCacheEntry;

typedef struct DriverCProgItemLookupEntry {
  const char *owner_module;
  const char *label;
  DriverCProgItem *item;
  int32_t used;
  int32_t ambiguous;
} DriverCProgItemLookupEntry;

typedef struct DriverCProgItemKindLookupEntry {
  const char *owner_module;
  const char *label;
  const char *top_level_kind;
  DriverCProgItem *item;
  int32_t used;
  int32_t ambiguous;
} DriverCProgItemKindLookupEntry;

typedef struct DriverCProgImportLookupEntry {
  const char *owner_module;
  const char *visible_name;
  DriverCProgItem *item;
  int32_t used;
  int32_t ambiguous;
} DriverCProgImportLookupEntry;

typedef struct DriverCProgTypeDeclLookupEntry {
  const char *owner_module;
  const char *decl_name;
  DriverCProgTypeDecl *decl;
  int32_t used;
  int32_t ambiguous;
} DriverCProgTypeDeclLookupEntry;

typedef struct DriverCProgVisibleItemCacheEntry {
  const char *owner_module;
  const char *name;
  DriverCProgItem *item;
  int32_t used;
} DriverCProgVisibleItemCacheEntry;

typedef struct DriverCProgVisibleTypeDeclCacheEntry {
  const char *owner_module;
  char *type_text;
  DriverCProgTypeDecl *decl;
  int32_t used;
  int32_t missing;
} DriverCProgVisibleTypeDeclCacheEntry;

typedef struct DriverCProgRoutineTypeArgsLookupEntry {
  int32_t item_id;
  int32_t type_arg_count;
  char **type_arg_texts;
  int32_t used;
} DriverCProgRoutineTypeArgsLookupEntry;

typedef struct DriverCProgRegistry {
  int loaded;
  int32_t entry_item_id;
  char *entry_label;
  int32_t item_count;
  DriverCProgItem *items;
  DriverCProgItem **items_by_id;
  int32_t items_by_id_len;
  DriverCProgValue *global_values;
  int32_t *global_initialized;
  int32_t item_lookup_cap;
  DriverCProgItemLookupEntry *item_lookup;
  int32_t item_kind_lookup_cap;
  DriverCProgItemKindLookupEntry *item_kind_lookup;
  int32_t import_lookup_cap;
  DriverCProgImportLookupEntry *import_lookup;
  int32_t type_decl_count;
  int32_t type_decl_cap;
  DriverCProgTypeDecl *type_decls;
  int32_t type_decl_lookup_cap;
  DriverCProgTypeDeclLookupEntry *type_decl_lookup;
  int32_t visible_item_cache_cap;
  DriverCProgVisibleItemCacheEntry *visible_item_cache;
  int32_t visible_type_decl_cache_cap;
  DriverCProgVisibleTypeDeclCacheEntry *visible_type_decl_cache;
  int32_t routine_type_args_lookup_cap;
  DriverCProgRoutineTypeArgsLookupEntry *routine_type_args_lookup;
  int32_t zero_plan_cache_cap;
  DriverCProgZeroPlanCacheEntry *zero_plan_cache;
} DriverCProgRegistry;

#define DRIVER_C_PROG_FRAME_INLINE_PARAM_CAP 8
#define DRIVER_C_PROG_FRAME_INLINE_LOCAL_CAP 24
#define DRIVER_C_PROG_FRAME_INLINE_STACK_CAP 64
#define DRIVER_C_PROG_FRAME_INLINE_LABEL_CAP 64

typedef struct DriverCProgFrame {
  DriverCProgRegistry *registry;
  DriverCProgItem *item;
  DriverCProgValue *params;
  DriverCProgZeroPlan **param_zero_plans;
  DriverCProgValue *locals;
  DriverCProgZeroPlan **local_zero_plans;
  char **local_names;
  int32_t *local_is_var;
  DriverCProgValue *stack;
  int32_t stack_len;
  int32_t stack_cap;
  char **label_names;
  int32_t *label_pcs;
  int32_t label_count;
  char **loop_names;
  int64_t *loop_currents;
  int64_t *loop_ends;
  int32_t *loop_stmt_ids;
  int32_t loop_count;
  const char **type_arg_texts;
  int32_t type_arg_count;
  int32_t current_pc;
  const DriverCProgOp *current_op;
  const char *current_op_kind;
  const char *current_op_primary;
  const char *current_op_secondary;
  DriverCProgValue inline_params[DRIVER_C_PROG_FRAME_INLINE_PARAM_CAP];
  DriverCProgZeroPlan *inline_param_zero_plans[DRIVER_C_PROG_FRAME_INLINE_PARAM_CAP];
  DriverCProgValue inline_locals[DRIVER_C_PROG_FRAME_INLINE_LOCAL_CAP];
  DriverCProgZeroPlan *inline_local_zero_plans[DRIVER_C_PROG_FRAME_INLINE_LOCAL_CAP];
  char *inline_local_names[DRIVER_C_PROG_FRAME_INLINE_LOCAL_CAP];
  int32_t inline_local_is_var[DRIVER_C_PROG_FRAME_INLINE_LOCAL_CAP];
  DriverCProgValue inline_stack[DRIVER_C_PROG_FRAME_INLINE_STACK_CAP];
  char *inline_label_names[DRIVER_C_PROG_FRAME_INLINE_LABEL_CAP];
  int32_t inline_label_pcs[DRIVER_C_PROG_FRAME_INLINE_LABEL_CAP];
} DriverCProgFrame;

static const uint32_t driver_c_sha256_table[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

#define DRIVER_C_SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define DRIVER_C_SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define DRIVER_C_SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define DRIVER_C_SHA256_EP0(x) (DRIVER_C_SHA256_ROTR((x), 2) ^ DRIVER_C_SHA256_ROTR((x), 13) ^ DRIVER_C_SHA256_ROTR((x), 22))
#define DRIVER_C_SHA256_EP1(x) (DRIVER_C_SHA256_ROTR((x), 6) ^ DRIVER_C_SHA256_ROTR((x), 11) ^ DRIVER_C_SHA256_ROTR((x), 25))
#define DRIVER_C_SHA256_SIG0(x) (DRIVER_C_SHA256_ROTR((x), 7) ^ DRIVER_C_SHA256_ROTR((x), 18) ^ ((x) >> 3))
#define DRIVER_C_SHA256_SIG1(x) (DRIVER_C_SHA256_ROTR((x), 17) ^ DRIVER_C_SHA256_ROTR((x), 19) ^ ((x) >> 10))

__attribute__((weak)) char *driver_c_read_file_all(const char *path);
static void driver_c_die(const char *message);
static char *driver_c_absolute_path_dup(const char *path);
static char *driver_c_join_path2_dup(const char *left, const char *right);

static char *driver_c_bridge_to_cstring(ChengStrBridge value) {
  size_t n = 0;
  char *out = NULL;
  if (value.len > 0) {
    n = (size_t)value.len;
  }
  out = (char *)malloc(n + 1u);
  if (out == NULL) return NULL;
  if (n > 0 && value.ptr != NULL) {
    memcpy(out, value.ptr, n);
  }
  out[n] = '\0';
  return out;
}

static DriverCProgTypeDecl *driver_c_prog_lookup_type_decl(DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *decl_name);
static int driver_c_prog_type_text_split_suffix_ctor(const char *type_text,
                                                     char **out_base,
                                                     char **out_arg);
static int driver_c_prog_type_text_split_ctor(const char *type_text,
                                              char **out_base,
                                              char **out_args_text);
static int driver_c_prog_type_text_split_fn(const char *type_text,
                                            char **out_params_text,
                                            char **out_return_text);
static char **driver_c_prog_split_type_args_dup(const char *args_text, int32_t *out_count);
static char *driver_c_prog_normalize_param_type_text_dup(const char *type_text);
static char *driver_c_prog_function_param_type_text_dup(const char *text);
static char *driver_c_prog_join_function_type_dup(char **param_types,
                                                  int32_t param_count,
                                                  const char *return_type);
static void driver_c_prog_free_string_array(char **values, int32_t count);
static DriverCProgValue driver_c_prog_materialize_slot(DriverCProgValue *slot);
static DriverCProgValue driver_c_prog_materialize(DriverCProgValue value);
static int32_t driver_c_prog_value_to_i32(DriverCProgValue value);
static int64_t driver_c_prog_value_to_i64(DriverCProgValue value);
static uint32_t driver_c_prog_value_to_u32(DriverCProgValue value);
static uint64_t driver_c_prog_value_to_u64(DriverCProgValue value);
static double driver_c_prog_value_to_f64(DriverCProgValue value);
static int driver_c_prog_value_truthy(DriverCProgValue value);
static char *driver_c_prog_normalize_visible_type_text_dup(DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *type_text,
                                                           int allow_type_param_passthrough);
static char *driver_c_prog_substitute_type_token_dup(const char *text,
                                                     const char *token,
                                                     const char *replacement);
static char *driver_c_prog_resolve_frame_type_text_dup(const DriverCProgFrame *frame,
                                                       const char *type_text);
static void driver_c_prog_array_ensure_cap(DriverCProgArray *array, int32_t need);
static DriverCProgZeroPlan *driver_c_prog_zero_plan_lookup(DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *inst_owner_module,
                                                           const char *type_text);
static void driver_c_prog_zero_plan_insert(DriverCProgRegistry *registry,
                                           const char *owner_module,
                                           const char *inst_owner_module,
                                           const char *type_text,
                                           DriverCProgZeroPlan *plan);
static DriverCProgZeroPlan *driver_c_prog_zero_plan_for_type(DriverCProgRegistry *registry,
                                                             const char *owner_module,
                                                             const char *inst_owner_module,
                                                             const char *type_text,
                                                             int depth);
static DriverCProgValue driver_c_prog_zero_value_from_plan(const DriverCProgZeroPlan *plan);
static DriverCProgValue driver_c_prog_zero_record_shell_from_plan(const DriverCProgZeroPlan *plan);
static DriverCProgValue driver_c_prog_zero_array_shell_from_plan(const DriverCProgZeroPlan *plan);

static int driver_c_prog_trace_any_enabled(void) {
  static int trace_config_init = 0;
  static int trace_enabled = 0;
  static const char *trace_item = NULL;
  if (!trace_config_init) {
    const char *flag = getenv("CHENG_DRIVER_TRACE");
    trace_enabled = !(flag == NULL || flag[0] == '\0' || strcmp(flag, "0") == 0);
    trace_item = getenv("CHENG_DRIVER_TRACE_ITEM");
    trace_config_init = 1;
  }
  return trace_enabled;
}

static int driver_c_prog_trace_item_enabled(const char *label) {
  static const char *trace_item = NULL;
  static int trace_item_init = 0;
  const char *cursor = NULL;
  const char *target = label != NULL ? label : "";
  if (!driver_c_prog_trace_any_enabled()) return 0;
  if (!trace_item_init) {
    trace_item = getenv("CHENG_DRIVER_TRACE_ITEM");
    trace_item_init = 1;
  }
  if (trace_item == NULL || trace_item[0] == '\0') return 1;
  cursor = trace_item;
  while (*cursor != '\0' && *target != '\0' && *cursor == *target) {
    cursor = cursor + 1;
    target = target + 1;
  }
  return *cursor == '\0' && *target == '\0';
}

static uint64_t driver_c_prog_hash_text(const char *text) {
  const unsigned char *cursor = (const unsigned char *)(text != NULL ? text : "");
  uint64_t hash = 1469598103934665603ULL;
  while (*cursor != '\0') {
    hash ^= (uint64_t)(*cursor);
    hash *= 1099511628211ULL;
    cursor = cursor + 1;
  }
  return hash;
}

static void *driver_c_prog_xcalloc(size_t count, size_t size);
static char *driver_c_dup_cstring(const char *text);
static int driver_c_prog_text_eq(const char *a, const char *b);

typedef struct DriverCProgInternedTextTable {
  int32_t count;
  int32_t cap;
  char **slots;
} DriverCProgInternedTextTable;

static DriverCProgInternedTextTable driver_c_prog_interned_texts = {0};

static void driver_c_prog_interned_text_table_grow(int32_t need_cap) {
  DriverCProgInternedTextTable next = {0};
  int32_t cap = 64;
  int32_t i = 0;
  if (need_cap < 64) {
    need_cap = 64;
  }
  while (cap < need_cap) {
    cap = cap * 2;
  }
  next.cap = cap;
  next.slots = (char **)driver_c_prog_xcalloc((size_t)cap, sizeof(char *));
  for (i = 0; i < driver_c_prog_interned_texts.cap; ++i) {
    char *text = driver_c_prog_interned_texts.slots != NULL ? driver_c_prog_interned_texts.slots[i] : NULL;
    if (text != NULL) {
      uint64_t hash = driver_c_prog_hash_text(text);
      int32_t slot = (int32_t)(hash & (uint64_t)(next.cap - 1));
      while (next.slots[slot] != NULL) {
        slot = (slot + 1) & (next.cap - 1);
      }
      next.slots[slot] = text;
      next.count = next.count + 1;
    }
  }
  free(driver_c_prog_interned_texts.slots);
  driver_c_prog_interned_texts = next;
}

static const char *driver_c_prog_intern_text(const char *text) {
  uint64_t hash = 0;
  int32_t slot = 0;
  const char *next_text = text != NULL ? text : "";
  if (next_text[0] == '\0') {
    return "";
  }
  if (driver_c_prog_interned_texts.cap <= 0 ||
      driver_c_prog_interned_texts.count * 10 >= driver_c_prog_interned_texts.cap * 7) {
    int32_t need_cap =
        driver_c_prog_interned_texts.cap > 0 ? driver_c_prog_interned_texts.cap * 2 : 64;
    driver_c_prog_interned_text_table_grow(need_cap);
  }
  hash = driver_c_prog_hash_text(next_text);
  slot = (int32_t)(hash & (uint64_t)(driver_c_prog_interned_texts.cap - 1));
  while (driver_c_prog_interned_texts.slots[slot] != NULL) {
    if (driver_c_prog_text_eq(driver_c_prog_interned_texts.slots[slot], next_text)) {
      return driver_c_prog_interned_texts.slots[slot];
    }
    slot = (slot + 1) & (driver_c_prog_interned_texts.cap - 1);
  }
  driver_c_prog_interned_texts.slots[slot] = driver_c_dup_cstring(next_text);
  driver_c_prog_interned_texts.count = driver_c_prog_interned_texts.count + 1;
  return driver_c_prog_interned_texts.slots[slot];
}

static uint64_t driver_c_prog_hash_visible_item_key(const char *owner_module,
                                                    const char *name) {
  uint64_t hash = driver_c_prog_hash_text(owner_module);
  hash ^= 0x9e3779b97f4a7c15ULL;
  hash *= 1099511628211ULL;
  hash ^= driver_c_prog_hash_text(name);
  return hash;
}

static uint64_t driver_c_prog_hash_visible_type_decl_key(const char *owner_module,
                                                         const char *type_text) {
  uint64_t hash = driver_c_prog_hash_text(owner_module);
  hash ^= 0x4f1bbcdcaa0b7d31ULL;
  hash *= 1099511628211ULL;
  hash ^= driver_c_prog_hash_text(type_text);
  return hash;
}

static uint64_t driver_c_prog_hash_item_key(const char *owner_module,
                                            const char *label) {
  return driver_c_prog_hash_visible_item_key(owner_module, label);
}

static uint64_t driver_c_prog_hash_item_kind_key(const char *owner_module,
                                                 const char *label,
                                                 const char *top_level_kind) {
  uint64_t hash = driver_c_prog_hash_item_key(owner_module, label);
  hash ^= 0x517cc1b727220a95ULL;
  hash *= 1099511628211ULL;
  hash ^= driver_c_prog_hash_text(top_level_kind);
  return hash;
}

static uint64_t driver_c_prog_hash_import_key(const char *owner_module,
                                              const char *visible_name) {
  uint64_t hash = driver_c_prog_hash_text(owner_module);
  hash ^= 0xc2b2ae3d27d4eb4fULL;
  hash *= 1099511628211ULL;
  hash ^= driver_c_prog_hash_text(visible_name);
  return hash;
}

static uint64_t driver_c_prog_hash_type_decl_key(const char *owner_module,
                                                 const char *decl_name) {
  uint64_t hash = driver_c_prog_hash_text(owner_module);
  hash ^= 0x94d049bb133111ebULL;
  hash *= 1099511628211ULL;
  hash ^= driver_c_prog_hash_text(decl_name);
  return hash;
}

static uint64_t driver_c_prog_hash_zero_plan_key(const char *owner_module,
                                                 const char *inst_owner_module,
                                                 const char *type_text) {
  uint64_t hash = driver_c_prog_hash_text(owner_module);
  hash ^= 0x62a9d9ed799705f5ULL;
  hash *= 1099511628211ULL;
  hash ^= driver_c_prog_hash_text(inst_owner_module);
  hash ^= 0x9e3779b97f4a7c15ULL;
  hash *= 1099511628211ULL;
  hash ^= driver_c_prog_hash_text(type_text);
  return hash;
}

static uint64_t driver_c_prog_hash_routine_type_args_key(int32_t item_id,
                                                         const char **type_arg_texts,
                                                         int32_t type_arg_count) {
  uint64_t hash = (uint64_t)(uint32_t)item_id;
  int32_t i = 0;
  hash ^= 0x6eed0e9da4d94a4fULL;
  hash *= 1099511628211ULL;
  hash ^= (uint64_t)(uint32_t)type_arg_count;
  for (i = 0; i < type_arg_count; ++i) {
    hash ^= 0x9e3779b97f4a7c15ULL;
    hash *= 1099511628211ULL;
    hash ^= driver_c_prog_hash_text(type_arg_texts != NULL ? type_arg_texts[i] : "");
  }
  return hash;
}

static void driver_c_prog_trace_op(DriverCProgFrame *frame, const char *phase) {
  if (frame == NULL || frame->item == NULL) return;
  if (!driver_c_prog_trace_item_enabled(frame->item->label)) return;
  fprintf(stderr,
          "driver_c trace: phase=%s item=%s module=%s pc=%d op=%s primary=%s secondary=%s arg0=%d arg1=%d arg2=%d stack_len=%d\n",
          phase != NULL ? phase : "",
          frame->item->label != NULL ? frame->item->label : "",
          frame->item->owner_module != NULL ? frame->item->owner_module : "",
          frame->current_pc,
          frame->current_op_kind != NULL ? frame->current_op_kind : "",
          frame->current_op_primary != NULL ? frame->current_op_primary : "",
          frame->current_op_secondary != NULL ? frame->current_op_secondary : "",
          frame->current_op != NULL ? frame->current_op->arg0 : -1,
          frame->current_op != NULL ? frame->current_op->arg1 : -1,
          frame->current_op != NULL ? frame->current_op->arg2 : -1,
          frame->stack_len);
}

static ChengStrBridge driver_c_owned_bridge_from_cstring(char *text) {
  ChengStrBridge out = {0};
  out.ptr = text;
  out.len = text != NULL ? (int32_t)strlen(text) : 0;
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_OWNED;
  return out;
}

static char *driver_c_dup_cstring(const char *text) {
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

static char *driver_c_dup_range(const char *start, const char *stop) {
  size_t n = 0;
  char *out = NULL;
  if (start == NULL || stop == NULL || stop < start) {
    return driver_c_dup_cstring("");
  }
  n = (size_t)(stop - start);
  out = (char *)malloc(n + 1u);
  if (out == NULL) return NULL;
  if (n > 0) {
    memcpy(out, start, n);
  }
  out[n] = '\0';
  return out;
}

static ChengStrBridge driver_c_prog_str_bridge_empty(void) {
  ChengStrBridge out = {0};
  out.ptr = NULL;
  out.len = 0;
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_NONE;
  return out;
}

static const char *driver_c_prog_str_ptr(ChengStrBridge value) {
  return value.ptr != NULL ? value.ptr : "";
}

static int32_t driver_c_prog_str_len(ChengStrBridge value) {
  return value.len > 0 ? value.len : 0;
}

static const char *driver_c_bridge_target_text(ChengStrBridge target) {
  return target.ptr != NULL ? target.ptr : "";
}

static void driver_c_require_machine_target_bridge(ChengStrBridge target) {
  const char *raw = driver_c_bridge_target_text(target);
  if (driver_c_str_eq_raw_bridge(target, "arm64-apple-darwin", 19)) {
    return;
  }
  fprintf(stderr, "v2 machine target: unsupported target %s\n", raw);
  abort();
}

static ChengStrBridge driver_c_machine_target_text_bridge(const char *text) {
  return driver_c_owned_bridge_from_cstring(driver_c_dup_cstring(text));
}

static char *driver_c_find_line_value_dup(const char *text, const char *key) {
  const char *cursor = NULL;
  size_t key_len = 0;
  if (text == NULL || key == NULL || key[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  key_len = strlen(key);
  cursor = text;
  while (*cursor != '\0') {
    const char *line_start = cursor;
    const char *line_stop = cursor;
    while (*line_stop != '\0' && *line_stop != '\n') {
      line_stop = line_stop + 1;
    }
    if ((size_t)(line_stop - line_start) > key_len &&
        memcmp(line_start, key, key_len) == 0 &&
        line_start[key_len] == '=') {
      return driver_c_dup_range(line_start + key_len + 1, line_stop);
    }
    cursor = *line_stop == '\0' ? line_stop : line_stop + 1;
  }
  return driver_c_dup_cstring("");
}

static void *driver_c_prog_xmalloc(size_t size) {
  void *out = NULL;
  if (size == 0u) {
    size = 1u;
  }
  out = malloc(size);
  if (out == NULL) {
    driver_c_die("driver_c program runtime: alloc failed");
  }
  return out;
}

static void *driver_c_prog_xcalloc(size_t count, size_t size) {
  void *out = NULL;
  if (count == 0u) {
    count = 1u;
  }
  if (size == 0u) {
    size = 1u;
  }
  out = calloc(count, size);
  if (out == NULL) {
    driver_c_die("driver_c program runtime: calloc failed");
  }
  return out;
}

static void *driver_c_prog_xrealloc(void *ptr, size_t size) {
  void *out = NULL;
  if (size == 0u) {
    size = 1u;
  }
  out = realloc(ptr, size);
  if (out == NULL) {
    driver_c_die("driver_c program runtime: realloc failed");
  }
  return out;
}

static void *driver_c_prog_seq_set_grow_local(void *seq_ptr, int32_t idx, int32_t elem_size) {
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  int32_t actual_elem_size = elem_size > 0 ? elem_size : 1;
  int32_t old_len = 0;
  int32_t old_cap = 0;
  if (seq == NULL || idx < 0) {
    return NULL;
  }
  old_len = seq->len;
  old_cap = seq->cap;
  if (idx >= seq->cap) {
    int32_t new_cap = seq->cap > 0 ? seq->cap : 4;
    while (new_cap <= idx) {
      int32_t doubled = new_cap * 2;
      if (doubled <= new_cap) {
        new_cap = idx + 1;
        break;
      }
      new_cap = doubled;
    }
    seq->buffer = driver_c_prog_xrealloc(seq->buffer, (size_t)new_cap * (size_t)actual_elem_size);
    if (new_cap > old_cap) {
      memset((char *)seq->buffer + ((size_t)old_cap * (size_t)actual_elem_size),
             0,
             ((size_t)(new_cap - old_cap) * (size_t)actual_elem_size));
    }
    seq->cap = new_cap;
  }
  if (idx >= seq->len) {
    memset((char *)seq->buffer + ((size_t)old_len * (size_t)actual_elem_size),
           0,
           ((size_t)(idx + 1 - old_len) * (size_t)actual_elem_size));
    seq->len = idx + 1;
  }
  return (char *)seq->buffer + ((size_t)idx * (size_t)actual_elem_size);
}

static void driver_c_prog_seq_grow_inst_local(void *seq_ptr, int32_t min_cap, int32_t elem_size) {
  ChengSeqHeader *seq = (ChengSeqHeader *)seq_ptr;
  int32_t actual_elem_size = elem_size > 0 ? elem_size : 1;
  int32_t old_cap = 0;
  if (seq == NULL || min_cap <= 0) {
    return;
  }
  old_cap = seq->cap;
  if (seq->buffer == NULL || min_cap > seq->cap) {
    int32_t new_cap = seq->cap > 0 ? seq->cap : 4;
    while (new_cap < min_cap) {
      int32_t doubled = new_cap * 2;
      if (doubled <= new_cap) {
        new_cap = min_cap;
        break;
      }
      new_cap = doubled;
    }
    seq->buffer = driver_c_prog_xrealloc(seq->buffer, (size_t)new_cap * (size_t)actual_elem_size);
    if (new_cap > old_cap) {
      memset((char *)seq->buffer + ((size_t)old_cap * (size_t)actual_elem_size),
             0,
             ((size_t)(new_cap - old_cap) * (size_t)actual_elem_size));
    }
    seq->cap = new_cap;
  }
}

static void driver_c_prog_seq_zero_tail_raw_local(void *buffer,
                                                  int32_t seq_cap,
                                                  int32_t len,
                                                  int32_t target,
                                                  int32_t elem_size) {
  int32_t actual_elem_size = elem_size > 0 ? elem_size : 1;
  int32_t from_bytes = 0;
  int32_t to_bytes = 0;
  (void)seq_cap;
  if (buffer == NULL) {
    return;
  }
  if (target <= len) {
    return;
  }
  from_bytes = len * actual_elem_size;
  to_bytes = target * actual_elem_size;
  if (to_bytes <= from_bytes) {
    return;
  }
  memset((uint8_t *)buffer + from_bytes, 0, (size_t)(to_bytes - from_bytes));
}

static int32_t driver_c_prog_parse_i32_or_zero(const char *text) {
  long value = 0;
  char *end = NULL;
  if (text == NULL || text[0] == '\0') {
    return 0;
  }
  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || end == text) {
    return 0;
  }
  return (int32_t)value;
}

static int64_t driver_c_prog_parse_i64_or_zero(const char *text) {
  long long value = 0;
  char *end = NULL;
  if (text == NULL || text[0] == '\0') {
    return 0;
  }
  errno = 0;
  value = strtoll(text, &end, 10);
  if (errno != 0 || end == text) {
    return 0;
  }
  return (int64_t)value;
}

static int driver_c_prog_hex_nibble(char c) {
  if (c >= '0' && c <= '9') return (int)(c - '0');
  if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
  if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
  return -1;
}

static char *driver_c_prog_hex_decode_text_dup(const char *hex) {
  size_t hex_len = 0;
  size_t out_len = 0;
  char *out = NULL;
  size_t i = 0;
  if (hex == NULL || hex[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  hex_len = strlen(hex);
  if ((hex_len & 1u) != 0u) {
    driver_c_die("driver_c program runtime: odd hex text");
  }
  out_len = hex_len / 2u;
  out = (char *)driver_c_prog_xmalloc(out_len + 1u);
  for (i = 0; i < out_len; ++i) {
    int hi = driver_c_prog_hex_nibble(hex[i * 2u]);
    int lo = driver_c_prog_hex_nibble(hex[i * 2u + 1u]);
    if (hi < 0 || lo < 0) {
      driver_c_die("driver_c program runtime: invalid hex text");
    }
    out[i] = (char)((hi << 4) | lo);
  }
  out[out_len] = '\0';
  return out;
}

static char *driver_c_prog_trim_dup(const char *text) {
  const char *start = text;
  const char *stop = NULL;
  if (text == NULL) {
    return driver_c_dup_cstring("");
  }
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
    start = start + 1;
  }
  stop = start + strlen(start);
  while (stop > start &&
         (stop[-1] == ' ' || stop[-1] == '\t' || stop[-1] == '\r' || stop[-1] == '\n')) {
    stop = stop - 1;
  }
  return driver_c_dup_range(start, stop);
}

static int driver_c_prog_strip_char_mask_has(uint64_t mask, unsigned char ch) {
  if (ch >= 64u) {
    return 0;
  }
  return ((mask >> ch) & 1u) != 0u;
}

static char *driver_c_prog_trim_chars_dup(const char *text, uint64_t mask) {
  const char *start = text;
  const char *stop = NULL;
  if (text == NULL) {
    return driver_c_dup_cstring("");
  }
  while (*start != '\0' && driver_c_prog_strip_char_mask_has(mask, (unsigned char)*start)) {
    start = start + 1;
  }
  stop = start + strlen(start);
  while (stop > start && driver_c_prog_strip_char_mask_has(mask, (unsigned char)stop[-1])) {
    stop = stop - 1;
  }
  return driver_c_dup_range(start, stop);
}

static char *driver_c_prog_parse_string_literal_dup(const char *text) {
  char *trimmed = driver_c_prog_trim_dup(text);
  size_t n = strlen(trimmed);
  size_t cap = n > 0 ? n + 1u : 2u;
  size_t len = 0;
  char *out = NULL;
  const char *p = NULL;
  if (n < 2u || trimmed[0] != '"' || trimmed[n - 1u] != '"') {
    free(trimmed);
    return NULL;
  }
  out = (char *)driver_c_prog_xcalloc(cap, 1u);
  p = trimmed + 1;
  while (*p != '\0' && *p != '"') {
    unsigned char ch = (unsigned char)*p++;
    if (ch == '\\' && *p != '\0') {
      unsigned char esc = (unsigned char)*p++;
      if (esc == 'n') ch = '\n';
      else if (esc == 'r') ch = '\r';
      else if (esc == 't') ch = '\t';
      else if (esc == '0') ch = '\0';
      else ch = esc;
    }
    if (len + 2u > cap) {
      cap *= 2u;
      out = (char *)driver_c_prog_xrealloc(out, cap);
    }
    out[len++] = (char)ch;
    out[len] = '\0';
  }
  if (*p != '"') {
    free(trimmed);
    free(out);
    return NULL;
  }
  p = p + 1;
  while (*p != '\0') {
    if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
      free(trimmed);
      free(out);
      return NULL;
    }
    p = p + 1;
  }
  free(trimmed);
  return out;
}

static int driver_c_prog_text_eq(const char *a, const char *b) {
  const unsigned char *lhs = NULL;
  const unsigned char *rhs = NULL;
  if (a == NULL) a = "";
  if (b == NULL) b = "";
  if (a == b) return 1;
  lhs = (const unsigned char *)a;
  rhs = (const unsigned char *)b;
  while (*lhs != '\0' && *rhs != '\0' && *lhs == *rhs) {
    lhs = lhs + 1;
    rhs = rhs + 1;
  }
  return *lhs == *rhs;
}

static int32_t driver_c_prog_top_level_tag_from_text(const char *text) {
  if (driver_c_prog_text_eq(text, "import")) {
    return DRIVER_C_PROG_TOP_IMPORT;
  }
  if (driver_c_prog_text_eq(text, "type")) {
    return DRIVER_C_PROG_TOP_TYPE;
  }
  if (driver_c_prog_text_eq(text, "var")) {
    return DRIVER_C_PROG_TOP_VAR;
  }
  if (driver_c_prog_text_eq(text, "const")) {
    return DRIVER_C_PROG_TOP_CONST;
  }
  if (driver_c_prog_text_eq(text, "fn")) {
    return DRIVER_C_PROG_TOP_FN;
  }
  if (driver_c_prog_text_eq(text, "iterator")) {
    return DRIVER_C_PROG_TOP_ITERATOR;
  }
  if (driver_c_prog_text_eq(text, "importc_fn")) {
    return DRIVER_C_PROG_TOP_IMPORTC_FN;
  }
  if (driver_c_prog_text_eq(text, "builtin")) {
    return DRIVER_C_PROG_TOP_BUILTIN;
  }
  return DRIVER_C_PROG_TOP_UNKNOWN;
}

static int driver_c_prog_top_level_tag_is_routine(int32_t tag) {
  return tag == DRIVER_C_PROG_TOP_FN ||
         tag == DRIVER_C_PROG_TOP_ITERATOR ||
         tag == DRIVER_C_PROG_TOP_IMPORTC_FN;
}

static int32_t driver_c_prog_builtin_tag_from_text(const char *text) {
  if (driver_c_prog_text_eq(text, "paramCount") || driver_c_prog_text_eq(text, "argCount")) {
    return DRIVER_C_PROG_BUILTIN_PARAM_COUNT;
  }
  if (driver_c_prog_text_eq(text, "paramStr") || driver_c_prog_text_eq(text, "argStr")) {
    return DRIVER_C_PROG_BUILTIN_PARAM_STR;
  }
  if (driver_c_prog_text_eq(text, "readFlagValue")) return DRIVER_C_PROG_BUILTIN_READ_FLAG_VALUE;
  if (driver_c_prog_text_eq(text, "parseInt32")) return DRIVER_C_PROG_BUILTIN_PARSE_INT32;
  if (driver_c_prog_text_eq(text, "parseInt64")) return DRIVER_C_PROG_BUILTIN_PARSE_INT64;
  if (driver_c_prog_text_eq(text, "intToStr")) return DRIVER_C_PROG_BUILTIN_INT_TO_STR;
  if (driver_c_prog_text_eq(text, "uint64ToStr")) return DRIVER_C_PROG_BUILTIN_UINT64_TO_STR;
  if (driver_c_prog_text_eq(text, "int64ToStr")) return DRIVER_C_PROG_BUILTIN_INT64_TO_STR;
  if (driver_c_prog_text_eq(text, "len")) return DRIVER_C_PROG_BUILTIN_LEN;
  if (driver_c_prog_text_eq(text, "sliceBytes")) return DRIVER_C_PROG_BUILTIN_SLICE_BYTES;
  if (driver_c_prog_text_eq(text, "find")) return DRIVER_C_PROG_BUILTIN_FIND;
  if (driver_c_prog_text_eq(text, "startsWith") || driver_c_prog_text_eq(text, "hasPrefix")) {
    return DRIVER_C_PROG_BUILTIN_STARTS_WITH;
  }
  if (driver_c_prog_text_eq(text, "strContains")) return DRIVER_C_PROG_BUILTIN_STR_CONTAINS;
  if (driver_c_prog_text_eq(text, "strip")) return DRIVER_C_PROG_BUILTIN_STRIP;
  if (driver_c_prog_text_eq(text, "split")) return DRIVER_C_PROG_BUILTIN_SPLIT;
  if (driver_c_prog_text_eq(text, "charToStr")) return DRIVER_C_PROG_BUILTIN_CHAR_TO_STR;
  if (driver_c_prog_text_eq(text, "parentDir")) return DRIVER_C_PROG_BUILTIN_PARENT_DIR;
  if (driver_c_prog_text_eq(text, "absolutePath")) return DRIVER_C_PROG_BUILTIN_ABSOLUTE_PATH;
  if (driver_c_prog_text_eq(text, "joinPath")) return DRIVER_C_PROG_BUILTIN_JOIN_PATH;
  if (driver_c_prog_text_eq(text, "fileExists")) return DRIVER_C_PROG_BUILTIN_FILE_EXISTS;
  if (driver_c_prog_text_eq(text, "dirExists")) return DRIVER_C_PROG_BUILTIN_DIR_EXISTS;
  if (driver_c_prog_text_eq(text, "createDir")) return DRIVER_C_PROG_BUILTIN_CREATE_DIR;
  if (driver_c_prog_text_eq(text, "readFile")) return DRIVER_C_PROG_BUILTIN_READ_FILE;
  if (driver_c_prog_text_eq(text, "writeFile")) return DRIVER_C_PROG_BUILTIN_WRITE_FILE;
  if (driver_c_prog_text_eq(text, "chainAppendLine")) return DRIVER_C_PROG_BUILTIN_CHAIN_APPEND_LINE;
  if (driver_c_prog_text_eq(text, "echo")) return DRIVER_C_PROG_BUILTIN_ECHO;
  if (driver_c_prog_text_eq(text, "emptyBytes")) return DRIVER_C_PROG_BUILTIN_EMPTY_BYTES;
  if (driver_c_prog_text_eq(text, "bytesAlloc")) return DRIVER_C_PROG_BUILTIN_BYTES_ALLOC;
  if (driver_c_prog_text_eq(text, "bytesLen")) return DRIVER_C_PROG_BUILTIN_BYTES_LEN;
  if (driver_c_prog_text_eq(text, "bytesFromString")) return DRIVER_C_PROG_BUILTIN_BYTES_FROM_STRING;
  if (driver_c_prog_text_eq(text, "bytesToString")) return DRIVER_C_PROG_BUILTIN_BYTES_TO_STRING;
  if (driver_c_prog_text_eq(text, "bytesGet")) return DRIVER_C_PROG_BUILTIN_BYTES_GET;
  if (driver_c_prog_text_eq(text, "bytesSet")) return DRIVER_C_PROG_BUILTIN_BYTES_SET;
  if (driver_c_prog_text_eq(text, "bytesSlice")) return DRIVER_C_PROG_BUILTIN_BYTES_SLICE;
  if (driver_c_prog_text_eq(text, "bytesToHex")) return DRIVER_C_PROG_BUILTIN_BYTES_TO_HEX;
  if (driver_c_prog_text_eq(text, "sha256Digest")) return DRIVER_C_PROG_BUILTIN_SHA256_DIGEST;
  if (driver_c_prog_text_eq(text, "k256")) return DRIVER_C_PROG_BUILTIN_K256;
  if (driver_c_prog_text_eq(text, "k512")) return DRIVER_C_PROG_BUILTIN_K512;
  if (driver_c_prog_text_eq(text, "alloc")) return DRIVER_C_PROG_BUILTIN_ALLOC;
  if (driver_c_prog_text_eq(text, "dealloc")) return DRIVER_C_PROG_BUILTIN_DEALLOC;
  if (driver_c_prog_text_eq(text, "realloc")) return DRIVER_C_PROG_BUILTIN_REALLOC;
  if (driver_c_prog_text_eq(text, "setLen")) return DRIVER_C_PROG_BUILTIN_SET_LEN;
  if (driver_c_prog_text_eq(text, "reserve")) return DRIVER_C_PROG_BUILTIN_RESERVE;
  if (driver_c_prog_text_eq(text, "add")) return DRIVER_C_PROG_BUILTIN_ADD;
  if (driver_c_prog_text_eq(text, "sizeof")) return DRIVER_C_PROG_BUILTIN_SIZEOF;
  if (driver_c_prog_text_eq(text, "initMsQuicTransport")) return DRIVER_C_PROG_BUILTIN_INIT_MSQUIC_TRANSPORT;
  if (driver_c_prog_text_eq(text, "cmdlineParamStrCopyIntoBridge")) {
    return DRIVER_C_PROG_BUILTIN_CMDLINE_PARAM_STR_COPY_INTO_BRIDGE;
  }
  if (driver_c_prog_text_eq(text, "intToStrRaw")) return DRIVER_C_PROG_BUILTIN_INT_TO_STR_RAW;
  if (driver_c_prog_text_eq(text, "uint64ToStrRaw")) return DRIVER_C_PROG_BUILTIN_UINT64_TO_STR_RAW;
  if (driver_c_prog_text_eq(text, "int64ToStrRaw")) return DRIVER_C_PROG_BUILTIN_INT64_TO_STR_RAW;
  return DRIVER_C_PROG_BUILTIN_NONE;
}

static int32_t driver_c_prog_builtin_tag_for_callable_item(const DriverCProgItem *item) {
  if (item == NULL || item->label == NULL) {
    return DRIVER_C_PROG_BUILTIN_NONE;
  }
  if (item->top_level_tag == DRIVER_C_PROG_TOP_BUILTIN ||
      item->top_level_tag == DRIVER_C_PROG_TOP_IMPORTC_FN) {
    return driver_c_prog_builtin_tag_from_text(item->label);
  }
  if (item->top_level_tag == DRIVER_C_PROG_TOP_FN && item->owner_module != NULL) {
    if (driver_c_prog_text_eq(item->owner_module, "std/crypto/sha256") &&
        driver_c_prog_text_eq(item->label, "k256")) {
      return DRIVER_C_PROG_BUILTIN_K256;
    }
    if (driver_c_prog_text_eq(item->owner_module, "std/crypto/sha512") &&
        driver_c_prog_text_eq(item->label, "k512")) {
      return DRIVER_C_PROG_BUILTIN_K512;
    }
  }
  return DRIVER_C_PROG_BUILTIN_NONE;
}

static int32_t driver_c_prog_op_tag_from_text(const char *text) {
  if (driver_c_prog_text_eq(text, "local_decl")) return DRIVER_C_PROG_OP_LOCAL_DECL;
  if (driver_c_prog_text_eq(text, "load_bool")) return DRIVER_C_PROG_OP_LOAD_BOOL;
  if (driver_c_prog_text_eq(text, "load_nil")) return DRIVER_C_PROG_OP_LOAD_NIL;
  if (driver_c_prog_text_eq(text, "load_str")) return DRIVER_C_PROG_OP_LOAD_STR;
  if (driver_c_prog_text_eq(text, "load_int32")) return DRIVER_C_PROG_OP_LOAD_INT32;
  if (driver_c_prog_text_eq(text, "load_int64")) return DRIVER_C_PROG_OP_LOAD_INT64;
  if (driver_c_prog_text_eq(text, "load_float64")) return DRIVER_C_PROG_OP_LOAD_FLOAT64;
  if (driver_c_prog_text_eq(text, "load_param")) return DRIVER_C_PROG_OP_LOAD_PARAM;
  if (driver_c_prog_text_eq(text, "load_local")) return DRIVER_C_PROG_OP_LOAD_LOCAL;
  if (driver_c_prog_text_eq(text, "addr_of_param")) return DRIVER_C_PROG_OP_ADDR_OF_PARAM;
  if (driver_c_prog_text_eq(text, "addr_of_local")) return DRIVER_C_PROG_OP_ADDR_OF_LOCAL;
  if (driver_c_prog_text_eq(text, "addr_of_name")) return DRIVER_C_PROG_OP_ADDR_OF_NAME;
  if (driver_c_prog_text_eq(text, "addr_of_field")) return DRIVER_C_PROG_OP_ADDR_OF_FIELD;
  if (driver_c_prog_text_eq(text, "addr_of_index")) return DRIVER_C_PROG_OP_ADDR_OF_INDEX;
  if (driver_c_prog_text_eq(text, "load_import")) return DRIVER_C_PROG_OP_LOAD_IMPORT;
  if (driver_c_prog_text_eq(text, "load_routine")) return DRIVER_C_PROG_OP_LOAD_ROUTINE;
  if (driver_c_prog_text_eq(text, "load_global")) return DRIVER_C_PROG_OP_LOAD_GLOBAL;
  if (driver_c_prog_text_eq(text, "load_name")) return DRIVER_C_PROG_OP_LOAD_NAME;
  if (driver_c_prog_text_eq(text, "load_field")) return DRIVER_C_PROG_OP_LOAD_FIELD;
  if (driver_c_prog_text_eq(text, "load_index")) return DRIVER_C_PROG_OP_LOAD_INDEX;
  if (driver_c_prog_text_eq(text, "call")) return DRIVER_C_PROG_OP_CALL;
  if (driver_c_prog_text_eq(text, "binary_op")) return DRIVER_C_PROG_OP_BINARY_OP;
  if (driver_c_prog_text_eq(text, "unary_not")) return DRIVER_C_PROG_OP_UNARY_NOT;
  if (driver_c_prog_text_eq(text, "unary_neg")) return DRIVER_C_PROG_OP_UNARY_NEG;
  if (driver_c_prog_text_eq(text, "make_array")) return DRIVER_C_PROG_OP_MAKE_ARRAY;
  if (driver_c_prog_text_eq(text, "make_tuple")) return DRIVER_C_PROG_OP_MAKE_TUPLE;
  if (driver_c_prog_text_eq(text, "make_record")) return DRIVER_C_PROG_OP_MAKE_RECORD;
  if (driver_c_prog_text_eq(text, "materialize")) return DRIVER_C_PROG_OP_MATERIALIZE;
  if (driver_c_prog_text_eq(text, "jump_if_false")) return DRIVER_C_PROG_OP_JUMP_IF_FALSE;
  if (driver_c_prog_text_eq(text, "jump")) return DRIVER_C_PROG_OP_JUMP;
  if (driver_c_prog_text_eq(text, "label")) return DRIVER_C_PROG_OP_LABEL;
  if (driver_c_prog_text_eq(text, "phi_merge")) return DRIVER_C_PROG_OP_PHI_MERGE;
  if (driver_c_prog_text_eq(text, "return_value")) return DRIVER_C_PROG_OP_RETURN_VALUE;
  if (driver_c_prog_text_eq(text, "return_void")) return DRIVER_C_PROG_OP_RETURN_VOID;
  if (driver_c_prog_text_eq(text, "store_local")) return DRIVER_C_PROG_OP_STORE_LOCAL;
  if (driver_c_prog_text_eq(text, "store_param")) return DRIVER_C_PROG_OP_STORE_PARAM;
  if (driver_c_prog_text_eq(text, "store_name")) return DRIVER_C_PROG_OP_STORE_NAME;
  if (driver_c_prog_text_eq(text, "store_local_field")) return DRIVER_C_PROG_OP_STORE_LOCAL_FIELD;
  if (driver_c_prog_text_eq(text, "store_param_field")) return DRIVER_C_PROG_OP_STORE_PARAM_FIELD;
  if (driver_c_prog_text_eq(text, "store_field")) return DRIVER_C_PROG_OP_STORE_FIELD;
  if (driver_c_prog_text_eq(text, "store_index")) return DRIVER_C_PROG_OP_STORE_INDEX;
  if (driver_c_prog_text_eq(text, "drop_value")) return DRIVER_C_PROG_OP_DROP_VALUE;
  if (driver_c_prog_text_eq(text, "for_range_init")) return DRIVER_C_PROG_OP_FOR_RANGE_INIT;
  if (driver_c_prog_text_eq(text, "for_range_check")) return DRIVER_C_PROG_OP_FOR_RANGE_CHECK;
  if (driver_c_prog_text_eq(text, "for_range_step")) return DRIVER_C_PROG_OP_FOR_RANGE_STEP;
  if (driver_c_prog_text_eq(text, "cast_int32")) return DRIVER_C_PROG_OP_CAST_INT32;
  if (driver_c_prog_text_eq(text, "cast_int64")) return DRIVER_C_PROG_OP_CAST_INT64;
  if (driver_c_prog_text_eq(text, "cast_type")) return DRIVER_C_PROG_OP_CAST_TYPE;
  if (driver_c_prog_text_eq(text, "new_ref")) return DRIVER_C_PROG_OP_NEW_REF;
  return DRIVER_C_PROG_OP_UNKNOWN;
}

static int driver_c_prog_text_starts_with(const char *text, const char *prefix) {
  size_t n = 0;
  if (text == NULL || prefix == NULL) return 0;
  n = strlen(prefix);
  return strncmp(text, prefix, n) == 0 ? 1 : 0;
}

static char *driver_c_prog_module_basename_dup(const char *module_path) {
  const char *last = NULL;
  if (module_path == NULL || module_path[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  last = strrchr(module_path, '/');
  if (last == NULL) {
    return driver_c_dup_cstring(module_path);
  }
  return driver_c_dup_cstring(last + 1);
}

static const char *driver_c_prog_import_target_module_text(const DriverCProgItem *item) {
  if (item == NULL) {
    return "";
  }
  if (item->import_target_module != NULL && item->import_target_module[0] != '\0') {
    return item->import_target_module;
  }
  if (item->import_path != NULL && item->import_path[0] != '\0') {
    return item->import_path;
  }
  if (item->label != NULL && item->label[0] != '\0') {
    return item->label;
  }
  return "";
}

static DriverCProgRegistry driver_c_prog_registry_singleton;

static const char *driver_c_prog_registry_symbol_text(void) {
  static const char *cached = NULL;
  void *sym = NULL;
  if (cached != NULL) {
    return cached;
  }
  sym = dlsym(RTLD_DEFAULT, "cheng_v2_compiler_core_program_registry");
  if (sym == NULL) {
    sym = dlsym(RTLD_DEFAULT, "_cheng_v2_compiler_core_program_registry");
  }
  if (sym == NULL) {
    driver_c_die("driver_c program runtime: compiler_core program registry symbol missing");
  }
  cached = (const char *)sym;
  return cached;
}

static DriverCProgValue driver_c_prog_value_nil(void) {
  DriverCProgValue out;
  memset(&out, 0, sizeof(out));
  out.kind = DRIVER_C_PROG_VALUE_NIL;
  return out;
}

static DriverCProgValue driver_c_prog_value_bool(int32_t value) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_BOOL;
  out.as.b = value != 0 ? 1 : 0;
  return out;
}

static DriverCProgValue driver_c_prog_value_i32(int32_t value) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_I32;
  out.as.i32 = value;
  return out;
}

static DriverCProgValue driver_c_prog_value_i64(int64_t value) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_I64;
  out.as.i64 = value;
  return out;
}

static DriverCProgValue driver_c_prog_value_u32(uint32_t value) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_U32;
  out.as.u32 = value;
  return out;
}

static DriverCProgValue driver_c_prog_value_u64(uint64_t value) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_U64;
  out.as.u64 = value;
  return out;
}

static DriverCProgValue driver_c_prog_value_f64(double value) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_F64;
  out.as.f64 = value;
  return out;
}

static DriverCProgValue driver_c_prog_value_str_take(char *text) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_STR;
  out.as.str = driver_c_owned_bridge_from_cstring(text != NULL ? text : driver_c_dup_cstring(""));
  return out;
}

static DriverCProgValue driver_c_prog_value_str_dup(const char *text) {
  return driver_c_prog_value_str_take(driver_c_dup_cstring(text));
}

static DriverCProgValue driver_c_prog_value_str_empty(void) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_STR;
  out.as.str = driver_c_prog_str_bridge_empty();
  return out;
}

static DriverCProgValue driver_c_prog_value_ptr(void *ptr) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_PTR;
  out.as.ptr = ptr;
  return out;
}

static DriverCProgValue driver_c_prog_value_bytes_dup(const uint8_t *data, int32_t len) {
  DriverCProgValue out = driver_c_prog_value_nil();
  uint8_t *dup = NULL;
  if (len < 0) {
    len = 0;
  }
  if (len > 0) {
    dup = (uint8_t *)driver_c_prog_xmalloc((size_t)len);
    memcpy(dup, data, (size_t)len);
  }
  out.kind = DRIVER_C_PROG_VALUE_BYTES;
  out.as.bytes.data = dup;
  out.as.bytes.len = len;
  return out;
}

static DriverCProgValue driver_c_prog_value_bytes_empty(void) {
  return driver_c_prog_value_bytes_dup(NULL, 0);
}

static DriverCProgArray *driver_c_prog_registered_arrays = NULL;

static DriverCProgArray *driver_c_prog_array_new_with_elem(const char *elem_type_text) {
  DriverCProgArray *out = (DriverCProgArray *)driver_c_prog_xcalloc(1u, sizeof(*out));
  out->elem_type_text = driver_c_prog_intern_text(elem_type_text);
  out->items = out->inline_items;
  out->cap = DRIVER_C_PROG_ARRAY_INLINE_CAP;
  out->next_registered = driver_c_prog_registered_arrays;
  driver_c_prog_registered_arrays = out;
  return out;
}

static DriverCProgArray *driver_c_prog_array_new(void) {
  return driver_c_prog_array_new_with_elem("");
}

static DriverCProgArray *driver_c_prog_array_new_with_elem_and_len_uninit(const char *elem_type_text,
                                                                          int32_t len) {
  DriverCProgArray *out = driver_c_prog_array_new_with_elem(elem_type_text);
  if (len < 0) {
    len = 0;
  }
  driver_c_prog_array_ensure_cap(out, len);
  out->len = len;
  return out;
}

static void driver_c_prog_array_set_elem_type(DriverCProgArray *array, const char *elem_type_text) {
  if (array == NULL) return;
  array->elem_type_text = driver_c_prog_intern_text(elem_type_text);
}

static DriverCProgArray *driver_c_prog_array_from_buffer_ptr(void *buffer) {
  DriverCProgArray *cursor = driver_c_prog_registered_arrays;
  if (buffer == NULL) return NULL;
  while (cursor != NULL) {
    if ((void *)cursor->items == buffer) {
      return cursor;
    }
    cursor = cursor->next_registered;
  }
  return NULL;
}

static DriverCProgRecord *driver_c_prog_record_new_with_type(const char *type_text) {
  DriverCProgRecord *out = (DriverCProgRecord *)driver_c_prog_xcalloc(1u, sizeof(*out));
  int32_t i = 0;
  out->type_text = driver_c_prog_intern_text(type_text);
  out->names = out->inline_names;
  out->values = out->inline_values;
  out->lookup_indices = out->inline_lookup_indices;
  out->cap = DRIVER_C_PROG_RECORD_INLINE_CAP;
  out->lookup_cap = DRIVER_C_PROG_RECORD_LOOKUP_INLINE_CAP;
  for (i = 0; i < out->lookup_cap; ++i) {
    out->lookup_indices[i] = -1;
  }
  return out;
}

static DriverCProgRecord *driver_c_prog_record_new(void) {
  return driver_c_prog_record_new_with_type("");
}

static void driver_c_prog_record_set_type(DriverCProgRecord *record, const char *type_text) {
  if (record == NULL) return;
  record->type_text = driver_c_prog_intern_text(type_text);
}

static void driver_c_prog_field_lookup_reset(int32_t *lookup_indices,
                                             int32_t lookup_cap);

static void driver_c_prog_record_detach_names(DriverCProgRecord *record, int32_t need_cap) {
  char **next_names = NULL;
  int32_t cap = 0;
  if (record == NULL || !record->names_shared) {
    return;
  }
  cap = record->cap > 0 ? record->cap : DRIVER_C_PROG_RECORD_INLINE_CAP;
  if (need_cap > cap) {
    cap = need_cap;
  }
  next_names = (char **)driver_c_prog_xcalloc((size_t)cap, sizeof(char *));
  if (record->names != NULL && record->len > 0) {
    memcpy(next_names, record->names, sizeof(char *) * (size_t)record->len);
  }
  record->names = next_names;
  record->names_shared = 0;
}

static void driver_c_prog_record_detach_lookup(DriverCProgRecord *record,
                                               int32_t need_cap,
                                               int copy_existing) {
  int32_t cap = 0;
  int32_t *next_lookup = NULL;
  if (record == NULL || !record->lookup_shared) {
    return;
  }
  cap = record->lookup_cap > 0 ? record->lookup_cap : DRIVER_C_PROG_RECORD_LOOKUP_INLINE_CAP;
  if (need_cap > cap) {
    cap = need_cap;
  }
  if (cap <= DRIVER_C_PROG_RECORD_LOOKUP_INLINE_CAP) {
    next_lookup = record->inline_lookup_indices;
  } else {
    next_lookup = (int32_t *)driver_c_prog_xcalloc((size_t)cap, sizeof(int32_t));
  }
  driver_c_prog_field_lookup_reset(next_lookup, cap);
  if (copy_existing && record->lookup_indices != NULL && record->lookup_cap > 0) {
    memcpy(next_lookup, record->lookup_indices, sizeof(int32_t) * (size_t)record->lookup_cap);
  }
  record->lookup_indices = next_lookup;
  record->lookup_cap = cap;
  record->lookup_shared = 0;
}

static void driver_c_prog_record_set_shared_names(DriverCProgRecord *record, char **shared_names) {
  if (record == NULL) {
    return;
  }
  if (!record->names_shared &&
      record->names != NULL &&
      record->names != record->inline_names) {
    free(record->names);
  }
  record->names = shared_names;
  record->names_shared = 1;
}

static void driver_c_prog_record_set_shared_lookup(DriverCProgRecord *record,
                                                   int32_t *shared_lookup,
                                                   int32_t shared_lookup_cap) {
  if (record == NULL) {
    return;
  }
  if (!record->lookup_shared &&
      record->lookup_indices != NULL &&
      record->lookup_indices != record->inline_lookup_indices) {
    free(record->lookup_indices);
  }
  record->lookup_indices = shared_lookup;
  record->lookup_cap = shared_lookup_cap;
  record->lookup_shared = 1;
}

static int32_t driver_c_prog_record_lookup_cap_for_count(int32_t count) {
  int32_t cap = DRIVER_C_PROG_RECORD_LOOKUP_INLINE_CAP;
  if (count < 0) {
    count = 0;
  }
  while (cap < count * 2) {
    cap = cap * 2;
  }
  return cap;
}

static void driver_c_prog_record_lookup_reset(DriverCProgRecord *record, int32_t field_count) {
  int32_t need_cap = 0;
  int32_t i = 0;
  if (record == NULL) {
    return;
  }
  need_cap = driver_c_prog_record_lookup_cap_for_count(field_count);
  if (record->lookup_shared) {
    driver_c_prog_record_detach_lookup(record, need_cap, 0);
  }
  if (record->lookup_indices == NULL) {
    record->lookup_indices = record->inline_lookup_indices;
    record->lookup_cap = DRIVER_C_PROG_RECORD_LOOKUP_INLINE_CAP;
  }
  if (record->lookup_cap < need_cap) {
    if (record->lookup_indices == record->inline_lookup_indices) {
      record->lookup_indices =
          (int32_t *)driver_c_prog_xcalloc((size_t)need_cap, sizeof(int32_t));
    } else {
      record->lookup_indices =
          (int32_t *)driver_c_prog_xrealloc(record->lookup_indices,
                                            sizeof(int32_t) * (size_t)need_cap);
    }
    record->lookup_cap = need_cap;
  }
  for (i = 0; i < record->lookup_cap; ++i) {
    record->lookup_indices[i] = -1;
  }
}

static void driver_c_prog_record_lookup_ensure_cap_exact(DriverCProgRecord *record, int32_t need_cap) {
  if (record == NULL || need_cap <= 0) {
    return;
  }
  if (record->lookup_shared) {
    driver_c_prog_record_detach_lookup(record, need_cap, 1);
  }
  if (record->lookup_indices == NULL) {
    record->lookup_indices = record->inline_lookup_indices;
    record->lookup_cap = DRIVER_C_PROG_RECORD_LOOKUP_INLINE_CAP;
  }
  if (record->lookup_cap >= need_cap) {
    return;
  }
  if (record->lookup_indices == record->inline_lookup_indices) {
    record->lookup_indices =
        (int32_t *)driver_c_prog_xcalloc((size_t)need_cap, sizeof(int32_t));
  } else {
    record->lookup_indices =
        (int32_t *)driver_c_prog_xrealloc(record->lookup_indices,
                                          sizeof(int32_t) * (size_t)need_cap);
  }
  record->lookup_cap = need_cap;
}

static void driver_c_prog_record_lookup_insert(DriverCProgRecord *record, int32_t field_index) {
  uint64_t hash = 0;
  int32_t start = 0;
  int32_t slot = 0;
  if (record != NULL && record->lookup_shared) {
    driver_c_prog_record_detach_lookup(record, record->lookup_cap, 1);
  }
  if (record == NULL ||
      field_index < 0 ||
      field_index >= record->len ||
      record->lookup_cap <= 0 ||
      record->lookup_indices == NULL ||
      record->names[field_index] == NULL) {
    return;
  }
  hash = driver_c_prog_hash_text(record->names[field_index]);
  start = (int32_t)(hash & (uint64_t)(record->lookup_cap - 1));
  slot = start;
  while (record->lookup_indices[slot] >= 0) {
    slot = (slot + 1) & (record->lookup_cap - 1);
    if (slot == start) {
      driver_c_die("driver_c program runtime: record lookup saturated");
    }
  }
  record->lookup_indices[slot] = field_index;
}

static void driver_c_prog_record_lookup_rebuild(DriverCProgRecord *record) {
  int32_t i = 0;
  if (record == NULL) {
    return;
  }
  driver_c_prog_record_lookup_reset(record, record->len);
  for (i = 0; i < record->len; ++i) {
    driver_c_prog_record_lookup_insert(record, i);
  }
}

static int32_t driver_c_prog_record_lookup_find(DriverCProgRecord *record, const char *name) {
  uint64_t hash = 0;
  int32_t start = 0;
  int32_t slot = 0;
  if (record == NULL ||
      name == NULL ||
      record->lookup_cap <= 0 ||
      record->lookup_indices == NULL) {
    return -1;
  }
  hash = driver_c_prog_hash_text(name);
  start = (int32_t)(hash & (uint64_t)(record->lookup_cap - 1));
  slot = start;
  while (record->lookup_indices[slot] >= 0) {
    int32_t field_index = record->lookup_indices[slot];
    if (field_index >= 0 &&
        field_index < record->len &&
        driver_c_prog_text_eq(record->names[field_index], name)) {
      return field_index;
    }
    slot = (slot + 1) & (record->lookup_cap - 1);
    if (slot == start) {
      break;
    }
  }
  return -1;
}

static void driver_c_prog_field_lookup_reset(int32_t *lookup_indices,
                                             int32_t lookup_cap) {
  int32_t i = 0;
  if (lookup_indices == NULL || lookup_cap <= 0) {
    return;
  }
  for (i = 0; i < lookup_cap; ++i) {
    lookup_indices[i] = -1;
  }
}

static void driver_c_prog_field_lookup_insert(int32_t *lookup_indices,
                                              int32_t lookup_cap,
                                              char **field_names,
                                              int32_t field_count,
                                              int32_t field_index) {
  uint64_t hash = 0;
  int32_t start = 0;
  int32_t slot = 0;
  if (lookup_indices == NULL ||
      lookup_cap <= 0 ||
      field_names == NULL ||
      field_index < 0 ||
      field_index >= field_count ||
      field_names[field_index] == NULL) {
    return;
  }
  hash = driver_c_prog_hash_text(field_names[field_index]);
  start = (int32_t)(hash & (uint64_t)(lookup_cap - 1));
  slot = start;
  while (lookup_indices[slot] >= 0) {
    slot = (slot + 1) & (lookup_cap - 1);
    if (slot == start) {
      driver_c_die("driver_c program runtime: field lookup saturated");
    }
  }
  lookup_indices[slot] = field_index;
}

static int32_t driver_c_prog_field_lookup_find(int32_t *lookup_indices,
                                               int32_t lookup_cap,
                                               char **field_names,
                                               int32_t field_count,
                                               const char *field_name) {
  uint64_t hash = 0;
  int32_t start = 0;
  int32_t slot = 0;
  if (lookup_indices == NULL ||
      lookup_cap <= 0 ||
      field_names == NULL ||
      field_name == NULL) {
    return -1;
  }
  hash = driver_c_prog_hash_text(field_name);
  start = (int32_t)(hash & (uint64_t)(lookup_cap - 1));
  slot = start;
  while (lookup_indices[slot] >= 0) {
    int32_t field_index = lookup_indices[slot];
    if (field_index >= 0 &&
        field_index < field_count &&
        driver_c_prog_text_eq(field_names[field_index], field_name)) {
      return field_index;
    }
    slot = (slot + 1) & (lookup_cap - 1);
    if (slot == start) {
      break;
    }
  }
  return -1;
}

static int driver_c_prog_type_ctor_base_matches(const char *left_text, const char *right_text) {
  char *left_base = NULL;
  char *left_args = NULL;
  char *right_base = NULL;
  char *right_args = NULL;
  char *left_tail = NULL;
  char *right_tail = NULL;
  int ok = 0;
  if (left_text == NULL || right_text == NULL || left_text[0] == '\0' || right_text[0] == '\0') {
    return 0;
  }
  if (!driver_c_prog_type_text_split_ctor(left_text, &left_base, &left_args)) {
    left_base = driver_c_prog_trim_dup(left_text);
  }
  if (!driver_c_prog_type_text_split_ctor(right_text, &right_base, &right_args)) {
    right_base = driver_c_prog_trim_dup(right_text);
  }
  if (driver_c_prog_text_eq(left_base, right_base)) {
    ok = 1;
  } else {
    char *left_dot = strrchr(left_base, '.');
    char *right_dot = strrchr(right_base, '.');
    left_tail = left_dot != NULL ? left_dot + 1 : left_base;
    right_tail = right_dot != NULL ? right_dot + 1 : right_base;
    ok = driver_c_prog_text_eq(left_tail, right_tail);
  }
  free(left_base);
  free(left_args);
  free(right_base);
  free(right_args);
  return ok;
}

static int driver_c_prog_type_text_is_more_specific_than(const char *current_type,
                                                         const char *next_type) {
  char *current_base = NULL;
  char *current_args = NULL;
  char *next_base = NULL;
  char *next_args = NULL;
  int current_has_args = 0;
  int next_has_args = 0;
  int ok = 0;
  if (current_type == NULL || current_type[0] == '\0') {
    return 0;
  }
  if (next_type == NULL || next_type[0] == '\0') {
    return 1;
  }
  if (!driver_c_prog_type_ctor_base_matches(current_type, next_type)) {
    return 0;
  }
  if (driver_c_prog_text_eq(current_type, next_type)) {
    return 0;
  }
  if (driver_c_prog_type_text_split_ctor(current_type, &current_base, &current_args) &&
      current_args != NULL &&
      current_args[0] != '\0') {
    current_has_args = 1;
  }
  if (driver_c_prog_type_text_split_ctor(next_type, &next_base, &next_args) &&
      next_args != NULL &&
      next_args[0] != '\0') {
    next_has_args = 1;
  }
  ok = current_has_args && !next_has_args;
  free(current_base);
  free(current_args);
  free(next_base);
  free(next_args);
  return ok;
}

static DriverCProgValue driver_c_prog_refine_value_to_declared_type(DriverCProgRegistry *registry,
                                                                    const char *owner_module,
                                                                    const char *declared_type,
                                                                    DriverCProgValue value) {
  DriverCProgValue out = driver_c_prog_materialize(value);
  char *normalized = NULL;
  if (declared_type == NULL || declared_type[0] == '\0') {
    return out;
  }
  normalized = driver_c_prog_normalize_visible_type_text_dup(registry,
                                                             owner_module != NULL ? owner_module : "",
                                                             declared_type,
                                                             0);
  if (normalized == NULL || normalized[0] == '\0') {
    free(normalized);
    return out;
  }
  if (out.kind == DRIVER_C_PROG_VALUE_RECORD &&
      out.as.record != NULL &&
      ((out.as.record->type_text == NULL || out.as.record->type_text[0] == '\0') ||
       driver_c_prog_type_text_is_more_specific_than(normalized, out.as.record->type_text))) {
    driver_c_prog_record_set_type(out.as.record, normalized);
  }
  if (out.kind == DRIVER_C_PROG_VALUE_ARRAY &&
      out.as.array != NULL) {
    char *base = NULL;
    char *args_text = NULL;
    if (driver_c_prog_type_text_split_ctor(normalized, &base, &args_text) &&
        base != NULL &&
        base[0] != '\0' &&
        args_text != NULL &&
        args_text[0] == '\0') {
      free(base);
      free(args_text);
      base = NULL;
      args_text = NULL;
    }
    free(base);
    free(args_text);
  }
  free(normalized);
  return out;
}

static DriverCProgValue driver_c_prog_refine_value_to_slot_kind(DriverCProgValue current,
                                                                DriverCProgValue value) {
  DriverCProgValue cur = driver_c_prog_materialize(current);
  DriverCProgValue out = driver_c_prog_materialize(value);
  switch (cur.kind) {
    case DRIVER_C_PROG_VALUE_BOOL:
      if (out.kind != DRIVER_C_PROG_VALUE_BOOL) {
        return driver_c_prog_value_bool(driver_c_prog_value_truthy(out));
      }
      return out;
    case DRIVER_C_PROG_VALUE_I32:
      if (out.kind != DRIVER_C_PROG_VALUE_I32) {
        return driver_c_prog_value_i32(driver_c_prog_value_to_i32(out));
      }
      return out;
    case DRIVER_C_PROG_VALUE_I64:
      if (out.kind != DRIVER_C_PROG_VALUE_I64) {
        return driver_c_prog_value_i64(driver_c_prog_value_to_i64(out));
      }
      return out;
    case DRIVER_C_PROG_VALUE_U32:
      if (out.kind != DRIVER_C_PROG_VALUE_U32) {
        return driver_c_prog_value_u32(driver_c_prog_value_to_u32(out));
      }
      return out;
    case DRIVER_C_PROG_VALUE_U64:
      if (out.kind != DRIVER_C_PROG_VALUE_U64) {
        return driver_c_prog_value_u64(driver_c_prog_value_to_u64(out));
      }
      return out;
    case DRIVER_C_PROG_VALUE_F64:
      if (out.kind != DRIVER_C_PROG_VALUE_F64) {
        return driver_c_prog_value_f64(driver_c_prog_value_to_f64(out));
      }
      return out;
    default:
      return out;
  }
}

static DriverCProgValue driver_c_prog_refine_value_to_zero_plan(const DriverCProgZeroPlan *plan,
                                                                DriverCProgValue value) {
  DriverCProgValue out = driver_c_prog_materialize(value);
  if (plan == NULL) {
    return out;
  }
  switch (plan->kind) {
    case DRIVER_C_PROG_ZERO_PLAN_BOOL:
      return out.kind == DRIVER_C_PROG_VALUE_BOOL ? out : driver_c_prog_value_bool(driver_c_prog_value_truthy(out));
    case DRIVER_C_PROG_ZERO_PLAN_I32:
    case DRIVER_C_PROG_ZERO_PLAN_ENUM:
      return out.kind == DRIVER_C_PROG_VALUE_I32 ? out : driver_c_prog_value_i32(driver_c_prog_value_to_i32(out));
    case DRIVER_C_PROG_ZERO_PLAN_U32:
      return out.kind == DRIVER_C_PROG_VALUE_U32 ? out : driver_c_prog_value_u32(driver_c_prog_value_to_u32(out));
    case DRIVER_C_PROG_ZERO_PLAN_I64:
      return out.kind == DRIVER_C_PROG_VALUE_I64 ? out : driver_c_prog_value_i64(driver_c_prog_value_to_i64(out));
    case DRIVER_C_PROG_ZERO_PLAN_U64:
      return out.kind == DRIVER_C_PROG_VALUE_U64 ? out : driver_c_prog_value_u64(driver_c_prog_value_to_u64(out));
    case DRIVER_C_PROG_ZERO_PLAN_F64:
      return out.kind == DRIVER_C_PROG_VALUE_F64 ? out : driver_c_prog_value_f64(driver_c_prog_value_to_f64(out));
    case DRIVER_C_PROG_ZERO_PLAN_DYNAMIC_ARRAY:
    case DRIVER_C_PROG_ZERO_PLAN_FIXED_ARRAY:
      if (out.kind == DRIVER_C_PROG_VALUE_ARRAY &&
          out.as.array != NULL &&
          plan->elem_type_text != NULL &&
          plan->elem_type_text[0] != '\0' &&
          (out.as.array->elem_type_text == NULL || out.as.array->elem_type_text[0] == '\0')) {
        driver_c_prog_array_set_elem_type(out.as.array, plan->elem_type_text);
      }
      return out;
    case DRIVER_C_PROG_ZERO_PLAN_RECORD:
      if (out.kind == DRIVER_C_PROG_VALUE_RECORD &&
          out.as.record != NULL &&
          plan->type_text != NULL &&
          plan->type_text[0] != '\0' &&
          ((out.as.record->type_text == NULL || out.as.record->type_text[0] == '\0') ||
           driver_c_prog_type_text_is_more_specific_than(plan->type_text, out.as.record->type_text))) {
        driver_c_prog_record_set_type(out.as.record, plan->type_text);
      }
      return out;
    default:
      return out;
  }
}

static DriverCProgValue driver_c_prog_value_array(DriverCProgArray *array) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_ARRAY;
  out.as.array = array != NULL ? array : driver_c_prog_array_new();
  return out;
}

static DriverCProgValue driver_c_prog_value_record(DriverCProgRecord *record) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_RECORD;
  out.as.record = record != NULL ? record : driver_c_prog_record_new();
  return out;
}

static DriverCProgValue driver_c_prog_value_array_temp(DriverCProgArray *array) {
  DriverCProgValue out = driver_c_prog_value_array(array);
  out.flags |= DRIVER_C_PROG_VALUE_FLAG_EPHEMERAL_AGGREGATE;
  return out;
}

static DriverCProgValue driver_c_prog_value_record_temp(DriverCProgRecord *record) {
  DriverCProgValue out = driver_c_prog_value_record(record);
  out.flags |= DRIVER_C_PROG_VALUE_FLAG_EPHEMERAL_AGGREGATE;
  return out;
}

static DriverCProgRoutine driver_c_prog_routine_from_item(DriverCProgItem *item) {
  DriverCProgRoutine out;
  memset(&out, 0, sizeof(out));
  out.item_id = item != NULL ? item->item_id : -1;
  out.label = item != NULL ? item->label : "";
  out.owner_module = item != NULL ? item->owner_module : "";
  out.top_level_kind = item != NULL ? item->top_level_kind : "";
  out.top_level_tag = item != NULL ? item->top_level_tag : DRIVER_C_PROG_TOP_UNKNOWN;
  out.builtin_tag = (item != NULL && item->top_level_tag == DRIVER_C_PROG_TOP_BUILTIN)
                        ? driver_c_prog_builtin_tag_from_text(item->label)
                        : DRIVER_C_PROG_BUILTIN_NONE;
  out.importc_symbol = item != NULL ? item->importc_symbol : "";
  out.type_arg_texts = NULL;
  out.type_arg_count = 0;
  return out;
}

static DriverCProgValue driver_c_prog_value_routine(DriverCProgItem *item) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_ROUTINE;
  out.as.routine = driver_c_prog_routine_from_item(item);
  return out;
}

static DriverCProgValue driver_c_prog_value_builtin_routine(const char *label,
                                                            const char *owner_module) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_ROUTINE;
  out.as.routine.item_id = -1;
  out.as.routine.label = label != NULL ? label : "";
  out.as.routine.owner_module = owner_module != NULL ? owner_module : "";
  out.as.routine.top_level_kind = "builtin";
  out.as.routine.top_level_tag = DRIVER_C_PROG_TOP_BUILTIN;
  out.as.routine.builtin_tag = driver_c_prog_builtin_tag_from_text(label);
  out.as.routine.importc_symbol = "";
  out.as.routine.type_arg_texts = NULL;
  out.as.routine.type_arg_count = 0;
  return out;
}

static DriverCProgValue driver_c_prog_value_module(int32_t import_item_id,
                                                   const char *alias,
                                                   const char *target_module) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_MODULE;
  out.as.module.import_item_id = import_item_id;
  out.as.module.alias = alias != NULL ? alias : "";
  out.as.module.target_module = target_module != NULL ? target_module : "";
  return out;
}

static DriverCProgValue driver_c_prog_value_type_token(const char *type_text) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_TYPE_TOKEN;
  out.as.type_text = type_text != NULL ? type_text : "";
  return out;
}

static DriverCProgValue driver_c_prog_value_zero_plan(DriverCProgZeroPlan *plan) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_ZERO_PLAN;
  out.as.zero_plan = plan;
  return out;
}

static DriverCProgValue driver_c_prog_value_ref(DriverCProgValue *slot) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_REF;
  out.as.ref.slot = slot;
  return out;
}

#define DRIVER_C_PROG_STR_SEQ_SLOT_MAGIC 0x53535154u

static DriverCProgValue driver_c_prog_materialize_zero_plan_shallow(const DriverCProgZeroPlan *plan) {
  if (plan == NULL) {
    return driver_c_prog_value_nil();
  }
  switch (plan->kind) {
    case DRIVER_C_PROG_ZERO_PLAN_DYNAMIC_ARRAY:
    case DRIVER_C_PROG_ZERO_PLAN_FIXED_ARRAY:
      return driver_c_prog_zero_array_shell_from_plan(plan);
    case DRIVER_C_PROG_ZERO_PLAN_RECORD:
      return driver_c_prog_zero_record_shell_from_plan(plan);
    default:
      return driver_c_prog_zero_value_from_plan(plan);
  }
}

static DriverCProgValue driver_c_prog_unwrap_ref_value(DriverCProgValue value) {
  while (value.kind == DRIVER_C_PROG_VALUE_REF) {
    if (value.as.ref.slot == NULL) {
      return driver_c_prog_value_nil();
    }
    value = *value.as.ref.slot;
  }
  return value;
}

static DriverCProgValue driver_c_prog_materialize_slot(DriverCProgValue *slot) {
  while (slot != NULL && slot->kind == DRIVER_C_PROG_VALUE_REF) {
    slot = slot->as.ref.slot;
  }
  if (slot == NULL) {
    return driver_c_prog_value_nil();
  }
  if (slot->kind == DRIVER_C_PROG_VALUE_ZERO_PLAN) {
    *slot = driver_c_prog_materialize_zero_plan_shallow(slot->as.zero_plan);
  }
  return *slot;
}

static DriverCProgValue driver_c_prog_materialize(DriverCProgValue value) {
  while (value.kind == DRIVER_C_PROG_VALUE_REF) {
    if (value.as.ref.slot == NULL) {
      return driver_c_prog_value_nil();
    }
    value = driver_c_prog_materialize_slot(value.as.ref.slot);
  }
  if (value.kind == DRIVER_C_PROG_VALUE_ZERO_PLAN) {
    return driver_c_prog_materialize_zero_plan_shallow(value.as.zero_plan);
  }
  return value;
}

static int driver_c_prog_value_is_ephemeral_aggregate(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  return (value.kind == DRIVER_C_PROG_VALUE_ARRAY || value.kind == DRIVER_C_PROG_VALUE_RECORD) &&
         (value.flags & DRIVER_C_PROG_VALUE_FLAG_EPHEMERAL_AGGREGATE) != 0;
}

static DriverCProgValue driver_c_prog_clone_value_deep(DriverCProgValue value);

static void driver_c_prog_value_clear_ephemeral_flag_root(DriverCProgValue *value) {
  if (value == NULL) {
    return;
  }
  *value = driver_c_prog_unwrap_ref_value(*value);
  if (value->kind == DRIVER_C_PROG_VALUE_ZERO_PLAN) {
    return;
  }
  value->flags &= ~((uint32_t)DRIVER_C_PROG_VALUE_FLAG_EPHEMERAL_AGGREGATE);
}

static DriverCProgValue driver_c_prog_prepare_fresh_slot_value(DriverCProgValue value) {
  DriverCProgValue next = driver_c_prog_materialize(value);
  if (next.kind == DRIVER_C_PROG_VALUE_ARRAY || next.kind == DRIVER_C_PROG_VALUE_RECORD) {
    if (!driver_c_prog_value_is_ephemeral_aggregate(next)) {
      next = driver_c_prog_clone_value_deep(next);
    }
    driver_c_prog_value_clear_ephemeral_flag_root(&next);
  } else {
    next.flags = 0;
  }
  return next;
}

static DriverCProgValue *driver_c_prog_record_slot(DriverCProgRecord *record,
                                                   const char *name,
                                                   int create_if_missing);
static DriverCProgValue *driver_c_prog_record_slot_at(DriverCProgRecord *record,
                                                      int32_t field_index,
                                                      const char *field_name);
static void driver_c_prog_record_ensure_value_cap(DriverCProgRecord *record, int32_t need);
static void driver_c_prog_record_ensure_cap(DriverCProgRecord *record, int32_t need);
static void driver_c_prog_array_set_len(DriverCProgArray *array, int32_t new_len);

static DriverCProgValue driver_c_prog_clone_value_deep(DriverCProgValue value) {
  int32_t i = 0;
  value = driver_c_prog_unwrap_ref_value(value);
  if (value.kind == DRIVER_C_PROG_VALUE_ZERO_PLAN) {
    return value;
  }
  if (value.kind == DRIVER_C_PROG_VALUE_ARRAY) {
    DriverCProgArray *src = value.as.array;
    DriverCProgArray *dst = NULL;
    if (src == NULL) {
      return driver_c_prog_value_array(driver_c_prog_array_new());
    }
    dst = driver_c_prog_array_new_with_elem(src->elem_type_text);
    driver_c_prog_array_ensure_cap(dst, src->cap);
    dst->len = src->len;
    for (i = 0; i < src->len; ++i) {
      dst->items[i] = driver_c_prog_clone_value_deep(src->items[i]);
    }
    return driver_c_prog_value_array(dst);
  }
  if (value.kind == DRIVER_C_PROG_VALUE_RECORD) {
    DriverCProgRecord *src = value.as.record;
    DriverCProgRecord *dst = NULL;
    if (src == NULL) {
      return driver_c_prog_value_record(driver_c_prog_record_new());
    }
    dst = driver_c_prog_record_new_with_type(src->type_text);
    driver_c_prog_record_ensure_value_cap(dst, src->cap);
    for (i = 0; i < src->len; ++i) {
      dst->values[i] = driver_c_prog_clone_value_deep(src->values[i]);
    }
    if (src->names_shared) {
      driver_c_prog_record_set_shared_names(dst, src->names);
    } else {
      for (i = 0; i < src->len; ++i) {
        dst->names[i] = src->names[i];
      }
    }
    dst->len = src->len;
    if (src->lookup_shared) {
      driver_c_prog_record_set_shared_lookup(dst, src->lookup_indices, src->lookup_cap);
    } else {
      driver_c_prog_record_lookup_ensure_cap_exact(dst, src->lookup_cap);
      memcpy(dst->lookup_indices, src->lookup_indices, sizeof(int32_t) * (size_t)src->lookup_cap);
    }
    return driver_c_prog_value_record(dst);
  }
  return value;
}

static int32_t driver_c_prog_value_to_i32(DriverCProgValue value);
static int64_t driver_c_prog_value_to_i64(DriverCProgValue value);
static uint32_t driver_c_prog_value_to_u32(DriverCProgValue value);
static uint64_t driver_c_prog_value_to_u64(DriverCProgValue value);
static double driver_c_prog_value_to_f64(DriverCProgValue value);
static void *driver_c_prog_value_to_void_ptr(DriverCProgValue value);
int32_t cheng_recvfrom_fd_ex(int32_t fd, void *buf, int32_t len, int32_t flags, void *addr, void *addrlen, int32_t *outErr);
static const char *driver_c_prog_value_cstring_borrow(DriverCProgValue value);
static DriverCProgValue *driver_c_prog_slot_from_refish(DriverCProgValue value);
static void driver_c_prog_assign_slot(DriverCProgValue *slot, DriverCProgValue value);
static void driver_c_prog_record_detach_names(DriverCProgRecord *record, int32_t need_cap);
static void driver_c_prog_record_detach_lookup(DriverCProgRecord *record,
                                               int32_t need_cap,
                                               int copy_existing);
static DriverCProgValue driver_c_prog_zero_value_from_type(struct DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *inst_owner_module,
                                                           const char *type_text,
                                                           int depth);
static int driver_c_prog_is_builtin_type_ctor_label(const char *label);

static DriverCProgValue driver_c_prog_cast_to_type(const char *type_text, DriverCProgValue value) {
  DriverCProgValue materialized = driver_c_prog_materialize(value);
  if (driver_c_prog_text_eq(type_text, "ptr")) {
    if (value.kind == DRIVER_C_PROG_VALUE_REF && value.as.ref.slot != NULL) {
      return driver_c_prog_value_ptr((void *)value.as.ref.slot);
    }
    if (materialized.kind == DRIVER_C_PROG_VALUE_STR) {
      return driver_c_prog_value_ptr((void *)materialized.as.str.ptr);
    }
    if (materialized.kind == DRIVER_C_PROG_VALUE_BYTES) {
      return driver_c_prog_value_ptr(materialized.as.bytes.data);
    }
    return driver_c_prog_value_ptr(materialized.kind == DRIVER_C_PROG_VALUE_PTR ? materialized.as.ptr : NULL);
  }
  if (driver_c_prog_text_eq(type_text, "cstring")) {
    return driver_c_prog_value_ptr((void *)driver_c_prog_value_cstring_borrow(materialized));
  }
  if (driver_c_prog_text_eq(type_text, "char")) {
    return driver_c_prog_value_i32(driver_c_prog_value_to_i32(materialized) & 255);
  }
  if (driver_c_prog_text_eq(type_text, "float64")) {
    return driver_c_prog_value_f64(driver_c_prog_value_to_f64(materialized));
  }
  if (driver_c_prog_text_eq(type_text, "int") ||
      driver_c_prog_text_eq(type_text, "int32") ||
      driver_c_prog_text_eq(type_text, "int16") ||
      driver_c_prog_text_eq(type_text, "int8")) {
    return driver_c_prog_value_i32(driver_c_prog_value_to_i32(materialized));
  }
  if (driver_c_prog_text_eq(type_text, "uint8") ||
      driver_c_prog_text_eq(type_text, "uint16") ||
      driver_c_prog_text_eq(type_text, "uint32")) {
    return driver_c_prog_value_u32(driver_c_prog_value_to_u32(materialized));
  }
  if (driver_c_prog_text_eq(type_text, "int64")) {
    return driver_c_prog_value_i64(driver_c_prog_value_to_i64(materialized));
  }
  if (driver_c_prog_text_eq(type_text, "uint64")) {
    return driver_c_prog_value_u64(driver_c_prog_value_to_u64(materialized));
  }
  return materialized;
}

static void driver_c_prog_record_ensure_value_cap(DriverCProgRecord *record, int32_t need) {
  int32_t next_cap = 0;
  if (record == NULL) return;
  if (need <= record->cap) return;
  next_cap = record->cap > 0 ? record->cap : DRIVER_C_PROG_RECORD_INLINE_CAP;
  while (next_cap < need) {
    next_cap = next_cap * 2;
  }
  if (record->values == record->inline_values) {
    DriverCProgValue *next_values =
        (DriverCProgValue *)driver_c_prog_xcalloc((size_t)next_cap, sizeof(DriverCProgValue));
    memcpy(next_values, record->inline_values, sizeof(DriverCProgValue) * (size_t)record->len);
    record->values = next_values;
  } else {
    record->values =
        (DriverCProgValue *)driver_c_prog_xrealloc(record->values,
                                                   sizeof(DriverCProgValue) * (size_t)next_cap);
    memset(record->values + record->cap,
           0,
           sizeof(DriverCProgValue) * (size_t)(next_cap - record->cap));
  }
  record->cap = next_cap;
}

static void driver_c_prog_record_ensure_cap(DriverCProgRecord *record, int32_t need) {
  if (record == NULL) return;
  if (need <= record->cap) return;
  driver_c_prog_record_ensure_value_cap(record, need);
  if (record->names_shared) {
    return;
  }
  if (record->names == record->inline_names) {
    char **next_names = (char **)driver_c_prog_xcalloc((size_t)record->cap, sizeof(char *));
    memcpy(next_names, record->inline_names, sizeof(char *) * (size_t)record->len);
    record->names = next_names;
    return;
  }
  record->names =
      (char **)driver_c_prog_xrealloc(record->names, sizeof(char *) * (size_t)record->cap);
  memset(record->names + record->len, 0, sizeof(char *) * (size_t)(record->cap - record->len));
}

static DriverCProgValue *driver_c_prog_record_slot(DriverCProgRecord *record,
                                                   const char *name,
                                                   int create_if_missing) {
  int32_t i = 0;
  int32_t cached_index = -1;
  if (record == NULL || name == NULL) {
    return NULL;
  }
  cached_index = driver_c_prog_record_lookup_find(record, name);
  if (cached_index >= 0) {
    return &record->values[cached_index];
  }
  for (i = 0; i < record->len; ++i) {
    if (driver_c_prog_text_eq(record->names[i], name)) {
      driver_c_prog_record_lookup_insert(record, i);
      return &record->values[i];
    }
  }
  if (!create_if_missing) {
    return NULL;
  }
  if (record->names_shared) {
    driver_c_prog_record_detach_names(record, record->len + 1);
  }
  if (record->lookup_shared) {
    driver_c_prog_record_detach_lookup(record,
                                       driver_c_prog_record_lookup_cap_for_count(record->len + 1),
                                       1);
  }
  driver_c_prog_record_ensure_cap(record, record->len + 1);
  record->names[record->len] = driver_c_dup_cstring(name);
  record->values[record->len] = driver_c_prog_value_nil();
  record->len = record->len + 1;
  driver_c_prog_record_lookup_insert(record, record->len - 1);
  return &record->values[record->len - 1];
}

static void driver_c_prog_array_ensure_cap(DriverCProgArray *array, int32_t need) {
  int32_t next_cap = 0;
  if (array == NULL) return;
  if (need <= array->cap) return;
  next_cap = array->cap > 0 ? array->cap : DRIVER_C_PROG_ARRAY_INLINE_CAP;
  while (next_cap < need) {
    next_cap = next_cap * 2;
  }
  if (array->items == array->inline_items) {
    DriverCProgValue *next_items =
        (DriverCProgValue *)driver_c_prog_xcalloc((size_t)next_cap, sizeof(DriverCProgValue));
    memcpy(next_items, array->inline_items, sizeof(DriverCProgValue) * (size_t)array->len);
    array->items = next_items;
  } else {
    array->items = (DriverCProgValue *)driver_c_prog_xrealloc(array->items,
                                                              sizeof(DriverCProgValue) * (size_t)next_cap);
    memset(array->items + array->cap, 0, sizeof(DriverCProgValue) * (size_t)(next_cap - array->cap));
  }
  array->cap = next_cap;
}

static void driver_c_prog_array_set_len(DriverCProgArray *array, int32_t new_len) {
  int32_t i = 0;
  if (array == NULL) return;
  if (new_len < 0) new_len = 0;
  driver_c_prog_array_ensure_cap(array, new_len);
  for (i = array->len; i < new_len; ++i) {
    array->items[i] = driver_c_prog_value_nil();
  }
  array->len = new_len;
}

static void driver_c_prog_array_push(DriverCProgArray *array, DriverCProgValue value) {
  if (array == NULL) return;
  driver_c_prog_array_set_len(array, array->len + 1);
  array->items[array->len - 1] = driver_c_prog_prepare_fresh_slot_value(value);
}

static DriverCProgArray *driver_c_prog_array_from_refish(DriverCProgValue value, int create_if_nil) {
  DriverCProgValue *slot = driver_c_prog_slot_from_refish(value);
  DriverCProgValue materialized =
      slot != NULL ? driver_c_prog_materialize(*slot) : driver_c_prog_materialize(value);
  if (materialized.kind == DRIVER_C_PROG_VALUE_ARRAY && materialized.as.array != NULL) {
    return materialized.as.array;
  }
  if (slot != NULL && create_if_nil && materialized.kind == DRIVER_C_PROG_VALUE_NIL) {
    driver_c_prog_assign_slot(slot, driver_c_prog_value_array_temp(driver_c_prog_array_new()));
    materialized = driver_c_prog_materialize(*slot);
    if (materialized.kind == DRIVER_C_PROG_VALUE_ARRAY) {
      return materialized.as.array;
    }
  }
  return NULL;
}

static void *driver_c_prog_str_seq_slot_token_new(DriverCProgValue *slot) {
  DriverCProgStrSeqSlotToken *token = NULL;
  if (slot == NULL) return NULL;
  token = (DriverCProgStrSeqSlotToken *)driver_c_prog_xmalloc(sizeof(*token));
  token->magic = DRIVER_C_PROG_STR_SEQ_SLOT_MAGIC;
  token->slot = slot;
  return token;
}

static DriverCProgValue *driver_c_prog_str_seq_slot_token_take(void *raw) {
  DriverCProgStrSeqSlotToken *token = (DriverCProgStrSeqSlotToken *)raw;
  DriverCProgValue *slot = NULL;
  if (token == NULL || token->magic != DRIVER_C_PROG_STR_SEQ_SLOT_MAGIC) {
    return NULL;
  }
  slot = token->slot;
  token->magic = 0;
  token->slot = NULL;
  free(token);
  return slot;
}

static void driver_c_prog_store_str_bridge_local(void *raw, ChengStrBridge value) {
  ChengStrBridge *slot = (ChengStrBridge *)raw;
  if (slot == NULL) return;
  *slot = value;
}

static int32_t driver_c_prog_value_to_i32(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  switch (value.kind) {
    case DRIVER_C_PROG_VALUE_BOOL: return value.as.b != 0 ? 1 : 0;
    case DRIVER_C_PROG_VALUE_I32: return value.as.i32;
    case DRIVER_C_PROG_VALUE_I64: return (int32_t)value.as.i64;
    case DRIVER_C_PROG_VALUE_U32: return (int32_t)value.as.u32;
    case DRIVER_C_PROG_VALUE_U64: return (int32_t)value.as.u64;
    case DRIVER_C_PROG_VALUE_F64: return (int32_t)value.as.f64;
    case DRIVER_C_PROG_VALUE_PTR: return (int32_t)(intptr_t)value.as.ptr;
    default: return 0;
  }
}

static int64_t driver_c_prog_value_to_i64(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  switch (value.kind) {
    case DRIVER_C_PROG_VALUE_BOOL: return value.as.b != 0 ? 1 : 0;
    case DRIVER_C_PROG_VALUE_I32: return value.as.i32;
    case DRIVER_C_PROG_VALUE_I64: return value.as.i64;
    case DRIVER_C_PROG_VALUE_U32: return (int64_t)value.as.u32;
    case DRIVER_C_PROG_VALUE_U64: return (int64_t)value.as.u64;
    case DRIVER_C_PROG_VALUE_F64: return (int64_t)value.as.f64;
    case DRIVER_C_PROG_VALUE_PTR: return (int64_t)(intptr_t)value.as.ptr;
    default: return 0;
  }
}

static uint32_t driver_c_prog_value_to_u32(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  switch (value.kind) {
    case DRIVER_C_PROG_VALUE_BOOL: return value.as.b != 0 ? 1u : 0u;
    case DRIVER_C_PROG_VALUE_I32: return (uint32_t)value.as.i32;
    case DRIVER_C_PROG_VALUE_I64: return (uint32_t)value.as.i64;
    case DRIVER_C_PROG_VALUE_U32: return value.as.u32;
    case DRIVER_C_PROG_VALUE_U64: return (uint32_t)value.as.u64;
    case DRIVER_C_PROG_VALUE_F64: return (uint32_t)value.as.f64;
    case DRIVER_C_PROG_VALUE_PTR: return (uint32_t)(uintptr_t)value.as.ptr;
    default: return 0u;
  }
}

static uint64_t driver_c_prog_value_to_u64(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  switch (value.kind) {
    case DRIVER_C_PROG_VALUE_BOOL: return value.as.b != 0 ? 1u : 0u;
    case DRIVER_C_PROG_VALUE_I32: return (uint64_t)(uint32_t)value.as.i32;
    case DRIVER_C_PROG_VALUE_I64: return (uint64_t)value.as.i64;
    case DRIVER_C_PROG_VALUE_U32: return (uint64_t)value.as.u32;
    case DRIVER_C_PROG_VALUE_U64: return value.as.u64;
    case DRIVER_C_PROG_VALUE_F64: return (uint64_t)value.as.f64;
    case DRIVER_C_PROG_VALUE_PTR: return (uint64_t)(uintptr_t)value.as.ptr;
    default: return 0u;
  }
}

static double driver_c_prog_value_to_f64(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  switch (value.kind) {
    case DRIVER_C_PROG_VALUE_BOOL: return value.as.b != 0 ? 1.0 : 0.0;
    case DRIVER_C_PROG_VALUE_I32: return (double)value.as.i32;
    case DRIVER_C_PROG_VALUE_I64: return (double)value.as.i64;
    case DRIVER_C_PROG_VALUE_U32: return (double)value.as.u32;
    case DRIVER_C_PROG_VALUE_U64: return (double)value.as.u64;
    case DRIVER_C_PROG_VALUE_F64: return value.as.f64;
    default: return 0.0;
  }
}

static void *driver_c_prog_value_to_void_ptr(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  switch (value.kind) {
    case DRIVER_C_PROG_VALUE_NIL: return NULL;
    case DRIVER_C_PROG_VALUE_PTR: return value.as.ptr;
    case DRIVER_C_PROG_VALUE_STR: return (void *)value.as.str.ptr;
    case DRIVER_C_PROG_VALUE_BYTES: return value.as.bytes.data;
    case DRIVER_C_PROG_VALUE_U32: return (void *)(uintptr_t)value.as.u32;
    case DRIVER_C_PROG_VALUE_U64: return (void *)(uintptr_t)value.as.u64;
    default: return (void *)(intptr_t)driver_c_prog_value_to_i64(value);
  }
}

int32_t cheng_recvfrom_fd_ex(int32_t fd,
                             void *buf,
                             int32_t len,
                             int32_t flags,
                             void *addr,
                             void *addrlen,
                             int32_t *outErr) {
  socklen_t addr_len = 0;
  socklen_t *addr_len_ptr = (socklen_t *)addrlen;
  int32_t rc = 0;
  errno = 0;
  if (addr_len_ptr != NULL) {
    addr_len = *addr_len_ptr;
  }
  rc = (int32_t)recvfrom(fd,
                         buf,
                         (size_t)len,
                         flags,
                         (struct sockaddr *)addr,
                         addr_len_ptr != NULL ? &addr_len : NULL);
  if (addr_len_ptr != NULL) {
    *addr_len_ptr = addr_len;
  }
  if (outErr != NULL) {
    *outErr = errno;
  }
  return rc;
}

static int32_t driver_c_prog_align_up_i32(int32_t value, int32_t align) {
  int32_t rem = 0;
  if (align <= 1) return value;
  rem = value % align;
  if (rem == 0) return value;
  return value + (align - rem);
}

static DriverCProgTypeDecl *driver_c_prog_lookup_unique_type_decl_by_name(DriverCProgRegistry *registry,
                                                                          const char *decl_name) {
  DriverCProgTypeDecl *found = NULL;
  int32_t i = 0;
  if (registry == NULL || decl_name == NULL || decl_name[0] == '\0') {
    return NULL;
  }
  for (i = 0; i < registry->type_decl_count; ++i) {
    DriverCProgTypeDecl *decl = &registry->type_decls[i];
    if (!driver_c_prog_text_eq(decl->decl_name, decl_name)) {
      continue;
    }
    if (found != NULL && found != decl) {
      char message[512];
      snprintf(message,
               sizeof(message),
               "driver_c program runtime: ambiguous sizeof type=%s",
               decl_name);
      driver_c_die(message);
    }
    found = decl;
  }
  return found;
}

static int32_t driver_c_prog_alignof_type_text(DriverCProgRegistry *registry,
                                               const char *type_text,
                                               int depth);

static int32_t driver_c_prog_sizeof_type_text_resolved(DriverCProgRegistry *registry,
                                                       const char *type_text,
                                                       int depth) {
  char *trimmed = NULL;
  char *suffix_base = NULL;
  char *suffix_arg = NULL;
  char *ctor_base = NULL;
  char *ctor_args_text = NULL;
  char *dot = NULL;
  DriverCProgTypeDecl *decl = NULL;
  int32_t size = 0;
  int32_t i = 0;
  if (depth > 64) {
    driver_c_die("driver_c program runtime: sizeof type recursion too deep");
  }
  if (type_text == NULL || type_text[0] == '\0') return 0;
  trimmed = driver_c_prog_trim_dup(type_text);
  if (driver_c_prog_text_eq(trimmed, "bool") ||
      driver_c_prog_text_eq(trimmed, "char") ||
      driver_c_prog_text_eq(trimmed, "int8") ||
      driver_c_prog_text_eq(trimmed, "uint8")) {
    free(trimmed);
    return 1;
  }
  if (driver_c_prog_text_eq(trimmed, "int16") ||
      driver_c_prog_text_eq(trimmed, "uint16")) {
    free(trimmed);
    return 2;
  }
  if (driver_c_prog_text_eq(trimmed, "int32") ||
      driver_c_prog_text_eq(trimmed, "uint32")) {
    free(trimmed);
    return 4;
  }
  if (driver_c_prog_text_eq(trimmed, "int64") ||
      driver_c_prog_text_eq(trimmed, "uint64") ||
      driver_c_prog_text_eq(trimmed, "float64")) {
    free(trimmed);
    return 8;
  }
  if (driver_c_prog_text_eq(trimmed, "ptr") ||
      driver_c_prog_text_eq(trimmed, "cstring")) {
    free(trimmed);
    return (int32_t)sizeof(void *);
  }
  if (driver_c_prog_text_eq(trimmed, "str")) {
    free(trimmed);
    return (int32_t)sizeof(ChengStrBridge);
  }
  if (driver_c_prog_text_eq(trimmed, "Bytes")) {
    free(trimmed);
    return (int32_t)sizeof(DriverCProgBytes);
  }
  if (driver_c_prog_type_text_split_suffix_ctor(trimmed, &suffix_base, &suffix_arg)) {
    if (driver_c_prog_text_eq(suffix_base, "[]")) {
      free(trimmed);
      free(suffix_base);
      free(suffix_arg);
      return (int32_t)sizeof(ChengSeqHeader);
    }
    if (driver_c_prog_text_eq(suffix_base, "*")) {
      free(trimmed);
      free(suffix_base);
      free(suffix_arg);
      return (int32_t)sizeof(void *);
    }
  }
  free(suffix_base);
  free(suffix_arg);
  suffix_base = NULL;
  suffix_arg = NULL;
  if (driver_c_prog_type_text_split_ctor(trimmed, &ctor_base, &ctor_args_text)) {
    int32_t arg_count = 0;
    char **args = driver_c_prog_split_type_args_dup(ctor_args_text, &arg_count);
    char *ctor_dot = strchr(ctor_base, '.');
    if (ctor_dot != NULL && ctor_dot > ctor_base) {
      char *owner_module = driver_c_dup_range(ctor_base, ctor_dot);
      char *type_name = driver_c_prog_trim_dup(ctor_dot + 1);
      decl = driver_c_prog_lookup_type_decl(registry, owner_module, type_name);
      free(owner_module);
      free(type_name);
    } else {
      decl = driver_c_prog_lookup_unique_type_decl_by_name(registry, ctor_base);
    }
    if (decl == NULL) {
      driver_c_prog_free_string_array(args, arg_count);
      free(ctor_base);
      free(ctor_args_text);
      free(trimmed);
      return 0;
    }
    if (decl->alias_target != NULL && decl->alias_target[0] != '\0') {
      char *resolved = driver_c_dup_cstring(decl->alias_target);
      if (decl->param_count != arg_count) {
        driver_c_prog_free_string_array(args, arg_count);
        free(resolved);
        free(ctor_base);
        free(ctor_args_text);
        free(trimmed);
        driver_c_die("driver_c program runtime: sizeof generic alias arity mismatch");
      }
      for (i = 0; i < decl->param_count; ++i) {
        char *next = driver_c_prog_substitute_type_token_dup(resolved, decl->param_names[i], args[i]);
        free(resolved);
        resolved = next;
      }
      size = driver_c_prog_sizeof_type_text_resolved(registry, resolved, depth + 1);
      free(resolved);
      driver_c_prog_free_string_array(args, arg_count);
      free(ctor_base);
      free(ctor_args_text);
      free(trimmed);
      return size;
    }
    if (decl->has_first_enum_ordinal) {
      driver_c_prog_free_string_array(args, arg_count);
      free(ctor_base);
      free(ctor_args_text);
      free(trimmed);
      return 4;
    }
    if (decl->field_count > 0) {
      int32_t offset = 0;
      int32_t max_align = 1;
      if (decl->param_count != arg_count) {
        driver_c_prog_free_string_array(args, arg_count);
        free(ctor_base);
        free(ctor_args_text);
        free(trimmed);
        driver_c_die("driver_c program runtime: sizeof generic record arity mismatch");
      }
      for (i = 0; i < decl->field_count; ++i) {
        char *resolved_field = driver_c_dup_cstring(decl->field_types[i]);
        int32_t field_size = 0;
        int32_t field_align = 1;
        int32_t j = 0;
        for (j = 0; j < decl->param_count; ++j) {
          char *next = driver_c_prog_substitute_type_token_dup(resolved_field, decl->param_names[j], args[j]);
          free(resolved_field);
          resolved_field = next;
        }
        field_size = driver_c_prog_sizeof_type_text_resolved(registry, resolved_field, depth + 1);
        field_align = driver_c_prog_alignof_type_text(registry, resolved_field, depth + 1);
        free(resolved_field);
        if (field_size <= 0 || field_align <= 0) {
          driver_c_prog_free_string_array(args, arg_count);
          free(ctor_base);
          free(ctor_args_text);
          free(trimmed);
          return 0;
        }
        if (field_align > max_align) max_align = field_align;
        offset = driver_c_prog_align_up_i32(offset, field_align);
        offset = offset + field_size;
      }
      size = driver_c_prog_align_up_i32(offset, max_align);
      driver_c_prog_free_string_array(args, arg_count);
      free(ctor_base);
      free(ctor_args_text);
      free(trimmed);
      return size;
    }
    driver_c_prog_free_string_array(args, arg_count);
    free(ctor_base);
    free(ctor_args_text);
    free(trimmed);
    return 0;
  }
  free(ctor_base);
  free(ctor_args_text);
  dot = strchr(trimmed, '.');
  if (dot != NULL && dot > trimmed) {
    char *owner_module = driver_c_dup_range(trimmed, dot);
    char *type_name = driver_c_prog_trim_dup(dot + 1);
    decl = driver_c_prog_lookup_type_decl(registry, owner_module, type_name);
    free(owner_module);
    free(type_name);
  } else {
    decl = driver_c_prog_lookup_unique_type_decl_by_name(registry, trimmed);
  }
  if (decl == NULL) {
    free(trimmed);
    return 0;
  }
  if (decl->alias_target != NULL && decl->alias_target[0] != '\0') {
    size = driver_c_prog_sizeof_type_text_resolved(registry, decl->alias_target, depth + 1);
    free(trimmed);
    return size;
  }
  if (decl->has_first_enum_ordinal) {
    free(trimmed);
    return 4;
  }
  if (decl->field_count > 0) {
    int32_t offset = 0;
    int32_t max_align = 1;
    for (i = 0; i < decl->field_count; ++i) {
      int32_t field_size = driver_c_prog_sizeof_type_text_resolved(registry, decl->field_types[i], depth + 1);
      int32_t field_align = driver_c_prog_alignof_type_text(registry, decl->field_types[i], depth + 1);
      if (field_size <= 0 || field_align <= 0) {
        free(trimmed);
        return 0;
      }
      if (field_align > max_align) max_align = field_align;
      offset = driver_c_prog_align_up_i32(offset, field_align);
      offset = offset + field_size;
    }
    free(trimmed);
    return driver_c_prog_align_up_i32(offset, max_align);
  }
  free(trimmed);
  return 0;
}

static int32_t driver_c_prog_alignof_type_text(DriverCProgRegistry *registry,
                                               const char *type_text,
                                               int depth) {
  int32_t size = driver_c_prog_sizeof_type_text_resolved(registry, type_text, depth + 1);
  int32_t ptr_align = (int32_t)sizeof(void *);
  if (size <= 0) return 0;
  if (size >= ptr_align) return ptr_align;
  if (size >= 4) return 4;
  if (size >= 2) return 2;
  return 1;
}

static int32_t driver_c_prog_sizeof_type_text(DriverCProgRegistry *registry, const char *type_text) {
  return driver_c_prog_sizeof_type_text_resolved(registry, type_text, 0);
}

static int driver_c_prog_value_truthy(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  switch (value.kind) {
    case DRIVER_C_PROG_VALUE_NIL: return 0;
    case DRIVER_C_PROG_VALUE_BOOL: return value.as.b != 0;
    case DRIVER_C_PROG_VALUE_I32: return value.as.i32 != 0;
    case DRIVER_C_PROG_VALUE_I64: return value.as.i64 != 0;
    case DRIVER_C_PROG_VALUE_U32: return value.as.u32 != 0;
    case DRIVER_C_PROG_VALUE_U64: return value.as.u64 != 0;
    case DRIVER_C_PROG_VALUE_F64: return value.as.f64 != 0.0;
    case DRIVER_C_PROG_VALUE_STR: return driver_c_prog_str_len(value.as.str) > 0;
    case DRIVER_C_PROG_VALUE_BYTES: return value.as.bytes.len > 0;
    case DRIVER_C_PROG_VALUE_ARRAY: return value.as.array != NULL && value.as.array->len > 0;
    case DRIVER_C_PROG_VALUE_RECORD: return value.as.record != NULL && value.as.record->len > 0;
    case DRIVER_C_PROG_VALUE_PTR: return value.as.ptr != NULL;
    default: return 1;
  }
}

static char *driver_c_prog_value_to_cstring_dup(DriverCProgValue value) {
  char buf[64];
  value = driver_c_prog_materialize(value);
  switch (value.kind) {
    case DRIVER_C_PROG_VALUE_NIL:
      return driver_c_dup_cstring("");
    case DRIVER_C_PROG_VALUE_BOOL:
      return driver_c_dup_cstring(value.as.b != 0 ? "true" : "false");
    case DRIVER_C_PROG_VALUE_I32:
      snprintf(buf, sizeof(buf), "%d", value.as.i32);
      return driver_c_dup_cstring(buf);
    case DRIVER_C_PROG_VALUE_I64:
      snprintf(buf, sizeof(buf), "%lld", (long long)value.as.i64);
      return driver_c_dup_cstring(buf);
    case DRIVER_C_PROG_VALUE_U32:
      snprintf(buf, sizeof(buf), "%u", (unsigned)value.as.u32);
      return driver_c_dup_cstring(buf);
    case DRIVER_C_PROG_VALUE_U64:
      snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value.as.u64);
      return driver_c_dup_cstring(buf);
    case DRIVER_C_PROG_VALUE_F64:
      snprintf(buf, sizeof(buf), "%.17g", value.as.f64);
      return driver_c_dup_cstring(buf);
    case DRIVER_C_PROG_VALUE_STR:
      return driver_c_bridge_to_cstring(value.as.str);
    case DRIVER_C_PROG_VALUE_PTR:
      return driver_c_dup_cstring(value.as.ptr != NULL ? (const char *)value.as.ptr : "");
    case DRIVER_C_PROG_VALUE_TYPE_TOKEN:
      return driver_c_dup_cstring(value.as.type_text != NULL ? value.as.type_text : "");
    default:
      return driver_c_dup_cstring("");
  }
}

static const char *driver_c_prog_value_cstring_borrow(DriverCProgValue value) {
  static char *scratch = NULL;
  value = driver_c_prog_materialize(value);
  if (value.kind == DRIVER_C_PROG_VALUE_PTR) {
    return value.as.ptr != NULL ? (const char *)value.as.ptr : "";
  }
  if (value.kind == DRIVER_C_PROG_VALUE_STR) {
    if ((value.as.str.flags & CHENG_STR_BRIDGE_FLAG_OWNED) != 0 && value.as.str.ptr != NULL) {
      return value.as.str.ptr;
    }
    free(scratch);
    scratch = driver_c_bridge_to_cstring(value.as.str);
    return scratch != NULL ? scratch : "";
  }
  return "";
}

static DriverCProgValue *driver_c_prog_slot_from_refish(DriverCProgValue value) {
  if (value.kind == DRIVER_C_PROG_VALUE_REF) {
    return value.as.ref.slot;
  }
  return NULL;
}

static const char *driver_c_prog_value_kind_name(DriverCProgValueKind kind) {
  switch (kind) {
    case DRIVER_C_PROG_VALUE_NIL: return "nil";
    case DRIVER_C_PROG_VALUE_BOOL: return "bool";
    case DRIVER_C_PROG_VALUE_I32: return "i32";
    case DRIVER_C_PROG_VALUE_I64: return "i64";
    case DRIVER_C_PROG_VALUE_U32: return "u32";
    case DRIVER_C_PROG_VALUE_U64: return "u64";
    case DRIVER_C_PROG_VALUE_F64: return "f64";
    case DRIVER_C_PROG_VALUE_STR: return "str";
    case DRIVER_C_PROG_VALUE_BYTES: return "bytes";
    case DRIVER_C_PROG_VALUE_ARRAY: return "array";
    case DRIVER_C_PROG_VALUE_RECORD: return "record";
    case DRIVER_C_PROG_VALUE_ROUTINE: return "routine";
    case DRIVER_C_PROG_VALUE_MODULE: return "module";
    case DRIVER_C_PROG_VALUE_TYPE_TOKEN: return "type";
    case DRIVER_C_PROG_VALUE_REF: return "ref";
    case DRIVER_C_PROG_VALUE_PTR: return "ptr";
    case DRIVER_C_PROG_VALUE_ZERO_PLAN: return "zero_plan";
    default: return "unknown";
  }
}

static int driver_c_prog_value_is_u32_or_u64(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  return value.kind == DRIVER_C_PROG_VALUE_U32 || value.kind == DRIVER_C_PROG_VALUE_U64;
}

static int driver_c_prog_value_is_u64(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  return value.kind == DRIVER_C_PROG_VALUE_U64;
}

static int driver_c_prog_value_is_i64_family(DriverCProgValue value) {
  value = driver_c_prog_materialize(value);
  return value.kind == DRIVER_C_PROG_VALUE_I64 || value.kind == DRIVER_C_PROG_VALUE_U64;
}

static int driver_c_prog_binary_prefers_u64(DriverCProgValue lhs, DriverCProgValue rhs) {
  return driver_c_prog_value_is_u64(lhs) || driver_c_prog_value_is_u64(rhs);
}

static int driver_c_prog_binary_prefers_i64(DriverCProgValue lhs, DriverCProgValue rhs) {
  return driver_c_prog_value_is_i64_family(lhs) || driver_c_prog_value_is_i64_family(rhs);
}

static int driver_c_prog_binary_prefers_u32(DriverCProgValue lhs, DriverCProgValue rhs) {
  return driver_c_prog_value_is_u32_or_u64(lhs) || driver_c_prog_value_is_u32_or_u64(rhs);
}

static DriverCProgItem *driver_c_prog_find_item_by_id(DriverCProgRegistry *registry, int32_t item_id) {
  if (registry == NULL || item_id < 0 || item_id >= registry->items_by_id_len) {
    return NULL;
  }
  return registry->items_by_id[item_id];
}

static DriverCProgItem *driver_c_prog_find_item_by_label(DriverCProgRegistry *registry, const char *label) {
  int32_t i = 0;
  if (registry == NULL || label == NULL || label[0] == '\0') {
    return NULL;
  }
  for (i = 0; i < registry->item_count; ++i) {
    if (driver_c_prog_text_eq(registry->items[i].label, label)) {
      return &registry->items[i];
    }
  }
  return NULL;
}

static DriverCProgItem *driver_c_prog_find_entry_item(DriverCProgRegistry *registry) {
  DriverCProgItem *item = NULL;
  if (registry == NULL) {
    return NULL;
  }
  if (registry->entry_item_id >= 0) {
    item = driver_c_prog_find_item_by_id(registry, registry->entry_item_id);
    if (item != NULL) {
      return item;
    }
  }
  if (registry->entry_label != NULL && registry->entry_label[0] != '\0') {
    item = driver_c_prog_find_item_by_label(registry, registry->entry_label);
    if (item != NULL) {
      return item;
    }
  }
  return NULL;
}

static int driver_c_prog_is_routine_item(const DriverCProgItem *item) {
  if (item == NULL) return 0;
  return driver_c_prog_top_level_tag_is_routine(item->top_level_tag);
}

static DriverCProgItem *driver_c_prog_lookup_item_in_module(DriverCProgRegistry *registry,
                                                            const char *module_path,
                                                            const char *label) {
  DriverCProgItemLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || module_path == NULL || label == NULL ||
      registry->item_lookup_cap <= 0 || registry->item_lookup == NULL) {
    return NULL;
  }
  cache = registry->item_lookup;
  hash = driver_c_prog_hash_item_key(module_path, label);
  start = (int32_t)(hash & (uint64_t)(registry->item_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, module_path) &&
        driver_c_prog_text_eq(cache[slot].label, label)) {
      if (cache[slot].ambiguous) {
        char message[512];
        snprintf(message,
                 sizeof(message),
                 "driver_c program runtime: ambiguous visible item module=%s label=%s",
                 module_path != NULL ? module_path : "",
                 label != NULL ? label : "");
        driver_c_die(message);
      }
      return cache[slot].item;
    }
    slot = (slot + 1) & (registry->item_lookup_cap - 1);
    if (slot == start) {
      break;
    }
  }
  return NULL;
}

static DriverCProgItem *driver_c_prog_lookup_item_kind_in_module(DriverCProgRegistry *registry,
                                                                 const char *module_path,
                                                                 const char *label,
                                                                 const char *top_level_kind) {
  DriverCProgItemKindLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || module_path == NULL || label == NULL || top_level_kind == NULL ||
      registry->item_kind_lookup_cap <= 0 || registry->item_kind_lookup == NULL) {
    return NULL;
  }
  cache = registry->item_kind_lookup;
  hash = driver_c_prog_hash_item_kind_key(module_path, label, top_level_kind);
  start = (int32_t)(hash & (uint64_t)(registry->item_kind_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, module_path) &&
        driver_c_prog_text_eq(cache[slot].label, label) &&
        driver_c_prog_text_eq(cache[slot].top_level_kind, top_level_kind)) {
      if (cache[slot].ambiguous) {
        char message[512];
        snprintf(message,
                 sizeof(message),
                 "driver_c program runtime: ambiguous item kind module=%s label=%s kind=%s",
                 module_path != NULL ? module_path : "",
                 label != NULL ? label : "",
                 top_level_kind != NULL ? top_level_kind : "");
        driver_c_die(message);
      }
      return cache[slot].item;
    }
    slot = (slot + 1) & (registry->item_kind_lookup_cap - 1);
    if (slot == start) {
      break;
    }
  }
  return NULL;
}

static DriverCProgItem *driver_c_prog_lookup_import_item(DriverCProgRegistry *registry,
                                                         const char *owner_module,
                                                         const char *visible_name) {
  DriverCProgImportLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || owner_module == NULL || visible_name == NULL ||
      registry->import_lookup_cap <= 0 || registry->import_lookup == NULL) {
    return NULL;
  }
  cache = registry->import_lookup;
  hash = driver_c_prog_hash_import_key(owner_module, visible_name);
  start = (int32_t)(hash & (uint64_t)(registry->import_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, owner_module) &&
        driver_c_prog_text_eq(cache[slot].visible_name, visible_name)) {
      if (cache[slot].ambiguous) {
        char message[512];
        snprintf(message,
                 sizeof(message),
                 "driver_c program runtime: ambiguous import target module owner=%s visible=%s",
                 owner_module != NULL ? owner_module : "",
                 visible_name != NULL ? visible_name : "");
        driver_c_die(message);
      }
      return cache[slot].item;
    }
    slot = (slot + 1) & (registry->import_lookup_cap - 1);
    if (slot == start) {
      break;
    }
  }
  return NULL;
}

static DriverCProgItem *driver_c_prog_find_single_item_in_module(DriverCProgRegistry *registry,
                                                                 const char *module_path,
                                                                 const char *label) {
  DriverCProgItem *out = NULL;
  int32_t i = 0;
  if (registry == NULL || label == NULL) return NULL;
  if (module_path != NULL) {
    if (registry->item_lookup_cap > 0 && registry->item_lookup != NULL) {
      return driver_c_prog_lookup_item_in_module(registry, module_path, label);
    }
  }
  for (i = 0; i < registry->item_count; ++i) {
    DriverCProgItem *item = &registry->items[i];
    if (item->label == NULL || !driver_c_prog_text_eq(item->label, label)) {
      continue;
    }
    if (module_path != NULL && !driver_c_prog_text_eq(item->owner_module, module_path)) {
      continue;
    }
    if (item->top_level_tag == DRIVER_C_PROG_TOP_IMPORT) {
      continue;
    }
    if (out != NULL) {
      if (driver_c_prog_is_routine_item(out) && driver_c_prog_is_routine_item(item)) {
        continue;
      }
      char message[512];
      snprintf(message,
               sizeof(message),
               "driver_c program runtime: ambiguous visible item module=%s label=%s first_kind=%s second_kind=%s",
               module_path != NULL ? module_path : "",
               label != NULL ? label : "",
               out->top_level_kind != NULL ? out->top_level_kind : "",
               item->top_level_kind != NULL ? item->top_level_kind : "");
      driver_c_die(message);
    }
    out = item;
  }
  return out;
}

static DriverCProgItem *driver_c_prog_find_routine_item_in_module_by_arity(DriverCProgRegistry *registry,
                                                                            const char *module_path,
                                                                            const char *label,
                                                                            int32_t argc) {
  DriverCProgItem *out = NULL;
  int32_t i = 0;
  if (registry == NULL || module_path == NULL || label == NULL) return NULL;
  for (i = 0; i < registry->item_count; ++i) {
    DriverCProgItem *item = &registry->items[i];
    if (!driver_c_prog_is_routine_item(item)) {
      continue;
    }
    if (item->label == NULL || !driver_c_prog_text_eq(item->label, label)) {
      continue;
    }
    if (!driver_c_prog_text_eq(item->owner_module, module_path)) {
      continue;
    }
    if (item->param_count != argc) {
      continue;
    }
    if (out != NULL && out != item) {
      char message[512];
      snprintf(message,
               sizeof(message),
               "driver_c program runtime: ambiguous routine overload module=%s label=%s argc=%d",
               module_path,
               label,
               argc);
      driver_c_die(message);
    }
    out = item;
  }
  return out;
}

static DriverCProgItem *driver_c_prog_find_import_item_by_visible_name(DriverCProgRegistry *registry,
                                                                       const char *owner_module,
                                                                       const char *name) {
  int32_t i = 0;
  if (registry == NULL || owner_module == NULL || name == NULL) return NULL;
  if (registry->import_lookup_cap > 0 && registry->import_lookup != NULL) {
    return driver_c_prog_lookup_import_item(registry, owner_module, name);
  }
  for (i = 0; i < registry->item_count; ++i) {
    DriverCProgItem *item = &registry->items[i];
    char *visible = NULL;
    int match = 0;
    if (item->top_level_tag != DRIVER_C_PROG_TOP_IMPORT) {
      continue;
    }
    if (!driver_c_prog_text_eq(item->owner_module, owner_module)) {
      continue;
    }
    if (item->import_alias != NULL && item->import_alias[0] != '\0') {
      match = driver_c_prog_text_eq(item->import_alias, name);
    } else {
      visible = driver_c_prog_module_basename_dup(driver_c_prog_import_target_module_text(item));
      match = driver_c_prog_text_eq(visible, name);
      free(visible);
    }
    if (match) {
      return item;
    }
  }
  return NULL;
}

static DriverCProgItem *driver_c_prog_resolve_visible_item_uncached(DriverCProgRegistry *registry,
                                                                    const char *owner_module,
                                                                    const char *name) {
  DriverCProgItem *direct = driver_c_prog_find_single_item_in_module(registry, owner_module, name);
  int32_t i = 0;
  DriverCProgItem *found = NULL;
  if (direct != NULL) {
    return direct;
  }
  if (registry == NULL || owner_module == NULL || name == NULL) {
    return NULL;
  }
  for (i = 0; i < registry->item_count; ++i) {
    DriverCProgItem *import_item = &registry->items[i];
    DriverCProgItem *target = NULL;
    const char *target_module = NULL;
    if (import_item->top_level_tag != DRIVER_C_PROG_TOP_IMPORT) {
      continue;
    }
    if (!driver_c_prog_text_eq(import_item->owner_module, owner_module)) {
      continue;
    }
    target_module = driver_c_prog_import_target_module_text(import_item);
    target = driver_c_prog_find_single_item_in_module(registry, target_module, name);
    if (target == NULL) {
      continue;
    }
    if (found != NULL && found != target) {
      char message[1024];
      snprintf(message,
               sizeof(message),
               "driver_c program runtime: ambiguous imported visible item owner=%s name=%s first_module=%s second_module=%s",
               owner_module != NULL ? owner_module : "",
               name != NULL ? name : "",
               found->owner_module != NULL ? found->owner_module : "",
               target->owner_module != NULL ? target->owner_module : "");
      driver_c_die(message);
    }
    found = target;
  }
  return found;
}

static DriverCProgItem *driver_c_prog_resolve_visible_item(DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *name) {
  DriverCProgVisibleItemCacheEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  DriverCProgItem *resolved = NULL;
  if (registry == NULL || owner_module == NULL || name == NULL) {
    return driver_c_prog_resolve_visible_item_uncached(registry, owner_module, name);
  }
  if (registry->visible_item_cache_cap <= 0 || registry->visible_item_cache == NULL) {
    return driver_c_prog_resolve_visible_item_uncached(registry, owner_module, name);
  }
  cache = registry->visible_item_cache;
  hash = driver_c_prog_hash_visible_item_key(owner_module, name);
  start = (int32_t)(hash & (uint64_t)(registry->visible_item_cache_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, owner_module) &&
        driver_c_prog_text_eq(cache[slot].name, name)) {
      return cache[slot].item;
    }
    slot = (slot + 1) & (registry->visible_item_cache_cap - 1);
    if (slot == start) {
      return driver_c_prog_resolve_visible_item_uncached(registry, owner_module, name);
    }
  }
  resolved = driver_c_prog_resolve_visible_item_uncached(registry, owner_module, name);
  cache[slot].owner_module = owner_module;
  cache[slot].name = name;
  cache[slot].item = resolved;
  cache[slot].used = 1;
  return resolved;
}

static DriverCProgTypeDecl *driver_c_prog_lookup_visible_type_decl_cache(DriverCProgRegistry *registry,
                                                                         const char *owner_module,
                                                                         const char *type_text,
                                                                         int *out_missing) {
  DriverCProgVisibleTypeDeclCacheEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (out_missing != NULL) {
    *out_missing = 0;
  }
  if (registry == NULL || owner_module == NULL || type_text == NULL ||
      registry->visible_type_decl_cache_cap <= 0 || registry->visible_type_decl_cache == NULL) {
    return NULL;
  }
  cache = registry->visible_type_decl_cache;
  hash = driver_c_prog_hash_visible_type_decl_key(owner_module, type_text);
  start = (int32_t)(hash & (uint64_t)(registry->visible_type_decl_cache_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, owner_module) &&
        driver_c_prog_text_eq(cache[slot].type_text, type_text)) {
      if (out_missing != NULL) {
        *out_missing = cache[slot].missing;
      }
      return cache[slot].decl;
    }
    slot = (slot + 1) & (registry->visible_type_decl_cache_cap - 1);
    if (slot == start) {
      break;
    }
  }
  return NULL;
}

static void driver_c_prog_insert_visible_type_decl_cache(DriverCProgRegistry *registry,
                                                         const char *owner_module,
                                                         const char *type_text,
                                                         DriverCProgTypeDecl *decl) {
  DriverCProgVisibleTypeDeclCacheEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || owner_module == NULL || type_text == NULL ||
      registry->visible_type_decl_cache_cap <= 0 || registry->visible_type_decl_cache == NULL) {
    return;
  }
  cache = registry->visible_type_decl_cache;
  hash = driver_c_prog_hash_visible_type_decl_key(owner_module, type_text);
  start = (int32_t)(hash & (uint64_t)(registry->visible_type_decl_cache_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, owner_module) &&
        driver_c_prog_text_eq(cache[slot].type_text, type_text)) {
      return;
    }
    slot = (slot + 1) & (registry->visible_type_decl_cache_cap - 1);
    if (slot == start) {
      return;
    }
  }
  cache[slot].owner_module = owner_module;
  cache[slot].type_text = driver_c_dup_cstring(type_text);
  cache[slot].decl = decl;
  cache[slot].missing = decl == NULL ? 1 : 0;
  cache[slot].used = 1;
}

static DriverCProgZeroPlan *driver_c_prog_zero_plan_lookup(DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *inst_owner_module,
                                                           const char *type_text) {
  DriverCProgZeroPlanCacheEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || owner_module == NULL || inst_owner_module == NULL || type_text == NULL ||
      registry->zero_plan_cache_cap <= 0 || registry->zero_plan_cache == NULL) {
    return NULL;
  }
  cache = registry->zero_plan_cache;
  hash = driver_c_prog_hash_zero_plan_key(owner_module, inst_owner_module, type_text);
  start = (int32_t)(hash & (uint64_t)(registry->zero_plan_cache_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, owner_module) &&
        driver_c_prog_text_eq(cache[slot].inst_owner_module, inst_owner_module) &&
        driver_c_prog_text_eq(cache[slot].type_text, type_text)) {
      return cache[slot].plan;
    }
    slot = (slot + 1) & (registry->zero_plan_cache_cap - 1);
    if (slot == start) {
      break;
    }
  }
  return NULL;
}

static void driver_c_prog_zero_plan_insert(DriverCProgRegistry *registry,
                                           const char *owner_module,
                                           const char *inst_owner_module,
                                           const char *type_text,
                                           DriverCProgZeroPlan *plan) {
  DriverCProgZeroPlanCacheEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || owner_module == NULL || inst_owner_module == NULL || type_text == NULL ||
      plan == NULL || registry->zero_plan_cache_cap <= 0 || registry->zero_plan_cache == NULL) {
    return;
  }
  cache = registry->zero_plan_cache;
  hash = driver_c_prog_hash_zero_plan_key(owner_module, inst_owner_module, type_text);
  start = (int32_t)(hash & (uint64_t)(registry->zero_plan_cache_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, owner_module) &&
        driver_c_prog_text_eq(cache[slot].inst_owner_module, inst_owner_module) &&
        driver_c_prog_text_eq(cache[slot].type_text, type_text)) {
      return;
    }
    slot = (slot + 1) & (registry->zero_plan_cache_cap - 1);
    if (slot == start) {
      return;
    }
  }
  cache[slot].owner_module = owner_module;
  cache[slot].inst_owner_module = inst_owner_module;
  cache[slot].type_text = driver_c_dup_cstring(type_text);
  cache[slot].plan = plan;
  cache[slot].used = 1;
}

static const char **driver_c_prog_intern_routine_type_args(DriverCProgRegistry *registry,
                                                           int32_t item_id,
                                                           const char **type_arg_texts,
                                                           int32_t type_arg_count) {
  DriverCProgRoutineTypeArgsLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  int32_t i = 0;
  char **owned = NULL;
  if (type_arg_count <= 0) {
    return NULL;
  }
  if (registry == NULL ||
      registry->routine_type_args_lookup_cap <= 0 ||
      registry->routine_type_args_lookup == NULL) {
    driver_c_die("driver_c program runtime: routine type args cache unavailable");
  }
  cache = registry->routine_type_args_lookup;
  hash = driver_c_prog_hash_routine_type_args_key(item_id, type_arg_texts, type_arg_count);
  start = (int32_t)(hash & (uint64_t)(registry->routine_type_args_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (cache[slot].item_id == item_id &&
        cache[slot].type_arg_count == type_arg_count) {
      int same = 1;
      for (i = 0; i < type_arg_count; ++i) {
        if (!driver_c_prog_text_eq(cache[slot].type_arg_texts[i], type_arg_texts[i])) {
          same = 0;
          break;
        }
      }
      if (same) {
        return (const char **)cache[slot].type_arg_texts;
      }
    }
    slot = (slot + 1) & (registry->routine_type_args_lookup_cap - 1);
    if (slot == start) {
      driver_c_die("driver_c program runtime: routine type args cache full");
    }
  }
  owned = (char **)driver_c_prog_xcalloc((size_t)type_arg_count, sizeof(char *));
  for (i = 0; i < type_arg_count; ++i) {
    owned[i] = driver_c_dup_cstring(type_arg_texts[i] != NULL ? type_arg_texts[i] : "");
  }
  cache[slot].item_id = item_id;
  cache[slot].type_arg_count = type_arg_count;
  cache[slot].type_arg_texts = owned;
  cache[slot].used = 1;
  return (const char **)cache[slot].type_arg_texts;
}

static int32_t driver_c_prog_find_type_param_index(const DriverCProgItem *item, const char *name) {
  int32_t i = 0;
  if (item == NULL || name == NULL) {
    return -1;
  }
  for (i = 0; i < item->type_param_count; ++i) {
    if (driver_c_prog_text_eq(item->type_param_names[i], name)) {
      return i;
    }
  }
  return -1;
}

static void driver_c_prog_parse_signature_var_flags(DriverCProgItem *item) {
  const char *sig = NULL;
  const char *open = NULL;
  const char *cursor = NULL;
  int depth = 0;
  int32_t param_idx = 0;
  int found_payload_flags = 0;
  if (item == NULL || item->param_count <= 0) {
    return;
  }
  item->param_is_var = (int32_t *)driver_c_prog_xcalloc((size_t)item->param_count, sizeof(int32_t));
  for (param_idx = 0; param_idx < item->param_count; ++param_idx) {
    char key[64];
    char *kind = NULL;
    snprintf(key, sizeof(key), "param_kind.%d", param_idx);
    kind = driver_c_find_line_value_dup(item->payload_text, key);
    if (kind != NULL && kind[0] != '\0') {
      found_payload_flags = 1;
      if (driver_c_prog_text_eq(kind, "var_param")) {
        item->param_is_var[param_idx] = 1;
      }
    }
    free(kind);
  }
  if (found_payload_flags) {
    return;
  }
  if (item->signature_line_count <= 0) {
    return;
  }
  param_idx = 0;
  sig = item->signature_lines[item->signature_line_count - 1];
  if (sig == NULL) {
    return;
  }
  open = strchr(sig, '(');
  if (open == NULL) {
    return;
  }
  cursor = open + 1;
  while (*cursor != '\0' && param_idx < item->param_count) {
    const char *part_start = cursor;
    const char *part_stop = cursor;
    char *part = NULL;
    if (*cursor == ')') {
      break;
    }
    depth = 0;
    while (*part_stop != '\0') {
      if (*part_stop == '[' || *part_stop == '(') depth = depth + 1;
      else if (*part_stop == ']' || *part_stop == ')') {
        if (depth == 0 && *part_stop == ')') break;
        if (depth > 0) depth = depth - 1;
      } else if (*part_stop == ',' && depth == 0) {
        break;
      }
      part_stop = part_stop + 1;
    }
    part = driver_c_dup_range(part_start, part_stop);
    {
      char *trimmed = driver_c_prog_trim_dup(part);
      free(part);
      part = trimmed;
    }
    if (part != NULL && strstr(part, ": var ") != NULL) {
      item->param_is_var[param_idx] = 1;
    }
    free(part);
    param_idx = param_idx + 1;
    cursor = *part_stop == ',' ? part_stop + 1 : part_stop;
    while (*cursor == ' ') cursor = cursor + 1;
    if (*cursor == ')') {
      break;
    }
  }
}

static void driver_c_prog_item_append_type_param_name(DriverCProgItem *item, char *name) {
  int32_t next_cap = 0;
  if (item == NULL || name == NULL || name[0] == '\0') {
    free(name);
    return;
  }
  if (item->type_param_count >= item->type_param_cap) {
    next_cap = item->type_param_cap > 0 ? item->type_param_cap * 2 : 4;
    item->type_param_names =
        (char **)realloc(item->type_param_names, (size_t)next_cap * sizeof(char *));
    if (item->type_param_names == NULL) {
      driver_c_die("driver_c program runtime: alloc type param names failed");
    }
    item->type_param_cap = next_cap;
  }
  item->type_param_names[item->type_param_count] = name;
  item->type_param_count = item->type_param_count + 1;
}

static char *driver_c_prog_type_param_name_dup(const char *part) {
  const char *cursor = NULL;
  char *trimmed = NULL;
  char *name = NULL;
  if (part == NULL) {
    return driver_c_dup_cstring("");
  }
  trimmed = driver_c_prog_trim_dup(part);
  if (trimmed == NULL) {
    return driver_c_dup_cstring("");
  }
  cursor = trimmed;
  while (*cursor != '\0' &&
         *cursor != ':' &&
         *cursor != ' ' &&
         *cursor != '\t' &&
         *cursor != '\n' &&
         *cursor != '\r') {
    cursor = cursor + 1;
  }
  name = driver_c_dup_range(trimmed, cursor);
  free(trimmed);
  return name;
}

static void driver_c_prog_parse_signature_type_params(DriverCProgItem *item) {
  const char *sig = NULL;
  const char *label_pos = NULL;
  const char *cursor = NULL;
  int depth = 0;
  if (item == NULL ||
      item->label == NULL ||
      item->signature_line_count <= 0 ||
      item->signature_lines == NULL ||
      item->signature_lines[item->signature_line_count - 1] == NULL) {
    return;
  }
  sig = item->signature_lines[item->signature_line_count - 1];
  label_pos = strstr(sig, item->label);
  if (label_pos == NULL) {
    return;
  }
  cursor = label_pos + strlen(item->label);
  while (*cursor == ' ' || *cursor == '\t') {
    cursor = cursor + 1;
  }
  if (*cursor != '[') {
    return;
  }
  cursor = cursor + 1;
  while (*cursor != '\0') {
    const char *part_start = cursor;
    const char *part_stop = cursor;
    char *part = NULL;
    char *name = NULL;
    if (*cursor == ']') {
      break;
    }
    depth = 0;
    while (*part_stop != '\0') {
      if ((*part_stop == '[' || *part_stop == '(') && depth >= 0) {
        depth = depth + 1;
      } else if (*part_stop == ']' || *part_stop == ')') {
        if (depth == 0 && *part_stop == ']') {
          break;
        }
        if (depth > 0) {
          depth = depth - 1;
        }
      } else if (*part_stop == ',' && depth == 0) {
        break;
      }
      part_stop = part_stop + 1;
    }
    part = driver_c_dup_range(part_start, part_stop);
    name = driver_c_prog_type_param_name_dup(part);
    free(part);
    driver_c_prog_item_append_type_param_name(item, name);
    cursor = *part_stop == ',' ? part_stop + 1 : part_stop;
    while (*cursor == ' ' || *cursor == '\t') {
      cursor = cursor + 1;
    }
    if (*cursor == ']') {
      break;
    }
  }
}

static char *driver_c_prog_normalize_param_type_text_dup(const char *type_text) {
  char *out = driver_c_prog_trim_dup(type_text != NULL ? type_text : "");
  while (driver_c_prog_text_starts_with(out, "var ") ||
         driver_c_prog_text_starts_with(out, "sink ") ||
         driver_c_prog_text_starts_with(out, "lent ")) {
    int skip = driver_c_prog_text_starts_with(out, "var ") ? 4
             : driver_c_prog_text_starts_with(out, "sink ") ? 5
             : 5;
    char *next = driver_c_prog_trim_dup(out + skip);
    free(out);
    out = next;
  }
  return out;
}

static void driver_c_prog_parse_signature_param_types(DriverCProgItem *item) {
  const char *sig = NULL;
  const char *open = NULL;
  const char *cursor = NULL;
  int depth = 0;
  int32_t param_idx = 0;
  if (item == NULL || item->param_count <= 0 || item->signature_line_count <= 0) {
    return;
  }
  item->param_type_texts = (char **)driver_c_prog_xcalloc((size_t)item->param_count, sizeof(char *));
  sig = item->signature_lines[item->signature_line_count - 1];
  if (sig == NULL) {
    return;
  }
  open = strchr(sig, '(');
  if (open == NULL) {
    return;
  }
  cursor = open + 1;
  while (*cursor != '\0' && param_idx < item->param_count) {
    const char *part_start = cursor;
    const char *part_stop = cursor;
    char *part = NULL;
    char *type_text = NULL;
    char *colon = NULL;
    if (*cursor == ')') {
      break;
    }
    depth = 0;
    while (*part_stop != '\0') {
      if (*part_stop == '[' || *part_stop == '(') depth = depth + 1;
      else if (*part_stop == ']' || *part_stop == ')') {
        if (depth == 0 && *part_stop == ')') break;
        if (depth > 0) depth = depth - 1;
      } else if (*part_stop == ',' && depth == 0) {
        break;
      }
      part_stop = part_stop + 1;
    }
    part = driver_c_dup_range(part_start, part_stop);
    colon = strchr(part, ':');
    if (colon != NULL) {
      type_text = driver_c_prog_normalize_param_type_text_dup(colon + 1);
    } else {
      type_text = driver_c_dup_cstring("");
    }
    item->param_type_texts[param_idx] = type_text;
    free(part);
    param_idx = param_idx + 1;
    cursor = *part_stop == ',' ? part_stop + 1 : part_stop;
    while (*cursor == ' ' || *cursor == '\t') cursor = cursor + 1;
    if (*cursor == ')') {
      break;
    }
  }
}

static void driver_c_prog_parse_item_payload(DriverCProgItem *item) {
  int32_t i = 0;
  if (item == NULL || item->payload_text == NULL) {
    return;
  }
  item->return_type = driver_c_find_line_value_dup(item->payload_text, "return_type");
  item->body_text = driver_c_prog_hex_decode_text_dup(driver_c_find_line_value_dup(item->payload_text, "body_text_hex"));
  item->ast_ready = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(item->payload_text, "ast_ready"));
  item->param_count = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(item->payload_text, "param_count"));
  item->local_count = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(item->payload_text, "local_count"));
  item->stmt_count = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(item->payload_text, "stmt_count"));
  item->expr_count = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(item->payload_text, "expr_count"));
  item->signature_line_count = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(item->payload_text,
                                                                                             "signature_line_count"));
  if (item->signature_line_count > 0) {
    item->signature_lines = (char **)driver_c_prog_xcalloc((size_t)item->signature_line_count, sizeof(char *));
    for (i = 0; i < item->signature_line_count; ++i) {
      char key[64];
      snprintf(key, sizeof(key), "signature_line.%d", i);
      item->signature_lines[i] = driver_c_find_line_value_dup(item->payload_text, key);
    }
  }
  if (item->local_count > 0) {
    item->local_type_texts = (char **)driver_c_prog_xcalloc((size_t)item->local_count, sizeof(char *));
    for (i = 0; i < item->local_count; ++i) {
      char key[64];
      snprintf(key, sizeof(key), "local_type_hex.%d", i);
      item->local_type_texts[i] =
          driver_c_prog_hex_decode_text_dup(driver_c_find_line_value_dup(item->payload_text, key));
    }
  }
  driver_c_prog_parse_signature_var_flags(item);
  driver_c_prog_parse_signature_param_types(item);
  driver_c_prog_parse_signature_type_params(item);
  item->exec_op_count = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(item->payload_text, "exec_op_count"));
  if (item->exec_op_count > 0) {
    item->ops = (DriverCProgOp *)driver_c_prog_xcalloc((size_t)item->exec_op_count, sizeof(DriverCProgOp));
    for (i = 0; i < item->exec_op_count; ++i) {
      char key[64];
      char *line = NULL;
      char *parts[6];
      int part_count = 0;
      char *cursor = NULL;
      snprintf(key, sizeof(key), "op.%d", i);
      line = driver_c_find_line_value_dup(item->payload_text, key);
      cursor = line;
      while (cursor != NULL && part_count < 6) {
        char *sep = strchr(cursor, '|');
        if (sep != NULL) {
          *sep = '\0';
          parts[part_count++] = cursor;
          cursor = sep + 1;
        } else {
          parts[part_count++] = cursor;
          cursor = NULL;
        }
      }
      if (part_count != 6) {
        free(line);
        driver_c_die("driver_c program runtime: malformed exec op");
      }
      item->ops[i].kind = driver_c_dup_cstring(parts[0]);
      item->ops[i].primary = driver_c_prog_hex_decode_text_dup(parts[1]);
      item->ops[i].secondary = driver_c_prog_hex_decode_text_dup(parts[2]);
      item->ops[i].kind_tag = driver_c_prog_op_tag_from_text(item->ops[i].kind);
      item->ops[i].arg0 = driver_c_prog_parse_i32_or_zero(parts[3]);
      item->ops[i].arg1 = driver_c_prog_parse_i32_or_zero(parts[4]);
      item->ops[i].arg2 = driver_c_prog_parse_i32_or_zero(parts[5]);
      free(line);
    }
  }
}

static char *driver_c_prog_trim_inline_comment_dup(const char *text) {
  const char *start = text != NULL ? text : "";
  const char *stop = start + strlen(start);
  const char *cursor = start;
  char *raw = NULL;
  char *trimmed = NULL;
  while (cursor < stop) {
    if (*cursor == '#') {
      stop = cursor;
      break;
    }
    cursor = cursor + 1;
  }
  raw = driver_c_dup_range(start, stop);
  trimmed = driver_c_prog_trim_dup(raw);
  free(raw);
  return trimmed;
}

static int driver_c_prog_is_name_start(char ch) {
  return (ch >= 'a' && ch <= 'z') ||
         (ch >= 'A' && ch <= 'Z') ||
         ch == '_';
}

static int driver_c_prog_is_name_continue(char ch) {
  return driver_c_prog_is_name_start(ch) || (ch >= '0' && ch <= '9');
}

static int32_t driver_c_prog_line_indent_width(const char *raw_line) {
  int32_t out = 0;
  if (raw_line == NULL) return 0;
  while (raw_line[out] == ' ' || raw_line[out] == '\t') {
    out = out + 1;
  }
  return out;
}

static char **driver_c_prog_split_lines_dup(const char *text, int32_t *out_count) {
  int32_t count = 0;
  int32_t i = 0;
  int32_t start = 0;
  char **out = NULL;
  if (out_count != NULL) {
    *out_count = 0;
  }
  if (text == NULL || text[0] == '\0') {
    return NULL;
  }
  for (i = 0; ; ++i) {
    if (text[i] == '\0' || text[i] == '\n') {
      count = count + 1;
      if (text[i] == '\0') {
        break;
      }
    }
  }
  out = (char **)driver_c_prog_xcalloc((size_t)count, sizeof(char *));
  count = 0;
  for (i = 0; ; ++i) {
    if (text[i] == '\0' || text[i] == '\n') {
      out[count++] = driver_c_dup_range(text + start, text + i);
      start = i + 1;
      if (text[i] == '\0') {
        break;
      }
    }
  }
  if (out_count != NULL) {
    *out_count = count;
  }
  return out;
}

static void driver_c_prog_free_string_array(char **values, int32_t count) {
  int32_t i = 0;
  if (values == NULL) return;
  for (i = 0; i < count; ++i) {
    free(values[i]);
  }
  free(values);
}

static int driver_c_prog_parse_i32_text_strict(const char *text, int32_t *out_value) {
  char *trimmed = driver_c_prog_trim_dup(text);
  char *end = NULL;
  long long value = 0;
  const char *start = trimmed;
  int base = 10;
  int ok = 0;
  if (trimmed[0] == '+' || trimmed[0] == '-') {
    start = trimmed + 1;
  }
  if ((start[0] == '0') && (start[1] == 'x' || start[1] == 'X')) {
    base = 16;
  }
  if (trimmed[0] != '\0') {
    errno = 0;
    value = strtoll(trimmed, &end, base);
    ok = errno == 0 &&
         end != trimmed &&
         end != NULL &&
         *end == '\0' &&
         value >= (long long)INT32_MIN &&
         value <= (long long)INT32_MAX;
    if (ok && out_value != NULL) {
      *out_value = (int32_t)value;
    }
  }
  free(trimmed);
  return ok;
}

static char *driver_c_prog_leading_name_token_dup(const char *text) {
  int32_t end = 0;
  if (text == NULL) {
    return driver_c_dup_cstring("");
  }
  while (text[end] != '\0' && driver_c_prog_is_name_continue(text[end])) {
    end = end + 1;
  }
  return driver_c_dup_range(text, text + end);
}

static char *driver_c_prog_type_decl_name_from_line_dup(const char *raw_line) {
  char *line = driver_c_prog_trim_inline_comment_dup(raw_line);
  char *eq = NULL;
  char *head = NULL;
  char *name = NULL;
  if (line[0] == '\0') {
    free(line);
    return driver_c_dup_cstring("");
  }
  eq = strchr(line, '=');
  if (eq == NULL) {
    free(line);
    return driver_c_dup_cstring("");
  }
  head = driver_c_prog_trim_dup(driver_c_dup_range(line, eq));
  name = driver_c_prog_leading_name_token_dup(head);
  free(head);
  free(line);
  return name;
}

static char *driver_c_prog_type_alias_target_from_line_dup(const char *raw_line, const char *type_name) {
  char **lines = NULL;
  int32_t line_count = 0;
  int32_t base_indent = -1;
  int32_t i = 0;
  lines = driver_c_prog_split_lines_dup(raw_line, &line_count);
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    int32_t indent = driver_c_prog_line_indent_width(lines[i]);
    if (line[0] != '\0' && (base_indent < 0 || indent < base_indent)) {
      base_indent = indent;
    }
    free(line);
  }
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    char *decl_name = NULL;
    char *eq = NULL;
    if (line[0] == '\0' || driver_c_prog_line_indent_width(lines[i]) != base_indent) {
      free(line);
      continue;
    }
    decl_name = driver_c_prog_type_decl_name_from_line_dup(lines[i]);
    if (!driver_c_prog_text_eq(decl_name, type_name)) {
      free(decl_name);
      free(line);
      continue;
    }
    free(decl_name);
    eq = strchr(line, '=');
    if (eq == NULL) {
      free(line);
      break;
    }
    {
      char *out = driver_c_prog_trim_dup(eq + 1);
      free(line);
      driver_c_prog_free_string_array(lines, line_count);
      return out;
    }
  }
  driver_c_prog_free_string_array(lines, line_count);
  return driver_c_dup_cstring("");
}

static char *driver_c_prog_transparent_alias_target_dup(const char *alias_target) {
  char *text = driver_c_prog_trim_dup(alias_target);
  int opaque = 0;
  if (text[0] == '\0') {
    return text;
  }
  opaque = driver_c_prog_text_eq(text, "object") ||
           driver_c_prog_text_eq(text, "enum") ||
           driver_c_prog_text_eq(text, "tuple") ||
           driver_c_prog_text_eq(text, "distinct") ||
           driver_c_prog_text_starts_with(text, "object ") ||
           driver_c_prog_text_starts_with(text, "enum ") ||
           driver_c_prog_text_starts_with(text, "tuple ") ||
           driver_c_prog_text_starts_with(text, "distinct ") ||
           driver_c_prog_text_starts_with(text, "ref object") ||
           driver_c_prog_text_starts_with(text, "ptr object");
  if (opaque) {
    free(text);
    return driver_c_dup_cstring("");
  }
  return text;
}

static char *driver_c_prog_type_field_name_from_line_dup(const char *raw_line) {
  char *line = driver_c_prog_trim_inline_comment_dup(raw_line);
  char *colon = strchr(line, ':');
  char *name = NULL;
  if (colon == NULL || colon == line) {
    free(line);
    return driver_c_dup_cstring("");
  }
  *colon = '\0';
  name = driver_c_prog_leading_name_token_dup(driver_c_prog_trim_dup(line));
  free(line);
  return name;
}

static char *driver_c_prog_type_field_type_from_line_dup(const char *raw_line, const char *field_name) {
  char *line = driver_c_prog_trim_inline_comment_dup(raw_line);
  char *colon = strchr(line, ':');
  char *name = NULL;
  char *type_text = NULL;
  char *eq = NULL;
  if (colon == NULL || colon == line) {
    free(line);
    return driver_c_dup_cstring("");
  }
  *colon = '\0';
  name = driver_c_prog_leading_name_token_dup(driver_c_prog_trim_dup(line));
  if (!driver_c_prog_text_eq(name, field_name)) {
    free(name);
    free(line);
    return driver_c_dup_cstring("");
  }
  type_text = driver_c_prog_trim_dup(colon + 1);
  eq = strchr(type_text, '=');
  if (eq != NULL) {
    char *trimmed = driver_c_prog_trim_dup(driver_c_dup_range(type_text, eq));
    free(type_text);
    type_text = trimmed;
  }
  free(name);
  free(line);
  return type_text;
}

static int driver_c_prog_type_text_split_suffix_ctor(const char *type_text,
                                                     char **out_base,
                                                     char **out_arg) {
  char *text = driver_c_prog_trim_dup(type_text);
  size_t n = strlen(text);
  if (out_base != NULL) *out_base = driver_c_dup_cstring("");
  if (out_arg != NULL) *out_arg = driver_c_dup_cstring("");
  if (n > 2 && strcmp(text + n - 2, "[]") == 0) {
    if (out_base != NULL) {
      free(*out_base);
      *out_base = driver_c_dup_cstring("[]");
    }
    if (out_arg != NULL) {
      free(*out_arg);
      *out_arg = driver_c_prog_trim_dup(driver_c_dup_range(text, text + n - 2));
    }
    free(text);
    return 1;
  }
  if (n > 1 && text[n - 1] == '*') {
    if (out_base != NULL) {
      free(*out_base);
      *out_base = driver_c_dup_cstring("*");
    }
    if (out_arg != NULL) {
      free(*out_arg);
      *out_arg = driver_c_prog_trim_dup(driver_c_dup_range(text, text + n - 1));
    }
    free(text);
    return 1;
  }
  free(text);
  return 0;
}

static int driver_c_prog_type_text_split_ctor(const char *type_text,
                                              char **out_base,
                                              char **out_args_text) {
  char *text = driver_c_prog_trim_dup(type_text);
  char *open = strchr(text, '[');
  int depth = 0;
  int i = 0;
  if (out_base != NULL) *out_base = driver_c_dup_cstring("");
  if (out_args_text != NULL) *out_args_text = driver_c_dup_cstring("");
  if (open == NULL || open == text) {
    free(text);
    return 0;
  }
  for (i = (int)(open - text); text[i] != '\0'; ++i) {
    if (text[i] == '[') {
      depth = depth + 1;
    } else if (text[i] == ']') {
      depth = depth - 1;
      if (depth == 0) {
        if (text[i + 1] != '\0') {
          free(text);
          return 0;
        }
        if (out_base != NULL) {
          free(*out_base);
          *out_base = driver_c_prog_trim_dup(driver_c_dup_range(text, open));
        }
        if (out_args_text != NULL) {
          free(*out_args_text);
          *out_args_text = driver_c_prog_trim_dup(driver_c_dup_range(open + 1, text + i));
        }
        free(text);
        return 1;
      }
    }
  }
  free(text);
  return 0;
}

static int driver_c_prog_type_text_split_fn(const char *type_text,
                                            char **out_params_text,
                                            char **out_return_text) {
  char *text = driver_c_prog_trim_dup(type_text);
  const char *cursor = NULL;
  const char *open = NULL;
  const char *close = NULL;
  int bracket_depth = 0;
  int paren_depth = 0;
  if (out_params_text != NULL) *out_params_text = driver_c_dup_cstring("");
  if (out_return_text != NULL) *out_return_text = driver_c_dup_cstring("");
  if (!driver_c_prog_text_starts_with(text, "fn")) {
    free(text);
    return 0;
  }
  cursor = text + 2;
  while (*cursor == ' ' || *cursor == '\t') {
    cursor = cursor + 1;
  }
  if (*cursor != '(') {
    free(text);
    return 0;
  }
  open = cursor;
  close = NULL;
  bracket_depth = 0;
  paren_depth = 0;
  while (*cursor != '\0') {
    if (*cursor == '[') {
      bracket_depth = bracket_depth + 1;
    } else if (*cursor == ']') {
      bracket_depth = bracket_depth - 1;
    } else if (*cursor == '(') {
      paren_depth = paren_depth + 1;
    } else if (*cursor == ')') {
      paren_depth = paren_depth - 1;
      if (paren_depth == 0 && bracket_depth == 0) {
        close = cursor;
        break;
      }
    }
    cursor = cursor + 1;
  }
  if (close == NULL) {
    free(text);
    return 0;
  }
  cursor = close + 1;
  while (*cursor == ' ' || *cursor == '\t') {
    cursor = cursor + 1;
  }
  if (*cursor != ':') {
    free(text);
    return 0;
  }
  cursor = cursor + 1;
  while (*cursor == ' ' || *cursor == '\t') {
    cursor = cursor + 1;
  }
  if (*cursor == '\0') {
    free(text);
    return 0;
  }
  if (out_params_text != NULL) {
    free(*out_params_text);
    *out_params_text = driver_c_prog_trim_dup(driver_c_dup_range(open + 1, close));
  }
  if (out_return_text != NULL) {
    free(*out_return_text);
    *out_return_text = driver_c_prog_trim_dup(cursor);
  }
  free(text);
  return 1;
}

static char **driver_c_prog_split_type_args_dup(const char *text, int32_t *out_count) {
  int32_t count = 0;
  int32_t start = 0;
  int32_t i = 0;
  int32_t bracket_depth = 0;
  int32_t paren_depth = 0;
  char **out = NULL;
  if (out_count != NULL) *out_count = 0;
  if (text == NULL || text[0] == '\0') {
    return NULL;
  }
  for (i = 0;; ++i) {
    char ch = text[i];
    if (ch == '[') bracket_depth = bracket_depth + 1;
    else if (ch == ']') bracket_depth = bracket_depth - 1;
    else if (ch == '(') paren_depth = paren_depth + 1;
    else if (ch == ')') paren_depth = paren_depth - 1;
    if ((ch == ',' && bracket_depth == 0 && paren_depth == 0) || ch == '\0') {
      count = count + 1;
      if (ch == '\0') break;
    }
  }
  out = (char **)driver_c_prog_xcalloc((size_t)count, sizeof(char *));
  count = 0;
  bracket_depth = 0;
  paren_depth = 0;
  for (i = 0;; ++i) {
    char ch = text[i];
    if (ch == '[') bracket_depth = bracket_depth + 1;
    else if (ch == ']') bracket_depth = bracket_depth - 1;
    else if (ch == '(') paren_depth = paren_depth + 1;
    else if (ch == ')') paren_depth = paren_depth - 1;
    if ((ch == ',' && bracket_depth == 0 && paren_depth == 0) || ch == '\0') {
      out[count++] = driver_c_prog_trim_dup(driver_c_dup_range(text + start, text + i));
      start = i + 1;
      if (ch == '\0') break;
    }
  }
  if (out_count != NULL) *out_count = count;
  return out;
}

static char *driver_c_prog_function_param_type_text_dup(const char *text) {
  char *part = driver_c_prog_trim_dup(text != NULL ? text : "");
  const char *cursor = part;
  const char *colon = NULL;
  int bracket_depth = 0;
  int paren_depth = 0;
  while (*cursor != '\0') {
    if (*cursor == '[') {
      bracket_depth = bracket_depth + 1;
    } else if (*cursor == ']') {
      bracket_depth = bracket_depth - 1;
    } else if (*cursor == '(') {
      paren_depth = paren_depth + 1;
    } else if (*cursor == ')') {
      paren_depth = paren_depth - 1;
    } else if (*cursor == ':' && bracket_depth == 0 && paren_depth == 0) {
      colon = cursor;
      break;
    }
    cursor = cursor + 1;
  }
  if (colon != NULL) {
    char *out = driver_c_prog_normalize_param_type_text_dup(colon + 1);
    free(part);
    return out;
  }
  return part;
}

static char *driver_c_prog_join_function_type_dup(char **param_types,
                                                  int32_t param_count,
                                                  const char *return_type) {
  size_t cap = 8u + strlen(return_type != NULL ? return_type : "");
  size_t len = 0;
  int32_t i = 0;
  char *out = NULL;
  for (i = 0; i < param_count; ++i) {
    cap += strlen(param_types[i] != NULL ? param_types[i] : "") + 2u;
  }
  out = (char *)driver_c_prog_xcalloc(cap + 1u, 1u);
  memcpy(out + len, "fn (", 4u);
  len += 4u;
  for (i = 0; i < param_count; ++i) {
    const char *part = param_types[i] != NULL ? param_types[i] : "";
    size_t part_len = strlen(part);
    if (i > 0) {
      memcpy(out + len, ", ", 2u);
      len += 2u;
    }
    if (part_len > 0) {
      memcpy(out + len, part, part_len);
      len += part_len;
    }
  }
  memcpy(out + len, "): ", 3u);
  len += 3u;
  if (return_type != NULL && return_type[0] != '\0') {
    size_t return_len = strlen(return_type);
    memcpy(out + len, return_type, return_len);
    len += return_len;
  }
  out[len] = '\0';
  return out;
}

static int driver_c_prog_type_alias_is_enum(const char *alias_target) {
  return driver_c_prog_text_eq(alias_target, "enum") ||
         driver_c_prog_text_starts_with(alias_target, "enum ");
}

static DriverCProgTypeDecl *driver_c_prog_registry_push_type_decl(DriverCProgRegistry *registry) {
  DriverCProgTypeDecl *decl = NULL;
  if (registry->type_decl_count >= registry->type_decl_cap) {
    int32_t next_cap = registry->type_decl_cap > 0 ? registry->type_decl_cap * 2 : 16;
    registry->type_decls =
        (DriverCProgTypeDecl *)driver_c_prog_xrealloc(registry->type_decls,
                                                      sizeof(DriverCProgTypeDecl) * (size_t)next_cap);
    memset(registry->type_decls + registry->type_decl_cap,
           0,
           sizeof(DriverCProgTypeDecl) * (size_t)(next_cap - registry->type_decl_cap));
    registry->type_decl_cap = next_cap;
  }
  decl = &registry->type_decls[registry->type_decl_count++];
  memset(decl, 0, sizeof(*decl));
  return decl;
}

static void driver_c_prog_type_decl_append_field(DriverCProgTypeDecl *decl,
                                                 char *field_name,
                                                 char *field_type) {
  if (decl == NULL || field_name == NULL || field_type == NULL) {
    free(field_name);
    free(field_type);
    return;
  }
  if (decl->field_count >= decl->field_cap) {
    int32_t next_cap = decl->field_cap > 0 ? decl->field_cap * 2 : 4;
    decl->field_names =
        (char **)driver_c_prog_xrealloc(decl->field_names, sizeof(char *) * (size_t)next_cap);
    decl->field_types =
        (char **)driver_c_prog_xrealloc(decl->field_types, sizeof(char *) * (size_t)next_cap);
    decl->field_cap = next_cap;
  }
  decl->field_names[decl->field_count] = field_name;
  decl->field_types[decl->field_count] = field_type;
  decl->field_count = decl->field_count + 1;
  if (decl->field_lookup_indices != NULL) {
    free(decl->field_lookup_indices);
    decl->field_lookup_indices = NULL;
    decl->field_lookup_cap = 0;
  }
}

static void driver_c_prog_type_decl_ensure_field_lookup(DriverCProgTypeDecl *decl) {
  int32_t i = 0;
  if (decl == NULL || decl->field_count <= 0) {
    return;
  }
  if (decl->field_lookup_indices != NULL && decl->field_lookup_cap > 0) {
    return;
  }
  decl->field_lookup_cap = driver_c_prog_record_lookup_cap_for_count(decl->field_count);
  decl->field_lookup_indices =
      (int32_t *)driver_c_prog_xcalloc((size_t)decl->field_lookup_cap, sizeof(int32_t));
  driver_c_prog_field_lookup_reset(decl->field_lookup_indices, decl->field_lookup_cap);
  for (i = 0; i < decl->field_count; ++i) {
    driver_c_prog_field_lookup_insert(decl->field_lookup_indices,
                                      decl->field_lookup_cap,
                                      decl->field_names,
                                      decl->field_count,
                                      i);
  }
}

static int32_t driver_c_prog_type_decl_field_index(DriverCProgTypeDecl *decl,
                                                   const char *field_name) {
  if (decl == NULL || field_name == NULL) {
    return -1;
  }
  driver_c_prog_type_decl_ensure_field_lookup(decl);
  return driver_c_prog_field_lookup_find(decl->field_lookup_indices,
                                         decl->field_lookup_cap,
                                         decl->field_names,
                                         decl->field_count,
                                         field_name);
}

static void driver_c_prog_record_init_from_decl(DriverCProgRecord *record,
                                                DriverCProgTypeDecl *decl) {
  int32_t i = 0;
  if (record == NULL || decl == NULL || decl->field_count <= 0) {
    return;
  }
  driver_c_prog_type_decl_ensure_field_lookup(decl);
  driver_c_prog_record_ensure_value_cap(record, decl->field_count);
  driver_c_prog_record_set_shared_names(record, decl->field_names);
  for (i = 0; i < decl->field_count; ++i) {
    record->values[i] = driver_c_prog_value_nil();
  }
  record->len = decl->field_count;
  if (decl->field_lookup_indices != NULL && decl->field_lookup_cap > 0) {
    driver_c_prog_record_set_shared_lookup(record, decl->field_lookup_indices, decl->field_lookup_cap);
  } else {
    driver_c_prog_record_lookup_rebuild(record);
  }
}

static int driver_c_prog_parse_first_enum_ordinal_from_text(const char *text,
                                                            int32_t *out_ordinal) {
  char **parts = NULL;
  int32_t part_count = 0;
  int32_t ordinal = 0;
  if (text == NULL) {
    return 0;
  }
  parts = driver_c_prog_split_type_args_dup(text, &part_count);
  if (part_count <= 0) {
    driver_c_prog_free_string_array(parts, part_count);
    return 0;
  }
  {
    char *part = driver_c_prog_trim_dup(parts[0]);
    char *eq = strchr(part, '=');
    if (eq != NULL) {
      if (!driver_c_prog_parse_i32_text_strict(eq + 1, &ordinal)) {
        fprintf(stderr,
                "driver_c program runtime: unsupported enum ordinal part=%s\n",
                eq + 1);
        free(part);
        driver_c_prog_free_string_array(parts, part_count);
        exit(1);
      }
    }
    free(part);
  }
  driver_c_prog_free_string_array(parts, part_count);
  if (out_ordinal != NULL) {
    *out_ordinal = ordinal;
  }
  return 1;
}

static void driver_c_prog_parse_type_item_decls(DriverCProgRegistry *registry,
                                                DriverCProgItem *item) {
  char **lines = NULL;
  int32_t line_count = 0;
  int32_t base_indent = -1;
  int32_t i = 0;
  DriverCProgTypeDecl *active = NULL;
  if (registry == NULL || item == NULL || item->body_text == NULL) {
    return;
  }
  if (item->top_level_tag != DRIVER_C_PROG_TOP_TYPE) {
    return;
  }
  lines = driver_c_prog_split_lines_dup(item->body_text, &line_count);
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    int32_t indent = driver_c_prog_line_indent_width(lines[i]);
    if (line[0] != '\0' && (base_indent < 0 || indent < base_indent)) {
      base_indent = indent;
    }
    free(line);
  }
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    int32_t indent = driver_c_prog_line_indent_width(lines[i]);
    if (line[0] == '\0') {
      free(line);
      continue;
    }
    if (indent == base_indent) {
      char *decl_name = driver_c_prog_type_decl_name_from_line_dup(lines[i]);
      if (decl_name[0] == '\0') {
        active = NULL;
        free(decl_name);
        free(line);
        continue;
      }
      {
        char *eq = strchr(line, '=');
        char *head = NULL;
        char *base = NULL;
        char *args_text = NULL;
        DriverCProgTypeDecl *decl = driver_c_prog_registry_push_type_decl(registry);
        decl->item = item;
        decl->owner_module = item->owner_module;
        decl->decl_name = decl_name;
        decl->alias_target = eq != NULL ? driver_c_prog_trim_dup(eq + 1) : driver_c_dup_cstring("");
        if (eq != NULL) {
          head = driver_c_prog_trim_dup(driver_c_dup_range(line, eq));
          if (driver_c_prog_type_text_split_ctor(head, &base, &args_text)) {
            decl->param_names = driver_c_prog_split_type_args_dup(args_text, &decl->param_count);
            decl->param_cap = decl->param_count;
          }
        }
        if (driver_c_prog_type_alias_is_enum(decl->alias_target)) {
          char *suffix = driver_c_prog_trim_dup(decl->alias_target + 4);
          if (suffix[0] != '\0' &&
              driver_c_prog_parse_first_enum_ordinal_from_text(suffix, &decl->first_enum_ordinal)) {
            decl->has_first_enum_ordinal = 1;
          }
          free(suffix);
        }
        free(head);
        free(base);
        free(args_text);
        active = decl;
      }
      free(line);
      continue;
    }
    if (active == NULL || indent <= base_indent) {
      free(line);
      continue;
    }
    if (driver_c_prog_type_alias_is_enum(active->alias_target)) {
      if (!active->has_first_enum_ordinal &&
          driver_c_prog_parse_first_enum_ordinal_from_text(line, &active->first_enum_ordinal)) {
        active->has_first_enum_ordinal = 1;
      }
      free(line);
      continue;
    }
    {
      char *field_name = driver_c_prog_type_field_name_from_line_dup(lines[i]);
      char *field_type = NULL;
      if (field_name[0] != '\0') {
        field_type = driver_c_prog_type_field_type_from_line_dup(lines[i], field_name);
        if (field_type[0] != '\0') {
          driver_c_prog_type_decl_append_field(active, field_name, field_type);
          field_name = NULL;
          field_type = NULL;
        }
      }
      free(field_name);
      free(field_type);
    }
    free(line);
  }
  driver_c_prog_free_string_array(lines, line_count);
}

static char *driver_c_prog_substitute_type_token_dup(const char *text,
                                                     const char *param_name,
                                                     const char *replacement_text) {
  size_t out_cap = strlen(text != NULL ? text : "") + strlen(replacement_text != NULL ? replacement_text : "") + 32u;
  size_t out_len = 0;
  char *out = (char *)driver_c_prog_xcalloc(out_cap, 1u);
  int32_t i = 0;
  if (text == NULL || param_name == NULL || param_name[0] == '\0') {
    free(out);
    return driver_c_dup_cstring(text != NULL ? text : "");
  }
  while (text[i] != '\0') {
    if (!driver_c_prog_is_name_start(text[i])) {
      if (out_len + 2u >= out_cap) {
        out_cap = out_cap * 2u;
        out = (char *)driver_c_prog_xrealloc(out, out_cap);
      }
      out[out_len++] = text[i++];
      continue;
    }
    {
      int32_t start = i;
      const char *replacement = NULL;
      size_t token_len = 0;
      while (text[i] != '\0' && driver_c_prog_is_name_continue(text[i])) {
        i = i + 1;
      }
      token_len = (size_t)(i - start);
      if (strlen(param_name) == token_len && strncmp(text + start, param_name, token_len) == 0) {
        replacement = replacement_text != NULL ? replacement_text : "";
      }
      if (replacement != NULL) {
        size_t repl_len = strlen(replacement);
        if (out_len + repl_len + 1u >= out_cap) {
          while (out_len + repl_len + 1u >= out_cap) {
            out_cap = out_cap * 2u;
          }
          out = (char *)driver_c_prog_xrealloc(out, out_cap);
        }
        memcpy(out + out_len, replacement, repl_len);
        out_len = out_len + repl_len;
      } else {
        if (out_len + token_len + 1u >= out_cap) {
          while (out_len + token_len + 1u >= out_cap) {
            out_cap = out_cap * 2u;
          }
          out = (char *)driver_c_prog_xrealloc(out, out_cap);
        }
        memcpy(out + out_len, text + start, token_len);
        out_len = out_len + token_len;
      }
    }
  }
  out[out_len] = '\0';
  return out;
}

static DriverCProgItem *driver_c_prog_find_single_item_kind_in_module(DriverCProgRegistry *registry,
                                                                      const char *module_path,
                                                                      const char *label,
                                                                      const char *top_level_kind) {
  DriverCProgItem *out = NULL;
  int32_t i = 0;
  int32_t required_tag = DRIVER_C_PROG_TOP_UNKNOWN;
  if (registry == NULL || module_path == NULL || label == NULL || top_level_kind == NULL) {
    return NULL;
  }
  required_tag = driver_c_prog_top_level_tag_from_text(top_level_kind);
  if (registry->item_kind_lookup_cap > 0 && registry->item_kind_lookup != NULL) {
    return driver_c_prog_lookup_item_kind_in_module(registry, module_path, label, top_level_kind);
  }
  for (i = 0; i < registry->item_count; ++i) {
    DriverCProgItem *item = &registry->items[i];
    if (!driver_c_prog_text_eq(item->owner_module, module_path)) {
      continue;
    }
    if (!driver_c_prog_text_eq(item->label, label)) {
      continue;
    }
    if (required_tag != DRIVER_C_PROG_TOP_UNKNOWN) {
      if (item->top_level_tag != required_tag) {
        continue;
      }
    } else if (!driver_c_prog_text_eq(item->top_level_kind, top_level_kind)) {
        continue;
    }
    if (out != NULL && out != item) {
      driver_c_die("driver_c program runtime: ambiguous item kind in module");
    }
    out = item;
  }
  return out;
}

static char *driver_c_prog_resolve_import_target_module_dup(DriverCProgRegistry *registry,
                                                            const char *owner_module,
                                                            const char *import_name) {
  char *out = NULL;
  int32_t i = 0;
  if (registry == NULL || owner_module == NULL || import_name == NULL) {
    return driver_c_dup_cstring("");
  }
  if (registry->import_lookup_cap > 0 && registry->import_lookup != NULL) {
    DriverCProgItem *cached = driver_c_prog_lookup_import_item(registry, owner_module, import_name);
    if (cached == NULL) {
      return driver_c_dup_cstring("");
    }
    return driver_c_dup_cstring(driver_c_prog_import_target_module_text(cached));
  }
  for (i = 0; i < registry->item_count; ++i) {
    DriverCProgItem *item = &registry->items[i];
    char *visible = NULL;
    int match = 0;
    if (item->top_level_tag != DRIVER_C_PROG_TOP_IMPORT) {
      continue;
    }
    if (!driver_c_prog_text_eq(item->owner_module, owner_module)) {
      continue;
    }
    if (item->import_alias != NULL && item->import_alias[0] != '\0') {
      match = driver_c_prog_text_eq(item->import_alias, import_name);
    } else {
      visible = driver_c_prog_module_basename_dup(driver_c_prog_import_target_module_text(item));
      match = driver_c_prog_text_eq(visible, import_name);
      free(visible);
    }
    if (!match) {
      continue;
    }
    if (out != NULL && !driver_c_prog_text_eq(out, driver_c_prog_import_target_module_text(item))) {
      driver_c_die("driver_c program runtime: ambiguous import target module");
    }
    free(out);
    out = driver_c_dup_cstring(driver_c_prog_import_target_module_text(item));
  }
  return out != NULL ? out : driver_c_dup_cstring("");
}

static DriverCProgItem *driver_c_prog_resolve_visible_item_kind(DriverCProgRegistry *registry,
                                                                const char *owner_module,
                                                                const char *name,
                                                                const char *top_level_kind) {
  DriverCProgItem *direct = NULL;
  DriverCProgItem *found = NULL;
  int32_t i = 0;
  if (registry == NULL || owner_module == NULL || name == NULL || top_level_kind == NULL) {
    return NULL;
  }
  direct = driver_c_prog_find_single_item_kind_in_module(registry, owner_module, name, top_level_kind);
  if (direct != NULL) {
    return direct;
  }
  for (i = 0; i < registry->item_count; ++i) {
    DriverCProgItem *import_item = &registry->items[i];
    DriverCProgItem *target = NULL;
    if (import_item->top_level_tag != DRIVER_C_PROG_TOP_IMPORT) {
      continue;
    }
    if (!driver_c_prog_text_eq(import_item->owner_module, owner_module)) {
      continue;
    }
    target = driver_c_prog_find_single_item_kind_in_module(registry,
                                                           driver_c_prog_import_target_module_text(import_item),
                                                           name,
                                                           top_level_kind);
    if (target == NULL) {
      continue;
    }
    if (found != NULL && found != target) {
      driver_c_die("driver_c program runtime: ambiguous imported visible item kind");
    }
    found = target;
  }
  return found;
}

static DriverCProgTypeDecl *driver_c_prog_lookup_type_decl(DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *decl_name) {
  DriverCProgTypeDeclLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || owner_module == NULL || decl_name == NULL ||
      registry->type_decl_lookup_cap <= 0 || registry->type_decl_lookup == NULL) {
    return NULL;
  }
  cache = registry->type_decl_lookup;
  hash = driver_c_prog_hash_type_decl_key(owner_module, decl_name);
  start = (int32_t)(hash & (uint64_t)(registry->type_decl_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, owner_module) &&
        driver_c_prog_text_eq(cache[slot].decl_name, decl_name)) {
      if (cache[slot].ambiguous) {
        char message[512];
        snprintf(message,
                 sizeof(message),
                 "driver_c program runtime: ambiguous visible type item owner=%s decl=%s",
                 owner_module != NULL ? owner_module : "",
                 decl_name != NULL ? decl_name : "");
        driver_c_die(message);
      }
      return cache[slot].decl;
    }
    slot = (slot + 1) & (registry->type_decl_lookup_cap - 1);
    if (slot == start) {
      break;
    }
  }
  return NULL;
}

static DriverCProgTypeDecl *driver_c_prog_resolve_visible_type_decl(DriverCProgRegistry *registry,
                                                                    const char *owner_module,
                                                                    const char *type_text) {
  int cache_missing = 0;
  char *trimmed = driver_c_prog_trim_dup(type_text);
  char *dot = strchr(trimmed, '.');
  DriverCProgTypeDecl *out = NULL;
  if (registry != NULL && owner_module != NULL && type_text != NULL) {
    DriverCProgTypeDecl *cached =
        driver_c_prog_lookup_visible_type_decl_cache(registry, owner_module, type_text, &cache_missing);
    if (cached != NULL || cache_missing) {
      free(trimmed);
      return cached;
    }
  }
  if (dot != NULL && dot > trimmed) {
    char *import_name = driver_c_dup_range(trimmed, dot);
    char *type_name = driver_c_prog_trim_dup(dot + 1);
    out = driver_c_prog_lookup_type_decl(registry, import_name, type_name);
    if (out == NULL) {
      char *target_module = driver_c_prog_resolve_import_target_module_dup(registry, owner_module, import_name);
      if (target_module[0] != '\0') {
        out = driver_c_prog_lookup_type_decl(registry, target_module, type_name);
      }
      free(target_module);
    }
    free(import_name);
    free(type_name);
  } else {
    DriverCProgTypeDecl *direct = driver_c_prog_lookup_type_decl(registry, owner_module, trimmed);
    DriverCProgTypeDecl *found = NULL;
    int32_t i = 0;
    if (direct != NULL) {
      out = direct;
    } else {
      for (i = 0; i < registry->item_count; ++i) {
        DriverCProgItem *import_item = &registry->items[i];
        DriverCProgTypeDecl *target = NULL;
        if (import_item->top_level_tag != DRIVER_C_PROG_TOP_IMPORT) {
          continue;
        }
        if (!driver_c_prog_text_eq(import_item->owner_module, owner_module)) {
          continue;
        }
        target = driver_c_prog_lookup_type_decl(registry,
                                                driver_c_prog_import_target_module_text(import_item),
                                                trimmed);
        if (target == NULL) {
          continue;
        }
        if (found != NULL && found != target) {
          driver_c_die("driver_c program runtime: ambiguous imported visible type item");
        }
        found = target;
      }
      out = found;
    }
  }
  if (registry != NULL && owner_module != NULL && type_text != NULL) {
    driver_c_prog_insert_visible_type_decl_cache(registry, owner_module, type_text, out);
  }
  free(trimmed);
  return out;
}

static DriverCProgTypeDecl *driver_c_prog_resolve_visible_type_decl_in_scopes(DriverCProgRegistry *registry,
                                                                              const char *owner_module,
                                                                              const char *inst_owner_module,
                                                                              const char *type_text) {
  DriverCProgTypeDecl *out = driver_c_prog_resolve_visible_type_decl(registry, owner_module, type_text);
  if (out != NULL) {
    return out;
  }
  if (inst_owner_module == NULL || inst_owner_module[0] == '\0') {
    return NULL;
  }
  if (owner_module != NULL && driver_c_prog_text_eq(owner_module, inst_owner_module)) {
    return NULL;
  }
  return driver_c_prog_resolve_visible_type_decl(registry, inst_owner_module, type_text);
}

static void driver_c_prog_item_append_type_param_name_unique(DriverCProgItem *item, char *name) {
  int32_t i = 0;
  if (item == NULL || name == NULL || name[0] == '\0') {
    free(name);
    return;
  }
  for (i = 0; i < item->type_param_count; ++i) {
    if (driver_c_prog_text_eq(item->type_param_names[i], name)) {
      free(name);
      return;
    }
  }
  driver_c_prog_item_append_type_param_name(item, name);
}

static void driver_c_prog_collect_implicit_type_params_from_text(DriverCProgRegistry *registry,
                                                                 DriverCProgItem *item,
                                                                 const char *type_text,
                                                                 int depth) {
  char *normalized = NULL;
  char *fn_params_text = NULL;
  char *fn_return_text = NULL;
  char *suffix_base = NULL;
  char *suffix_arg = NULL;
  char *ctor_base = NULL;
  char *ctor_args_text = NULL;
  int32_t arg_count = 0;
  char **args = NULL;
  int32_t i = 0;
  if (item == NULL || type_text == NULL || type_text[0] == '\0' || depth > 32) {
    return;
  }
  normalized = driver_c_prog_normalize_param_type_text_dup(type_text);
  if (normalized[0] == '\0' ||
      driver_c_prog_text_eq(normalized, "bool") ||
      driver_c_prog_text_eq(normalized, "byte") ||
      driver_c_prog_text_eq(normalized, "char") ||
      driver_c_prog_text_eq(normalized, "int") ||
      driver_c_prog_text_eq(normalized, "int8") ||
      driver_c_prog_text_eq(normalized, "int16") ||
      driver_c_prog_text_eq(normalized, "int32") ||
      driver_c_prog_text_eq(normalized, "int64") ||
      driver_c_prog_text_eq(normalized, "uint8") ||
      driver_c_prog_text_eq(normalized, "uint16") ||
      driver_c_prog_text_eq(normalized, "uint32") ||
      driver_c_prog_text_eq(normalized, "uint64") ||
      driver_c_prog_text_eq(normalized, "float") ||
      driver_c_prog_text_eq(normalized, "float64") ||
      driver_c_prog_text_eq(normalized, "str") ||
      driver_c_prog_text_eq(normalized, "ptr") ||
      driver_c_prog_text_eq(normalized, "cstring") ||
      driver_c_prog_text_eq(normalized, "void")) {
    free(normalized);
    return;
  }
  if (driver_c_prog_type_text_split_fn(normalized, &fn_params_text, &fn_return_text)) {
    args = driver_c_prog_split_type_args_dup(fn_params_text, &arg_count);
    for (i = 0; i < arg_count; ++i) {
      char *param_type = driver_c_prog_function_param_type_text_dup(args[i]);
      driver_c_prog_collect_implicit_type_params_from_text(registry, item, param_type, depth + 1);
      free(param_type);
    }
    driver_c_prog_collect_implicit_type_params_from_text(registry, item, fn_return_text, depth + 1);
    driver_c_prog_free_string_array(args, arg_count);
    free(fn_params_text);
    free(fn_return_text);
    free(normalized);
    return;
  }
  if (driver_c_prog_type_text_split_suffix_ctor(normalized, &suffix_base, &suffix_arg)) {
    driver_c_prog_collect_implicit_type_params_from_text(registry, item, suffix_arg, depth + 1);
    free(normalized);
    free(fn_params_text);
    free(fn_return_text);
    free(suffix_base);
    free(suffix_arg);
    return;
  }
  if (driver_c_prog_type_text_split_ctor(normalized, &ctor_base, &ctor_args_text)) {
    args = driver_c_prog_split_type_args_dup(ctor_args_text, &arg_count);
    for (i = 0; i < arg_count; ++i) {
      driver_c_prog_collect_implicit_type_params_from_text(registry, item, args[i], depth + 1);
    }
    driver_c_prog_free_string_array(args, arg_count);
    free(normalized);
    free(fn_params_text);
    free(fn_return_text);
    free(ctor_base);
    free(ctor_args_text);
    return;
  }
  if (driver_c_prog_resolve_visible_type_decl_in_scopes(registry,
                                                        item->owner_module,
                                                        item->owner_module,
                                                        normalized) == NULL &&
      !driver_c_prog_is_builtin_type_ctor_label(normalized)) {
    driver_c_prog_item_append_type_param_name_unique(item, normalized);
    free(fn_params_text);
    free(fn_return_text);
    return;
  }
  free(fn_params_text);
  free(fn_return_text);
  free(normalized);
}

static void driver_c_prog_collect_item_implicit_type_params(DriverCProgRegistry *registry,
                                                            DriverCProgItem *item) {
  int32_t i = 0;
  if (item == NULL || !driver_c_prog_is_routine_item(item)) {
    return;
  }
  for (i = 0; i < item->param_count; ++i) {
    if (item->param_type_texts != NULL && item->param_type_texts[i] != NULL) {
      driver_c_prog_collect_implicit_type_params_from_text(registry, item, item->param_type_texts[i], 0);
    }
  }
  if (item->return_type != NULL && item->return_type[0] != '\0') {
    driver_c_prog_collect_implicit_type_params_from_text(registry, item, item->return_type, 0);
  }
}

static char *driver_c_prog_join_type_ctor_dup(const char *base,
                                              char **args,
                                              int32_t arg_count) {
  size_t cap = strlen(base != NULL ? base : "") + 8u;
  size_t len = 0;
  int32_t i = 0;
  char *out = NULL;
  for (i = 0; i < arg_count; ++i) {
    cap += strlen(args[i] != NULL ? args[i] : "") + 2u;
  }
  out = (char *)driver_c_prog_xcalloc(cap, 1u);
  if (base != NULL) {
    len = strlen(base);
    memcpy(out, base, len);
  }
  out[len++] = '[';
  for (i = 0; i < arg_count; ++i) {
    const char *arg = args[i] != NULL ? args[i] : "";
    size_t arg_len = strlen(arg);
    if (i > 0) {
      out[len++] = ',';
    }
    memcpy(out + len, arg, arg_len);
    len = len + arg_len;
  }
  out[len++] = ']';
  out[len] = '\0';
  return out;
}

static char *driver_c_prog_normalize_visible_type_text_dup(DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *type_text,
                                                           int depth) {
  char *normalized = NULL;
  char *fn_params_text = NULL;
  char *fn_return_text = NULL;
  char *suffix_base = NULL;
  char *suffix_arg = NULL;
  char *ctor_base = NULL;
  char *ctor_args_text = NULL;
  if (depth > 32) {
    driver_c_die("driver_c program runtime: type normalization recursion too deep");
  }
  normalized = driver_c_prog_trim_dup(type_text);
  while (driver_c_prog_text_starts_with(normalized, "var ")) {
    char *next = driver_c_prog_trim_dup(normalized + 4);
    free(normalized);
    normalized = next;
  }
  if (normalized[0] == '\0') {
    return normalized;
  }
  if (driver_c_prog_type_text_split_fn(normalized, &fn_params_text, &fn_return_text)) {
    int32_t param_count = 0;
    char **params = driver_c_prog_split_type_args_dup(fn_params_text, &param_count);
    char *return_norm = driver_c_prog_normalize_visible_type_text_dup(registry,
                                                                      owner_module,
                                                                      fn_return_text,
                                                                      depth + 1);
    char *out = NULL;
    int32_t i = 0;
    for (i = 0; i < param_count; ++i) {
      char *param_type = driver_c_prog_function_param_type_text_dup(params[i]);
      char *param_norm = driver_c_prog_normalize_visible_type_text_dup(registry,
                                                                       owner_module,
                                                                       param_type,
                                                                       depth + 1);
      free(params[i]);
      params[i] = param_norm;
      free(param_type);
    }
    out = driver_c_prog_join_function_type_dup(params, param_count, return_norm);
    driver_c_prog_free_string_array(params, param_count);
    free(return_norm);
    free(fn_params_text);
    free(fn_return_text);
    free(normalized);
    return out;
  }
  if (driver_c_prog_type_text_split_suffix_ctor(normalized, &suffix_base, &suffix_arg)) {
    char *arg_norm = driver_c_prog_normalize_visible_type_text_dup(registry,
                                                                   owner_module,
                                                                   suffix_arg,
                                                                   depth + 1);
    char *out = NULL;
    size_t arg_len = strlen(arg_norm);
    if (driver_c_prog_text_eq(suffix_base, "[]")) {
      out = (char *)driver_c_prog_xcalloc(arg_len + 3u, 1u);
      memcpy(out, arg_norm, arg_len);
      memcpy(out + arg_len, "[]", 3u);
    } else {
      out = (char *)driver_c_prog_xcalloc(arg_len + 2u, 1u);
      memcpy(out, arg_norm, arg_len);
      out[arg_len] = '*';
      out[arg_len + 1] = '\0';
    }
    free(arg_norm);
    free(fn_params_text);
    free(fn_return_text);
    free(suffix_base);
    free(suffix_arg);
    free(normalized);
    return out;
  }
  free(suffix_base);
  free(suffix_arg);
  suffix_base = NULL;
  suffix_arg = NULL;
  if (driver_c_prog_type_text_split_ctor(normalized, &ctor_base, &ctor_args_text)) {
    char *base_norm = driver_c_prog_normalize_visible_type_text_dup(registry,
                                                                    owner_module,
                                                                    ctor_base,
                                                                    depth + 1);
    int32_t arg_count = 0;
    char **args = driver_c_prog_split_type_args_dup(ctor_args_text, &arg_count);
    int32_t i = 0;
    char *out = NULL;
    for (i = 0; i < arg_count; ++i) {
      char *next = driver_c_prog_normalize_visible_type_text_dup(registry,
                                                                 owner_module,
                                                                 args[i],
                                                                 depth + 1);
      free(args[i]);
      args[i] = next;
    }
    out = driver_c_prog_join_type_ctor_dup(base_norm, args, arg_count);
    driver_c_prog_free_string_array(args, arg_count);
    free(base_norm);
    free(fn_params_text);
    free(fn_return_text);
    free(ctor_base);
    free(ctor_args_text);
    free(normalized);
    return out;
  }
  free(ctor_base);
  free(ctor_args_text);
  ctor_base = NULL;
  ctor_args_text = NULL;
  {
    char *dot = strchr(normalized, '.');
    if (dot != NULL && dot > normalized) {
      char *module_or_alias = driver_c_dup_range(normalized, dot);
      char *type_name = driver_c_prog_trim_dup(dot + 1);
      DriverCProgTypeDecl *direct = driver_c_prog_lookup_type_decl(registry,
                                                                   module_or_alias,
                                                                   type_name);
      if (direct == NULL) {
        char *target_module = driver_c_prog_resolve_import_target_module_dup(registry,
                                                                             owner_module,
                                                                             module_or_alias);
        if (target_module[0] != '\0') {
          char *out = (char *)driver_c_prog_xcalloc(strlen(target_module) + strlen(type_name) + 2u, 1u);
          memcpy(out, target_module, strlen(target_module));
          out[strlen(target_module)] = '.';
          memcpy(out + strlen(target_module) + 1u, type_name, strlen(type_name) + 1u);
          free(target_module);
          free(module_or_alias);
          free(type_name);
          free(normalized);
          return out;
        }
        free(target_module);
      }
      free(module_or_alias);
      free(type_name);
    }
  }
  return normalized;
}

static int driver_c_prog_parse_const_i32_item(DriverCProgItem *item, int32_t *out_value) {
  char *line = NULL;
  char *eq = NULL;
  int ok = 0;
  if (item == NULL || item->top_level_tag != DRIVER_C_PROG_TOP_CONST) {
    return 0;
  }
  if (driver_c_prog_parse_i32_text_strict(item->body_text, out_value)) {
    return 1;
  }
  if (item->signature_line_count <= 0 ||
      item->signature_lines == NULL ||
      item->signature_lines[0] == NULL) {
    return 0;
  }
  line = driver_c_prog_trim_inline_comment_dup(item->signature_lines[0]);
  eq = strchr(line, '=');
  if (eq != NULL) {
    ok = driver_c_prog_parse_i32_text_strict(eq + 1, out_value);
  } else {
    ok = driver_c_prog_parse_i32_text_strict(line, out_value);
  }
  free(line);
  return ok;
}

static int driver_c_prog_resolve_visible_const_i32_in_module(DriverCProgRegistry *registry,
                                                             const char *owner_module,
                                                             const char *name,
                                                             int32_t *out_value) {
  char *trimmed = driver_c_prog_trim_dup(name);
  char *dot = strchr(trimmed, '.');
  DriverCProgItem *item = NULL;
  int ok = 0;
  if (driver_c_prog_parse_i32_text_strict(trimmed, out_value)) {
    free(trimmed);
    return 1;
  }
  if (dot != NULL && dot > trimmed) {
    char *import_name = driver_c_dup_range(trimmed, dot);
    char *const_name = driver_c_prog_trim_dup(dot + 1);
    char *target_module = driver_c_prog_resolve_import_target_module_dup(registry, owner_module, import_name);
    if (target_module[0] != '\0') {
      item = driver_c_prog_find_single_item_kind_in_module(registry, target_module, const_name, "const");
    }
    free(import_name);
    free(const_name);
    free(target_module);
  } else {
    item = driver_c_prog_resolve_visible_item_kind(registry, owner_module, trimmed, "const");
  }
  ok = driver_c_prog_parse_const_i32_item(item, out_value);
  free(trimmed);
  return ok;
}

static int driver_c_prog_type_decl_generic_param_names(const char *body_text,
                                                       const char *type_name,
                                                       char ***out_names,
                                                       int32_t *out_count) {
  char **lines = NULL;
  int32_t line_count = 0;
  int32_t base_indent = -1;
  int32_t i = 0;
  if (out_names != NULL) *out_names = NULL;
  if (out_count != NULL) *out_count = 0;
  lines = driver_c_prog_split_lines_dup(body_text, &line_count);
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    int32_t indent = driver_c_prog_line_indent_width(lines[i]);
    if (line[0] != '\0' && (base_indent < 0 || indent < base_indent)) {
      base_indent = indent;
    }
    free(line);
  }
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    char *decl_name = NULL;
    if (line[0] == '\0' || driver_c_prog_line_indent_width(lines[i]) != base_indent) {
      free(line);
      continue;
    }
    decl_name = driver_c_prog_type_decl_name_from_line_dup(lines[i]);
    if (driver_c_prog_text_eq(decl_name, type_name)) {
      char *eq = strchr(line, '=');
      char *head = NULL;
      char *base = NULL;
      char *args_text = NULL;
      free(decl_name);
      if (eq == NULL) {
        free(line);
        break;
      }
      head = driver_c_prog_trim_dup(driver_c_dup_range(line, eq));
      if (driver_c_prog_type_text_split_ctor(head, &base, &args_text)) {
        int32_t count = 0;
        char **names = driver_c_prog_split_type_args_dup(args_text, &count);
        if (out_names != NULL) *out_names = names;
        if (out_count != NULL) *out_count = count;
        free(base);
        free(args_text);
        free(head);
        free(line);
        driver_c_prog_free_string_array(lines, line_count);
        return 1;
      }
      free(base);
      free(args_text);
      free(head);
      free(line);
      break;
    }
    free(decl_name);
    free(line);
  }
  driver_c_prog_free_string_array(lines, line_count);
  return 0;
}

static int driver_c_prog_collect_type_fields(const char *body_text,
                                             const char *type_name,
                                             char ***out_names,
                                             char ***out_types,
                                             int32_t *out_count) {
  char **lines = NULL;
  int32_t line_count = 0;
  int32_t base_indent = -1;
  int32_t cap = 0;
  int32_t count = 0;
  int32_t i = 0;
  int inside_type = 0;
  char **names = NULL;
  char **types = NULL;
  if (out_names != NULL) *out_names = NULL;
  if (out_types != NULL) *out_types = NULL;
  if (out_count != NULL) *out_count = 0;
  lines = driver_c_prog_split_lines_dup(body_text, &line_count);
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    int32_t indent = driver_c_prog_line_indent_width(lines[i]);
    if (line[0] != '\0' && (base_indent < 0 || indent < base_indent)) {
      base_indent = indent;
    }
    free(line);
  }
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    int32_t indent = driver_c_prog_line_indent_width(lines[i]);
    char *decl_name = NULL;
    if (line[0] == '\0') {
      free(line);
      continue;
    }
    decl_name = driver_c_prog_type_decl_name_from_line_dup(lines[i]);
    if (indent == base_indent) {
      if (inside_type && decl_name[0] != '\0') {
        free(decl_name);
        free(line);
        break;
      }
      inside_type = driver_c_prog_text_eq(decl_name, type_name);
      free(decl_name);
      free(line);
      continue;
    }
    free(decl_name);
    if (!inside_type || indent <= base_indent) {
      free(line);
      continue;
    }
    {
      char *field_name = driver_c_prog_type_field_name_from_line_dup(lines[i]);
      char *field_type = NULL;
      if (field_name[0] == '\0') {
        free(field_name);
        free(line);
        continue;
      }
      field_type = driver_c_prog_type_field_type_from_line_dup(lines[i], field_name);
      if (field_type[0] != '\0') {
        if (count >= cap) {
          cap = cap > 0 ? cap * 2 : 4;
          names = (char **)driver_c_prog_xrealloc(names, sizeof(char *) * (size_t)cap);
          types = (char **)driver_c_prog_xrealloc(types, sizeof(char *) * (size_t)cap);
        }
        names[count] = field_name;
        types[count] = field_type;
        count = count + 1;
      } else {
        free(field_name);
        free(field_type);
      }
    }
    free(line);
  }
  driver_c_prog_free_string_array(lines, line_count);
  if (out_names != NULL) *out_names = names; else driver_c_prog_free_string_array(names, count);
  if (out_types != NULL) *out_types = types; else driver_c_prog_free_string_array(types, count);
  if (out_count != NULL) *out_count = count;
  return count > 0;
}

static int driver_c_prog_first_enum_ordinal_in_type_body(const char *body_text,
                                                         const char *type_name,
                                                         int32_t *out_ordinal) {
  char **lines = NULL;
  int32_t line_count = 0;
  int32_t base_indent = -1;
  int32_t active_enum_indent = -1;
  int32_t next_ordinal = 0;
  int32_t i = 0;
  int found = 0;
  lines = driver_c_prog_split_lines_dup(body_text, &line_count);
  for (i = 0; i < line_count; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    int32_t indent = driver_c_prog_line_indent_width(lines[i]);
    if (line[0] != '\0' && (base_indent < 0 || indent < base_indent)) {
      base_indent = indent;
    }
    free(line);
  }
  for (i = 0; i < line_count && !found; ++i) {
    char *line = driver_c_prog_trim_inline_comment_dup(lines[i]);
    int32_t indent = driver_c_prog_line_indent_width(lines[i]);
    char *decl_name = driver_c_prog_type_decl_name_from_line_dup(lines[i]);
    if (line[0] == '\0') {
      free(decl_name);
      free(line);
      continue;
    }
    if (indent == base_indent) {
      char *alias = NULL;
      if (active_enum_indent >= 0 && decl_name[0] != '\0') {
        free(decl_name);
        free(line);
        break;
      }
      alias = driver_c_prog_type_alias_target_from_line_dup(lines[i], type_name);
      if (driver_c_prog_text_eq(decl_name, type_name) &&
          driver_c_prog_text_starts_with(alias, "enum")) {
        char *suffix = driver_c_prog_trim_dup(alias + 4);
        active_enum_indent = indent;
        next_ordinal = 0;
        if (suffix[0] != '\0') {
          char **parts = NULL;
          int32_t part_count = 0;
          int32_t j = 0;
          parts = driver_c_prog_split_type_args_dup(suffix, &part_count);
          for (j = 0; j < part_count; ++j) {
            char *part = driver_c_prog_trim_dup(parts[j]);
            char *eq = strchr(part, '=');
            int32_t field_ordinal = next_ordinal;
            if (eq != NULL) {
              if (!driver_c_prog_parse_i32_text_strict(eq + 1, &field_ordinal)) {
                fprintf(stderr,
                        "driver_c program runtime: unsupported enum ordinal type=%s part=%s\n",
                        type_name != NULL ? type_name : "",
                        eq + 1);
                free(part);
                driver_c_prog_free_string_array(parts, part_count);
                free(suffix);
                free(alias);
                free(decl_name);
                free(line);
                driver_c_prog_free_string_array(lines, line_count);
                exit(1);
              }
            }
            if (out_ordinal != NULL) *out_ordinal = field_ordinal;
            next_ordinal = field_ordinal + 1;
            found = 1;
            free(part);
            break;
          }
          driver_c_prog_free_string_array(parts, part_count);
        }
        free(suffix);
      } else {
        active_enum_indent = -1;
      }
      free(alias);
      free(decl_name);
      free(line);
      continue;
    }
    free(decl_name);
    if (active_enum_indent >= 0 && indent > active_enum_indent) {
      char **parts = NULL;
      int32_t part_count = 0;
      int32_t j = 0;
      parts = driver_c_prog_split_type_args_dup(line, &part_count);
      for (j = 0; j < part_count; ++j) {
        char *part = driver_c_prog_trim_dup(parts[j]);
        char *eq = strchr(part, '=');
        int32_t field_ordinal = next_ordinal;
        if (eq != NULL) {
          if (!driver_c_prog_parse_i32_text_strict(eq + 1, &field_ordinal)) {
            fprintf(stderr,
                    "driver_c program runtime: unsupported enum ordinal type=%s part=%s\n",
                    type_name != NULL ? type_name : "",
                    eq + 1);
            free(part);
            driver_c_prog_free_string_array(parts, part_count);
            free(line);
            driver_c_prog_free_string_array(lines, line_count);
            exit(1);
          }
        }
        if (out_ordinal != NULL) *out_ordinal = field_ordinal;
        next_ordinal = field_ordinal + 1;
        found = 1;
        free(part);
        break;
      }
      driver_c_prog_free_string_array(parts, part_count);
    } else {
      active_enum_indent = -1;
    }
    free(line);
  }
  driver_c_prog_free_string_array(lines, line_count);
  return found;
}

static char *driver_c_prog_type_terminal_name_dup(const char *type_text) {
  char *trimmed = driver_c_prog_trim_dup(type_text);
  char *dot = strrchr(trimmed, '.');
  if (dot != NULL && dot[1] != '\0') {
    char *out = driver_c_prog_trim_dup(dot + 1);
    free(trimmed);
    return out;
  }
  return trimmed;
}

static DriverCProgZeroPlan *driver_c_prog_zero_plan_new(int32_t kind) {
  DriverCProgZeroPlan *plan = (DriverCProgZeroPlan *)driver_c_prog_xcalloc(1u, sizeof(*plan));
  plan->kind = kind;
  return plan;
}

static DriverCProgZeroPlan *driver_c_prog_zero_plan_from_type_decl(DriverCProgRegistry *registry,
                                                                   DriverCProgTypeDecl *type_decl,
                                                                   const char *inst_owner_module,
                                                                   const char *resolved_type_text,
                                                                   char **actual_args,
                                                                   int32_t actual_arg_count,
                                                                   int depth);

static DriverCProgZeroPlan *driver_c_prog_zero_plan_from_type_decl(DriverCProgRegistry *registry,
                                                                   DriverCProgTypeDecl *type_decl,
                                                                   const char *inst_owner_module,
                                                                   const char *resolved_type_text,
                                                                   char **actual_args,
                                                                   int32_t actual_arg_count,
                                                                   int depth) {
  DriverCProgZeroPlan *plan = NULL;
  char *type_name = NULL;
  int32_t i = 0;
  if (type_decl == NULL || type_decl->item == NULL) {
    fprintf(stderr,
            "driver_c program runtime: missing type decl for zero init type=%s inst_owner=%s\n",
            resolved_type_text != NULL ? resolved_type_text : "",
            inst_owner_module != NULL ? inst_owner_module : "");
    exit(1);
  }
  type_name = driver_c_prog_type_terminal_name_dup(resolved_type_text != NULL ? resolved_type_text : "");
  if (driver_c_prog_text_eq(type_decl->owner_module, "std/rawbytes") &&
      driver_c_prog_text_eq(type_name, "Bytes")) {
    free(type_name);
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_BYTES);
  }
  if (type_decl->param_count != actual_arg_count) {
    free(type_name);
    driver_c_die("driver_c program runtime: generic type arg count mismatch");
  }
  if (type_decl->alias_target != NULL && type_decl->alias_target[0] != '\0') {
    char *transparent = driver_c_prog_transparent_alias_target_dup(type_decl->alias_target);
    if (transparent[0] != '\0') {
      for (i = 0; i < type_decl->param_count; ++i) {
        char *next = driver_c_prog_substitute_type_token_dup(transparent,
                                                             type_decl->param_names[i],
                                                             actual_args[i]);
        free(transparent);
        transparent = next;
      }
      plan = driver_c_prog_zero_plan_for_type(registry,
                                              type_decl->owner_module,
                                              inst_owner_module,
                                              transparent,
                                              depth + 1);
      free(type_name);
      free(transparent);
      return plan;
    }
    free(transparent);
  }
  if (type_decl->field_count > 0) {
    plan = driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_RECORD);
    plan->type_text = driver_c_dup_cstring(resolved_type_text != NULL ? resolved_type_text : "");
    plan->field_count = type_decl->field_count;
    plan->field_names = (char **)driver_c_prog_xcalloc((size_t)plan->field_count, sizeof(char *));
    plan->field_plans = (DriverCProgZeroPlan **)driver_c_prog_xcalloc((size_t)plan->field_count,
                                                                      sizeof(DriverCProgZeroPlan *));
    for (i = 0; i < type_decl->field_count; ++i) {
      char *resolved_type = driver_c_dup_cstring(type_decl->field_types[i]);
      int32_t j = 0;
      for (j = 0; j < type_decl->param_count; ++j) {
        char *next = driver_c_prog_substitute_type_token_dup(resolved_type,
                                                             type_decl->param_names[j],
                                                             actual_args[j]);
        free(resolved_type);
        resolved_type = next;
      }
      plan->field_names[i] = driver_c_dup_cstring(type_decl->field_names[i]);
      plan->field_plans[i] = driver_c_prog_zero_plan_for_type(registry,
                                                              type_decl->owner_module,
                                                              inst_owner_module,
                                                              resolved_type,
                                                              depth + 1);
      free(resolved_type);
    }
    plan->field_lookup_cap = driver_c_prog_record_lookup_cap_for_count(plan->field_count);
    plan->field_lookup_indices =
        (int32_t *)driver_c_prog_xcalloc((size_t)plan->field_lookup_cap, sizeof(int32_t));
    driver_c_prog_field_lookup_reset(plan->field_lookup_indices, plan->field_lookup_cap);
    for (i = 0; i < plan->field_count; ++i) {
      driver_c_prog_field_lookup_insert(plan->field_lookup_indices,
                                        plan->field_lookup_cap,
                                        plan->field_names,
                                        plan->field_count,
                                        i);
    }
    free(type_name);
    return plan;
  }
  if (type_decl->has_first_enum_ordinal) {
    plan = driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_ENUM);
    plan->enum_ordinal = type_decl->first_enum_ordinal;
    free(type_name);
    return plan;
  }
  fprintf(stderr,
          "driver_c program runtime: unsupported zero-init type decl owner=%s type=%s decl=%s\n",
          type_decl->owner_module != NULL ? type_decl->owner_module : "",
          resolved_type_text != NULL ? resolved_type_text : "",
          type_decl->decl_name != NULL ? type_decl->decl_name : "");
  exit(1);
  return NULL;
}

static DriverCProgZeroPlan *driver_c_prog_zero_plan_build(DriverCProgRegistry *registry,
                                                          const char *owner_module,
                                                          const char *inst_owner_module,
                                                          const char *normalized_type_text,
                                                          int depth) {
  char *fn_params_text = NULL;
  char *fn_return_text = NULL;
  char *suffix_base = NULL;
  char *suffix_arg = NULL;
  char *ctor_base = NULL;
  char *ctor_args_text = NULL;
  DriverCProgZeroPlan *plan = NULL;
  if (normalized_type_text == NULL) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_NIL);
  }
  if (normalized_type_text[0] == '\0' || driver_c_prog_text_eq(normalized_type_text, "void")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_NIL);
  }
  if (driver_c_prog_text_eq(normalized_type_text, "bool")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_BOOL);
  }
  if (driver_c_prog_text_eq(normalized_type_text, "int") ||
      driver_c_prog_text_eq(normalized_type_text, "int8") ||
      driver_c_prog_text_eq(normalized_type_text, "int16") ||
      driver_c_prog_text_eq(normalized_type_text, "int32") ||
      driver_c_prog_text_eq(normalized_type_text, "byte") ||
      driver_c_prog_text_eq(normalized_type_text, "char")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_I32);
  }
  if (driver_c_prog_text_eq(normalized_type_text, "uint8") ||
      driver_c_prog_text_eq(normalized_type_text, "uint16") ||
      driver_c_prog_text_eq(normalized_type_text, "uint32")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_U32);
  }
  if (driver_c_prog_text_eq(normalized_type_text, "int64")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_I64);
  }
  if (driver_c_prog_text_eq(normalized_type_text, "uint64")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_U64);
  }
  if (driver_c_prog_text_eq(normalized_type_text, "float") ||
      driver_c_prog_text_eq(normalized_type_text, "float64")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_F64);
  }
  if (driver_c_prog_text_eq(normalized_type_text, "str")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_STR);
  }
  if (driver_c_prog_text_eq(normalized_type_text, "ptr") ||
      driver_c_prog_text_eq(normalized_type_text, "cstring")) {
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_PTR);
  }
  if (driver_c_prog_type_text_split_fn(normalized_type_text, &fn_params_text, &fn_return_text)) {
    free(fn_params_text);
    free(fn_return_text);
    return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_NIL);
  }
  if (driver_c_prog_type_text_split_suffix_ctor(normalized_type_text, &suffix_base, &suffix_arg) &&
      driver_c_prog_text_eq(suffix_base, "[]")) {
    plan = driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_DYNAMIC_ARRAY);
    plan->elem_type_text = driver_c_dup_cstring(suffix_arg);
    free(suffix_base);
    free(suffix_arg);
    return plan;
  }
  free(suffix_base);
  free(suffix_arg);
  suffix_base = NULL;
  suffix_arg = NULL;
  if (driver_c_prog_type_text_split_ctor(normalized_type_text, &ctor_base, &ctor_args_text)) {
    int32_t arg_count = 0;
    char **args = driver_c_prog_split_type_args_dup(ctor_args_text, &arg_count);
    int32_t fixed_len = 0;
    if (arg_count == 1 &&
        driver_c_prog_resolve_visible_const_i32_in_module(registry, owner_module, args[0], &fixed_len)) {
      if (fixed_len < 0) {
        driver_c_prog_free_string_array(args, arg_count);
        free(ctor_base);
        free(ctor_args_text);
        driver_c_die("driver_c program runtime: negative fixed array length");
      }
      plan = driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_FIXED_ARRAY);
      plan->elem_type_text = driver_c_dup_cstring(ctor_base);
      plan->fixed_len = fixed_len;
      plan->field_count = 1;
      plan->field_plans = (DriverCProgZeroPlan **)driver_c_prog_xcalloc(1u, sizeof(DriverCProgZeroPlan *));
      plan->field_plans[0] = driver_c_prog_zero_plan_for_type(registry,
                                                              owner_module,
                                                              inst_owner_module,
                                                              ctor_base,
                                                              depth + 1);
    } else {
      DriverCProgTypeDecl *type_decl = driver_c_prog_resolve_visible_type_decl_in_scopes(registry,
                                                                                          owner_module,
                                                                                          inst_owner_module,
                                                                                          ctor_base);
      plan = driver_c_prog_zero_plan_from_type_decl(registry,
                                                    type_decl,
                                                    inst_owner_module,
                                                    ctor_base,
                                                    args,
                                                    arg_count,
                                                    depth + 1);
    }
    driver_c_prog_free_string_array(args, arg_count);
    free(ctor_base);
    free(ctor_args_text);
    return plan;
  }
  {
    DriverCProgTypeDecl *type_decl = driver_c_prog_resolve_visible_type_decl_in_scopes(registry,
                                                                                        owner_module,
                                                                                        inst_owner_module,
                                                                                        normalized_type_text);
    if (type_decl == NULL) {
      char *token = driver_c_prog_leading_name_token_dup(normalized_type_text);
      int is_bare_token = driver_c_prog_text_eq(token, normalized_type_text);
      free(token);
      if (is_bare_token) {
        return driver_c_prog_zero_plan_new(DRIVER_C_PROG_ZERO_PLAN_NIL);
      }
    }
    return driver_c_prog_zero_plan_from_type_decl(registry,
                                                  type_decl,
                                                  inst_owner_module,
                                                  normalized_type_text,
                                                  NULL,
                                                  0,
                                                  depth + 1);
  }
}

static DriverCProgZeroPlan *driver_c_prog_zero_plan_for_type(DriverCProgRegistry *registry,
                                                             const char *owner_module,
                                                             const char *inst_owner_module,
                                                             const char *type_text,
                                                             int depth) {
  DriverCProgZeroPlan *cached = NULL;
  char *normalized = NULL;
  if (depth > 32) {
    driver_c_die("driver_c program runtime: zero-init type recursion too deep");
  }
  normalized = driver_c_prog_trim_dup(type_text != NULL ? type_text : "");
  while (driver_c_prog_text_starts_with(normalized, "var ")) {
    char *next = driver_c_prog_trim_dup(normalized + 4);
    free(normalized);
    normalized = next;
  }
  {
    char *canonical = driver_c_prog_normalize_visible_type_text_dup(registry,
                                                                    owner_module,
                                                                    normalized,
                                                                    depth + 1);
    free(normalized);
    normalized = canonical;
  }
  cached = driver_c_prog_zero_plan_lookup(registry,
                                          owner_module != NULL ? owner_module : "",
                                          inst_owner_module != NULL ? inst_owner_module : "",
                                          normalized);
  if (cached != NULL) {
    free(normalized);
    return cached;
  }
  cached = driver_c_prog_zero_plan_build(registry,
                                         owner_module != NULL ? owner_module : "",
                                         inst_owner_module != NULL ? inst_owner_module : "",
                                         normalized,
                                         depth + 1);
  driver_c_prog_zero_plan_insert(registry,
                                 owner_module != NULL ? owner_module : "",
                                 inst_owner_module != NULL ? inst_owner_module : "",
                                 normalized,
                                 cached);
  free(normalized);
  return cached;
}

static DriverCProgValue driver_c_prog_zero_value_from_plan(const DriverCProgZeroPlan *plan) {
  int32_t i = 0;
  if (plan == NULL) {
    return driver_c_prog_value_nil();
  }
  switch (plan->kind) {
    case DRIVER_C_PROG_ZERO_PLAN_NIL:
      return driver_c_prog_value_nil();
    case DRIVER_C_PROG_ZERO_PLAN_BOOL:
      return driver_c_prog_value_bool(0);
    case DRIVER_C_PROG_ZERO_PLAN_I32:
      return driver_c_prog_value_i32(0);
    case DRIVER_C_PROG_ZERO_PLAN_U32:
      return driver_c_prog_value_u32(0u);
    case DRIVER_C_PROG_ZERO_PLAN_I64:
      return driver_c_prog_value_i64(0);
    case DRIVER_C_PROG_ZERO_PLAN_U64:
      return driver_c_prog_value_u64(0u);
    case DRIVER_C_PROG_ZERO_PLAN_F64:
      return driver_c_prog_value_f64(0.0);
    case DRIVER_C_PROG_ZERO_PLAN_STR:
      return driver_c_prog_value_str_empty();
    case DRIVER_C_PROG_ZERO_PLAN_PTR:
      return driver_c_prog_value_ptr(NULL);
    case DRIVER_C_PROG_ZERO_PLAN_BYTES:
      return driver_c_prog_value_bytes_empty();
    case DRIVER_C_PROG_ZERO_PLAN_ENUM:
      return driver_c_prog_value_i32(plan->enum_ordinal);
    case DRIVER_C_PROG_ZERO_PLAN_DYNAMIC_ARRAY: {
      DriverCProgArray *array = driver_c_prog_array_new_with_elem(plan->elem_type_text);
      return driver_c_prog_value_array(array);
    }
    case DRIVER_C_PROG_ZERO_PLAN_FIXED_ARRAY: {
      DriverCProgArray *array =
          driver_c_prog_array_new_with_elem_and_len_uninit(plan->elem_type_text, plan->fixed_len);
      for (i = 0; i < plan->fixed_len; ++i) {
        array->items[i] = driver_c_prog_zero_value_from_plan(plan->field_plans != NULL ? plan->field_plans[0] : NULL);
      }
      return driver_c_prog_value_array(array);
    }
    case DRIVER_C_PROG_ZERO_PLAN_RECORD: {
      DriverCProgRecord *record = driver_c_prog_record_new_with_type(plan->type_text);
      driver_c_prog_record_ensure_value_cap(record, plan->field_count);
      driver_c_prog_record_set_shared_names(record, plan->field_names);
      for (i = 0; i < plan->field_count; ++i) {
        record->values[i] = driver_c_prog_zero_value_from_plan(plan->field_plans[i]);
      }
      record->len = plan->field_count;
      if (plan->field_lookup_indices != NULL && plan->field_lookup_cap > 0) {
        driver_c_prog_record_set_shared_lookup(record, plan->field_lookup_indices, plan->field_lookup_cap);
      } else {
        driver_c_prog_record_lookup_rebuild(record);
      }
      return driver_c_prog_value_record(record);
    }
    default:
      driver_c_die("driver_c program runtime: unknown zero plan kind");
      return driver_c_prog_value_nil();
  }
}

static DriverCProgValue driver_c_prog_zero_value_from_type(DriverCProgRegistry *registry,
                                                           const char *owner_module,
                                                           const char *inst_owner_module,
                                                           const char *type_text,
                                                           int depth) {
  DriverCProgZeroPlan *plan = driver_c_prog_zero_plan_for_type(registry,
                                                               owner_module,
                                                               inst_owner_module,
                                                               type_text,
                                                               depth);
  return driver_c_prog_zero_value_from_plan(plan);
}

static char *driver_c_prog_import_visible_name_dup(DriverCProgItem *item) {
  if (item == NULL) {
    return driver_c_dup_cstring("");
  }
  if (item->import_alias != NULL && item->import_alias[0] != '\0') {
    return driver_c_dup_cstring(item->import_alias);
  }
  return driver_c_prog_module_basename_dup(driver_c_prog_import_target_module_text(item));
}

static int32_t driver_c_prog_lookup_cap_for_count(int32_t count, int32_t minimum) {
  int32_t cap = 1;
  while (cap < count * 4) {
    cap = cap << 1;
  }
  if (cap < minimum) {
    cap = minimum;
  }
  return cap;
}

static void driver_c_prog_registry_insert_item_lookup(DriverCProgRegistry *registry,
                                                      DriverCProgItem *item) {
  DriverCProgItemLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || item == NULL || item->owner_module == NULL || item->label == NULL) {
    return;
  }
  if (item->top_level_tag == DRIVER_C_PROG_TOP_IMPORT) {
    return;
  }
  cache = registry->item_lookup;
  hash = driver_c_prog_hash_item_key(item->owner_module, item->label);
  start = (int32_t)(hash & (uint64_t)(registry->item_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, item->owner_module) &&
        driver_c_prog_text_eq(cache[slot].label, item->label)) {
      if (cache[slot].item != item) {
        if (driver_c_prog_is_routine_item(cache[slot].item) && driver_c_prog_is_routine_item(item)) {
          return;
        }
        cache[slot].item = NULL;
        cache[slot].ambiguous = 1;
        return;
      }
      return;
    }
    slot = (slot + 1) & (registry->item_lookup_cap - 1);
    if (slot == start) {
      driver_c_die("driver_c program runtime: item lookup cache full");
    }
  }
  cache[slot].owner_module = item->owner_module;
  cache[slot].label = item->label;
  cache[slot].item = item;
  cache[slot].used = 1;
}

static void driver_c_prog_registry_insert_item_kind_lookup(DriverCProgRegistry *registry,
                                                           DriverCProgItem *item) {
  DriverCProgItemKindLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || item == NULL || item->owner_module == NULL ||
      item->label == NULL || item->top_level_kind == NULL) {
    return;
  }
  cache = registry->item_kind_lookup;
  hash = driver_c_prog_hash_item_kind_key(item->owner_module, item->label, item->top_level_kind);
  start = (int32_t)(hash & (uint64_t)(registry->item_kind_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, item->owner_module) &&
        driver_c_prog_text_eq(cache[slot].label, item->label) &&
        driver_c_prog_text_eq(cache[slot].top_level_kind, item->top_level_kind)) {
      if (cache[slot].item != item) {
        cache[slot].item = NULL;
        cache[slot].ambiguous = 1;
        return;
      }
      return;
    }
    slot = (slot + 1) & (registry->item_kind_lookup_cap - 1);
    if (slot == start) {
      driver_c_die("driver_c program runtime: item kind cache full");
    }
  }
  cache[slot].owner_module = item->owner_module;
  cache[slot].label = item->label;
  cache[slot].top_level_kind = item->top_level_kind;
  cache[slot].item = item;
  cache[slot].used = 1;
}

static void driver_c_prog_registry_insert_import_lookup(DriverCProgRegistry *registry,
                                                        DriverCProgItem *item) {
  DriverCProgImportLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || item == NULL || item->owner_module == NULL || item->import_visible_name == NULL) {
    return;
  }
  if (item->top_level_tag != DRIVER_C_PROG_TOP_IMPORT) {
    return;
  }
  cache = registry->import_lookup;
  hash = driver_c_prog_hash_import_key(item->owner_module, item->import_visible_name);
  start = (int32_t)(hash & (uint64_t)(registry->import_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, item->owner_module) &&
        driver_c_prog_text_eq(cache[slot].visible_name, item->import_visible_name)) {
      if (cache[slot].item != item &&
          !driver_c_prog_text_eq(driver_c_prog_import_target_module_text(cache[slot].item),
                                 driver_c_prog_import_target_module_text(item))) {
        cache[slot].item = NULL;
        cache[slot].ambiguous = 1;
        return;
      }
      return;
    }
    slot = (slot + 1) & (registry->import_lookup_cap - 1);
    if (slot == start) {
      driver_c_die("driver_c program runtime: import cache full");
    }
  }
  cache[slot].owner_module = item->owner_module;
  cache[slot].visible_name = item->import_visible_name;
  cache[slot].item = item;
  cache[slot].used = 1;
}

static void driver_c_prog_registry_insert_type_decl_lookup(DriverCProgRegistry *registry,
                                                           DriverCProgTypeDecl *decl) {
  DriverCProgTypeDeclLookupEntry *cache = NULL;
  uint64_t hash = 0;
  int32_t slot = 0;
  int32_t start = 0;
  if (registry == NULL || decl == NULL || decl->owner_module == NULL || decl->decl_name == NULL) {
    return;
  }
  cache = registry->type_decl_lookup;
  hash = driver_c_prog_hash_type_decl_key(decl->owner_module, decl->decl_name);
  start = (int32_t)(hash & (uint64_t)(registry->type_decl_lookup_cap - 1));
  slot = start;
  while (cache[slot].used) {
    if (driver_c_prog_text_eq(cache[slot].owner_module, decl->owner_module) &&
        driver_c_prog_text_eq(cache[slot].decl_name, decl->decl_name)) {
      if (cache[slot].decl != decl) {
        cache[slot].decl = NULL;
        cache[slot].ambiguous = 1;
        return;
      }
      return;
    }
    slot = (slot + 1) & (registry->type_decl_lookup_cap - 1);
    if (slot == start) {
      driver_c_die("driver_c program runtime: type decl cache full");
    }
  }
  cache[slot].owner_module = decl->owner_module;
  cache[slot].decl_name = decl->decl_name;
  cache[slot].decl = decl;
  cache[slot].used = 1;
}

static void driver_c_prog_registry_load(DriverCProgRegistry *registry) {
  const char *text = NULL;
  const char *cursor = NULL;
  int32_t max_item_id = 0;
  int32_t i = 0;
  if (registry == NULL || registry->loaded) {
    return;
  }
  memset(registry, 0, sizeof(*registry));
  text = driver_c_prog_registry_symbol_text();
  registry->entry_item_id = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(text, "entry_item_id"));
  registry->entry_label = driver_c_find_line_value_dup(text, "entry_label");
  registry->item_count = driver_c_prog_parse_i32_or_zero(driver_c_find_line_value_dup(text, "item_count"));
  if (registry->item_count <= 0) {
    driver_c_die("driver_c program runtime: empty program registry");
  }
  registry->items = (DriverCProgItem *)driver_c_prog_xcalloc((size_t)registry->item_count, sizeof(DriverCProgItem));
  cursor = text;
  while (*cursor != '\0') {
    const char *line_start = cursor;
    const char *line_stop = cursor;
    if (driver_c_prog_text_starts_with(cursor, "item.")) {
      int32_t item_idx = 0;
      const char *p = cursor + 5;
      const char *key_start = NULL;
      const char *eq = NULL;
      while (*p >= '0' && *p <= '9') {
        item_idx = item_idx * 10 + (int32_t)(*p - '0');
        p = p + 1;
      }
      if (*p == '.' && item_idx >= 0 && item_idx < registry->item_count) {
        DriverCProgItem *item = &registry->items[item_idx];
        key_start = p + 1;
        while (*line_stop != '\0' && *line_stop != '\n') {
          line_stop = line_stop + 1;
        }
        eq = memchr(key_start, '=', (size_t)(line_stop - key_start));
        if (eq != NULL) {
          char *key = driver_c_dup_range(key_start, eq);
          char *value = driver_c_dup_range(eq + 1, line_stop);
          if (driver_c_prog_text_eq(key, "item_id")) item->item_id = driver_c_prog_parse_i32_or_zero(value);
          else if (driver_c_prog_text_eq(key, "label")) item->label = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "canonical_op")) item->canonical_op = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "top_level_kind")) {
            item->top_level_kind = value, value = NULL;
            item->top_level_tag = driver_c_prog_top_level_tag_from_text(item->top_level_kind);
          }
          else if (driver_c_prog_text_eq(key, "owner_module")) item->owner_module = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "source_path")) item->source_path = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "import_path")) item->import_path = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "import_target_module")) item->import_target_module = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "import_alias")) item->import_alias = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "import_category")) item->import_category = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "importc_symbol")) item->importc_symbol = value, value = NULL;
          else if (driver_c_prog_text_eq(key, "payload_hex")) {
            item->payload_text = driver_c_prog_hex_decode_text_dup(value);
          } else if (driver_c_prog_text_eq(key, "metadata_mode")) {
          }
          free(key);
          free(value);
        }
      }
    }
    while (*cursor != '\0' && *cursor != '\n') {
      cursor = cursor + 1;
    }
    if (*cursor == '\n') {
      cursor = cursor + 1;
    }
    if (cursor == line_start) {
      break;
    }
  }
  for (i = 0; i < registry->item_count; ++i) {
    if (registry->items[i].item_id > max_item_id) {
      max_item_id = registry->items[i].item_id;
    }
  }
  registry->items_by_id_len = max_item_id + 1;
  registry->items_by_id = (DriverCProgItem **)driver_c_prog_xcalloc((size_t)registry->items_by_id_len,
                                                                    sizeof(DriverCProgItem *));
  registry->global_values = (DriverCProgValue *)driver_c_prog_xcalloc((size_t)registry->items_by_id_len,
                                                                      sizeof(DriverCProgValue));
  registry->global_initialized = (int32_t *)driver_c_prog_xcalloc((size_t)registry->items_by_id_len,
                                                                  sizeof(int32_t));
  for (i = 0; i < registry->item_count; ++i) {
    DriverCProgItem *item = &registry->items[i];
    driver_c_prog_parse_item_payload(item);
    item->top_level_tag = driver_c_prog_top_level_tag_from_text(item->top_level_kind);
    if (item->top_level_tag == DRIVER_C_PROG_TOP_IMPORT) {
      item->import_visible_name = driver_c_prog_import_visible_name_dup(item);
    }
    if (item->top_level_tag == DRIVER_C_PROG_TOP_TYPE) {
      driver_c_prog_parse_type_item_decls(registry, item);
    }
    if (item->item_id >= 0 && item->item_id < registry->items_by_id_len) {
      registry->items_by_id[item->item_id] = item;
    }
  }
  registry->item_lookup_cap = driver_c_prog_lookup_cap_for_count(registry->item_count, 1024);
  registry->item_lookup =
      (DriverCProgItemLookupEntry *)driver_c_prog_xcalloc((size_t)registry->item_lookup_cap,
                                                          sizeof(DriverCProgItemLookupEntry));
  registry->item_kind_lookup_cap = driver_c_prog_lookup_cap_for_count(registry->item_count, 1024);
  registry->item_kind_lookup =
      (DriverCProgItemKindLookupEntry *)driver_c_prog_xcalloc((size_t)registry->item_kind_lookup_cap,
                                                              sizeof(DriverCProgItemKindLookupEntry));
  registry->import_lookup_cap = driver_c_prog_lookup_cap_for_count(registry->item_count, 256);
  registry->import_lookup =
      (DriverCProgImportLookupEntry *)driver_c_prog_xcalloc((size_t)registry->import_lookup_cap,
                                                            sizeof(DriverCProgImportLookupEntry));
  registry->type_decl_lookup_cap = driver_c_prog_lookup_cap_for_count(registry->type_decl_count, 256);
  registry->type_decl_lookup =
      (DriverCProgTypeDeclLookupEntry *)driver_c_prog_xcalloc((size_t)registry->type_decl_lookup_cap,
                                                              sizeof(DriverCProgTypeDeclLookupEntry));
  registry->visible_item_cache_cap = driver_c_prog_lookup_cap_for_count(registry->item_count, 1024);
  registry->visible_item_cache =
      (DriverCProgVisibleItemCacheEntry *)driver_c_prog_xcalloc((size_t)registry->visible_item_cache_cap,
                                                                sizeof(DriverCProgVisibleItemCacheEntry));
  registry->visible_type_decl_cache_cap = driver_c_prog_lookup_cap_for_count(registry->type_decl_count, 256);
  registry->visible_type_decl_cache =
      (DriverCProgVisibleTypeDeclCacheEntry *)driver_c_prog_xcalloc((size_t)registry->visible_type_decl_cache_cap,
                                                                    sizeof(DriverCProgVisibleTypeDeclCacheEntry));
  registry->routine_type_args_lookup_cap = driver_c_prog_lookup_cap_for_count(registry->item_count * 4, 256);
  registry->routine_type_args_lookup =
      (DriverCProgRoutineTypeArgsLookupEntry *)driver_c_prog_xcalloc((size_t)registry->routine_type_args_lookup_cap,
                                                                     sizeof(DriverCProgRoutineTypeArgsLookupEntry));
  registry->zero_plan_cache_cap = driver_c_prog_lookup_cap_for_count(registry->item_count * 8, 1024);
  registry->zero_plan_cache =
      (DriverCProgZeroPlanCacheEntry *)driver_c_prog_xcalloc((size_t)registry->zero_plan_cache_cap,
                                                             sizeof(DriverCProgZeroPlanCacheEntry));
  for (i = 0; i < registry->item_count; ++i) {
    driver_c_prog_registry_insert_item_lookup(registry, &registry->items[i]);
    driver_c_prog_registry_insert_item_kind_lookup(registry, &registry->items[i]);
    driver_c_prog_registry_insert_import_lookup(registry, &registry->items[i]);
  }
  for (i = 0; i < registry->type_decl_count; ++i) {
    driver_c_prog_registry_insert_type_decl_lookup(registry, &registry->type_decls[i]);
  }
  for (i = 0; i < registry->item_count; ++i) {
    driver_c_prog_collect_item_implicit_type_params(registry, &registry->items[i]);
  }
  registry->loaded = 1;
}

static DriverCProgRegistry *driver_c_prog_registry_get(void) {
  driver_c_prog_registry_load(&driver_c_prog_registry_singleton);
  return &driver_c_prog_registry_singleton;
}

static char *driver_c_provider_field_for_module_dup(const char *plan_text,
                                                    const char *module_name,
                                                    const char *field_name) {
  const char *cursor = NULL;
  const char *name_prefix = "provider_module.";
  size_t name_prefix_len = strlen(name_prefix);
  size_t module_name_len = 0;
  char key_buf[256];
  if (plan_text == NULL || module_name == NULL || field_name == NULL ||
      module_name[0] == '\0' || field_name[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  module_name_len = strlen(module_name);
  cursor = plan_text;
  while (*cursor != '\0') {
    const char *line_start = cursor;
    const char *line_stop = cursor;
    const char *suffix = NULL;
    const char *eq = NULL;
    int idx_len = 0;
    while (*line_stop != '\0' && *line_stop != '\n') {
      line_stop = line_stop + 1;
    }
    if ((size_t)(line_stop - line_start) > name_prefix_len &&
        memcmp(line_start, name_prefix, name_prefix_len) == 0) {
      suffix = line_start + name_prefix_len;
      while (suffix < line_stop && *suffix >= '0' && *suffix <= '9') {
        suffix = suffix + 1;
        idx_len = idx_len + 1;
      }
      if (idx_len > 0 &&
          suffix + 6 < line_stop &&
          memcmp(suffix, ".name=", 6u) == 0 &&
          (size_t)(line_stop - (suffix + 6)) == module_name_len &&
          memcmp(suffix + 6, module_name, module_name_len) == 0 &&
          name_prefix_len + (size_t)idx_len + 1u + strlen(field_name) + 1u < sizeof(key_buf)) {
        memcpy(key_buf, name_prefix, name_prefix_len);
        memcpy(key_buf + name_prefix_len, line_start + name_prefix_len, (size_t)idx_len);
        eq = key_buf + name_prefix_len + (size_t)idx_len;
        * (char *)eq = '.';
        strcpy((char *)eq + 1, field_name);
        strcat(key_buf, "=");
        return driver_c_find_line_value_dup(plan_text, key_buf);
      }
    }
    cursor = *line_stop == '\0' ? line_stop : line_stop + 1;
  }
  return driver_c_dup_cstring("");
}

static char *driver_c_absolute_path_dup(const char *path) {
  char *cwd = NULL;
  char *out = NULL;
  size_t cwd_len = 0;
  size_t path_len = 0;
  if (path == NULL || path[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  if (path[0] == '/') {
    return driver_c_dup_cstring(path);
  }
  cwd = getcwd(NULL, 0);
  if (cwd == NULL || cwd[0] == '\0') {
    free(cwd);
    return driver_c_dup_cstring(path);
  }
  cwd_len = strlen(cwd);
  path_len = strlen(path);
  out = (char *)malloc(cwd_len + 1u + path_len + 1u);
  if (out == NULL) {
    free(cwd);
    return NULL;
  }
  memcpy(out, cwd, cwd_len);
  out[cwd_len] = '/';
  memcpy(out + cwd_len + 1u, path, path_len);
  out[cwd_len + 1u + path_len] = '\0';
  free(cwd);
  return out;
}

ChengStrBridge driver_c_machine_target_architecture_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("aarch64");
}

ChengStrBridge driver_c_machine_target_obj_format_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("macho");
}

ChengStrBridge driver_c_machine_target_symbol_prefix_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("_");
}

ChengStrBridge driver_c_machine_target_text_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("__TEXT,__text");
}

ChengStrBridge driver_c_machine_target_cstring_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("__TEXT,__cstring");
}

ChengStrBridge driver_c_machine_target_symtab_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("__LINKEDIT,symtab");
}

ChengStrBridge driver_c_machine_target_strtab_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("__LINKEDIT,strtab");
}

ChengStrBridge driver_c_machine_target_reloc_section_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("__LINKEDIT,reloc");
}

ChengStrBridge driver_c_machine_target_call_relocation_kind_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("ARM64_RELOC_BRANCH26");
}

ChengStrBridge driver_c_machine_target_metadata_page_relocation_kind_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("ARM64_RELOC_PAGE21");
}

ChengStrBridge driver_c_machine_target_metadata_pageoff_relocation_kind_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("ARM64_RELOC_PAGEOFF12");
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
  return driver_c_machine_target_text_bridge("macos");
}

int32_t driver_c_machine_target_darwin_minos_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return 0x000d0000;
}

ChengStrBridge driver_c_machine_target_darwin_minos_text_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("13.0");
}

ChengStrBridge driver_c_machine_target_darwin_arch_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("arm64");
}

ChengStrBridge driver_c_machine_target_darwin_sdk_name_bridge(ChengStrBridge target) {
  driver_c_require_machine_target_bridge(target);
  return driver_c_machine_target_text_bridge("macosx");
}

static char *driver_c_join_path2_dup(const char *left, const char *right) {
  size_t left_len = 0;
  size_t right_len = 0;
  int need_sep = 0;
  char *out = NULL;
  if (left == NULL || left[0] == '\0') {
    return driver_c_dup_cstring(right);
  }
  if (right == NULL || right[0] == '\0') {
    return driver_c_dup_cstring(left);
  }
  left_len = strlen(left);
  right_len = strlen(right);
  need_sep = left[left_len - 1] != '/';
  out = (char *)malloc(left_len + (size_t)need_sep + right_len + 1u);
  if (out == NULL) return NULL;
  memcpy(out, left, left_len);
  if (need_sep) {
    out[left_len] = '/';
  }
  memcpy(out + left_len + (size_t)need_sep, right, right_len);
  out[left_len + (size_t)need_sep + right_len] = '\0';
  return out;
}

static void driver_c_die(const char *message) {
  fputs(message != NULL ? message : "driver_c fatal", stderr);
  fputc('\n', stderr);
  fflush(stderr);
  exit(1);
}

static void driver_c_die_errno(const char *message, const char *path) {
  if (message != NULL && message[0] != '\0') {
    fputs(message, stderr);
  } else {
    fputs("driver_c errno", stderr);
  }
  if (path != NULL && path[0] != '\0') {
    fputs(": ", stderr);
    fputs(path, stderr);
  }
  if (errno != 0) {
    fputs(": ", stderr);
    fputs(strerror(errno), stderr);
  }
  fputc('\n', stderr);
  fflush(stderr);
  exit(1);
}

__attribute__((weak)) int32_t driver_c_compiler_core_print_usage_bridge(void) {
  static const char *const lines[] = {
      "cheng_v2",
      "usage: cheng_v2 <command>",
      "",
      "commands:",
      "  help                         show this message",
      "  status                       print the current dev-track contract",
      "  <tooling-command>            forward to cheng_tooling_v2",
      "",
      "tooling commands:",
      "  stage-selfhost-host          build stage1 -> stage2 -> stage3 native closure",
      "  tooling-selfhost-host        run the native tooling selfhost orchestration",
      "  tooling-selfhost-check       verify tooling stage0 -> stage1 -> stage2 fixed-point closure",
      "  release-compile              emit a deterministic release artifact spec",
      "  lsmr-address                 emit a deterministic LSMR address",
      "  lsmr-route-plan              emit a deterministic canonical LSMR route plan",
      "  outline-parse                emit a compiler_core outline parse",
      "  machine-plan                 emit a deterministic machine plan",
      "  obj-image                    emit a deterministic object image",
      "  obj-file                     emit a deterministic object-file byte layout",
      "  system-link-plan             emit a deterministic native system-link plan",
      "  system-link-exec             materialize object files and invoke the deterministic system linker",
      "  resolve-manifest             resolve a deterministic source manifest",
      "  publish-source-manifest      emit a source manifest artifact",
      "  publish-rule-pack            emit a rule-pack artifact",
      "  publish-compiler-rule-pack   emit a compiler rule-pack artifact",
      "  verify-network-selfhost      verify manifest + topology + rule-pack closure",
  };
  size_t i;
  for (i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
    fputs(lines[i], stdout);
    fputc('\n', stdout);
  }
  return 0;
}

__attribute__((weak)) int32_t driver_c_compiler_core_print_status_bridge(void) {
  fputs("cheng_v2: dev-only entry\n", stdout);
  fputs("track=dev\n", stdout);
  fputs("execution=direct_exe\n", stdout);
  fputs("tooling_forwarded=1\n", stdout);
  fputs("tooling_entry=tooling/cheng_tooling_v2\n", stdout);
  fputs("parallel=function_task\n", stdout);
  fputs("soa_index_only=1\n", stdout);
  fputs("infra_surface=1\n", stdout);
  return 0;
}

static int driver_c_bridge_text_eq_raw(ChengStrBridge text, const char *raw) {
  size_t raw_len = 0;
  if (raw == NULL) {
    raw = "";
  }
  if (text.ptr == NULL || text.len < 0) {
    return 0;
  }
  raw_len = strlen(raw);
  if ((size_t)text.len != raw_len) {
    return 0;
  }
  if (raw_len == 0) {
    return 1;
  }
  return memcmp(text.ptr, raw, raw_len) == 0 ? 1 : 0;
}

static int driver_c_starts_with(const char *text, const char *prefix) {
  size_t n = 0;
  if (text == NULL || prefix == NULL) return 0;
  n = strlen(prefix);
  return strncmp(text, prefix, n) == 0;
}

static int driver_c_path_is_dir(const char *path) {
  struct stat st;
  if (path == NULL || path[0] == '\0') return 0;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int driver_c_path_is_file(const char *path) {
  struct stat st;
  if (path == NULL || path[0] == '\0') return 0;
  if (stat(path, &st) != 0) return 0;
  return S_ISREG(st.st_mode) ? 1 : 0;
}

static void driver_c_path_join_buf(char *out,
                                   size_t out_cap,
                                   const char *left,
                                   const char *right) {
  int n = 0;
  if (left == NULL || left[0] == '\0') {
    n = snprintf(out, out_cap, "%s", right != NULL ? right : "");
  } else if (right == NULL || right[0] == '\0') {
    n = snprintf(out, out_cap, "%s", left);
  } else if (left[strlen(left) - 1] == '/') {
    n = snprintf(out, out_cap, "%s%s", left, right);
  } else {
    n = snprintf(out, out_cap, "%s/%s", left, right);
  }
  if (n < 0 || (size_t)n >= out_cap) {
    driver_c_die("driver_c path too long");
  }
}

static void driver_c_parent_dir_inplace(char *path) {
  char *slash = NULL;
  if (path == NULL || path[0] == '\0') return;
  slash = strrchr(path, '/');
  if (slash == NULL) {
    path[0] = '\0';
    return;
  }
  if (slash == path) {
    path[1] = '\0';
    return;
  }
  *slash = '\0';
}

static int driver_c_path_has_prefix(const char *path, const char *prefix) {
  size_t prefix_len = 0;
  if (path == NULL || prefix == NULL) return 0;
  prefix_len = strlen(prefix);
  if (strncmp(path, prefix, prefix_len) != 0) return 0;
  return path[prefix_len] == '\0' || path[prefix_len] == '/';
}

static void driver_c_normalize_relpath_inplace(char *path) {
  char temp[PATH_MAX];
  char *segments[PATH_MAX / 2];
  size_t seg_count = 0;
  char *cursor = NULL;
  size_t i = 0;
  if (path == NULL || path[0] == '\0') return;
  if (strlen(path) >= sizeof(temp)) {
    driver_c_die("driver_c relative path too long");
  }
  memcpy(temp, path, strlen(path) + 1u);
  cursor = temp;
  while (*cursor != '\0') {
    char *start = cursor;
    while (*cursor != '\0' && *cursor != '/') {
      cursor = cursor + 1;
    }
    if (*cursor == '/') {
      *cursor = '\0';
      cursor = cursor + 1;
    }
    if (start[0] == '\0' || strcmp(start, ".") == 0) {
      continue;
    }
    if (strcmp(start, "..") == 0) {
      if (seg_count > 0 && strcmp(segments[seg_count - 1], "..") != 0) {
        seg_count = seg_count - 1;
      } else {
        segments[seg_count++] = start;
      }
      continue;
    }
    segments[seg_count++] = start;
  }
  path[0] = '\0';
  for (i = 0; i < seg_count; ++i) {
    if (i > 0) {
      if (strlen(path) + 1u >= PATH_MAX) {
        driver_c_die("driver_c normalized relative path too long");
      }
      strcat(path, "/");
    }
    if (strlen(path) + strlen(segments[i]) >= PATH_MAX) {
      driver_c_die("driver_c normalized relative path too long");
    }
    strcat(path, segments[i]);
  }
}

static int driver_c_relative_to_prefix(const char *path,
                                       const char *prefix,
                                       char *out,
                                       size_t out_cap) {
  size_t prefix_len = 0;
  const char *rel = NULL;
  if (!driver_c_path_has_prefix(path, prefix)) return 0;
  prefix_len = strlen(prefix);
  rel = path + prefix_len;
  if (*rel == '/') rel = rel + 1;
  if (snprintf(out, out_cap, "%s", rel) >= (int)out_cap) {
    driver_c_die("driver_c relative path too long");
  }
  driver_c_normalize_relpath_inplace(out);
  return 1;
}

static int driver_c_find_repo_root_from(const char *start_path,
                                        char *out,
                                        size_t out_cap) {
  char probe[PATH_MAX];
  char marker[PATH_MAX];
  if (start_path == NULL || start_path[0] == '\0') return 0;
  if (realpath(start_path, probe) == NULL) return 0;
  if (!driver_c_path_is_dir(probe)) {
    driver_c_parent_dir_inplace(probe);
  }
  while (probe[0] != '\0') {
    driver_c_path_join_buf(marker, sizeof(marker), probe, "v2/cheng-package.toml");
    if (driver_c_path_is_file(marker)) {
      if (snprintf(out, out_cap, "%s", probe) >= (int)out_cap) {
        driver_c_die("driver_c repo root path too long");
      }
      return 1;
    }
    if (strcmp(probe, "/") == 0) break;
    driver_c_parent_dir_inplace(probe);
  }
  return 0;
}

static void driver_c_detect_repo_root(const char *hint_a,
                                      const char *hint_b,
                                      char *out,
                                      size_t out_cap) {
  char cwd[PATH_MAX];
  if (driver_c_find_repo_root_from(hint_a, out, out_cap)) return;
  if (driver_c_find_repo_root_from(hint_b, out, out_cap)) return;
  if (getcwd(cwd, sizeof(cwd)) != NULL && driver_c_find_repo_root_from(cwd, out, out_cap)) {
    return;
  }
  driver_c_die("driver_c failed to detect repo root");
}

static void driver_c_resolve_existing_input_path(const char *repo_root,
                                                 const char *raw,
                                                 char *abs_out,
                                                 size_t abs_out_cap) {
  char cwd[PATH_MAX];
  char probe[PATH_MAX];
  (void)abs_out_cap;
  if (raw == NULL || raw[0] == '\0') {
    driver_c_die("driver_c missing input path");
  }
  if (raw[0] == '/') {
    if (snprintf(probe, sizeof(probe), "%s", raw) >= (int)sizeof(probe)) {
      driver_c_die("driver_c input path too long");
    }
  } else if (driver_c_starts_with(raw, "./") || driver_c_starts_with(raw, "../")) {
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      driver_c_die_errno("driver_c getcwd failed", "");
    }
    driver_c_path_join_buf(probe, sizeof(probe), cwd, raw);
  } else {
    driver_c_path_join_buf(probe, sizeof(probe), repo_root, raw);
  }
  if (realpath(probe, abs_out) == NULL) {
    driver_c_die_errno("driver_c realpath failed", probe);
  }
}

static void driver_c_resolve_output_path(const char *repo_root,
                                         const char *raw,
                                         char *out,
                                         size_t out_cap) {
  char cwd[PATH_MAX];
  if (raw == NULL || raw[0] == '\0') {
    driver_c_die("driver_c missing output path");
  }
  if (raw[0] == '/') {
    if (snprintf(out, out_cap, "%s", raw) >= (int)out_cap) {
      driver_c_die("driver_c output path too long");
    }
    return;
  }
  if (driver_c_starts_with(raw, "./") || driver_c_starts_with(raw, "../")) {
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      driver_c_die_errno("driver_c getcwd failed", "");
    }
    driver_c_path_join_buf(out, out_cap, cwd, raw);
    return;
  }
  driver_c_path_join_buf(out, out_cap, repo_root, raw);
}

static const char *driver_c_cli_arg_raw(int32_t idx) {
  return __cheng_rt_paramStr(idx);
}

static int32_t driver_c_cli_argc(void) {
  return __cheng_rt_paramCount();
}

static const char *driver_c_program_name_raw(void);

static char **driver_c_cli_current_argv_dup(int *out_argc) {
  int32_t param_count = driver_c_cli_argc();
  int argc = (int)param_count;
  int32_t i = 0;
  char **argv;
  if (argc <= 0) {
    argc = 1;
  }
  argv = (char **)malloc(sizeof(char *) * (size_t)(argc + 1));
  if (argv == NULL) return NULL;
  if (param_count <= 0) {
    argv[0] = (char *)driver_c_program_name_raw();
    argv[1] = NULL;
    if (out_argc != NULL) *out_argc = 1;
    return argv;
  }
  for (i = 0; i < param_count; ++i) {
    argv[i] = (char *)driver_c_cli_arg_raw(i);
  }
  argv[argc] = NULL;
  if (out_argc != NULL) *out_argc = argc;
  return argv;
}

static char *driver_c_payload_label_dup(const char *payload) {
  const char *start = NULL;
  const char *stop = NULL;
  if (payload == NULL || payload[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  start = strstr(payload, "|label=");
  if (start != NULL) {
    start = start + 1;
  } else if (strncmp(payload, "label=", 6u) == 0) {
    start = payload;
  } else {
    return driver_c_dup_cstring("");
  }
  start = start + 6;
  stop = start;
  while (*stop != '\0' && *stop != '|') {
    stop = stop + 1;
  }
  return driver_c_dup_range(start, stop);
}

static int driver_c_flag_key_matches_raw(const char *raw, const char *key) {
  size_t key_len = 0;
  if (raw == NULL || key == NULL || key[0] == '\0') return 0;
  key_len = strlen(key);
  return strlen(raw) == key_len && memcmp(raw, key, key_len) == 0;
}

static int driver_c_flag_inline_value_raw(const char *raw,
                                          const char *key,
                                          const char **out_value) {
  size_t key_len = 0;
  if (out_value == NULL) return 0;
  *out_value = NULL;
  if (raw == NULL || key == NULL || key[0] == '\0') return 0;
  key_len = strlen(key);
  if (strncmp(raw, key, key_len) != 0) return 0;
  if (raw[key_len] == ':' || raw[key_len] == '=') {
    *out_value = raw + key_len + 1u;
    return 1;
  }
  return 0;
}

static char *driver_c_read_flag_dup_raw(const char *key, const char *default_value) {
  int32_t argc = driver_c_cli_argc();
  int32_t i = 0;
  if (argc <= 1) {
    return driver_c_dup_cstring(default_value);
  }
  for (i = 1; i < argc; ++i) {
    const char *raw = driver_c_cli_arg_raw(i);
    const char *inline_value = NULL;
    const char *next_raw = NULL;
    if (raw == NULL) continue;
    if (driver_c_flag_inline_value_raw(raw, key, &inline_value)) {
      return driver_c_dup_cstring(inline_value);
    }
    if (!driver_c_flag_key_matches_raw(raw, key)) continue;
    if (i + 1 >= argc) {
      return driver_c_dup_cstring(default_value);
    }
    next_raw = driver_c_cli_arg_raw(i + 1);
    return driver_c_dup_cstring(next_raw != NULL ? next_raw : "");
  }
  return driver_c_dup_cstring(default_value);
}

static int32_t driver_c_read_int32_flag_default_raw(const char *key, int32_t default_value) {
  char *raw = driver_c_read_flag_dup_raw(key, "");
  char *end = NULL;
  long parsed = 0;
  int32_t out = default_value;
  if (raw != NULL && raw[0] != '\0') {
    errno = 0;
    parsed = strtol(raw, &end, 10);
    if (errno == 0 && end != raw && end != NULL && *end == '\0' &&
        parsed >= -2147483648L && parsed <= 2147483647L) {
      out = (int32_t)parsed;
    }
  }
  free(raw);
  return out;
}

static void driver_c_sha256_init(DriverCSha256Ctx *ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667U;
  ctx->state[1] = 0xbb67ae85U;
  ctx->state[2] = 0x3c6ef372U;
  ctx->state[3] = 0xa54ff53aU;
  ctx->state[4] = 0x510e527fU;
  ctx->state[5] = 0x9b05688cU;
  ctx->state[6] = 0x1f83d9abU;
  ctx->state[7] = 0x5be0cd19U;
}

static void driver_c_sha256_transform(DriverCSha256Ctx *ctx, const uint8_t data[]) {
  uint32_t a, b, c, d, e, f, g, h;
  uint32_t m[64];
  uint32_t t1, t2;
  size_t i = 0;
  for (i = 0; i < 16; ++i) {
    m[i] = ((uint32_t)data[i * 4] << 24) |
           ((uint32_t)data[i * 4 + 1] << 16) |
           ((uint32_t)data[i * 4 + 2] << 8) |
           ((uint32_t)data[i * 4 + 3]);
  }
  for (i = 16; i < 64; ++i) {
    m[i] = DRIVER_C_SHA256_SIG1(m[i - 2]) + m[i - 7] +
           DRIVER_C_SHA256_SIG0(m[i - 15]) + m[i - 16];
  }
  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];
  for (i = 0; i < 64; ++i) {
    t1 = h + DRIVER_C_SHA256_EP1(e) + DRIVER_C_SHA256_CH(e, f, g) +
         driver_c_sha256_table[i] + m[i];
    t2 = DRIVER_C_SHA256_EP0(a) + DRIVER_C_SHA256_MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void driver_c_sha256_update(DriverCSha256Ctx *ctx, const uint8_t *data, size_t len) {
  size_t i = 0;
  for (i = 0; i < len; ++i) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == 64) {
      driver_c_sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

static void driver_c_sha256_final(DriverCSha256Ctx *ctx, uint8_t hash[32]) {
  size_t i = ctx->datalen;
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) ctx->data[i++] = 0x00;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64) ctx->data[i++] = 0x00;
    driver_c_sha256_transform(ctx, ctx->data);
    memset(ctx->data, 0, 56);
  }
  ctx->bitlen += (uint64_t)ctx->datalen * 8u;
  ctx->data[63] = (uint8_t)(ctx->bitlen);
  ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
  ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
  ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
  ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
  ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
  ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
  ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
  driver_c_sha256_transform(ctx, ctx->data);
  for (i = 0; i < 4; ++i) {
    hash[i] = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0xffU);
    hash[i + 4] = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0xffU);
    hash[i + 8] = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0xffU);
    hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0xffU);
    hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0xffU);
    hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0xffU);
    hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0xffU);
    hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0xffU);
  }
}

static char *driver_c_sha256_hex_file_dup(const char *path) {
  static const char hex[] = "0123456789abcdef";
  DriverCSha256Ctx ctx;
  uint8_t digest[32];
  uint8_t buf[4096];
  char *out = NULL;
  FILE *f = NULL;
  size_t n = 0;
  size_t i = 0;
  if (path == NULL || path[0] == '\0') {
    return driver_c_dup_cstring("");
  }
  f = fopen(path, "rb");
  if (f == NULL) {
    driver_c_die_errno("driver_c fopen failed", path);
  }
  driver_c_sha256_init(&ctx);
  for (;;) {
    n = fread(buf, 1, sizeof(buf), f);
    if (n > 0) {
      driver_c_sha256_update(&ctx, buf, n);
    }
    if (n < sizeof(buf)) {
      if (ferror(f)) {
        fclose(f);
        driver_c_die_errno("driver_c fread failed", path);
      }
      break;
    }
  }
  fclose(f);
  driver_c_sha256_final(&ctx, digest);
  out = (char *)malloc(65u);
  if (out == NULL) {
    driver_c_die("driver_c malloc failed");
  }
  for (i = 0; i < 32; ++i) {
    out[i * 2] = hex[digest[i] >> 4];
    out[i * 2 + 1] = hex[digest[i] & 0x0fU];
  }
  out[64] = '\0';
  return out;
}

static const char *driver_c_program_name_raw(void) {
#if defined(__APPLE__)
  char ***argv_ptr = _NSGetArgv();
  if (argv_ptr == NULL || *argv_ptr == NULL || (*argv_ptr)[0] == NULL) {
    return "";
  }
  return (*argv_ptr)[0];
#else
  return "";
#endif
}

static void driver_c_mkdir_parents_if_needed(const char *path) {
  char *buf = NULL;
  char *slash = NULL;
  char *p = NULL;
  if (path == NULL || path[0] == '\0') return;
  buf = (char *)malloc(strlen(path) + 1u);
  if (buf == NULL) return;
  strcpy(buf, path);
  slash = strrchr(buf, '/');
  if (slash == NULL) {
    free(buf);
    return;
  }
  *slash = '\0';
  if (buf[0] == '\0') {
    free(buf);
    return;
  }
  for (p = buf + 1; *p != '\0'; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(buf, 0755);
    *p = '/';
  }
  mkdir(buf, 0755);
  free(buf);
}

static void driver_c_mkdir_all_if_needed(const char *path) {
  char *buf = NULL;
  char *p = NULL;
  if (path == NULL || path[0] == '\0') return;
  buf = driver_c_dup_cstring(path);
  if (buf == NULL) return;
  for (p = buf + 1; *p != '\0'; ++p) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(buf, 0755);
    *p = '/';
  }
  mkdir(buf, 0755);
  free(buf);
}

static int32_t driver_c_compare_files_bytes_impl(const char *left_path, const char *right_path) {
  FILE *left = NULL;
  FILE *right = NULL;
  unsigned char left_buf[4096];
  unsigned char right_buf[4096];
  int32_t same = 1;
  if (left_path == NULL || right_path == NULL || left_path[0] == '\0' || right_path[0] == '\0') {
    return 0;
  }
  left = fopen(left_path, "rb");
  if (left == NULL) return 0;
  right = fopen(right_path, "rb");
  if (right == NULL) {
    fclose(left);
    return 0;
  }
  for (;;) {
    size_t left_n = fread(left_buf, 1, sizeof(left_buf), left);
    size_t right_n = fread(right_buf, 1, sizeof(right_buf), right);
    if (left_n != right_n) {
      same = 0;
      break;
    }
    if (left_n == 0u) {
      break;
    }
    if (memcmp(left_buf, right_buf, left_n) != 0) {
      same = 0;
      break;
    }
  }
  fclose(left);
  fclose(right);
  return same;
}

static void driver_c_abort_with_label_output(const char *label, const char *output) {
  if (label != NULL && label[0] != '\0') {
    fputs(label, stderr);
  } else {
    fputs("driver_c bridge failure", stderr);
  }
  if (output != NULL && output[0] != '\0') {
    fputc('\n', stderr);
    fputs(output, stderr);
  }
  fputc('\n', stderr);
  fflush(stderr);
  exit(1);
}

static int64_t driver_c_monotime_ns(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static const char *driver_c_seq_string_item(ChengSeqHeader seq, int32_t idx) {
  if (idx < 0 || idx >= seq.len || seq.buffer == NULL) return "";
  {
    char **items = (char **)seq.buffer;
    const char *value = items[idx];
    return value != NULL ? value : "";
  }
}

static char **driver_c_exec_build_argv(const char *file_path, ChengSeqHeader argv_seq) {
  size_t extra_count = (size_t)(argv_seq.len > 0 ? argv_seq.len : 0);
  size_t i = 0;
  char **argv = (char **)malloc(sizeof(char *) * (extra_count + 2u));
  if (argv == NULL) return NULL;
  argv[0] = (char *)(file_path != NULL ? file_path : "");
  for (i = 0; i < extra_count; ++i) {
    argv[i + 1u] = (char *)driver_c_seq_string_item(argv_seq, (int32_t)i);
  }
  argv[extra_count + 1u] = NULL;
  return argv;
}

static int64_t driver_c_wait_status_timeout(pid_t pid, int32_t timeout_sec) {
  int status = 0;
  int timed_out = 0;
  int term_sent = 0;
  int64_t term_sent_ns = 0;
  int64_t deadline_ns = 0;
  if (timeout_sec > 0) {
    deadline_ns = driver_c_monotime_ns() + (int64_t)timeout_sec * 1000000000LL;
  }
  for (;;) {
    pid_t wait_rc = waitpid(pid, &status, WNOHANG);
    if (wait_rc == pid) {
      break;
    }
    if (wait_rc < 0) {
      status = 0;
      break;
    }
    if (deadline_ns > 0) {
      int64_t now_ns = driver_c_monotime_ns();
      if (now_ns >= deadline_ns) {
        if (!term_sent) {
          kill(pid, SIGTERM);
          term_sent = 1;
          term_sent_ns = now_ns;
        } else if (now_ns - term_sent_ns >= 50000000LL) {
          kill(pid, SIGKILL);
        }
        timed_out = 1;
      }
    }
    usleep(1000);
  }
  if (timed_out) return 124;
  if (WIFEXITED(status)) return (int64_t)WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return (int64_t)(128 + WTERMSIG(status));
  return (int64_t)status;
}

static char *driver_c_exec_file_capture_minimal(const char *file_path,
                                                ChengSeqHeader argv_seq,
                                                const char *working_dir,
                                                int32_t timeout_sec,
                                                int64_t *exit_code) {
  char capture_path[] = "/tmp/cheng_exec_capture.XXXXXX";
  char **argv = NULL;
  int capture_fd = -1;
  pid_t pid = -1;
  char *out = NULL;
  if (exit_code != NULL) *exit_code = -1;
  if (file_path == NULL || file_path[0] == '\0') {
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  argv = driver_c_exec_build_argv(file_path, argv_seq);
  if (argv == NULL) {
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  capture_fd = mkstemp(capture_path);
  if (capture_fd < 0) {
    free(argv);
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  pid = fork();
  if (pid < 0) {
    close(capture_fd);
    unlink(capture_path);
    free(argv);
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
    return out;
  }
  if (pid == 0) {
    if (working_dir != NULL && working_dir[0] != '\0') {
      chdir(working_dir);
    }
    dup2(capture_fd, STDOUT_FILENO);
    dup2(capture_fd, STDERR_FILENO);
    close(capture_fd);
    execve(file_path, argv, environ);
    {
      const char *msg = strerror(errno);
      if (msg == NULL) msg = "execve failed";
      dprintf(STDERR_FILENO, "%s\n", msg);
    }
    _exit(127);
  }
  close(capture_fd);
  capture_fd = -1;
  free(argv);
  if (exit_code != NULL) {
    *exit_code = driver_c_wait_status_timeout(pid, timeout_sec);
  } else {
    (void)driver_c_wait_status_timeout(pid, timeout_sec);
  }
  out = driver_c_read_file_all(capture_path);
  unlink(capture_path);
  if (out == NULL) {
    out = (char *)malloc(1u);
    if (out != NULL) out[0] = '\0';
  }
  return out;
}

__attribute__((weak)) void *cheng_fopen(const char *filename, const char *mode) {
  if (filename == NULL || mode == NULL) return NULL;
  return (void *)fopen(filename, mode);
}

__attribute__((weak)) void *get_stdin(void) { return (void *)stdin; }

__attribute__((weak)) void *get_stdout(void) { return (void *)stdout; }

__attribute__((weak)) void *get_stderr(void) { return (void *)stderr; }

__attribute__((weak)) int32_t cheng_fclose(void *f) {
  if (f == NULL) return -1;
  return (int32_t)fclose((FILE *)f);
}

__attribute__((weak)) int32_t cheng_fread(void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || stream == NULL || size <= 0 || n <= 0) return 0;
  return (int32_t)fread(ptr, (size_t)size, (size_t)n, (FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fwrite(const void *ptr, int64_t size, int64_t n, void *stream) {
  if (ptr == NULL || stream == NULL || size <= 0 || n <= 0) return 0;
  return (int32_t)fwrite(ptr, (size_t)size, (size_t)n, (FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fseek(void *stream, int64_t offset, int32_t whence) {
  if (stream == NULL) return -1;
  return (int32_t)fseeko((FILE *)stream, (off_t)offset, whence);
}

__attribute__((weak)) int64_t cheng_ftell(void *stream) {
  if (stream == NULL) return -1;
  return (int64_t)ftello((FILE *)stream);
}

__attribute__((weak)) int32_t cheng_system_entropy_fill(void *dst, int32_t len) {
  if (dst == NULL || len <= 0) return 0;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  arc4random_buf(dst, (size_t)len);
  return 1;
#elif defined(__linux__)
  uint8_t *cursor = (uint8_t *)dst;
  int32_t remaining = len;
  while (remaining > 0) {
    size_t chunk = remaining > 256 ? 256u : (size_t)remaining;
    if (getentropy(cursor, chunk) != 0) {
      return 0;
    }
    cursor += chunk;
    remaining -= (int32_t)chunk;
  }
  return 1;
#else
  return 0;
#endif
}

__attribute__((weak)) int32_t cheng_fflush(void *stream) {
  if (stream == NULL) return -1;
  return (int32_t)fflush((FILE *)stream);
}

__attribute__((weak)) int32_t cheng_fgetc(void *stream) {
  if (stream == NULL) return -1;
  return (int32_t)fgetc((FILE *)stream);
}

__attribute__((weak)) int32_t cheng_file_exists(const char *path) {
  struct stat st;
  if (path == NULL) return 0;
  return stat(path, &st) == 0 ? 1 : 0;
}

__attribute__((weak)) int64_t cheng_file_size(const char *path) {
  struct stat st;
  if (path == NULL) return -1;
  if (stat(path, &st) != 0) return -1;
  return (int64_t)st.st_size;
}

__attribute__((weak)) int32_t cheng_dir_exists(const char *path) {
  struct stat st;
  if (path == NULL) return 0;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

__attribute__((weak)) int32_t cheng_mkdir1(const char *path) {
  if (path == NULL || path[0] == '\0') return -1;
  return mkdir(path, 0755);
}

__attribute__((weak)) int64_t cheng_file_mtime(const char *path) {
  struct stat st;
  if (path == NULL) return -1;
  if (stat(path, &st) != 0) return -1;
  return (int64_t)st.st_mtime;
}

__attribute__((weak)) char *cheng_getcwd(void) {
  static char buf[4096];
  if (getcwd(buf, sizeof(buf)) == NULL) return "";
  buf[sizeof(buf) - 1] = 0;
  return buf;
}

__attribute__((weak)) int32_t cheng_rawbytes_get_at(void *base, int32_t idx) {
  if (base == NULL || idx < 0) return 0;
  return (int32_t)((uint8_t *)base)[idx];
}

__attribute__((weak)) void cheng_rawbytes_set_at(void *base, int32_t idx, int32_t value) {
  if (base == NULL || idx < 0) return;
  ((uint8_t *)base)[idx] = (uint8_t)value;
}

__attribute__((weak)) char *driver_c_read_file_all(const char *path) {
  FILE *f = NULL;
  char *out = NULL;
  long sizeLong = 0;
  size_t readCount = 0;
  if (path == NULL || path[0] == '\0') {
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  f = fopen(path, "rb");
  if (f == NULL) {
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  sizeLong = ftell(f);
  if (sizeLong < 0) {
    fclose(f);
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    out = (char *)malloc(1);
    if (out != NULL) out[0] = 0;
    return out;
  }
  out = (char *)malloc((size_t)sizeLong + 1);
  if (out == NULL) {
    fclose(f);
    return NULL;
  }
  if (sizeLong > 0) {
    readCount = fread(out, 1, (size_t)sizeLong, f);
    if (readCount != (size_t)sizeLong && ferror(f)) {
      fclose(f);
      free(out);
      out = (char *)malloc(1);
      if (out != NULL) out[0] = 0;
      return out;
    }
  }
  out[readCount] = 0;
  fclose(f);
  return out;
}

__attribute__((weak)) ChengStrBridge driver_c_exec_file_capture_or_panic_bridge(ChengStrBridge file_path,
                                                                                ChengSeqHeader argv,
                                                                                ChengStrBridge working_dir,
                                                                                ChengStrBridge label) {
  char *file_path_c = driver_c_bridge_to_cstring(file_path);
  char *working_dir_c = driver_c_bridge_to_cstring(working_dir);
  char *label_c = driver_c_bridge_to_cstring(label);
  int64_t exit_code = -1;
  char *output = NULL;
  ChengStrBridge out = {0};
  output = driver_c_exec_file_capture_minimal(file_path_c != NULL ? file_path_c : "",
                                              argv,
                                              working_dir_c != NULL ? working_dir_c : "",
                                              300,
                                              &exit_code);
  free(file_path_c);
  free(working_dir_c);
  if (exit_code != 0) {
    driver_c_abort_with_label_output(label_c, output);
  }
  free(label_c);
  out.ptr = output;
  out.len = output != NULL ? (int32_t)strlen(output) : 0;
  out.store_id = 0;
  out.flags = CHENG_STR_BRIDGE_FLAG_OWNED;
  return out;
}

__attribute__((weak)) void driver_c_compare_text_files_or_panic_bridge(ChengStrBridge left_path,
                                                                       ChengStrBridge right_path,
                                                                       ChengStrBridge label) {
  char *left_path_c = driver_c_bridge_to_cstring(left_path);
  char *right_path_c = driver_c_bridge_to_cstring(right_path);
  char *label_c = driver_c_bridge_to_cstring(label);
  int32_t same = driver_c_compare_files_bytes_impl(left_path_c, right_path_c);
  free(left_path_c);
  free(right_path_c);
  if (!same) {
    driver_c_abort_with_label_output(label_c, "");
  }
  free(label_c);
}

__attribute__((weak)) ChengStrBridge driver_c_absolute_path_bridge(ChengStrBridge path) {
  char *path_c = driver_c_bridge_to_cstring(path);
  char *out = driver_c_absolute_path_dup(path_c);
  free(path_c);
  return driver_c_owned_bridge_from_cstring(out);
}

__attribute__((weak)) ChengStrBridge driver_c_program_absolute_path_bridge(void) {
  return driver_c_owned_bridge_from_cstring(driver_c_absolute_path_dup(driver_c_program_name_raw()));
}

__attribute__((weak)) ChengStrBridge driver_c_get_current_dir_bridge(void) {
  return driver_c_owned_bridge_from_cstring(getcwd(NULL, 0));
}

__attribute__((weak)) ChengStrBridge driver_c_join_path2_bridge(ChengStrBridge left, ChengStrBridge right) {
  char *left_c = driver_c_bridge_to_cstring(left);
  char *right_c = driver_c_bridge_to_cstring(right);
  char *out = driver_c_join_path2_dup(left_c, right_c);
  free(left_c);
  free(right_c);
  return driver_c_owned_bridge_from_cstring(out);
}

__attribute__((weak)) void driver_c_create_dir_all_bridge(ChengStrBridge path) {
  char *path_c = driver_c_bridge_to_cstring(path);
  driver_c_mkdir_all_if_needed(path_c);
  free(path_c);
}

__attribute__((weak)) void driver_c_write_text_file_bridge(ChengStrBridge path, ChengStrBridge content) {
  char *path_c = driver_c_bridge_to_cstring(path);
  FILE *f = NULL;
  if (path_c == NULL || path_c[0] == '\0') {
    free(path_c);
    return;
  }
  driver_c_mkdir_parents_if_needed(path_c);
  f = fopen(path_c, "wb");
  if (f != NULL) {
    if (content.ptr != NULL && content.len > 0) {
      fwrite(content.ptr, 1, (size_t)content.len, f);
    }
    fclose(f);
  }
  free(path_c);
}

__attribute__((weak)) int32_t driver_c_compare_text_files_bridge(ChengStrBridge left_path, ChengStrBridge right_path) {
  char *left_c = driver_c_bridge_to_cstring(left_path);
  char *right_c = driver_c_bridge_to_cstring(right_path);
  int32_t same = driver_c_compare_files_bytes_impl(left_c, right_c);
  free(left_c);
  free(right_c);
  return same;
}

__attribute__((weak)) int32_t driver_c_compare_binary_files_bridge(ChengStrBridge left_path, ChengStrBridge right_path) {
  char *left_c = driver_c_bridge_to_cstring(left_path);
  char *right_c = driver_c_bridge_to_cstring(right_path);
  int32_t same = driver_c_compare_files_bytes_impl(left_c, right_c);
  free(left_c);
  free(right_c);
  return same;
}

__attribute__((weak)) ChengStrBridge driver_c_extract_line_value_bridge(ChengStrBridge text, ChengStrBridge key) {
  char *text_c = driver_c_bridge_to_cstring(text);
  char *key_c = driver_c_bridge_to_cstring(key);
  char *value = driver_c_find_line_value_dup(text_c, key_c);
  free(text_c);
  free(key_c);
  return driver_c_owned_bridge_from_cstring(value);
}

__attribute__((weak)) int32_t driver_c_count_external_cc_providers_bridge(ChengStrBridge plan_text) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  const char *cursor = plan_c;
  const char *needle = "compile_mode=external_cc_obj";
  int32_t count = 0;
  if (cursor != NULL) {
    while ((cursor = strstr(cursor, needle)) != NULL) {
      count = count + 1;
      cursor = cursor + strlen(needle);
    }
  }
  free(plan_c);
  return count;
}

__attribute__((weak)) int32_t driver_c_parse_plan_int32_or_zero_bridge(ChengStrBridge plan_text,
                                                                       ChengStrBridge key) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  char *key_c = driver_c_bridge_to_cstring(key);
  char *value = driver_c_find_line_value_dup(plan_c, key_c);
  char *end_ptr = NULL;
  long parsed = 0;
  int32_t out = 0;
  if (value != NULL && value[0] != '\0') {
    errno = 0;
    parsed = strtol(value, &end_ptr, 10);
    if (errno == 0 && end_ptr != NULL && *end_ptr == '\0') {
      out = (int32_t)parsed;
    }
  }
  free(value);
  free(plan_c);
  free(key_c);
  return out;
}

__attribute__((weak)) ChengStrBridge driver_c_provider_field_for_module_from_plan_text_bridge(ChengStrBridge plan_text,
                                                                                               ChengStrBridge module_name,
                                                                                               ChengStrBridge field_name) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  char *module_name_c = driver_c_bridge_to_cstring(module_name);
  char *field_name_c = driver_c_bridge_to_cstring(field_name);
  char *value = driver_c_provider_field_for_module_dup(plan_c, module_name_c, field_name_c);
  free(plan_c);
  free(module_name_c);
  free(field_name_c);
  return driver_c_owned_bridge_from_cstring(value);
}

__attribute__((weak)) ChengStrBridge driver_c_compiler_core_provider_source_kind_bridge(ChengStrBridge plan_text) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  char *value = driver_c_provider_field_for_module_dup(plan_c,
                                                       "runtime/compiler_core_runtime_v2",
                                                       "source_kind");
  free(plan_c);
  return driver_c_owned_bridge_from_cstring(value);
}

__attribute__((weak)) ChengStrBridge driver_c_compiler_core_provider_compile_mode_bridge(ChengStrBridge plan_text) {
  char *plan_c = driver_c_bridge_to_cstring(plan_text);
  char *value = driver_c_provider_field_for_module_dup(plan_c,
                                                       "runtime/compiler_core_runtime_v2",
                                                       "compile_mode");
  free(plan_c);
  return driver_c_owned_bridge_from_cstring(value);
}

static int32_t driver_c_count_external_cc_providers_text(const char *plan_text) {
  const char *cursor = plan_text;
  const char *needle = "compile_mode=external_cc_obj";
  int32_t count = 0;
  if (cursor == NULL) return 0;
  while ((cursor = strstr(cursor, needle)) != NULL) {
    count = count + 1;
    cursor = cursor + strlen(needle);
  }
  return count;
}

static void driver_c_write_text_file_raw(const char *path, const char *content) {
  FILE *f = NULL;
  if (path == NULL || path[0] == '\0') {
    driver_c_die("driver_c missing output path");
  }
  driver_c_mkdir_parents_if_needed(path);
  f = fopen(path, "wb");
  if (f == NULL) {
    driver_c_die_errno("driver_c fopen failed", path);
  }
  if (content != NULL && content[0] != '\0') {
    fwrite(content, 1, strlen(content), f);
  }
  fclose(f);
}

static char *driver_c_run_command_capture_or_die(const char *file_path,
                                                 const char **argv_items,
                                                 int32_t argc,
                                                 const char *working_dir,
                                                 const char *label) {
  ChengSeqHeader argv = {0};
  int64_t exit_code = -1;
  char *output = NULL;
  argv.len = argc;
  argv.cap = argc;
  argv.buffer = (void *)argv_items;
  output = driver_c_exec_file_capture_minimal(file_path, argv, working_dir, 300, &exit_code);
  if (exit_code != 0) {
    driver_c_abort_with_label_output(label, output);
  }
  return output;
}

static void driver_c_compare_expected_text_or_die(const char *repo_root,
                                                  const char *expected_rel,
                                                  const char *actual_path,
                                                  const char *label) {
  char expected_abs[PATH_MAX];
  driver_c_resolve_existing_input_path(repo_root, expected_rel, expected_abs, sizeof(expected_abs));
  if (!driver_c_compare_files_bytes_impl(expected_abs, actual_path)) {
    driver_c_abort_with_label_output(label, "");
  }
}

static DriverCNativeStageArtifacts driver_c_build_stage_artifacts(const char *repo_root,
                                                                  const char *compiler_path,
                                                                  const char *source_path,
                                                                  const char *target_triple,
                                                                  const char *stage_dir,
                                                                  const char *binary_name) {
  DriverCNativeStageArtifacts out;
  char release_path[PATH_MAX];
  char system_link_plan_path[PATH_MAX];
  char system_link_exec_path[PATH_MAX];
  char binary_path[PATH_MAX];
  const char *release_argv[9];
  const char *plan_argv[9];
  const char *exec_argv[11];
  char *release_text = NULL;
  char *plan_text = NULL;
  char *exec_text = NULL;
  memset(&out, 0, sizeof(out));
  driver_c_path_join_buf(release_path, sizeof(release_path), stage_dir, "release.txt");
  driver_c_path_join_buf(system_link_plan_path, sizeof(system_link_plan_path), stage_dir, "system_link_plan.txt");
  driver_c_path_join_buf(system_link_exec_path, sizeof(system_link_exec_path), stage_dir, "system_link_exec.txt");
  driver_c_path_join_buf(binary_path, sizeof(binary_path), stage_dir, binary_name);
  driver_c_mkdir_all_if_needed(stage_dir);

  release_argv[0] = "release-compile";
  release_argv[1] = "--in";
  release_argv[2] = source_path;
  release_argv[3] = "--emit";
  release_argv[4] = "exe";
  release_argv[5] = "--target";
  release_argv[6] = target_triple;
  release_argv[7] = "--out";
  release_argv[8] = release_path;
  release_text = driver_c_run_command_capture_or_die(compiler_path,
                                                     release_argv,
                                                     9,
                                                     repo_root,
                                                     "stage-selfhost-host release-compile failed");
  driver_c_write_text_file_raw(release_path, release_text);
  free(release_text);

  plan_argv[0] = "system-link-plan";
  plan_argv[1] = "--in";
  plan_argv[2] = source_path;
  plan_argv[3] = "--emit";
  plan_argv[4] = "exe";
  plan_argv[5] = "--target";
  plan_argv[6] = target_triple;
  plan_argv[7] = "--out";
  plan_argv[8] = system_link_plan_path;
  plan_text = driver_c_run_command_capture_or_die(compiler_path,
                                                  plan_argv,
                                                  9,
                                                  repo_root,
                                                  "stage-selfhost-host system-link-plan failed");
  driver_c_write_text_file_raw(system_link_plan_path, plan_text);

  exec_argv[0] = "system-link-exec";
  exec_argv[1] = "--in";
  exec_argv[2] = source_path;
  exec_argv[3] = "--emit";
  exec_argv[4] = "exe";
  exec_argv[5] = "--target";
  exec_argv[6] = target_triple;
  exec_argv[7] = "--out";
  exec_argv[8] = binary_path;
  exec_argv[9] = "--report-out";
  exec_argv[10] = system_link_exec_path;
  exec_text = driver_c_run_command_capture_or_die(compiler_path,
                                                  exec_argv,
                                                  11,
                                                  repo_root,
                                                  "stage-selfhost-host system-link-exec failed");
  driver_c_write_text_file_raw(system_link_exec_path, exec_text);

  out.compiler_path = driver_c_dup_cstring(compiler_path);
  out.release_path = driver_c_dup_cstring(release_path);
  out.system_link_plan_path = driver_c_dup_cstring(system_link_plan_path);
  out.system_link_exec_path = driver_c_dup_cstring(system_link_exec_path);
  out.binary_path = driver_c_dup_cstring(binary_path);
  out.binary_cid = driver_c_sha256_hex_file_dup(binary_path);
  out.output_file_cid = driver_c_find_line_value_dup(exec_text, "output_file_cid");
  out.external_cc_provider_count = driver_c_count_external_cc_providers_text(plan_text);
  out.compiler_core_provider_source_kind =
      driver_c_provider_field_for_module_dup(plan_text,
                                             "runtime/compiler_core_runtime_v2",
                                             "source_kind");
  out.compiler_core_provider_compile_mode =
      driver_c_provider_field_for_module_dup(plan_text,
                                             "runtime/compiler_core_runtime_v2",
                                             "compile_mode");
  free(plan_text);
  free(exec_text);
  return out;
}

static void driver_c_free_stage_artifacts(DriverCNativeStageArtifacts *bundle) {
  if (bundle == NULL) return;
  free(bundle->compiler_path);
  free(bundle->release_path);
  free(bundle->system_link_plan_path);
  free(bundle->system_link_exec_path);
  free(bundle->binary_path);
  free(bundle->binary_cid);
  free(bundle->output_file_cid);
  free(bundle->compiler_core_provider_source_kind);
  free(bundle->compiler_core_provider_compile_mode);
  memset(bundle, 0, sizeof(*bundle));
}

__attribute__((weak)) int32_t driver_c_run_tooling_selfhost_host_bridge(void) {
  char repo_root[PATH_MAX];
  char compiler_abs[PATH_MAX];
  char out_dir[PATH_MAX];
  char source_manifest_stdout[PATH_MAX];
  char source_manifest_file[PATH_MAX];
  char rule_pack_stdout[PATH_MAX];
  char rule_pack_file[PATH_MAX];
  char compiler_rule_pack_stdout[PATH_MAX];
  char compiler_rule_pack_file[PATH_MAX];
  char release_stdout[PATH_MAX];
  char release_file[PATH_MAX];
  char network_selfhost_file[PATH_MAX];
  char tooling_selfhost_check_file[PATH_MAX];
  char *compiler_raw = NULL;
  char *out_raw = NULL;
  char *stdout_text = NULL;
  const char *root_raw = "v2/examples";
  const char *source_raw = "v2/examples/network_distribution_module.cheng";
  const char *target_raw = "arm64-apple-darwin";
  const char *source_manifest_argv[5];
  const char *rule_pack_argv[5];
  const char *compiler_rule_pack_argv[5];
  const char *release_argv[9];
  const char *network_selfhost_argv[5];
  const char *tooling_selfhost_check_argv[5];
  driver_c_detect_repo_root(driver_c_program_name_raw(), cheng_getcwd(), repo_root, sizeof(repo_root));
  compiler_raw = driver_c_read_flag_dup_raw("--compiler", driver_c_program_name_raw());
  out_raw = driver_c_read_flag_dup_raw("--out-dir", "v2/artifacts/full_selfhost/tooling_selfhost");
  driver_c_resolve_existing_input_path(repo_root, compiler_raw, compiler_abs, sizeof(compiler_abs));
  driver_c_resolve_output_path(repo_root, out_raw, out_dir, sizeof(out_dir));
  driver_c_mkdir_all_if_needed(out_dir);
  driver_c_path_join_buf(source_manifest_stdout, sizeof(source_manifest_stdout), out_dir, "source_manifest.stdout");
  driver_c_path_join_buf(source_manifest_file, sizeof(source_manifest_file), out_dir, "source_manifest.txt");
  driver_c_path_join_buf(rule_pack_stdout, sizeof(rule_pack_stdout), out_dir, "rule_pack.stdout");
  driver_c_path_join_buf(rule_pack_file, sizeof(rule_pack_file), out_dir, "rule_pack.txt");
  driver_c_path_join_buf(compiler_rule_pack_stdout, sizeof(compiler_rule_pack_stdout), out_dir, "compiler_rule_pack.stdout");
  driver_c_path_join_buf(compiler_rule_pack_file, sizeof(compiler_rule_pack_file), out_dir, "compiler_rule_pack.txt");
  driver_c_path_join_buf(release_stdout, sizeof(release_stdout), out_dir, "release_artifact.stdout");
  driver_c_path_join_buf(release_file, sizeof(release_file), out_dir, "release_artifact.txt");
  driver_c_path_join_buf(network_selfhost_file, sizeof(network_selfhost_file), out_dir, "network_selfhost.txt");
  driver_c_path_join_buf(tooling_selfhost_check_file,
                         sizeof(tooling_selfhost_check_file),
                         out_dir,
                         "tooling_selfhost_check.txt");

  source_manifest_argv[0] = "publish-source-manifest";
  source_manifest_argv[1] = "--root";
  source_manifest_argv[2] = root_raw;
  source_manifest_argv[3] = "--out";
  source_manifest_argv[4] = source_manifest_file;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    source_manifest_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host publish-source-manifest failed");
  driver_c_write_text_file_raw(source_manifest_stdout, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_source_manifest.expected",
                                        source_manifest_stdout,
                                        "tooling-selfhost-host source manifest stdout mismatch");
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_source_manifest.expected",
                                        source_manifest_file,
                                        "tooling-selfhost-host source manifest file mismatch");

  rule_pack_argv[0] = "publish-rule-pack";
  rule_pack_argv[1] = "--in";
  rule_pack_argv[2] = source_raw;
  rule_pack_argv[3] = "--out";
  rule_pack_argv[4] = rule_pack_file;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    rule_pack_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host publish-rule-pack failed");
  driver_c_write_text_file_raw(rule_pack_stdout, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_rule_pack.expected",
                                        rule_pack_stdout,
                                        "tooling-selfhost-host rule pack stdout mismatch");
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_rule_pack.expected",
                                        rule_pack_file,
                                        "tooling-selfhost-host rule pack file mismatch");

  compiler_rule_pack_argv[0] = "publish-compiler-rule-pack";
  compiler_rule_pack_argv[1] = "--in";
  compiler_rule_pack_argv[2] = source_raw;
  compiler_rule_pack_argv[3] = "--out";
  compiler_rule_pack_argv[4] = compiler_rule_pack_file;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    compiler_rule_pack_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host publish-compiler-rule-pack failed");
  driver_c_write_text_file_raw(compiler_rule_pack_stdout, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_compiler_rule_pack.expected",
                                        compiler_rule_pack_stdout,
                                        "tooling-selfhost-host compiler rule pack stdout mismatch");
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_compiler_rule_pack.expected",
                                        compiler_rule_pack_file,
                                        "tooling-selfhost-host compiler rule pack file mismatch");

  release_argv[0] = "release-compile";
  release_argv[1] = "--in";
  release_argv[2] = source_raw;
  release_argv[3] = "--emit";
  release_argv[4] = "exe";
  release_argv[5] = "--target";
  release_argv[6] = target_raw;
  release_argv[7] = "--out";
  release_argv[8] = release_file;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    release_argv,
                                                    9,
                                                    repo_root,
                                                    "tooling-selfhost-host release-compile failed");
  driver_c_write_text_file_raw(release_stdout, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_release_artifact.expected",
                                        release_stdout,
                                        "tooling-selfhost-host release stdout mismatch");
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_release_artifact.expected",
                                        release_file,
                                        "tooling-selfhost-host release file mismatch");

  network_selfhost_argv[0] = "verify-network-selfhost";
  network_selfhost_argv[1] = "--root";
  network_selfhost_argv[2] = root_raw;
  network_selfhost_argv[3] = "--in";
  network_selfhost_argv[4] = source_raw;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    network_selfhost_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host verify-network-selfhost failed");
  driver_c_write_text_file_raw(network_selfhost_file, stdout_text);
  free(stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/network_selfhost.expected",
                                        network_selfhost_file,
                                        "tooling-selfhost-host network selfhost mismatch");

  tooling_selfhost_check_argv[0] = "tooling-selfhost-check";
  tooling_selfhost_check_argv[1] = "--root";
  tooling_selfhost_check_argv[2] = root_raw;
  tooling_selfhost_check_argv[3] = "--in";
  tooling_selfhost_check_argv[4] = source_raw;
  stdout_text = driver_c_run_command_capture_or_die(compiler_abs,
                                                    tooling_selfhost_check_argv,
                                                    5,
                                                    repo_root,
                                                    "tooling-selfhost-host tooling-selfhost-check failed");
  driver_c_write_text_file_raw(tooling_selfhost_check_file, stdout_text);
  driver_c_compare_expected_text_or_die(repo_root,
                                        "v2/tests/contracts/tooling_selfhost.expected",
                                        tooling_selfhost_check_file,
                                        "tooling-selfhost-host tooling selfhost mismatch");

  printf("tooling_selfhost_host_ok=1\n");
  printf("compiler=%s\n", compiler_abs);
  fputs(stdout_text, stdout);
  free(stdout_text);
  free(compiler_raw);
  free(out_raw);
  return 0;
}

__attribute__((weak)) int32_t driver_c_run_tooling_selfhost_check_bridge(void) {
  int argc = 0;
  char **argv = driver_c_cli_current_argv_dup(&argc);
  int32_t rc = 0;
  if (argv == NULL) {
    driver_c_die("tooling-selfhost-check bridge: argv alloc failed");
  }
  if (cheng_v2c_tooling_handle == NULL) {
    free(argv);
    driver_c_die("tooling-selfhost-check bridge: bootstrap tooling handle unavailable");
  }
  rc = cheng_v2c_tooling_handle(argc, argv);
  free(argv);
  return rc;
}

__attribute__((weak)) int32_t driver_c_run_program_selfhost_check_bridge(void) {
  int argc = 0;
  char **argv = driver_c_cli_current_argv_dup(&argc);
  int32_t rc = 0;
  if (argv == NULL) {
    driver_c_die("program-selfhost-check bridge: argv alloc failed");
  }
  if (cheng_v2c_tooling_handle == NULL) {
    free(argv);
    driver_c_die("program-selfhost-check bridge: bootstrap tooling handle unavailable");
  }
  rc = cheng_v2c_tooling_handle(argc, argv);
  free(argv);
  return rc;
}

__attribute__((weak)) int32_t driver_c_run_stage_selfhost_host_bridge(void) {
  char repo_root[PATH_MAX];
  char compiler_abs[PATH_MAX];
  char out_dir[PATH_MAX];
  char stages_dir[PATH_MAX];
  char stage1_dir[PATH_MAX];
  char stage2_dir[PATH_MAX];
  char stage3_dir[PATH_MAX];
  char stage2_tooling_dir[PATH_MAX];
  char stage2_tooling_report[PATH_MAX];
  char *compiler_raw = NULL;
  char *out_raw = NULL;
  char *target_raw = NULL;
  const char *source_raw = "v2/src/tooling/cheng_v2.cheng";
  DriverCNativeStageArtifacts stage1;
  DriverCNativeStageArtifacts stage2;
  DriverCNativeStageArtifacts stage3;
  const char *tooling_selfhost_argv[5];
  char *tooling_selfhost_text = NULL;
  char *tooling_selfhost_ok = NULL;
  int release_equal = 0;
  int plan_equal = 0;
  int exec_equal = 0;
  int binary_equal = 0;
  int output_cid_equal = 0;
  int stage2_tooling_ok = 0;
  int no_external_c_provider = 0;
  int ok = 0;
  memset(&stage1, 0, sizeof(stage1));
  memset(&stage2, 0, sizeof(stage2));
  memset(&stage3, 0, sizeof(stage3));
  driver_c_detect_repo_root(driver_c_program_name_raw(), cheng_getcwd(), repo_root, sizeof(repo_root));
  compiler_raw = driver_c_read_flag_dup_raw("--compiler", driver_c_program_name_raw());
  out_raw = driver_c_read_flag_dup_raw("--out-dir", "v2/artifacts/full_selfhost");
  target_raw = driver_c_read_flag_dup_raw("--target", "arm64-apple-darwin");
  driver_c_resolve_existing_input_path(repo_root, compiler_raw, compiler_abs, sizeof(compiler_abs));
  driver_c_resolve_output_path(repo_root, out_raw, out_dir, sizeof(out_dir));
  driver_c_path_join_buf(stages_dir, sizeof(stages_dir), out_dir, "stages");
  driver_c_path_join_buf(stage1_dir, sizeof(stage1_dir), stages_dir, "cheng_v2.stage1");
  driver_c_path_join_buf(stage2_dir, sizeof(stage2_dir), stages_dir, "cheng_v2.stage2");
  driver_c_path_join_buf(stage3_dir, sizeof(stage3_dir), stages_dir, "cheng_v2.stage3");
  driver_c_path_join_buf(stage2_tooling_dir, sizeof(stage2_tooling_dir), out_dir, "stage2_tooling_selfhost");
  driver_c_path_join_buf(stage2_tooling_report, sizeof(stage2_tooling_report), stage2_tooling_dir, "tooling_selfhost_host.txt");
  driver_c_mkdir_all_if_needed(stage1_dir);
  driver_c_mkdir_all_if_needed(stage2_dir);
  driver_c_mkdir_all_if_needed(stage3_dir);

  stage1 = driver_c_build_stage_artifacts(repo_root, compiler_abs, source_raw, target_raw, stage1_dir, "cheng_v2");
  stage2 = driver_c_build_stage_artifacts(repo_root, stage1.binary_path, source_raw, target_raw, stage2_dir, "cheng_v2");
  stage3 = driver_c_build_stage_artifacts(repo_root, stage2.binary_path, source_raw, target_raw, stage3_dir, "cheng_v2");

  release_equal = driver_c_compare_files_bytes_impl(stage2.release_path, stage3.release_path);
  plan_equal = driver_c_compare_files_bytes_impl(stage2.system_link_plan_path, stage3.system_link_plan_path);
  exec_equal = driver_c_compare_files_bytes_impl(stage2.system_link_exec_path, stage3.system_link_exec_path);
  binary_equal = driver_c_compare_files_bytes_impl(stage2.binary_path, stage3.binary_path);
  output_cid_equal = strcmp(stage2.output_file_cid != NULL ? stage2.output_file_cid : "",
                            stage3.output_file_cid != NULL ? stage3.output_file_cid : "") == 0;
  no_external_c_provider =
      stage1.external_cc_provider_count == 0 &&
      stage2.external_cc_provider_count == 0 &&
      stage3.external_cc_provider_count == 0 &&
      strcmp(stage2.compiler_core_provider_source_kind != NULL ? stage2.compiler_core_provider_source_kind : "",
             "cheng_module") == 0 &&
      strcmp(stage2.compiler_core_provider_compile_mode != NULL ? stage2.compiler_core_provider_compile_mode : "",
             "machine_obj") == 0;

  tooling_selfhost_argv[0] = "tooling-selfhost-host";
  tooling_selfhost_argv[1] = "--compiler";
  tooling_selfhost_argv[2] = stage2.binary_path;
  tooling_selfhost_argv[3] = "--out-dir";
  tooling_selfhost_argv[4] = stage2_tooling_dir;
  tooling_selfhost_text = driver_c_run_command_capture_or_die(stage2.binary_path,
                                                              tooling_selfhost_argv,
                                                              5,
                                                              repo_root,
                                                              "stage-selfhost-host stage2 tooling-selfhost-host failed");
  driver_c_write_text_file_raw(stage2_tooling_report, tooling_selfhost_text);
  tooling_selfhost_ok = driver_c_find_line_value_dup(tooling_selfhost_text, "tooling_selfhost_host_ok");
  stage2_tooling_ok = tooling_selfhost_ok != NULL && strcmp(tooling_selfhost_ok, "1") == 0;
  free(tooling_selfhost_ok);
  free(tooling_selfhost_text);

  ok = release_equal &&
       plan_equal &&
       exec_equal &&
       binary_equal &&
       output_cid_equal &&
       stage2_tooling_ok &&
       no_external_c_provider;
  printf("full_selfhost_ok=%d\n", ok ? 1 : 0);
  printf("compiler=%s\n", compiler_abs);
  printf("stage1_binary_cid=%s\n", stage1.binary_cid != NULL ? stage1.binary_cid : "");
  printf("stage2_binary_cid=%s\n", stage2.binary_cid != NULL ? stage2.binary_cid : "");
  printf("stage3_binary_cid=%s\n", stage3.binary_cid != NULL ? stage3.binary_cid : "");
  printf("stage2_stage3_release_equal=%d\n", release_equal);
  printf("stage2_stage3_system_link_plan_equal=%d\n", plan_equal);
  printf("stage2_stage3_system_link_exec_equal=%d\n", exec_equal);
  printf("stage2_stage3_binary_equal=%d\n", binary_equal);
  printf("stage2_stage3_output_file_cid_equal=%d\n", output_cid_equal);
  printf("stage2_tooling_selfhost_ok=%d\n", stage2_tooling_ok);
  printf("stage1_external_cc_provider_count=%d\n", stage1.external_cc_provider_count);
  printf("stage2_external_cc_provider_count=%d\n", stage2.external_cc_provider_count);
  printf("stage3_external_cc_provider_count=%d\n", stage3.external_cc_provider_count);
  printf("compiler_core_provider_source_kind=%s\n",
         stage2.compiler_core_provider_source_kind != NULL ? stage2.compiler_core_provider_source_kind : "");
  printf("compiler_core_provider_compile_mode=%s\n",
         stage2.compiler_core_provider_compile_mode != NULL ? stage2.compiler_core_provider_compile_mode : "");
  printf("compiler_core_dispatch_provider_removed=%d\n", no_external_c_provider ? 1 : 0);
  printf("emit_c_used_after_stage0=0\n");

  driver_c_free_stage_artifacts(&stage1);
  driver_c_free_stage_artifacts(&stage2);
  driver_c_free_stage_artifacts(&stage3);
  free(compiler_raw);
  free(out_raw);
  free(target_raw);
  return ok ? 0 : 1;
}

static char *driver_c_prog_slice_dup(const char *text, int32_t start, int32_t count) {
  int32_t len = 0;
  if (text == NULL) {
    return driver_c_dup_cstring("");
  }
  len = (int32_t)strlen(text);
  if (start < 0) start = 0;
  if (start > len) start = len;
  if (count < 0) count = 0;
  if (start + count > len) count = len - start;
  return driver_c_dup_range(text + start, text + start + count);
}

static int32_t driver_c_prog_find_substr(const char *text, const char *sub) {
  const char *found = NULL;
  if (text == NULL || sub == NULL) {
    return -1;
  }
  if (sub[0] == '\0') {
    return 0;
  }
  found = strstr(text, sub);
  if (found == NULL) {
    return -1;
  }
  return (int32_t)(found - text);
}

static void driver_c_prog_assign_slot(DriverCProgValue *slot, DriverCProgValue value) {
  DriverCProgValue current = driver_c_prog_value_nil();
  DriverCProgZeroPlan *current_zero_plan = NULL;
  DriverCProgValue next = driver_c_prog_materialize(value);
  int same_aggregate_object = 0;
  if (slot == NULL) {
    driver_c_die("driver_c program runtime: null assign slot");
  }
  if (slot->kind == DRIVER_C_PROG_VALUE_REF && slot->as.ref.slot != NULL) {
    driver_c_prog_assign_slot(slot->as.ref.slot, next);
    return;
  }
  if (slot->kind == DRIVER_C_PROG_VALUE_ZERO_PLAN) {
    current_zero_plan = slot->as.zero_plan;
    next = driver_c_prog_refine_value_to_zero_plan(current_zero_plan, next);
  } else {
    current = driver_c_prog_materialize(*slot);
    next = driver_c_prog_refine_value_to_slot_kind(current, next);
  }
  if (current_zero_plan == NULL &&
      current.kind == DRIVER_C_PROG_VALUE_ARRAY &&
      current.as.array != NULL &&
      current.as.array->elem_type_text != NULL &&
      current.as.array->elem_type_text[0] != '\0' &&
      next.kind == DRIVER_C_PROG_VALUE_ARRAY &&
      next.as.array != NULL &&
      (next.as.array->elem_type_text == NULL || next.as.array->elem_type_text[0] == '\0')) {
    driver_c_prog_array_set_elem_type(next.as.array, current.as.array->elem_type_text);
  }
  if (current_zero_plan == NULL &&
      current.kind == DRIVER_C_PROG_VALUE_RECORD &&
      current.as.record != NULL &&
      current.as.record->type_text != NULL &&
      current.as.record->type_text[0] != '\0' &&
      next.kind == DRIVER_C_PROG_VALUE_RECORD &&
      next.as.record != NULL &&
      ((next.as.record->type_text == NULL || next.as.record->type_text[0] == '\0') ||
       driver_c_prog_type_text_is_more_specific_than(current.as.record->type_text,
                                                     next.as.record->type_text))) {
    driver_c_prog_record_set_type(next.as.record, current.as.record->type_text);
  }
  if (current_zero_plan == NULL &&
      current.kind == next.kind &&
      ((current.kind == DRIVER_C_PROG_VALUE_ARRAY &&
        current.as.array != NULL &&
        current.as.array == next.as.array) ||
       (current.kind == DRIVER_C_PROG_VALUE_RECORD &&
        current.as.record != NULL &&
        current.as.record == next.as.record))) {
    same_aggregate_object = 1;
  }
  if (next.kind == DRIVER_C_PROG_VALUE_ARRAY || next.kind == DRIVER_C_PROG_VALUE_RECORD) {
    if (!same_aggregate_object && !driver_c_prog_value_is_ephemeral_aggregate(next)) {
      next = driver_c_prog_clone_value_deep(next);
    }
    driver_c_prog_value_clear_ephemeral_flag_root(&next);
  } else {
    next.flags = 0;
  }
  *slot = next;
}

static DriverCProgValue driver_c_prog_zero_record_shell_from_plan(const DriverCProgZeroPlan *plan) {
  DriverCProgRecord *record = NULL;
  int32_t i = 0;
  if (plan == NULL || plan->kind != DRIVER_C_PROG_ZERO_PLAN_RECORD) {
    return driver_c_prog_value_record(driver_c_prog_record_new());
  }
  record = driver_c_prog_record_new_with_type(plan->type_text);
  driver_c_prog_record_ensure_value_cap(record, plan->field_count);
  driver_c_prog_record_set_shared_names(record, plan->field_names);
  for (i = 0; i < plan->field_count; ++i) {
    record->values[i] = driver_c_prog_value_zero_plan(plan->field_plans[i]);
  }
  record->len = plan->field_count;
  if (plan->field_lookup_indices != NULL && plan->field_lookup_cap > 0) {
    driver_c_prog_record_set_shared_lookup(record, plan->field_lookup_indices, plan->field_lookup_cap);
  } else {
    driver_c_prog_record_lookup_rebuild(record);
  }
  return driver_c_prog_value_record(record);
}

static DriverCProgValue driver_c_prog_zero_array_shell_from_plan(const DriverCProgZeroPlan *plan) {
  DriverCProgArray *array = NULL;
  int32_t i = 0;
  if (plan == NULL) {
    return driver_c_prog_value_array(driver_c_prog_array_new());
  }
  array = driver_c_prog_array_new_with_elem(plan->elem_type_text);
  if (plan->kind == DRIVER_C_PROG_ZERO_PLAN_DYNAMIC_ARRAY) {
    return driver_c_prog_value_array(array);
  }
  if (plan->kind != DRIVER_C_PROG_ZERO_PLAN_FIXED_ARRAY) {
    return driver_c_prog_value_array(array);
  }
  driver_c_prog_array_ensure_cap(array, plan->fixed_len);
  array->len = plan->fixed_len;
  for (i = 0; i < plan->fixed_len; ++i) {
    array->items[i] = driver_c_prog_value_zero_plan(plan->field_plans != NULL ? plan->field_plans[0] : NULL);
  }
  return driver_c_prog_value_array(array);
}

static DriverCProgValue *driver_c_prog_ref_record_field(DriverCProgValue base_value,
                                                        const char *field_name,
                                                        int32_t field_index) {
  DriverCProgValue *base_slot = driver_c_prog_slot_from_refish(base_value);
  DriverCProgValue materialized = driver_c_prog_value_nil();
  DriverCProgRecord *record = NULL;
  static DriverCProgValue scratch = {0};
  if (base_slot != NULL &&
      base_slot->kind == DRIVER_C_PROG_VALUE_ZERO_PLAN &&
      base_slot->as.zero_plan != NULL &&
      base_slot->as.zero_plan->kind == DRIVER_C_PROG_ZERO_PLAN_RECORD) {
    *base_slot = driver_c_prog_zero_record_shell_from_plan(base_slot->as.zero_plan);
    materialized = *base_slot;
  } else {
    materialized = driver_c_prog_materialize(base_value);
  }
  if (materialized.kind == DRIVER_C_PROG_VALUE_STR) {
    if (driver_c_prog_text_eq(field_name, "data")) {
      scratch = driver_c_prog_value_ptr((void *)materialized.as.str.ptr);
      return &scratch;
    }
    if (driver_c_prog_text_eq(field_name, "len")) {
      scratch = driver_c_prog_value_i32(driver_c_prog_str_len(materialized.as.str));
      return &scratch;
    }
    if (driver_c_prog_text_eq(field_name, "store_id")) {
      scratch = driver_c_prog_value_i32(materialized.as.str.store_id);
      return &scratch;
    }
    if (driver_c_prog_text_eq(field_name, "flags")) {
      scratch = driver_c_prog_value_i32(materialized.as.str.flags);
      return &scratch;
    }
  }
  if (materialized.kind != DRIVER_C_PROG_VALUE_RECORD || materialized.as.record == NULL) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: field access on non-record field=%s base_kind=%s",
             field_name != NULL ? field_name : "",
             driver_c_prog_value_kind_name(materialized.kind));
    driver_c_die(message);
  }
  record = materialized.as.record;
  if (field_index >= 0) {
    return driver_c_prog_record_slot_at(record, field_index, field_name);
  }
  return driver_c_prog_record_slot(record, field_name, 1);
}

static DriverCProgValue *driver_c_prog_record_slot_at(DriverCProgRecord *record,
                                                      int32_t field_index,
                                                      const char *field_name) {
  if (record == NULL) {
    driver_c_die("driver_c program runtime: record slot by index on nil record");
  }
  if (field_index < 0 || field_index >= record->len) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: record field ordinal out of range ordinal=%d len=%d field=%s",
             field_index,
             record->len,
             field_name != NULL ? field_name : "");
    driver_c_die(message);
  }
  if (field_name != NULL &&
      record->names[field_index] != NULL &&
      !driver_c_prog_text_eq(record->names[field_index], field_name)) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: record field ordinal/name mismatch ordinal=%d expected=%s actual=%s",
             field_index,
             field_name != NULL ? field_name : "",
             record->names[field_index] != NULL ? record->names[field_index] : "");
    driver_c_die(message);
  }
  return &record->values[field_index];
}

static DriverCProgValue *driver_c_prog_ref_record_field_for_store(DriverCProgValue base_value,
                                                                  const char *field_name,
                                                                  int32_t field_index,
                                                                  const char *owner_label,
                                                                  const char *op_kind) {
  DriverCProgValue *base_slot = driver_c_prog_slot_from_refish(base_value);
  DriverCProgValue materialized = driver_c_prog_value_nil();
  DriverCProgRecord *record = NULL;
  if (base_slot != NULL &&
      base_slot->kind == DRIVER_C_PROG_VALUE_ZERO_PLAN &&
      base_slot->as.zero_plan != NULL &&
      base_slot->as.zero_plan->kind == DRIVER_C_PROG_ZERO_PLAN_RECORD) {
    *base_slot = driver_c_prog_zero_record_shell_from_plan(base_slot->as.zero_plan);
    materialized = *base_slot;
  } else {
    materialized =
        base_slot != NULL ? driver_c_prog_materialize_slot(base_slot) : driver_c_prog_materialize(base_value);
  }
  if (materialized.kind == DRIVER_C_PROG_VALUE_NIL) {
    if (base_slot == NULL) {
      char message[512];
      snprintf(message,
               sizeof(message),
               "driver_c program runtime: field store on nil non-ref field=%s",
               field_name != NULL ? field_name : "");
      driver_c_die(message);
    }
    driver_c_prog_assign_slot(base_slot, driver_c_prog_value_record_temp(driver_c_prog_record_new()));
    materialized = driver_c_prog_materialize_slot(base_slot);
  }
  if (materialized.kind != DRIVER_C_PROG_VALUE_RECORD || materialized.as.record == NULL) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: field store on non-record owner=%s op=%s field=%s base_kind=%s",
             owner_label != NULL ? owner_label : "",
             op_kind != NULL ? op_kind : "",
             field_name != NULL ? field_name : "",
             driver_c_prog_value_kind_name(materialized.kind));
    driver_c_die(message);
  }
  record = materialized.as.record;
  if (field_index >= 0) {
    return driver_c_prog_record_slot_at(record, field_index, field_name);
  }
  return driver_c_prog_record_slot(record, field_name, 1);
}

static int driver_c_prog_try_store_bytes_field(DriverCProgValue base_value,
                                               const char *field_name,
                                               DriverCProgValue value) {
  DriverCProgValue *base_slot = driver_c_prog_slot_from_refish(base_value);
  DriverCProgValue materialized =
      base_slot != NULL ? driver_c_prog_materialize_slot(base_slot) : driver_c_prog_materialize(base_value);
  DriverCProgValue next = driver_c_prog_value_bytes_empty();
  if (field_name == NULL) return 0;
  if (base_slot != NULL &&
      materialized.kind == DRIVER_C_PROG_VALUE_NIL &&
      (driver_c_prog_text_eq(field_name, "data") || driver_c_prog_text_eq(field_name, "len"))) {
    driver_c_prog_assign_slot(base_slot, driver_c_prog_value_bytes_empty());
    materialized = driver_c_prog_materialize_slot(base_slot);
  }
  if (materialized.kind != DRIVER_C_PROG_VALUE_BYTES) {
    return 0;
  }
  if (base_slot == NULL) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: bytes field store on non-ref field=%s",
             field_name);
    driver_c_die(message);
  }
  next = materialized;
  if (driver_c_prog_text_eq(field_name, "data")) {
    next.as.bytes.data = (uint8_t *)driver_c_prog_value_to_void_ptr(value);
  } else if (driver_c_prog_text_eq(field_name, "len")) {
    int32_t len = driver_c_prog_value_to_i32(value);
    next.as.bytes.len = len > 0 ? len : 0;
  } else {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: unsupported bytes field store field=%s",
             field_name);
    driver_c_die(message);
  }
  driver_c_prog_assign_slot(base_slot, next);
  return 1;
}

static int driver_c_prog_try_store_str_field(DriverCProgValue base_value,
                                             const char *field_name,
                                             DriverCProgValue value) {
  DriverCProgValue *base_slot = driver_c_prog_slot_from_refish(base_value);
  DriverCProgValue materialized =
      base_slot != NULL ? driver_c_prog_materialize_slot(base_slot) : driver_c_prog_materialize(base_value);
  DriverCProgValue next = driver_c_prog_value_str_empty();
  if (field_name == NULL) return 0;
  if (base_slot != NULL &&
      materialized.kind == DRIVER_C_PROG_VALUE_NIL &&
      (driver_c_prog_text_eq(field_name, "data") ||
       driver_c_prog_text_eq(field_name, "len") ||
       driver_c_prog_text_eq(field_name, "store_id") ||
       driver_c_prog_text_eq(field_name, "flags"))) {
    driver_c_prog_assign_slot(base_slot, driver_c_prog_value_str_empty());
    materialized = driver_c_prog_materialize_slot(base_slot);
  }
  if (materialized.kind != DRIVER_C_PROG_VALUE_STR) {
    return 0;
  }
  if (base_slot == NULL) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: str field store on non-ref field=%s",
             field_name);
    driver_c_die(message);
  }
  next = materialized;
  if (driver_c_prog_text_eq(field_name, "data")) {
    next.as.str.ptr = (const char *)driver_c_prog_value_to_void_ptr(value);
  } else if (driver_c_prog_text_eq(field_name, "len")) {
    int32_t len = driver_c_prog_value_to_i32(value);
    next.as.str.len = len > 0 ? len : 0;
  } else if (driver_c_prog_text_eq(field_name, "store_id")) {
    next.as.str.store_id = driver_c_prog_value_to_i32(value);
  } else if (driver_c_prog_text_eq(field_name, "flags")) {
    next.as.str.flags = driver_c_prog_value_to_i32(value);
  } else {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: unsupported str field store field=%s",
             field_name);
    driver_c_die(message);
  }
  driver_c_prog_assign_slot(base_slot, next);
  return 1;
}

static int driver_c_prog_try_store_array_field(DriverCProgValue base_value,
                                               const char *field_name,
                                               DriverCProgValue value) {
  DriverCProgValue *base_slot = driver_c_prog_slot_from_refish(base_value);
  DriverCProgValue materialized = driver_c_prog_value_nil();
  DriverCProgArray *array = NULL;
  if (field_name == NULL) return 0;
  if (base_slot != NULL &&
      base_slot->kind == DRIVER_C_PROG_VALUE_ZERO_PLAN &&
      base_slot->as.zero_plan != NULL &&
      (base_slot->as.zero_plan->kind == DRIVER_C_PROG_ZERO_PLAN_DYNAMIC_ARRAY ||
       base_slot->as.zero_plan->kind == DRIVER_C_PROG_ZERO_PLAN_FIXED_ARRAY)) {
    *base_slot = driver_c_prog_zero_array_shell_from_plan(base_slot->as.zero_plan);
    materialized = *base_slot;
  } else {
    materialized =
        base_slot != NULL ? driver_c_prog_materialize_slot(base_slot) : driver_c_prog_materialize(base_value);
  }
  if (base_slot != NULL &&
      materialized.kind == DRIVER_C_PROG_VALUE_NIL &&
      (driver_c_prog_text_eq(field_name, "len") ||
       driver_c_prog_text_eq(field_name, "cap") ||
       driver_c_prog_text_eq(field_name, "buffer"))) {
    driver_c_prog_assign_slot(base_slot, driver_c_prog_value_array_temp(driver_c_prog_array_new()));
    materialized = driver_c_prog_materialize_slot(base_slot);
  }
  if (materialized.kind != DRIVER_C_PROG_VALUE_ARRAY || materialized.as.array == NULL) {
    return 0;
  }
  if (base_slot == NULL) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: array field store on non-ref field=%s",
             field_name);
    driver_c_die(message);
  }
  array = materialized.as.array;
  if (driver_c_prog_text_eq(field_name, "len")) {
    driver_c_prog_array_set_len(array, driver_c_prog_value_to_i32(value));
  } else if (driver_c_prog_text_eq(field_name, "cap")) {
    int32_t need = driver_c_prog_value_to_i32(value);
    if (need < 0) need = 0;
    driver_c_prog_array_ensure_cap(array, need);
  } else if (driver_c_prog_text_eq(field_name, "buffer")) {
    void *buffer = driver_c_prog_value_to_void_ptr(value);
    if (buffer == NULL) {
      array->items = NULL;
      array->len = 0;
      array->cap = 0;
    } else if (buffer != (void *)array->items) {
      driver_c_die("driver_c program runtime: array buffer field does not accept foreign pointer");
    }
  } else {
    return 0;
  }
  return 1;
}

static DriverCProgValue *driver_c_prog_ref_array_index(DriverCProgValue base_value,
                                                       int32_t index,
                                                       const char *context_label) {
  DriverCProgValue *base_slot = driver_c_prog_slot_from_refish(base_value);
  DriverCProgValue materialized = driver_c_prog_value_nil();
  DriverCProgArray *array = NULL;
  if (base_slot != NULL &&
      base_slot->kind == DRIVER_C_PROG_VALUE_ZERO_PLAN &&
      base_slot->as.zero_plan != NULL &&
      (base_slot->as.zero_plan->kind == DRIVER_C_PROG_ZERO_PLAN_DYNAMIC_ARRAY ||
       base_slot->as.zero_plan->kind == DRIVER_C_PROG_ZERO_PLAN_FIXED_ARRAY)) {
    *base_slot = driver_c_prog_zero_array_shell_from_plan(base_slot->as.zero_plan);
    materialized = *base_slot;
  } else {
    materialized = driver_c_prog_materialize(base_value);
  }
  if (materialized.kind != DRIVER_C_PROG_VALUE_ARRAY || materialized.as.array == NULL) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: index access on non-array label=%s base_kind=%s",
             context_label != NULL ? context_label : "",
             driver_c_prog_value_kind_name(materialized.kind));
    driver_c_die(message);
  }
  array = materialized.as.array;
  if (index < 0 || index >= array->len) {
    driver_c_die("driver_c program runtime: array index out of range");
  }
  return &array->items[index];
}

static void driver_c_prog_stack_push(DriverCProgFrame *frame, DriverCProgValue value) {
  if (frame->stack_len >= frame->stack_cap) {
    int32_t next_cap = frame->stack_cap > 0 ? frame->stack_cap * 2 : 16;
    if (frame->stack == frame->inline_stack) {
      DriverCProgValue *grown =
          (DriverCProgValue *)driver_c_prog_xmalloc(sizeof(DriverCProgValue) * (size_t)next_cap);
      memcpy(grown, frame->stack, sizeof(DriverCProgValue) * (size_t)frame->stack_len);
      frame->stack = grown;
    } else {
      frame->stack = (DriverCProgValue *)driver_c_prog_xrealloc(frame->stack,
                                                                sizeof(DriverCProgValue) * (size_t)next_cap);
    }
    frame->stack_cap = next_cap;
  }
  frame->stack[frame->stack_len++] = value;
}

static DriverCProgValue driver_c_prog_stack_pop(DriverCProgFrame *frame) {
  if (frame->stack_len <= 0) {
    char message[1024];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: stack underflow item=%s module=%s pc=%d op=%s primary=%s secondary=%s stack_len=%d",
             frame != NULL && frame->item != NULL && frame->item->label != NULL ? frame->item->label : "",
             frame != NULL && frame->item != NULL && frame->item->owner_module != NULL ? frame->item->owner_module : "",
             frame != NULL ? frame->current_pc : -1,
             frame != NULL && frame->current_op_kind != NULL ? frame->current_op_kind : "",
             frame != NULL && frame->current_op_primary != NULL ? frame->current_op_primary : "",
             frame != NULL && frame->current_op_secondary != NULL ? frame->current_op_secondary : "",
             frame != NULL ? frame->stack_len : -1);
    driver_c_die(message);
  }
  frame->stack_len = frame->stack_len - 1;
  return frame->stack[frame->stack_len];
}

static int32_t driver_c_prog_find_local_slot_by_name(DriverCProgFrame *frame, const char *name) {
  int32_t i = 0;
  if (frame == NULL || name == NULL) return -1;
  for (i = 0; i < frame->item->local_count; ++i) {
    if (frame->local_names[i] != NULL && driver_c_prog_text_eq(frame->local_names[i], name)) {
      return i;
    }
  }
  if (frame->item != NULL && frame->item->exec_op_count > 0) {
    for (i = 0; i < frame->item->exec_op_count; ++i) {
      DriverCProgOp *op = &frame->item->ops[i];
      if (op->arg0 < 0 || op->arg0 >= frame->item->local_count) {
        continue;
      }
      if (op->primary == NULL || !driver_c_prog_text_eq(op->primary, name)) {
        continue;
      }
      if (op->kind_tag == DRIVER_C_PROG_OP_LOCAL_DECL ||
          op->kind_tag == DRIVER_C_PROG_OP_LOAD_LOCAL ||
          op->kind_tag == DRIVER_C_PROG_OP_STORE_LOCAL ||
          op->kind_tag == DRIVER_C_PROG_OP_STORE_LOCAL_FIELD) {
        frame->local_names[op->arg0] = op->primary;
        return op->arg0;
      }
    }
  }
  return -1;
}

static int32_t driver_c_prog_find_label_pc(DriverCProgFrame *frame, const char *name) {
  int32_t i = 0;
  if (frame == NULL || name == NULL) return -1;
  for (i = 0; i < frame->label_count; ++i) {
    if (driver_c_prog_text_eq(frame->label_names[i], name)) {
      return frame->label_pcs[i];
    }
  }
  return -1;
}

static int32_t driver_c_prog_find_loop_slot(DriverCProgFrame *frame, const char *name) {
  int32_t i = 0;
  if (frame == NULL || name == NULL) return -1;
  for (i = 0; i < frame->loop_count; ++i) {
    if (frame->loop_names[i] != NULL && driver_c_prog_text_eq(frame->loop_names[i], name)) {
      return i;
    }
  }
  return -1;
}

static int32_t driver_c_prog_ensure_loop_slot(DriverCProgFrame *frame, const char *name, int32_t stmt_id) {
  int32_t pos = driver_c_prog_find_loop_slot(frame, name);
  if (pos >= 0) {
    frame->loop_stmt_ids[pos] = stmt_id;
    return pos;
  }
  pos = frame->loop_count;
  frame->loop_names = (char **)driver_c_prog_xrealloc(frame->loop_names, sizeof(char *) * (size_t)(pos + 1));
  frame->loop_currents = (int64_t *)driver_c_prog_xrealloc(frame->loop_currents, sizeof(int64_t) * (size_t)(pos + 1));
  frame->loop_ends = (int64_t *)driver_c_prog_xrealloc(frame->loop_ends, sizeof(int64_t) * (size_t)(pos + 1));
  frame->loop_stmt_ids = (int32_t *)driver_c_prog_xrealloc(frame->loop_stmt_ids, sizeof(int32_t) * (size_t)(pos + 1));
  frame->loop_names[pos] = driver_c_dup_cstring(name != NULL ? name : "");
  frame->loop_currents[pos] = 0;
  frame->loop_ends[pos] = 0;
  frame->loop_stmt_ids[pos] = stmt_id;
  frame->loop_count = pos + 1;
  return pos;
}

static void driver_c_prog_frame_init(DriverCProgFrame *frame,
                                     DriverCProgRegistry *registry,
                                     DriverCProgItem *item,
                                     DriverCProgValue *args,
                                     int32_t argc,
                                     const DriverCProgRoutine *routine_ctx) {
  int32_t i = 0;
  memset(frame, 0, sizeof(*frame));
  frame->registry = registry;
  frame->item = item;
  if (routine_ctx != NULL) {
    frame->type_arg_texts = routine_ctx->type_arg_texts;
    frame->type_arg_count = routine_ctx->type_arg_count;
  }
  frame->stack = frame->inline_stack;
  frame->stack_cap = DRIVER_C_PROG_FRAME_INLINE_STACK_CAP;
  if (item->param_count > 0) {
    if (item->param_count <= DRIVER_C_PROG_FRAME_INLINE_PARAM_CAP) {
      frame->params = frame->inline_params;
      frame->param_zero_plans = frame->inline_param_zero_plans;
      memset(frame->inline_params, 0, sizeof(DriverCProgValue) * (size_t)item->param_count);
      memset(frame->inline_param_zero_plans, 0, sizeof(DriverCProgZeroPlan *) * (size_t)item->param_count);
    } else {
      frame->params = (DriverCProgValue *)driver_c_prog_xcalloc((size_t)item->param_count, sizeof(DriverCProgValue));
      frame->param_zero_plans =
          (DriverCProgZeroPlan **)driver_c_prog_xcalloc((size_t)item->param_count, sizeof(DriverCProgZeroPlan *));
    }
    for (i = 0; i < item->param_count; ++i) {
      DriverCProgValue incoming = i < argc ? args[i] : driver_c_prog_value_nil();
      const char *param_type = item->param_type_texts != NULL ? item->param_type_texts[i] : NULL;
      if (item->param_is_var != NULL && item->param_is_var[i] != 0) {
        if (incoming.kind != DRIVER_C_PROG_VALUE_REF || incoming.as.ref.slot == NULL) {
          char message[512];
          snprintf(message,
                   sizeof(message),
                   "driver_c program runtime: var param missing ref callee=%s owner=%s param_index=%d incoming_kind=%s",
                   item->label != NULL ? item->label : "",
                   item->owner_module != NULL ? item->owner_module : "",
                   i,
                   driver_c_prog_value_kind_name(incoming.kind));
          driver_c_die(message);
        }
        frame->params[i] = incoming;
      } else if (param_type != NULL && param_type[0] != '\0') {
        char *resolved_param_type = driver_c_prog_resolve_frame_type_text_dup(frame, param_type);
        frame->param_zero_plans[i] =
            driver_c_prog_zero_plan_for_type(registry,
                                             item->owner_module,
                                             item->owner_module,
                                             resolved_param_type,
                                             0);
        frame->params[i] = driver_c_prog_value_zero_plan(frame->param_zero_plans[i]);
        driver_c_prog_assign_slot(&frame->params[i], incoming);
        free(resolved_param_type);
      } else {
        driver_c_prog_assign_slot(&frame->params[i], incoming);
      }
    }
  }
  if (item->local_count > 0) {
    if (item->local_count <= DRIVER_C_PROG_FRAME_INLINE_LOCAL_CAP) {
      frame->locals = frame->inline_locals;
      frame->local_zero_plans = frame->inline_local_zero_plans;
      frame->local_names = frame->inline_local_names;
      frame->local_is_var = frame->inline_local_is_var;
      memset(frame->inline_locals, 0, sizeof(DriverCProgValue) * (size_t)item->local_count);
      memset(frame->inline_local_zero_plans, 0, sizeof(DriverCProgZeroPlan *) * (size_t)item->local_count);
      memset(frame->inline_local_names, 0, sizeof(char *) * (size_t)item->local_count);
      memset(frame->inline_local_is_var, 0, sizeof(int32_t) * (size_t)item->local_count);
    } else {
      frame->locals = (DriverCProgValue *)driver_c_prog_xcalloc((size_t)item->local_count, sizeof(DriverCProgValue));
      frame->local_zero_plans =
          (DriverCProgZeroPlan **)driver_c_prog_xcalloc((size_t)item->local_count, sizeof(DriverCProgZeroPlan *));
      frame->local_names = (char **)driver_c_prog_xcalloc((size_t)item->local_count, sizeof(char *));
      frame->local_is_var = (int32_t *)driver_c_prog_xcalloc((size_t)item->local_count, sizeof(int32_t));
    }
    if (item->local_type_texts != NULL) {
      for (i = 0; i < item->local_count; ++i) {
        const char *local_type = item->local_type_texts[i];
        if (local_type == NULL || local_type[0] == '\0') {
          continue;
        }
        {
          char *resolved_local_type = driver_c_prog_resolve_frame_type_text_dup(frame, local_type);
          frame->local_zero_plans[i] =
              driver_c_prog_zero_plan_for_type(registry,
                                               item->owner_module,
                                               item->owner_module,
                                               resolved_local_type,
                                               0);
          free(resolved_local_type);
        }
      }
    }
  }
  if (item->exec_op_count > 0) {
    if (item->exec_op_count <= DRIVER_C_PROG_FRAME_INLINE_LABEL_CAP) {
      frame->label_names = frame->inline_label_names;
      frame->label_pcs = frame->inline_label_pcs;
      memset(frame->inline_label_names, 0, sizeof(char *) * (size_t)item->exec_op_count);
      memset(frame->inline_label_pcs, 0, sizeof(int32_t) * (size_t)item->exec_op_count);
    } else {
      frame->label_names = (char **)driver_c_prog_xcalloc((size_t)item->exec_op_count, sizeof(char *));
      frame->label_pcs = (int32_t *)driver_c_prog_xcalloc((size_t)item->exec_op_count, sizeof(int32_t));
    }
    for (i = 0; i < item->exec_op_count; ++i) {
      DriverCProgOp *op = &item->ops[i];
      if (op->kind_tag == DRIVER_C_PROG_OP_LABEL) {
        frame->label_names[frame->label_count] = op->primary;
        frame->label_pcs[frame->label_count] = i + 1;
        frame->label_count = frame->label_count + 1;
      } else if (op->kind_tag == DRIVER_C_PROG_OP_LOCAL_DECL) {
        int32_t slot = op->arg0;
        if (slot >= 0 && slot < item->local_count && frame->local_names[slot] == NULL) {
          frame->local_names[slot] = op->primary;
          frame->local_is_var[slot] = driver_c_prog_text_eq(op->secondary, "var") ? 1 : 0;
        }
      }
    }
  }
}

static void driver_c_prog_frame_cleanup(DriverCProgFrame *frame) {
  int32_t i = 0;
  if (frame == NULL) {
    return;
  }
  if (frame->loop_names != NULL) {
    for (i = 0; i < frame->loop_count; ++i) {
      free(frame->loop_names[i]);
    }
  }
  if (frame->params != NULL && frame->params != frame->inline_params) {
    free(frame->params);
  }
  if (frame->param_zero_plans != NULL && frame->param_zero_plans != frame->inline_param_zero_plans) {
    free(frame->param_zero_plans);
  }
  if (frame->locals != NULL && frame->locals != frame->inline_locals) {
    free(frame->locals);
  }
  if (frame->local_zero_plans != NULL && frame->local_zero_plans != frame->inline_local_zero_plans) {
    free(frame->local_zero_plans);
  }
  if (frame->local_names != NULL && frame->local_names != frame->inline_local_names) {
    free(frame->local_names);
  }
  if (frame->local_is_var != NULL && frame->local_is_var != frame->inline_local_is_var) {
    free(frame->local_is_var);
  }
  if (frame->stack != NULL && frame->stack != frame->inline_stack) {
    free(frame->stack);
  }
  if (frame->label_names != NULL && frame->label_names != frame->inline_label_names) {
    free(frame->label_names);
  }
  if (frame->label_pcs != NULL && frame->label_pcs != frame->inline_label_pcs) {
    free(frame->label_pcs);
  }
  free(frame->loop_names);
  free(frame->loop_currents);
  free(frame->loop_ends);
  free(frame->loop_stmt_ids);
}

static char *driver_c_prog_resolve_frame_type_text_dup(const DriverCProgFrame *frame,
                                                       const char *type_text) {
  char *resolved = NULL;
  int32_t i = 0;
  if (type_text == NULL) {
    return driver_c_dup_cstring("");
  }
  resolved = driver_c_prog_normalize_param_type_text_dup(type_text);
  if (frame == NULL || frame->item == NULL || frame->type_arg_texts == NULL) {
    return resolved;
  }
  for (i = 0; i < frame->item->type_param_count && i < frame->type_arg_count; ++i) {
    if (frame->item->type_param_names[i] == NULL ||
        frame->item->type_param_names[i][0] == '\0' ||
        frame->type_arg_texts[i] == NULL ||
        frame->type_arg_texts[i][0] == '\0') {
      continue;
    }
    {
      char *next = driver_c_prog_substitute_type_token_dup(resolved,
                                                           frame->item->type_param_names[i],
                                                           frame->type_arg_texts[i]);
      free(resolved);
      resolved = next;
    }
  }
  return resolved;
}

static DriverCProgValue driver_c_prog_eval_item(DriverCProgRegistry *registry,
                                                DriverCProgItem *item,
                                                DriverCProgValue *args,
                                                int32_t argc,
                                                const DriverCProgRoutine *routine_ctx);

static DriverCProgValue driver_c_prog_eval_global(DriverCProgRegistry *registry, int32_t item_id) {
  DriverCProgItem *item = driver_c_prog_find_item_by_id(registry, item_id);
  if (item == NULL) {
    driver_c_die("driver_c program runtime: global item missing");
  }
  if (item->top_level_tag == DRIVER_C_PROG_TOP_TYPE) {
    return driver_c_prog_value_type_token(item->label);
  }
  if (item_id >= 0 && item_id < registry->items_by_id_len && registry->global_initialized[item_id]) {
    return registry->global_values[item_id];
  }
  if (item->top_level_tag == DRIVER_C_PROG_TOP_VAR &&
      item->exec_op_count <= 0 &&
      item->signature_line_count > 0 &&
      item->signature_lines != NULL &&
      item->signature_lines[0] != NULL) {
    char *declared_type = driver_c_prog_type_field_type_from_line_dup(item->signature_lines[0], item->label);
    if (declared_type[0] != '\0') {
      registry->global_values[item_id] =
          driver_c_prog_zero_value_from_type(registry,
                                             item->owner_module,
                                             item->owner_module,
                                             declared_type,
                                             0);
      registry->global_initialized[item_id] = 1;
      free(declared_type);
      return registry->global_values[item_id];
    }
    free(declared_type);
  }
  registry->global_values[item_id] = driver_c_prog_eval_item(registry, item, NULL, 0, NULL);
  registry->global_initialized[item_id] = 1;
  return registry->global_values[item_id];
}

static DriverCProgValue driver_c_prog_builtin_param_count(void) {
  return driver_c_prog_value_i32(__cheng_rt_paramCount());
}

static DriverCProgValue driver_c_prog_builtin_param_str(DriverCProgValue idx_value) {
  int32_t idx = driver_c_prog_value_to_i32(idx_value);
  char *dup = __cheng_rt_paramStrCopy(idx);
  if (dup == NULL) {
    return driver_c_prog_value_str_empty();
  }
  return driver_c_prog_value_str_take(dup);
}

static int driver_c_prog_read_flag_raw(const char *key, char **out_value) {
  int32_t argc = __cheng_rt_paramCount();
  int32_t i = 0;
  if (out_value != NULL) {
    *out_value = driver_c_dup_cstring("");
  }
  for (i = 1; i < argc; ++i) {
    const char *arg = __cheng_rt_paramStr(i);
    if (arg != NULL && driver_c_prog_text_eq(arg, key)) {
      if (out_value != NULL) {
        free(*out_value);
        if (i + 1 < argc) {
          *out_value = __cheng_rt_paramStrCopy(i + 1);
        } else {
          *out_value = driver_c_dup_cstring("");
        }
      }
      return 1;
    }
  }
  return 0;
}

static int driver_c_prog_is_builtin_label(const char *label) {
  return driver_c_prog_builtin_tag_from_text(label) != DRIVER_C_PROG_BUILTIN_NONE;
}

static int driver_c_prog_is_builtin_type_ctor_label(const char *label) {
  return driver_c_prog_text_eq(label, "bool") ||
         driver_c_prog_text_eq(label, "int") ||
         driver_c_prog_text_eq(label, "int8") ||
         driver_c_prog_text_eq(label, "int16") ||
         driver_c_prog_text_eq(label, "int32") ||
         driver_c_prog_text_eq(label, "int64") ||
         driver_c_prog_text_eq(label, "uint8") ||
         driver_c_prog_text_eq(label, "uint16") ||
         driver_c_prog_text_eq(label, "uint32") ||
         driver_c_prog_text_eq(label, "uint64") ||
         driver_c_prog_text_eq(label, "char") ||
         driver_c_prog_text_eq(label, "float64") ||
         driver_c_prog_text_eq(label, "str") ||
         driver_c_prog_text_eq(label, "cstring") ||
         driver_c_prog_text_eq(label, "ptr");
}

static int driver_c_prog_is_builtin_module_field(const char *module_name, const char *field_name) {
  return driver_c_prog_text_eq(module_name, "std/strutils") &&
         driver_c_prog_text_eq(field_name, "strip");
}

static int driver_c_prog_try_builtin(DriverCProgRegistry *registry,
                                     int32_t builtin_tag,
                                     const char *label,
                                     const char *symbol,
                                     DriverCProgValue *args,
                                     int32_t argc,
                                     DriverCProgValue *out) {
  static const uint32_t driver_c_sha256_k[64] = {
      UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
      UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
      UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
      UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
      UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
      UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
      UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
      UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
      UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
      UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
      UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
      UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
      UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
      UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
      UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
      UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2),
  };
  static const uint64_t driver_c_sha512_k[80] = {
      UINT64_C(0x428a2f98d728ae22), UINT64_C(0x7137449123ef65cd),
      UINT64_C(0xb5c0fbcfec4d3b2f), UINT64_C(0xe9b5dba58189dbbc),
      UINT64_C(0x3956c25bf348b538), UINT64_C(0x59f111f1b605d019),
      UINT64_C(0x923f82a4af194f9b), UINT64_C(0xab1c5ed5da6d8118),
      UINT64_C(0xd807aa98a3030242), UINT64_C(0x12835b0145706fbe),
      UINT64_C(0x243185be4ee4b28c), UINT64_C(0x550c7dc3d5ffb4e2),
      UINT64_C(0x72be5d74f27b896f), UINT64_C(0x80deb1fe3b1696b1),
      UINT64_C(0x9bdc06a725c71235), UINT64_C(0xc19bf174cf692694),
      UINT64_C(0xe49b69c19ef14ad2), UINT64_C(0xefbe4786384f25e3),
      UINT64_C(0x0fc19dc68b8cd5b5), UINT64_C(0x240ca1cc77ac9c65),
      UINT64_C(0x2de92c6f592b0275), UINT64_C(0x4a7484aa6ea6e483),
      UINT64_C(0x5cb0a9dcbd41fbd4), UINT64_C(0x76f988da831153b5),
      UINT64_C(0x983e5152ee66dfab), UINT64_C(0xa831c66d2db43210),
      UINT64_C(0xb00327c898fb213f), UINT64_C(0xbf597fc7beef0ee4),
      UINT64_C(0xc6e00bf33da88fc2), UINT64_C(0xd5a79147930aa725),
      UINT64_C(0x06ca6351e003826f), UINT64_C(0x142929670a0e6e70),
      UINT64_C(0x27b70a8546d22ffc), UINT64_C(0x2e1b21385c26c926),
      UINT64_C(0x4d2c6dfc5ac42aed), UINT64_C(0x53380d139d95b3df),
      UINT64_C(0x650a73548baf63de), UINT64_C(0x766a0abb3c77b2a8),
      UINT64_C(0x81c2c92e47edaee6), UINT64_C(0x92722c851482353b),
      UINT64_C(0xa2bfe8a14cf10364), UINT64_C(0xa81a664bbc423001),
      UINT64_C(0xc24b8b70d0f89791), UINT64_C(0xc76c51a30654be30),
      UINT64_C(0xd192e819d6ef5218), UINT64_C(0xd69906245565a910),
      UINT64_C(0xf40e35855771202a), UINT64_C(0x106aa07032bbd1b8),
      UINT64_C(0x19a4c116b8d2d0c8), UINT64_C(0x1e376c085141ab53),
      UINT64_C(0x2748774cdf8eeb99), UINT64_C(0x34b0bcb5e19b48a8),
      UINT64_C(0x391c0cb3c5c95a63), UINT64_C(0x4ed8aa4ae3418acb),
      UINT64_C(0x5b9cca4f7763e373), UINT64_C(0x682e6ff3d6b2b8a3),
      UINT64_C(0x748f82ee5defb2fc), UINT64_C(0x78a5636f43172f60),
      UINT64_C(0x84c87814a1f0ab72), UINT64_C(0x8cc702081a6439ec),
      UINT64_C(0x90befffa23631e28), UINT64_C(0xa4506cebde82bde9),
      UINT64_C(0xbef9a3f7b2c67915), UINT64_C(0xc67178f2e372532b),
      UINT64_C(0xca273eceea26619c), UINT64_C(0xd186b8c721c0c207),
      UINT64_C(0xeada7dd6cde0eb1e), UINT64_C(0xf57d4f7fee6ed178),
      UINT64_C(0x06f067aa72176fba), UINT64_C(0x0a637dc5a2c898a6),
      UINT64_C(0x113f9804bef90dae), UINT64_C(0x1b710b35131c471b),
      UINT64_C(0x28db77f523047d84), UINT64_C(0x32caab7b40c72493),
      UINT64_C(0x3c9ebe0a15c9bebc), UINT64_C(0x431d67c49c100d4c),
      UINT64_C(0x4cc5d4becb3e42b6), UINT64_C(0x597f299cfc657e2a),
      UINT64_C(0x5fcb6fab3ad6faec), UINT64_C(0x6c44198c4a475817),
  };
  int32_t i = 0;
  if (label == NULL) label = "";
  if (symbol == NULL) symbol = "";
  if (builtin_tag == DRIVER_C_PROG_BUILTIN_NONE) goto try_builtin_symbols;
  if (driver_c_prog_text_eq(label, "paramCount") || driver_c_prog_text_eq(label, "argCount")) {
    *out = driver_c_prog_builtin_param_count();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "paramStr") || driver_c_prog_text_eq(label, "argStr")) {
    if (argc != 1) driver_c_die("driver_c program runtime: paramStr argc mismatch");
    *out = driver_c_prog_builtin_param_str(args[0]);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "readFlagValue")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[1]);
    ChengStrBridge out_value = driver_c_prog_str_bridge_empty();
    DriverCProgValue out_str = driver_c_prog_value_str_empty();
    int32_t found = 0;
    if (slot == NULL) driver_c_die("driver_c program runtime: readFlagValue out must be ref");
    found = driver_c_read_flag_value_bridge(driver_c_prog_materialize(args[0]).as.str, &out_value);
    out_str.as.str = out_value;
    driver_c_prog_assign_slot(slot, out_str);
    *out = driver_c_prog_value_bool(found);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "parseInt32")) {
    char *raw = driver_c_prog_value_to_cstring_dup(args[0]);
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[1]);
    char *end = NULL;
    long value = 0;
    int ok = 0;
    if (slot == NULL) driver_c_die("driver_c program runtime: parseInt32 out must be ref");
    errno = 0;
    value = strtol(raw, &end, 10);
    ok = errno == 0 && end != raw && end != NULL && *end == '\0';
    if (ok) {
      driver_c_prog_assign_slot(slot, driver_c_prog_value_i32((int32_t)value));
    }
    free(raw);
    *out = driver_c_prog_value_bool(ok);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "parseInt64")) {
    char *raw = driver_c_prog_value_to_cstring_dup(args[0]);
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[1]);
    char *end = NULL;
    long long value = 0;
    int ok = 0;
    if (slot == NULL) driver_c_die("driver_c program runtime: parseInt64 out must be ref");
    errno = 0;
    value = strtoll(raw, &end, 10);
    ok = errno == 0 && end != raw && end != NULL && *end == '\0';
    if (ok) {
      driver_c_prog_assign_slot(slot, driver_c_prog_value_i64((int64_t)value));
    }
    free(raw);
    *out = driver_c_prog_value_i64(ok ? 1 : 0);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "intToStr")) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", driver_c_prog_value_to_i32(args[0]));
    *out = driver_c_prog_value_str_dup(buf);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "uint64ToStr")) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)driver_c_prog_value_to_u64(args[0]));
    *out = driver_c_prog_value_str_dup(buf);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "int64ToStr")) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld", (long long)driver_c_prog_value_to_i64(args[0]));
    *out = driver_c_prog_value_str_dup(buf);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "len")) {
    DriverCProgValue value = driver_c_prog_materialize(args[0]);
    if (value.kind == DRIVER_C_PROG_VALUE_STR) *out = driver_c_prog_value_i32(driver_c_prog_str_len(value.as.str));
    else if (value.kind == DRIVER_C_PROG_VALUE_BYTES) *out = driver_c_prog_value_i32(value.as.bytes.len);
    else if (value.kind == DRIVER_C_PROG_VALUE_ARRAY) *out = driver_c_prog_value_i32(value.as.array != NULL ? value.as.array->len : 0);
    else *out = driver_c_prog_value_i32(0);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "sliceBytes")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    int32_t start = driver_c_prog_value_to_i32(args[1]);
    int32_t count = driver_c_prog_value_to_i32(args[2]);
    char *slice = driver_c_prog_slice_dup(text, start, count);
    free(text);
    *out = driver_c_prog_value_str_take(slice);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "find")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    char *sub = driver_c_prog_value_to_cstring_dup(args[1]);
    int32_t pos = driver_c_prog_find_substr(text, sub);
    free(text);
    free(sub);
    *out = driver_c_prog_value_i32(pos);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "startsWith") || driver_c_prog_text_eq(label, "hasPrefix")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    char *prefix = driver_c_prog_value_to_cstring_dup(args[1]);
    int ok = driver_c_prog_text_starts_with(text, prefix);
    free(text);
    free(prefix);
    *out = driver_c_prog_value_bool(ok);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "strContains")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    char *sub = driver_c_prog_value_to_cstring_dup(args[1]);
    int32_t pos = driver_c_prog_find_substr(text, sub);
    free(text);
    free(sub);
    *out = driver_c_prog_value_bool(pos >= 0);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "strip")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    char *trimmed = NULL;
    if (argc >= 2) {
      uint64_t mask = (uint64_t)driver_c_prog_value_to_i64(args[1]);
      trimmed = driver_c_prog_trim_chars_dup(text, mask);
    } else {
      trimmed = driver_c_prog_trim_dup(text);
    }
    free(text);
    *out = driver_c_prog_value_str_take(trimmed);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "split")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    DriverCProgValue sep_value = driver_c_prog_materialize(args[1]);
    char *sep = NULL;
    DriverCProgArray *array = driver_c_prog_array_new();
    char *cursor = text;
    size_t sep_len = 0u;
    if (sep_value.kind == DRIVER_C_PROG_VALUE_I32 || sep_value.kind == DRIVER_C_PROG_VALUE_U32) {
      sep = (char *)driver_c_prog_xmalloc(2u);
      sep[0] = (char)(driver_c_prog_value_to_u32(sep_value) & 255u);
      sep[1] = '\0';
    } else {
      sep = driver_c_prog_value_to_cstring_dup(sep_value);
    }
    sep_len = strlen(sep);
    if (sep_len == 0u) {
      driver_c_prog_array_push(array, driver_c_prog_value_str_take(text));
      free(sep);
      *out = driver_c_prog_value_array(array);
      return 1;
    }
    while (1) {
      char *hit = strstr(cursor, sep);
      if (hit == NULL) {
        driver_c_prog_array_push(array, driver_c_prog_value_str_take(driver_c_dup_cstring(cursor)));
        break;
      }
      driver_c_prog_array_push(array, driver_c_prog_value_str_take(driver_c_dup_range(cursor, hit)));
      cursor = hit + sep_len;
    }
    free(text);
    free(sep);
    *out = driver_c_prog_value_array(array);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "charToStr")) {
    char buf[8];
    int32_t code = driver_c_prog_value_to_i32(args[0]);
    buf[0] = (char)(code & 255);
    buf[1] = '\0';
    *out = driver_c_prog_value_str_dup(buf);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "parentDir")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    driver_c_parent_dir_inplace(path);
    *out = driver_c_prog_value_str_take(path);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "absolutePath")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    char *abs = driver_c_absolute_path_dup(path);
    free(path);
    *out = driver_c_prog_value_str_take(abs);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "joinPath")) {
    char *left = driver_c_prog_value_to_cstring_dup(args[0]);
    char *right = driver_c_prog_value_to_cstring_dup(args[1]);
    char *joined = driver_c_join_path2_dup(left, right);
    free(left);
    free(right);
    *out = driver_c_prog_value_str_take(joined);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "fileExists")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    int ok = cheng_file_exists(path) != 0;
    free(path);
    *out = driver_c_prog_value_bool(ok);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "dirExists")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    int ok = cheng_dir_exists(path) != 0;
    free(path);
    *out = driver_c_prog_value_bool(ok);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "createDir")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    if (path[0] != '\0' && cheng_dir_exists(path) == 0) {
      if (cheng_mkdir1(path) != 0 && errno != EEXIST) {
        free(path);
        driver_c_die_errno("driver_c program runtime: mkdir failed", "");
      }
    }
    free(path);
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "readFile")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    char *text = driver_c_read_file_all(path);
    free(path);
    *out = driver_c_prog_value_str_take(text != NULL ? text : driver_c_dup_cstring(""));
    return 1;
  }
  if (driver_c_prog_text_eq(label, "writeFile")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    char *content = driver_c_prog_value_to_cstring_dup(args[1]);
    driver_c_write_text_file_bridge(driver_c_owned_bridge_from_cstring(path),
                                    driver_c_owned_bridge_from_cstring(content));
    free(path);
    free(content);
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "chainAppendLine")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    char *line = driver_c_prog_value_to_cstring_dup(args[1]);
    FILE *f = fopen(path, "a");
    int ok = 0;
    if (f != NULL) {
      ok = fputs(line, f) >= 0 && fputc('\n', f) != EOF && fclose(f) == 0;
    }
    free(path);
    free(line);
    *out = driver_c_prog_value_bool(ok);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "echo")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    fputs(text, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    free(text);
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "emptyBytes")) {
    *out = driver_c_prog_value_bytes_empty();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "bytesAlloc")) {
    int32_t len = driver_c_prog_value_to_i32(args[0]);
    uint8_t *buf = NULL;
    if (len < 0) len = 0;
    if (len > 0) {
      buf = (uint8_t *)driver_c_prog_xcalloc((size_t)len, 1u);
    }
    *out = driver_c_prog_value_bytes_dup(buf, len);
    free(buf);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "bytesLen")) {
    DriverCProgValue value = driver_c_prog_materialize(args[0]);
    *out = driver_c_prog_value_i32(value.kind == DRIVER_C_PROG_VALUE_BYTES ? value.as.bytes.len : 0);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "bytesFromString")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_bytes_dup((const uint8_t *)text, (int32_t)strlen(text));
    free(text);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "bytesToString")) {
    DriverCProgValue value = driver_c_prog_materialize(args[0]);
    char *text = NULL;
    if (value.kind != DRIVER_C_PROG_VALUE_BYTES) {
      *out = driver_c_prog_value_str_dup("");
      return 1;
    }
    text = (char *)driver_c_prog_xmalloc((size_t)value.as.bytes.len + 1u);
    if (value.as.bytes.len > 0 && value.as.bytes.data != NULL) {
      memcpy(text, value.as.bytes.data, (size_t)value.as.bytes.len);
    }
    text[value.as.bytes.len] = '\0';
    *out = driver_c_prog_value_str_take(text);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "bytesGet")) {
    DriverCProgValue value = driver_c_prog_materialize(args[0]);
    int32_t idx = driver_c_prog_value_to_i32(args[1]);
    if (value.kind != DRIVER_C_PROG_VALUE_BYTES || idx < 0 || idx >= value.as.bytes.len) {
      driver_c_die("driver_c program runtime: bytesGet out of range");
    }
    *out = driver_c_prog_value_i32((int32_t)value.as.bytes.data[idx]);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "bytesSet")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[0]);
    DriverCProgValue value = slot != NULL ? driver_c_prog_materialize(*slot) : driver_c_prog_materialize(args[0]);
    int32_t idx = driver_c_prog_value_to_i32(args[1]);
    int32_t byte = driver_c_prog_value_to_i32(args[2]);
    if (value.kind != DRIVER_C_PROG_VALUE_BYTES || idx < 0 || idx >= value.as.bytes.len) {
      driver_c_die("driver_c program runtime: bytesSet out of range");
    }
    value.as.bytes.data[idx] = (uint8_t)(byte & 255);
    if (slot != NULL) {
      driver_c_prog_assign_slot(slot, value);
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "bytesSlice")) {
    DriverCProgValue value = driver_c_prog_materialize(args[0]);
    int32_t start = driver_c_prog_value_to_i32(args[1]);
    int32_t count = driver_c_prog_value_to_i32(args[2]);
    if (value.kind != DRIVER_C_PROG_VALUE_BYTES) {
      *out = driver_c_prog_value_bytes_empty();
      return 1;
    }
    if (start < 0) start = 0;
    if (start > value.as.bytes.len) start = value.as.bytes.len;
    if (count < 0) count = 0;
    if (start + count > value.as.bytes.len) count = value.as.bytes.len - start;
    *out = driver_c_prog_value_bytes_dup(value.as.bytes.data + start, count);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "bytesToHex")) {
    DriverCProgValue value = driver_c_prog_materialize(args[0]);
    char *text = NULL;
    static const char *digits = "0123456789abcdef";
    if (value.kind != DRIVER_C_PROG_VALUE_BYTES) {
      *out = driver_c_prog_value_str_dup("");
      return 1;
    }
    text = (char *)driver_c_prog_xmalloc((size_t)value.as.bytes.len * 2u + 1u);
    for (i = 0; i < value.as.bytes.len; ++i) {
      text[i * 2] = digits[(value.as.bytes.data[i] >> 4) & 15];
      text[i * 2 + 1] = digits[value.as.bytes.data[i] & 15];
    }
    text[(size_t)value.as.bytes.len * 2u] = '\0';
    *out = driver_c_prog_value_str_take(text);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "sha256Digest")) {
    DriverCProgValue value = driver_c_prog_materialize(args[0]);
    DriverCSha256Ctx ctx;
    uint8_t hash[32];
    if (value.kind != DRIVER_C_PROG_VALUE_BYTES) {
      driver_c_die("driver_c program runtime: sha256Digest expects Bytes");
    }
    driver_c_sha256_init(&ctx);
    driver_c_sha256_update(&ctx, value.as.bytes.data, (size_t)value.as.bytes.len);
    driver_c_sha256_final(&ctx, hash);
    *out = driver_c_prog_value_bytes_dup(hash, 32);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "k256")) {
    int32_t idx = driver_c_prog_value_to_i32(args[0]);
    if (idx < 0 || idx >= 64) {
      *out = driver_c_prog_value_i64(0);
    } else {
      *out = driver_c_prog_value_i64((int64_t)driver_c_sha256_k[idx]);
    }
    return 1;
  }
  if (driver_c_prog_text_eq(label, "sizeof")) {
    DriverCProgValue type_value = argc > 0 ? driver_c_prog_materialize(args[0]) : driver_c_prog_value_nil();
    int32_t size = 0;
    if (type_value.kind != DRIVER_C_PROG_VALUE_TYPE_TOKEN) {
      driver_c_die("driver_c program runtime: sizeof expects type token");
    }
    size = driver_c_prog_sizeof_type_text(registry, type_value.as.type_text);
    if (size <= 0) {
      char message[512];
      snprintf(message,
               sizeof(message),
               "driver_c program runtime: unsupported sizeof type=%s",
               type_value.as.type_text != NULL ? type_value.as.type_text : "");
      driver_c_die(message);
    }
    *out = driver_c_prog_value_i32(size);
    return 1;
  }
  if (driver_c_prog_text_eq(label, "alloc")) {
    int32_t size = driver_c_prog_value_to_i32(args[0]);
    int64_t bytes = size <= 0 ? 1 : (int64_t)size;
    *out = driver_c_prog_value_ptr(malloc((size_t)bytes));
    return 1;
  }
  if (driver_c_prog_text_eq(label, "dealloc")) {
    void *p = driver_c_prog_value_to_void_ptr(args[0]);
    free(p);
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "realloc")) {
    void *p = driver_c_prog_value_to_void_ptr(args[0]);
    int32_t size = driver_c_prog_value_to_i32(args[1]);
    int64_t bytes = size <= 0 ? 1 : (int64_t)size;
    *out = driver_c_prog_value_ptr(realloc(p, (size_t)bytes));
    return 1;
  }
  if (driver_c_prog_text_eq(label, "setLen")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[0]);
    DriverCProgValue value = slot != NULL ? driver_c_prog_materialize(*slot) : driver_c_prog_materialize(args[0]);
    int32_t new_len = driver_c_prog_value_to_i32(args[1]);
    if (value.kind != DRIVER_C_PROG_VALUE_ARRAY) {
      value = driver_c_prog_value_array_temp(driver_c_prog_array_new());
    }
    driver_c_prog_array_set_len(value.as.array, new_len);
    if (slot != NULL) {
      driver_c_prog_assign_slot(slot, value);
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "reserve")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[0]);
    DriverCProgValue value = slot != NULL ? driver_c_prog_materialize(*slot) : driver_c_prog_materialize(args[0]);
    int32_t need = driver_c_prog_value_to_i32(args[1]);
    if (value.kind != DRIVER_C_PROG_VALUE_ARRAY) {
      value = driver_c_prog_value_array_temp(driver_c_prog_array_new());
    }
    driver_c_prog_array_ensure_cap(value.as.array, need);
    if (slot != NULL) {
      driver_c_prog_assign_slot(slot, value);
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "add")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[0]);
    DriverCProgValue value = slot != NULL ? driver_c_prog_materialize(*slot) : driver_c_prog_materialize(args[0]);
    if (value.kind != DRIVER_C_PROG_VALUE_ARRAY) {
      value = driver_c_prog_value_array_temp(driver_c_prog_array_new());
    }
    driver_c_prog_array_push(value.as.array, args[1]);
    if (slot != NULL) {
      driver_c_prog_assign_slot(slot, value);
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "initMsQuicTransport")) {
    *out = driver_c_prog_value_record(driver_c_prog_record_new());
    return 1;
  }
  if (driver_c_prog_text_eq(label, "strutils_cheng_seq_string_elem_bytes_compat") ||
      driver_c_prog_text_eq(label, "cheng_seq_string_elem_bytes_compat")) {
    *out = driver_c_prog_value_i32(cheng_seq_string_elem_bytes_compat());
    return 1;
  }
  if (driver_c_prog_text_eq(label, "strutils_cheng_seq_string_register_compat") ||
      driver_c_prog_text_eq(label, "cheng_seq_string_register_compat")) {
    DriverCProgArray *array = driver_c_prog_array_from_refish(args[0], 1);
    if (array == NULL) {
      cheng_seq_string_register_compat(driver_c_prog_value_to_void_ptr(args[0]));
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "strutils_cheng_seq_string_buffer_register_compat") ||
      driver_c_prog_text_eq(label, "cheng_seq_string_buffer_register_compat")) {
    cheng_seq_string_buffer_register_compat(driver_c_prog_value_to_void_ptr(args[0]));
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "strutils_cheng_seq_set_grow") ||
      driver_c_prog_text_eq(label, "cheng_seq_set_grow")) {
    DriverCProgArray *array = driver_c_prog_array_from_refish(args[0], 1);
    if (array != NULL) {
      int32_t idx = driver_c_prog_value_to_i32(args[1]);
      if (idx < 0) {
        driver_c_die("driver_c program runtime: cheng_seq_set_grow negative index");
      }
      driver_c_prog_array_set_len(array, idx + 1);
      *out = driver_c_prog_value_ptr(driver_c_prog_str_seq_slot_token_new(&array->items[idx]));
    } else {
      *out = driver_c_prog_value_ptr(
          driver_c_prog_seq_set_grow_local(driver_c_prog_value_to_void_ptr(args[0]),
                                           driver_c_prog_value_to_i32(args[1]),
                                           driver_c_prog_value_to_i32(args[2])));
    }
    return 1;
  }
  if (driver_c_prog_text_eq(label, "cheng__seqGrowInst")) {
    DriverCProgArray *array = driver_c_prog_array_from_refish(args[0], 1);
    if (array != NULL) {
      int32_t min_cap = driver_c_prog_value_to_i32(args[1]);
      if (min_cap < 0) {
        min_cap = 0;
      }
      driver_c_prog_array_ensure_cap(array, min_cap);
      *out = driver_c_prog_value_nil();
    } else {
      driver_c_prog_seq_grow_inst_local(driver_c_prog_value_to_void_ptr(args[0]),
                                        driver_c_prog_value_to_i32(args[1]),
                                        driver_c_prog_value_to_i32(args[2]));
      *out = driver_c_prog_value_nil();
    }
    return 1;
  }
  if (driver_c_prog_text_eq(label, "cheng__seqZeroTailRaw")) {
    DriverCProgArray *array = driver_c_prog_array_from_refish(args[0], 1);
    if (array == NULL) {
      array = driver_c_prog_array_from_buffer_ptr(driver_c_prog_value_to_void_ptr(args[0]));
    }
    if (array != NULL) {
      int32_t target = driver_c_prog_value_to_i32(args[3]);
      if (target < 0) {
        target = 0;
      }
      driver_c_prog_array_set_len(array, target);
      *out = driver_c_prog_value_nil();
    } else {
      driver_c_prog_seq_zero_tail_raw_local(driver_c_prog_value_to_void_ptr(args[0]),
                                            driver_c_prog_value_to_i32(args[1]),
                                            driver_c_prog_value_to_i32(args[2]),
                                            driver_c_prog_value_to_i32(args[3]),
                                            driver_c_prog_value_to_i32(args[4]));
      *out = driver_c_prog_value_nil();
    }
    return 1;
  }
  if (driver_c_prog_text_eq(label, "cmdlineParamStrCopyIntoBridge")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[1]);
    if (slot == NULL) driver_c_die("driver_c program runtime: cmdlineParamStrCopyIntoBridge out must be ref");
    driver_c_prog_assign_slot(slot, driver_c_prog_builtin_param_str(args[0]));
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "intToStrRaw")) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", driver_c_prog_value_to_i32(args[0]));
    *out = driver_c_prog_value_ptr(driver_c_dup_cstring(buf));
    return 1;
  }
  if (driver_c_prog_text_eq(label, "uint64ToStrRaw")) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)driver_c_prog_value_to_u64(args[0]));
    *out = driver_c_prog_value_ptr(driver_c_dup_cstring(buf));
    return 1;
  }
  if (driver_c_prog_text_eq(label, "int64ToStrRaw")) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld", (long long)driver_c_prog_value_to_i64(args[0]));
    *out = driver_c_prog_value_ptr(driver_c_dup_cstring(buf));
    return 1;
  }
try_builtin_symbols:
  if (driver_c_prog_text_eq(symbol, "driver_c_read_flag_value_bridge")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[1]);
    ChengStrBridge out_value = driver_c_prog_str_bridge_empty();
    DriverCProgValue out_str = driver_c_prog_value_str_empty();
    int32_t ok = 0;
    if (slot == NULL) driver_c_die("driver_c program runtime: read_flag_value_bridge out must be ref");
    ok = driver_c_read_flag_value_bridge(driver_c_prog_materialize(args[0]).as.str, &out_value);
    out_str.as.str = out_value;
    driver_c_prog_assign_slot(slot, out_str);
    *out = driver_c_prog_value_bool(ok != 0);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_seq_string_elem_bytes_compat")) {
    *out = driver_c_prog_value_i32(cheng_seq_string_elem_bytes_compat());
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_seq_string_register_compat")) {
    DriverCProgArray *array = driver_c_prog_array_from_refish(args[0], 1);
    if (array == NULL) {
      cheng_seq_string_register_compat(driver_c_prog_value_to_void_ptr(args[0]));
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_seq_string_buffer_register_compat")) {
    cheng_seq_string_buffer_register_compat(driver_c_prog_value_to_void_ptr(args[0]));
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_seq_set_grow")) {
    DriverCProgArray *array = driver_c_prog_array_from_refish(args[0], 1);
    if (array != NULL) {
      int32_t idx = driver_c_prog_value_to_i32(args[1]);
      if (idx < 0) {
        driver_c_die("driver_c program runtime: cheng_seq_set_grow negative index");
      }
      driver_c_prog_array_set_len(array, idx + 1);
      *out = driver_c_prog_value_ptr(driver_c_prog_str_seq_slot_token_new(&array->items[idx]));
    } else {
      *out = driver_c_prog_value_ptr(
          driver_c_prog_seq_set_grow_local(driver_c_prog_value_to_void_ptr(args[0]),
                                           driver_c_prog_value_to_i32(args[1]),
                                           driver_c_prog_value_to_i32(args[2])));
    }
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_seq_grow_inst")) {
    DriverCProgArray *array = driver_c_prog_array_from_refish(args[0], 1);
    if (array != NULL) {
      int32_t min_cap = driver_c_prog_value_to_i32(args[1]);
      if (min_cap < 0) {
        min_cap = 0;
      }
      driver_c_prog_array_ensure_cap(array, min_cap);
      *out = driver_c_prog_value_nil();
    } else {
      driver_c_prog_seq_grow_inst_local(driver_c_prog_value_to_void_ptr(args[0]),
                                        driver_c_prog_value_to_i32(args[1]),
                                        driver_c_prog_value_to_i32(args[2]));
      *out = driver_c_prog_value_nil();
    }
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_seq_zero_tail_raw")) {
    DriverCProgArray *array = driver_c_prog_array_from_refish(args[0], 1);
    if (array == NULL) {
      array = driver_c_prog_array_from_buffer_ptr(driver_c_prog_value_to_void_ptr(args[0]));
    }
    if (array != NULL) {
      int32_t target = driver_c_prog_value_to_i32(args[3]);
      if (target < 0) {
        target = 0;
      }
      driver_c_prog_array_set_len(array, target);
      *out = driver_c_prog_value_nil();
    } else {
      driver_c_prog_seq_zero_tail_raw_local(driver_c_prog_value_to_void_ptr(args[0]),
                                            driver_c_prog_value_to_i32(args[1]),
                                            driver_c_prog_value_to_i32(args[2]),
                                            driver_c_prog_value_to_i32(args[3]),
                                            driver_c_prog_value_to_i32(args[4]));
      *out = driver_c_prog_value_nil();
    }
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "store")) {
    void *raw = driver_c_prog_value_to_void_ptr(args[0]);
    DriverCProgValue *slot = driver_c_prog_str_seq_slot_token_take(raw);
    if (slot != NULL) {
      driver_c_prog_assign_slot(slot, args[1]);
      *out = driver_c_prog_value_nil();
      return 1;
    }
    {
      DriverCProgValue value = driver_c_prog_materialize(args[1]);
      ChengStrBridge bridge = driver_c_prog_str_bridge_empty();
      if (value.kind == DRIVER_C_PROG_VALUE_STR) {
        bridge = value.as.str;
      } else {
        bridge = driver_c_owned_bridge_from_cstring(driver_c_prog_value_to_cstring_dup(value));
      }
      driver_c_prog_store_str_bridge_local(raw, bridge);
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "puts") || driver_c_prog_text_eq(symbol, "c_puts")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    int rc = puts(text != NULL ? text : "");
    free(text);
    *out = driver_c_prog_value_i32((int32_t)rc);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "exit")) {
    exit(driver_c_prog_value_to_i32(args[0]));
  }
  if (driver_c_prog_text_eq(symbol, "__cheng_rt_paramStrCopyBridgeInto")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[1]);
    if (slot == NULL) driver_c_die("driver_c program runtime: paramStrCopyInto out must be ref");
    driver_c_prog_assign_slot(slot, driver_c_prog_builtin_param_str(args[0]));
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "driver_c_read_file_all_bridge_into")) {
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[1]);
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    char *text = driver_c_read_file_all(path);
    if (slot == NULL) driver_c_die("driver_c program runtime: read_file_all_bridge_into out must be ref");
    driver_c_prog_assign_slot(slot, driver_c_prog_value_str_take(text != NULL ? text : driver_c_dup_cstring("")));
    free(path);
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_str_param_to_cstring_compat")) {
    char *text = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_ptr(text);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_cstrlen")) {
    const char *text = (const char *)driver_c_prog_value_to_void_ptr(args[0]);
    *out = driver_c_prog_value_i32(text != NULL ? (int32_t)strlen(text) : 0);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_file_exists")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_i32(cheng_file_exists(path));
    free(path);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_file_size")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_i64(cheng_file_size(path));
    free(path);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "remove")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_i32(remove(path));
    free(path);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "rmdir")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_i32(rmdir(path));
    free(path);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_dir_exists")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_i32(cheng_dir_exists(path));
    free(path);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_list_dir")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_ptr(cheng_list_dir(path));
    free(path);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_mkdir1")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    *out = driver_c_prog_value_i32(cheng_mkdir1(path));
    free(path);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_fopen")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    char *mode = driver_c_prog_value_to_cstring_dup(args[1]);
    *out = driver_c_prog_value_ptr(cheng_fopen(path, mode));
    free(path);
    free(mode);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "get_stdin")) {
    *out = driver_c_prog_value_ptr(get_stdin());
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "get_stdout")) {
    *out = driver_c_prog_value_ptr(get_stdout());
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "get_stderr")) {
    *out = driver_c_prog_value_ptr(get_stderr());
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_fclose")) {
    *out = driver_c_prog_value_i32(cheng_fclose(driver_c_prog_value_to_void_ptr(args[0])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_fread")) {
    *out = driver_c_prog_value_i32(cheng_fread(driver_c_prog_value_to_void_ptr(args[0]),
                                               driver_c_prog_value_to_i64(args[1]),
                                               driver_c_prog_value_to_i64(args[2]),
                                               driver_c_prog_value_to_void_ptr(args[3])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_fwrite")) {
    *out = driver_c_prog_value_i32(cheng_fwrite(driver_c_prog_value_to_void_ptr(args[0]),
                                                driver_c_prog_value_to_i64(args[1]),
                                                driver_c_prog_value_to_i64(args[2]),
                                                driver_c_prog_value_to_void_ptr(args[3])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_fseek")) {
    *out = driver_c_prog_value_i32(cheng_fseek(driver_c_prog_value_to_void_ptr(args[0]),
                                               driver_c_prog_value_to_i64(args[1]),
                                               driver_c_prog_value_to_i32(args[2])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_ftell")) {
    *out = driver_c_prog_value_i64(cheng_ftell(driver_c_prog_value_to_void_ptr(args[0])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_system_entropy_fill")) {
    *out = driver_c_prog_value_i32(cheng_system_entropy_fill(driver_c_prog_value_to_void_ptr(args[0]),
                                                             driver_c_prog_value_to_i32(args[1])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "fopen")) {
    char *path = driver_c_prog_value_to_cstring_dup(args[0]);
    char *mode = driver_c_prog_value_to_cstring_dup(args[1]);
    *out = driver_c_prog_value_ptr((void *)fopen(path, mode));
    free(path);
    free(mode);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "fclose")) {
    FILE *fp = (FILE *)driver_c_prog_value_to_void_ptr(args[0]);
    *out = driver_c_prog_value_i32(fp != NULL ? (int32_t)fclose(fp) : -1);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "fread")) {
    FILE *fp = (FILE *)driver_c_prog_value_to_void_ptr(args[3]);
    *out = driver_c_prog_value_i32((fp != NULL && driver_c_prog_value_to_void_ptr(args[0]) != NULL)
                                       ? (int32_t)fread(driver_c_prog_value_to_void_ptr(args[0]),
                                                        (size_t)driver_c_prog_value_to_i64(args[1]),
                                                        (size_t)driver_c_prog_value_to_i64(args[2]),
                                                        fp)
                                       : 0);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "fwrite")) {
    FILE *fp = (FILE *)driver_c_prog_value_to_void_ptr(args[3]);
    *out = driver_c_prog_value_i32((fp != NULL && driver_c_prog_value_to_void_ptr(args[0]) != NULL)
                                       ? (int32_t)fwrite(driver_c_prog_value_to_void_ptr(args[0]),
                                                         (size_t)driver_c_prog_value_to_i64(args[1]),
                                                         (size_t)driver_c_prog_value_to_i64(args[2]),
                                                         fp)
                                       : 0);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "fseek")) {
    FILE *fp = (FILE *)driver_c_prog_value_to_void_ptr(args[0]);
    *out = driver_c_prog_value_i32(fp != NULL
                                       ? (int32_t)fseeko(fp,
                                                         (off_t)driver_c_prog_value_to_i64(args[1]),
                                                         driver_c_prog_value_to_i32(args[2]))
                                       : -1);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "ftell")) {
    FILE *fp = (FILE *)driver_c_prog_value_to_void_ptr(args[0]);
    *out = driver_c_prog_value_i64(fp != NULL ? (int64_t)ftello(fp) : -1);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "close")) {
    *out = driver_c_prog_value_i32(close(driver_c_prog_value_to_i32(args[0])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "getenv")) {
    char *name = driver_c_prog_value_to_cstring_dup(args[0]);
    const char *raw = getenv(name != NULL ? name : "");
    free(name);
    *out = driver_c_prog_value_ptr((void *)raw);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_mem_scope_escape")) {
    (void)driver_c_prog_value_to_void_ptr(args[0]);
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "socket")) {
    *out = driver_c_prog_value_i32(socket(driver_c_prog_value_to_i32(args[0]),
                                          driver_c_prog_value_to_i32(args[1]),
                                          driver_c_prog_value_to_i32(args[2])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "bind")) {
    *out = driver_c_prog_value_i32(bind(driver_c_prog_value_to_i32(args[0]),
                                        (const struct sockaddr *)driver_c_prog_value_to_void_ptr(args[1]),
                                        (socklen_t)driver_c_prog_value_to_i32(args[2])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "fcntl")) {
    *out = driver_c_prog_value_i32(fcntl(driver_c_prog_value_to_i32(args[0]),
                                         driver_c_prog_value_to_i32(args[1]),
                                         driver_c_prog_value_to_i32(args[2])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "setsockopt")) {
    *out = driver_c_prog_value_i32(setsockopt(driver_c_prog_value_to_i32(args[0]),
                                              driver_c_prog_value_to_i32(args[1]),
                                              driver_c_prog_value_to_i32(args[2]),
                                              driver_c_prog_value_to_void_ptr(args[3]),
                                              (socklen_t)driver_c_prog_value_to_i32(args[4])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "sendto")) {
    *out = driver_c_prog_value_i32((int32_t)sendto(driver_c_prog_value_to_i32(args[0]),
                                                   driver_c_prog_value_to_void_ptr(args[1]),
                                                   (size_t)driver_c_prog_value_to_i32(args[2]),
                                                   driver_c_prog_value_to_i32(args[3]),
                                                   (const struct sockaddr *)driver_c_prog_value_to_void_ptr(args[4]),
                                                   (socklen_t)driver_c_prog_value_to_i32(args[5])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "recvfrom")) {
    socklen_t addr_len = 0;
    socklen_t *addr_len_ptr = (socklen_t *)driver_c_prog_value_to_void_ptr(args[5]);
    if (addr_len_ptr != NULL) {
      addr_len = *addr_len_ptr;
    }
    *out = driver_c_prog_value_i32((int32_t)recvfrom(driver_c_prog_value_to_i32(args[0]),
                                                     driver_c_prog_value_to_void_ptr(args[1]),
                                                     (size_t)driver_c_prog_value_to_i32(args[2]),
                                                     driver_c_prog_value_to_i32(args[3]),
                                                     (struct sockaddr *)driver_c_prog_value_to_void_ptr(args[4]),
                                                     addr_len_ptr != NULL ? &addr_len : NULL));
    if (addr_len_ptr != NULL) {
      *addr_len_ptr = addr_len;
    }
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_recvfrom_fd_ex")) {
    DriverCProgValue *addr_len_slot = driver_c_prog_slot_from_refish(args[5]);
    DriverCProgValue *slot = driver_c_prog_slot_from_refish(args[6]);
    void *addr_len_ptr = NULL;
    int32_t addr_len = 0;
    int32_t out_err = 0;
    if (slot == NULL) driver_c_die("driver_c program runtime: cheng_recvfrom_fd_ex outErr must be ref");
    if (addr_len_slot != NULL) {
      addr_len = driver_c_prog_value_to_i32(*addr_len_slot);
      addr_len_ptr = &addr_len;
    } else {
      addr_len_ptr = driver_c_prog_value_to_void_ptr(args[5]);
    }
    *out = driver_c_prog_value_i32(cheng_recvfrom_fd_ex(driver_c_prog_value_to_i32(args[0]),
                                                        driver_c_prog_value_to_void_ptr(args[1]),
                                                        driver_c_prog_value_to_i32(args[2]),
                                                        driver_c_prog_value_to_i32(args[3]),
                                                        driver_c_prog_value_to_void_ptr(args[4]),
                                                        addr_len_ptr,
                                                        &out_err));
    if (addr_len_slot != NULL) {
      driver_c_prog_assign_slot(addr_len_slot, driver_c_prog_value_i32(addr_len));
    }
    driver_c_prog_assign_slot(slot, driver_c_prog_value_i32(out_err));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "inet_pton")) {
    char *src = driver_c_prog_value_to_cstring_dup(args[1]);
    *out = driver_c_prog_value_i32(inet_pton(driver_c_prog_value_to_i32(args[0]),
                                             src,
                                             driver_c_prog_value_to_void_ptr(args[2])));
    free(src);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "inet_ntop")) {
    *out = driver_c_prog_value_ptr((void *)inet_ntop(driver_c_prog_value_to_i32(args[0]),
                                                     driver_c_prog_value_to_void_ptr(args[1]),
                                                     (char *)driver_c_prog_value_to_void_ptr(args[2]),
                                                     (socklen_t)driver_c_prog_value_to_i32(args[3])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "ptr_add")) {
    uint8_t *base = (uint8_t *)driver_c_prog_value_to_void_ptr(args[0]);
    int32_t off = driver_c_prog_value_to_i32(args[1]);
    *out = driver_c_prog_value_ptr(base != NULL ? (void *)(base + off) : NULL);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "copyMem")) {
    void *dst = driver_c_prog_value_to_void_ptr(args[0]);
    void *src = driver_c_prog_value_to_void_ptr(args[1]);
    int32_t n = driver_c_prog_value_to_i32(args[2]);
    if (dst != NULL && src != NULL && n > 0) {
      memcpy(dst, src, (size_t)n);
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "setMem")) {
    void *dst = driver_c_prog_value_to_void_ptr(args[0]);
    int32_t byte = driver_c_prog_value_to_i32(args[1]);
    int32_t n = driver_c_prog_value_to_i32(args[2]);
    if (dst != NULL && n > 0) {
      memset(dst, byte, (size_t)n);
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "rawmemAsVoid")) {
    *out = driver_c_prog_value_ptr(driver_c_prog_value_to_void_ptr(args[0]));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_rawbytes_get_at")) {
    *out = driver_c_prog_value_i32(
        cheng_rawbytes_get_at(driver_c_prog_value_to_void_ptr(args[0]),
                              driver_c_prog_value_to_i32(args[1])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_rawbytes_set_at")) {
    cheng_rawbytes_set_at(driver_c_prog_value_to_void_ptr(args[0]),
                          driver_c_prog_value_to_i32(args[1]),
                          driver_c_prog_value_to_i32(args[2]));
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(label, "k512")) {
    int32_t idx = driver_c_prog_value_to_i32(args[0]);
    if (idx < 0 || idx >= 80) {
      *out = driver_c_prog_value_u64(0u);
    } else {
      *out = driver_c_prog_value_u64((uint64_t)driver_c_sha512_k[idx]);
    }
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_rawmem_write_i8")) {
    int8_t *dst = (int8_t *)driver_c_prog_value_to_void_ptr(args[0]);
    int32_t idx = driver_c_prog_value_to_i32(args[1]);
    int32_t v = driver_c_prog_value_to_i32(args[2]);
    if (dst != NULL && idx >= 0) {
      dst[idx] = (int8_t)v;
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_rawmem_write_char")) {
    uint8_t *dst = (uint8_t *)driver_c_prog_value_to_void_ptr(args[0]);
    int32_t idx = driver_c_prog_value_to_i32(args[1]);
    int32_t v = driver_c_prog_value_to_i32(args[2]);
    if (dst != NULL && idx >= 0) {
      dst[idx] = (uint8_t)(v & 255);
    }
    *out = driver_c_prog_value_nil();
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_errno")) {
    *out = driver_c_prog_value_i32(errno);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_strerror")) {
    *out = driver_c_prog_value_ptr((void *)strerror(driver_c_prog_value_to_i32(args[0])));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "driver_c_new_string")) {
    int32_t n = driver_c_prog_value_to_i32(args[0]);
    int64_t bytes = n <= 0 ? 1 : (int64_t)n + 1;
    char *buf = (char *)malloc((size_t)bytes);
    if (buf == NULL) {
      *out = driver_c_prog_value_ptr(NULL);
      return 1;
    }
    memset(buf, 0, (size_t)bytes);
    *out = driver_c_prog_value_ptr(buf);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "driver_c_new_string_copy_n")) {
    void *raw = driver_c_prog_value_to_void_ptr(args[0]);
    int32_t n = driver_c_prog_value_to_i32(args[1]);
    int64_t bytes = n <= 0 ? 1 : (int64_t)n + 1;
    char *buf = (char *)malloc((size_t)bytes);
    if (buf == NULL) {
      *out = driver_c_prog_value_ptr(NULL);
      return 1;
    }
    if (raw != NULL && n > 0) {
      memcpy(buf, raw, (size_t)n);
    }
    buf[n > 0 ? n : 0] = '\0';
    *out = driver_c_prog_value_ptr(buf);
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_epoch_time")) {
    *out = driver_c_prog_value_f64(cheng_epoch_time());
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "cheng_getcwd")) {
    *out = driver_c_prog_value_ptr((void *)cheng_getcwd());
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "driver_c_i32_to_str")) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", driver_c_prog_value_to_i32(args[0]));
    *out = driver_c_prog_value_ptr(driver_c_dup_cstring(buf));
    return 1;
  }
  if (driver_c_prog_text_eq(symbol, "driver_c_i64_to_str")) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld", (long long)driver_c_prog_value_to_i64(args[0]));
    *out = driver_c_prog_value_ptr(driver_c_dup_cstring(buf));
    return 1;
  }
  return 0;
}

static DriverCProgRoutine driver_c_prog_routine_bind_type_arg(DriverCProgRegistry *registry,
                                                              DriverCProgRoutine base,
                                                              const char *type_text) {
  DriverCProgItem *item = NULL;
  const char **next = NULL;
  DriverCProgRoutine out = base;
  int32_t next_count = 0;
  int32_t i = 0;
  if (base.item_id < 0 || type_text == NULL || type_text[0] == '\0') {
    return base;
  }
  item = driver_c_prog_find_item_by_id(registry, base.item_id);
  if (item == NULL) {
    return base;
  }
  if (item->type_param_count <= 0) {
    return base;
  }
  if (base.type_arg_count >= item->type_param_count) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: too many type args label=%s owner=%s",
             item->label != NULL ? item->label : "",
             item->owner_module != NULL ? item->owner_module : "");
    driver_c_die(message);
  }
  next_count = base.type_arg_count + 1;
  next = (const char **)driver_c_prog_xcalloc((size_t)next_count, sizeof(char *));
  for (i = 0; i < base.type_arg_count; ++i) {
    next[i] = base.type_arg_texts[i];
  }
  next[next_count - 1] = type_text;
  out.type_arg_texts = driver_c_prog_intern_routine_type_args(registry, base.item_id, next, next_count);
  out.type_arg_count = next_count;
  free((void *)next);
  return out;
}

static const char *driver_c_prog_value_static_type_text(DriverCProgValue value) {
  DriverCProgValue materialized = driver_c_prog_materialize(value);
  if (materialized.kind == DRIVER_C_PROG_VALUE_BOOL) return "bool";
  if (materialized.kind == DRIVER_C_PROG_VALUE_I32) return "int32";
  if (materialized.kind == DRIVER_C_PROG_VALUE_I64) return "int64";
  if (materialized.kind == DRIVER_C_PROG_VALUE_U32) return "uint32";
  if (materialized.kind == DRIVER_C_PROG_VALUE_U64) return "uint64";
  if (materialized.kind == DRIVER_C_PROG_VALUE_F64) return "float64";
  if (materialized.kind == DRIVER_C_PROG_VALUE_STR) return "str";
  if (materialized.kind == DRIVER_C_PROG_VALUE_PTR) return "ptr";
  if (materialized.kind == DRIVER_C_PROG_VALUE_RECORD &&
      materialized.as.record != NULL &&
      materialized.as.record->type_text != NULL &&
      materialized.as.record->type_text[0] != '\0') {
    return materialized.as.record->type_text;
  }
  if (materialized.kind == DRIVER_C_PROG_VALUE_TYPE_TOKEN) return materialized.as.type_text;
  if (materialized.kind == DRIVER_C_PROG_VALUE_ARRAY &&
      materialized.as.array != NULL &&
      materialized.as.array->elem_type_text != NULL &&
      materialized.as.array->elem_type_text[0] != '\0') {
    return materialized.as.array->elem_type_text;
  }
  return NULL;
}

static char *driver_c_prog_infer_type_arg_from_type_text(const char *param_type_text,
                                                         const char *arg_type_text,
                                                         const char *type_param_name) {
  char *param_norm = NULL;
  char *arg_norm = NULL;
  char *param_fn_params_text = NULL;
  char *param_fn_return_text = NULL;
  char *arg_fn_params_text = NULL;
  char *arg_fn_return_text = NULL;
  char *param_suffix_base = NULL;
  char *param_suffix_arg = NULL;
  char *arg_suffix_base = NULL;
  char *arg_suffix_arg = NULL;
  char *param_ctor_base = NULL;
  char *param_ctor_args_text = NULL;
  char *arg_ctor_base = NULL;
  char *arg_ctor_args_text = NULL;
  char **param_args = NULL;
  char **arg_args = NULL;
  int32_t param_arg_count = 0;
  int32_t arg_arg_count = 0;
  char *resolved = NULL;
  int32_t i = 0;
  if (param_type_text == NULL || arg_type_text == NULL || type_param_name == NULL) {
    return NULL;
  }
  param_norm = driver_c_prog_normalize_param_type_text_dup(param_type_text);
  arg_norm = driver_c_prog_normalize_param_type_text_dup(arg_type_text);
  if (driver_c_prog_text_eq(param_norm, type_param_name)) {
    resolved = driver_c_dup_cstring(arg_norm);
    goto done;
  }
  if (driver_c_prog_type_text_split_fn(param_norm, &param_fn_params_text, &param_fn_return_text) &&
      driver_c_prog_type_text_split_fn(arg_norm, &arg_fn_params_text, &arg_fn_return_text)) {
    param_args = driver_c_prog_split_type_args_dup(param_fn_params_text, &param_arg_count);
    arg_args = driver_c_prog_split_type_args_dup(arg_fn_params_text, &arg_arg_count);
    if (param_arg_count == arg_arg_count) {
      for (i = 0; i < param_arg_count && resolved == NULL; ++i) {
        char *param_arg_type = driver_c_prog_function_param_type_text_dup(param_args[i]);
        char *arg_arg_type = driver_c_prog_function_param_type_text_dup(arg_args[i]);
        resolved = driver_c_prog_infer_type_arg_from_type_text(param_arg_type,
                                                               arg_arg_type,
                                                               type_param_name);
        free(param_arg_type);
        free(arg_arg_type);
      }
      if (resolved == NULL) {
        resolved = driver_c_prog_infer_type_arg_from_type_text(param_fn_return_text,
                                                               arg_fn_return_text,
                                                               type_param_name);
      }
    }
    goto done;
  }
  if (driver_c_prog_type_text_split_suffix_ctor(param_norm, &param_suffix_base, &param_suffix_arg) &&
      driver_c_prog_type_text_split_suffix_ctor(arg_norm, &arg_suffix_base, &arg_suffix_arg) &&
      driver_c_prog_text_eq(param_suffix_base, arg_suffix_base)) {
    resolved = driver_c_prog_infer_type_arg_from_type_text(param_suffix_arg, arg_suffix_arg, type_param_name);
    goto done;
  }
  if (driver_c_prog_type_text_split_ctor(param_norm, &param_ctor_base, &param_ctor_args_text) &&
      driver_c_prog_type_text_split_ctor(arg_norm, &arg_ctor_base, &arg_ctor_args_text) &&
      driver_c_prog_text_eq(param_ctor_base, arg_ctor_base)) {
    param_args = driver_c_prog_split_type_args_dup(param_ctor_args_text, &param_arg_count);
    arg_args = driver_c_prog_split_type_args_dup(arg_ctor_args_text, &arg_arg_count);
    if (param_arg_count == arg_arg_count) {
      for (i = 0; i < param_arg_count && resolved == NULL; ++i) {
        resolved = driver_c_prog_infer_type_arg_from_type_text(param_args[i], arg_args[i], type_param_name);
      }
    }
    goto done;
  }
done:
  free(param_norm);
  free(arg_norm);
  free(param_fn_params_text);
  free(param_fn_return_text);
  free(arg_fn_params_text);
  free(arg_fn_return_text);
  free(param_suffix_base);
  free(param_suffix_arg);
  free(arg_suffix_base);
  free(arg_suffix_arg);
  free(param_ctor_base);
  free(param_ctor_args_text);
  free(arg_ctor_base);
  free(arg_ctor_args_text);
  driver_c_prog_free_string_array(param_args, param_arg_count);
  driver_c_prog_free_string_array(arg_args, arg_arg_count);
  return resolved;
}

static char *driver_c_prog_infer_type_arg_from_param_text(DriverCProgRegistry *registry,
                                                          const char *owner_module,
                                                          const char *param_type_text,
                                                          DriverCProgValue arg_value,
                                                          const char *type_param_name);

static char *driver_c_prog_infer_type_arg_from_record_fields(DriverCProgRegistry *registry,
                                                             const char *owner_module,
                                                             const char *param_type_text,
                                                             DriverCProgValue arg_value,
                                                             const char *type_param_name) {
  DriverCProgValue materialized = driver_c_prog_materialize(arg_value);
  char *param_norm = NULL;
  char *ctor_base = NULL;
  char *ctor_args_text = NULL;
  char **param_args = NULL;
  int32_t param_arg_count = 0;
  DriverCProgTypeDecl *decl = NULL;
  char *resolved = NULL;
  int32_t i = 0;
  if (registry == NULL || owner_module == NULL || type_param_name == NULL) {
    return NULL;
  }
  if (materialized.kind != DRIVER_C_PROG_VALUE_RECORD || materialized.as.record == NULL) {
    return NULL;
  }
  param_norm = driver_c_prog_normalize_param_type_text_dup(param_type_text);
  if (driver_c_prog_type_text_split_ctor(param_norm, &ctor_base, &ctor_args_text)) {
    param_args = driver_c_prog_split_type_args_dup(ctor_args_text, &param_arg_count);
    decl = driver_c_prog_resolve_visible_type_decl_in_scopes(registry,
                                                             owner_module,
                                                             owner_module,
                                                             ctor_base);
  } else {
    decl = driver_c_prog_resolve_visible_type_decl_in_scopes(registry,
                                                             owner_module,
                                                             owner_module,
                                                             param_norm);
  }
  if (decl != NULL) {
    if (decl->param_count != param_arg_count) {
      driver_c_die("driver_c program runtime: generic record inference arity mismatch");
    }
    for (i = 0; i < decl->field_count && resolved == NULL; ++i) {
      DriverCProgValue *slot =
          driver_c_prog_record_slot_at(materialized.as.record, i, decl->field_names[i]);
      char *field_type = driver_c_dup_cstring(decl->field_types[i]);
      int32_t j = 0;
      for (j = 0; j < decl->param_count; ++j) {
        char *next = driver_c_prog_substitute_type_token_dup(field_type, decl->param_names[j], param_args[j]);
        free(field_type);
        field_type = next;
      }
      if (slot != NULL) {
        resolved = driver_c_prog_infer_type_arg_from_param_text(registry,
                                                                owner_module,
                                                                field_type,
                                                                *slot,
                                                                type_param_name);
      }
      free(field_type);
    }
  }
  free(param_norm);
  free(ctor_base);
  free(ctor_args_text);
  driver_c_prog_free_string_array(param_args, param_arg_count);
  return resolved;
}

static char *driver_c_prog_infer_type_arg_from_param_text(DriverCProgRegistry *registry,
                                                          const char *owner_module,
                                                          const char *param_type_text,
                                                          DriverCProgValue arg_value,
                                                          const char *type_param_name) {
  DriverCProgValue materialized = driver_c_prog_materialize(arg_value);
  const char *arg_type_text = NULL;
  char *resolved = NULL;
  if (materialized.kind == DRIVER_C_PROG_VALUE_ARRAY &&
      materialized.as.array != NULL &&
      materialized.as.array->elem_type_text != NULL &&
      materialized.as.array->elem_type_text[0] != '\0') {
    char *array_type = (char *)driver_c_prog_xcalloc(strlen(materialized.as.array->elem_type_text) + 3u, 1u);
    memcpy(array_type, materialized.as.array->elem_type_text, strlen(materialized.as.array->elem_type_text));
    memcpy(array_type + strlen(materialized.as.array->elem_type_text), "[]", 3u);
    resolved = driver_c_prog_infer_type_arg_from_type_text(param_type_text, array_type, type_param_name);
    free(array_type);
    if (resolved != NULL && resolved[0] != '\0') {
      return resolved;
    }
    free(resolved);
  }
  arg_type_text = driver_c_prog_value_static_type_text(materialized);
  resolved = driver_c_prog_infer_type_arg_from_type_text(param_type_text, arg_type_text, type_param_name);
  if (resolved != NULL && resolved[0] != '\0') {
    return resolved;
  }
  free(resolved);
  return driver_c_prog_infer_type_arg_from_record_fields(registry,
                                                         owner_module,
                                                         param_type_text,
                                                         materialized,
                                                         type_param_name);
}

static DriverCProgRoutine driver_c_prog_routine_infer_type_args_from_call(DriverCProgRegistry *registry,
                                                                          DriverCProgRoutine base,
                                                                          DriverCProgValue *args,
                                                                          int32_t argc) {
  DriverCProgItem *callee_item = NULL;
  DriverCProgRoutine out = base;
  int32_t i = 0;
  if (registry == NULL || base.item_id < 0) {
    return base;
  }
  callee_item = driver_c_prog_find_item_by_id(registry, base.item_id);
  if (callee_item == NULL ||
      callee_item->type_param_count <= 0 ||
      callee_item->param_type_texts == NULL ||
      base.type_arg_count >= callee_item->type_param_count) {
    return base;
  }
  for (i = base.type_arg_count; i < callee_item->type_param_count; ++i) {
    const char *param_name = callee_item->type_param_names[i];
    char *resolved = NULL;
    int32_t arg_idx = 0;
    for (arg_idx = 0; arg_idx < argc && resolved == NULL; ++arg_idx) {
      if (arg_idx >= callee_item->param_count) {
        break;
      }
      resolved = driver_c_prog_infer_type_arg_from_param_text(registry,
                                                              callee_item->owner_module,
                                                              callee_item->param_type_texts[arg_idx],
                                                              args[arg_idx],
                                                              param_name);
    }
    if (resolved == NULL || resolved[0] == '\0') {
      free(resolved);
      return out;
    }
    out = driver_c_prog_routine_bind_type_arg(registry, out, resolved);
    free(resolved);
  }
  return out;
}

static DriverCProgRoutine driver_c_prog_routine_bind_visible_type_args(DriverCProgRegistry *registry,
                                                                       const DriverCProgFrame *frame,
                                                                       DriverCProgRoutine base) {
  DriverCProgItem *callee_item = NULL;
  DriverCProgItem *owner_item = NULL;
  DriverCProgRoutine out = base;
  int32_t i = 0;
  if (registry == NULL || frame == NULL || frame->item == NULL || base.item_id < 0) {
    return base;
  }
  callee_item = driver_c_prog_find_item_by_id(registry, base.item_id);
  owner_item = frame->item;
  if (callee_item == NULL ||
      callee_item->type_param_count <= 0 ||
      base.type_arg_count >= callee_item->type_param_count) {
    return base;
  }
  for (i = base.type_arg_count; i < callee_item->type_param_count; ++i) {
    const char *param_name = callee_item->type_param_names[i];
    int32_t owner_param_idx = driver_c_prog_find_type_param_index(owner_item, param_name);
    if (owner_param_idx < 0 ||
        owner_param_idx >= frame->type_arg_count ||
        frame->type_arg_texts == NULL ||
        frame->type_arg_texts[owner_param_idx] == NULL ||
        frame->type_arg_texts[owner_param_idx][0] == '\0') {
      return out;
    }
    out = driver_c_prog_routine_bind_type_arg(registry, out, frame->type_arg_texts[owner_param_idx]);
  }
  return out;
}

static char *driver_c_prog_resolve_routine_return_type_dup(DriverCProgRegistry *registry,
                                                           DriverCProgRoutine routine) {
  DriverCProgItem *item = NULL;
  char *resolved = NULL;
  int32_t i = 0;
  if (registry == NULL || routine.item_id < 0) {
    return NULL;
  }
  item = driver_c_prog_find_item_by_id(registry, routine.item_id);
  if (item == NULL || item->return_type == NULL || item->return_type[0] == '\0') {
    return NULL;
  }
  resolved = driver_c_prog_normalize_param_type_text_dup(item->return_type);
  for (i = 0; i < item->type_param_count && i < routine.type_arg_count; ++i) {
    if (item->type_param_names == NULL ||
        item->type_param_names[i] == NULL ||
        item->type_param_names[i][0] == '\0' ||
        routine.type_arg_texts == NULL ||
        routine.type_arg_texts[i] == NULL ||
        routine.type_arg_texts[i][0] == '\0') {
      continue;
    }
    {
      char *next = driver_c_prog_substitute_type_token_dup(resolved,
                                                           item->type_param_names[i],
                                                           routine.type_arg_texts[i]);
      free(resolved);
      resolved = next;
    }
  }
  return resolved;
}

static char *driver_c_prog_expected_call_result_type_dup(DriverCProgRegistry *registry,
                                                         const DriverCProgFrame *frame) {
  const DriverCProgOp *next = NULL;
  DriverCProgItem *target = NULL;
  if (registry == NULL || frame == NULL || frame->item == NULL) {
    return NULL;
  }
  if (frame->current_pc < 0 || frame->current_pc + 1 >= frame->item->exec_op_count) {
    return NULL;
  }
  next = &frame->item->ops[frame->current_pc + 1];
  if (next->kind_tag == DRIVER_C_PROG_OP_RETURN_VALUE) {
    return driver_c_prog_resolve_frame_type_text_dup(frame, frame->item->return_type);
  }
  if (next->kind_tag == DRIVER_C_PROG_OP_STORE_LOCAL &&
      frame->item->local_type_texts != NULL &&
      next->arg0 >= 0 &&
      next->arg0 < frame->item->local_count &&
      frame->item->local_type_texts[next->arg0] != NULL &&
      frame->item->local_type_texts[next->arg0][0] != '\0') {
    return driver_c_prog_resolve_frame_type_text_dup(frame,
                                                     frame->item->local_type_texts[next->arg0]);
  }
  if (next->kind_tag == DRIVER_C_PROG_OP_STORE_PARAM &&
      frame->item->param_type_texts != NULL &&
      next->arg0 >= 0 &&
      next->arg0 < frame->item->param_count &&
      frame->item->param_type_texts[next->arg0] != NULL &&
      frame->item->param_type_texts[next->arg0][0] != '\0') {
    return driver_c_prog_resolve_frame_type_text_dup(frame,
                                                     frame->item->param_type_texts[next->arg0]);
  }
  if (next->kind_tag == DRIVER_C_PROG_OP_STORE_NAME) {
    target = driver_c_prog_resolve_visible_item(registry,
                                                frame->item->owner_module,
                                                next->primary);
    if (target != NULL && target->return_type != NULL && target->return_type[0] != '\0') {
      return driver_c_prog_normalize_param_type_text_dup(target->return_type);
    }
  }
  return NULL;
}

static DriverCProgRoutine driver_c_prog_routine_bind_expected_type_args(DriverCProgRegistry *registry,
                                                                        const DriverCProgFrame *frame,
                                                                        DriverCProgRoutine base) {
  DriverCProgItem *callee_item = NULL;
  DriverCProgRoutine out = base;
  char *expected_type = NULL;
  int32_t i = 0;
  if (registry == NULL || frame == NULL || base.item_id < 0) {
    return base;
  }
  callee_item = driver_c_prog_find_item_by_id(registry, base.item_id);
  if (callee_item == NULL ||
      callee_item->type_param_count <= 0 ||
      base.type_arg_count >= callee_item->type_param_count) {
    return base;
  }
  expected_type = driver_c_prog_expected_call_result_type_dup(registry, frame);
  if (expected_type == NULL || expected_type[0] == '\0') {
    free(expected_type);
    return base;
  }
  for (i = out.type_arg_count; i < callee_item->type_param_count; ++i) {
    char *resolved_return = driver_c_prog_resolve_routine_return_type_dup(registry, out);
    char *resolved = NULL;
    if (resolved_return != NULL && resolved_return[0] != '\0') {
      resolved = driver_c_prog_infer_type_arg_from_type_text(resolved_return,
                                                             expected_type,
                                                             callee_item->type_param_names[i]);
    }
    free(resolved_return);
    if (resolved == NULL || resolved[0] == '\0') {
      free(resolved);
      free(expected_type);
      return out;
    }
    out = driver_c_prog_routine_bind_type_arg(registry, out, resolved);
    free(resolved);
  }
  free(expected_type);
  return out;
}

static int driver_c_prog_routine_has_unbound_type_params(DriverCProgRegistry *registry,
                                                         DriverCProgRoutine routine) {
  DriverCProgItem *item = NULL;
  if (registry == NULL || routine.item_id < 0) {
    return 0;
  }
  item = driver_c_prog_find_item_by_id(registry, routine.item_id);
  if (item == NULL || item->type_param_count <= 0) {
    return 0;
  }
  return routine.type_arg_count < item->type_param_count;
}

static char *driver_c_prog_make_record_type_dup(DriverCProgRegistry *registry,
                                                const DriverCProgFrame *frame,
                                                const DriverCProgItem *item,
                                                const char *type_text) {
  char *resolved = NULL;
  char *normalized = NULL;
  char *base = NULL;
  char *args_text = NULL;
  char *expected_type = NULL;
  char *expected_norm = NULL;
  char *expected_base = NULL;
  char *expected_args_text = NULL;
  DriverCProgTypeDecl *decl = NULL;
  const char *owner_module = item != NULL && item->owner_module != NULL ? item->owner_module : "";
  resolved = driver_c_prog_resolve_frame_type_text_dup(frame, type_text);
  normalized = driver_c_prog_normalize_visible_type_text_dup(registry, owner_module, resolved, 0);
  free(resolved);
  resolved = NULL;
  if (normalized == NULL || normalized[0] == '\0') {
    free(normalized);
    return driver_c_dup_cstring("");
  }
  if (driver_c_prog_type_text_split_ctor(normalized, &base, &args_text) &&
      args_text != NULL &&
      args_text[0] != '\0') {
    free(base);
    free(args_text);
    return normalized;
  }
  decl = driver_c_prog_resolve_visible_type_decl_in_scopes(registry,
                                                           owner_module,
                                                           owner_module,
                                                           normalized);
  if (decl == NULL || decl->param_count <= 0) {
    free(base);
    free(args_text);
    return normalized;
  }
  expected_type = driver_c_prog_expected_call_result_type_dup(registry, frame);
  if (expected_type == NULL || expected_type[0] == '\0') {
    free(base);
    free(args_text);
    free(expected_type);
    return normalized;
  }
  expected_norm = driver_c_prog_normalize_visible_type_text_dup(registry, owner_module, expected_type, 0);
  free(expected_type);
  expected_type = NULL;
  if (expected_norm == NULL || expected_norm[0] == '\0') {
    free(base);
    free(args_text);
    free(expected_norm);
    return normalized;
  }
  if (!driver_c_prog_type_text_split_ctor(expected_norm, &expected_base, &expected_args_text) ||
      expected_args_text == NULL ||
      expected_args_text[0] == '\0') {
    free(base);
    free(args_text);
    free(expected_base);
    free(expected_args_text);
    free(expected_norm);
    return normalized;
  }
  if (driver_c_prog_text_eq(expected_base, normalized) ||
      (base != NULL && base[0] != '\0' && driver_c_prog_text_eq(expected_base, base))) {
    free(base);
    free(args_text);
    free(expected_base);
    free(expected_args_text);
    free(normalized);
    return expected_norm;
  }
  free(base);
  free(args_text);
  free(expected_base);
  free(expected_args_text);
  free(expected_norm);
  return normalized;
}

static char **driver_c_prog_op_secondary_lines(DriverCProgOp *op, int32_t *out_count) {
  int32_t count = 0;
  const char *cursor = NULL;
  char **out = NULL;
  int32_t i = 0;
  if (out_count != NULL) {
    *out_count = 0;
  }
  if (op == NULL) {
    return NULL;
  }
  if (op->secondary_lines != NULL) {
    if (out_count != NULL) {
      *out_count = op->secondary_line_count;
    }
    return op->secondary_lines;
  }
  cursor = op->secondary != NULL ? op->secondary : "";
  if (cursor[0] != '\0') {
    count = 1;
    while (*cursor != '\0') {
      if (*cursor == '\n') {
        count = count + 1;
      }
      cursor = cursor + 1;
    }
  }
  if (count > 0) {
    cursor = op->secondary;
    out = (char **)driver_c_prog_xcalloc((size_t)count, sizeof(char *));
    for (i = 0; i < count; ++i) {
      const char *line_start = cursor;
      const char *line_stop = cursor;
      while (*line_stop != '\0' && *line_stop != '\n') {
        line_stop = line_stop + 1;
      }
      out[i] = driver_c_dup_range(line_start, line_stop);
      cursor = *line_stop == '\0' ? line_stop : line_stop + 1;
    }
  }
  op->secondary_lines = out;
  op->secondary_line_count = count;
  if (out_count != NULL) {
    *out_count = count;
  }
  return op->secondary_lines;
}

static DriverCProgTypeDecl *driver_c_prog_make_record_decl_cached(DriverCProgRegistry *registry,
                                                                  const DriverCProgItem *item,
                                                                  DriverCProgOp *op,
                                                                  const char *record_type) {
  const char *owner_module = item != NULL && item->owner_module != NULL ? item->owner_module : "";
  if (op == NULL || record_type == NULL || record_type[0] == '\0') {
    return NULL;
  }
  if (op->cached_record_type != NULL &&
      driver_c_prog_text_eq(op->cached_record_type, record_type)) {
    return op->cached_record_decl;
  }
  free(op->cached_record_type);
  op->cached_record_type = driver_c_dup_cstring(record_type);
  op->cached_record_decl = driver_c_prog_resolve_visible_type_decl_in_scopes(registry,
                                                                              owner_module,
                                                                              owner_module,
                                                                              record_type);
  return op->cached_record_decl;
}

static DriverCProgValue driver_c_prog_value_routine_from_specialization(DriverCProgRoutine routine) {
  DriverCProgValue out = driver_c_prog_value_nil();
  out.kind = DRIVER_C_PROG_VALUE_ROUTINE;
  out.as.routine = routine;
  return out;
}

static DriverCProgValue driver_c_prog_execute_call(DriverCProgRegistry *registry,
                                                   DriverCProgRoutine routine,
                                                   DriverCProgValue *args,
                                                   int32_t argc) {
  DriverCProgItem *item = driver_c_prog_find_item_by_id(registry, routine.item_id);
  DriverCProgValue out = driver_c_prog_value_nil();
  const char *label = item != NULL ? item->label : routine.label;
  const char *owner_module = item != NULL ? item->owner_module : routine.owner_module;
  const char *symbol = item != NULL ? item->importc_symbol : routine.importc_symbol;
  int32_t builtin_tag = item != NULL ? driver_c_prog_builtin_tag_for_callable_item(item) : routine.builtin_tag;
  int routine_is_builtin = routine.top_level_tag == DRIVER_C_PROG_TOP_BUILTIN;
  if (!routine_is_builtin && label != NULL && owner_module != NULL) {
    if (item == NULL || (driver_c_prog_is_routine_item(item) && item->param_count != argc)) {
      DriverCProgItem *resolved = driver_c_prog_find_routine_item_in_module_by_arity(registry,
                                                                                      owner_module,
                                                                                      label,
                                                                                      argc);
      if (resolved != NULL) {
        item = resolved;
        label = item->label;
        owner_module = item->owner_module;
        symbol = item->importc_symbol;
        builtin_tag = driver_c_prog_builtin_tag_for_callable_item(item);
      }
    }
  }
  if (builtin_tag != DRIVER_C_PROG_BUILTIN_NONE) {
    if (driver_c_prog_try_builtin(registry, builtin_tag, label, "", args, argc, &out)) {
      return out;
    }
  }
  if (symbol != NULL && symbol[0] != '\0') {
    if (driver_c_prog_try_builtin(registry, DRIVER_C_PROG_BUILTIN_NONE, label, symbol, args, argc, &out)) {
      return out;
    }
  }
  if (item == NULL) {
    char message[256];
    snprintf(message, sizeof(message), "driver_c program runtime: missing callee item_id=%d label=%s",
             routine.item_id, routine.label != NULL ? routine.label : "");
    driver_c_die(message);
  }
  if (item->exec_op_count > 0) {
    DriverCProgValue out_value = driver_c_prog_eval_item(registry, item, args, argc, &routine);
    char *resolved_return = driver_c_prog_resolve_routine_return_type_dup(registry, routine);
    if (resolved_return != NULL && resolved_return[0] != '\0') {
      out_value = driver_c_prog_refine_value_to_declared_type(registry,
                                                              owner_module,
                                                              resolved_return,
                                                              out_value);
    }
    free(resolved_return);
    return out_value;
  }
  {
    char message[512];
    snprintf(message, sizeof(message),
             "driver_c program runtime: unsupported callee label=%s importc=%s top=%s",
             item->label != NULL ? item->label : "",
             item->importc_symbol != NULL ? item->importc_symbol : "",
             item->top_level_kind != NULL ? item->top_level_kind : "");
    driver_c_die(message);
  }
  return driver_c_prog_value_nil();
}

static DriverCProgValue driver_c_prog_resolve_name_value(DriverCProgRegistry *registry,
                                                         DriverCProgFrame *frame,
                                                         DriverCProgItem *owner_item,
                                                         const char *name,
                                                         const char *target_kind) {
  DriverCProgItem *import_item = NULL;
  DriverCProgItem *item = NULL;
  if (driver_c_prog_text_eq(target_kind, "type_param")) {
    int32_t param_idx = driver_c_prog_find_type_param_index(owner_item, name);
    if (param_idx >= 0) {
      if (frame != NULL &&
          frame->type_arg_texts != NULL &&
          param_idx < frame->type_arg_count &&
          frame->type_arg_texts[param_idx] != NULL &&
          frame->type_arg_texts[param_idx][0] != '\0') {
        return driver_c_prog_value_type_token(frame->type_arg_texts[param_idx]);
      }
      {
        char message[512];
        snprintf(message,
                 sizeof(message),
                 "driver_c program runtime: missing type arg binding label=%s owner=%s param=%s bound_count=%d",
                 owner_item != NULL && owner_item->label != NULL ? owner_item->label : "",
                 owner_item != NULL && owner_item->owner_module != NULL ? owner_item->owner_module : "",
                 name != NULL ? name : "",
                 frame != NULL ? frame->type_arg_count : -1);
        driver_c_die(message);
      }
    }
    return driver_c_prog_value_type_token(name);
  }
  item = driver_c_prog_resolve_visible_item(registry,
                                            owner_item != NULL ? owner_item->owner_module : "",
                                            name);
  if (item != NULL) {
    if (driver_c_prog_is_builtin_type_ctor_label(item->label)) {
      return driver_c_prog_value_type_token(item->label);
    }
    switch (item->top_level_tag) {
      case DRIVER_C_PROG_TOP_TYPE:
        return driver_c_prog_value_type_token(item->label);
      case DRIVER_C_PROG_TOP_VAR:
      case DRIVER_C_PROG_TOP_CONST:
      if (item->item_id < 0 || item->item_id >= registry->items_by_id_len) {
        driver_c_die("driver_c program runtime: load_name global var out of range");
      }
      (void)driver_c_prog_eval_global(registry, item->item_id);
      return driver_c_prog_value_ref(&registry->global_values[item->item_id]);
      case DRIVER_C_PROG_TOP_FN:
      case DRIVER_C_PROG_TOP_ITERATOR:
      case DRIVER_C_PROG_TOP_IMPORTC_FN:
        return driver_c_prog_value_routine(item);
      case DRIVER_C_PROG_TOP_IMPORT:
        return driver_c_prog_value_module(item->item_id,
                                          item->import_alias,
                                          driver_c_prog_import_target_module_text(item));
      default:
        return driver_c_prog_eval_global(registry, item->item_id);
    }
  }
  import_item = driver_c_prog_find_import_item_by_visible_name(registry,
                                                               owner_item != NULL ? owner_item->owner_module : "",
                                                               name);
  if (import_item != NULL) {
    return driver_c_prog_value_module(import_item->item_id,
                                      import_item->import_alias,
                                      driver_c_prog_import_target_module_text(import_item));
  }
  if (driver_c_prog_is_builtin_type_ctor_label(name)) {
    return driver_c_prog_value_type_token(name);
  }
  if (driver_c_prog_is_builtin_label(name)) {
    return driver_c_prog_value_builtin_routine(name,
                                               owner_item != NULL ? owner_item->owner_module : "");
  }
  {
    char message[512];
    snprintf(message, sizeof(message),
             "driver_c program runtime: unresolved name label=%s owner=%s",
             name != NULL ? name : "",
             owner_item != NULL && owner_item->owner_module != NULL ? owner_item->owner_module : "");
    driver_c_die(message);
  }
  return driver_c_prog_value_nil();
}

static DriverCProgValue driver_c_prog_resolve_module_field_value(DriverCProgRegistry *registry,
                                                                 DriverCProgValue module_value,
                                                                 const char *field_name) {
  DriverCProgValue materialized = driver_c_prog_materialize(module_value);
  DriverCProgItem *item = NULL;
  const char *target_module = NULL;
  if (materialized.kind != DRIVER_C_PROG_VALUE_MODULE) {
    driver_c_die("driver_c program runtime: module field access on non-module");
  }
  target_module = materialized.as.module.target_module != NULL ? materialized.as.module.target_module : "";
  if (driver_c_prog_is_builtin_module_field(target_module, field_name)) {
    return driver_c_prog_value_builtin_routine(field_name, target_module);
  }
  item = driver_c_prog_resolve_visible_item(registry, target_module, field_name);
  if (item == NULL) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c program runtime: unresolved module field module=%s field=%s",
             target_module,
             field_name != NULL ? field_name : "");
    driver_c_die(message);
  }
  switch (item->top_level_tag) {
    case DRIVER_C_PROG_TOP_TYPE:
      return driver_c_prog_value_type_token(item->label);
    case DRIVER_C_PROG_TOP_FN:
    case DRIVER_C_PROG_TOP_ITERATOR:
    case DRIVER_C_PROG_TOP_IMPORTC_FN:
      return driver_c_prog_value_routine(item);
    case DRIVER_C_PROG_TOP_IMPORT:
      return driver_c_prog_value_module(item->item_id,
                                        item->import_alias,
                                        driver_c_prog_import_target_module_text(item));
    default:
      return driver_c_prog_eval_global(registry, item->item_id);
  }
}

static DriverCProgValue driver_c_prog_eval_item(DriverCProgRegistry *registry,
                                                DriverCProgItem *item,
                                                DriverCProgValue *args,
                                                int32_t argc,
                                                const DriverCProgRoutine *routine_ctx) {
  DriverCProgFrame frame;
  int32_t pc = 0;
  int trace_enabled = driver_c_prog_trace_any_enabled();
  DriverCProgValue return_value = driver_c_prog_value_nil();
  int has_return_value = 0;
  driver_c_prog_frame_init(&frame, registry, item, args, argc, routine_ctx);
  while (pc < item->exec_op_count) {
    DriverCProgOp *op = &item->ops[pc];
    frame.current_pc = pc;
    frame.current_op = op;
    frame.current_op_kind = op->kind;
    frame.current_op_primary = op->primary;
    frame.current_op_secondary = op->secondary;
    if (trace_enabled) {
      driver_c_prog_trace_op(&frame, "before");
    }
    if (op->kind_tag == DRIVER_C_PROG_OP_LOCAL_DECL) {
      if (op->arg0 >= 0 && op->arg0 < item->local_count) {
        DriverCProgZeroPlan *local_zero_plan =
            frame.local_zero_plans != NULL ? frame.local_zero_plans[op->arg0] : NULL;
        if (local_zero_plan != NULL) {
          frame.locals[op->arg0] = driver_c_prog_value_zero_plan(local_zero_plan);
        } else {
          frame.locals[op->arg0] = driver_c_prog_value_nil();
        }
        frame.local_names[op->arg0] = op->primary;
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_BOOL) {
      driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_text_eq(op->primary, "true")));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_NIL) {
      driver_c_prog_stack_push(&frame, driver_c_prog_value_nil());
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_STR) {
      char *text = driver_c_prog_parse_string_literal_dup(op->primary);
      if (text == NULL) {
        driver_c_die("driver_c program runtime: malformed load_str literal");
      }
      driver_c_prog_stack_push(&frame, driver_c_prog_value_str_take(text));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_INT32) {
      driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_parse_i32_or_zero(op->primary)));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_INT64) {
      driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_parse_i64_or_zero(op->primary)));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_FLOAT64) {
      driver_c_prog_stack_push(&frame, driver_c_prog_value_f64(strtod(op->primary, NULL)));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_PARAM) {
      if (op->arg0 >= 0 && op->arg0 < item->param_count) {
        if (item->param_is_var != NULL && item->param_is_var[op->arg0] != 0) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(&frame.params[op->arg0]));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_materialize_slot(&frame.params[op->arg0]));
        }
      } else {
        driver_c_prog_stack_push(&frame, driver_c_prog_value_nil());
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_LOCAL) {
      if (op->arg0 >= 0 && op->arg0 < item->local_count) {
        if (frame.local_is_var != NULL && frame.local_is_var[op->arg0] != 0) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(&frame.locals[op->arg0]));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_materialize_slot(&frame.locals[op->arg0]));
        }
      } else {
        driver_c_prog_stack_push(&frame, driver_c_prog_value_nil());
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_ADDR_OF_PARAM) {
      if (op->arg0 < 0 || op->arg0 >= item->param_count) driver_c_die("driver_c program runtime: addr_of_param slot");
      driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(&frame.params[op->arg0]));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_ADDR_OF_LOCAL) {
      if (op->arg0 < 0 || op->arg0 >= item->local_count) driver_c_die("driver_c program runtime: addr_of_local slot");
      driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(&frame.locals[op->arg0]));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_ADDR_OF_NAME) {
      DriverCProgItem *target = driver_c_prog_resolve_visible_item(registry, item->owner_module, op->primary);
      if (target == NULL) driver_c_die("driver_c program runtime: addr_of_name unresolved");
      if (target->item_id < 0 || target->item_id >= registry->items_by_id_len) {
        driver_c_die("driver_c program runtime: addr_of_name target out of range");
      }
      if (target->top_level_tag == DRIVER_C_PROG_TOP_VAR) {
        (void)driver_c_prog_eval_global(registry, target->item_id);
        driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(&registry->global_values[target->item_id]));
      } else {
        driver_c_die("driver_c program runtime: addr_of_name on non-var");
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_ADDR_OF_FIELD) {
      DriverCProgValue base = driver_c_prog_stack_pop(&frame);
      DriverCProgValue *slot =
          driver_c_prog_ref_record_field_for_store(base, op->primary, op->arg2, item->label, op->kind);
      driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(slot));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_ADDR_OF_INDEX) {
      DriverCProgValue index_value = driver_c_prog_stack_pop(&frame);
      DriverCProgValue base = driver_c_prog_stack_pop(&frame);
      DriverCProgValue *slot = driver_c_prog_ref_array_index(base,
                                                             driver_c_prog_value_to_i32(index_value),
                                                             item->label);
      driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(slot));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_IMPORT) {
      DriverCProgItem *import_item = driver_c_prog_find_item_by_id(registry, op->arg0);
      if (import_item == NULL) driver_c_die("driver_c program runtime: import item missing");
      driver_c_prog_stack_push(&frame,
                               driver_c_prog_value_module(import_item->item_id,
                                                          import_item->import_alias,
                                                          driver_c_prog_import_target_module_text(import_item)));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_ROUTINE) {
      DriverCProgItem *callee = driver_c_prog_find_item_by_id(registry, op->arg0);
      if (callee == NULL) driver_c_die("driver_c program runtime: routine item missing");
      driver_c_prog_stack_push(&frame, driver_c_prog_value_routine(callee));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_GLOBAL) {
      DriverCProgItem *global_item = driver_c_prog_find_item_by_id(registry, op->arg0);
      if (global_item != NULL && global_item->top_level_tag == DRIVER_C_PROG_TOP_VAR) {
        (void)driver_c_prog_eval_global(registry, op->arg0);
        driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(&registry->global_values[op->arg0]));
      } else {
        driver_c_prog_stack_push(&frame, driver_c_prog_eval_global(registry, op->arg0));
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_NAME) {
      driver_c_prog_stack_push(&frame, driver_c_prog_resolve_name_value(registry, &frame, item, op->primary, op->secondary));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_FIELD) {
      DriverCProgValue base = driver_c_prog_stack_pop(&frame);
      DriverCProgValue materialized = driver_c_prog_materialize(base);
      if (materialized.kind == DRIVER_C_PROG_VALUE_MODULE) {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_resolve_module_field_value(registry, materialized, op->primary));
      } else if (driver_c_prog_text_eq(op->primary, "data") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_STR) {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_ptr((void *)materialized.as.str.ptr));
      } else if (driver_c_prog_text_eq(op->primary, "data") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_BYTES) {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_ptr((void *)materialized.as.bytes.data));
      } else if (driver_c_prog_text_eq(op->primary, "store_id") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_STR) {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_i32(materialized.as.str.store_id));
      } else if (driver_c_prog_text_eq(op->primary, "flags") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_STR) {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_i32(materialized.as.str.flags));
      } else if (driver_c_prog_text_eq(op->primary, "len") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_ARRAY) {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_i32(materialized.as.array != NULL
                                                             ? materialized.as.array->len
                                                             : 0));
      } else if (driver_c_prog_text_eq(op->primary, "cap") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_ARRAY) {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_i32(materialized.as.array != NULL
                                                             ? materialized.as.array->cap
                                                             : 0));
      } else if (driver_c_prog_text_eq(op->primary, "buffer") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_ARRAY) {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_ptr(materialized.as.array != NULL
                                                             ? (void *)materialized.as.array->items
                                                             : NULL));
      } else if (driver_c_prog_text_eq(op->primary, "len") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_BYTES) {
        driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(materialized.as.bytes.len));
      } else if (driver_c_prog_text_eq(op->primary, "len") &&
                 materialized.kind == DRIVER_C_PROG_VALUE_STR) {
        driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_str_len(materialized.as.str)));
      } else {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_ref(driver_c_prog_ref_record_field(base,
                                                                                        op->primary,
                                                                                        op->arg2)));
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LOAD_INDEX) {
      DriverCProgValue index_value = driver_c_prog_stack_pop(&frame);
      DriverCProgValue base = driver_c_prog_stack_pop(&frame);
      DriverCProgValue materialized = driver_c_prog_materialize(base);
      DriverCProgValue index_materialized = driver_c_prog_materialize(index_value);
      int32_t idx = driver_c_prog_value_to_i32(index_value);
      if (materialized.kind == DRIVER_C_PROG_VALUE_ROUTINE &&
          (index_materialized.kind == DRIVER_C_PROG_VALUE_TYPE_TOKEN ||
           (index_materialized.kind == DRIVER_C_PROG_VALUE_ROUTINE &&
            driver_c_prog_is_builtin_type_ctor_label(index_materialized.as.routine.label)))) {
        const char *type_text = index_materialized.kind == DRIVER_C_PROG_VALUE_TYPE_TOKEN
                                    ? index_materialized.as.type_text
                                    : index_materialized.as.routine.label;
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_routine_from_specialization(
                                     driver_c_prog_routine_bind_type_arg(registry,
                                                                         materialized.as.routine,
                                                                         type_text)));
      } else if (materialized.kind == DRIVER_C_PROG_VALUE_STR) {
        const char *text = driver_c_prog_str_ptr(materialized.as.str);
        int32_t len = driver_c_prog_str_len(materialized.as.str);
        if (idx < 0 || idx >= len) driver_c_die("driver_c program runtime: string index out of range");
        driver_c_prog_stack_push(&frame, driver_c_prog_value_i32((unsigned char)text[idx]));
      } else if (materialized.kind == DRIVER_C_PROG_VALUE_BYTES) {
        if (idx < 0 || idx >= materialized.as.bytes.len) driver_c_die("driver_c program runtime: bytes index out of range");
        driver_c_prog_stack_push(&frame, driver_c_prog_value_i32((int32_t)materialized.as.bytes.data[idx]));
      } else {
        driver_c_prog_stack_push(&frame,
                                 driver_c_prog_value_ref(driver_c_prog_ref_array_index(base,
                                                                                        idx,
                                                                                        item->label)));
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_CALL) {
      int32_t arg_count = op->arg0;
      DriverCProgValue inline_args[8];
      DriverCProgValue *call_args = NULL;
      DriverCProgValue callee = driver_c_prog_value_nil();
      DriverCProgValue call_out = driver_c_prog_value_nil();
      int32_t i = 0;
      if (arg_count < 0) driver_c_die("driver_c program runtime: negative call argc");
      if (arg_count > 0) {
        if (arg_count <= (int32_t)(sizeof(inline_args) / sizeof(inline_args[0]))) {
          memset(inline_args, 0, sizeof(inline_args));
          call_args = inline_args;
        } else {
          call_args = (DriverCProgValue *)driver_c_prog_xcalloc((size_t)arg_count, sizeof(DriverCProgValue));
        }
      }
      for (i = arg_count - 1; i >= 0; --i) {
        call_args[i] = driver_c_prog_stack_pop(&frame);
        if (i == 0) break;
      }
      callee = driver_c_prog_stack_pop(&frame);
      callee = driver_c_prog_materialize(callee);
      if (callee.kind == DRIVER_C_PROG_VALUE_ROUTINE) {
        callee.as.routine =
            driver_c_prog_routine_infer_type_args_from_call(registry,
                                                            callee.as.routine,
                                                            call_args,
                                                            arg_count);
        callee.as.routine =
            driver_c_prog_routine_bind_visible_type_args(registry, &frame, callee.as.routine);
        callee.as.routine =
            driver_c_prog_routine_bind_expected_type_args(registry, &frame, callee.as.routine);
        if (driver_c_prog_routine_has_unbound_type_params(registry, callee.as.routine)) {
          int32_t builtin_tag = driver_c_prog_builtin_tag_from_text(callee.as.routine.label);
          if (builtin_tag != DRIVER_C_PROG_BUILTIN_NONE &&
              driver_c_prog_try_builtin(registry,
                                        builtin_tag,
                                        callee.as.routine.label,
                                        "",
                                        call_args,
                                        arg_count,
                                        &call_out)) {
            if (call_args != inline_args) {
              free(call_args);
            }
            driver_c_prog_stack_push(&frame, call_out);
            pc = pc + 1;
            continue;
          }
        }
        if (callee.as.routine.item_id >= 0) {
          DriverCProgItem *callee_item = driver_c_prog_find_item_by_id(registry, callee.as.routine.item_id);
          if (callee_item != NULL &&
              callee_item->type_param_count > 0 &&
              callee.as.routine.type_arg_count < callee_item->type_param_count) {
            const char *arg0_type = arg_count > 0 ? driver_c_prog_value_static_type_text(call_args[0]) : "";
            const char *arg1_type = arg_count > 1 ? driver_c_prog_value_static_type_text(call_args[1]) : "";
            const char *arg2_type = arg_count > 2 ? driver_c_prog_value_static_type_text(call_args[2]) : "";
            char message[768];
            snprintf(message,
                     sizeof(message),
                     "driver_c program runtime: incomplete generic call caller=%s caller_module=%s callee=%s callee_module=%s bound=%d need=%d argc=%d arg0=%s arg1=%s arg2=%s",
                     frame.item != NULL && frame.item->label != NULL ? frame.item->label : "",
                     frame.item != NULL && frame.item->owner_module != NULL ? frame.item->owner_module : "",
                     callee_item->label != NULL ? callee_item->label : "",
                     callee_item->owner_module != NULL ? callee_item->owner_module : "",
                     callee.as.routine.type_arg_count,
                     callee_item->type_param_count,
                     arg_count,
                     arg0_type != NULL ? arg0_type : "",
                     arg1_type != NULL ? arg1_type : "",
                     arg2_type != NULL ? arg2_type : "");
            driver_c_die(message);
          }
        }
      }
      if (callee.kind == DRIVER_C_PROG_VALUE_TYPE_TOKEN) {
        if (arg_count != 1) driver_c_die("driver_c program runtime: type ctor call argc");
        call_out = driver_c_prog_cast_to_type(callee.as.type_text, call_args[0]);
      } else if (callee.kind != DRIVER_C_PROG_VALUE_ROUTINE) {
        driver_c_die("driver_c program runtime: call on non-routine");
      } else {
        if (callee.as.routine.item_id >= 0) {
          DriverCProgItem *callee_item = driver_c_prog_find_item_by_id(registry, callee.as.routine.item_id);
          if (callee_item != NULL && callee_item->param_is_var != NULL) {
            int32_t arg_idx = 0;
            for (arg_idx = 0; arg_idx < arg_count && arg_idx < callee_item->param_count; ++arg_idx) {
              if (callee_item->param_is_var[arg_idx] != 0 &&
                  (call_args[arg_idx].kind != DRIVER_C_PROG_VALUE_REF ||
                   call_args[arg_idx].as.ref.slot == NULL)) {
                char message[768];
                snprintf(message,
                         sizeof(message),
                         "driver_c program runtime: var arg not ref caller=%s caller_owner=%s callee=%s callee_owner=%s arg_index=%d arg_kind=%s",
                         frame.item != NULL && frame.item->label != NULL ? frame.item->label : "",
                         frame.item != NULL && frame.item->owner_module != NULL ? frame.item->owner_module : "",
                         callee_item->label != NULL ? callee_item->label : "",
                         callee_item->owner_module != NULL ? callee_item->owner_module : "",
                         arg_idx,
                         driver_c_prog_value_kind_name(call_args[arg_idx].kind));
                driver_c_die(message);
              }
            }
          }
        }
        call_out = driver_c_prog_execute_call(registry, callee.as.routine, call_args, arg_count);
      }
      if (call_args != inline_args) {
        free(call_args);
      }
      driver_c_prog_stack_push(&frame, call_out);
    } else if (op->kind_tag == DRIVER_C_PROG_OP_BINARY_OP) {
      DriverCProgValue rhs = driver_c_prog_stack_pop(&frame);
      DriverCProgValue lhs = driver_c_prog_stack_pop(&frame);
      DriverCProgValue lhs_m = driver_c_prog_materialize(lhs);
      DriverCProgValue rhs_m = driver_c_prog_materialize(rhs);
      if (driver_c_prog_text_eq(op->secondary, "add")) {
        if (lhs_m.kind == DRIVER_C_PROG_VALUE_STR || rhs_m.kind == DRIVER_C_PROG_VALUE_STR) {
          char *left = driver_c_prog_value_to_cstring_dup(lhs_m);
          char *right = driver_c_prog_value_to_cstring_dup(rhs_m);
          size_t n0 = strlen(left);
          size_t n1 = strlen(right);
          char *out_text = (char *)driver_c_prog_xmalloc(n0 + n1 + 1u);
          memcpy(out_text, left, n0);
          memcpy(out_text + n0, right, n1);
          out_text[n0 + n1] = '\0';
          free(left);
          free(right);
          driver_c_prog_stack_push(&frame, driver_c_prog_value_str_take(out_text));
        } else if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) + driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) + driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) + driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) + driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "sub")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) - driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) - driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) - driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) - driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "mul")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) * driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) * driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) * driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) * driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "div")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          uint64_t rhs_u = driver_c_prog_value_to_u64(rhs_m);
          if (rhs_u == 0u) driver_c_die("driver_c program runtime: divide by zero");
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) / rhs_u));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          int64_t rhs_i = driver_c_prog_value_to_i64(rhs_m);
          if (rhs_i == 0) driver_c_die("driver_c program runtime: divide by zero");
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) / rhs_i));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          uint32_t rhs_u = driver_c_prog_value_to_u32(rhs_m);
          if (rhs_u == 0u) driver_c_die("driver_c program runtime: divide by zero");
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) / rhs_u));
        } else {
          int32_t rhs_i = driver_c_prog_value_to_i32(rhs_m);
          if (rhs_i == 0) driver_c_die("driver_c program runtime: divide by zero");
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) / rhs_i));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "mod")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          uint64_t rhs_u = driver_c_prog_value_to_u64(rhs_m);
          if (rhs_u == 0u) driver_c_die("driver_c program runtime: mod by zero");
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) % rhs_u));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          int64_t rhs_i = driver_c_prog_value_to_i64(rhs_m);
          if (rhs_i == 0) driver_c_die("driver_c program runtime: mod by zero");
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) % rhs_i));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          uint32_t rhs_u = driver_c_prog_value_to_u32(rhs_m);
          if (rhs_u == 0u) driver_c_die("driver_c program runtime: mod by zero");
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) % rhs_u));
        } else {
          int32_t rhs_i = driver_c_prog_value_to_i32(rhs_m);
          if (rhs_i == 0) driver_c_die("driver_c program runtime: mod by zero");
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) % rhs_i));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "eq")) {
        int ok = 0;
        if (lhs_m.kind == DRIVER_C_PROG_VALUE_PTR || rhs_m.kind == DRIVER_C_PROG_VALUE_PTR) {
          ok = driver_c_prog_value_to_void_ptr(lhs_m) == driver_c_prog_value_to_void_ptr(rhs_m);
        } else if (lhs_m.kind == DRIVER_C_PROG_VALUE_STR || rhs_m.kind == DRIVER_C_PROG_VALUE_STR) {
          char *left = driver_c_prog_value_to_cstring_dup(lhs_m);
          char *right = driver_c_prog_value_to_cstring_dup(rhs_m);
          ok = driver_c_prog_text_eq(left, right);
          free(left);
          free(right);
        } else if (lhs_m.kind == DRIVER_C_PROG_VALUE_NIL || rhs_m.kind == DRIVER_C_PROG_VALUE_NIL) {
          ok = lhs_m.kind == rhs_m.kind;
        } else if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          ok = driver_c_prog_value_to_u64(lhs_m) == driver_c_prog_value_to_u64(rhs_m);
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          ok = driver_c_prog_value_to_i64(lhs_m) == driver_c_prog_value_to_i64(rhs_m);
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          ok = driver_c_prog_value_to_u32(lhs_m) == driver_c_prog_value_to_u32(rhs_m);
        } else {
          ok = driver_c_prog_value_to_i32(lhs_m) == driver_c_prog_value_to_i32(rhs_m);
        }
        driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(ok));
      } else if (driver_c_prog_text_eq(op->secondary, "ne")) {
        int ok = 0;
        if (lhs_m.kind == DRIVER_C_PROG_VALUE_PTR || rhs_m.kind == DRIVER_C_PROG_VALUE_PTR) {
          ok = driver_c_prog_value_to_void_ptr(lhs_m) != driver_c_prog_value_to_void_ptr(rhs_m);
        } else if (lhs_m.kind == DRIVER_C_PROG_VALUE_STR || rhs_m.kind == DRIVER_C_PROG_VALUE_STR) {
          char *left = driver_c_prog_value_to_cstring_dup(lhs_m);
          char *right = driver_c_prog_value_to_cstring_dup(rhs_m);
          ok = !driver_c_prog_text_eq(left, right);
          free(left);
          free(right);
        } else if (lhs_m.kind == DRIVER_C_PROG_VALUE_NIL || rhs_m.kind == DRIVER_C_PROG_VALUE_NIL) {
          ok = lhs_m.kind != rhs_m.kind;
        } else if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          ok = driver_c_prog_value_to_u64(lhs_m) != driver_c_prog_value_to_u64(rhs_m);
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          ok = driver_c_prog_value_to_i64(lhs_m) != driver_c_prog_value_to_i64(rhs_m);
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          ok = driver_c_prog_value_to_u32(lhs_m) != driver_c_prog_value_to_u32(rhs_m);
        } else {
          ok = driver_c_prog_value_to_i32(lhs_m) != driver_c_prog_value_to_i32(rhs_m);
        }
        driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(ok));
      } else if (driver_c_prog_text_eq(op->secondary, "lt")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_u64(lhs_m) < driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_i64(lhs_m) < driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_u32(lhs_m) < driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_i32(lhs_m) < driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "le")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_u64(lhs_m) <= driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_i64(lhs_m) <= driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_u32(lhs_m) <= driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_i32(lhs_m) <= driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "gt")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_u64(lhs_m) > driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_i64(lhs_m) > driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_u32(lhs_m) > driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_i32(lhs_m) > driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "ge")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_u64(lhs_m) >= driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_i64(lhs_m) >= driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_u32(lhs_m) >= driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_to_i32(lhs_m) >= driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "logical_and")) {
        driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_truthy(lhs_m) && driver_c_prog_value_truthy(rhs_m)));
      } else if (driver_c_prog_text_eq(op->secondary, "logical_or")) {
        driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(driver_c_prog_value_truthy(lhs_m) || driver_c_prog_value_truthy(rhs_m)));
      } else if (driver_c_prog_text_eq(op->secondary, "bitwise_and")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) & driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) & driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) & driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) & driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "bitwise_or")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) | driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) | driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) | driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) | driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "bitwise_xor")) {
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) ^ driver_c_prog_value_to_u64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) ^ driver_c_prog_value_to_i64(rhs_m)));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) ^ driver_c_prog_value_to_u32(rhs_m)));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) ^ driver_c_prog_value_to_i32(rhs_m)));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "shift_left")) {
        int32_t shift = driver_c_prog_value_to_i32(rhs_m);
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) << shift));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) << shift));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) << shift));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) << shift));
        }
      } else if (driver_c_prog_text_eq(op->secondary, "shift_right")) {
        int32_t shift = driver_c_prog_value_to_i32(rhs_m);
        if (driver_c_prog_binary_prefers_u64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u64(driver_c_prog_value_to_u64(lhs_m) >> shift));
        } else if (driver_c_prog_binary_prefers_i64(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(lhs_m) >> shift));
        } else if (driver_c_prog_binary_prefers_u32(lhs_m, rhs_m)) {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_u32(driver_c_prog_value_to_u32(lhs_m) >> shift));
        } else {
          driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(lhs_m) >> shift));
        }
      } else {
        driver_c_die("driver_c program runtime: unsupported binary_op");
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_UNARY_NOT) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(!driver_c_prog_value_truthy(value)));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_UNARY_NEG) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(-driver_c_prog_value_to_i64(value)));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_MAKE_ARRAY || op->kind_tag == DRIVER_C_PROG_OP_MAKE_TUPLE) {
      int32_t count = op->arg0;
      DriverCProgArray *array = driver_c_prog_array_new_with_elem_and_len_uninit("", count);
      DriverCProgValue inline_tmp[8];
      DriverCProgValue *tmp = NULL;
      int32_t i = 0;
      if (count < 0) driver_c_die("driver_c program runtime: negative aggregate count");
      if (count > 0) {
        if (count <= (int32_t)(sizeof(inline_tmp) / sizeof(inline_tmp[0]))) {
          memset(inline_tmp, 0, sizeof(inline_tmp));
          tmp = inline_tmp;
        } else {
          tmp = (DriverCProgValue *)driver_c_prog_xcalloc((size_t)count, sizeof(DriverCProgValue));
        }
      }
      for (i = count - 1; i >= 0; --i) {
        tmp[i] = driver_c_prog_stack_pop(&frame);
        if (i == 0) break;
      }
      for (i = 0; i < count; ++i) {
        array->items[i] = driver_c_prog_prepare_fresh_slot_value(tmp[i]);
      }
      if (tmp != inline_tmp) {
        free(tmp);
      }
      driver_c_prog_stack_push(&frame, driver_c_prog_value_array_temp(array));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_MAKE_RECORD) {
      int32_t count = op->arg0;
      DriverCProgValue inline_tmp[8];
      DriverCProgValue *tmp = NULL;
      char **field_names = NULL;
      int32_t field_name_count = 0;
      int32_t i = 0;
      if (frame.stack_len < count) {
        char message[1024];
        snprintf(message,
                 sizeof(message),
                 "driver_c program runtime: make_record stack short item=%s module=%s pc=%d primary=%s secondary=%s count=%d stack_len=%d",
                 item->label != NULL ? item->label : "",
                 item->owner_module != NULL ? item->owner_module : "",
                 pc,
                 op->primary != NULL ? op->primary : "",
                 op->secondary != NULL ? op->secondary : "",
                 count,
                 frame.stack_len);
        driver_c_die(message);
      }
      if (count > 0) {
        if (count <= (int32_t)(sizeof(inline_tmp) / sizeof(inline_tmp[0]))) {
          memset(inline_tmp, 0, sizeof(inline_tmp));
          tmp = inline_tmp;
        } else {
          tmp = (DriverCProgValue *)driver_c_prog_xcalloc((size_t)count, sizeof(DriverCProgValue));
        }
      }
      for (i = count - 1; i >= 0; --i) {
        tmp[i] = driver_c_prog_stack_pop(&frame);
        if (i == 0) break;
      }
      field_names = driver_c_prog_op_secondary_lines(op, &field_name_count);
      if (field_name_count != count) {
        driver_c_die("driver_c program runtime: make_record field name count mismatch");
      }
      if (driver_c_prog_text_eq(op->primary, "Bytes")) {
        DriverCProgValue data_value = driver_c_prog_value_nil();
        DriverCProgValue len_value = driver_c_prog_value_i32(0);
        int saw_data = 0;
        int saw_len = 0;
        for (i = 0; i < count; ++i) {
          const char *name = field_names[i];
          DriverCProgValue value = driver_c_prog_materialize(tmp[i]);
          if (driver_c_prog_text_eq(name, "data")) {
            data_value = value;
            saw_data = 1;
          } else if (driver_c_prog_text_eq(name, "len")) {
            len_value = value;
            saw_len = 1;
          }
        }
        if (tmp != inline_tmp) {
          free(tmp);
        }
        if (!saw_data || !saw_len) {
          driver_c_die("driver_c program runtime: Bytes make_record missing data/len");
        }
        {
          DriverCProgValue bytes_value = driver_c_prog_value_bytes_empty();
          int32_t len = driver_c_prog_value_to_i32(len_value);
          bytes_value.as.bytes.data = (uint8_t *)driver_c_prog_value_to_void_ptr(data_value);
          bytes_value.as.bytes.len = len > 0 ? len : 0;
          driver_c_prog_stack_push(&frame, bytes_value);
        }
        continue;
      }
      if (driver_c_prog_text_eq(op->primary, "str")) {
        DriverCProgValue data_value = driver_c_prog_value_nil();
        DriverCProgValue len_value = driver_c_prog_value_i32(0);
        DriverCProgValue store_id_value = driver_c_prog_value_i32(0);
        DriverCProgValue flags_value = driver_c_prog_value_i32(0);
        int saw_data = 0;
        for (i = 0; i < count; ++i) {
          const char *name = field_names[i];
          DriverCProgValue value = driver_c_prog_materialize(tmp[i]);
          if (driver_c_prog_text_eq(name, "data")) {
            data_value = value;
            saw_data = 1;
          } else if (driver_c_prog_text_eq(name, "len")) {
            len_value = value;
          } else if (driver_c_prog_text_eq(name, "store_id")) {
            store_id_value = value;
          } else if (driver_c_prog_text_eq(name, "flags")) {
            flags_value = value;
          }
        }
        if (tmp != inline_tmp) {
          free(tmp);
        }
        {
          DriverCProgValue str_value = driver_c_prog_value_str_empty();
          str_value.as.str.ptr = (const char *)driver_c_prog_value_to_void_ptr(data_value);
          str_value.as.str.len = driver_c_prog_value_to_i32(len_value);
          if (str_value.as.str.len < 0) str_value.as.str.len = 0;
          str_value.as.str.store_id = driver_c_prog_value_to_i32(store_id_value);
          str_value.as.str.flags = driver_c_prog_value_to_i32(flags_value);
          if (!saw_data && str_value.as.str.len == 0) {
            str_value.as.str.ptr = NULL;
          }
          driver_c_prog_stack_push(&frame, str_value);
        }
        continue;
      }
      {
        char *record_type = driver_c_prog_make_record_type_dup(registry,
                                                               &frame,
                                                               item,
                                                               op->primary != NULL ? op->primary : "");
        DriverCProgRecord *record = driver_c_prog_record_new_with_type(record_type);
        DriverCProgTypeDecl *record_decl =
            driver_c_prog_make_record_decl_cached(registry, item, op, record_type);
        int fields_in_decl_order = 0;
        if (record_decl != NULL && record_decl->field_count > 0) {
          driver_c_prog_record_init_from_decl(record, record_decl);
          fields_in_decl_order = record_decl->field_count == count ? 1 : 0;
          if (fields_in_decl_order) {
            for (i = 0; i < count; ++i) {
              if (!driver_c_prog_text_eq(record_decl->field_names[i], field_names[i])) {
                fields_in_decl_order = 0;
                break;
              }
            }
          }
        }
        if (fields_in_decl_order) {
          for (i = 0; i < count; ++i) {
            record->values[i] = driver_c_prog_prepare_fresh_slot_value(tmp[i]);
          }
        } else {
          for (i = 0; i < count; ++i) {
            const char *name = field_names[i];
            DriverCProgValue *slot = NULL;
            int32_t field_index =
                record_decl != NULL ? driver_c_prog_type_decl_field_index(record_decl, name) : -1;
            if (field_index >= 0) {
              slot = driver_c_prog_record_slot_at(record, field_index, name);
            } else {
              slot = driver_c_prog_record_slot(record, name, 1);
            }
            *slot = driver_c_prog_prepare_fresh_slot_value(tmp[i]);
          }
        }
        if (tmp != inline_tmp) {
          free(tmp);
        }
        free(record_type);
        driver_c_prog_stack_push(&frame, driver_c_prog_value_record_temp(record));
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_MATERIALIZE) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      driver_c_prog_stack_push(&frame, driver_c_prog_materialize(value));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_JUMP_IF_FALSE) {
      DriverCProgValue cond = driver_c_prog_stack_pop(&frame);
      if (!driver_c_prog_value_truthy(cond)) {
        int32_t target_pc = driver_c_prog_find_label_pc(&frame, op->primary);
        if (target_pc < 0) driver_c_die("driver_c program runtime: missing jump label");
        pc = target_pc;
        continue;
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_JUMP) {
      int32_t target_pc = driver_c_prog_find_label_pc(&frame, op->primary);
      if (target_pc < 0) driver_c_die("driver_c program runtime: missing jump label");
      pc = target_pc;
      continue;
    } else if (op->kind_tag == DRIVER_C_PROG_OP_LABEL || op->kind_tag == DRIVER_C_PROG_OP_PHI_MERGE) {
    } else if (op->kind_tag == DRIVER_C_PROG_OP_RETURN_VALUE) {
      return_value = driver_c_prog_stack_pop(&frame);
      has_return_value = 1;
      goto driver_c_prog_eval_item_done;
    } else if (op->kind_tag == DRIVER_C_PROG_OP_RETURN_VOID) {
      has_return_value = 0;
      return_value = driver_c_prog_value_nil();
      goto driver_c_prog_eval_item_done;
    } else if (op->kind_tag == DRIVER_C_PROG_OP_STORE_LOCAL) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      if (op->arg0 < 0 || op->arg0 >= item->local_count) driver_c_die("driver_c program runtime: store_local slot");
      driver_c_prog_assign_slot(&frame.locals[op->arg0], value);
    } else if (op->kind_tag == DRIVER_C_PROG_OP_STORE_PARAM) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      if (op->arg0 < 0 || op->arg0 >= item->param_count) driver_c_die("driver_c program runtime: store_param slot");
      driver_c_prog_assign_slot(&frame.params[op->arg0], value);
    } else if (op->kind_tag == DRIVER_C_PROG_OP_STORE_NAME) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      DriverCProgItem *target = driver_c_prog_resolve_visible_item(registry, item->owner_module, op->primary);
      if (target == NULL) driver_c_die("driver_c program runtime: store_name unresolved");
      if (target->item_id < 0 || target->item_id >= registry->items_by_id_len) {
        driver_c_die("driver_c program runtime: store_name target out of range");
      }
      driver_c_prog_assign_slot(&registry->global_values[target->item_id], value);
      registry->global_initialized[target->item_id] = 1;
    } else if (op->kind_tag == DRIVER_C_PROG_OP_STORE_LOCAL_FIELD) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      DriverCProgValue *slot = NULL;
      if (op->arg0 < 0 || op->arg0 >= item->local_count) driver_c_die("driver_c program runtime: store_local_field slot");
      if (driver_c_prog_try_store_str_field(driver_c_prog_value_ref(&frame.locals[op->arg0]),
                                            op->primary,
                                            value)) {
        pc = pc + 1;
        continue;
      }
      if (driver_c_prog_try_store_array_field(driver_c_prog_value_ref(&frame.locals[op->arg0]),
                                              op->primary,
                                              value)) {
        pc = pc + 1;
        continue;
      }
      if (driver_c_prog_try_store_bytes_field(driver_c_prog_value_ref(&frame.locals[op->arg0]),
                                              op->primary,
                                              value)) {
        pc = pc + 1;
        continue;
      }
      slot = driver_c_prog_ref_record_field_for_store(driver_c_prog_value_ref(&frame.locals[op->arg0]),
                                                      op->primary,
                                                      op->arg1,
                                                      item->label,
                                                      op->kind);
      driver_c_prog_assign_slot(slot, value);
    } else if (op->kind_tag == DRIVER_C_PROG_OP_STORE_PARAM_FIELD) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      DriverCProgValue *slot = NULL;
      if (op->arg0 < 0 || op->arg0 >= item->param_count) driver_c_die("driver_c program runtime: store_param_field slot");
      if (driver_c_prog_try_store_str_field(driver_c_prog_value_ref(&frame.params[op->arg0]),
                                            op->primary,
                                            value)) {
        pc = pc + 1;
        continue;
      }
      if (driver_c_prog_try_store_array_field(driver_c_prog_value_ref(&frame.params[op->arg0]),
                                              op->primary,
                                              value)) {
        pc = pc + 1;
        continue;
      }
      if (driver_c_prog_try_store_bytes_field(driver_c_prog_value_ref(&frame.params[op->arg0]),
                                              op->primary,
                                              value)) {
        pc = pc + 1;
        continue;
      }
      slot = driver_c_prog_ref_record_field_for_store(driver_c_prog_value_ref(&frame.params[op->arg0]),
                                                      op->primary,
                                                      op->arg1,
                                                      item->label,
                                                      op->kind);
      driver_c_prog_assign_slot(slot, value);
    } else if (op->kind_tag == DRIVER_C_PROG_OP_STORE_FIELD) {
      DriverCProgValue base = driver_c_prog_stack_pop(&frame);
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      if (driver_c_prog_try_store_str_field(base, op->primary, value)) {
        pc = pc + 1;
        continue;
      }
      if (driver_c_prog_try_store_array_field(base, op->primary, value)) {
        pc = pc + 1;
        continue;
      }
      if (driver_c_prog_try_store_bytes_field(base, op->primary, value)) {
        pc = pc + 1;
        continue;
      }
      DriverCProgValue *slot =
          driver_c_prog_ref_record_field_for_store(base, op->primary, op->arg2, item->label, op->kind);
      driver_c_prog_assign_slot(slot, value);
    } else if (op->kind_tag == DRIVER_C_PROG_OP_STORE_INDEX) {
      DriverCProgValue index_value = driver_c_prog_stack_pop(&frame);
      DriverCProgValue base = driver_c_prog_stack_pop(&frame);
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      DriverCProgValue *slot = driver_c_prog_ref_array_index(base,
                                                             driver_c_prog_value_to_i32(index_value),
                                                             item->label);
      driver_c_prog_assign_slot(slot, value);
    } else if (op->kind_tag == DRIVER_C_PROG_OP_DROP_VALUE) {
      (void)driver_c_prog_stack_pop(&frame);
    } else if (op->kind_tag == DRIVER_C_PROG_OP_FOR_RANGE_INIT) {
      DriverCProgValue end_value = driver_c_prog_stack_pop(&frame);
      DriverCProgValue start_value = driver_c_prog_stack_pop(&frame);
      int32_t loop_pos = driver_c_prog_ensure_loop_slot(&frame, op->primary, op->arg0);
      int32_t local_slot = driver_c_prog_find_local_slot_by_name(&frame, op->primary);
      frame.loop_currents[loop_pos] = driver_c_prog_value_to_i64(start_value);
      frame.loop_ends[loop_pos] = driver_c_prog_value_to_i64(end_value);
      if (local_slot >= 0) {
        driver_c_prog_assign_slot(&frame.locals[local_slot], driver_c_prog_value_i64(frame.loop_currents[loop_pos]));
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_FOR_RANGE_CHECK) {
      int32_t loop_pos = driver_c_prog_find_loop_slot(&frame, op->primary);
      int ok = 0;
      if (loop_pos < 0) driver_c_die("driver_c program runtime: for_range_check loop missing");
      if (driver_c_prog_text_eq(op->secondary, "inclusive")) {
        ok = frame.loop_currents[loop_pos] <= frame.loop_ends[loop_pos];
      } else {
        ok = frame.loop_currents[loop_pos] < frame.loop_ends[loop_pos];
      }
      driver_c_prog_stack_push(&frame, driver_c_prog_value_bool(ok));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_FOR_RANGE_STEP) {
      int32_t loop_pos = driver_c_prog_find_loop_slot(&frame, op->primary);
      int32_t local_slot = driver_c_prog_find_local_slot_by_name(&frame, op->primary);
      if (loop_pos < 0) driver_c_die("driver_c program runtime: for_range_step loop missing");
      frame.loop_currents[loop_pos] = frame.loop_currents[loop_pos] + 1;
      if (local_slot >= 0) {
        driver_c_prog_assign_slot(&frame.locals[local_slot], driver_c_prog_value_i64(frame.loop_currents[loop_pos]));
      }
    } else if (op->kind_tag == DRIVER_C_PROG_OP_CAST_INT32) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      driver_c_prog_stack_push(&frame, driver_c_prog_value_i32(driver_c_prog_value_to_i32(value)));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_CAST_INT64) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      driver_c_prog_stack_push(&frame, driver_c_prog_value_i64(driver_c_prog_value_to_i64(value)));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_CAST_TYPE) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      driver_c_prog_stack_push(&frame, driver_c_prog_cast_to_type(op->primary, value));
    } else if (op->kind_tag == DRIVER_C_PROG_OP_NEW_REF) {
      DriverCProgValue value = driver_c_prog_stack_pop(&frame);
      DriverCProgValue *heap_slot = (DriverCProgValue *)driver_c_prog_xmalloc(sizeof(DriverCProgValue));
      *heap_slot = value;
      driver_c_prog_value_clear_ephemeral_flag_root(heap_slot);
      driver_c_prog_stack_push(&frame, driver_c_prog_value_ref(heap_slot));
    } else {
      char message[256];
      snprintf(message, sizeof(message), "driver_c program runtime: unsupported exec op kind=%s",
               op->kind != NULL ? op->kind : "");
      driver_c_die(message);
    }
    if (trace_enabled) {
      driver_c_prog_trace_op(&frame, "after");
    }
    pc = pc + 1;
  }
driver_c_prog_eval_item_done:
  return_value = has_return_value ? driver_c_prog_materialize(return_value) : driver_c_prog_value_nil();
  driver_c_prog_frame_cleanup(&frame);
  return return_value;
}

static int32_t driver_c_prog_entry_exit_code(DriverCProgValue value) {
  DriverCProgValue materialized = driver_c_prog_materialize(value);
  if (materialized.kind == DRIVER_C_PROG_VALUE_NIL) {
    return 0;
  }
  if (materialized.kind == DRIVER_C_PROG_VALUE_BOOL) {
    return materialized.as.b ? 0 : 1;
  }
  if (materialized.kind == DRIVER_C_PROG_VALUE_I32) {
    return materialized.as.i32;
  }
  if (materialized.kind == DRIVER_C_PROG_VALUE_I64) {
    return (int32_t)materialized.as.i64;
  }
  if (materialized.kind == DRIVER_C_PROG_VALUE_U32) {
    return (int32_t)materialized.as.u32;
  }
  if (materialized.kind == DRIVER_C_PROG_VALUE_U64) {
    return (int32_t)materialized.as.u64;
  }
  driver_c_die("driver_c program runtime: unsupported entry return type");
  return 1;
}

__attribute__((weak)) int32_t driver_c_compiler_core_local_payload_bridge(const char *payload) {
  int argc = 0;
  char **argv = NULL;
  char *label = driver_c_payload_label_dup(payload);
  const char *cmd = NULL;
  int32_t rc = 0;
  argv = driver_c_cli_current_argv_dup(&argc);
  if (argv == NULL) {
    free(label);
    driver_c_die("driver_c compiler_core local payload bridge: argv alloc failed");
  }
  cmd = argc > 1 ? argv[1] : "";
  if (cheng_v2c_tooling_handle == NULL || cheng_v2c_tooling_is_command == NULL) {
    free(argv);
    free(label);
    driver_c_die("driver_c compiler_core local payload bridge: bootstrap tooling handle unavailable");
  }
  if (cmd == NULL || !cheng_v2c_tooling_is_command(cmd)) {
    char message[512];
    snprintf(message,
             sizeof(message),
             "driver_c compiler_core local payload bridge: unsupported label=%s cmd=%s",
             label != NULL ? label : "",
             cmd != NULL ? cmd : "");
    free(argv);
    free(label);
    driver_c_die(message);
  }
  rc = cheng_v2c_tooling_handle(argc, argv);
  free(argv);
  free(label);
  return rc;
}

__attribute__((weak)) int32_t driver_c_compiler_core_tooling_local_payload_bridge(const char *payload) {
  return driver_c_compiler_core_local_payload_bridge(payload);
}

__attribute__((weak)) int32_t driver_c_compiler_core_program_local_payload_bridge(const char *payload) {
  DriverCProgRegistry *registry = driver_c_prog_registry_get();
  DriverCProgItem *item = NULL;
  DriverCProgValue value = driver_c_prog_value_nil();
  char *label = driver_c_payload_label_dup(payload);
  char message[1024];
  if (label != NULL && label[0] != '\0') {
    item = driver_c_prog_find_item_by_label(registry, label);
  }
  if (item == NULL) {
    item = driver_c_prog_find_entry_item(registry);
  }
  if (item == NULL) {
    snprintf(message,
             sizeof(message),
             "driver_c compiler_core program local payload bridge: item missing label=%s",
             label != NULL ? label : "");
    free(label);
    driver_c_die(message);
  }
  value = driver_c_prog_eval_item(registry, item, NULL, 0, NULL);
  free(label);
  return driver_c_prog_entry_exit_code(value);
}

__attribute__((weak)) int32_t driver_c_compiler_core_program_argv_bridge(int32_t argc, const char **argv) {
  DriverCProgRegistry *registry = NULL;
  DriverCProgItem *item = NULL;
  DriverCProgValue value = driver_c_prog_value_nil();
  if (argv != NULL) {
    __cheng_setCmdLine(argc, argv);
  }
  registry = driver_c_prog_registry_get();
  item = driver_c_prog_find_entry_item(registry);
  if (item == NULL) {
    driver_c_die("driver_c compiler_core program argv bridge: entry item missing");
  }
  value = driver_c_prog_eval_item(registry, item, NULL, 0, NULL);
  return driver_c_prog_entry_exit_code(value);
}
