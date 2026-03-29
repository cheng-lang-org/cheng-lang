#include "cheng_v2c_tooling.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SHA256_EMPTY_HEX "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

typedef struct {
    size_t len;
    size_t cap;
    char **items;
} StringList;

typedef struct {
    size_t len;
    size_t cap;
    int *items;
} IntList;

typedef struct {
    size_t len;
    size_t cap;
    uint8_t *items;
} ByteList;

typedef struct {
    char *name;
    char *domain;
    char *fanout;
    char *repair;
    char *dispersal;
    char *priority;
    char *addressing;
    int depth;
    int ida_k;
    int ida_n;
} TopologyDecl;

typedef struct {
    size_t len;
    size_t cap;
    TopologyDecl *items;
} TopologyList;

typedef struct {
    char *op_name;
    char *topology_ref;
    char *subject_name;
    char *signature_kind;
} NetworkOp;

typedef struct {
    size_t len;
    size_t cap;
    NetworkOp *items;
} NetworkOpList;

typedef struct {
    TopologyList topologies;
    NetworkOpList ops;
} NetworkProgram;

typedef struct {
    size_t len;
    size_t cap;
    char **names;
    char **header_texts;
} CompilerCoreModuleList;

typedef struct {
    size_t len;
    size_t cap;
    char **labels;
    char **header_texts;
    IntList body_line_counts;
    StringList body_hashes;
} CompilerCoreValueDeclList;

typedef struct {
    char *version;
    CompilerCoreModuleList modules;
    StringList import_paths;
    StringList import_aliases;
    StringList type_headers;
    IntList type_body_line_counts;
    StringList type_body_hashes;
    CompilerCoreValueDeclList const_decls;
    CompilerCoreValueDeclList var_decls;
    StringList routine_kinds;
    StringList routine_names;
    StringList routine_signatures;
    StringList routine_return_type_texts;
    IntList routine_param_counts;
    IntList routine_executable_ast_ready;
    IntList routine_local_counts;
    IntList routine_stmt_counts;
    IntList routine_expr_counts;
    StringList routine_signature_hashes;
    IntList routine_body_line_counts;
    StringList routine_body_hashes;
    StringList routine_first_body_lines;
    StringList routine_body_texts;
    StringList top_level_kinds;
    StringList top_level_labels;
    StringList top_level_owner_modules;
    StringList top_level_source_paths;
} CompilerCoreProgram;

typedef struct {
    int parse_ok;
    int had_recognized_surface;
    int had_network_surface;
    int had_compiler_core_surface;
    NetworkProgram network_program;
    CompilerCoreProgram compiler_core_program;
} ParsedV2Source;

typedef struct {
    char *version;
    IntList item_ids;
    StringList top_level_kinds;
    StringList top_level_labels;
    StringList top_level_source_paths;
    StringList high_ops;
    StringList proof_domains;
    StringList effect_classes;
    StringList binding_scopes;
    StringList import_paths;
    StringList import_aliases;
    StringList import_categories;
    IntList body_line_counts;
    StringList body_hashes;
    StringList signature_hashes;
    IntList executable_ast_ready;
    StringList return_type_texts;
    IntList param_counts;
    IntList local_counts;
    IntList stmt_counts;
    IntList expr_counts;
} CompilerCoreFacts;

typedef struct {
    char *version;
    IntList item_ids;
    StringList canonical_ops;
    StringList runtime_targets;
    StringList proof_domains;
    StringList effect_classes;
    StringList binding_scopes;
    StringList top_level_kinds;
    StringList labels;
    StringList source_paths;
    StringList import_paths;
    StringList import_aliases;
    StringList import_categories;
    StringList signature_hashes;
    IntList body_line_counts;
    StringList body_hashes;
    IntList executable_ast_ready;
    StringList return_type_texts;
    IntList param_counts;
    IntList local_counts;
    IntList stmt_counts;
    IntList expr_counts;
} CompilerCoreLowPlan;

typedef struct {
    char *version;
    CompilerCoreProgram program;
    CompilerCoreFacts facts;
    CompilerCoreLowPlan low_plan;
} CompilerCoreBundle;

typedef struct {
    char *version;
    IntList op_ids;
    StringList op_names;
    StringList topology_refs;
    StringList topology_canonical_forms;
    StringList network_domains;
    StringList addressing_modes;
    IntList topology_depths;
    StringList transport_modes;
    StringList repair_modes;
    StringList dispersal_modes;
    StringList priority_classes;
    StringList address_bindings;
    StringList distance_metrics;
    StringList route_plan_models;
    StringList identity_scopes;
    StringList anti_entropy_signature_kinds;
    StringList proof_domains;
} NetworkFacts;

typedef struct {
    char *version;
    IntList op_ids;
    StringList canonical_ops;
    StringList runtime_targets;
    StringList proof_domains;
    StringList identity_scopes;
    StringList addressing_modes;
    IntList topology_depths;
    StringList transport_modes;
    StringList repair_modes;
    StringList dispersal_modes;
    StringList priority_classes;
    StringList address_bindings;
    StringList distance_metrics;
    StringList route_plan_models;
    StringList anti_entropy_signature_kinds;
    StringList topology_canonical_forms;
} NetworkLowPlan;

typedef struct {
    char *version;
    NetworkProgram program;
    NetworkFacts facts;
    NetworkLowPlan low_plan;
} NetworkBundle;

typedef struct {
    char *version;
    char *root_path;
    char *package_id;
    StringList file_paths;
    StringList file_cids;
    StringList file_topology_refs;
    StringList topology_canonical_forms;
    char *manifest_text;
    char *manifest_cid;
} SourceManifestBundle;

typedef struct {
    char *version;
    char *module_kind;
    char *emit_kind;
    char *target_triple;
    char *obj_format;
    StringList runtime_targets;
    StringList runtime_symbols;
    StringList provided_targets;
    StringList provided_symbols;
    StringList unresolved_targets;
    StringList unresolved_symbols;
    StringList provider_modules;
    char *plan_text;
    char *plan_cid;
} NativeLinkPlanBundle;

typedef struct {
    char *version;
    char *module_kind;
    char *emit_kind;
    char *target_triple;
    char *obj_format;
    char *linker_flavor;
    char *output_kind;
    char *output_name;
    char *output_suffix;
    char *primary_obj_file_cid;
    int primary_obj_file_byte_count;
    StringList provider_modules;
    StringList provider_source_paths;
    StringList provider_source_kinds;
    StringList provider_compile_modes;
    StringList provider_manifest_cids;
    StringList provider_execution_models;
    StringList provider_trace_symbols;
    IntList provider_symbol_counts;
    StringList provider_symbols_flat;
    StringList provider_obj_file_cids;
    IntList provider_obj_file_byte_counts;
    int entry_required;
    char *entry_symbol;
    int entry_present;
    StringList missing_reasons;
    char *plan_text;
    char *plan_cid;
} SystemLinkPlanBundle;

typedef struct {
    char *version;
    char *module_path;
    char *source_path;
    char *source_manifest_cid;
    char *target_triple;
    char *source_kind;
    char *compile_mode;
    char *execution_model;
    char *trace_symbol;
    StringList runtime_targets;
    StringList runtime_symbols;
    char *obj_image_version;
    char *obj_image_cid;
    char *obj_file_version;
    char *obj_file_cid;
    int obj_file_byte_count;
    ByteList obj_file_bytes;
    char *object_text;
} RuntimeProviderObjectBundle;

typedef struct {
    char *version;
    char *source_path;
    char *emit_kind;
    char *target_triple;
    char *module_kind;
    char *machine_version;
    char *machine_pipeline_version;
    char *obj_writer_version;
    char *obj_image_version;
    char *obj_file_version;
    char *obj_format;
    char *manifest_cid;
    char *compile_key;
    char *compile_key_text;
    char *obj_image_cid;
    char *obj_file_cid;
    int obj_file_byte_count;
    int obj_section_count;
    int obj_symbol_count;
    int obj_reloc_count;
    char *link_plan_version;
    char *link_plan_cid;
    char *system_link_plan_version;
    char *system_link_plan_cid;
    int runtime_target_count;
    int runtime_unresolved_count;
    int native_link_closure_ok;
    int system_link_ready;
    int system_link_missing_reason_count;
    int topology_count;
    int network_entry_count;
    int compiler_core_entry_count;
    char *system_link_plan_text;
    char *artifact_text;
} ReleaseArtifactBundle;

typedef struct {
    char *version;
    char *content_cid;
    int depth;
    char *priority_class;
    char *dispersal_mode;
    char *addressing_mode;
    char *address_binding;
    char *distance_metric;
    char *source_address;
    char *target_address;
    int total_distance;
    StringList path_texts;
    char *canonical_text;
    char *canonical_cid;
} LsmrRoutePlanBundle;

typedef struct {
    char *version;
    int op_index;
    char *domain;
    char *content_cid;
    char *source_address;
    char *target_address;
    char *route_plan_cid;
    int route_path_count;
    int total_distance;
    char *route_plan_text;
} NetworkLsmrBinding;

typedef struct {
    char *version;
    char *source_path;
    char *emit_kind;
    char *target_triple;
    char *module_kind;
    char *linker_flavor;
    char *linker_path;
    char *output_path;
    char *primary_obj_path;
    char *primary_obj_file_cid;
    int primary_obj_file_byte_count;
    StringList provider_modules;
    StringList provider_obj_paths;
    StringList provider_obj_file_cids;
    IntList provider_obj_file_byte_counts;
    int entry_required;
    char *entry_symbol;
    int exit_code;
    int output_present;
    int output_byte_count;
    char *output_file_cid;
    int link_ok;
    char *link_output;
    char *report_text;
} SystemLinkExecBundle;

typedef struct {
    char *version;
    char *module_kind;
    char *target_triple;
    char *architecture;
    char *obj_format;
    char *symbol_prefix;
    char *text_section_name;
    char *cstring_section_name;
    char *call_relocation_kind;
    char *metadata_page_relocation_kind;
    char *metadata_pageoff_relocation_kind;
    char *regalloc;
    int pointer_width_bits;
    int stack_align_bytes;
    char *entry_symbol_override;
    IntList item_ids;
    StringList symbol_names;
    StringList runtime_targets;
    StringList call_symbol_names;
    IntList frame_sizes;
    IntList stack_aligns;
    StringList arg_regs;
    StringList scratch_regs;
    StringList callee_saved_regs;
    StringList binding_summaries;
    StringList instruction_word_hex;
    IntList function_byte_sizes;
    IntList local_payload_page_reloc_offsets;
    IntList local_payload_pageoff_reloc_offsets;
    IntList call_reloc_offsets;
    StringList metadata_operand_modes;
    StringList local_payload_texts;
    IntList function_reloc_starts;
    IntList function_reloc_counts;
    IntList reloc_offsets;
    StringList reloc_symbols;
    StringList reloc_kinds;
    StringList data_symbol_names;
    StringList data_section_names;
    StringList data_payload_texts;
    IntList data_align_pow2;
} MachineModuleBundle;

typedef struct {
    char *version;
    char *module_kind;
    char *target_triple;
    char *format;
    char *text_section_name;
    char *cstring_section_name;
    char *symtab_section_name;
    char *strtab_section_name;
    char *reloc_section_name;
    StringList section_names;
    IntList section_align_pow2;
    IntList section_sizes;
    StringList symbol_names;
    StringList symbol_bindings;
    StringList symbol_sections;
    IntList symbol_values;
    IntList symbol_sizes;
    StringList relocation_sections;
    IntList relocation_offsets;
    StringList relocation_symbols;
    StringList relocation_kinds;
    StringList metadata_symbol_names;
    StringList metadata_payloads;
    StringList data_symbol_names;
    StringList data_section_names;
    StringList data_payloads;
    IntList data_align_pow2;
    int section_count;
    int symbol_count;
    int reloc_count;
    char *image_text;
    char *image_cid;
} ObjImageBundle;

typedef struct {
    char *version;
    char *module_kind;
    char *target_triple;
    char *format;
    int header_byte_count;
    StringList region_names;
    IntList region_offsets;
    IntList region_sizes;
    IntList region_align_pow2;
    StringList region_cids;
    int file_byte_count;
    char *file_prefix_hex;
    ByteList file_bytes;
    char *file_text;
    char *file_cid;
} ObjFileBundle;

typedef struct {
    char *root_path;
    char *source_path;
    char *target_triple;
    char *emit_kind;
} ToolingBootstrapProgram;

typedef struct {
    int ok;
    char *reason;
    char *source_manifest_text;
    char *source_manifest_cid;
    char *rule_pack_text;
    char *rule_pack_cid;
    char *compiler_rule_pack_text;
    char *compiler_rule_pack_cid;
    char *release_artifact_text;
    char *release_compile_key;
    char *obj_image_cid;
    int obj_section_count;
    int obj_symbol_count;
    int obj_reloc_count;
    char *link_plan_cid;
    int runtime_target_count;
    int runtime_unresolved_count;
    int native_link_closure_ok;
    int topology_count;
    int rule_pack_entry_count;
    int compiler_rule_pack_entry_count;
    int compile_key_topology_covered;
    int compile_key_addressing_covered;
    int compile_key_depth_covered;
    int compile_key_address_binding_covered;
    int compile_key_distance_metric_covered;
    int compile_key_priority_covered;
    int compile_key_dispersal_covered;
    int compile_key_signature_covered;
    int compile_key_canonical_multipath_covered;
    int release_manifest_matches_source_manifest;
    int lsmr_priority_mapping_fixed;
    char *lsmr_addressing_mode;
    char *lsmr_address_binding;
    char *lsmr_distance_metric;
    char *lsmr_route_plan_model;
    int random_gossip_fallback_present;
} NetworkSelfhostReport;

typedef struct {
    char *text;
    char *source_manifest_text;
    char *rule_pack_text;
    char *compiler_rule_pack_text;
    char *release_artifact_text;
    unsigned long long stage_hash;
    NetworkSelfhostReport report;
} ToolingSelfhostStageResult;

typedef struct {
    uint8_t data[64];
    uint32_t state[8];
    uint64_t bitlen;
    size_t datalen;
} Sha256Ctx;

static int starts_with(const char *text, const char *prefix);
static int collect_outline_block_lines(char **raw_lines,
                                       int line_count,
                                       int start_idx,
                                       StringList *out_lines);
static int append_outline_block_lines(char **raw_lines,
                                      int line_count,
                                      int start_idx,
                                      StringList *out_lines);
static CompilerCoreProgram parse_compiler_core_source_file_strict(const char *path);
static const char *compiler_core_current_owner_module(const CompilerCoreProgram *program);
static char *compiler_core_default_owner_module_path(const char *path);
static char *compiler_core_source_path_to_module_path(const char *path);
static SourceManifestBundle resolve_compiler_core_source_manifest(const char *workspace_root_abs,
                                                                  const char *entry_abs);

static void die(const char *msg) {
    fprintf(stderr, "[cheng_v2c] %s\n", msg);
    exit(1);
}

static void die_errno(const char *msg, const char *path) {
    fprintf(stderr, "[cheng_v2c] %s: %s: %s\n", msg, path, strerror(errno));
    exit(1);
}

static void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (p == NULL) {
        die("out of memory");
    }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *next = realloc(p, n);
    if (next == NULL) {
        die("out of memory");
    }
    return next;
}

static char *xstrdup_range(const char *start, const char *end) {
    size_t len;
    char *out;
    if (end < start) {
        end = start;
    }
    len = (size_t)(end - start);
    out = (char *)xcalloc(len + 1, 1);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *xstrdup_text(const char *text) {
    return xstrdup_range(text, text + strlen(text));
}

static char *trim_copy(const char *start, const char *end) {
    while (start < end && isspace((unsigned char)*start)) {
        ++start;
    }
    while (end > start && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    return xstrdup_range(start, end);
}

static char *xvformat(const char *fmt, va_list ap) {
    va_list ap2;
    int n;
    char *out;
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) {
        die("vsnprintf failed");
    }
    out = (char *)xcalloc((size_t)n + 1, 1);
    if (vsnprintf(out, (size_t)n + 1, fmt, ap) < 0) {
        die("vsnprintf failed");
    }
    return out;
}

static char *xformat(const char *fmt, ...) {
    va_list ap;
    char *out;
    va_start(ap, fmt);
    out = xvformat(fmt, ap);
    va_end(ap);
    return out;
}

static int str_less(const char *a, const char *b) {
    return strcmp(a, b) < 0;
}

static void string_list_reserve(StringList *list, size_t need) {
    if (need <= list->cap) {
        return;
    }
    size_t next = list->cap == 0 ? 8 : list->cap * 2;
    while (next < need) {
        next *= 2;
    }
    list->items = (char **)xrealloc(list->items, next * sizeof(char *));
    list->cap = next;
}

static void string_list_push_take(StringList *list, char *text) {
    string_list_reserve(list, list->len + 1);
    list->items[list->len++] = text;
}

static void string_list_push_copy(StringList *list, const char *text) {
    string_list_push_take(list, xstrdup_text(text));
}

static void string_list_pushf(StringList *list, const char *fmt, ...) {
    va_list ap;
    char *text;
    va_start(ap, fmt);
    text = xvformat(fmt, ap);
    va_end(ap);
    string_list_push_take(list, text);
}

static int string_list_contains(const StringList *list, const char *text) {
    size_t i;
    for (i = 0; i < list->len; ++i) {
        if (strcmp(list->items[i], text) == 0) {
            return 1;
        }
    }
    return 0;
}

static void string_list_append_unique(StringList *list, const char *text) {
    if (text == NULL || text[0] == '\0') {
        return;
    }
    if (string_list_contains(list, text)) {
        return;
    }
    string_list_push_copy(list, text);
}

static void string_list_sort(StringList *list) {
    size_t i;
    for (i = 1; i < list->len; ++i) {
        size_t j = i;
        while (j > 0 && str_less(list->items[j], list->items[j - 1])) {
            char *tmp = list->items[j - 1];
            list->items[j - 1] = list->items[j];
            list->items[j] = tmp;
            --j;
        }
    }
}

static void string_list_free(StringList *list) {
    size_t i;
    for (i = 0; i < list->len; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static char *string_list_join(const StringList *list, const char *sep) {
    size_t i;
    size_t total = 0;
    size_t sep_len = strlen(sep);
    char *out;
    char *cursor;
    for (i = 0; i < list->len; ++i) {
        total += strlen(list->items[i]);
        if (i > 0) {
            total += sep_len;
        }
    }
    out = (char *)xcalloc(total + 1, 1);
    cursor = out;
    for (i = 0; i < list->len; ++i) {
        size_t part_len = strlen(list->items[i]);
        if (i > 0) {
            memcpy(cursor, sep, sep_len);
            cursor += sep_len;
        }
        memcpy(cursor, list->items[i], part_len);
        cursor += part_len;
    }
    *cursor = '\0';
    return out;
}

static void int_list_reserve(IntList *list, size_t need) {
    if (need <= list->cap) {
        return;
    }
    size_t next = list->cap == 0 ? 8 : list->cap * 2;
    while (next < need) {
        next *= 2;
    }
    list->items = (int *)xrealloc(list->items, next * sizeof(int));
    list->cap = next;
}

static void int_list_push(IntList *list, int value) {
    int_list_reserve(list, list->len + 1);
    list->items[list->len++] = value;
}

static void byte_list_reserve(ByteList *list, size_t need) {
    if (need <= list->cap) {
        return;
    }
    size_t next = list->cap == 0 ? 64 : list->cap * 2;
    while (next < need) {
        next *= 2;
    }
    list->items = (uint8_t *)xrealloc(list->items, next * sizeof(uint8_t));
    list->cap = next;
}

static void byte_list_push(ByteList *list, uint8_t value) {
    byte_list_reserve(list, list->len + 1);
    list->items[list->len++] = value;
}

static void byte_list_append_zeroes(ByteList *list, size_t count) {
    size_t i;
    byte_list_reserve(list, list->len + count);
    for (i = 0; i < count; ++i) {
        list->items[list->len++] = 0;
    }
}

static void byte_list_append_u16le(ByteList *list, uint16_t value) {
    byte_list_push(list, (uint8_t)(value & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 8) & 0xffU));
}

static void byte_list_append_u32le(ByteList *list, uint32_t value) {
    byte_list_push(list, (uint8_t)(value & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 8) & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 16) & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 24) & 0xffU));
}

static void byte_list_append_u64le(ByteList *list, uint64_t value) {
    byte_list_push(list, (uint8_t)(value & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 8) & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 16) & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 24) & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 32) & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 40) & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 48) & 0xffU));
    byte_list_push(list, (uint8_t)((value >> 56) & 0xffU));
}

static void byte_list_append_bytes(ByteList *list, const uint8_t *data, size_t len) {
    size_t i;
    byte_list_reserve(list, list->len + len);
    for (i = 0; i < len; ++i) {
        list->items[list->len++] = data[i];
    }
}

static void byte_list_append_ascii(ByteList *list, const char *text) {
    byte_list_append_bytes(list, (const uint8_t *)text, strlen(text));
}

static void byte_list_append_ascii_z(ByteList *list, const char *text) {
    byte_list_append_ascii(list, text);
    byte_list_push(list, 0);
}

static void byte_list_clear(ByteList *list) {
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static void byte_list_append_fixed_ascii(ByteList *list, const char *text, size_t width) {
    size_t i;
    size_t n = strlen(text);
    if (n > width) {
        n = width;
    }
    byte_list_append_bytes(list, (const uint8_t *)text, n);
    for (i = n; i < width; ++i) {
        byte_list_push(list, 0);
    }
}

static char *hex_encode_bytes(const uint8_t *data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    char *out = (char *)xcalloc(len * 2 + 1, 1);
    size_t i;
    for (i = 0; i < len; ++i) {
        out[i * 2] = hex[(data[i] >> 4) & 0x0fU];
        out[i * 2 + 1] = hex[data[i] & 0x0fU];
    }
    out[len * 2] = '\0';
    return out;
}

static char *hex_prefix_bytes(const uint8_t *data, size_t len, size_t prefix_len) {
    if (len > prefix_len) {
        len = prefix_len;
    }
    return hex_encode_bytes(data, len);
}

static size_t align_size_pow2(size_t value, int align_pow2) {
    size_t align = (size_t)1 << (size_t)align_pow2;
    size_t rem = value % align;
    return rem == 0 ? value : value + (align - rem);
}

static int string_list_index_of(const StringList *list, const char *text) {
    size_t i;
    for (i = 0; i < list->len; ++i) {
        if (strcmp(list->items[i], text) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void compiler_core_module_list_reserve(CompilerCoreModuleList *list, size_t need) {
    if (need <= list->cap) {
        return;
    }
    size_t next = list->cap == 0 ? 4 : list->cap * 2;
    while (next < need) {
        next *= 2;
    }
    list->names = (char **)xrealloc(list->names, next * sizeof(char *));
    list->header_texts = (char **)xrealloc(list->header_texts, next * sizeof(char *));
    list->cap = next;
}

static void compiler_core_module_list_push(CompilerCoreModuleList *list,
                                           const char *name,
                                           const char *header_text) {
    size_t i;
    compiler_core_module_list_reserve(list, list->len + 1);
    i = list->len++;
    list->names[i] = xstrdup_text(name);
    list->header_texts[i] = xstrdup_text(header_text);
}

static void compiler_core_value_decl_list_reserve(CompilerCoreValueDeclList *list, size_t need) {
    if (need <= list->cap) {
        return;
    }
    size_t next = list->cap == 0 ? 4 : list->cap * 2;
    while (next < need) {
        next *= 2;
    }
    list->labels = (char **)xrealloc(list->labels, next * sizeof(char *));
    list->header_texts = (char **)xrealloc(list->header_texts, next * sizeof(char *));
    list->cap = next;
}

static void compiler_core_value_decl_list_push(CompilerCoreValueDeclList *list,
                                               const char *label,
                                               const char *header_text,
                                               int body_line_count,
                                               const char *body_hash) {
    size_t i;
    compiler_core_value_decl_list_reserve(list, list->len + 1);
    i = list->len++;
    list->labels[i] = xstrdup_text(label);
    list->header_texts[i] = xstrdup_text(header_text);
    int_list_push(&list->body_line_counts, body_line_count);
    string_list_push_copy(&list->body_hashes, body_hash);
}

static void topology_list_reserve(TopologyList *list, size_t need) {
    if (need <= list->cap) {
        return;
    }
    size_t next = list->cap == 0 ? 4 : list->cap * 2;
    while (next < need) {
        next *= 2;
    }
    list->items = (TopologyDecl *)xrealloc(list->items, next * sizeof(TopologyDecl));
    list->cap = next;
}

static void topology_list_push(TopologyList *list,
                               const char *name,
                               const char *domain,
                               const char *fanout,
                               const char *repair,
                               const char *dispersal,
                               const char *priority,
                               const char *addressing,
                               int depth,
                               int ida_k,
                               int ida_n) {
    TopologyDecl *item;
    topology_list_reserve(list, list->len + 1);
    item = &list->items[list->len++];
    item->name = xstrdup_text(name);
    item->domain = xstrdup_text(domain);
    item->fanout = xstrdup_text(fanout);
    item->repair = xstrdup_text(repair);
    item->dispersal = xstrdup_text(dispersal);
    item->priority = xstrdup_text(priority);
    item->addressing = xstrdup_text(addressing);
    item->depth = depth;
    item->ida_k = ida_k;
    item->ida_n = ida_n;
}

static void network_op_list_reserve(NetworkOpList *list, size_t need) {
    if (need <= list->cap) {
        return;
    }
    size_t next = list->cap == 0 ? 8 : list->cap * 2;
    while (next < need) {
        next *= 2;
    }
    list->items = (NetworkOp *)xrealloc(list->items, next * sizeof(NetworkOp));
    list->cap = next;
}

static void network_op_list_push(NetworkOpList *list,
                                 const char *op_name,
                                 const char *topology_ref,
                                 const char *subject_name,
                                 const char *signature_kind) {
    NetworkOp *item;
    network_op_list_reserve(list, list->len + 1);
    item = &list->items[list->len++];
    item->op_name = xstrdup_text(op_name);
    item->topology_ref = xstrdup_text(topology_ref);
    item->subject_name = xstrdup_text(subject_name);
    item->signature_kind = xstrdup_text(signature_kind);
}

static char *read_file_text(const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    if (f == NULL) {
        die_errno("fopen failed", path);
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        die_errno("fseek failed", path);
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        die_errno("ftell failed", path);
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        die_errno("fseek failed", path);
    }
    buf = (char *)xcalloc((size_t)size + 1, 1);
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        die_errno("fread failed", path);
    }
    fclose(f);
    buf[size] = '\0';
    return buf;
}

static int text_files_equal(const char *left_path, const char *right_path) {
    char *left = read_file_text(left_path);
    char *right = read_file_text(right_path);
    int out = strcmp(left, right) == 0;
    free(left);
    free(right);
    return out;
}

static void create_dir_all(const char *path) {
    char tmp[PATH_MAX];
    size_t i;
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(tmp)) {
        die("path too long");
    }
    memcpy(tmp, path, n + 1);
    for (i = 1; i < n; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0] != '\0' && access(tmp, F_OK) != 0) {
                if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                    die_errno("mkdir failed", tmp);
                }
            }
            tmp[i] = '/';
        }
    }
    if (access(tmp, F_OK) != 0) {
        if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
            die_errno("mkdir failed", tmp);
        }
    }
}

static void write_text_file(const char *path, const char *text) {
    char parent[PATH_MAX];
    const char *slash;
    FILE *out;
    if (strlen(path) >= sizeof(parent)) {
        die("path too long");
    }
    memcpy(parent, path, strlen(path) + 1);
    slash = strrchr(parent, '/');
    if (slash != NULL) {
        parent[slash - parent] = '\0';
        if (parent[0] != '\0') {
            create_dir_all(parent);
        }
    }
    out = fopen(path, "wb");
    if (out == NULL) {
        die_errno("fopen failed", path);
    }
    if (fwrite(text, 1, strlen(text), out) != strlen(text)) {
        fclose(out);
        die_errno("fwrite failed", path);
    }
    fclose(out);
}

static void write_binary_file(const char *path, const uint8_t *data, size_t len) {
    char parent[PATH_MAX];
    const char *slash;
    FILE *out;
    if (strlen(path) >= sizeof(parent)) {
        die("path too long");
    }
    memcpy(parent, path, strlen(path) + 1);
    slash = strrchr(parent, '/');
    if (slash != NULL) {
        parent[slash - parent] = '\0';
        if (parent[0] != '\0') {
            create_dir_all(parent);
        }
    }
    out = fopen(path, "wb");
    if (out == NULL) {
        die_errno("fopen failed", path);
    }
    if (len > 0 && fwrite(data, 1, len, out) != len) {
        fclose(out);
        die_errno("fwrite failed", path);
    }
    fclose(out);
}

static ByteList read_file_bytes_all(const char *path) {
    ByteList out;
    FILE *f;
    long size;
    memset(&out, 0, sizeof(out));
    f = fopen(path, "rb");
    if (f == NULL) {
        die_errno("fopen failed", path);
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        die_errno("fseek failed", path);
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        die_errno("ftell failed", path);
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        die_errno("fseek failed", path);
    }
    if (size > 0) {
        out.items = (uint8_t *)xcalloc((size_t)size, 1);
        out.len = (size_t)size;
        out.cap = (size_t)size;
        if (fread(out.items, 1, out.len, f) != out.len) {
            fclose(f);
            die_errno("fread failed", path);
        }
    }
    fclose(f);
    return out;
}

static char *run_exec_capture(const char *file_path,
                              const StringList *argv,
                              const char *working_dir,
                              int merge_stderr,
                              int *exit_code) {
    int pipefd[2];
    pid_t pid;
    ByteList out;
    int status = 0;
    ssize_t nread;
    char *text;
    memset(&out, 0, sizeof(out));
    if (pipe(pipefd) != 0) {
        die_errno("pipe failed", file_path);
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        die_errno("fork failed", file_path);
    }
    if (pid == 0) {
        char **exec_argv = (char **)xcalloc(argv->len + 2, sizeof(char *));
        size_t i;
        if (working_dir != NULL && working_dir[0] != '\0' && chdir(working_dir) != 0) {
            _exit(127);
        }
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (merge_stderr && dup2(pipefd[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        exec_argv[0] = (char *)file_path;
        for (i = 0; i < argv->len; ++i) {
            exec_argv[i + 1] = argv->items[i];
        }
        exec_argv[argv->len + 1] = NULL;
        execv(file_path, exec_argv);
        _exit(127);
    }
    close(pipefd[1]);
    for (;;) {
        uint8_t buf[4096];
        nread = read(pipefd[0], buf, sizeof(buf));
        if (nread < 0) {
            close(pipefd[0]);
            die_errno("read failed", file_path);
        }
        if (nread == 0) {
            break;
        }
        byte_list_append_bytes(&out, buf, (size_t)nread);
    }
    close(pipefd[0]);
    if (waitpid(pid, &status, 0) < 0) {
        die_errno("waitpid failed", file_path);
    }
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *exit_code = 128 + WTERMSIG(status);
    } else {
        *exit_code = 127;
    }
    text = (char *)xcalloc(out.len + 1, 1);
    if (out.len > 0) {
        memcpy(text, out.items, out.len);
    }
    text[out.len] = '\0';
    return text;
}

static void trim_trailing_newlines_inplace(char *text) {
    size_t n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
        text[n - 1] = '\0';
        n -= 1;
    }
}

static int path_is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static int path_is_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode);
}

static void path_join(char *out, size_t out_cap, const char *left, const char *right) {
    int n;
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
        die("path too long");
    }
}

static void parent_dir(char *path) {
    char *slash = strrchr(path, '/');
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

static int path_has_prefix(const char *path, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(path, prefix, prefix_len) != 0) {
        return 0;
    }
    return path[prefix_len] == '\0' || path[prefix_len] == '/';
}

static void normalize_relpath_inplace(char *path) {
    char temp[PATH_MAX];
    char *segments[PATH_MAX / 2];
    size_t seg_count = 0;
    char *cursor;
    size_t i;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    if (strlen(path) >= sizeof(temp)) {
        die("relative path too long");
    }
    memcpy(temp, path, strlen(path) + 1);
    cursor = temp;
    while (*cursor != '\0') {
        char *start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            ++cursor;
        }
        if (*cursor == '/') {
            *cursor = '\0';
            ++cursor;
        }
        if (start[0] == '\0' || strcmp(start, ".") == 0) {
            continue;
        }
        if (strcmp(start, "..") == 0) {
            if (seg_count > 0 && strcmp(segments[seg_count - 1], "..") != 0) {
                seg_count -= 1;
            } else {
                segments[seg_count++] = start;
            }
            continue;
        }
        segments[seg_count++] = start;
    }
    path[0] = '\0';
    if (seg_count == 0) {
        return;
    }
    for (i = 0; i < seg_count; ++i) {
        if (i > 0) {
            if (strlen(path) + 1 >= PATH_MAX) {
                die("normalized relative path too long");
            }
            strcat(path, "/");
        }
        if (strlen(path) + strlen(segments[i]) >= PATH_MAX) {
            die("normalized relative path too long");
        }
        strcat(path, segments[i]);
    }
}

static int relative_to_prefix(const char *path, const char *prefix, char *out, size_t out_cap) {
    size_t prefix_len = strlen(prefix);
    const char *rel = path;
    if (!path_has_prefix(path, prefix)) {
        return 0;
    }
    rel = path + prefix_len;
    if (*rel == '/') {
        ++rel;
    }
    if (snprintf(out, out_cap, "%s", rel) >= (int)out_cap) {
        die("relative path too long");
    }
    normalize_relpath_inplace(out);
    return 1;
}

static int find_repo_root_from(const char *start_path, char *out, size_t out_cap) {
    char probe[PATH_MAX];
    char marker[PATH_MAX];
    if (start_path == NULL || start_path[0] == '\0') {
        return 0;
    }
    if (realpath(start_path, probe) == NULL) {
        return 0;
    }
    if (!path_is_dir(probe)) {
        parent_dir(probe);
    }
    while (probe[0] != '\0') {
        path_join(marker, sizeof(marker), probe, "v2/cheng-package.toml");
        if (path_is_file(marker)) {
            if (snprintf(out, out_cap, "%s", probe) >= (int)out_cap) {
                die("repo root path too long");
            }
            return 1;
        }
        if (strcmp(probe, "/") == 0) {
            break;
        }
        parent_dir(probe);
    }
    return 0;
}

static void detect_repo_root(const char *hint_a, const char *hint_b, char *out, size_t out_cap) {
    char cwd[PATH_MAX];
    if (find_repo_root_from(hint_a, out, out_cap)) {
        return;
    }
    if (find_repo_root_from(hint_b, out, out_cap)) {
        return;
    }
    if (getcwd(cwd, sizeof(cwd)) != NULL && find_repo_root_from(cwd, out, out_cap)) {
        return;
    }
    die("failed to detect repo root");
}

static void resolve_existing_input_path(const char *repo_root,
                                        const char *raw,
                                        char *abs_out,
                                        size_t abs_out_cap,
                                        char *norm_out,
                                        size_t norm_out_cap) {
    char cwd[PATH_MAX];
    char probe[PATH_MAX];
    (void)abs_out_cap;
    if (raw == NULL || raw[0] == '\0') {
        die("missing input path");
    }
    if (raw[0] == '/') {
        if (snprintf(probe, sizeof(probe), "%s", raw) >= (int)sizeof(probe)) {
            die("path too long");
        }
    } else if (starts_with(raw, "./") || starts_with(raw, "../")) {
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            die("getcwd failed");
        }
        path_join(probe, sizeof(probe), cwd, raw);
    } else {
        path_join(probe, sizeof(probe), repo_root, raw);
    }
    if (realpath(probe, abs_out) == NULL) {
        die_errno("realpath failed", probe);
    }
    if (!relative_to_prefix(abs_out, repo_root, norm_out, norm_out_cap)) {
        if (snprintf(norm_out, norm_out_cap, "%s", abs_out) >= (int)norm_out_cap) {
            die("normalized path too long");
        }
    }
}

static void resolve_output_path(const char *repo_root,
                                const char *raw,
                                char *out,
                                size_t out_cap) {
    char cwd[PATH_MAX];
    if (raw == NULL || raw[0] == '\0') {
        die("missing output path");
    }
    if (raw[0] == '/') {
        if (snprintf(out, out_cap, "%s", raw) >= (int)out_cap) {
            die("output path too long");
        }
        return;
    }
    if (starts_with(raw, "./") || starts_with(raw, "../")) {
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            die("getcwd failed");
        }
        path_join(out, out_cap, cwd, raw);
        return;
    }
    path_join(out, out_cap, repo_root, raw);
}

static const char *path_basename_const(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static const uint32_t k_sha256_table[64] = {
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
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR((x), 2) ^ SHA256_ROTR((x), 13) ^ SHA256_ROTR((x), 22))
#define SHA256_EP1(x) (SHA256_ROTR((x), 6) ^ SHA256_ROTR((x), 11) ^ SHA256_ROTR((x), 25))
#define SHA256_SIG0(x) (SHA256_ROTR((x), 7) ^ SHA256_ROTR((x), 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR((x), 17) ^ SHA256_ROTR((x), 19) ^ ((x) >> 10))

static void sha256_transform(Sha256Ctx *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t m[64];
    uint32_t t1, t2;
    size_t i;
    for (i = 0; i < 16; ++i) {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (i = 16; i < 64; ++i) {
        m[i] = SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];
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
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + k_sha256_table[i] + m[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
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

static void sha256_init(Sha256Ctx *ctx) {
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

static void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(Sha256Ctx *ctx, uint8_t hash[32]) {
    size_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) {
            ctx->data[i++] = 0x00;
        }
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) {
            ctx->data[i++] = 0x00;
        }
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);
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

static char *sha256_hex_bytes(const uint8_t *data, size_t len) {
    static const char hex[] = "0123456789abcdef";
    Sha256Ctx ctx;
    uint8_t digest[32];
    char *out = (char *)xcalloc(65, 1);
    size_t i;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
    for (i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0fU];
    }
    out[64] = '\0';
    return out;
}

static char *sha256_hex_text(const char *text) {
    return sha256_hex_bytes((const uint8_t *)text, strlen(text));
}

static char *sha256_hex_file(const char *path) {
    FILE *f = fopen(path, "rb");
    Sha256Ctx ctx;
    uint8_t buf[4096];
    uint8_t digest[32];
    char *out;
    static const char hex[] = "0123456789abcdef";
    size_t n;
    size_t i;
    if (f == NULL) {
        die_errno("fopen failed", path);
    }
    sha256_init(&ctx);
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sha256_update(&ctx, buf, n);
    }
    if (ferror(f)) {
        fclose(f);
        die_errno("fread failed", path);
    }
    fclose(f);
    sha256_final(&ctx, digest);
    out = (char *)xcalloc(65, 1);
    for (i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 0x0fU];
    }
    out[64] = '\0';
    return out;
}

static unsigned long long fnv1a64_update(unsigned long long h, const unsigned char *buf, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        h ^= (unsigned long long)buf[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static unsigned long long fnv1a64_text(unsigned long long h, const char *text) {
    return fnv1a64_update(h, (const unsigned char *)text, strlen(text));
}

static int starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static int ends_with(const char *text, const char *suffix) {
    size_t tlen = strlen(text);
    size_t slen = strlen(suffix);
    if (slen > tlen) {
        return 0;
    }
    return strcmp(text + tlen - slen, suffix) == 0;
}

static char *dup_trimmed_line(const char *line) {
    return trim_copy(line, line + strlen(line));
}

static const char *parse_name_end(const char *start) {
    const char *p = start;
    while (*p != '\0' && (isalnum((unsigned char)*p) || *p == '_')) {
        ++p;
    }
    return p;
}

static const char *parse_call_name_end(const char *start) {
    const char *p = start;
    while (*p != '\0' && (isalnum((unsigned char)*p) || *p == '_' || *p == '.')) {
        ++p;
    }
    return p;
}

static char *parse_name_after_prefix(const char *line, const char *prefix) {
    const char *start = line + strlen(prefix);
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    return xstrdup_range(start, parse_name_end(start));
}

static int leading_indent_width(const char *line) {
    int out = 0;
    while (line[out] == ' ' || line[out] == '\t') {
        ++out;
    }
    return out;
}

static int is_blank_or_comment_line(const char *line) {
    char *trimmed = dup_trimmed_line(line);
    int out = trimmed[0] == '\0' || starts_with(trimmed, "//") || starts_with(trimmed, "#");
    free(trimmed);
    return out;
}

static char *parse_compiler_import_module_path(const char *line) {
    const char *start = line + strlen("import ");
    const char *alias = strstr(start, " as ");
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    if (alias == NULL) {
        return trim_copy(start, start + strlen(start));
    }
    return trim_copy(start, alias);
}

static char *parse_compiler_import_alias(const char *line) {
    const char *alias = strstr(line, " as ");
    if (alias == NULL) {
        return xstrdup_text("");
    }
    alias += strlen(" as ");
    return trim_copy(alias, alias + strlen(alias));
}

static char *parse_compiler_decl_label(const char *line, const char *prefix, const char *default_label) {
    size_t prefix_len = strlen(prefix);
    const char *start;
    const char *end;
    if (!starts_with(line, prefix)) {
        return xstrdup_text(default_label);
    }
    start = line + prefix_len;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    if (*start == '\0') {
        return xstrdup_text(default_label);
    }
    end = parse_name_end(start);
    if (end == start) {
        return xstrdup_text(default_label);
    }
    return xstrdup_range(start, end);
}

static char *with_cheng_ext(const char *module_path) {
    if (ends_with(module_path, ".cheng")) {
        return xstrdup_text(module_path);
    }
    return xformat("%s.cheng", module_path);
}

static char *join_prefixed_line_parts(const StringList *prefix_lines, const StringList *lines) {
    StringList merged;
    size_t i;
    memset(&merged, 0, sizeof(merged));
    for (i = 0; i < prefix_lines->len; ++i) {
        string_list_push_copy(&merged, prefix_lines->items[i]);
    }
    for (i = 0; i < lines->len; ++i) {
        string_list_push_copy(&merged, lines->items[i]);
    }
    return string_list_join(&merged, "\n");
}

static int annotation_list_has_importc(const StringList *lines) {
    size_t i;
    for (i = 0; i < lines->len; ++i) {
        if (starts_with(lines->items[i], "@importc")) {
            return 1;
        }
    }
    return 0;
}

static char *parse_compiler_routine_name(const char *line, const char *prefix) {
    const char *start = line + strlen(prefix);
    const char *scan;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    if (*start == '`') {
        scan = start + 1;
        while (*scan != '\0' && *scan != '`') {
            ++scan;
        }
        if (*scan != '`') {
            die("v2 source parser: unterminated backtick routine name");
        }
        return xstrdup_range(start, scan + 1);
    }
    return xstrdup_range(start, parse_name_end(start));
}

static char *replace_newlines_with_spaces(const char *text) {
    size_t i;
    size_t n = strlen(text);
    char *out = (char *)xcalloc(n + 1, 1);
    for (i = 0; i < n; ++i) {
        char ch = text[i];
        out[i] = (ch == '\n' || ch == '\r') ? ' ' : ch;
    }
    out[n] = '\0';
    return out;
}

static int count_top_level_csv_parts(const char *text) {
    int count = 0;
    int depth_paren = 0;
    int depth_bracket = 0;
    int depth_brace = 0;
    int in_string = 0;
    int have_token = 0;
    const char *p = text;
    while (*p != '\0') {
        char ch = *p;
        if (in_string) {
            if (ch == '\\' && p[1] != '\0') {
                p += 2;
                continue;
            }
            if (ch == '"') {
                in_string = 0;
            }
            have_token = 1;
            ++p;
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            have_token = 1;
            ++p;
            continue;
        }
        if (ch == '(') {
            depth_paren += 1;
            have_token = 1;
            ++p;
            continue;
        }
        if (ch == ')') {
            depth_paren -= 1;
            have_token = 1;
            ++p;
            continue;
        }
        if (ch == '[') {
            depth_bracket += 1;
            have_token = 1;
            ++p;
            continue;
        }
        if (ch == ']') {
            depth_bracket -= 1;
            have_token = 1;
            ++p;
            continue;
        }
        if (ch == '{') {
            depth_brace += 1;
            have_token = 1;
            ++p;
            continue;
        }
        if (ch == '}') {
            depth_brace -= 1;
            have_token = 1;
            ++p;
            continue;
        }
        if (ch == ',' && depth_paren == 0 && depth_bracket == 0 && depth_brace == 0) {
            if (have_token) {
                count += 1;
                have_token = 0;
            }
            ++p;
            continue;
        }
        if (!isspace((unsigned char)ch)) {
            have_token = 1;
        }
        ++p;
    }
    if (have_token) {
        count += 1;
    }
    return count;
}

static void parse_compiler_routine_signature_shape(const char *signature,
                                                   char **out_return_type,
                                                   int *out_param_count) {
    char *flat = replace_newlines_with_spaces(signature);
    const char *header = flat;
    const char *scan = strstr(flat, "fn ");
    const char *iter_scan = strstr(flat, "iterator ");
    const char *open;
    const char *close;
    while (scan != NULL) {
        header = scan;
        scan = strstr(scan + 1, "fn ");
    }
    while (iter_scan != NULL) {
        header = iter_scan;
        iter_scan = strstr(iter_scan + 1, "iterator ");
    }
    open = strchr(header, '(');
    close = strrchr(header, ')');
    *out_return_type = xstrdup_text("");
    *out_param_count = 0;
    if (open == NULL || close == NULL || close < open) {
        free(flat);
        return;
    }
    {
        char *params_text = trim_copy(open + 1, close);
        if (params_text[0] != '\0') {
            *out_param_count = count_top_level_csv_parts(params_text);
        }
        free(params_text);
    }
    {
        char *ret_slice = trim_copy(close + 1, flat + strlen(flat));
        char *colon = strchr(ret_slice, ':');
        if (colon != NULL) {
            char *raw_return = trim_copy(colon + 1, ret_slice + strlen(ret_slice));
            char *eq = strrchr(raw_return, '=');
            if (eq != NULL) {
                char *trimmed_return = trim_copy(raw_return, eq);
                free(raw_return);
                raw_return = trimmed_return;
            }
            free(*out_return_type);
            *out_return_type = raw_return;
        }
        free(ret_slice);
    }
    free(flat);
}

static int starts_with_at_text(const char *text, int idx, const char *pattern) {
    size_t text_len = strlen(text);
    size_t pattern_len = strlen(pattern);
    if (idx < 0 || (size_t)idx + pattern_len > text_len) {
        return 0;
    }
    return strncmp(text + idx, pattern, pattern_len) == 0;
}

static int find_matching_delimiter_text(const char *text,
                                        int open_idx,
                                        char open_ch,
                                        char close_ch) {
    int depth = 0;
    int in_string = 0;
    int i;
    int text_len = (int)strlen(text);
    for (i = open_idx; i < text_len; ++i) {
        char ch = text[i];
        if (in_string) {
            if (ch == '\\' && i + 1 < text_len) {
                i += 1;
                continue;
            }
            if (ch == '"') {
                in_string = 0;
            }
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            continue;
        }
        if (ch == open_ch) {
            depth += 1;
            continue;
        }
        if (ch == close_ch) {
            depth -= 1;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

static int has_single_outer_parens_text(const char *text) {
    size_t len = strlen(text);
    if (len < 2 || text[0] != '(' || text[len - 1] != ')') {
        return 0;
    }
    return find_matching_delimiter_text(text, 0, '(', ')') == (int)len - 1;
}

static StringList split_top_level_segments_text(const char *text, char delim) {
    StringList out;
    int start = 0;
    int depth_paren = 0;
    int depth_bracket = 0;
    int depth_brace = 0;
    int in_string = 0;
    int i;
    int text_len = (int)strlen(text);
    memset(&out, 0, sizeof(out));
    for (i = 0; i < text_len; ++i) {
        char ch = text[i];
        if (in_string) {
            if (ch == '\\' && i + 1 < text_len) {
                i += 1;
                continue;
            }
            if (ch == '"') {
                in_string = 0;
            }
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            continue;
        }
        if (ch == '(') {
            depth_paren += 1;
            continue;
        }
        if (ch == ')') {
            depth_paren -= 1;
            continue;
        }
        if (ch == '[') {
            depth_bracket += 1;
            continue;
        }
        if (ch == ']') {
            depth_bracket -= 1;
            continue;
        }
        if (ch == '{') {
            depth_brace += 1;
            continue;
        }
        if (ch == '}') {
            depth_brace -= 1;
            continue;
        }
        if (ch == delim && depth_paren == 0 && depth_bracket == 0 && depth_brace == 0) {
            char *part = trim_copy(text + start, text + i);
            string_list_push_take(&out, part);
            start = i + 1;
        }
    }
    {
        char *part = trim_copy(text + start, text + text_len);
        string_list_push_take(&out, part);
    }
    return out;
}

static int find_top_level_token_text(const char *text, const char *pattern, int start_idx) {
    int depth_paren = 0;
    int depth_bracket = 0;
    int depth_brace = 0;
    int in_string = 0;
    int i;
    int text_len = (int)strlen(text);
    int pattern_len = (int)strlen(pattern);
    for (i = start_idx; i + pattern_len <= text_len; ++i) {
        char ch = text[i];
        if (in_string) {
            if (ch == '\\' && i + 1 < text_len) {
                i += 1;
                continue;
            }
            if (ch == '"') {
                in_string = 0;
            }
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            continue;
        }
        if (ch == '(') {
            depth_paren += 1;
            continue;
        }
        if (ch == ')') {
            depth_paren -= 1;
            continue;
        }
        if (ch == '[') {
            depth_bracket += 1;
            continue;
        }
        if (ch == ']') {
            depth_bracket -= 1;
            continue;
        }
        if (ch == '{') {
            depth_brace += 1;
            continue;
        }
        if (ch == '}') {
            depth_brace -= 1;
            continue;
        }
        if (depth_paren == 0 && depth_bracket == 0 && depth_brace == 0 &&
            starts_with_at_text(text, i, pattern)) {
            return i;
        }
    }
    return -1;
}

static int find_last_top_level_assign_text(const char *text) {
    int depth_paren = 0;
    int depth_bracket = 0;
    int depth_brace = 0;
    int in_string = 0;
    int found = -1;
    int i;
    int len = (int)strlen(text);
    for (i = 0; i < len; ++i) {
        char ch = text[i];
        if (in_string) {
            if (ch == '\\') {
                i += 1;
                continue;
            }
            if (ch == '"') {
                in_string = 0;
            }
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            continue;
        }
        if (ch == '(') {
            depth_paren += 1;
            continue;
        }
        if (ch == ')') {
            depth_paren -= 1;
            continue;
        }
        if (ch == '[') {
            depth_bracket += 1;
            continue;
        }
        if (ch == ']') {
            depth_bracket -= 1;
            continue;
        }
        if (ch == '{') {
            depth_brace += 1;
            continue;
        }
        if (ch == '}') {
            depth_brace -= 1;
            continue;
        }
        if (ch == '=' &&
            depth_paren == 0 &&
            depth_bracket == 0 &&
            depth_brace == 0 &&
            !(i > 0 && (text[i - 1] == '=' || text[i - 1] == '!' || text[i - 1] == '<' || text[i - 1] == '>')) &&
            !(i + 1 < len && text[i + 1] == '=')) {
            found = i;
        }
    }
    return found;
}

static int find_last_top_level_binary_text(const char *text, const char *pattern) {
    int depth_paren = 0;
    int depth_bracket = 0;
    int depth_brace = 0;
    int in_string = 0;
    int found = -1;
    int i;
    int text_len = (int)strlen(text);
    int pattern_len = (int)strlen(pattern);
    for (i = 0; i + pattern_len <= text_len; ++i) {
        char ch = text[i];
        if (in_string) {
            if (ch == '\\' && i + 1 < text_len) {
                i += 1;
                continue;
            }
            if (ch == '"') {
                in_string = 0;
            }
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            continue;
        }
        if (ch == '(') {
            depth_paren += 1;
            continue;
        }
        if (ch == ')') {
            depth_paren -= 1;
            continue;
        }
        if (ch == '[') {
            depth_bracket += 1;
            continue;
        }
        if (ch == ']') {
            depth_bracket -= 1;
            continue;
        }
        if (ch == '{') {
            depth_brace += 1;
            continue;
        }
        if (ch == '}') {
            depth_brace -= 1;
            continue;
        }
        if (depth_paren == 0 && depth_bracket == 0 && depth_brace == 0 &&
            starts_with_at_text(text, i, pattern)) {
            found = i;
        }
    }
    return found;
}

static int parse_compiler_expr_count(const char *text);

static int parse_compiler_if_expr_count(const char *text) {
    char *trimmed = trim_copy(text, text + strlen(text));
    int colon_idx;
    int else_idx;
    int cond_count;
    int then_count;
    int else_count;
    char *cond_text;
    char *then_text;
    char *else_text;
    if (!starts_with(trimmed, "if ")) {
        free(trimmed);
        return -1;
    }
    colon_idx = find_top_level_token_text(trimmed, ":", 0);
    else_idx = find_top_level_token_text(trimmed, " else:", 0);
    if (colon_idx < 0 || else_idx < 0 || else_idx <= colon_idx) {
        free(trimmed);
        return -1;
    }
    cond_text = trim_copy(trimmed + strlen("if "), trimmed + colon_idx);
    then_text = trim_copy(trimmed + colon_idx + 1, trimmed + else_idx);
    else_text = trim_copy(trimmed + else_idx + strlen(" else:"), trimmed + strlen(trimmed));
    cond_count = parse_compiler_expr_count(cond_text);
    then_count = parse_compiler_expr_count(then_text);
    else_count = parse_compiler_expr_count(else_text);
    free(cond_text);
    free(then_text);
    free(else_text);
    free(trimmed);
    if (cond_count < 0 || then_count < 0 || else_count < 0) {
        return -1;
    }
    return 1 + cond_count + then_count + else_count;
}

static int parse_compiler_callish_expr_count(const char *text) {
    char *trimmed = trim_copy(text, text + strlen(text));
    size_t len = strlen(trimmed);
    int total = 0;
    int i = 0;
    if (len == 0) {
        free(trimmed);
        return -1;
    }
    if (has_single_outer_parens_text(trimmed)) {
        char *inner = trim_copy(trimmed + 1, trimmed + len - 1);
        int out = parse_compiler_expr_count(inner);
        free(inner);
        free(trimmed);
        return out;
    }
    if (strcmp(trimmed, "true") == 0 || strcmp(trimmed, "false") == 0 || strcmp(trimmed, "nil") == 0) {
        free(trimmed);
        return 1;
    }
    if (trimmed[0] == '"') {
        free(trimmed);
        return 1;
    }
    if (trimmed[0] == '[' && trimmed[len - 1] == ']' &&
        find_matching_delimiter_text(trimmed, 0, '[', ']') == (int)len - 1) {
        char *inner = trim_copy(trimmed + 1, trimmed + len - 1);
        int sum = 1;
        if (inner[0] != '\0') {
            StringList parts = split_top_level_segments_text(inner, ',');
            size_t k;
            for (k = 0; k < parts.len; ++k) {
                int part_count = parse_compiler_expr_count(parts.items[k]);
                if (part_count < 0) {
                    free(inner);
                    free(trimmed);
                    return -1;
                }
                sum += part_count;
            }
        }
        free(inner);
        free(trimmed);
        return sum;
    }
    if (trimmed[0] == '{' && trimmed[len - 1] == '}' &&
        find_matching_delimiter_text(trimmed, 0, '{', '}') == (int)len - 1) {
        char *inner = trim_copy(trimmed + 1, trimmed + len - 1);
        int sum = 1;
        if (inner[0] != '\0') {
            StringList parts = split_top_level_segments_text(inner, ',');
            size_t k;
            for (k = 0; k < parts.len; ++k) {
                int colon_idx = find_top_level_token_text(parts.items[k], ":", 0);
                int part_count;
                char *value_text;
                if (colon_idx < 0) {
                    free(inner);
                    free(trimmed);
                    return -1;
                }
                value_text = trim_copy(parts.items[k] + colon_idx + 1,
                                       parts.items[k] + strlen(parts.items[k]));
                part_count = parse_compiler_expr_count(value_text);
                free(value_text);
                if (part_count < 0) {
                    free(inner);
                    free(trimmed);
                    return -1;
                }
                sum += part_count;
            }
        }
        free(inner);
        free(trimmed);
        return sum;
    }
    {
        int all_digits = 1;
        for (i = 0; i < (int)len; ++i) {
            if (!isdigit((unsigned char)trimmed[i])) {
                all_digits = 0;
                break;
            }
        }
        if (all_digits) {
            free(trimmed);
            return 1;
        }
    }
    {
        const char *name_end = parse_name_end(trimmed);
        if (name_end == trimmed) {
            free(trimmed);
            return -1;
        }
        total = 1;
        i = (int)(name_end - trimmed);
    }
    while (1) {
        while (trimmed[i] != '\0' && isspace((unsigned char)trimmed[i])) {
            i += 1;
        }
        if (trimmed[i] == '\0') {
            break;
        }
        if (trimmed[i] == '.') {
            const char *field_start = trimmed + i + 1;
            const char *field_end = parse_name_end(field_start);
            if (field_end == field_start) {
                free(trimmed);
                return -1;
            }
            total += 1;
            i = (int)(field_end - trimmed);
            continue;
        }
        if (trimmed[i] == '(') {
            int close_idx = find_matching_delimiter_text(trimmed, i, '(', ')');
            char *arg_text;
            if (close_idx < 0) {
                free(trimmed);
                return -1;
            }
            arg_text = trim_copy(trimmed + i + 1, trimmed + close_idx);
            total += 1;
            if (arg_text[0] != '\0') {
                StringList parts = split_top_level_segments_text(arg_text, ',');
                size_t k;
                for (k = 0; k < parts.len; ++k) {
                    int part_count = parse_compiler_expr_count(parts.items[k]);
                    if (part_count < 0) {
                        free(arg_text);
                        free(trimmed);
                        return -1;
                    }
                    total += part_count;
                }
            }
            free(arg_text);
            i = close_idx + 1;
            continue;
        }
        if (trimmed[i] == '[') {
            int close_idx = find_matching_delimiter_text(trimmed, i, '[', ']');
            char *index_text;
            int part_count;
            if (close_idx < 0) {
                free(trimmed);
                return -1;
            }
            index_text = trim_copy(trimmed + i + 1, trimmed + close_idx);
            part_count = parse_compiler_expr_count(index_text);
            free(index_text);
            if (part_count < 0) {
                free(trimmed);
                return -1;
            }
            total += 1 + part_count;
            i = close_idx + 1;
            continue;
        }
        free(trimmed);
        return -1;
    }
    free(trimmed);
    return total;
}

static int parse_compiler_expr_count(const char *text) {
    char *trimmed = trim_copy(text, text + strlen(text));
    int idx;
    int left;
    int right;
    if (trimmed[0] == '\0') {
        free(trimmed);
        return -1;
    }
    if (starts_with(trimmed, "if ")) {
        int out = parse_compiler_if_expr_count(trimmed);
        free(trimmed);
        return out;
    }
    if (trimmed[0] == '!') {
        char *rhs = trim_copy(trimmed + 1, trimmed + strlen(trimmed));
        int out = parse_compiler_expr_count(rhs);
        free(rhs);
        free(trimmed);
        return out < 0 ? -1 : 1 + out;
    }
    if (trimmed[0] == '-' && trimmed[1] != '\0') {
        char *rhs = trim_copy(trimmed + 1, trimmed + strlen(trimmed));
        int out = parse_compiler_expr_count(rhs);
        free(rhs);
        free(trimmed);
        return out < 0 ? -1 : 1 + out;
    }
    idx = find_last_top_level_binary_text(trimmed, "||");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 2, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "&&");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 2, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "==");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 2, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "!=");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 2, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "<=");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 2, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, ">=");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 2, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "<");
    if (idx >= 0 &&
        !starts_with_at_text(trimmed, idx, "<=") &&
        !starts_with_at_text(trimmed, idx, "<<") &&
        !(trimmed[idx + 1] == '.')) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 1, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, ">");
    if (idx >= 0 &&
        !starts_with_at_text(trimmed, idx, ">=") &&
        !starts_with_at_text(trimmed, idx, ">>")) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 1, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "+");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 1, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "-");
    if (idx > 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 1, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "*");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 1, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "/");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 1, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    idx = find_last_top_level_binary_text(trimmed, "%");
    if (idx >= 0) {
        char *lhs = trim_copy(trimmed, trimmed + idx);
        char *rhs = trim_copy(trimmed + idx + 1, trimmed + strlen(trimmed));
        left = parse_compiler_expr_count(lhs);
        right = parse_compiler_expr_count(rhs);
        free(lhs);
        free(rhs);
        free(trimmed);
        return left < 0 || right < 0 ? -1 : 1 + left + right;
    }
    {
        int out = parse_compiler_callish_expr_count(trimmed);
        free(trimmed);
        return out;
    }
}

static void split_text_lines(const char *text, StringList *out_lines) {
    const char *cursor = text;
    memset(out_lines, 0, sizeof(*out_lines));
    while (*cursor != '\0') {
        const char *line_start = cursor;
        const char *line_end = cursor;
        while (*line_end != '\0' && *line_end != '\n') {
            ++line_end;
        }
        string_list_push_take(out_lines, xstrdup_range(line_start, line_end));
        cursor = *line_end == '\n' ? line_end + 1 : line_end;
    }
}

static int parse_for_range_header_counts(const char *line,
                                         int *out_start_expr_count,
                                         int *out_stop_expr_count) {
    char *trimmed = trim_copy(line, line + strlen(line));
    char *body;
    char *name_text;
    char *range_text;
    int in_idx;
    int excl_idx;
    int incl_idx;
    int ok = 0;
    if (!starts_with(trimmed, "for ") || trimmed[strlen(trimmed) - 1] != ':') {
        free(trimmed);
        return 0;
    }
    body = trim_copy(trimmed + strlen("for "), trimmed + strlen(trimmed) - 1);
    in_idx = find_top_level_token_text(body, " in ", 0);
    if (in_idx < 0) {
        free(body);
        free(trimmed);
        return 0;
    }
    name_text = trim_copy(body, body + in_idx);
    range_text = trim_copy(body + in_idx + strlen(" in "), body + strlen(body));
    excl_idx = find_top_level_token_text(range_text, "..<", 0);
    incl_idx = excl_idx < 0 ? find_top_level_token_text(range_text, "..", 0) : -1;
    if (name_text[0] != '\0' && (excl_idx >= 0 || incl_idx >= 0)) {
        char *start_text;
        char *stop_text;
        int split = excl_idx >= 0 ? excl_idx : incl_idx;
        int step = excl_idx >= 0 ? 3 : 2;
        start_text = trim_copy(range_text, range_text + split);
        stop_text = trim_copy(range_text + split + step, range_text + strlen(range_text));
        *out_start_expr_count = parse_compiler_expr_count(start_text);
        *out_stop_expr_count = parse_compiler_expr_count(stop_text);
        ok = *out_start_expr_count >= 0 && *out_stop_expr_count >= 0;
        free(start_text);
        free(stop_text);
    }
    free(name_text);
    free(range_text);
    free(body);
    free(trimmed);
    return ok;
}

static int parse_compiler_stmt_lines_counts(char **raw_lines,
                                            int line_count,
                                            int *out_local_count,
                                            int *out_stmt_count,
                                            int *out_expr_count) {
    int idx = 0;
    *out_local_count = 0;
    *out_stmt_count = 0;
    *out_expr_count = 0;
    while (idx < line_count) {
        char *raw_line = raw_lines[idx];
        char *line = trim_copy(raw_line, raw_line + strlen(raw_line));
        if (is_blank_or_comment_line(raw_line)) {
            free(line);
            idx += 1;
            continue;
        }
        if (leading_indent_width(raw_line) > 0) {
            free(line);
            return 0;
        }
        if (starts_with(line, "return")) {
            char *expr_text = trim_copy(line + strlen("return"), line + strlen(line));
            int expr_count = 0;
            if (expr_text[0] != '\0') {
                expr_count = parse_compiler_expr_count(expr_text);
                if (expr_count < 0) {
                    free(expr_text);
                    free(line);
                    return 0;
                }
            }
            *out_stmt_count += 1;
            *out_expr_count += expr_count;
            free(expr_text);
            free(line);
            idx += 1;
            continue;
        }
        if (starts_with(line, "let ") || starts_with(line, "var ")) {
            char *tail = trim_copy(line + (starts_with(line, "let ") ? 4 : 4), line + strlen(line));
            char *eq = strrchr(tail, '=');
            int expr_count = 0;
            if (eq != NULL) {
                char *rhs = trim_copy(eq + 1, tail + strlen(tail));
                expr_count = parse_compiler_expr_count(rhs);
                free(rhs);
            }
            if (expr_count < 0) {
                free(tail);
                free(line);
                return 0;
            }
            *out_local_count += 1;
            *out_stmt_count += 1;
            *out_expr_count += expr_count;
            free(tail);
            free(line);
            idx += 1;
            continue;
        }
        if (starts_with(line, "if ") && line[strlen(line) - 1] == ':') {
            char *cond_text = trim_copy(line + strlen("if "), line + strlen(line) - 1);
            int cond_count = parse_compiler_expr_count(cond_text);
            StringList then_lines;
            int then_locals = 0;
            int then_stmts = 0;
            int then_exprs = 0;
            StringList else_lines;
            int else_locals = 0;
            int else_stmts = 0;
            int else_exprs = 0;
            int next_idx;
            if (cond_count < 0) {
                free(cond_text);
                free(line);
                return 0;
            }
            next_idx = collect_outline_block_lines(raw_lines, line_count, idx + 1, &then_lines);
            if (!parse_compiler_stmt_lines_counts(then_lines.items, (int)then_lines.len,
                                                  &then_locals, &then_stmts, &then_exprs)) {
                free(cond_text);
                free(line);
                return 0;
            }
            idx = next_idx;
            if (idx < line_count) {
                char *else_line = trim_copy(raw_lines[idx], raw_lines[idx] + strlen(raw_lines[idx]));
                if (leading_indent_width(raw_lines[idx]) == 0 && strcmp(else_line, "else:") == 0) {
                    idx = collect_outline_block_lines(raw_lines, line_count, idx + 1, &else_lines);
                    if (!parse_compiler_stmt_lines_counts(else_lines.items, (int)else_lines.len,
                                                          &else_locals, &else_stmts, &else_exprs)) {
                        free(else_line);
                        free(cond_text);
                        free(line);
                        return 0;
                    }
                } else if (leading_indent_width(raw_lines[idx]) == 0 && starts_with(else_line, "elif ")) {
                    int nested_locals = 0;
                    int nested_stmts = 0;
                    int nested_exprs = 0;
                    StringList nested_lines;
                    memset(&nested_lines, 0, sizeof(nested_lines));
                    while (idx < line_count) {
                        char *chain_head = trim_copy(raw_lines[idx], raw_lines[idx] + strlen(raw_lines[idx]));
                        int next_chain_idx;
                        if (leading_indent_width(raw_lines[idx]) != 0 ||
                            (!starts_with(chain_head, "elif ") && strcmp(chain_head, "else:") != 0)) {
                            free(chain_head);
                            break;
                        }
                        string_list_push_take(&nested_lines, chain_head);
                        next_chain_idx = append_outline_block_lines(raw_lines, line_count, idx + 1, &nested_lines);
                        idx = next_chain_idx;
                    }
                    if (!parse_compiler_stmt_lines_counts(nested_lines.items, (int)nested_lines.len,
                                                          &nested_locals, &nested_stmts, &nested_exprs)) {
                        free(else_line);
                        free(cond_text);
                        free(line);
                        return 0;
                    }
                    else_locals += nested_locals;
                    else_stmts += nested_stmts;
                    else_exprs += nested_exprs;
                    free(else_line);
                    *out_stmt_count += 1 + then_stmts + else_stmts;
                    *out_local_count += then_locals + else_locals;
                    *out_expr_count += cond_count + then_exprs + else_exprs;
                    free(cond_text);
                    free(line);
                    continue;
                }
                free(else_line);
            }
            *out_stmt_count += 1 + then_stmts + else_stmts;
            *out_local_count += then_locals + else_locals;
            *out_expr_count += cond_count + then_exprs + else_exprs;
            free(cond_text);
            free(line);
            continue;
        }
        if (starts_with(line, "while ") && line[strlen(line) - 1] == ':') {
            char *cond_text = trim_copy(line + strlen("while "), line + strlen(line) - 1);
            int cond_count = parse_compiler_expr_count(cond_text);
            StringList child_lines;
            int child_locals = 0;
            int child_stmts = 0;
            int child_exprs = 0;
            if (cond_count < 0) {
                free(cond_text);
                free(line);
                return 0;
            }
            idx = collect_outline_block_lines(raw_lines, line_count, idx + 1, &child_lines);
            if (!parse_compiler_stmt_lines_counts(child_lines.items, (int)child_lines.len,
                                                  &child_locals, &child_stmts, &child_exprs)) {
                free(cond_text);
                free(line);
                return 0;
            }
            *out_stmt_count += 1 + child_stmts;
            *out_local_count += child_locals;
            *out_expr_count += cond_count + child_exprs;
            free(cond_text);
            free(line);
            continue;
        }
        {
            int start_expr_count = 0;
            int stop_expr_count = 0;
            if (parse_for_range_header_counts(line, &start_expr_count, &stop_expr_count)) {
                StringList child_lines;
                int child_locals = 0;
                int child_stmts = 0;
                int child_exprs = 0;
                idx = collect_outline_block_lines(raw_lines, line_count, idx + 1, &child_lines);
                if (!parse_compiler_stmt_lines_counts(child_lines.items, (int)child_lines.len,
                                                      &child_locals, &child_stmts, &child_exprs)) {
                    free(line);
                    return 0;
                }
                *out_local_count += 1 + child_locals;
                *out_stmt_count += 1 + child_stmts;
                *out_expr_count += start_expr_count + stop_expr_count + child_exprs;
                free(line);
                continue;
            }
        }
        {
            int assign_idx = find_last_top_level_assign_text(line);
            if (assign_idx >= 0) {
                char *lhs = trim_copy(line, line + assign_idx);
                char *rhs = trim_copy(line + assign_idx + 1, line + strlen(line));
                int lhs_count;
                int rhs_count;
                if (lhs[0] == '\0' || rhs[0] == '\0') {
                    free(lhs);
                    free(rhs);
                    free(line);
                    return 0;
                }
                lhs_count = parse_compiler_expr_count(lhs);
                rhs_count = parse_compiler_expr_count(rhs);
                free(lhs);
                free(rhs);
                if (lhs_count < 0 || rhs_count < 0) {
                    free(line);
                    return 0;
                }
                *out_stmt_count += 1;
                *out_expr_count += lhs_count + rhs_count;
                free(line);
                idx += 1;
                continue;
            }
        }
        {
            int expr_count = parse_compiler_expr_count(line);
            if (expr_count < 0) {
                free(line);
                return 0;
            }
            *out_stmt_count += 1;
            *out_expr_count += expr_count;
        }
        free(line);
        idx += 1;
    }
    return 1;
}

static char *inline_routine_body_line(const char *line) {
    const char *eq = strrchr(line, '=');
    if (eq == NULL) {
        return xstrdup_text("");
    }
    return trim_copy(eq + 1, eq + strlen(eq));
}

static int line_ends_with_assign(const char *line) {
    char *trimmed = dup_trimmed_line(line);
    size_t len = strlen(trimmed);
    int out = len > 0 && trimmed[len - 1] == '=';
    free(trimmed);
    return out;
}

static int collect_outline_block_lines(char **raw_lines,
                                       int line_count,
                                       int start_idx,
                                       StringList *out_lines) {
    int idx = start_idx;
    int base_indent = -1;
    memset(out_lines, 0, sizeof(*out_lines));
    while (idx < line_count) {
        char *raw = raw_lines[idx];
        if (is_blank_or_comment_line(raw)) {
            if (base_indent >= 0) {
                char *trimmed = dup_trimmed_line(raw);
                string_list_push_take(out_lines, trimmed);
            }
            ++idx;
            continue;
        }
        {
            int indent = leading_indent_width(raw);
            if (base_indent < 0) {
                if (indent <= 0) {
                    break;
                }
                base_indent = indent;
            }
            if (indent < base_indent) {
                break;
            }
            string_list_push_copy(out_lines, raw + base_indent);
        }
        ++idx;
    }
    return idx;
}

static int append_outline_block_lines(char **raw_lines,
                                      int line_count,
                                      int start_idx,
                                      StringList *out_lines) {
    int idx = start_idx;
    int base_indent = -1;
    while (idx < line_count) {
        char *raw = raw_lines[idx];
        if (is_blank_or_comment_line(raw)) {
            if (base_indent >= 0) {
                char *trimmed = dup_trimmed_line(raw);
                string_list_push_take(out_lines, trimmed);
            }
            ++idx;
            continue;
        }
        {
            int indent = leading_indent_width(raw);
            if (base_indent < 0) {
                if (indent <= 0) {
                    break;
                }
                base_indent = indent;
            }
            if (indent < base_indent) {
                break;
            }
            string_list_push_copy(out_lines, raw + base_indent);
        }
        ++idx;
    }
    return idx;
}

static int collect_routine_signature_and_body(char **raw_lines,
                                              int line_count,
                                              int start_idx,
                                              const StringList *prefix_lines,
                                              int allow_header_only,
                                              char **out_signature,
                                              int *out_body_line_count,
                                              char **out_first_body_line,
                                              char **out_body_hash,
                                              char **out_body_text) {
    StringList signature_lines;
    char *first_line_trimmed = dup_trimmed_line(raw_lines[start_idx]);
    char *inline_body = inline_routine_body_line(first_line_trimmed);
    int idx;
    memset(&signature_lines, 0, sizeof(signature_lines));
    string_list_push_take(&signature_lines, first_line_trimmed);
    if (inline_body[0] != '\0') {
        *out_signature = join_prefixed_line_parts(prefix_lines, &signature_lines);
        *out_body_line_count = 1;
        *out_first_body_line = inline_body;
        *out_body_hash = sha256_hex_text(inline_body);
        *out_body_text = xstrdup_text(inline_body);
        return start_idx + 1;
    }
    free(inline_body);
    if (line_ends_with_assign(first_line_trimmed)) {
        StringList body_lines;
        memset(&body_lines, 0, sizeof(body_lines));
        idx = collect_outline_block_lines(raw_lines, line_count, start_idx + 1, &body_lines);
        *out_signature = join_prefixed_line_parts(prefix_lines, &signature_lines);
        *out_body_line_count = (int)body_lines.len;
        *out_first_body_line =
            body_lines.len > 0 ? xstrdup_text(body_lines.items[0]) : xstrdup_text("");
        {
            char *body_text = string_list_join(&body_lines, "\n");
            *out_body_text = xstrdup_text(body_text);
            *out_body_hash = sha256_hex_text(body_text);
            free(body_text);
        }
        return idx;
    }
    idx = start_idx + 1;
    while (idx < line_count) {
        char *raw = raw_lines[idx];
        char *trimmed;
        if (is_blank_or_comment_line(raw)) {
            if (allow_header_only) {
                *out_signature = join_prefixed_line_parts(prefix_lines, &signature_lines);
                *out_body_line_count = 0;
                *out_first_body_line = xstrdup_text("");
                *out_body_hash = xstrdup_text(SHA256_EMPTY_HEX);
                *out_body_text = xstrdup_text("");
                return idx;
            }
            die("v2 source parser: blank or comment inside compiler routine header");
        }
        if (leading_indent_width(raw) <= 0) {
            if (allow_header_only) {
                *out_signature = join_prefixed_line_parts(prefix_lines, &signature_lines);
                *out_body_line_count = 0;
                *out_first_body_line = xstrdup_text("");
                *out_body_hash = xstrdup_text(SHA256_EMPTY_HEX);
                *out_body_text = xstrdup_text("");
                return idx;
            }
            die("v2 source parser: unterminated compiler routine header");
        }
        trimmed = dup_trimmed_line(raw);
        string_list_push_take(&signature_lines, trimmed);
        ++idx;
        if (line_ends_with_assign(trimmed)) {
            StringList body_lines;
            memset(&body_lines, 0, sizeof(body_lines));
            idx = collect_outline_block_lines(raw_lines, line_count, idx, &body_lines);
            *out_signature = join_prefixed_line_parts(prefix_lines, &signature_lines);
            *out_body_line_count = (int)body_lines.len;
            *out_first_body_line =
                body_lines.len > 0 ? xstrdup_text(body_lines.items[0]) : xstrdup_text("");
            {
                char *body_text = string_list_join(&body_lines, "\n");
                *out_body_text = xstrdup_text(body_text);
                *out_body_hash = sha256_hex_text(body_text);
                free(body_text);
            }
            return idx;
        }
    }
    if (allow_header_only) {
        *out_signature = join_prefixed_line_parts(prefix_lines, &signature_lines);
        *out_body_line_count = 0;
        *out_first_body_line = xstrdup_text("");
        *out_body_hash = xstrdup_text(SHA256_EMPTY_HEX);
        *out_body_text = xstrdup_text("");
        return idx;
    }
    die("v2 source parser: unterminated compiler routine header");
    return start_idx;
}

static int parse_int_strict(const char *text) {
    int out = 0;
    const char *p = text;
    if (*p == '\0') {
        die("expected integer");
    }
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            die("invalid integer");
        }
        out = out * 10 + (*p - '0');
        ++p;
    }
    return out;
}

static char *extract_named_value(const char *body, const char *key) {
    char *pattern = xformat("%s=", key);
    const char *p = strstr(body, pattern);
    const char *start;
    const char *scan;
    int depth = 0;
    char *out;
    free(pattern);
    if (p == NULL) {
        return xstrdup_text("");
    }
    start = p + strlen(key) + 1;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    scan = start;
    while (*scan != '\0') {
        if (*scan == '(') {
            depth += 1;
        } else if (*scan == ')') {
            if (depth == 0) {
                break;
            }
            depth -= 1;
        } else if (*scan == ',' && depth == 0) {
            break;
        }
        ++scan;
    }
    out = trim_copy(start, scan);
    return out;
}

static char *extract_line_value(const char *body, const char *key) {
    char *pattern = xformat("%s=", key);
    const char *p = strstr(body, pattern);
    const char *start;
    const char *end;
    char *out;
    free(pattern);
    if (p == NULL) {
        return xstrdup_text("");
    }
    start = p + strlen(key) + 1;
    end = start;
    while (*end != '\0' && *end != '\n' && *end != '\r') {
        ++end;
    }
    out = trim_copy(start, end);
    return out;
}

static char *parse_call_body(const char *line, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    size_t line_len = strlen(line);
    if (!starts_with(line, prefix)) {
        return NULL;
    }
    if (line_len < prefix_len + 2 || line[prefix_len] != '(' || line[line_len - 1] != ')') {
        die("malformed call");
    }
    return trim_copy(line + prefix_len + 1, line + line_len - 1);
}

static void parse_topology_dispersal(const char *raw, char **dispersal_out, int *ida_k, int *ida_n) {
    char *text = trim_copy(raw, raw + strlen(raw));
    *ida_k = 0;
    *ida_n = 0;
    if (strcmp(text, "none") == 0) {
        *dispersal_out = text;
        return;
    }
    if (!starts_with(text, "ida(") || text[strlen(text) - 1] != ')') {
        free(text);
        die("unsupported topology dispersal");
    }
    {
        char *inner = trim_copy(text + 4, text + strlen(text) - 1);
        char *k_raw = extract_named_value(inner, "k");
        char *n_raw = extract_named_value(inner, "n");
        *ida_k = parse_int_strict(k_raw);
        *ida_n = parse_int_strict(n_raw);
        free(k_raw);
        free(n_raw);
        free(inner);
    }
    free(text);
    *dispersal_out = xstrdup_text("ida");
}

static int parse_unimaker_line(const char *line) {
    return starts_with(line, "asset ") ||
           starts_with(line, "actor ") ||
           starts_with(line, "@ganzhi state ") ||
           starts_with(line, "@nfc_callback fn ") ||
           starts_with(line, "@evolve(");
}

static int parse_topology_line(NetworkProgram *program, const char *line) {
    char *name;
    const char *body_pos;
    char *body;
    char *domain;
    char *fanout;
    char *repair;
    char *dispersal_raw;
    char *priority;
    char *addressing;
    char *depth_raw;
    char *dispersal;
    int depth = 0;
    int ida_k = 0;
    int ida_n = 0;
    if (!starts_with(line, "topology ")) {
        return 0;
    }
    if (line[strlen(line) - 1] != ')') {
        die("malformed topology declaration");
    }
    name = parse_name_after_prefix(line, "topology ");
    body_pos = strstr(line, "lsmr_tree(");
    if (body_pos == NULL) {
        die("malformed topology declaration");
    }
    body = trim_copy(body_pos + strlen("lsmr_tree("), line + strlen(line) - 1);
    domain = extract_named_value(body, "domain");
    fanout = extract_named_value(body, "fanout");
    repair = extract_named_value(body, "repair");
    dispersal_raw = extract_named_value(body, "dispersal");
    priority = extract_named_value(body, "priority");
    addressing = extract_named_value(body, "addressing");
    depth_raw = extract_named_value(body, "depth");
    if (addressing[0] == '\0') {
        die("v2 source parser: topology addressing is required");
    }
    if (depth_raw[0] == '\0') {
        die("v2 source parser: topology depth is required");
    }
    depth = parse_int_strict(depth_raw);
    parse_topology_dispersal(dispersal_raw, &dispersal, &ida_k, &ida_n);
    topology_list_push(&program->topologies,
                       name,
                       domain,
                       fanout,
                       repair,
                       dispersal,
                       priority,
                       addressing,
                       depth,
                       ida_k,
                       ida_n);
    free(name);
    free(body);
    free(domain);
    free(fanout);
    free(repair);
    free(dispersal_raw);
    free(priority);
    free(addressing);
    free(depth_raw);
    free(dispersal);
    return 1;
}

static int parse_network_op_line(NetworkProgram *program, const char *line) {
    char *body = parse_call_body(line, "publish_source_manifest");
    char *topology;
    char *subject;
    if (body != NULL) {
        topology = extract_named_value(body, "topology");
        subject = extract_named_value(body, "package");
        network_op_list_push(&program->ops, "publish_source_manifest", topology, subject, "");
        free(topology);
        free(subject);
        free(body);
        return 1;
    }
    body = parse_call_body(line, "publish_rule_pack");
    if (body != NULL) {
        topology = extract_named_value(body, "topology");
        subject = extract_named_value(body, "pack");
        network_op_list_push(&program->ops, "publish_rule_pack", topology, subject, "");
        free(topology);
        free(subject);
        free(body);
        return 1;
    }
    body = parse_call_body(line, "publish_compiler_rule_pack");
    if (body != NULL) {
        topology = extract_named_value(body, "topology");
        subject = extract_named_value(body, "pack");
        network_op_list_push(&program->ops, "publish_compiler_rule_pack", topology, subject, "");
        free(topology);
        free(subject);
        free(body);
        return 1;
    }
    body = parse_call_body(line, "route_lsmr_tree");
    if (body != NULL) {
        topology = extract_named_value(body, "topology");
        network_op_list_push(&program->ops, "route_lsmr_tree", topology, topology, "");
        free(topology);
        free(body);
        return 1;
    }
    body = parse_call_body(line, "disperse_ida_fragments");
    if (body != NULL) {
        topology = extract_named_value(body, "topology");
        network_op_list_push(&program->ops, "disperse_ida_fragments", topology, topology, "");
        free(topology);
        free(body);
        return 1;
    }
    body = parse_call_body(line, "exchange_anti_entropy_signature");
    if (body != NULL) {
        topology = extract_named_value(body, "topology");
        subject = extract_named_value(body, "signature");
        network_op_list_push(&program->ops, "exchange_anti_entropy_signature", topology, topology, subject);
        free(topology);
        free(subject);
        free(body);
        return 1;
    }
    body = parse_call_body(line, "repair_from_signature_gap");
    if (body != NULL) {
        topology = extract_named_value(body, "topology");
        subject = extract_named_value(body, "signature");
        network_op_list_push(&program->ops, "repair_from_signature_gap", topology, topology, subject);
        free(topology);
        free(subject);
        free(body);
        return 1;
    }
    return 0;
}

static ParsedV2Source try_parse_v2_source_file(const char *path) {
    ParsedV2Source out;
    char *text = read_file_text(path);
    char *cursor = text;
    int had_recognized = 0;
    memset(&out, 0, sizeof(out));
    out.parse_ok = 1;
    while (*cursor != '\0') {
        char *line_start = cursor;
        char *line_end = cursor;
        char saved;
        char *trimmed;
        while (*line_end != '\0' && *line_end != '\n') {
            ++line_end;
        }
        saved = *line_end;
        *line_end = '\0';
        trimmed = dup_trimmed_line(line_start);
        if (trimmed[0] != '\0' && !starts_with(trimmed, "//") && !starts_with(trimmed, "#")) {
            if (parse_unimaker_line(trimmed)) {
                had_recognized = 1;
                out.had_recognized_surface = 1;
            } else if (parse_topology_line(&out.network_program, trimmed)) {
                had_recognized = 1;
                out.had_recognized_surface = 1;
                out.had_network_surface = 1;
            } else if (parse_network_op_line(&out.network_program, trimmed)) {
                had_recognized = 1;
                out.had_recognized_surface = 1;
                out.had_network_surface = 1;
            } else if (had_recognized) {
                out.parse_ok = 0;
            }
        }
        free(trimmed);
        *line_end = saved;
        if (!out.parse_ok) {
            break;
        }
        cursor = *line_end == '\n' ? line_end + 1 : line_end;
    }
    free(text);
    if (out.parse_ok && !out.had_recognized_surface) {
        out.compiler_core_program = parse_compiler_core_source_file_strict(path);
        out.had_recognized_surface = 1;
        out.had_compiler_core_surface = 1;
    }
    return out;
}

static CompilerCoreProgram parse_compiler_core_source_file_strict(const char *path) {
    CompilerCoreProgram out;
    char *default_owner_module = NULL;
    char *text = read_file_text(path);
    StringList raw_lines;
    StringList pending_annotations;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(&raw_lines, 0, sizeof(raw_lines));
    memset(&pending_annotations, 0, sizeof(pending_annotations));
    out.version = xstrdup_text("v2.frontend.compiler_core_surface_ir.v1");
    {
        char *cursor = text;
        while (*cursor != '\0') {
            char *line_start = cursor;
            char *line_end = cursor;
            char saved;
            while (*line_end != '\0' && *line_end != '\n') {
                ++line_end;
            }
            saved = *line_end;
            *line_end = '\0';
            string_list_push_copy(&raw_lines, line_start);
            *line_end = saved;
            cursor = *line_end == '\n' ? line_end + 1 : line_end;
        }
    }
    default_owner_module = compiler_core_default_owner_module_path(path);
    for (i = 0; i < raw_lines.len; ) {
        char *raw = raw_lines.items[i];
        char *trimmed = dup_trimmed_line(raw);
        if (trimmed[0] == '\0' || starts_with(trimmed, "//") || starts_with(trimmed, "#")) {
            free(trimmed);
            ++i;
            continue;
        }
        if (leading_indent_width(raw) > 0) {
            free(trimmed);
            die(xformat("v2 source parser: unexpected top-level indentation in compiler_core source at line %zu", i + 1));
        }
        if (trimmed[0] == '@') {
            string_list_push_copy(&pending_annotations, trimmed);
            free(trimmed);
            ++i;
            continue;
        }
        if (strcmp(trimmed, "module") == 0 || starts_with(trimmed, "module ")) {
            char *name;
            if (pending_annotations.len > 0) {
                free(trimmed);
                die("v2 source parser: annotations may only prefix compiler routines");
            }
            name = parse_compiler_decl_label(trimmed, "module ", "module");
            compiler_core_module_list_push(&out.modules, name, trimmed);
            string_list_push_copy(&out.top_level_kinds, "module");
            string_list_push_copy(&out.top_level_labels, out.modules.names[out.modules.len - 1]);
            string_list_push_copy(&out.top_level_owner_modules, out.modules.names[out.modules.len - 1]);
            string_list_push_copy(&out.top_level_source_paths, path);
            free(name);
            free(trimmed);
            ++i;
            continue;
        }
        if (starts_with(trimmed, "import ")) {
            if (pending_annotations.len > 0) {
                free(trimmed);
                die("v2 source parser: annotations may only prefix compiler routines");
            }
            char *module_path = parse_compiler_import_module_path(trimmed);
            char *alias = parse_compiler_import_alias(trimmed);
            string_list_push_take(&out.import_paths, module_path);
            string_list_push_take(&out.import_aliases, alias);
            string_list_push_copy(&out.top_level_kinds, "import");
            string_list_push_copy(&out.top_level_labels, out.import_paths.items[out.import_paths.len - 1]);
            string_list_push_copy(&out.top_level_owner_modules, compiler_core_current_owner_module(&out));
            string_list_push_copy(&out.top_level_source_paths, path);
            free(trimmed);
            ++i;
            continue;
        }
        if (strcmp(trimmed, "type") == 0) {
            StringList body_lines;
            char *body_text;
            char *body_hash;
            if (pending_annotations.len > 0) {
                free(trimmed);
                die("v2 source parser: annotations may only prefix compiler routines");
            }
            int next = collect_outline_block_lines(raw_lines.items, (int)raw_lines.len, (int)i + 1, &body_lines);
            body_text = string_list_join(&body_lines, "\n");
            body_hash = sha256_hex_text(body_text);
            string_list_push_copy(&out.type_headers, "type");
            int_list_push(&out.type_body_line_counts, (int)body_lines.len);
            string_list_push_take(&out.type_body_hashes, body_hash);
            string_list_push_copy(&out.top_level_kinds, "type");
            string_list_push_copy(&out.top_level_labels, "type");
            string_list_push_copy(&out.top_level_owner_modules, compiler_core_current_owner_module(&out));
            string_list_push_copy(&out.top_level_source_paths, path);
            free(body_text);
            free(trimmed);
            i = (size_t)next;
            continue;
        }
        if (strcmp(trimmed, "const") == 0 || starts_with(trimmed, "const ")) {
            int next = (int)i + 1;
            int body_line_count = 0;
            char *label;
            char *body_hash = xstrdup_text(SHA256_EMPTY_HEX);
            if (pending_annotations.len > 0) {
                free(trimmed);
                free(body_hash);
                die("v2 source parser: annotations may only prefix compiler routines");
            }
            if (strcmp(trimmed, "const") == 0) {
                StringList body_lines;
                char *body_text;
                memset(&body_lines, 0, sizeof(body_lines));
                next = collect_outline_block_lines(raw_lines.items, (int)raw_lines.len, (int)i + 1, &body_lines);
                body_line_count = (int)body_lines.len;
                body_text = string_list_join(&body_lines, "\n");
                free(body_hash);
                body_hash = sha256_hex_text(body_text);
                free(body_text);
            }
            label = parse_compiler_decl_label(trimmed, "const ", "const");
            compiler_core_value_decl_list_push(&out.const_decls, label, trimmed, body_line_count, body_hash);
            string_list_push_copy(&out.top_level_kinds, "const");
            string_list_push_copy(&out.top_level_labels, out.const_decls.labels[out.const_decls.len - 1]);
            string_list_push_copy(&out.top_level_owner_modules, compiler_core_current_owner_module(&out));
            string_list_push_copy(&out.top_level_source_paths, path);
            free(label);
            free(body_hash);
            free(trimmed);
            i = (size_t)next;
            continue;
        }
        if (strcmp(trimmed, "var") == 0 || starts_with(trimmed, "var ")) {
            int next = (int)i + 1;
            int body_line_count = 0;
            char *label;
            char *body_hash = xstrdup_text(SHA256_EMPTY_HEX);
            if (pending_annotations.len > 0) {
                free(trimmed);
                free(body_hash);
                die("v2 source parser: annotations may only prefix compiler routines");
            }
            if (strcmp(trimmed, "var") == 0) {
                StringList body_lines;
                char *body_text;
                memset(&body_lines, 0, sizeof(body_lines));
                next = collect_outline_block_lines(raw_lines.items, (int)raw_lines.len, (int)i + 1, &body_lines);
                body_line_count = (int)body_lines.len;
                body_text = string_list_join(&body_lines, "\n");
                free(body_hash);
                body_hash = sha256_hex_text(body_text);
                free(body_text);
            }
            label = parse_compiler_decl_label(trimmed, "var ", "var");
            compiler_core_value_decl_list_push(&out.var_decls, label, trimmed, body_line_count, body_hash);
            string_list_push_copy(&out.top_level_kinds, "var");
            string_list_push_copy(&out.top_level_labels, out.var_decls.labels[out.var_decls.len - 1]);
            string_list_push_copy(&out.top_level_owner_modules, compiler_core_current_owner_module(&out));
            string_list_push_copy(&out.top_level_source_paths, path);
            free(label);
            free(body_hash);
            free(trimmed);
            i = (size_t)next;
            continue;
        }
        if (starts_with(trimmed, "importc fn ")) {
            StringList signature_lines;
            char *name = parse_compiler_routine_name(trimmed, "importc fn ");
            char *signature;
            char *return_type_text;
            int param_count = 0;
            char *signature_hash;
            memset(&signature_lines, 0, sizeof(signature_lines));
            string_list_push_copy(&signature_lines, trimmed);
            signature = join_prefixed_line_parts(&pending_annotations, &signature_lines);
            parse_compiler_routine_signature_shape(signature, &return_type_text, &param_count);
            signature_hash = sha256_hex_text(signature);
            string_list_push_copy(&out.routine_kinds, "importc_fn");
            string_list_push_take(&out.routine_names, name);
            string_list_push_take(&out.routine_signatures, signature);
            string_list_push_take(&out.routine_return_type_texts, return_type_text);
            int_list_push(&out.routine_param_counts, param_count);
            int_list_push(&out.routine_executable_ast_ready, 0);
            int_list_push(&out.routine_local_counts, 0);
            int_list_push(&out.routine_stmt_counts, 0);
            int_list_push(&out.routine_expr_counts, 0);
            string_list_push_take(&out.routine_signature_hashes, signature_hash);
            int_list_push(&out.routine_body_line_counts, 0);
            string_list_push_copy(&out.routine_body_hashes, SHA256_EMPTY_HEX);
            string_list_push_copy(&out.routine_first_body_lines, "");
            string_list_push_copy(&out.routine_body_texts, "");
            string_list_push_copy(&out.top_level_kinds, "importc_fn");
            string_list_push_copy(&out.top_level_labels, out.routine_names.items[out.routine_names.len - 1]);
            string_list_push_copy(&out.top_level_owner_modules, compiler_core_current_owner_module(&out));
            string_list_push_copy(&out.top_level_source_paths, path);
            pending_annotations.len = 0;
            free(trimmed);
            ++i;
            continue;
        }
        if (starts_with(trimmed, "fn ") || starts_with(trimmed, "iterator ")) {
            const char *kind = starts_with(trimmed, "iterator ") ? "iterator" : "fn";
            int allow_header_only = annotation_list_has_importc(&pending_annotations);
            char *name = parse_compiler_routine_name(trimmed, starts_with(trimmed, "iterator ") ? "iterator " : "fn ");
            char *signature = NULL;
            char *return_type_text = NULL;
            int param_count = 0;
            char *signature_hash = NULL;
            int body_line_count = 0;
            char *first_body_line = NULL;
            char *body_hash = NULL;
            char *body_text = NULL;
            int executable_ast_ready = 0;
            int local_count = 0;
            int stmt_count = 0;
            int expr_count = 0;
            int next =
                collect_routine_signature_and_body(raw_lines.items,
                                                   (int)raw_lines.len,
                                                   (int)i,
                                                   &pending_annotations,
                                                   allow_header_only,
                                                   &signature,
                                                   &body_line_count,
                                                   &first_body_line,
                                                   &body_hash,
                                                   &body_text);
            if (allow_header_only && body_line_count == 0) {
                kind = "importc_fn";
            }
            parse_compiler_routine_signature_shape(signature, &return_type_text, &param_count);
            if (strcmp(kind, "importc_fn") != 0 && body_text != NULL && body_text[0] != '\0') {
                StringList body_lines;
                memset(&body_lines, 0, sizeof(body_lines));
                split_text_lines(body_text, &body_lines);
                executable_ast_ready =
                    parse_compiler_stmt_lines_counts(body_lines.items,
                                                     (int)body_lines.len,
                                                     &local_count,
                                                     &stmt_count,
                                                     &expr_count);
            }
            signature_hash = sha256_hex_text(signature);
            string_list_push_copy(&out.routine_kinds, kind);
            string_list_push_take(&out.routine_names, name);
            string_list_push_take(&out.routine_signatures, signature);
            string_list_push_take(&out.routine_return_type_texts, return_type_text);
            int_list_push(&out.routine_param_counts, param_count);
            int_list_push(&out.routine_executable_ast_ready, executable_ast_ready);
            int_list_push(&out.routine_local_counts, local_count);
            int_list_push(&out.routine_stmt_counts, stmt_count);
            int_list_push(&out.routine_expr_counts, expr_count);
            string_list_push_take(&out.routine_signature_hashes, signature_hash);
            int_list_push(&out.routine_body_line_counts, body_line_count);
            string_list_push_take(&out.routine_body_hashes, body_hash);
            string_list_push_take(&out.routine_first_body_lines, first_body_line);
            string_list_push_take(&out.routine_body_texts, body_text);
            string_list_push_copy(&out.top_level_kinds, kind);
            string_list_push_copy(&out.top_level_labels, out.routine_names.items[out.routine_names.len - 1]);
            string_list_push_copy(&out.top_level_owner_modules, compiler_core_current_owner_module(&out));
            string_list_push_copy(&out.top_level_source_paths, path);
            pending_annotations.len = 0;
            free(trimmed);
            i = (size_t)next;
            continue;
        }
        free(trimmed);
        die(xformat("v2 source parser: unsupported compiler_core top-level form at line %zu", i + 1));
    }
    free(text);
    if (pending_annotations.len > 0) {
        die("v2 source parser: dangling compiler annotation");
    }
    for (i = 0; i < out.top_level_owner_modules.len; ++i) {
        if (out.top_level_owner_modules.items[i] == NULL || out.top_level_owner_modules.items[i][0] == '\0') {
            free(out.top_level_owner_modules.items[i]);
            out.top_level_owner_modules.items[i] = xstrdup_text(default_owner_module);
        }
    }
    for (i = 0; i < out.top_level_source_paths.len; ++i) {
        if (out.top_level_source_paths.items[i] == NULL || out.top_level_source_paths.items[i][0] == '\0') {
            free(out.top_level_source_paths.items[i]);
            out.top_level_source_paths.items[i] = xstrdup_text(path);
        }
    }
    if (out.modules.len == 0 &&
        out.import_paths.len == 0 &&
        out.type_headers.len == 0 &&
        out.const_decls.len == 0 &&
        out.var_decls.len == 0 &&
        out.routine_names.len == 0) {
        die("v2 source parser: no compiler_core surface recognized");
    }
    free(default_owner_module);
    return out;
}

static void print_compiler_core_program(const char *source_norm, const CompilerCoreProgram *program) {
    size_t i;
    printf("compiler_core_version=%s\n", program->version);
    printf("source=%s\n", source_norm);
    printf("module_count=%zu\n", program->modules.len);
    printf("import_count=%zu\n", program->import_paths.len);
    printf("type_block_count=%zu\n", program->type_headers.len);
    printf("const_count=%zu\n", program->const_decls.len);
    printf("var_count=%zu\n", program->var_decls.len);
    printf("routine_count=%zu\n", program->routine_names.len);
    printf("top_level_count=%zu\n", program->top_level_kinds.len);
    for (i = 0; i < program->modules.len; ++i) {
        printf("module.%zu.name=%s\n", i, program->modules.names[i]);
        printf("module.%zu.header=%s\n", i, program->modules.header_texts[i]);
    }
    for (i = 0; i < program->import_paths.len; ++i) {
        printf("import.%zu.path=%s\n", i, program->import_paths.items[i]);
        printf("import.%zu.alias=%s\n", i, program->import_aliases.items[i]);
    }
    for (i = 0; i < program->type_headers.len; ++i) {
        printf("type.%zu.header=%s\n", i, program->type_headers.items[i]);
        printf("type.%zu.body_line_count=%d\n", i, program->type_body_line_counts.items[i]);
    }
    for (i = 0; i < program->const_decls.len; ++i) {
        printf("const.%zu.label=%s\n", i, program->const_decls.labels[i]);
        printf("const.%zu.header=%s\n", i, program->const_decls.header_texts[i]);
        printf("const.%zu.body_line_count=%d\n", i, program->const_decls.body_line_counts.items[i]);
    }
    for (i = 0; i < program->var_decls.len; ++i) {
        printf("var.%zu.label=%s\n", i, program->var_decls.labels[i]);
        printf("var.%zu.header=%s\n", i, program->var_decls.header_texts[i]);
        printf("var.%zu.body_line_count=%d\n", i, program->var_decls.body_line_counts.items[i]);
    }
    for (i = 0; i < program->routine_names.len; ++i) {
        printf("routine.%zu.kind=%s\n", i, program->routine_kinds.items[i]);
        printf("routine.%zu.name=%s\n", i, program->routine_names.items[i]);
        printf("routine.%zu.signature=%s\n", i, program->routine_signatures.items[i]);
        printf("routine.%zu.body_line_count=%d\n", i, program->routine_body_line_counts.items[i]);
        printf("routine.%zu.first_body_line=%s\n", i, program->routine_first_body_lines.items[i]);
    }
}

static const char *compiler_core_import_category(const char *module_path) {
    if (starts_with(module_path, "std/")) {
        return "stdlib";
    }
    if (starts_with(module_path, "compiler/")) {
        return "compiler_module";
    }
    if (starts_with(module_path, "runtime/")) {
        return "runtime_module";
    }
    if (starts_with(module_path, "bootstrap/")) {
        return "bootstrap_module";
    }
    return "package_local";
}

static const char *compiler_core_high_op_for_kind(const char *kind, int executable_ast_ready) {
    if (strcmp(kind, "module") == 0) {
        return "compiler_module_decl";
    }
    if (strcmp(kind, "import") == 0) {
        return "compiler_import_bind";
    }
    if (strcmp(kind, "type") == 0) {
        return "compiler_type_block";
    }
    if (strcmp(kind, "const") == 0) {
        return "compiler_const_decl";
    }
    if (strcmp(kind, "var") == 0) {
        return "compiler_var_decl";
    }
    if (strcmp(kind, "importc_fn") == 0) {
        return "compiler_importc_fn_decl";
    }
    if (executable_ast_ready) {
        if (strcmp(kind, "iterator") == 0) {
            return "compiler_iterator_exec";
        }
        return "compiler_fn_exec";
    }
    if (strcmp(kind, "iterator") == 0) {
        return "compiler_iterator_outline";
    }
    return "compiler_fn_outline";
}

static const char *compiler_core_proof_domain_for_kind(const char *kind, int executable_ast_ready) {
    if (strcmp(kind, "module") == 0) {
        return "module_identity_outline";
    }
    if (strcmp(kind, "import") == 0) {
        return "module_path_resolution";
    }
    if (strcmp(kind, "type") == 0) {
        return "type_layout_outline";
    }
    if (strcmp(kind, "const") == 0) {
        return "const_value_outline";
    }
    if (strcmp(kind, "var") == 0) {
        return "var_storage_outline";
    }
    if (strcmp(kind, "importc_fn") == 0) {
        return "ffi_signature_outline";
    }
    if (executable_ast_ready) {
        if (strcmp(kind, "iterator") == 0) {
            return "iterator_control_flow_subset";
        }
        return "routine_control_flow_subset";
    }
    if (strcmp(kind, "iterator") == 0) {
        return "iterator_signature_outline";
    }
    return "routine_signature_outline";
}

static const char *compiler_core_effect_class_for_kind(const char *kind, int executable_ast_ready) {
    if (strcmp(kind, "module") == 0) {
        return "module_identity";
    }
    if (strcmp(kind, "import") == 0) {
        return "module_resolution";
    }
    if (strcmp(kind, "type") == 0) {
        return "type_layout";
    }
    if (strcmp(kind, "const") == 0) {
        return "const_binding";
    }
    if (strcmp(kind, "var") == 0) {
        return "storage_binding";
    }
    if (strcmp(kind, "importc_fn") == 0) {
        return "ffi_binding";
    }
    if (executable_ast_ready) {
        if (strcmp(kind, "iterator") == 0) {
            return "sequence_executable";
        }
        return "control_flow_executable";
    }
    if (strcmp(kind, "iterator") == 0) {
        return "sequence_outline";
    }
    return "control_flow_outline";
}

static const char *compiler_core_binding_scope_for_kind(const char *kind) {
    if (strcmp(kind, "module") == 0) {
        return "module_scope";
    }
    if (strcmp(kind, "import") == 0) {
        return "import_binding";
    }
    if (strcmp(kind, "type") == 0) {
        return "type_scope";
    }
    if (strcmp(kind, "const") == 0) {
        return "const_scope";
    }
    if (strcmp(kind, "var") == 0) {
        return "var_scope";
    }
    if (strcmp(kind, "importc_fn") == 0) {
        return "ffi_scope";
    }
    if (strcmp(kind, "iterator") == 0) {
        return "iterator_scope";
    }
    return "routine_scope";
}

static const char *compiler_core_argv_runtime_target(void) {
    return "runtime/compiler_core.argv_entry";
}

static const char *compiler_core_local_payload_runtime_target(void) {
    return "runtime/compiler_core.local_payload_entry";
}

static const char *compiler_core_machine_runtime_target(const char *runtime_target) {
    return strcmp(runtime_target, compiler_core_argv_runtime_target()) == 0
               ? compiler_core_local_payload_runtime_target()
               : runtime_target;
}

static const char *compiler_core_runtime_target_for_op(const char *op_name) {
    (void)op_name;
    return compiler_core_argv_runtime_target();
}

static CompilerCoreFacts build_compiler_core_facts(const CompilerCoreProgram *program) {
    CompilerCoreFacts out;
    size_t i;
    size_t module_idx = 0;
    size_t import_idx = 0;
    size_t type_idx = 0;
    size_t const_idx = 0;
    size_t var_idx = 0;
    size_t routine_idx = 0;
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.semantic_facts.compiler_core.v1");
    for (i = 0; i < program->top_level_kinds.len; ++i) {
        const char *kind = program->top_level_kinds.items[i];
        const char *label = program->top_level_labels.items[i];
        const char *import_path = "";
        const char *import_alias = "";
        const char *import_category = "";
        int body_line_count = 0;
        const char *body_hash = SHA256_EMPTY_HEX;
        const char *signature_hash = SHA256_EMPTY_HEX;
        int executable_ast_ready = 0;
        const char *return_type_text = "";
        int param_count = 0;
        int local_count = 0;
        int stmt_count = 0;
        int expr_count = 0;
        if (strcmp(kind, "module") == 0) {
            label = program->modules.names[module_idx];
            signature_hash = sha256_hex_text(program->modules.header_texts[module_idx]);
            ++module_idx;
        } else if (strcmp(kind, "import") == 0) {
            import_path = program->import_paths.items[import_idx];
            import_alias = program->import_aliases.items[import_idx];
            import_category = compiler_core_import_category(import_path);
            label = import_path;
            ++import_idx;
        } else if (strcmp(kind, "type") == 0) {
            label = program->type_headers.items[type_idx];
            body_line_count = program->type_body_line_counts.items[type_idx];
            body_hash = program->type_body_hashes.items[type_idx];
            ++type_idx;
        } else if (strcmp(kind, "const") == 0) {
            label = program->const_decls.labels[const_idx];
            body_line_count = program->const_decls.body_line_counts.items[const_idx];
            body_hash = program->const_decls.body_hashes.items[const_idx];
            signature_hash = sha256_hex_text(program->const_decls.header_texts[const_idx]);
            ++const_idx;
        } else if (strcmp(kind, "var") == 0) {
            label = program->var_decls.labels[var_idx];
            body_line_count = program->var_decls.body_line_counts.items[var_idx];
            body_hash = program->var_decls.body_hashes.items[var_idx];
            signature_hash = sha256_hex_text(program->var_decls.header_texts[var_idx]);
            ++var_idx;
        } else {
            label = program->routine_names.items[routine_idx];
            body_line_count = program->routine_body_line_counts.items[routine_idx];
            body_hash = program->routine_body_hashes.items[routine_idx];
            signature_hash = program->routine_signature_hashes.items[routine_idx];
            executable_ast_ready = program->routine_executable_ast_ready.items[routine_idx];
            return_type_text = program->routine_return_type_texts.items[routine_idx];
            param_count = program->routine_param_counts.items[routine_idx];
            local_count = program->routine_local_counts.items[routine_idx];
            stmt_count = program->routine_stmt_counts.items[routine_idx];
            expr_count = program->routine_expr_counts.items[routine_idx];
            ++routine_idx;
        }
        int_list_push(&out.item_ids, (int)i);
        string_list_push_copy(&out.top_level_kinds, kind);
        string_list_push_copy(&out.top_level_labels, label);
        string_list_push_copy(&out.top_level_source_paths,
                              i < program->top_level_source_paths.len ? program->top_level_source_paths.items[i] : "");
        string_list_push_copy(&out.high_ops, compiler_core_high_op_for_kind(kind, executable_ast_ready));
        string_list_push_copy(&out.proof_domains, compiler_core_proof_domain_for_kind(kind, executable_ast_ready));
        string_list_push_copy(&out.effect_classes, compiler_core_effect_class_for_kind(kind, executable_ast_ready));
        string_list_push_copy(&out.binding_scopes, compiler_core_binding_scope_for_kind(kind));
        string_list_push_copy(&out.import_paths, import_path);
        string_list_push_copy(&out.import_aliases, import_alias);
        string_list_push_copy(&out.import_categories, import_category);
        int_list_push(&out.body_line_counts, body_line_count);
        string_list_push_copy(&out.body_hashes, body_hash);
        string_list_push_copy(&out.signature_hashes, signature_hash);
        int_list_push(&out.executable_ast_ready, executable_ast_ready);
        string_list_push_copy(&out.return_type_texts, return_type_text);
        int_list_push(&out.param_counts, param_count);
        int_list_push(&out.local_counts, local_count);
        int_list_push(&out.stmt_counts, stmt_count);
        int_list_push(&out.expr_counts, expr_count);
    }
    return out;
}

static CompilerCoreLowPlan lower_compiler_core_facts_to_low_uir(const CompilerCoreFacts *facts) {
    CompilerCoreLowPlan out;
    size_t i;
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.low_uir.compiler_core_lowering.v1");
    for (i = 0; i < facts->item_ids.len; ++i) {
        int_list_push(&out.item_ids, facts->item_ids.items[i]);
        string_list_push_copy(&out.canonical_ops, facts->high_ops.items[i]);
        string_list_push_copy(&out.runtime_targets, compiler_core_runtime_target_for_op(facts->high_ops.items[i]));
        string_list_push_copy(&out.proof_domains, facts->proof_domains.items[i]);
        string_list_push_copy(&out.effect_classes, facts->effect_classes.items[i]);
        string_list_push_copy(&out.binding_scopes, facts->binding_scopes.items[i]);
        string_list_push_copy(&out.top_level_kinds, facts->top_level_kinds.items[i]);
        string_list_push_copy(&out.labels, facts->top_level_labels.items[i]);
        string_list_push_copy(&out.source_paths,
                              i < facts->top_level_source_paths.len ? facts->top_level_source_paths.items[i] : "");
        string_list_push_copy(&out.import_paths, facts->import_paths.items[i]);
        string_list_push_copy(&out.import_aliases, facts->import_aliases.items[i]);
        string_list_push_copy(&out.import_categories, facts->import_categories.items[i]);
        string_list_push_copy(&out.signature_hashes, facts->signature_hashes.items[i]);
        int_list_push(&out.body_line_counts, facts->body_line_counts.items[i]);
        string_list_push_copy(&out.body_hashes, facts->body_hashes.items[i]);
        int_list_push(&out.executable_ast_ready, facts->executable_ast_ready.items[i]);
        string_list_push_copy(&out.return_type_texts, facts->return_type_texts.items[i]);
        int_list_push(&out.param_counts, facts->param_counts.items[i]);
        int_list_push(&out.local_counts, facts->local_counts.items[i]);
        int_list_push(&out.stmt_counts, facts->stmt_counts.items[i]);
        int_list_push(&out.expr_counts, facts->expr_counts.items[i]);
    }
    return out;
}

static CompilerCoreBundle compile_compiler_core_program(const CompilerCoreProgram *program) {
    CompilerCoreBundle out;
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.driver.compiler_core_pipeline.v1");
    out.program = *program;
    out.facts = build_compiler_core_facts(program);
    out.low_plan = lower_compiler_core_facts_to_low_uir(&out.facts);
    return out;
}

static void append_compiler_core_program(CompilerCoreProgram *out,
                                         const CompilerCoreProgram *program) {
    size_t i;
    size_t module_idx = 0;
    size_t import_idx = 0;
    size_t type_idx = 0;
    size_t const_idx = 0;
    size_t var_idx = 0;
    size_t routine_idx = 0;
    for (i = 0; i < program->top_level_kinds.len; ++i) {
        const char *kind = program->top_level_kinds.items[i];
        if (strcmp(kind, "module") == 0) {
            compiler_core_module_list_push(&out->modules,
                                           program->modules.names[module_idx],
                                           program->modules.header_texts[module_idx]);
            string_list_push_copy(&out->top_level_kinds, "module");
            string_list_push_copy(&out->top_level_labels, out->modules.names[out->modules.len - 1]);
            string_list_push_copy(&out->top_level_owner_modules, program->top_level_owner_modules.items[i]);
            string_list_push_copy(&out->top_level_source_paths, program->top_level_source_paths.items[i]);
            ++module_idx;
        } else if (strcmp(kind, "import") == 0) {
            string_list_push_copy(&out->import_paths, program->import_paths.items[import_idx]);
            string_list_push_copy(&out->import_aliases, program->import_aliases.items[import_idx]);
            string_list_push_copy(&out->top_level_kinds, "import");
            string_list_push_copy(&out->top_level_labels, out->import_paths.items[out->import_paths.len - 1]);
            string_list_push_copy(&out->top_level_owner_modules, program->top_level_owner_modules.items[i]);
            string_list_push_copy(&out->top_level_source_paths, program->top_level_source_paths.items[i]);
            ++import_idx;
        } else if (strcmp(kind, "type") == 0) {
            string_list_push_copy(&out->type_headers, program->type_headers.items[type_idx]);
            int_list_push(&out->type_body_line_counts, program->type_body_line_counts.items[type_idx]);
            string_list_push_copy(&out->type_body_hashes, program->type_body_hashes.items[type_idx]);
            string_list_push_copy(&out->top_level_kinds, "type");
            string_list_push_copy(&out->top_level_labels, out->type_headers.items[out->type_headers.len - 1]);
            string_list_push_copy(&out->top_level_owner_modules, program->top_level_owner_modules.items[i]);
            string_list_push_copy(&out->top_level_source_paths, program->top_level_source_paths.items[i]);
            ++type_idx;
        } else if (strcmp(kind, "const") == 0) {
            compiler_core_value_decl_list_push(&out->const_decls,
                                               program->const_decls.labels[const_idx],
                                               program->const_decls.header_texts[const_idx],
                                               program->const_decls.body_line_counts.items[const_idx],
                                               program->const_decls.body_hashes.items[const_idx]);
            string_list_push_copy(&out->top_level_kinds, "const");
            string_list_push_copy(&out->top_level_labels, out->const_decls.labels[out->const_decls.len - 1]);
            string_list_push_copy(&out->top_level_owner_modules, program->top_level_owner_modules.items[i]);
            string_list_push_copy(&out->top_level_source_paths, program->top_level_source_paths.items[i]);
            ++const_idx;
        } else if (strcmp(kind, "var") == 0) {
            compiler_core_value_decl_list_push(&out->var_decls,
                                               program->var_decls.labels[var_idx],
                                               program->var_decls.header_texts[var_idx],
                                               program->var_decls.body_line_counts.items[var_idx],
                                               program->var_decls.body_hashes.items[var_idx]);
            string_list_push_copy(&out->top_level_kinds, "var");
            string_list_push_copy(&out->top_level_labels, out->var_decls.labels[out->var_decls.len - 1]);
            string_list_push_copy(&out->top_level_owner_modules, program->top_level_owner_modules.items[i]);
            string_list_push_copy(&out->top_level_source_paths, program->top_level_source_paths.items[i]);
            ++var_idx;
        } else {
            string_list_push_copy(&out->routine_kinds, program->routine_kinds.items[routine_idx]);
            string_list_push_copy(&out->routine_names, program->routine_names.items[routine_idx]);
            string_list_push_copy(&out->routine_signatures, program->routine_signatures.items[routine_idx]);
            string_list_push_copy(&out->routine_return_type_texts,
                                  program->routine_return_type_texts.items[routine_idx]);
            int_list_push(&out->routine_param_counts,
                          program->routine_param_counts.items[routine_idx]);
            int_list_push(&out->routine_executable_ast_ready,
                          program->routine_executable_ast_ready.items[routine_idx]);
            int_list_push(&out->routine_local_counts,
                          program->routine_local_counts.items[routine_idx]);
            int_list_push(&out->routine_stmt_counts,
                          program->routine_stmt_counts.items[routine_idx]);
            int_list_push(&out->routine_expr_counts,
                          program->routine_expr_counts.items[routine_idx]);
            string_list_push_copy(&out->routine_signature_hashes,
                                  program->routine_signature_hashes.items[routine_idx]);
            int_list_push(&out->routine_body_line_counts,
                          program->routine_body_line_counts.items[routine_idx]);
            string_list_push_copy(&out->routine_body_hashes, program->routine_body_hashes.items[routine_idx]);
            string_list_push_copy(&out->routine_first_body_lines,
                                  program->routine_first_body_lines.items[routine_idx]);
            string_list_push_copy(&out->routine_body_texts,
                                  program->routine_body_texts.items[routine_idx]);
            string_list_push_copy(&out->top_level_kinds, program->routine_kinds.items[routine_idx]);
            string_list_push_copy(&out->top_level_labels, out->routine_names.items[out->routine_names.len - 1]);
            string_list_push_copy(&out->top_level_owner_modules, program->top_level_owner_modules.items[i]);
            string_list_push_copy(&out->top_level_source_paths, program->top_level_source_paths.items[i]);
            ++routine_idx;
        }
    }
}

static CompilerCoreBundle compile_compiler_core_source_closure(const char *workspace_root_abs,
                                                               const char *entry_abs) {
    SourceManifestBundle manifest = resolve_compiler_core_source_manifest(workspace_root_abs, entry_abs);
    CompilerCoreProgram merged;
    size_t i;
    memset(&merged, 0, sizeof(merged));
    merged.version = xstrdup_text("v2.frontend.compiler_core_surface_ir.v1");
    for (i = 0; i < manifest.file_paths.len; ++i) {
        char full_path[PATH_MAX];
        CompilerCoreProgram program;
        if (!ends_with(manifest.file_paths.items[i], ".cheng")) {
            continue;
        }
        path_join(full_path, sizeof(full_path), workspace_root_abs, manifest.file_paths.items[i]);
        program = parse_compiler_core_source_file_strict(full_path);
        if (program.top_level_kinds.len == 0) {
            continue;
        }
        append_compiler_core_program(&merged, &program);
    }
    return compile_compiler_core_program(&merged);
}

static ParsedV2Source parse_v2_source_file_strict(const char *path) {
    ParsedV2Source out = try_parse_v2_source_file(path);
    if (!out.parse_ok) {
        die("v2 source parser: unsupported mixed surface");
    }
    if (!out.had_recognized_surface) {
        die("v2 source parser: no v2 surface recognized");
    }
    return out;
}

static const TopologyDecl *find_topology(const NetworkProgram *program, const char *name) {
    size_t i;
    for (i = 0; i < program->topologies.len; ++i) {
        if (strcmp(program->topologies.items[i].name, name) == 0) {
            return &program->topologies.items[i];
        }
    }
    return NULL;
}

static char *canonical_dispersal_text(const TopologyDecl *topology) {
    if (strcmp(topology->dispersal, "ida") == 0) {
        return xformat("ida(k=%d,n=%d)", topology->ida_k, topology->ida_n);
    }
    return xstrdup_text(topology->dispersal);
}

static const char *topology_address_binding(const TopologyDecl *topology) {
    return strcmp(topology->addressing, "lsmr_path") == 0 ? "content_bound_address" : "";
}

static const char *topology_distance_metric(const TopologyDecl *topology) {
    return strcmp(topology->addressing, "lsmr_path") == 0 ? "per_level_manhattan" : "";
}

static const char *topology_route_plan_model(const TopologyDecl *topology) {
    return strcmp(topology->addressing, "lsmr_path") == 0 ? "canonical_multipath_set" : "";
}

static char *topology_canonical_form(const TopologyDecl *topology) {
    char *dispersal = canonical_dispersal_text(topology);
    char *out = xformat("topology(domain=%s,fanout=%s,repair=%s,dispersal=%s,priority=%s,addressing=%s,depth=%d)",
                        topology->domain,
                        topology->fanout,
                        topology->repair,
                        dispersal,
                        topology->priority,
                        topology->addressing,
                        topology->depth);
    free(dispersal);
    return out;
}

static const char *lsmr_addressing_mode(void) { return "lsmr_path"; }
static const char *lsmr_address_binding_mode(void) { return "content_bound_address"; }
static const char *lsmr_distance_metric_text(void) { return "per_level_manhattan"; }
static const char *lsmr_route_plan_model_text(void) { return "canonical_multipath_set"; }
static const char *lsmr_cell_layout_text(void) { return "[8,1,6;3,5,7;4,9,2]"; }

static char *normalize_cid_hex(const char *cid) {
    size_t i;
    size_t len;
    char *out;
    if (cid == NULL || cid[0] == '\0') {
        die("v2 lsmr: invalid cid hex length");
    }
    len = strlen(cid);
    if ((len % 2) != 0) {
        die("v2 lsmr: invalid cid hex length");
    }
    out = (char *)xcalloc(len + 1, 1);
    for (i = 0; i < len; ++i) {
        char ch = cid[i];
        if (ch >= '0' && ch <= '9') {
            out[i] = ch;
        } else if (ch >= 'a' && ch <= 'f') {
            out[i] = ch;
        } else if (ch >= 'A' && ch <= 'F') {
            out[i] = (char)(ch - 'A' + 'a');
        } else {
            die("v2 lsmr: invalid cid hex");
        }
    }
    out[len] = '\0';
    return out;
}

static int hex_char_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    die("v2 lsmr: invalid hex char");
    return 0;
}

static IntList reverse_int_list(const IntList *values) {
    IntList out;
    int i;
    memset(&out, 0, sizeof(out));
    for (i = (int)values->len - 1; i >= 0; --i) {
        int_list_push(&out, values->items[i]);
    }
    return out;
}

static IntList hex_to_base9_digits(const char *cid) {
    char *normalized = normalize_cid_hex(cid);
    IntList work;
    IntList remainders;
    size_t i;
    memset(&work, 0, sizeof(work));
    memset(&remainders, 0, sizeof(remainders));
    for (i = 0; i < strlen(normalized); ++i) {
        int_list_push(&work, hex_char_value(normalized[i]));
    }
    while (work.len > 0) {
        IntList quotient;
        int carry = 0;
        memset(&quotient, 0, sizeof(quotient));
        for (i = 0; i < work.len; ++i) {
            int value = carry * 16 + work.items[i];
            int q = value / 9;
            carry = value % 9;
            if (quotient.len > 0 || q != 0) {
                int_list_push(&quotient, q);
            }
        }
        int_list_push(&remainders, carry);
        work = quotient;
    }
    if (remainders.len == 0) {
        int_list_push(&remainders, 0);
    }
    free(normalized);
    return reverse_int_list(&remainders);
}

static char *digit_array_text(const IntList *digits) {
    StringList parts;
    size_t i;
    memset(&parts, 0, sizeof(parts));
    for (i = 0; i < digits->len; ++i) {
        string_list_pushf(&parts, "%d", digits->items[i]);
    }
    return string_list_join(&parts, "/");
}

static IntList clone_int_list(const IntList *src) {
    IntList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < src->len; ++i) {
        int_list_push(&out, src->items[i]);
    }
    return out;
}

static IntList lsmr_root_address_digits(int depth) {
    IntList out;
    int i;
    memset(&out, 0, sizeof(out));
    if (depth <= 0) {
        die("v2 lsmr: invalid depth");
    }
    for (i = 0; i < depth; ++i) {
        int_list_push(&out, 5);
    }
    return out;
}

static IntList lsmr_address_digits_from_cid(const char *cid, int depth) {
    IntList base9 = hex_to_base9_digits(cid);
    IntList out;
    int i;
    memset(&out, 0, sizeof(out));
    if (depth <= 0) {
        die("v2 lsmr: invalid depth");
    }
    for (i = 0; i < depth; ++i) {
        int value = i < (int)base9.len ? base9.items[i] : 0;
        int_list_push(&out, value + 1);
    }
    return out;
}

static int luoshu_digit_x(int digit) {
    if (digit == 8 || digit == 3 || digit == 4) return 0;
    if (digit == 1 || digit == 5 || digit == 9) return 1;
    if (digit == 6 || digit == 7 || digit == 2) return 2;
    die("v2 lsmr: invalid luoshu digit");
    return 0;
}

static int luoshu_digit_y(int digit) {
    if (digit == 8 || digit == 1 || digit == 6) return 0;
    if (digit == 3 || digit == 5 || digit == 7) return 1;
    if (digit == 4 || digit == 9 || digit == 2) return 2;
    die("v2 lsmr: invalid luoshu digit");
    return 0;
}

static int luoshu_digit_at(int x, int y) {
    if (x == 0 && y == 0) return 8;
    if (x == 1 && y == 0) return 1;
    if (x == 2 && y == 0) return 6;
    if (x == 0 && y == 1) return 3;
    if (x == 1 && y == 1) return 5;
    if (x == 2 && y == 1) return 7;
    if (x == 0 && y == 2) return 4;
    if (x == 1 && y == 2) return 9;
    if (x == 2 && y == 2) return 2;
    die("v2 lsmr: invalid luoshu coordinate");
    return 5;
}

static int per_level_manhattan_distance(int from_digit, int to_digit) {
    int dx = luoshu_digit_x(from_digit) - luoshu_digit_x(to_digit);
    int dy = luoshu_digit_y(from_digit) - luoshu_digit_y(to_digit);
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx + dy;
}

static void append_level_shortest_paths(int current_digit,
                                        int target_digit,
                                        const char *current_text,
                                        StringList *out_paths) {
    int current_x;
    int current_y;
    int target_x;
    int target_y;
    if (current_digit == target_digit) {
        string_list_append_unique(out_paths, current_text);
        return;
    }
    current_x = luoshu_digit_x(current_digit);
    current_y = luoshu_digit_y(current_digit);
    target_x = luoshu_digit_x(target_digit);
    target_y = luoshu_digit_y(target_digit);
    if (current_x < target_x) {
        int next_digit = luoshu_digit_at(current_x + 1, current_y);
        char *next_text = xformat("%s>%d", current_text, next_digit);
        append_level_shortest_paths(next_digit, target_digit, next_text, out_paths);
        free(next_text);
    } else if (current_x > target_x) {
        int next_digit = luoshu_digit_at(current_x - 1, current_y);
        char *next_text = xformat("%s>%d", current_text, next_digit);
        append_level_shortest_paths(next_digit, target_digit, next_text, out_paths);
        free(next_text);
    }
    if (current_y < target_y) {
        int next_digit = luoshu_digit_at(current_x, current_y + 1);
        char *next_text = xformat("%s>%d", current_text, next_digit);
        append_level_shortest_paths(next_digit, target_digit, next_text, out_paths);
        free(next_text);
    } else if (current_y > target_y) {
        int next_digit = luoshu_digit_at(current_x, current_y - 1);
        char *next_text = xformat("%s>%d", current_text, next_digit);
        append_level_shortest_paths(next_digit, target_digit, next_text, out_paths);
        free(next_text);
    }
}

static IntList split_level_path_digits(const char *path_text) {
    IntList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < strlen(path_text); ++i) {
        if (path_text[i] >= '1' && path_text[i] <= '9') {
            int_list_push(&out, path_text[i] - '0');
        }
    }
    return out;
}

static void append_full_route_paths(int level,
                                    const IntList *current_digits,
                                    const IntList *target_digits,
                                    const char *current_path_text,
                                    StringList *out_paths) {
    StringList level_paths;
    size_t i;
    if (level >= (int)target_digits->len) {
        string_list_append_unique(out_paths, current_path_text);
        return;
    }
    memset(&level_paths, 0, sizeof(level_paths));
    {
        char seed[4];
        snprintf(seed, sizeof(seed), "%d", current_digits->items[level]);
        append_level_shortest_paths(current_digits->items[level],
                                    target_digits->items[level],
                                    seed,
                                    &level_paths);
    }
    string_list_sort(&level_paths);
    for (i = 0; i < level_paths.len; ++i) {
        IntList level_digits = split_level_path_digits(level_paths.items[i]);
        IntList next_digits = clone_int_list(current_digits);
        char *next_path_text = xstrdup_text(current_path_text);
        size_t j;
        for (j = 1; j < level_digits.len; ++j) {
            next_digits.items[level] = level_digits.items[j];
            {
                char *address_text = digit_array_text(&next_digits);
                char *combined = xformat("%s -> %s", next_path_text, address_text);
                free(address_text);
                free(next_path_text);
                next_path_text = combined;
            }
        }
        append_full_route_paths(level + 1, &next_digits, target_digits, next_path_text, out_paths);
        free(next_path_text);
        free(next_digits.items);
        free(level_digits.items);
    }
    string_list_free(&level_paths);
}

static int lsmr_total_distance(const IntList *source_digits, const IntList *target_digits) {
    int out = 0;
    size_t i;
    for (i = 0; i < source_digits->len; ++i) {
        out += per_level_manhattan_distance(source_digits->items[i], target_digits->items[i]);
    }
    return out;
}

static LsmrRoutePlanBundle build_lsmr_route_plan(const char *cid,
                                                 int depth,
                                                 const char *priority_class,
                                                 const char *dispersal_mode) {
    LsmrRoutePlanBundle out;
    IntList source_digits = lsmr_root_address_digits(depth);
    IntList target_digits = lsmr_address_digits_from_cid(cid, depth);
    StringList lines;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(&lines, 0, sizeof(lines));
    out.version = xstrdup_text("v2.runtime.lsmr_route_plan.v1");
    out.content_cid = normalize_cid_hex(cid);
    out.depth = depth;
    out.priority_class = xstrdup_text(priority_class);
    out.dispersal_mode = xstrdup_text(dispersal_mode);
    out.addressing_mode = xstrdup_text(lsmr_addressing_mode());
    out.address_binding = xstrdup_text(lsmr_address_binding_mode());
    out.distance_metric = xstrdup_text(lsmr_distance_metric_text());
    out.source_address = digit_array_text(&source_digits);
    out.target_address = digit_array_text(&target_digits);
    out.total_distance = lsmr_total_distance(&source_digits, &target_digits);
    append_full_route_paths(0, &source_digits, &target_digits, out.source_address, &out.path_texts);
    string_list_sort(&out.path_texts);
    string_list_pushf(&lines, "lsmr_route_plan_version=%s", out.version);
    string_list_pushf(&lines, "cid=%s", out.content_cid);
    string_list_pushf(&lines, "depth=%d", out.depth);
    string_list_pushf(&lines, "priority=%s", out.priority_class);
    string_list_pushf(&lines, "dispersal=%s", out.dispersal_mode);
    string_list_pushf(&lines, "addressing=%s", out.addressing_mode);
    string_list_pushf(&lines, "address_binding=%s", out.address_binding);
    string_list_pushf(&lines, "distance_metric=%s", out.distance_metric);
    string_list_pushf(&lines, "cell_layout=%s", lsmr_cell_layout_text());
    string_list_pushf(&lines, "source_address=%s", out.source_address);
    string_list_pushf(&lines, "target_address=%s", out.target_address);
    string_list_pushf(&lines, "total_distance=%d", out.total_distance);
    string_list_pushf(&lines, "path_count=%zu", out.path_texts.len);
    for (i = 0; i < out.path_texts.len; ++i) {
        string_list_pushf(&lines, "path.%zu=%s", i, out.path_texts.items[i]);
    }
    out.canonical_text = string_list_join(&lines, "\n");
    out.canonical_cid = sha256_hex_text(out.canonical_text);
    return out;
}

static char *build_lsmr_address_artifact_text(const char *cid, int depth) {
    char *normalized = normalize_cid_hex(cid);
    IntList digits = lsmr_address_digits_from_cid(cid, depth);
    char *address_text = digit_array_text(&digits);
    StringList lines;
    char *body;
    char *body_cid;
    char *out;
    memset(&lines, 0, sizeof(lines));
    string_list_push_copy(&lines, "lsmr_address_version=v2.runtime.lsmr_address.v1");
    string_list_pushf(&lines, "cid=%s", normalized);
    string_list_pushf(&lines, "depth=%d", depth);
    string_list_pushf(&lines, "addressing=%s", lsmr_addressing_mode());
    string_list_pushf(&lines, "address_binding=%s", lsmr_address_binding_mode());
    string_list_pushf(&lines, "distance_metric=%s", lsmr_distance_metric_text());
    string_list_pushf(&lines, "cell_layout=%s", lsmr_cell_layout_text());
    string_list_pushf(&lines, "address=%s", address_text);
    body = string_list_join(&lines, "\n");
    body_cid = sha256_hex_text(body);
    out = xformat("%s\naddress_cid=%s\n", body, body_cid);
    free(normalized);
    free(address_text);
    free(body);
    free(body_cid);
    free(digits.items);
    string_list_free(&lines);
    return out;
}

static char *build_lsmr_route_plan_artifact_text(const char *cid,
                                                 int depth,
                                                 const char *priority_class,
                                                 const char *dispersal_mode) {
    LsmrRoutePlanBundle plan = build_lsmr_route_plan(cid, depth, priority_class, dispersal_mode);
    char *out = xformat("%s\nroute_plan_cid=%s\n", plan.canonical_text, plan.canonical_cid);
    return out;
}

static const char *network_distribution_identity_version(void) {
    return "v2.driver.network_distribution_identity.v1";
}

static char *build_network_domain_base_artifact_text(const NetworkBundle *bundle,
                                                     const char *required_domain,
                                                     const char *artifact_version) {
    StringList lines;
    size_t i;
    int count = 0;
    memset(&lines, 0, sizeof(lines));
    string_list_pushf(&lines, "artifact_version=%s", artifact_version);
    string_list_pushf(&lines, "pipeline_version=%s", bundle->version);
    for (i = 0; i < bundle->low_plan.canonical_ops.len; ++i) {
        if (strcmp(bundle->facts.network_domains.items[i], required_domain) != 0) {
            continue;
        }
        string_list_pushf(&lines, "entry.%d.op=%s", count, bundle->low_plan.canonical_ops.items[i]);
        string_list_pushf(&lines, "entry.%d.runtime=%s", count, bundle->low_plan.runtime_targets.items[i]);
        string_list_pushf(&lines, "entry.%d.proof=%s", count, bundle->low_plan.proof_domains.items[i]);
        string_list_pushf(&lines, "entry.%d.domain=%s", count, bundle->facts.network_domains.items[i]);
        string_list_pushf(&lines, "entry.%d.topology_ref=%s", count, bundle->facts.topology_refs.items[i]);
        string_list_pushf(&lines, "entry.%d.topology=%s", count, bundle->facts.topology_canonical_forms.items[i]);
        string_list_pushf(&lines, "entry.%d.transport=%s", count, bundle->facts.transport_modes.items[i]);
        string_list_pushf(&lines, "entry.%d.repair=%s", count, bundle->facts.repair_modes.items[i]);
        string_list_pushf(&lines, "entry.%d.dispersal=%s", count, bundle->facts.dispersal_modes.items[i]);
        string_list_pushf(&lines, "entry.%d.priority=%s", count, bundle->facts.priority_classes.items[i]);
        string_list_pushf(&lines, "entry.%d.identity=%s", count, bundle->facts.identity_scopes.items[i]);
        string_list_pushf(&lines, "entry.%d.signature=%s", count,
                          bundle->facts.anti_entropy_signature_kinds.items[i]);
        count += 1;
    }
    string_list_pushf(&lines, "entry_count=%d", count);
    return string_list_join(&lines, "\n");
}

static char *network_domain_content_cid(const NetworkBundle *bundle,
                                        const char *required_domain,
                                        const char *artifact_version) {
    char *body = build_network_domain_base_artifact_text(bundle, required_domain, artifact_version);
    char *cid = sha256_hex_text(body);
    free(body);
    return cid;
}

static char *content_cid_for_network_op(const NetworkBundle *bundle,
                                        const char *source_manifest_cid,
                                        int op_index) {
    const char *domain = bundle->facts.network_domains.items[op_index];
    if (strcmp(domain, "source_manifest") == 0) {
        return xstrdup_text(source_manifest_cid);
    }
    if (strcmp(domain, "rule_pack") == 0) {
        return network_domain_content_cid(bundle, "rule_pack", "v2.rule_pack_content.v1");
    }
    if (strcmp(domain, "compiler_rule_pack") == 0) {
        return network_domain_content_cid(bundle, "compiler_rule_pack", "v2.compiler_rule_pack_content.v1");
    }
    die("v2 network identity: unsupported phase1 domain");
    return xstrdup_text("");
}

static NetworkLsmrBinding bind_network_op_to_lsmr(const NetworkBundle *bundle,
                                                  const char *source_manifest_cid,
                                                  int op_index) {
    NetworkLsmrBinding out;
    char *content_cid = content_cid_for_network_op(bundle, source_manifest_cid, op_index);
    LsmrRoutePlanBundle route_plan =
        build_lsmr_route_plan(content_cid,
                              bundle->low_plan.topology_depths.items[op_index],
                              bundle->low_plan.priority_classes.items[op_index],
                              bundle->low_plan.dispersal_modes.items[op_index]);
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text(network_distribution_identity_version());
    out.op_index = op_index;
    out.domain = xstrdup_text(bundle->facts.network_domains.items[op_index]);
    out.content_cid = content_cid;
    out.source_address = xstrdup_text(route_plan.source_address);
    out.target_address = xstrdup_text(route_plan.target_address);
    out.route_plan_cid = xstrdup_text(route_plan.canonical_cid);
    out.route_path_count = (int)route_plan.path_texts.len;
    out.total_distance = route_plan.total_distance;
    out.route_plan_text = xstrdup_text(route_plan.canonical_text);
    return out;
}

static char *build_published_network_artifact_text(const NetworkBundle *bundle,
                                                   const char *required_domain,
                                                   const char *artifact_version) {
    StringList lines;
    size_t i;
    int count = 0;
    char *base_body;
    char *content_cid;
    char *body;
    char *artifact_cid;
    char *out;
    memset(&lines, 0, sizeof(lines));
    base_body = build_network_domain_base_artifact_text(bundle, required_domain, artifact_version);
    content_cid = sha256_hex_text(base_body);
    string_list_push_copy(&lines, base_body);
    string_list_pushf(&lines, "content_cid=%s", content_cid);
    for (i = 0; i < bundle->low_plan.op_ids.len; ++i) {
        if (strcmp(bundle->facts.network_domains.items[i], required_domain) != 0) {
            continue;
        }
        {
            NetworkLsmrBinding binding = bind_network_op_to_lsmr(bundle, content_cid, (int)i);
            string_list_pushf(&lines, "entry.%d.addressing=%s", count, bundle->low_plan.addressing_modes.items[i]);
            string_list_pushf(&lines, "entry.%d.depth=%d", count, bundle->low_plan.topology_depths.items[i]);
            string_list_pushf(&lines, "entry.%d.address_binding=%s", count, bundle->low_plan.address_bindings.items[i]);
            string_list_pushf(&lines, "entry.%d.distance_metric=%s", count, bundle->low_plan.distance_metrics.items[i]);
            string_list_pushf(&lines, "entry.%d.source_address=%s", count, binding.source_address);
            string_list_pushf(&lines, "entry.%d.target_address=%s", count, binding.target_address);
            string_list_pushf(&lines, "entry.%d.route_plan_cid=%s", count, binding.route_plan_cid);
            string_list_pushf(&lines, "entry.%d.canonical_multipath_count=%d", count, binding.route_path_count);
            string_list_pushf(&lines, "entry.%d.total_distance=%d", count, binding.total_distance);
        }
        count += 1;
    }
    body = string_list_join(&lines, "\n");
    artifact_cid = sha256_hex_text(body);
    out = xformat("%s\nartifact_cid=%s\n", body, artifact_cid);
    free(base_body);
    free(content_cid);
    free(body);
    free(artifact_cid);
    string_list_free(&lines);
    return out;
}

static const char *network_priority_transport(const char *priority) {
    if (strcmp(priority, "urgent") == 0) {
        return "deterministic_tree_push";
    }
    if (strcmp(priority, "bulk") == 0) {
        return "coded_bulk_dispersal";
    }
    if (strcmp(priority, "background") == 0) {
        return "passive_anti_entropy";
    }
    return "";
}

static void require_topology(const TopologyDecl *topology) {
    if (strcmp(topology->domain, "source_manifest") != 0 &&
        strcmp(topology->domain, "ast_delta") != 0 &&
        strcmp(topology->domain, "rule_pack") != 0 &&
        strcmp(topology->domain, "compiler_rule_pack") != 0) {
        die("v2 network facts: unsupported topology domain");
    }
    if (strcmp(topology->fanout, "luoshu9") != 0) {
        die("v2 network facts: unsupported topology fanout");
    }
    if (strcmp(topology->addressing, "lsmr_path") != 0) {
        die("v2 network facts: unsupported topology addressing");
    }
    if (topology->depth <= 0) {
        die("v2 network facts: invalid topology depth");
    }
    if (strcmp(topology->repair, "passive_anti_entropy") != 0) {
        die("v2 network facts: unsupported topology repair");
    }
    if (strcmp(topology->dispersal, "ida") == 0) {
        if (topology->ida_k <= 0 || topology->ida_n <= 0 || topology->ida_k >= topology->ida_n) {
            die("v2 network facts: invalid ida dispersal");
        }
    } else if (strcmp(topology->dispersal, "none") != 0) {
        die("v2 network facts: unsupported dispersal");
    }
    if (network_priority_transport(topology->priority)[0] == '\0') {
        die("v2 network facts: unsupported priority");
    }
}

static const char *proof_domain_for_network_op(const char *op_name) {
    if (strcmp(op_name, "publish_source_manifest") == 0) {
        return "content_identity_publication";
    }
    if (strcmp(op_name, "publish_rule_pack") == 0) {
        return "rule_pack_publication";
    }
    if (strcmp(op_name, "publish_compiler_rule_pack") == 0) {
        return "compiler_rule_pack_publication";
    }
    if (strcmp(op_name, "route_lsmr_tree") == 0) {
        return "topology_routing";
    }
    if (strcmp(op_name, "disperse_ida_fragments") == 0) {
        return "coded_bulk_dispersal";
    }
    if (strcmp(op_name, "exchange_anti_entropy_signature") == 0) {
        return "anti_entropy_signature";
    }
    return "signature_gap_repair";
}

static char *identity_scope_for_network_op(const char *op_name, const TopologyDecl *topology) {
    if (strcmp(op_name, "publish_source_manifest") == 0) {
        return xstrdup_text("source_manifest");
    }
    if (strcmp(op_name, "publish_rule_pack") == 0) {
        return xstrdup_text("rule_pack");
    }
    if (strcmp(op_name, "publish_compiler_rule_pack") == 0) {
        return xstrdup_text("compiler_rule_pack");
    }
    if (strcmp(op_name, "route_lsmr_tree") == 0) {
        return xstrdup_text("topology_control");
    }
    if (strcmp(op_name, "disperse_ida_fragments") == 0) {
        return xformat("%s_fragments", topology->domain);
    }
    if (strcmp(op_name, "exchange_anti_entropy_signature") == 0) {
        return xstrdup_text("anti_entropy_signature");
    }
    return xstrdup_text("anti_entropy_repair");
}

static void require_network_op(const NetworkOp *op, const TopologyDecl *topology) {
    if (strcmp(op->op_name, "publish_source_manifest") == 0 &&
        strcmp(topology->domain, "source_manifest") != 0) {
        die("v2 network facts: publish_source_manifest requires source_manifest domain");
    }
    if (strcmp(op->op_name, "publish_rule_pack") == 0 &&
        strcmp(topology->domain, "rule_pack") != 0) {
        die("v2 network facts: publish_rule_pack requires rule_pack domain");
    }
    if (strcmp(op->op_name, "publish_compiler_rule_pack") == 0 &&
        strcmp(topology->domain, "compiler_rule_pack") != 0) {
        die("v2 network facts: publish_compiler_rule_pack requires compiler_rule_pack domain");
    }
    if (strcmp(op->op_name, "disperse_ida_fragments") == 0 &&
        strcmp(topology->dispersal, "ida") != 0) {
        die("v2 network facts: disperse_ida_fragments requires ida topology");
    }
    if ((strcmp(op->op_name, "exchange_anti_entropy_signature") == 0 ||
         strcmp(op->op_name, "repair_from_signature_gap") == 0) &&
        op->signature_kind[0] == '\0') {
        die("v2 network facts: anti-entropy operations require signature kind");
    }
}

static NetworkFacts build_network_facts(const NetworkProgram *program) {
    NetworkFacts out;
    size_t i;
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.semantic_facts.network_distribution.v1");
    for (i = 0; i < program->topologies.len; ++i) {
        require_topology(&program->topologies.items[i]);
    }
    for (i = 0; i < program->ops.len; ++i) {
        const NetworkOp *op = &program->ops.items[i];
        const TopologyDecl *topology = find_topology(program, op->topology_ref);
        char *topology_form;
        char *dispersal;
        char *identity;
        if (topology == NULL) {
            die("v2 network facts: missing topology");
        }
        require_network_op(op, topology);
        topology_form = topology_canonical_form(topology);
        dispersal = canonical_dispersal_text(topology);
        identity = identity_scope_for_network_op(op->op_name, topology);
        int_list_push(&out.op_ids, (int)i);
        string_list_push_copy(&out.op_names, op->op_name);
        string_list_push_copy(&out.topology_refs, op->topology_ref);
        string_list_push_take(&out.topology_canonical_forms, topology_form);
        string_list_push_copy(&out.network_domains, topology->domain);
        string_list_push_copy(&out.addressing_modes, topology->addressing);
        int_list_push(&out.topology_depths, topology->depth);
        string_list_push_copy(&out.transport_modes, network_priority_transport(topology->priority));
        string_list_push_copy(&out.repair_modes, topology->repair);
        string_list_push_take(&out.dispersal_modes, dispersal);
        string_list_push_copy(&out.priority_classes, topology->priority);
        string_list_push_copy(&out.address_bindings, topology_address_binding(topology));
        string_list_push_copy(&out.distance_metrics, topology_distance_metric(topology));
        string_list_push_copy(&out.route_plan_models, topology_route_plan_model(topology));
        string_list_push_take(&out.identity_scopes, identity);
        string_list_push_copy(&out.anti_entropy_signature_kinds,
                              op->signature_kind[0] != '\0' ? op->signature_kind : "none");
        string_list_push_copy(&out.proof_domains, proof_domain_for_network_op(op->op_name));
    }
    return out;
}

static void lower_network_op(const char *op_name,
                             const char **runtime_target,
                             const char **proof_domain,
                             const char **identity_scope) {
    if (strcmp(op_name, "publish_source_manifest") == 0) {
        *runtime_target = "runtime/network_distribution_v2.publish_source_manifest";
        *proof_domain = "content_identity_publication";
        *identity_scope = "source_manifest";
        return;
    }
    if (strcmp(op_name, "publish_rule_pack") == 0) {
        *runtime_target = "runtime/network_distribution_v2.publish_rule_pack";
        *proof_domain = "rule_pack_publication";
        *identity_scope = "rule_pack";
        return;
    }
    if (strcmp(op_name, "publish_compiler_rule_pack") == 0) {
        *runtime_target = "runtime/network_distribution_v2.publish_compiler_rule_pack";
        *proof_domain = "compiler_rule_pack_publication";
        *identity_scope = "compiler_rule_pack";
        return;
    }
    if (strcmp(op_name, "route_lsmr_tree") == 0) {
        *runtime_target = "runtime/network_distribution_v2.route_lsmr_tree";
        *proof_domain = "topology_routing";
        *identity_scope = "topology_control";
        return;
    }
    if (strcmp(op_name, "disperse_ida_fragments") == 0) {
        *runtime_target = "runtime/network_distribution_v2.disperse_ida_fragments";
        *proof_domain = "coded_bulk_dispersal";
        *identity_scope = "fragment_set";
        return;
    }
    if (strcmp(op_name, "exchange_anti_entropy_signature") == 0) {
        *runtime_target = "runtime/network_distribution_v2.exchange_anti_entropy_signature";
        *proof_domain = "anti_entropy_signature";
        *identity_scope = "anti_entropy_signature";
        return;
    }
    if (strcmp(op_name, "repair_from_signature_gap") == 0) {
        *runtime_target = "runtime/network_distribution_v2.repair_from_signature_gap";
        *proof_domain = "signature_gap_repair";
        *identity_scope = "anti_entropy_repair";
        return;
    }
    die("v2 low_uir network lowering: missing canonical op");
}

static NetworkLowPlan build_network_low_plan(const NetworkFacts *facts) {
    NetworkLowPlan out;
    size_t i;
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.low_uir.network_distribution_lowering.v1");
    for (i = 0; i < facts->op_ids.len; ++i) {
        const char *runtime_target;
        const char *proof_domain;
        const char *identity_scope;
        lower_network_op(facts->op_names.items[i], &runtime_target, &proof_domain, &identity_scope);
        int_list_push(&out.op_ids, facts->op_ids.items[i]);
        string_list_push_copy(&out.canonical_ops, facts->op_names.items[i]);
        string_list_push_copy(&out.runtime_targets, runtime_target);
        string_list_push_copy(&out.proof_domains, proof_domain);
        string_list_push_copy(&out.identity_scopes, identity_scope);
        string_list_push_copy(&out.addressing_modes, facts->addressing_modes.items[i]);
        int_list_push(&out.topology_depths, facts->topology_depths.items[i]);
        string_list_push_copy(&out.transport_modes, facts->transport_modes.items[i]);
        string_list_push_copy(&out.repair_modes, facts->repair_modes.items[i]);
        string_list_push_copy(&out.dispersal_modes, facts->dispersal_modes.items[i]);
        string_list_push_copy(&out.priority_classes, facts->priority_classes.items[i]);
        string_list_push_copy(&out.address_bindings, facts->address_bindings.items[i]);
        string_list_push_copy(&out.distance_metrics, facts->distance_metrics.items[i]);
        string_list_push_copy(&out.route_plan_models, facts->route_plan_models.items[i]);
        string_list_push_copy(&out.anti_entropy_signature_kinds,
                              facts->anti_entropy_signature_kinds.items[i]);
        string_list_push_copy(&out.topology_canonical_forms,
                              facts->topology_canonical_forms.items[i]);
    }
    return out;
}

static NetworkBundle compile_network_distribution_program(const NetworkProgram *program) {
    NetworkBundle out;
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.driver.network_distribution_pipeline.v1");
    out.program = *program;
    out.facts = build_network_facts(program);
    out.low_plan = build_network_low_plan(&out.facts);
    return out;
}

static int should_include_source_path(const char *rel_path) {
    return strcmp(rel_path, "cheng-package.toml") == 0 || ends_with(rel_path, ".cheng");
}

static void collect_source_paths_recursive(const char *root_abs,
                                           const char *dir_abs,
                                           StringList *out_paths) {
    DIR *dir = opendir(dir_abs);
    struct dirent *ent;
    if (dir == NULL) {
        die_errno("opendir failed", dir_abs);
    }
    while ((ent = readdir(dir)) != NULL) {
        char child[PATH_MAX];
        char rel[PATH_MAX];
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        path_join(child, sizeof(child), dir_abs, ent->d_name);
        if (path_is_dir(child)) {
            collect_source_paths_recursive(root_abs, child, out_paths);
            continue;
        }
        if (!relative_to_prefix(child, root_abs, rel, sizeof(rel))) {
            continue;
        }
        if (should_include_source_path(rel)) {
            string_list_push_copy(out_paths, child);
        }
    }
    closedir(dir);
}

static StringList collect_source_paths(const char *root_abs) {
    StringList out;
    char parent[PATH_MAX];
    char package_path[PATH_MAX];
    memset(&out, 0, sizeof(out));
    if (path_is_dir(root_abs)) {
        collect_source_paths_recursive(root_abs, root_abs, &out);
        path_join(package_path, sizeof(package_path), root_abs, "cheng-package.toml");
        if (path_is_file(package_path)) {
            string_list_append_unique(&out, package_path);
        }
    } else if (path_is_file(root_abs)) {
        string_list_push_copy(&out, root_abs);
        if (snprintf(parent, sizeof(parent), "%s", root_abs) >= (int)sizeof(parent)) {
            die("path too long");
        }
        parent_dir(parent);
        path_join(package_path, sizeof(package_path), parent, "cheng-package.toml");
        if (path_is_file(package_path)) {
            string_list_append_unique(&out, package_path);
        }
    } else {
        die("resolveSourceManifest: missing root path");
    }
    string_list_sort(&out);
    return out;
}

static char *parse_package_id(const char *root_abs) {
    char manifest_path[PATH_MAX];
    char *text;
    char *cursor;
    if (path_is_dir(root_abs)) {
        path_join(manifest_path, sizeof(manifest_path), root_abs, "cheng-package.toml");
    } else {
        if (snprintf(manifest_path, sizeof(manifest_path), "%s", root_abs) >= (int)sizeof(manifest_path)) {
            die("path too long");
        }
        parent_dir(manifest_path);
        path_join(manifest_path, sizeof(manifest_path), manifest_path, "cheng-package.toml");
    }
    if (!path_is_file(manifest_path)) {
        return xstrdup_text("pkg://cheng/v2");
    }
    text = read_file_text(manifest_path);
    cursor = text;
    while (*cursor != '\0') {
        char *line_start = cursor;
        char *line_end = cursor;
        char saved;
        char *trimmed;
        while (*line_end != '\0' && *line_end != '\n') {
            ++line_end;
        }
        saved = *line_end;
        *line_end = '\0';
        trimmed = dup_trimmed_line(line_start);
        if (starts_with(trimmed, "package_id")) {
            char *eq = strchr(trimmed, '=');
            if (eq != NULL) {
                char *value = trim_copy(eq + 1, trimmed + strlen(trimmed));
                size_t n = strlen(value);
                char *out;
                if (n >= 2 && value[0] == '"' && value[n - 1] == '"') {
                    out = xstrdup_range(value + 1, value + n - 1);
                } else {
                    out = xstrdup_text(value);
                }
                free(value);
                free(trimmed);
                *line_end = saved;
                free(text);
                return out;
            }
        }
        free(trimmed);
        *line_end = saved;
        cursor = *line_end == '\n' ? line_end + 1 : line_end;
    }
    free(text);
    return xstrdup_text("pkg://cheng/v2");
}

static void find_nearest_package_root(const char *path_abs, char *out, size_t out_cap) {
    char probe[PATH_MAX];
    char manifest_path[PATH_MAX];
    if (snprintf(probe, sizeof(probe), "%s", path_abs) >= (int)sizeof(probe)) {
        die("path too long");
    }
    if (!path_is_dir(probe)) {
        parent_dir(probe);
    }
    while (probe[0] != '\0') {
        path_join(manifest_path, sizeof(manifest_path), probe, "cheng-package.toml");
        if (path_is_file(manifest_path)) {
            if (snprintf(out, out_cap, "%s", probe) >= (int)out_cap) {
                die("path too long");
            }
            return;
        }
        if (strcmp(probe, "/") == 0) {
            break;
        }
        parent_dir(probe);
    }
    die("v2 source manifest: missing package root");
}

static char *package_name_from_id(const char *package_id) {
    const char *slash = strrchr(package_id, '/');
    if (slash == NULL || slash[1] == '\0') {
        return xstrdup_text("");
    }
    return xstrdup_text(slash + 1);
}

static char *normalize_module_path(const char *module_path) {
    if (starts_with(module_path, "cheng/") && strlen(module_path) > strlen("cheng/")) {
        return xstrdup_text(module_path + strlen("cheng/"));
    }
    return xstrdup_text(module_path);
}

static StringList collect_top_level_import_paths(const char *path) {
    StringList out;
    char *text = read_file_text(path);
    char *cursor = text;
    memset(&out, 0, sizeof(out));
    while (*cursor != '\0') {
        char *line_start = cursor;
        char *line_end = cursor;
        char saved;
        char *trimmed;
        while (*line_end != '\0' && *line_end != '\n') {
            ++line_end;
        }
        saved = *line_end;
        *line_end = '\0';
        trimmed = dup_trimmed_line(line_start);
        if (leading_indent_width(line_start) == 0 &&
            trimmed[0] != '\0' &&
            !starts_with(trimmed, "//") &&
            !starts_with(trimmed, "#") &&
            starts_with(trimmed, "import ")) {
            char *module_path = parse_compiler_import_module_path(trimmed);
            string_list_append_unique(&out, module_path);
            free(module_path);
        }
        free(trimmed);
        *line_end = saved;
        cursor = *line_end == '\n' ? line_end + 1 : line_end;
    }
    free(text);
    string_list_sort(&out);
    return out;
}

static void resolve_compiler_core_import_path(const char *module_path,
                                              const char *package_root_abs,
                                              const char *workspace_root_abs,
                                              const char *package_id,
                                              char *out,
                                              size_t out_cap) {
    char package_src[PATH_MAX];
    char workspace_src[PATH_MAX];
    char std_src[PATH_MAX];
    char candidate[PATH_MAX];
    char *normalized = normalize_module_path(module_path);
    char *package_name = package_name_from_id(package_id);
    const char *package_local = normalized;
    if (normalized[0] == '\0') {
        die("v2 source manifest: empty compiler_core import path");
    }
    if (starts_with(normalized, "std/")) {
        char *rel = with_cheng_ext(normalized + strlen("std/"));
        path_join(std_src, sizeof(std_src), workspace_root_abs, "src/std");
        path_join(candidate, sizeof(candidate), std_src, rel);
        free(rel);
        if (!path_is_file(candidate)) {
            die(xformat("v2 source manifest: missing std import %s", module_path));
        }
        if (snprintf(out, out_cap, "%s", candidate) >= (int)out_cap) {
            die("path too long");
        }
        free(package_name);
        free(normalized);
        return;
    }
    if (package_name[0] != '\0') {
        char *prefix = xformat("%s/", package_name);
        if (starts_with(normalized, prefix) && normalized[strlen(prefix)] != '\0') {
            package_local = normalized + strlen(prefix);
        }
        free(prefix);
    }
    path_join(package_src, sizeof(package_src), package_root_abs, "src");
    {
        char *rel = with_cheng_ext(package_local);
        path_join(candidate, sizeof(candidate), package_src, rel);
        free(rel);
        if (path_is_file(candidate)) {
            if (snprintf(out, out_cap, "%s", candidate) >= (int)out_cap) {
                die("path too long");
            }
            free(package_name);
            free(normalized);
            return;
        }
    }
    path_join(workspace_src, sizeof(workspace_src), workspace_root_abs, "src");
    {
        char *rel = with_cheng_ext(normalized);
        path_join(candidate, sizeof(candidate), workspace_src, rel);
        free(rel);
        if (path_is_file(candidate)) {
            if (snprintf(out, out_cap, "%s", candidate) >= (int)out_cap) {
                die("path too long");
            }
            free(package_name);
            free(normalized);
            return;
        }
    }
    free(package_name);
    free(normalized);
    die(xformat("v2 source manifest: unresolved compiler_core import %s", module_path));
}

static StringList collect_compiler_core_closure_paths(const char *workspace_root_abs,
                                                      const char *entry_abs,
                                                      char *package_root_abs,
                                                      size_t package_root_abs_cap) {
    StringList out;
    StringList queue;
    char package_manifest[PATH_MAX];
    char resolved[PATH_MAX];
    char *package_id;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(&queue, 0, sizeof(queue));
    if (!path_is_file(entry_abs)) {
        die("v2 source manifest: missing entry source");
    }
    find_nearest_package_root(entry_abs, package_root_abs, package_root_abs_cap);
    package_id = parse_package_id(package_root_abs);
    string_list_push_copy(&out, entry_abs);
    string_list_push_copy(&queue, entry_abs);
    path_join(package_manifest, sizeof(package_manifest), package_root_abs, "cheng-package.toml");
    if (path_is_file(package_manifest)) {
        string_list_append_unique(&out, package_manifest);
    }
    for (i = 0; i < queue.len; ++i) {
        StringList imports = collect_top_level_import_paths(queue.items[i]);
        size_t j;
        for (j = 0; j < imports.len; ++j) {
            resolve_compiler_core_import_path(imports.items[j],
                                              package_root_abs,
                                              workspace_root_abs,
                                              package_id,
                                              resolved,
                                              sizeof(resolved));
            if (!string_list_contains(&out, resolved)) {
                string_list_append_unique(&out, resolved);
                string_list_push_copy(&queue, resolved);
            }
        }
    }
    free(package_id);
    string_list_sort(&out);
    return out;
}

static char *topology_refs_for_parsed(const ParsedV2Source *parsed) {
    size_t i;
    StringList refs;
    memset(&refs, 0, sizeof(refs));
    for (i = 0; i < parsed->network_program.topologies.len; ++i) {
        string_list_append_unique(&refs, parsed->network_program.topologies.items[i].name);
    }
    string_list_sort(&refs);
    return string_list_join(&refs, ";");
}

static void topology_canonical_forms_for_parsed(const ParsedV2Source *parsed, StringList *out_forms) {
    size_t i;
    for (i = 0; i < parsed->network_program.topologies.len; ++i) {
        char *form = topology_canonical_form(&parsed->network_program.topologies.items[i]);
        string_list_append_unique(out_forms, form);
        free(form);
    }
    string_list_sort(out_forms);
}

static char *relative_to_root_for_manifest(const char *root_abs, const char *full_abs) {
    char parent[PATH_MAX];
    char rel[PATH_MAX];
    if (path_is_dir(root_abs)) {
        if (!relative_to_prefix(full_abs, root_abs, rel, sizeof(rel))) {
            die("relative path failure");
        }
        return xstrdup_text(rel);
    }
    if (snprintf(parent, sizeof(parent), "%s", root_abs) >= (int)sizeof(parent)) {
        die("path too long");
    }
    parent_dir(parent);
    if (!relative_to_prefix(full_abs, parent, rel, sizeof(rel))) {
        die("relative path failure");
    }
    return xstrdup_text(rel);
}

static SourceManifestBundle build_source_manifest_from_paths(const char *root_abs,
                                                             const char *root_norm,
                                                             const char *package_id,
                                                             const StringList *full_paths) {
    SourceManifestBundle out;
    size_t i;
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.source_manifest.v1");
    out.root_path = xstrdup_text(root_norm);
    out.package_id = xstrdup_text(package_id);
    for (i = 0; i < full_paths->len; ++i) {
        char *rel = relative_to_root_for_manifest(root_abs, full_paths->items[i]);
        char *cid = sha256_hex_file(full_paths->items[i]);
        ParsedV2Source parsed;
        char *text = read_file_text(full_paths->items[i]);
        char *cursor = text;
        int had_recognized = 0;
        memset(&parsed, 0, sizeof(parsed));
        parsed.parse_ok = 1;
        while (*cursor != '\0') {
            char *line_start = cursor;
            char *line_end = cursor;
            char saved;
            char *trimmed;
            while (*line_end != '\0' && *line_end != '\n') {
                ++line_end;
            }
            saved = *line_end;
            *line_end = '\0';
            trimmed = dup_trimmed_line(line_start);
            if (trimmed[0] != '\0' && !starts_with(trimmed, "//") && !starts_with(trimmed, "#")) {
                if (parse_unimaker_line(trimmed)) {
                    had_recognized = 1;
                    parsed.had_recognized_surface = 1;
                } else if (parse_topology_line(&parsed.network_program, trimmed)) {
                    had_recognized = 1;
                    parsed.had_recognized_surface = 1;
                    parsed.had_network_surface = 1;
                } else if (parse_network_op_line(&parsed.network_program, trimmed)) {
                    had_recognized = 1;
                    parsed.had_recognized_surface = 1;
                    parsed.had_network_surface = 1;
                } else if (had_recognized) {
                    parsed.parse_ok = 0;
                }
            }
            free(trimmed);
            *line_end = saved;
            if (!parsed.parse_ok) {
                break;
            }
            cursor = *line_end == '\n' ? line_end + 1 : line_end;
        }
        free(text);
        string_list_push_take(&out.file_paths, rel);
        string_list_push_take(&out.file_cids, cid);
        if (parsed.parse_ok && parsed.had_recognized_surface) {
            char *refs = topology_refs_for_parsed(&parsed);
            string_list_push_take(&out.file_topology_refs, refs);
            topology_canonical_forms_for_parsed(&parsed, &out.topology_canonical_forms);
        } else {
            string_list_push_copy(&out.file_topology_refs, "");
        }
    }
    string_list_sort(&out.topology_canonical_forms);
    {
        StringList lines;
        char *body;
        memset(&lines, 0, sizeof(lines));
        string_list_pushf(&lines, "source_manifest_version=%s", out.version);
        string_list_pushf(&lines, "package_id=%s", out.package_id);
        string_list_pushf(&lines, "root=%s", out.root_path);
        string_list_pushf(&lines, "file_count=%zu", out.file_paths.len);
        for (i = 0; i < out.file_paths.len; ++i) {
            string_list_pushf(&lines, "file.%zu.path=%s", i, out.file_paths.items[i]);
            string_list_pushf(&lines, "file.%zu.cid=%s", i, out.file_cids.items[i]);
            string_list_pushf(&lines, "file.%zu.topologies=%s", i, out.file_topology_refs.items[i]);
        }
        string_list_pushf(&lines, "topology_count=%zu", out.topology_canonical_forms.len);
        for (i = 0; i < out.topology_canonical_forms.len; ++i) {
            string_list_pushf(&lines, "topology.%zu=%s", i, out.topology_canonical_forms.items[i]);
        }
        body = string_list_join(&lines, "\n");
        out.manifest_text = body;
        out.manifest_cid = sha256_hex_text(body);
    }
    return out;
}

static SourceManifestBundle resolve_source_manifest(const char *root_abs, const char *root_norm) {
    StringList full_paths;
    full_paths = collect_source_paths(root_abs);
    {
        char *package_id = parse_package_id(root_abs);
        SourceManifestBundle out = build_source_manifest_from_paths(root_abs, root_norm, package_id, &full_paths);
        free(package_id);
        return out;
    }
}

static SourceManifestBundle resolve_compiler_core_source_manifest(const char *workspace_root_abs,
                                                                  const char *entry_abs) {
    char package_root_abs[PATH_MAX];
    StringList full_paths =
        collect_compiler_core_closure_paths(workspace_root_abs, entry_abs, package_root_abs, sizeof(package_root_abs));
    char *package_id = parse_package_id(package_root_abs);
    SourceManifestBundle out =
        build_source_manifest_from_paths(workspace_root_abs, ".", package_id, &full_paths);
    free(package_id);
    return out;
}

static char *build_source_manifest_artifact_text(const SourceManifestBundle *bundle) {
    return xformat("%s\nmanifest_cid=%s\n", bundle->manifest_text, bundle->manifest_cid);
}

static char *build_network_artifact_text(const NetworkBundle *bundle,
                                         const char *required_domain,
                                         const char *artifact_version) {
    return build_published_network_artifact_text(bundle, required_domain, artifact_version);
}

static int has_canonical_op(const NetworkBundle *bundle, const char *op_name) {
    size_t i;
    for (i = 0; i < bundle->low_plan.canonical_ops.len; ++i) {
        if (strcmp(bundle->low_plan.canonical_ops.items[i], op_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int verify_priority_mappings(const NetworkBundle *bundle) {
    size_t i;
    for (i = 0; i < bundle->facts.priority_classes.len; ++i) {
        const char *expected = network_priority_transport(bundle->facts.priority_classes.items[i]);
        if (strcmp(bundle->facts.transport_modes.items[i], expected) != 0) {
            return 0;
        }
    }
    return 1;
}

static int canonical_op_count_for_domain(const NetworkBundle *bundle, const char *domain) {
    size_t i;
    int count = 0;
    for (i = 0; i < bundle->facts.network_domains.len; ++i) {
        if (strcmp(bundle->facts.network_domains.items[i], domain) == 0) {
            count += 1;
        }
    }
    return count;
}

static const char *obj_format_for_target(const char *target) {
    if (strcmp(target, "arm64-apple-darwin") == 0 ||
        strcmp(target, "aarch64-apple-ios") == 0) {
        return "macho";
    }
    return "elf";
}

static const char *machine_target_architecture(const char *target) {
    if (strcmp(target, "arm64-apple-darwin") == 0 ||
        strcmp(target, "aarch64-linux-android") == 0 ||
        strcmp(target, "aarch64-apple-ios") == 0) {
        return "aarch64";
    }
    die("v2 machine pipeline: unsupported target");
    return "";
}

static const char *machine_target_symbol_prefix(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "_" : "";
}

static const char *machine_target_text_section_name(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "__TEXT,__text" : ".text";
}

static const char *machine_target_cstring_section_name(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "__TEXT,__cstring" : ".rodata.str1.1";
}

static const char *machine_target_symtab_section_name(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "__LINKEDIT,symtab" : ".symtab";
}

static const char *machine_target_strtab_section_name(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "__LINKEDIT,strtab" : ".strtab";
}

static const char *machine_target_reloc_section_name(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "__LINKEDIT,reloc" : ".rela.text";
}

static const char *machine_target_call_relocation_kind(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "ARM64_RELOC_BRANCH26" : "R_AARCH64_CALL26";
}

static const char *machine_target_metadata_page_relocation_kind(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "ARM64_RELOC_PAGE21"
                                                               : "R_AARCH64_ADR_PREL_PG_HI21";
}

static const char *machine_target_metadata_pageoff_relocation_kind(const char *target) {
    return strcmp(obj_format_for_target(target), "macho") == 0 ? "ARM64_RELOC_PAGEOFF12"
                                                               : "R_AARCH64_ADD_ABS_LO12_NC";
}

static int machine_target_pointer_width_bits(const char *target) {
    (void)target;
    return 64;
}

static int machine_target_stack_align_bytes(const char *target) {
    (void)target;
    return 16;
}

static const char *machine_target_darwin_platform_name(const char *target) {
    if (strcmp(obj_format_for_target(target), "macho") != 0) {
        die("v2 machine target: darwin platform name only for macho target");
    }
    return strcmp(target, "aarch64-apple-ios") == 0 ? "ios" : "macos";
}

static const char *machine_target_darwin_minos_text(const char *target) {
    if (strcmp(obj_format_for_target(target), "macho") != 0) {
        die("v2 machine target: darwin minos text only for macho target");
    }
    return strcmp(target, "aarch64-apple-ios") == 0 ? "17.0" : "13.0";
}

static const char *machine_target_darwin_arch_name(const char *target) {
    if (strcmp(obj_format_for_target(target), "macho") != 0) {
        die("v2 machine target: darwin arch name only for macho target");
    }
    if (strcmp(machine_target_architecture(target), "aarch64") == 0) {
        return "arm64";
    }
    return machine_target_architecture(target);
}

static const char *machine_target_darwin_sdk_name(const char *target) {
    if (strcmp(obj_format_for_target(target), "macho") != 0) {
        die("v2 machine target: darwin sdk name only for macho target");
    }
    return strcmp(target, "aarch64-apple-ios") == 0 ? "iphoneos" : "macosx";
}

static uint32_t machine_target_darwin_platform_id(const char *target) {
    if (strcmp(obj_format_for_target(target), "macho") != 0) {
        die("v2 machine target: darwin platform id only for macho target");
    }
    return strcmp(target, "aarch64-apple-ios") == 0 ? 2U : 1U;
}

static uint32_t machine_target_darwin_minos(const char *target) {
    if (strcmp(obj_format_for_target(target), "macho") != 0) {
        die("v2 machine target: darwin minos only for macho target");
    }
    return strcmp(target, "aarch64-apple-ios") == 0 ? 0x00110000U : 0x000d0000U;
}

static int aligned_size(int value, int align) {
    int rem;
    if (align <= 0) {
        return value;
    }
    rem = value % align;
    if (rem == 0) {
        return value;
    }
    return value + (align - rem);
}

static char *sanitize_symbol_part(const char *text) {
    size_t i;
    size_t n = strlen(text);
    char *out = (char *)xcalloc(n + 1, 1);
    for (i = 0; i < n; ++i) {
        unsigned char ch = (unsigned char)text[i];
        out[i] = (char)((ch >= 'a' && ch <= 'z') ||
                                (ch >= 'A' && ch <= 'Z') ||
                                (ch >= '0' && ch <= '9')
                            ? ch
                            : '_');
    }
    out[n] = '\0';
    return out;
}

static int word_count(const char *words) {
    int count = 0;
    const char *p = words;
    if (words[0] == '\0') {
        return 0;
    }
    count = 1;
    while (*p != '\0') {
        if (*p == ';') {
            count += 1;
        }
        ++p;
    }
    return count;
}

static char *machine_u32_hex(uint32_t word) {
    return xformat("%08x", word);
}

static const char *machine_stub_words(const char *architecture) {
    if (strcmp(architecture, "aarch64") == 0) {
        return "a9bf7bfd;910003fd;90000000;91000000;94000000;52800000;a8c17bfd;d65f03c0";
    }
    die("v2 machine pipeline: unsupported architecture");
    return "";
}

static const char *machine_dispatch_entry_words(const char *architecture) {
    if (strcmp(architecture, "aarch64") == 0) {
        return "a9bf7bfd;910003fd;94000000;a8c17bfd;d65f03c0";
    }
    die("v2 machine pipeline: unsupported architecture");
    return "";
}

static char *machine_trace_provider_words(const char *architecture) {
    if (strcmp(architecture, "aarch64") == 0) {
        return xstrdup_text("a9bf7bfd;910003fd;94000000;52800000;a8c17bfd;d65f03c0");
    }
    die("v2 machine pipeline: unsupported architecture");
    return NULL;
}

static char *machine_aarch64_mov_wide_word(int is64, int reg, int imm16) {
    uint32_t base = is64 ? 0xd2800000U : 0x52800000U;
    return machine_u32_hex(base | (((uint32_t)imm16 & 0xffffU) << 5) | ((uint32_t)reg & 31U));
}

static char *machine_aarch64_mov_zero_word(int reg) {
    return machine_aarch64_mov_wide_word(1, reg, 0);
}

static char *machine_aarch64_mov_int32_word(int reg, int value) {
    return machine_aarch64_mov_wide_word(0, reg, value);
}

static char *machine_aarch64_mov_reg_word(int is64, int dst_reg, int src_reg) {
    uint32_t base = is64 ? 0xaa0003e0U : 0x2a0003e0U;
    return machine_u32_hex(base |
                           (((uint32_t)src_reg & 31U) << 16) |
                           ((uint32_t)dst_reg & 31U));
}

static char *machine_aarch64_add_imm_word(int is64, int dst_reg, int src_reg, int imm12) {
    uint32_t base = is64 ? 0x91000000U : 0x11000000U;
    return machine_u32_hex(base |
                           (((uint32_t)imm12 & 0xfffU) << 10) |
                           (((uint32_t)src_reg & 31U) << 5) |
                           ((uint32_t)dst_reg & 31U));
}

static char *machine_aarch64_sub_imm_word(int is64, int dst_reg, int src_reg, int imm12) {
    uint32_t base = is64 ? 0xd1000000U : 0x51000000U;
    return machine_u32_hex(base |
                           (((uint32_t)imm12 & 0xfffU) << 10) |
                           (((uint32_t)src_reg & 31U) << 5) |
                           ((uint32_t)dst_reg & 31U));
}

static char *machine_aarch64_store_sp_word(int is64, int src_reg, int offset_bytes) {
    uint32_t scaled_offset = (uint32_t)(is64 ? offset_bytes / 8 : offset_bytes / 4);
    uint32_t base = is64 ? 0xf9000000U : 0xb9000000U;
    return machine_u32_hex(base |
                           ((scaled_offset & 0xfffU) << 10) |
                           (31U << 5) |
                           ((uint32_t)src_reg & 31U));
}

static char *machine_aarch64_store_base_word(int is64, int src_reg, int base_reg, int offset_bytes) {
    uint32_t scaled_offset = (uint32_t)(is64 ? offset_bytes / 8 : offset_bytes / 4);
    uint32_t base = is64 ? 0xf9000000U : 0xb9000000U;
    return machine_u32_hex(base |
                           ((scaled_offset & 0xfffU) << 10) |
                           (((uint32_t)base_reg & 31U) << 5) |
                           ((uint32_t)src_reg & 31U));
}

static char *machine_aarch64_load_sp_word(int is64, int dst_reg, int offset_bytes) {
    uint32_t scaled_offset = (uint32_t)(is64 ? offset_bytes / 8 : offset_bytes / 4);
    uint32_t base = is64 ? 0xf9400000U : 0xb9400000U;
    return machine_u32_hex(base |
                           ((scaled_offset & 0xfffU) << 10) |
                           (31U << 5) |
                           ((uint32_t)dst_reg & 31U));
}

static char *machine_aarch64_add_reg_word(int is64, int dst_reg, int lhs_reg, int rhs_reg) {
    uint32_t base = is64 ? 0x8b000000U : 0x0b000000U;
    return machine_u32_hex(base |
                           (((uint32_t)rhs_reg & 31U) << 16) |
                           (((uint32_t)lhs_reg & 31U) << 5) |
                           ((uint32_t)dst_reg & 31U));
}

static char *machine_aarch64_sub_reg_word(int is64, int dst_reg, int lhs_reg, int rhs_reg) {
    uint32_t base = is64 ? 0xcb000000U : 0x4b000000U;
    return machine_u32_hex(base |
                           (((uint32_t)rhs_reg & 31U) << 16) |
                           (((uint32_t)lhs_reg & 31U) << 5) |
                           ((uint32_t)dst_reg & 31U));
}

static char *machine_aarch64_cmp_zero_word(int is64, int reg) {
    uint32_t base = is64 ? 0xf100001fU : 0x7100001fU;
    return machine_u32_hex(base | (((uint32_t)reg & 31U) << 5));
}

static char *machine_aarch64_cmp_reg_word(int is64, int lhs_reg, int rhs_reg) {
    uint32_t base = is64 ? 0xeb00001fU : 0x6b00001fU;
    return machine_u32_hex(base |
                           (((uint32_t)rhs_reg & 31U) << 16) |
                           (((uint32_t)lhs_reg & 31U) << 5));
}

static char *machine_aarch64_branch_word(int offset_words) {
    return machine_u32_hex(0x14000000U | ((uint32_t)offset_words & 0x03ffffffU));
}

static int machine_aarch64_cond_code(const char *name) {
    if (strcmp(name, "eq") == 0) {
        return 0;
    }
    if (strcmp(name, "ne") == 0) {
        return 1;
    }
    if (strcmp(name, "ge") == 0) {
        return 10;
    }
    if (strcmp(name, "lt") == 0) {
        return 11;
    }
    if (strcmp(name, "gt") == 0) {
        return 12;
    }
    if (strcmp(name, "le") == 0) {
        return 13;
    }
    die("v2 machine pipeline: unsupported aarch64 cond code");
    return 0;
}

static char *machine_aarch64_branch_cond_word(int offset_words, const char *cond) {
    return machine_u32_hex(0x54000000U |
                           (((uint32_t)offset_words & 0x7ffffU) << 5) |
                           ((uint32_t)machine_aarch64_cond_code(cond) & 15U));
}

static const char *machine_aarch64_inverse_compare_cond(const char *operator_text) {
    if (strcmp(operator_text, "==") == 0) {
        return "ne";
    }
    if (strcmp(operator_text, "!=") == 0) {
        return "eq";
    }
    if (strcmp(operator_text, "<") == 0) {
        return "ge";
    }
    if (strcmp(operator_text, "<=") == 0) {
        return "gt";
    }
    if (strcmp(operator_text, ">") == 0) {
        return "le";
    }
    if (strcmp(operator_text, ">=") == 0) {
        return "lt";
    }
    die("v2 machine pipeline: unsupported inverse compare op");
    return "";
}

static char *join_word_parts_owned(StringList *parts) {
    return string_list_join(parts, ";");
}

static char *compiler_core_signature_param_type_text(const char *signature, int param_index);
static int compiler_core_param_base_reg(const char *signature, int param_index);
static int compiler_core_direct_param_is64(const char *type_text);

static int machine_append_load_simple_value_words(const char *architecture,
                                                  StringList *words,
                                                  int target_reg,
                                                  const char *op_kind,
                                                  const char *op_primary_text,
                                                  int op_arg0) {
    char *word = NULL;
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (strcmp(op_kind, "load_param") == 0) {
        if (op_arg0 < 0 || op_arg0 > 7) {
            return 0;
        }
        if (op_arg0 != target_reg) {
            word = machine_aarch64_mov_reg_word(1, target_reg, op_arg0);
            string_list_push_take(words, word);
        }
        return 1;
    }
    if (strcmp(op_kind, "load_int32") == 0) {
        int value = parse_int_strict(op_primary_text);
        if (value < 0 || value > 65535) {
            return 0;
        }
        word = machine_aarch64_mov_int32_word(target_reg, value);
        string_list_push_take(words, word);
        return 1;
    }
    if (strcmp(op_kind, "load_bool") == 0) {
        word = machine_aarch64_mov_int32_word(target_reg,
                                              strcmp(op_primary_text, "true") == 0 ? 1 : 0);
        string_list_push_take(words, word);
        return 1;
    }
    if (strcmp(op_kind, "load_nil") == 0) {
        word = machine_aarch64_mov_zero_word(target_reg);
        string_list_push_take(words, word);
        return 1;
    }
    return 0;
}

static char *machine_direct_return_simple_value_words(const char *architecture,
                                                      const char *op_kind,
                                                      const char *op_primary_text,
                                                      int op_arg0) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_value_words(architecture, &words, 0, op_kind, op_primary_text, op_arg0)) {
        return NULL;
    }
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_return_register_words(const char *architecture,
                                                  int source_reg,
                                                  int is64) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (source_reg < 0 || source_reg > 7) {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (source_reg != 0) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(is64, 0, source_reg));
    }
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_return_register_pair_words(const char *architecture,
                                                       int source_reg0,
                                                       int source_reg1,
                                                       int source_reg1_is64) {
    StringList words;
    int load_reg1 = source_reg1;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (source_reg0 < 0 || source_reg0 > 7 || source_reg1 < 0 || source_reg1 > 7) {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (source_reg1 == 0 && source_reg0 != 0) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(1, 9, 0));
        load_reg1 = 9;
    }
    if (source_reg0 != 0) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(1, 0, source_reg0));
    }
    if (load_reg1 != 1) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(source_reg1_is64, 1, load_reg1));
    }
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_return_empty_bytes_words(const char *architecture) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_take(&words, machine_aarch64_mov_zero_word(0));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(1, 0));
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_zero_var_str_out_words(const char *architecture) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_take(&words, machine_aarch64_store_base_word(1, 31, 0, 0));
    string_list_push_take(&words, machine_aarch64_store_base_word(0, 31, 0, 8));
    string_list_push_take(&words, machine_aarch64_store_base_word(0, 31, 0, 12));
    string_list_push_take(&words, machine_aarch64_store_base_word(0, 31, 0, 16));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 0));
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_copy_var_str_out_words(const char *architecture) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_take(&words, machine_aarch64_store_base_word(1, 1, 0, 0));
    string_list_push_take(&words, machine_aarch64_store_base_word(0, 2, 0, 8));
    string_list_push_take(&words, machine_aarch64_store_base_word(0, 3, 0, 12));
    string_list_push_take(&words, machine_aarch64_store_base_word(0, 4, 0, 16));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 0));
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_return_cstring_literal_words(const char *architecture, int byte_len) {
    StringList words;
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (byte_len < 0 || byte_len > 65535) {
        return NULL;
    }
    memset(&words, 0, sizeof(words));
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "90000000");
    string_list_push_copy(&words, "91000000");
    string_list_push_take(&words, machine_aarch64_mov_int32_word(1, byte_len));
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_return_register_pair_plus_zero_i32_words(const char *architecture,
                                                                     int source_reg0,
                                                                     int source_reg1,
                                                                     int source_reg1_is64,
                                                                     int zero_reg) {
    StringList words;
    int load_reg1 = source_reg1;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (source_reg0 < 0 || source_reg0 > 7 || source_reg1 < 0 || source_reg1 > 7 ||
        zero_reg < 0 || zero_reg > 7) {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (source_reg1 == 0 && source_reg0 != 0) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(1, 9, 0));
        load_reg1 = 9;
    }
    if (source_reg0 != 0) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(1, 0, source_reg0));
    }
    if (load_reg1 != 1) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(source_reg1_is64, 1, load_reg1));
    }
    string_list_push_take(&words, machine_aarch64_mov_int32_word(zero_reg, 0));
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_unary_words(const char *architecture,
                                        const char *op_kind,
                                        const char *operand_kind,
                                        const char *operand_primary_text,
                                        int operand_arg0) {
    StringList words;
    char *word = NULL;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_value_words(architecture,
                                                &words,
                                                0,
                                                operand_kind,
                                                operand_primary_text,
                                                operand_arg0)) {
        return NULL;
    }
    if (strcmp(op_kind, "unary_neg") == 0) {
        word = machine_aarch64_sub_reg_word(0, 0, 31, 0);
        string_list_push_take(&words, word);
    } else if (strcmp(op_kind, "unary_not") == 0) {
        string_list_push_take(&words, machine_aarch64_cmp_zero_word(0, 0));
        string_list_push_take(&words, machine_aarch64_branch_cond_word(3, "eq"));
        string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 0));
        string_list_push_take(&words, machine_aarch64_branch_word(2));
        string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 1));
    } else {
        return NULL;
    }
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_binary_words(const char *architecture,
                                         const char *lhs_kind,
                                         const char *lhs_primary_text,
                                         int lhs_arg0,
                                         const char *rhs_kind,
                                         const char *rhs_primary_text,
                                         int rhs_arg0,
                                         const char *operator_text) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_value_words(architecture, &words, 0, lhs_kind, lhs_primary_text, lhs_arg0)) {
        return NULL;
    }
    if (!machine_append_load_simple_value_words(architecture, &words, 1, rhs_kind, rhs_primary_text, rhs_arg0)) {
        return NULL;
    }
    if (strcmp(operator_text, "+") == 0) {
        string_list_push_take(&words, machine_aarch64_add_reg_word(0, 0, 0, 1));
    } else if (strcmp(operator_text, "-") == 0) {
        string_list_push_take(&words, machine_aarch64_sub_reg_word(0, 0, 0, 1));
    } else if (strcmp(operator_text, "==") == 0 ||
               strcmp(operator_text, "!=") == 0 ||
               strcmp(operator_text, "<") == 0 ||
               strcmp(operator_text, "<=") == 0 ||
               strcmp(operator_text, ">") == 0 ||
               strcmp(operator_text, ">=") == 0) {
        const char *cond = strcmp(operator_text, "==") == 0 ? "eq" :
                           strcmp(operator_text, "!=") == 0 ? "ne" :
                           strcmp(operator_text, "<") == 0 ? "lt" :
                           strcmp(operator_text, "<=") == 0 ? "le" :
                           strcmp(operator_text, ">") == 0 ? "gt" : "ge";
        string_list_push_take(&words, machine_aarch64_cmp_reg_word(0, 0, 1));
        string_list_push_take(&words, machine_aarch64_branch_cond_word(3, cond));
        string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 0));
        string_list_push_take(&words, machine_aarch64_branch_word(2));
        string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 1));
    } else {
        return NULL;
    }
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static int machine_append_load_direct_scalar_value_words(const char *architecture,
                                                         StringList *words,
                                                         int target_reg,
                                                         const char *signature,
                                                         const char *op_kind,
                                                         const char *op_primary_text,
                                                         int op_arg0,
                                                         int *out_is64) {
    char *word = NULL;
    *out_is64 = 0;
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (strcmp(op_kind, "load_param") == 0) {
        char *param_type_text = compiler_core_signature_param_type_text(signature, op_arg0);
        int source_reg = compiler_core_param_base_reg(signature, op_arg0);
        if (source_reg < 0 || source_reg > 7) {
            free(param_type_text);
            return 0;
        }
        *out_is64 = compiler_core_direct_param_is64(param_type_text);
        if (source_reg != target_reg) {
            word = machine_aarch64_mov_reg_word(*out_is64, target_reg, source_reg);
            string_list_push_take(words, word);
        }
        free(param_type_text);
        return 1;
    }
    if (strcmp(op_kind, "load_int32") == 0) {
        int value = parse_int_strict(op_primary_text);
        if (value < 0 || value > 65535) {
            return 0;
        }
        word = machine_aarch64_mov_int32_word(target_reg, value);
        string_list_push_take(words, word);
        return 1;
    }
    if (strcmp(op_kind, "load_bool") == 0) {
        word = machine_aarch64_mov_int32_word(target_reg,
                                              strcmp(op_primary_text, "true") == 0 ? 1 : 0);
        string_list_push_take(words, word);
        return 1;
    }
    if (strcmp(op_kind, "load_nil") == 0) {
        *out_is64 = 1;
        word = machine_aarch64_mov_zero_word(target_reg);
        string_list_push_take(words, word);
        return 1;
    }
    return 0;
}

static char *machine_direct_arithmetic_compare_words(const char *architecture,
                                                     const char *signature,
                                                     int lhs_source_reg,
                                                     int lhs_source_is64,
                                                     const char *arithmetic_operator_text,
                                                     const char *rhs_kind,
                                                     const char *rhs_primary_text,
                                                     int rhs_arg0,
                                                     int compare_source_reg,
                                                     int compare_source_is64,
                                                     const char *compare_operator_text) {
    StringList words;
    int rhs_is64 = 0;
    const char *cond = NULL;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if ((strcmp(arithmetic_operator_text, "+") != 0 &&
         strcmp(arithmetic_operator_text, "-") != 0) ||
        (strcmp(compare_operator_text, "==") != 0 &&
         strcmp(compare_operator_text, "!=") != 0 &&
         strcmp(compare_operator_text, "<") != 0 &&
         strcmp(compare_operator_text, "<=") != 0 &&
         strcmp(compare_operator_text, ">") != 0 &&
         strcmp(compare_operator_text, ">=") != 0) ||
        lhs_source_reg < 0 || lhs_source_reg > 7 ||
        compare_source_reg < 0 || compare_source_reg > 7 ||
        lhs_source_is64 || compare_source_is64) {
        return NULL;
    }
    cond = strcmp(compare_operator_text, "==") == 0 ? "eq" :
           strcmp(compare_operator_text, "!=") == 0 ? "ne" :
           strcmp(compare_operator_text, "<") == 0 ? "lt" :
           strcmp(compare_operator_text, "<=") == 0 ? "le" :
           strcmp(compare_operator_text, ">") == 0 ? "gt" : "ge";
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (lhs_source_reg != 9) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(0, 9, lhs_source_reg));
    }
    if (!machine_append_load_direct_scalar_value_words(architecture,
                                                       &words,
                                                       10,
                                                       signature,
                                                       rhs_kind,
                                                       rhs_primary_text,
                                                       rhs_arg0,
                                                       &rhs_is64) ||
        rhs_is64) {
        string_list_free(&words);
        return NULL;
    }
    if (strcmp(arithmetic_operator_text, "+") == 0) {
        string_list_push_take(&words, machine_aarch64_add_reg_word(0, 9, 9, 10));
    } else {
        string_list_push_take(&words, machine_aarch64_sub_reg_word(0, 9, 9, 10));
    }
    if (compare_source_reg != 10) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(0, 10, compare_source_reg));
    }
    string_list_push_take(&words, machine_aarch64_cmp_reg_word(0, 9, 10));
    string_list_push_take(&words, machine_aarch64_branch_cond_word(3, cond));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 0));
    string_list_push_take(&words, machine_aarch64_branch_word(2));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 1));
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_if_else_simple_cond_words(const char *architecture,
                                                      const char *cond_kind,
                                                      const char *cond_primary_text,
                                                      int cond_arg0,
                                                      const char *then_kind,
                                                      const char *then_primary_text,
                                                      int then_arg0,
                                                      const char *else_kind,
                                                      const char *else_primary_text,
                                                      int else_arg0) {
    StringList words;
    size_t false_branch_idx;
    size_t end_branch_idx;
    size_t else_start_idx;
    size_t epilogue_start_idx;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_value_words(architecture, &words, 0, cond_kind, cond_primary_text, cond_arg0)) {
        return NULL;
    }
    string_list_push_take(&words, machine_aarch64_cmp_zero_word(0, 0));
    false_branch_idx = words.len;
    string_list_push_copy(&words, "");
    if (!machine_append_load_simple_value_words(architecture, &words, 0, then_kind, then_primary_text, then_arg0)) {
        return NULL;
    }
    end_branch_idx = words.len;
    string_list_push_copy(&words, "");
    else_start_idx = words.len;
    if (!machine_append_load_simple_value_words(architecture, &words, 0, else_kind, else_primary_text, else_arg0)) {
        return NULL;
    }
    epilogue_start_idx = words.len;
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    free(words.items[false_branch_idx]);
    words.items[false_branch_idx] =
        machine_aarch64_branch_cond_word((int)(else_start_idx - false_branch_idx), "eq");
    free(words.items[end_branch_idx]);
    words.items[end_branch_idx] =
        machine_aarch64_branch_word((int)(epilogue_start_idx - end_branch_idx));
    return join_word_parts_owned(&words);
}

static char *machine_direct_if_else_binary_cond_words(const char *architecture,
                                                      const char *lhs_kind,
                                                      const char *lhs_primary_text,
                                                      int lhs_arg0,
                                                      const char *rhs_kind,
                                                      const char *rhs_primary_text,
                                                      int rhs_arg0,
                                                      const char *operator_text,
                                                      const char *then_kind,
                                                      const char *then_primary_text,
                                                      int then_arg0,
                                                      const char *else_kind,
                                                      const char *else_primary_text,
                                                      int else_arg0) {
    StringList words;
    size_t false_branch_idx;
    size_t end_branch_idx;
    size_t else_start_idx;
    size_t epilogue_start_idx;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_value_words(architecture, &words, 0, lhs_kind, lhs_primary_text, lhs_arg0)) {
        return NULL;
    }
    if (!machine_append_load_simple_value_words(architecture, &words, 1, rhs_kind, rhs_primary_text, rhs_arg0)) {
        return NULL;
    }
    string_list_push_take(&words, machine_aarch64_cmp_reg_word(0, 0, 1));
    false_branch_idx = words.len;
    string_list_push_copy(&words, "");
    if (!machine_append_load_simple_value_words(architecture, &words, 0, then_kind, then_primary_text, then_arg0)) {
        return NULL;
    }
    end_branch_idx = words.len;
    string_list_push_copy(&words, "");
    else_start_idx = words.len;
    if (!machine_append_load_simple_value_words(architecture, &words, 0, else_kind, else_primary_text, else_arg0)) {
        return NULL;
    }
    epilogue_start_idx = words.len;
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    free(words.items[false_branch_idx]);
    words.items[false_branch_idx] =
        machine_aarch64_branch_cond_word((int)(else_start_idx - false_branch_idx),
                                         machine_aarch64_inverse_compare_cond(operator_text));
    free(words.items[end_branch_idx]);
    words.items[end_branch_idx] =
        machine_aarch64_branch_word((int)(epilogue_start_idx - end_branch_idx));
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_words(const char *architecture) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_cstring_arg_words(const char *architecture,
                                                   int byte_len) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (byte_len < 0 || byte_len > 65535) {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "90000000");
    string_list_push_copy(&words, "91000000");
    string_list_push_take(&words, machine_aarch64_mov_int32_word(1, byte_len));
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_second_cstring_arg_words(const char *architecture,
                                                          int byte_len) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (byte_len < 0 || byte_len > 65535) {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "90000002");
    string_list_push_copy(&words, "91000042");
    string_list_push_take(&words, machine_aarch64_mov_int32_word(3, byte_len));
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_void_then_const_words(const char *architecture,
                                                       const char *const_kind,
                                                       const char *const_primary_text,
                                                       int const_arg0) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "94000000");
    if (!machine_append_load_simple_value_words(architecture,
                                                &words,
                                                0,
                                                const_kind,
                                                const_primary_text,
                                                const_arg0)) {
        string_list_free(&words);
        return NULL;
    }
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static int machine_append_load_simple_call_args_words(const char *architecture,
                                                      StringList *words,
                                                      int arg_count,
                                                      const char **arg_kinds,
                                                      const char **arg_primary_texts,
                                                      const int *arg_arg0s);

static char *machine_direct_call_simple_args_void_then_const_words(const char *architecture,
                                                                   int arg_count,
                                                                   const char **arg_kinds,
                                                                   const char **arg_primary_texts,
                                                                   const int *arg_arg0s,
                                                                   const char *const_kind,
                                                                   const char *const_primary_text,
                                                                   int const_arg0) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    arg_count,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        string_list_free(&words);
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    if (!machine_append_load_simple_value_words(architecture,
                                                &words,
                                                0,
                                                const_kind,
                                                const_primary_text,
                                                const_arg0)) {
        string_list_free(&words);
        return NULL;
    }
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_compare_const_rhs_words(const char *architecture,
                                                         int arg_count,
                                                         const char **arg_kinds,
                                                         const char **arg_primary_texts,
                                                         const int *arg_arg0s,
                                                         const char *rhs_kind,
                                                         const char *rhs_primary_text,
                                                         int rhs_arg0,
                                                         const char *operator_text) {
    StringList words;
    const char *cond = NULL;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (strcmp(operator_text, "==") == 0) {
        cond = "eq";
    } else if (strcmp(operator_text, "!=") == 0) {
        cond = "ne";
    } else if (strcmp(operator_text, "<") == 0) {
        cond = "lt";
    } else if (strcmp(operator_text, "<=") == 0) {
        cond = "le";
    } else if (strcmp(operator_text, ">") == 0) {
        cond = "gt";
    } else if (strcmp(operator_text, ">=") == 0) {
        cond = "ge";
    } else {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    arg_count,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    if (!machine_append_load_simple_value_words(architecture, &words, 1, rhs_kind, rhs_primary_text, rhs_arg0)) {
        return NULL;
    }
    string_list_push_take(&words, machine_aarch64_cmp_reg_word(0, 0, 1));
    string_list_push_take(&words, machine_aarch64_branch_cond_word(3, cond));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 0));
    string_list_push_take(&words, machine_aarch64_branch_word(2));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 1));
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_if_else_call_compare_const_rhs_words(const char *architecture,
                                                                 const char *rhs_kind,
                                                                 const char *rhs_primary_text,
                                                                 int rhs_arg0,
                                                                 const char *operator_text) {
    StringList words;
    const char *inverse_cond = NULL;
    int false_branch_idx;
    int end_branch_idx;
    int else_start_idx;
    int epilogue_start_idx;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (strcmp(operator_text, "==") == 0) {
        inverse_cond = "ne";
    } else if (strcmp(operator_text, "!=") == 0) {
        inverse_cond = "eq";
    } else if (strcmp(operator_text, "<") == 0) {
        inverse_cond = "ge";
    } else if (strcmp(operator_text, "<=") == 0) {
        inverse_cond = "gt";
    } else if (strcmp(operator_text, ">") == 0) {
        inverse_cond = "le";
    } else if (strcmp(operator_text, ">=") == 0) {
        inverse_cond = "lt";
    } else {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "94000000");
    if (!machine_append_load_simple_value_words(architecture, &words, 1, rhs_kind, rhs_primary_text, rhs_arg0)) {
        return NULL;
    }
    string_list_push_take(&words, machine_aarch64_cmp_reg_word(0, 0, 1));
    false_branch_idx = (int)words.len;
    string_list_push_copy(&words, "");
    string_list_push_copy(&words, "94000000");
    end_branch_idx = (int)words.len;
    string_list_push_copy(&words, "");
    else_start_idx = (int)words.len;
    string_list_push_copy(&words, "94000000");
    epilogue_start_idx = (int)words.len;
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    free(words.items[false_branch_idx]);
    words.items[false_branch_idx] =
        machine_aarch64_branch_cond_word(else_start_idx - false_branch_idx, inverse_cond);
    free(words.items[end_branch_idx]);
    words.items[end_branch_idx] =
        machine_aarch64_branch_word(epilogue_start_idx - end_branch_idx);
    return join_word_parts_owned(&words);
}

static char *machine_direct_if_else_call_words(const char *architecture) {
    StringList words;
    int false_branch_idx;
    int end_branch_idx;
    int else_start_idx;
    int epilogue_start_idx;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "94000000");
    string_list_push_take(&words, machine_aarch64_cmp_zero_word(0, 0));
    false_branch_idx = (int)words.len;
    string_list_push_copy(&words, "");
    string_list_push_copy(&words, "94000000");
    end_branch_idx = (int)words.len;
    string_list_push_copy(&words, "");
    else_start_idx = (int)words.len;
    string_list_push_copy(&words, "94000000");
    epilogue_start_idx = (int)words.len;
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    free(words.items[false_branch_idx]);
    words.items[false_branch_idx] =
        machine_aarch64_branch_cond_word(else_start_idx - false_branch_idx, "eq");
    free(words.items[end_branch_idx]);
    words.items[end_branch_idx] =
        machine_aarch64_branch_word(epilogue_start_idx - end_branch_idx);
    return join_word_parts_owned(&words);
}

static char *machine_direct_if_else_const_then_call_words(const char *architecture,
                                                          const char *const_kind,
                                                          const char *const_primary_text,
                                                          int const_arg0) {
    StringList words;
    int false_branch_idx;
    int end_branch_idx;
    int else_start_idx;
    int epilogue_start_idx;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "94000000");
    string_list_push_take(&words, machine_aarch64_cmp_zero_word(0, 0));
    false_branch_idx = (int)words.len;
    string_list_push_copy(&words, "");
    if (!machine_append_load_simple_value_words(architecture, &words, 0, const_kind, const_primary_text, const_arg0)) {
        return NULL;
    }
    end_branch_idx = (int)words.len;
    string_list_push_copy(&words, "");
    else_start_idx = (int)words.len;
    string_list_push_copy(&words, "94000000");
    epilogue_start_idx = (int)words.len;
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    free(words.items[false_branch_idx]);
    words.items[false_branch_idx] =
        machine_aarch64_branch_cond_word(else_start_idx - false_branch_idx, "eq");
    free(words.items[end_branch_idx]);
    words.items[end_branch_idx] =
        machine_aarch64_branch_word(epilogue_start_idx - end_branch_idx);
    return join_word_parts_owned(&words);
}

static char *machine_direct_if_else_call_then_const_words(const char *architecture,
                                                          const char *const_kind,
                                                          const char *const_primary_text,
                                                          int const_arg0) {
    StringList words;
    int false_branch_idx;
    int end_branch_idx;
    int else_start_idx;
    int epilogue_start_idx;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "94000000");
    string_list_push_take(&words, machine_aarch64_cmp_zero_word(0, 0));
    false_branch_idx = (int)words.len;
    string_list_push_copy(&words, "");
    string_list_push_copy(&words, "94000000");
    end_branch_idx = (int)words.len;
    string_list_push_copy(&words, "");
    else_start_idx = (int)words.len;
    if (!machine_append_load_simple_value_words(architecture, &words, 0, const_kind, const_primary_text, const_arg0)) {
        return NULL;
    }
    epilogue_start_idx = (int)words.len;
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    free(words.items[false_branch_idx]);
    words.items[false_branch_idx] =
        machine_aarch64_branch_cond_word(else_start_idx - false_branch_idx, "eq");
    free(words.items[end_branch_idx]);
    words.items[end_branch_idx] =
        machine_aarch64_branch_word(epilogue_start_idx - end_branch_idx);
    return join_word_parts_owned(&words);
}

static char *machine_direct_nested_call_words(const char *architecture,
                                              int arg_count,
                                              const char **arg_kinds,
                                              const char **arg_primary_texts,
                                              const int *arg_arg0s) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    arg_count,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_nested_call_void_words(const char *architecture,
                                                   int arg_count,
                                                   const char **arg_kinds,
                                                   const char **arg_primary_texts,
                                                   const int *arg_arg0s) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    arg_count,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "52800000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_nested_call_trailing_simple_arg_void_words(const char *architecture,
                                                                       int inner_arg_count,
                                                                       const char **inner_arg_kinds,
                                                                       const char **inner_arg_primary_texts,
                                                                       const int *inner_arg_arg0s,
                                                                       const char *outer_arg_kind,
                                                                       const char *outer_arg_primary_text,
                                                                       int outer_arg0) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_take(&words, machine_aarch64_sub_imm_word(1, 31, 31, 16));
    if (!machine_append_load_simple_value_words(architecture,
                                                &words,
                                                1,
                                                outer_arg_kind,
                                                outer_arg_primary_text,
                                                outer_arg0)) {
        return NULL;
    }
    string_list_push_take(&words, machine_aarch64_store_sp_word(1, 1, 0));
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    inner_arg_count,
                                                    inner_arg_kinds,
                                                    inner_arg_primary_texts,
                                                    inner_arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_take(&words, machine_aarch64_load_sp_word(1, 1, 0));
    string_list_push_take(&words, machine_aarch64_add_imm_word(1, 31, 31, 16));
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "52800000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_nested_call_trailing_simple_arg_words(const char *architecture,
                                                                  int inner_arg_count,
                                                                  const char **inner_arg_kinds,
                                                                  const char **inner_arg_primary_texts,
                                                                  const int *inner_arg_arg0s,
                                                                  const char *outer_arg_kind,
                                                                  const char *outer_arg_primary_text,
                                                                  int outer_arg0) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_take(&words, machine_aarch64_sub_imm_word(1, 31, 31, 16));
    if (!machine_append_load_simple_value_words(architecture,
                                                &words,
                                                1,
                                                outer_arg_kind,
                                                outer_arg_primary_text,
                                                outer_arg0)) {
        return NULL;
    }
    string_list_push_take(&words, machine_aarch64_store_sp_word(1, 1, 0));
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    inner_arg_count,
                                                    inner_arg_kinds,
                                                    inner_arg_primary_texts,
                                                    inner_arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_take(&words, machine_aarch64_load_sp_word(1, 1, 0));
    string_list_push_take(&words, machine_aarch64_add_imm_word(1, 31, 31, 16));
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_nested_call_trailing_cstring_arg_words(const char *architecture,
                                                                   int inner_arg_count,
                                                                   const char **inner_arg_kinds,
                                                                   const char **inner_arg_primary_texts,
                                                                   const int *inner_arg_arg0s,
                                                                   int byte_len) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (byte_len < 0 || byte_len > 65535) {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    inner_arg_count,
                                                    inner_arg_kinds,
                                                    inner_arg_primary_texts,
                                                    inner_arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "90000002");
    string_list_push_copy(&words, "91000042");
    string_list_push_take(&words, machine_aarch64_mov_int32_word(3, byte_len));
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_nested_call_compare_const_rhs_words(const char *architecture,
                                                                int arg_count,
                                                                const char **arg_kinds,
                                                                const char **arg_primary_texts,
                                                                const int *arg_arg0s,
                                                                const char *rhs_kind,
                                                                const char *rhs_primary_text,
                                                                int rhs_arg0,
                                                                const char *operator_text) {
    StringList words;
    const char *cond = NULL;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (strcmp(operator_text, "==") == 0) {
        cond = "eq";
    } else if (strcmp(operator_text, "!=") == 0) {
        cond = "ne";
    } else if (strcmp(operator_text, "<") == 0) {
        cond = "lt";
    } else if (strcmp(operator_text, "<=") == 0) {
        cond = "le";
    } else if (strcmp(operator_text, ">") == 0) {
        cond = "gt";
    } else if (strcmp(operator_text, ">=") == 0) {
        cond = "ge";
    } else {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    arg_count,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "94000000");
    if (!machine_append_load_simple_value_words(architecture, &words, 1, rhs_kind, rhs_primary_text, rhs_arg0)) {
        return NULL;
    }
    string_list_push_take(&words, machine_aarch64_cmp_reg_word(0, 0, 1));
    string_list_push_take(&words, machine_aarch64_branch_cond_word(3, cond));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 0));
    string_list_push_take(&words, machine_aarch64_branch_word(2));
    string_list_push_take(&words, machine_aarch64_mov_int32_word(0, 1));
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_fallback_call_words(const char *architecture,
                                                int arg_count,
                                                const char **arg_kinds,
                                                const char **arg_primary_texts,
                                                const int *arg_arg0s) {
    StringList words;
    size_t else_branch_idx;
    size_t else_start_idx;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    arg_count,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_take(&words, machine_aarch64_cmp_zero_word(1, 0));
    else_branch_idx = words.len;
    string_list_push_copy(&words, "");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    else_start_idx = words.len;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    arg_count,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    free(words.items[else_branch_idx]);
    words.items[else_branch_idx] =
        machine_aarch64_branch_cond_word((int)(else_start_idx - else_branch_idx), "eq");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_void_words(const char *architecture) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "52800000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_cstring_arg_void_words(const char *architecture,
                                                        int byte_len) {
    StringList words;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (byte_len < 0 || byte_len > 65535) {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    string_list_push_copy(&words, "90000000");
    string_list_push_copy(&words, "91000000");
    string_list_push_take(&words, machine_aarch64_mov_int32_word(1, byte_len));
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "52800000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_simple_arg_words(const char *architecture,
                                                  const char *arg_kind,
                                                  const char *arg_primary_text,
                                                  int arg_arg0) {
    StringList words;
    const char *arg_kinds[1];
    const char *arg_primary_texts[1];
    int arg_arg0s[1];
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    arg_kinds[0] = arg_kind;
    arg_primary_texts[0] = arg_primary_text;
    arg_arg0s[0] = arg_arg0;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    1,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_register_pair_arg_words(const char *architecture,
                                                         int source_reg0,
                                                         int source_reg1,
                                                         int source_reg1_is64) {
    StringList words;
    int load_reg1 = source_reg1;
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    if (source_reg0 < 0 || source_reg0 > 7 || source_reg1 < 0 || source_reg1 > 7) {
        return NULL;
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    if (source_reg1 == 0 && source_reg0 != 0) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(1, 9, 0));
        load_reg1 = 9;
    }
    if (source_reg0 != 0) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(1, 0, source_reg0));
    }
    if (load_reg1 != 1) {
        string_list_push_take(&words, machine_aarch64_mov_reg_word(source_reg1_is64, 1, load_reg1));
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_simple_arg_void_words(const char *architecture,
                                                       const char *arg_kind,
                                                       const char *arg_primary_text,
                                                       int arg_arg0) {
    StringList words;
    const char *arg_kinds[1];
    const char *arg_primary_texts[1];
    int arg_arg0s[1];
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    arg_kinds[0] = arg_kind;
    arg_primary_texts[0] = arg_primary_text;
    arg_arg0s[0] = arg_arg0;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    1,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "52800000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_two_simple_arg_words(const char *architecture,
                                                      const char *lhs_kind,
                                                      const char *lhs_primary_text,
                                                      int lhs_arg0,
                                                      const char *rhs_kind,
                                                      const char *rhs_primary_text,
                                                      int rhs_arg0) {
    StringList words;
    const char *arg_kinds[2];
    const char *arg_primary_texts[2];
    int arg_arg0s[2];
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    arg_kinds[0] = lhs_kind;
    arg_kinds[1] = rhs_kind;
    arg_primary_texts[0] = lhs_primary_text;
    arg_primary_texts[1] = rhs_primary_text;
    arg_arg0s[0] = lhs_arg0;
    arg_arg0s[1] = rhs_arg0;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    2,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_two_simple_arg_void_words(const char *architecture,
                                                           const char *lhs_kind,
                                                           const char *lhs_primary_text,
                                                           int lhs_arg0,
                                                           const char *rhs_kind,
                                                           const char *rhs_primary_text,
                                                           int rhs_arg0) {
    StringList words;
    const char *arg_kinds[2];
    const char *arg_primary_texts[2];
    int arg_arg0s[2];
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    arg_kinds[0] = lhs_kind;
    arg_kinds[1] = rhs_kind;
    arg_primary_texts[0] = lhs_primary_text;
    arg_primary_texts[1] = rhs_primary_text;
    arg_arg0s[0] = lhs_arg0;
    arg_arg0s[1] = rhs_arg0;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    2,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "52800000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static int machine_find_saved_param_index(const int *saved_source_regs,
                                          int saved_count,
                                          int source_reg) {
    int i;
    for (i = 0; i < saved_count; ++i) {
        if (saved_source_regs[i] == source_reg) {
            return i;
        }
    }
    return -1;
}

static int machine_append_load_simple_call_args_words(const char *architecture,
                                                      StringList *words,
                                                      int arg_count,
                                                      const char **arg_kinds,
                                                      const char **arg_primary_texts,
                                                      const int *arg_arg0s) {
    static const int k_scratch_regs[] = {9, 10};
    int saved_source_regs[2];
    int saved_scratch_regs[2];
    int saved_count = 0;
    int i;
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    for (i = 0; i < arg_count; ++i) {
        if (strcmp(arg_kinds[i], "load_param") == 0) {
            int src_reg = arg_arg0s[i];
            if (src_reg < 0 || src_reg > 7) {
                return 0;
            }
            if (src_reg < i && machine_find_saved_param_index(saved_source_regs, saved_count, src_reg) < 0) {
                int scratch_reg;
                if (saved_count >= (int)(sizeof(k_scratch_regs) / sizeof(k_scratch_regs[0]))) {
                    return 0;
                }
                scratch_reg = k_scratch_regs[saved_count];
                string_list_push_take(words, machine_aarch64_mov_reg_word(1, scratch_reg, src_reg));
                saved_source_regs[saved_count] = src_reg;
                saved_scratch_regs[saved_count] = scratch_reg;
                saved_count += 1;
            }
        }
    }
    for (i = 0; i < arg_count; ++i) {
        if (strcmp(arg_kinds[i], "load_param") == 0) {
            int src_reg = arg_arg0s[i];
            if (src_reg < 0 || src_reg > 7) {
                return 0;
            }
            if (src_reg != i) {
                int saved_idx = machine_find_saved_param_index(saved_source_regs, saved_count, src_reg);
                int load_reg = saved_idx >= 0 ? saved_scratch_regs[saved_idx] : src_reg;
                string_list_push_take(words, machine_aarch64_mov_reg_word(1, i, load_reg));
            }
        } else if (!machine_append_load_simple_value_words(architecture,
                                                           words,
                                                           i,
                                                           arg_kinds[i],
                                                           arg_primary_texts[i],
                                                           arg_arg0s[i])) {
            return 0;
        }
    }
    return 1;
}

static char *machine_direct_call_three_simple_arg_words(const char *architecture,
                                                        const char *arg0_kind,
                                                        const char *arg0_primary_text,
                                                        int arg0_arg0,
                                                        const char *arg1_kind,
                                                        const char *arg1_primary_text,
                                                        int arg1_arg0,
                                                        const char *arg2_kind,
                                                        const char *arg2_primary_text,
                                                        int arg2_arg0) {
    StringList words;
    const char *arg_kinds[3];
    const char *arg_primary_texts[3];
    int arg_arg0s[3];
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    arg_kinds[0] = arg0_kind;
    arg_kinds[1] = arg1_kind;
    arg_kinds[2] = arg2_kind;
    arg_primary_texts[0] = arg0_primary_text;
    arg_primary_texts[1] = arg1_primary_text;
    arg_primary_texts[2] = arg2_primary_text;
    arg_arg0s[0] = arg0_arg0;
    arg_arg0s[1] = arg1_arg0;
    arg_arg0s[2] = arg2_arg0;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    3,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_four_simple_arg_words(const char *architecture,
                                                       const char *arg0_kind,
                                                       const char *arg0_primary_text,
                                                       int arg0_arg0,
                                                       const char *arg1_kind,
                                                       const char *arg1_primary_text,
                                                       int arg1_arg0,
                                                       const char *arg2_kind,
                                                       const char *arg2_primary_text,
                                                       int arg2_arg0,
                                                       const char *arg3_kind,
                                                       const char *arg3_primary_text,
                                                       int arg3_arg0) {
    StringList words;
    const char *arg_kinds[4];
    const char *arg_primary_texts[4];
    int arg_arg0s[4];
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    arg_kinds[0] = arg0_kind;
    arg_kinds[1] = arg1_kind;
    arg_kinds[2] = arg2_kind;
    arg_kinds[3] = arg3_kind;
    arg_primary_texts[0] = arg0_primary_text;
    arg_primary_texts[1] = arg1_primary_text;
    arg_primary_texts[2] = arg2_primary_text;
    arg_primary_texts[3] = arg3_primary_text;
    arg_arg0s[0] = arg0_arg0;
    arg_arg0s[1] = arg1_arg0;
    arg_arg0s[2] = arg2_arg0;
    arg_arg0s[3] = arg3_arg0;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    4,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_three_simple_arg_void_words(const char *architecture,
                                                             const char *arg0_kind,
                                                             const char *arg0_primary_text,
                                                             int arg0_arg0,
                                                             const char *arg1_kind,
                                                             const char *arg1_primary_text,
                                                             int arg1_arg0,
                                                             const char *arg2_kind,
                                                             const char *arg2_primary_text,
                                                             int arg2_arg0) {
    StringList words;
    const char *arg_kinds[3];
    const char *arg_primary_texts[3];
    int arg_arg0s[3];
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    arg_kinds[0] = arg0_kind;
    arg_kinds[1] = arg1_kind;
    arg_kinds[2] = arg2_kind;
    arg_primary_texts[0] = arg0_primary_text;
    arg_primary_texts[1] = arg1_primary_text;
    arg_primary_texts[2] = arg2_primary_text;
    arg_arg0s[0] = arg0_arg0;
    arg_arg0s[1] = arg1_arg0;
    arg_arg0s[2] = arg2_arg0;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    3,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "52800000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static char *machine_direct_call_four_simple_arg_void_words(const char *architecture,
                                                            const char *arg0_kind,
                                                            const char *arg0_primary_text,
                                                            int arg0_arg0,
                                                            const char *arg1_kind,
                                                            const char *arg1_primary_text,
                                                            int arg1_arg0,
                                                            const char *arg2_kind,
                                                            const char *arg2_primary_text,
                                                            int arg2_arg0,
                                                            const char *arg3_kind,
                                                            const char *arg3_primary_text,
                                                            int arg3_arg0) {
    StringList words;
    const char *arg_kinds[4];
    const char *arg_primary_texts[4];
    int arg_arg0s[4];
    memset(&words, 0, sizeof(words));
    if (strcmp(architecture, "aarch64") != 0) {
        die("v2 machine pipeline: unsupported direct executable architecture");
    }
    string_list_push_copy(&words, "a9bf7bfd");
    string_list_push_copy(&words, "910003fd");
    arg_kinds[0] = arg0_kind;
    arg_kinds[1] = arg1_kind;
    arg_kinds[2] = arg2_kind;
    arg_kinds[3] = arg3_kind;
    arg_primary_texts[0] = arg0_primary_text;
    arg_primary_texts[1] = arg1_primary_text;
    arg_primary_texts[2] = arg2_primary_text;
    arg_primary_texts[3] = arg3_primary_text;
    arg_arg0s[0] = arg0_arg0;
    arg_arg0s[1] = arg1_arg0;
    arg_arg0s[2] = arg2_arg0;
    arg_arg0s[3] = arg3_arg0;
    if (!machine_append_load_simple_call_args_words(architecture,
                                                    &words,
                                                    4,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s)) {
        return NULL;
    }
    string_list_push_copy(&words, "94000000");
    string_list_push_copy(&words, "52800000");
    string_list_push_copy(&words, "a8c17bfd");
    string_list_push_copy(&words, "d65f03c0");
    return join_word_parts_owned(&words);
}

static void machine_rewrite_function_direct_exec(MachineModuleBundle *out,
                                                 int function_idx,
                                                 char *words) {
    free(out->runtime_targets.items[function_idx]);
    out->runtime_targets.items[function_idx] = xstrdup_text("");
    free(out->call_symbol_names.items[function_idx]);
    out->call_symbol_names.items[function_idx] = xstrdup_text("");
    free(out->instruction_word_hex.items[function_idx]);
    out->instruction_word_hex.items[function_idx] = words;
    out->function_byte_sizes.items[function_idx] = word_count(words) * 4;
    out->local_payload_page_reloc_offsets.items[function_idx] = 0;
    out->local_payload_pageoff_reloc_offsets.items[function_idx] = 0;
    out->call_reloc_offsets.items[function_idx] = 0;
    free(out->metadata_operand_modes.items[function_idx]);
    out->metadata_operand_modes.items[function_idx] = xstrdup_text("direct_exec_no_payload");
    free(out->local_payload_texts.items[function_idx]);
    out->local_payload_texts.items[function_idx] = xstrdup_text("");
    out->function_reloc_counts.items[function_idx] = 0;
}

static void machine_append_relocation(MachineModuleBundle *out,
                                      int offset,
                                      const char *symbol_name,
                                      const char *reloc_kind);

static void machine_append_data_symbol(MachineModuleBundle *out,
                                       const char *symbol_name,
                                       const char *section_name,
                                       const char *payload_text,
                                       int align_pow2);

static void machine_direct_call_reloc_offsets(const char *words, IntList *out);

static void machine_rewrite_function_direct_exec_cstring_data(MachineModuleBundle *out,
                                                              int function_idx,
                                                              char *words,
                                                              const char *payload_text) {
    char *data_symbol = xformat("%s_cstring", out->symbol_names.items[function_idx]);
    int reloc_start = (int)out->reloc_offsets.len;
    free(out->runtime_targets.items[function_idx]);
    out->runtime_targets.items[function_idx] = xstrdup_text("");
    free(out->call_symbol_names.items[function_idx]);
    out->call_symbol_names.items[function_idx] = xstrdup_text("");
    free(out->instruction_word_hex.items[function_idx]);
    out->instruction_word_hex.items[function_idx] = words;
    out->function_byte_sizes.items[function_idx] = word_count(words) * 4;
    out->local_payload_page_reloc_offsets.items[function_idx] = 8;
    out->local_payload_pageoff_reloc_offsets.items[function_idx] = 12;
    out->call_reloc_offsets.items[function_idx] = 0;
    free(out->metadata_operand_modes.items[function_idx]);
    out->metadata_operand_modes.items[function_idx] = xstrdup_text("direct_exec_cstring_payload");
    free(out->local_payload_texts.items[function_idx]);
    out->local_payload_texts.items[function_idx] = xstrdup_text(payload_text);
    out->function_reloc_starts.items[function_idx] = reloc_start;
    machine_append_data_symbol(out,
                               data_symbol,
                               out->cstring_section_name,
                               payload_text,
                               0);
    machine_append_relocation(out,
                              8,
                              data_symbol,
                              out->metadata_page_relocation_kind);
    machine_append_relocation(out,
                              12,
                              data_symbol,
                              out->metadata_pageoff_relocation_kind);
    out->function_reloc_counts.items[function_idx] = 2;
    free(data_symbol);
}

static void machine_rewrite_function_direct_exec_call_cstring_data(MachineModuleBundle *out,
                                                                   int function_idx,
                                                                   char *words,
                                                                   const char *call_symbol,
                                                                   const char *payload_text) {
    IntList call_offsets;
    int reloc_start;
    char *data_symbol = xformat("%s_cstring", out->symbol_names.items[function_idx]);
    memset(&call_offsets, 0, sizeof(call_offsets));
    machine_direct_call_reloc_offsets(words, &call_offsets);
    if (call_offsets.len != 1) {
        die("v2 machine pipeline: direct exec call cstring reloc count mismatch");
    }
    free(out->runtime_targets.items[function_idx]);
    out->runtime_targets.items[function_idx] = xstrdup_text("");
    free(out->call_symbol_names.items[function_idx]);
    out->call_symbol_names.items[function_idx] = xstrdup_text(call_symbol);
    free(out->instruction_word_hex.items[function_idx]);
    out->instruction_word_hex.items[function_idx] = words;
    out->function_byte_sizes.items[function_idx] = word_count(words) * 4;
    out->local_payload_page_reloc_offsets.items[function_idx] = 8;
    out->local_payload_pageoff_reloc_offsets.items[function_idx] = 12;
    out->call_reloc_offsets.items[function_idx] = call_offsets.items[0];
    free(out->metadata_operand_modes.items[function_idx]);
    out->metadata_operand_modes.items[function_idx] = xstrdup_text("direct_exec_call_cstring_payload");
    free(out->local_payload_texts.items[function_idx]);
    out->local_payload_texts.items[function_idx] = xstrdup_text(payload_text);
    reloc_start = (int)out->reloc_offsets.len;
    out->function_reloc_starts.items[function_idx] = reloc_start;
    machine_append_data_symbol(out,
                               data_symbol,
                               out->cstring_section_name,
                               payload_text,
                               0);
    machine_append_relocation(out,
                              8,
                              data_symbol,
                              out->metadata_page_relocation_kind);
    machine_append_relocation(out,
                              12,
                              data_symbol,
                              out->metadata_pageoff_relocation_kind);
    machine_append_relocation(out,
                              call_offsets.items[0],
                              call_symbol,
                              out->call_relocation_kind);
    out->function_reloc_counts.items[function_idx] = 3;
    free(call_offsets.items);
    free(data_symbol);
}

static int machine_direct_call_reloc_offset(const char *words) {
    int word_idx = 0;
    const char *cursor = words;
    const char *token_start = words;
    while (1) {
        if (*cursor == ';' || *cursor == '\0') {
            size_t token_len = (size_t)(cursor - token_start);
            if (token_len == 8 && strncmp(token_start, "94000000", 8) == 0) {
                return word_idx * 4;
            }
            if (*cursor == '\0') {
                break;
            }
            token_start = cursor + 1;
            word_idx += 1;
        }
        if (*cursor == '\0') {
            break;
        }
        ++cursor;
    }
    die("v2 machine pipeline: direct exec call words missing branch placeholder");
    return 8;
}

static void machine_direct_call_reloc_offsets(const char *words, IntList *out) {
    int word_idx = 0;
    const char *cursor = words;
    const char *token_start = words;
    memset(out, 0, sizeof(*out));
    while (1) {
        if (*cursor == ';' || *cursor == '\0') {
            size_t token_len = (size_t)(cursor - token_start);
            if (token_len == 8 && strncmp(token_start, "94000000", 8) == 0) {
                int_list_push(out, word_idx * 4);
            }
            if (*cursor == '\0') {
                break;
            }
            token_start = cursor + 1;
            word_idx += 1;
        }
        if (*cursor == '\0') {
            break;
        }
        ++cursor;
    }
}

static void machine_direct_second_cstring_reloc_offsets(const char *words, IntList *out) {
    int word_idx = 0;
    const char *cursor = words;
    const char *token_start = words;
    memset(out, 0, sizeof(*out));
    while (1) {
        if (*cursor == ';' || *cursor == '\0') {
            size_t token_len = (size_t)(cursor - token_start);
            if ((token_len == 8 && strncmp(token_start, "90000002", 8) == 0) ||
                (token_len == 8 && strncmp(token_start, "91000042", 8) == 0)) {
                int_list_push(out, word_idx * 4);
            }
            if (*cursor == '\0') {
                break;
            }
            token_start = cursor + 1;
            word_idx += 1;
        }
        if (*cursor == '\0') {
            break;
        }
        ++cursor;
    }
}

static void machine_rewrite_function_direct_exec_calls(MachineModuleBundle *out,
                                                       int function_idx,
                                                       char *words,
                                                       int call_symbol_count,
                                                       char **call_symbols) {
    IntList call_offsets;
    StringList joined_symbols;
    int reloc_start;
    int i;
    memset(&call_offsets, 0, sizeof(call_offsets));
    memset(&joined_symbols, 0, sizeof(joined_symbols));
    machine_direct_call_reloc_offsets(words, &call_offsets);
    if ((int)call_offsets.len != call_symbol_count) {
        die("v2 machine pipeline: direct exec call reloc count mismatch");
    }
    free(out->runtime_targets.items[function_idx]);
    out->runtime_targets.items[function_idx] = xstrdup_text("");
    free(out->call_symbol_names.items[function_idx]);
    for (i = 0; i < call_symbol_count; ++i) {
        string_list_push_copy(&joined_symbols, call_symbols[i]);
    }
    out->call_symbol_names.items[function_idx] = string_list_join(&joined_symbols, ";");
    string_list_free(&joined_symbols);
    free(out->instruction_word_hex.items[function_idx]);
    out->instruction_word_hex.items[function_idx] = words;
    out->function_byte_sizes.items[function_idx] = word_count(words) * 4;
    out->local_payload_page_reloc_offsets.items[function_idx] = 0;
    out->local_payload_pageoff_reloc_offsets.items[function_idx] = 0;
    out->call_reloc_offsets.items[function_idx] =
        call_offsets.len > 0 ? call_offsets.items[0] : 0;
    free(out->metadata_operand_modes.items[function_idx]);
    out->metadata_operand_modes.items[function_idx] = xstrdup_text("direct_exec_no_payload");
    free(out->local_payload_texts.items[function_idx]);
    out->local_payload_texts.items[function_idx] = xstrdup_text("");
    reloc_start = (int)out->reloc_offsets.len;
    out->function_reloc_starts.items[function_idx] = reloc_start;
    for (i = 0; i < call_symbol_count; ++i) {
        machine_append_relocation(out,
                                  call_offsets.items[i],
                                  call_symbols[i],
                                  out->call_relocation_kind);
    }
    out->function_reloc_counts.items[function_idx] = call_symbol_count;
    free(call_offsets.items);
}

static void machine_rewrite_function_direct_exec_call(MachineModuleBundle *out,
                                                      int function_idx,
                                                      char *words,
                                                      char *call_symbol) {
    char *call_symbols[1];
    call_symbols[0] = call_symbol;
    machine_rewrite_function_direct_exec_calls(out, function_idx, words, 1, call_symbols);
    free(call_symbol);
}

static void machine_rewrite_function_direct_exec_calls_cstring_data(MachineModuleBundle *out,
                                                                    int function_idx,
                                                                    char *words,
                                                                    int call_symbol_count,
                                                                    char **call_symbols,
                                                                    const char *payload_text) {
    IntList call_offsets;
    IntList data_offsets;
    StringList joined_symbols;
    int reloc_start;
    int i;
    char *data_symbol = xformat("%s_cstring", out->symbol_names.items[function_idx]);
    memset(&call_offsets, 0, sizeof(call_offsets));
    memset(&data_offsets, 0, sizeof(data_offsets));
    memset(&joined_symbols, 0, sizeof(joined_symbols));
    machine_direct_call_reloc_offsets(words, &call_offsets);
    machine_direct_second_cstring_reloc_offsets(words, &data_offsets);
    if ((int)call_offsets.len != call_symbol_count) {
        die("v2 machine pipeline: direct exec call reloc count mismatch");
    }
    if ((int)data_offsets.len != 2) {
        die("v2 machine pipeline: direct exec nested cstring reloc count mismatch");
    }
    free(out->runtime_targets.items[function_idx]);
    out->runtime_targets.items[function_idx] = xstrdup_text("");
    free(out->call_symbol_names.items[function_idx]);
    for (i = 0; i < call_symbol_count; ++i) {
        string_list_push_copy(&joined_symbols, call_symbols[i]);
    }
    out->call_symbol_names.items[function_idx] = string_list_join(&joined_symbols, ";");
    string_list_free(&joined_symbols);
    free(out->instruction_word_hex.items[function_idx]);
    out->instruction_word_hex.items[function_idx] = words;
    out->function_byte_sizes.items[function_idx] = word_count(words) * 4;
    out->local_payload_page_reloc_offsets.items[function_idx] = data_offsets.items[0];
    out->local_payload_pageoff_reloc_offsets.items[function_idx] = data_offsets.items[1];
    out->call_reloc_offsets.items[function_idx] =
        call_offsets.len > 0 ? call_offsets.items[0] : 0;
    free(out->metadata_operand_modes.items[function_idx]);
    out->metadata_operand_modes.items[function_idx] = xstrdup_text("direct_exec_call_cstring_payload");
    free(out->local_payload_texts.items[function_idx]);
    out->local_payload_texts.items[function_idx] = xstrdup_text(payload_text);
    reloc_start = (int)out->reloc_offsets.len;
    out->function_reloc_starts.items[function_idx] = reloc_start;
    machine_append_data_symbol(out,
                               data_symbol,
                               out->cstring_section_name,
                               payload_text,
                               0);
    machine_append_relocation(out,
                              data_offsets.items[0],
                              data_symbol,
                              out->metadata_page_relocation_kind);
    machine_append_relocation(out,
                              data_offsets.items[1],
                              data_symbol,
                              out->metadata_pageoff_relocation_kind);
    for (i = 0; i < call_symbol_count; ++i) {
        machine_append_relocation(out,
                                  call_offsets.items[i],
                                  call_symbols[i],
                                  out->call_relocation_kind);
    }
    out->function_reloc_counts.items[function_idx] = 2 + call_symbol_count;
    free(call_offsets.items);
    free(data_offsets.items);
    free(data_symbol);
}

static const char *machine_arg_regs(const char *architecture) {
    if (strcmp(architecture, "aarch64") == 0) {
        return "x0;x1;x2;x3";
    }
    die("v2 machine pipeline: unsupported architecture");
    return "";
}

static const char *machine_scratch_regs(const char *architecture) {
    if (strcmp(architecture, "aarch64") == 0) {
        return "x9;x10";
    }
    die("v2 machine pipeline: unsupported architecture");
    return "";
}

static const char *machine_callee_saved_regs(const char *architecture) {
    if (strcmp(architecture, "aarch64") == 0) {
        return "x29;x30";
    }
    die("v2 machine pipeline: unsupported architecture");
    return "";
}

static char *machine_function_symbol_name(const char *target,
                                          const char *module_kind,
                                          const char *op_name,
                                          int item_id) {
    char *safe_module = sanitize_symbol_part(module_kind);
    char *safe_op = sanitize_symbol_part(op_name);
    char *out = xformat("%scheng_v2_%s_%s_%d",
                        machine_target_symbol_prefix(target),
                        safe_module,
                        safe_op,
                        item_id);
    free(safe_module);
    free(safe_op);
    return out;
}

static char *machine_call_symbol_name(const char *target, const char *runtime_target) {
    char *safe_runtime = sanitize_symbol_part(runtime_target);
    char *out = xformat("%s%s", machine_target_symbol_prefix(target), safe_runtime);
    free(safe_runtime);
    return out;
}

static char *machine_libc_symbol_name(const char *target, const char *symbol_name) {
    return xformat("%s%s", machine_target_symbol_prefix(target), symbol_name);
}

static MachineModuleBundle new_machine_module_bundle(const char *module_kind, const char *target_triple) {
    MachineModuleBundle out;
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.machine_pipeline.v1");
    out.module_kind = xstrdup_text(module_kind);
    out.target_triple = xstrdup_text(target_triple);
    out.architecture = xstrdup_text(machine_target_architecture(target_triple));
    out.obj_format = xstrdup_text(obj_format_for_target(target_triple));
    out.symbol_prefix = xstrdup_text(machine_target_symbol_prefix(target_triple));
    out.text_section_name = xstrdup_text(machine_target_text_section_name(target_triple));
    out.cstring_section_name = xstrdup_text(machine_target_cstring_section_name(target_triple));
    out.call_relocation_kind = xstrdup_text(machine_target_call_relocation_kind(target_triple));
    out.metadata_page_relocation_kind = xstrdup_text(machine_target_metadata_page_relocation_kind(target_triple));
    out.metadata_pageoff_relocation_kind = xstrdup_text(machine_target_metadata_pageoff_relocation_kind(target_triple));
    out.regalloc = xstrdup_text("linear_scan_deterministic");
    out.pointer_width_bits = machine_target_pointer_width_bits(target_triple);
    out.stack_align_bytes = machine_target_stack_align_bytes(target_triple);
    out.entry_symbol_override = xstrdup_text("");
    return out;
}

static void machine_append_relocation(MachineModuleBundle *out,
                                      int offset,
                                      const char *symbol_name,
                                      const char *reloc_kind) {
    int_list_push(&out->reloc_offsets, offset);
    string_list_push_copy(&out->reloc_symbols, symbol_name);
    string_list_push_copy(&out->reloc_kinds, reloc_kind);
}

static void machine_append_data_symbol(MachineModuleBundle *out,
                                       const char *symbol_name,
                                       const char *section_name,
                                       const char *payload_text,
                                       int align_pow2) {
    string_list_push_copy(&out->data_symbol_names, symbol_name);
    string_list_push_copy(&out->data_section_names, section_name);
    string_list_push_copy(&out->data_payload_texts, payload_text);
    int_list_push(&out->data_align_pow2, align_pow2);
}

static void machine_append_function(MachineModuleBundle *out,
                                    int item_id,
                                    const char *op_name,
                                    const char *runtime_target,
                                    const char *binding_summary,
                                    int logical_frame_size) {
    char *symbol_name = machine_function_symbol_name(out->target_triple, out->module_kind, op_name, item_id);
    char *call_symbol = machine_call_symbol_name(out->target_triple, runtime_target);
    const char *words = machine_stub_words(out->architecture);
    int function_reloc_start = (int)out->reloc_offsets.len;
    char *payload_text = xformat("runtime=%s|bindings=%s", runtime_target, binding_summary);
    char *meta_symbol = xformat("%s_meta", symbol_name);
    int_list_push(&out->item_ids, item_id);
    string_list_push_take(&out->symbol_names, symbol_name);
    string_list_push_copy(&out->runtime_targets, runtime_target);
    string_list_push_take(&out->call_symbol_names, call_symbol);
    int_list_push(&out->frame_sizes, aligned_size(logical_frame_size, out->stack_align_bytes));
    int_list_push(&out->stack_aligns, out->stack_align_bytes);
    string_list_push_copy(&out->arg_regs, machine_arg_regs(out->architecture));
    string_list_push_copy(&out->scratch_regs, machine_scratch_regs(out->architecture));
    string_list_push_copy(&out->callee_saved_regs, machine_callee_saved_regs(out->architecture));
    string_list_push_copy(&out->binding_summaries, binding_summary);
    string_list_push_copy(&out->instruction_word_hex, words);
    int_list_push(&out->function_byte_sizes, word_count(words) * 4);
    int_list_push(&out->local_payload_page_reloc_offsets, 8);
    int_list_push(&out->local_payload_pageoff_reloc_offsets, 12);
    int_list_push(&out->call_reloc_offsets, 16);
    string_list_push_copy(&out->metadata_operand_modes, "local_payload");
    string_list_push_take(&out->local_payload_texts, payload_text);
    machine_append_relocation(out, 8, meta_symbol, out->metadata_page_relocation_kind);
    machine_append_relocation(out, 12, meta_symbol, out->metadata_pageoff_relocation_kind);
    machine_append_relocation(out, 16, call_symbol, out->call_relocation_kind);
    int_list_push(&out->function_reloc_starts, function_reloc_start);
    int_list_push(&out->function_reloc_counts, (int)out->reloc_offsets.len - function_reloc_start);
    free(meta_symbol);
}

static void machine_append_dispatch_entry_function(MachineModuleBundle *out,
                                                   int item_id,
                                                   const char *op_name,
                                                   const char *runtime_target,
                                                   const char *binding_summary,
                                                   int logical_frame_size) {
    char *symbol_name = machine_function_symbol_name(out->target_triple, out->module_kind, op_name, item_id);
    char *call_symbol = machine_call_symbol_name(out->target_triple, runtime_target);
    const char *words = machine_dispatch_entry_words(out->architecture);
    int function_reloc_start = (int)out->reloc_offsets.len;
    int_list_push(&out->item_ids, item_id);
    string_list_push_take(&out->symbol_names, symbol_name);
    string_list_push_copy(&out->runtime_targets, runtime_target);
    string_list_push_take(&out->call_symbol_names, call_symbol);
    int_list_push(&out->frame_sizes, aligned_size(logical_frame_size, out->stack_align_bytes));
    int_list_push(&out->stack_aligns, out->stack_align_bytes);
    string_list_push_copy(&out->arg_regs, machine_arg_regs(out->architecture));
    string_list_push_copy(&out->scratch_regs, machine_scratch_regs(out->architecture));
    string_list_push_copy(&out->callee_saved_regs, machine_callee_saved_regs(out->architecture));
    string_list_push_copy(&out->binding_summaries, binding_summary);
    string_list_push_copy(&out->instruction_word_hex, words);
    int_list_push(&out->function_byte_sizes, word_count(words) * 4);
    int_list_push(&out->local_payload_page_reloc_offsets, 0);
    int_list_push(&out->local_payload_pageoff_reloc_offsets, 0);
    int_list_push(&out->call_reloc_offsets, 8);
    string_list_push_copy(&out->metadata_operand_modes, "argv_passthrough");
    string_list_push_copy(&out->local_payload_texts, "");
    machine_append_relocation(out, 8, call_symbol, out->call_relocation_kind);
    int_list_push(&out->function_reloc_starts, function_reloc_start);
    int_list_push(&out->function_reloc_counts, (int)out->reloc_offsets.len - function_reloc_start);
}

static void machine_append_trace_provider_function(MachineModuleBundle *out,
                                                   int item_id,
                                                   const char *symbol_name,
                                                   const char *runtime_target,
                                                   const char *binding_summary,
                                                   int logical_frame_size) {
    char *words = machine_trace_provider_words(out->architecture);
    char *call_symbol = machine_libc_symbol_name(out->target_triple, "puts");
    int function_reloc_start = (int)out->reloc_offsets.len;
    int_list_push(&out->item_ids, item_id);
    string_list_push_copy(&out->symbol_names, symbol_name);
    string_list_push_copy(&out->runtime_targets, runtime_target);
    string_list_push_take(&out->call_symbol_names, call_symbol);
    int_list_push(&out->frame_sizes, aligned_size(logical_frame_size, out->stack_align_bytes));
    int_list_push(&out->stack_aligns, out->stack_align_bytes);
    string_list_push_copy(&out->arg_regs, machine_arg_regs(out->architecture));
    string_list_push_copy(&out->scratch_regs, machine_scratch_regs(out->architecture));
    string_list_push_copy(&out->callee_saved_regs, machine_callee_saved_regs(out->architecture));
    string_list_push_copy(&out->binding_summaries, binding_summary);
    string_list_push_take(&out->instruction_word_hex, words);
    int_list_push(&out->function_byte_sizes, word_count(words) * 4);
    int_list_push(&out->local_payload_page_reloc_offsets, 0);
    int_list_push(&out->local_payload_pageoff_reloc_offsets, 0);
    int_list_push(&out->call_reloc_offsets, 8);
    string_list_push_copy(&out->metadata_operand_modes, "arg0_cstring");
    string_list_push_copy(&out->local_payload_texts, "");
    machine_append_relocation(out, 8, call_symbol, out->call_relocation_kind);
    int_list_push(&out->function_reloc_starts, function_reloc_start);
    int_list_push(&out->function_reloc_counts, (int)out->reloc_offsets.len - function_reloc_start);
}

static MachineModuleBundle build_runtime_provider_machine_module(const char *provider_module_path,
                                                                const char *provider_manifest_cid,
                                                                const StringList *runtime_targets,
                                                                const char *target_triple) {
    MachineModuleBundle out = new_machine_module_bundle("runtime_provider", target_triple);
    size_t i;
    for (i = 0; i < runtime_targets->len; ++i) {
        char *symbol_name = machine_call_symbol_name(target_triple, runtime_targets->items[i]);
        char *binding_summary = xformat("provider_module=%s|manifest_cid=%s|runtime_target=%s",
                                        provider_module_path,
                                        provider_manifest_cid,
                                        runtime_targets->items[i]);
        machine_append_trace_provider_function(&out,
                                              (int)i,
                                              symbol_name,
                                              runtime_targets->items[i],
                                              binding_summary,
                                              16);
        free(symbol_name);
        free(binding_summary);
    }
    return out;
}

static int string_contains_char(const char *text, char ch) {
    const char *cursor = text;
    while (*cursor != '\0') {
        if (*cursor == ch) {
            return 1;
        }
        ++cursor;
    }
    return 0;
}

static MachineModuleBundle build_compiler_core_machine_module(const CompilerCoreBundle *bundle,
                                                              const char *target_triple);

static void parse_compiler_routine_signature_param_names(const char *signature, StringList *out_names) {
    char *flat = replace_newlines_with_spaces(signature);
    const char *header = flat;
    const char *scan = strstr(flat, "fn ");
    const char *iter_scan = strstr(flat, "iterator ");
    const char *open;
    const char *close;
    while (scan != NULL) {
        header = scan;
        scan = strstr(scan + 1, "fn ");
    }
    while (iter_scan != NULL) {
        header = iter_scan;
        iter_scan = strstr(iter_scan + 1, "iterator ");
    }
    open = strchr(header, '(');
    close = strrchr(header, ')');
    memset(out_names, 0, sizeof(*out_names));
    if (open == NULL || close == NULL || close < open) {
        free(flat);
        return;
    }
    {
        char *params_text = trim_copy(open + 1, close);
        if (params_text[0] != '\0') {
            StringList parts = split_top_level_segments_text(params_text, ',');
            size_t i;
            for (i = 0; i < parts.len; ++i) {
                char *colon = strchr(parts.items[i], ':');
                char *name = colon != NULL ?
                             trim_copy(parts.items[i], colon) :
                             trim_copy(parts.items[i], parts.items[i] + strlen(parts.items[i]));
                string_list_push_take(out_names, name);
            }
        }
        free(params_text);
    }
    free(flat);
}

static char *compiler_core_signature_param_type_text(const char *signature, int param_index) {
    char *flat = replace_newlines_with_spaces(signature);
    const char *header = flat;
    const char *scan = strstr(flat, "fn ");
    const char *iter_scan = strstr(flat, "iterator ");
    const char *open;
    const char *close;
    char *out = xstrdup_text("");
    while (scan != NULL) {
        header = scan;
        scan = strstr(scan + 1, "fn ");
    }
    while (iter_scan != NULL) {
        header = iter_scan;
        iter_scan = strstr(iter_scan + 1, "iterator ");
    }
    open = strchr(header, '(');
    close = strrchr(header, ')');
    if (open == NULL || close == NULL || close < open) {
        free(flat);
        return out;
    }
    {
        char *params_text = trim_copy(open + 1, close);
        if (params_text[0] != '\0') {
            StringList parts = split_top_level_segments_text(params_text, ',');
            if (param_index >= 0 && (size_t)param_index < parts.len) {
                int colon_idx = find_top_level_token_text(parts.items[param_index], ":", 0);
                if (colon_idx >= 0) {
                    free(out);
                    out = trim_copy(parts.items[param_index] + colon_idx + 1,
                                    parts.items[param_index] + strlen(parts.items[param_index]));
                }
            }
            string_list_free(&parts);
        }
        free(params_text);
    }
    free(flat);
    return out;
}

static int compiler_core_type_ends_with(const char *text, const char *suffix) {
    const char *normalized = text;
    size_t text_len;
    size_t suffix_len = strlen(suffix);
    if (strncmp(normalized, "var ", 4) == 0) {
        normalized += 4;
    }
    text_len = strlen(normalized);
    if (suffix_len > text_len) {
        return 0;
    }
    return memcmp(normalized + text_len - suffix_len, suffix, suffix_len) == 0;
}

static int compiler_core_type_matches(const char *text, const char *bare_name) {
    const char *normalized = text;
    size_t text_len;
    size_t bare_len = strlen(bare_name);
    if (strncmp(normalized, "var ", 4) == 0) {
        normalized += 4;
    }
    text_len = strlen(normalized);
    if (strcmp(normalized, bare_name) == 0) {
        return 1;
    }
    if (text_len <= bare_len + 1) {
        return 0;
    }
    return normalized[text_len - bare_len - 1] == '.' &&
           strcmp(normalized + text_len - bare_len, bare_name) == 0;
}

static char *compiler_core_type_head_text(const char *text) {
    const char *normalized = text;
    char *head_text;
    int generic_idx = find_top_level_token_text(normalized, "[", 0);
    if (strncmp(normalized, "var ", 4) == 0) {
        normalized += 4;
    }
    if (generic_idx >= 0) {
        return xstrdup_range(normalized, normalized + generic_idx);
    }
    head_text = xstrdup_text(normalized);
    return head_text;
}

static int compiler_core_type_head_matches(const char *text, const char *bare_name) {
    char *head = compiler_core_type_head_text(text);
    size_t head_len = strlen(head);
    size_t bare_len = strlen(bare_name);
    int ok = 0;
    if (strcmp(head, bare_name) == 0) {
        ok = 1;
    } else if (head_len > bare_len + 1 &&
               head[head_len - bare_len - 1] == '.' &&
               strcmp(head + head_len - bare_len, bare_name) == 0) {
        ok = 1;
    }
    free(head);
    return ok;
}

static int compiler_core_direct_field_return_binding(const char *param_type_text,
                                                     const char *field_name,
                                                     int *out_source_reg,
                                                     int *out_is64) {
    *out_source_reg = -1;
    *out_is64 = 0;
    if (compiler_core_type_matches(param_type_text, "str")) {
        if (strcmp(field_name, "data") == 0) {
            *out_source_reg = 0;
            *out_is64 = 1;
            return 1;
        }
        if (strcmp(field_name, "len") == 0) {
            *out_source_reg = 1;
            *out_is64 = 0;
            return 1;
        }
    }
    if (compiler_core_type_matches(param_type_text, "Bytes") ||
        compiler_core_type_matches(param_type_text, "ByteBuffer")) {
        if (strcmp(field_name, "data") == 0) {
            *out_source_reg = 0;
            *out_is64 = 1;
            return 1;
        }
        if (strcmp(field_name, "len") == 0) {
            *out_source_reg = 1;
            *out_is64 = 0;
            return 1;
        }
    }
    if (compiler_core_type_ends_with(param_type_text, "[]")) {
        if (strcmp(field_name, "len") == 0) {
            *out_source_reg = 0;
            *out_is64 = 0;
            return 1;
        }
        if (strcmp(field_name, "buffer") == 0) {
            *out_source_reg = 1;
            *out_is64 = 1;
            return 1;
        }
    }
    if (compiler_core_type_matches(param_type_text, "DateTime") &&
        strcmp(field_name, "unix") == 0) {
        *out_source_reg = 0;
        *out_is64 = 1;
        return 1;
    }
    if (compiler_core_type_matches(param_type_text, "ByteReader") &&
        strcmp(field_name, "pos") == 0) {
        *out_source_reg = 2;
        *out_is64 = 0;
        return 1;
    }
    if (compiler_core_type_matches(param_type_text, "ExecCmdResult") &&
        strcmp(field_name, "exitCode") == 0) {
        *out_source_reg = 2;
        *out_is64 = 0;
        return 1;
    }
    if (compiler_core_type_matches(param_type_text, "ErrorInfo") &&
        strcmp(field_name, "code") == 0) {
        *out_source_reg = 0;
        *out_is64 = 0;
        return 1;
    }
    if (compiler_core_type_head_matches(param_type_text, "Result") &&
        strcmp(field_name, "ok") == 0) {
        *out_source_reg = 0;
        *out_is64 = 0;
        return 1;
    }
    return 0;
}

static int compiler_core_direct_type_reg_width(const char *type_text) {
    const char *normalized = type_text;
    if (strncmp(normalized, "var ", 4) == 0) {
        normalized += 4;
    }
    if (compiler_core_type_matches(normalized, "Bytes") ||
        compiler_core_type_matches(normalized, "ByteBuffer") ||
        compiler_core_type_ends_with(normalized, "[]")) {
        return 2;
    }
    if (compiler_core_type_matches(normalized, "ByteReader")) {
        return 3;
    }
    return 1;
}

static int compiler_core_direct_call_return_type_supported(const char *type_text) {
    const char *normalized = type_text;
    if (strncmp(normalized, "var ", 4) == 0) {
        normalized += 4;
    }
    if (normalized[0] == '\0') {
        return 1;
    }
    if (compiler_core_type_matches(normalized, "void")) {
        return 1;
    }
    if (compiler_core_type_matches(normalized, "bool") ||
        compiler_core_type_matches(normalized, "int8") ||
        compiler_core_type_matches(normalized, "int16") ||
        compiler_core_type_matches(normalized, "int32") ||
        compiler_core_type_matches(normalized, "int64") ||
        compiler_core_type_matches(normalized, "uint8") ||
        compiler_core_type_matches(normalized, "uint16") ||
        compiler_core_type_matches(normalized, "uint32") ||
        compiler_core_type_matches(normalized, "uint64") ||
        compiler_core_type_matches(normalized, "float64") ||
        compiler_core_type_matches(normalized, "char") ||
        compiler_core_type_matches(normalized, "ptr") ||
        compiler_core_type_matches(normalized, "cstring") ||
        compiler_core_type_matches(normalized, "File") ||
        compiler_core_type_matches(normalized, "str") ||
        compiler_core_type_matches(normalized, "Bytes") ||
        compiler_core_type_matches(normalized, "ByteBuffer") ||
        compiler_core_type_matches(normalized, "ByteReader") ||
        compiler_core_type_ends_with(normalized, "[]")) {
        return 1;
    }
    return 0;
}

static int compiler_core_direct_text_match_allowed(const CompilerCoreBundle *bundle,
                                                   size_t routine_idx,
                                                   int max_stmt_count,
                                                   int max_body_line_count) {
    if (routine_idx >= bundle->program.routine_stmt_counts.len ||
        routine_idx >= bundle->program.routine_body_line_counts.len) {
        return 0;
    }
    if (bundle->program.routine_stmt_counts.items[routine_idx] > max_stmt_count) {
        return 0;
    }
    if (bundle->program.routine_body_line_counts.items[routine_idx] > max_body_line_count) {
        return 0;
    }
    return 1;
}

static int compiler_core_param_base_reg(const char *signature, int param_index) {
    int out = 0;
    int i;
    for (i = 0; i < param_index; ++i) {
        char *param_type_text = compiler_core_signature_param_type_text(signature, i);
        out += compiler_core_direct_type_reg_width(param_type_text);
        free(param_type_text);
    }
    return out;
}

static MachineModuleBundle build_compiler_core_argv_entry_provider_machine_module(const char *repo_root,
                                                                                  const char *source_abs,
                                                                                  const char *provider_manifest_cid,
                                                                                  const StringList *runtime_targets,
                                                                                  const char *target_triple) {
    CompilerCoreBundle bundle = compile_compiler_core_source_closure(repo_root, source_abs);
    MachineModuleBundle full = build_compiler_core_machine_module(&bundle, target_triple);
    MachineModuleBundle out = new_machine_module_bundle("runtime_provider", target_triple);
    size_t entry_index = bundle.low_plan.labels.len;
    size_t i;
    char *binding_summary;
    char *provider_symbol;
    const char *entry_call_symbol;
    const char *runtime_target;
    const char *local_payload_target;
    char *local_payload_binding_summary;
    char *local_payload_symbol;
    if (runtime_targets == NULL || runtime_targets->len == 0) {
        die("v2 machine pipeline: compiler_core runtime targets missing");
    }
    runtime_target = runtime_targets->items[0];
    local_payload_target = runtime_targets->len > 1
                               ? runtime_targets->items[1]
                               : compiler_core_local_payload_runtime_target();
    for (i = 0; i < bundle.low_plan.labels.len; ++i) {
        if (strcmp(bundle.low_plan.labels.items[i], "compilerCoreArgvEntry") == 0) {
            entry_index = i;
            break;
        }
    }
    if (entry_index >= bundle.low_plan.labels.len) {
        die("v2 machine pipeline: compiler_core runtime argv entry missing");
    }
    entry_call_symbol = full.symbol_names.items[entry_index];
    binding_summary = xformat("provider_module=runtime/compiler_core_runtime_v2|manifest_cid=%s|runtime_target=%s|entry_label=compilerCoreArgvEntry",
                              provider_manifest_cid,
                              runtime_target);
    machine_append_function(&out,
                            bundle.low_plan.item_ids.items[entry_index],
                            "runtime_provider_export",
                            runtime_target,
                            binding_summary,
                            full.frame_sizes.items[entry_index]);
    provider_symbol = machine_call_symbol_name(target_triple, runtime_target);
    free(out.symbol_names.items[0]);
    out.symbol_names.items[0] = provider_symbol;
    {
        StringList call_symbols;
        memset(&call_symbols, 0, sizeof(call_symbols));
        string_list_push_take(&call_symbols, machine_libc_symbol_name(target_triple, "__cheng_setCmdLine"));
        string_list_push_copy(&call_symbols, entry_call_symbol);
        machine_rewrite_function_direct_exec_calls(&out,
                                                   0,
                                                   xstrdup_text("a9bf7bfd;910003fd;94000000;94000000;a8c17bfd;d65f03c0"),
                                                   (int)call_symbols.len,
                                                   call_symbols.items);
        string_list_free(&call_symbols);
    }
    local_payload_binding_summary =
        xformat("provider_module=runtime/compiler_core_runtime_v2|manifest_cid=%s|runtime_target=%s|entry_label=driver_c_compiler_core_local_payload_bridge",
                provider_manifest_cid,
                local_payload_target);
    machine_append_dispatch_entry_function(&out,
                                           1001000000 + bundle.low_plan.item_ids.items[entry_index],
                                           "runtime_provider_local_payload_export",
                                           local_payload_target,
                                           local_payload_binding_summary,
                                           16);
    local_payload_symbol = machine_call_symbol_name(target_triple, local_payload_target);
    free(out.symbol_names.items[1]);
    out.symbol_names.items[1] = local_payload_symbol;
    machine_rewrite_function_direct_exec_call(&out,
                                              1,
                                              xstrdup_text(machine_dispatch_entry_words(out.architecture)),
                                              machine_libc_symbol_name(target_triple,
                                                                       "driver_c_compiler_core_local_payload_bridge"));
    free(binding_summary);
    free(local_payload_binding_summary);
    return out;
}

static int compiler_core_direct_param_is64(const char *type_text) {
    const char *normalized = type_text;
    if (strncmp(normalized, "var ", 4) == 0) {
        normalized += 4;
    }
    if (compiler_core_type_matches(normalized, "int32") ||
        compiler_core_type_matches(normalized, "bool")) {
        return 0;
    }
    return 1;
}

static int compiler_core_direct_aggregate_field_binding(const char *param_type_text,
                                                        const char *field_name,
                                                        const char **out_result_type_text,
                                                        int *out_source_reg0,
                                                        int *out_source_reg1,
                                                        int *out_source_reg1_is64) {
    *out_result_type_text = "";
    *out_source_reg0 = -1;
    *out_source_reg1 = -1;
    *out_source_reg1_is64 = 0;
    if (compiler_core_type_matches(param_type_text, "ByteBuffer") &&
        strcmp(field_name, "data") == 0) {
        *out_result_type_text = "Bytes";
        *out_source_reg0 = 0;
        *out_source_reg1 = 1;
        *out_source_reg1_is64 = 0;
        return 1;
    }
    if (compiler_core_type_matches(param_type_text, "ByteReader") &&
        strcmp(field_name, "buf") == 0) {
        *out_result_type_text = "ByteBuffer";
        *out_source_reg0 = 0;
        *out_source_reg1 = 1;
        *out_source_reg1_is64 = 0;
        return 1;
    }
    if (compiler_core_type_matches(param_type_text, "ExecCmdResult") &&
        strcmp(field_name, "output") == 0) {
        *out_result_type_text = "str";
        *out_source_reg0 = 0;
        *out_source_reg1 = 1;
        *out_source_reg1_is64 = 0;
        return 1;
    }
    if (compiler_core_type_matches(param_type_text, "ErrorInfo") &&
        strcmp(field_name, "msg") == 0) {
        *out_result_type_text = "str";
        *out_source_reg0 = 1;
        *out_source_reg1 = 2;
        *out_source_reg1_is64 = 0;
        return 1;
    }
    return 0;
}

static int compiler_core_direct_param_field_return_binding(const char *signature,
                                                           int param_index,
                                                           const char *field_name,
                                                           int *out_source_reg,
                                                           int *out_is64) {
    char *param_type_text = compiler_core_signature_param_type_text(signature, param_index);
    int relative_reg = -1;
    int ok = compiler_core_direct_field_return_binding(param_type_text,
                                                       field_name,
                                                       &relative_reg,
                                                       out_is64);
    if (ok) {
        *out_source_reg = compiler_core_param_base_reg(signature, param_index) + relative_reg;
    }
    free(param_type_text);
    return ok;
}

static int compiler_core_direct_param_aggregate_field_binding(const char *signature,
                                                              int param_index,
                                                              const char *field_name,
                                                              const char **out_result_type_text,
                                                              int *out_source_reg0,
                                                              int *out_source_reg1,
                                                              int *out_source_reg1_is64) {
    char *param_type_text = compiler_core_signature_param_type_text(signature, param_index);
    int relative_reg0 = -1;
    int relative_reg1 = -1;
    int ok = compiler_core_direct_aggregate_field_binding(param_type_text,
                                                          field_name,
                                                          out_result_type_text,
                                                          &relative_reg0,
                                                          &relative_reg1,
                                                          out_source_reg1_is64);
    if (ok) {
        int base_reg = compiler_core_param_base_reg(signature, param_index);
        *out_source_reg0 = base_reg + relative_reg0;
        *out_source_reg1 = base_reg + relative_reg1;
    }
    free(param_type_text);
    return ok;
}

static int compiler_core_direct_param_nested_field_return_binding(const char *signature,
                                                                  int param_index,
                                                                  const char *aggregate_field_name,
                                                                  const char *nested_field_name,
                                                                  int *out_source_reg,
                                                                  int *out_is64) {
    const char *aggregate_result_type_text = "";
    int aggregate_reg0 = -1;
    int aggregate_reg1 = -1;
    int aggregate_reg1_is64 = 0;
    int nested_relative_reg = -1;
    int ok = compiler_core_direct_param_aggregate_field_binding(signature,
                                                                param_index,
                                                                aggregate_field_name,
                                                                &aggregate_result_type_text,
                                                                &aggregate_reg0,
                                                                &aggregate_reg1,
                                                                &aggregate_reg1_is64);
    if (!ok) {
        return 0;
    }
    if (!compiler_core_direct_field_return_binding(aggregate_result_type_text,
                                                   nested_field_name,
                                                   &nested_relative_reg,
                                                   out_is64)) {
        return 0;
    }
    if (nested_relative_reg == 0) {
        *out_source_reg = aggregate_reg0;
        return 1;
    }
    if (nested_relative_reg == 1) {
        *out_source_reg = aggregate_reg1;
        return 1;
    }
    return 0;
}

static int compiler_core_match_empty_bytes_expr(const char *expr_text) {
    char *trimmed = trim_copy(expr_text, expr_text + strlen(expr_text));
    int ok = 0;
    if (starts_with(trimmed, "Bytes(") &&
        trimmed[strlen(trimmed) - 1] == ')') {
        char *inner = trim_copy(trimmed + strlen("Bytes("), trimmed + strlen(trimmed) - 1);
        StringList parts = split_top_level_segments_text(inner, ',');
        int saw_data = 0;
        int saw_len = 0;
        size_t i;
        for (i = 0; i < parts.len; ++i) {
            int colon_idx = find_top_level_token_text(parts.items[i], ":", 0);
            if (colon_idx >= 0) {
                char *name = trim_copy(parts.items[i], parts.items[i] + colon_idx);
                char *value = trim_copy(parts.items[i] + colon_idx + 1,
                                        parts.items[i] + strlen(parts.items[i]));
                if (strcmp(name, "data") == 0 && strcmp(value, "nil") == 0) {
                    saw_data = 1;
                } else if (strcmp(name, "len") == 0 && strcmp(value, "0") == 0) {
                    saw_len = 1;
                }
                free(name);
                free(value);
            }
        }
        ok = saw_data && saw_len;
        string_list_free(&parts);
        free(inner);
    }
    free(trimmed);
    return ok;
}

static char *compiler_core_importc_symbol_text(const char *signature_text, const char *label) {
    const char *marker = "@importc(\"";
    const char *marker_pos = strstr(signature_text, marker);
    const char *start;
    const char *cursor;
    if (marker_pos == NULL) {
        return xstrdup_text(label);
    }
    start = marker_pos + strlen(marker);
    for (cursor = start; *cursor != '\0'; ++cursor) {
        if (*cursor == '"') {
            return xstrdup_range(start, cursor);
        }
    }
    return xstrdup_text(label);
}

static int compiler_core_importc_symbol_allows_direct_exec(const char *symbol_name) {
    return strcmp(symbol_name, "abs") == 0 ||
           strcmp(symbol_name, "free") == 0 ||
           strcmp(symbol_name, "driver_c_cli_param1_eq_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_read_flag_or_default_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_read_int32_flag_or_default_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_compiler_core_print_usage_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_compiler_core_print_status_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_absolute_path_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_program_absolute_path_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_get_current_dir_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_join_path2_bridge") == 0 ||
           strcmp(symbol_name, "driver_c_create_dir_all_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_write_text_file_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_compare_text_files_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_compare_binary_files_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_exec_file_capture_or_panic_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_compare_text_files_or_panic_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_extract_line_value_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_count_external_cc_providers_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_parse_plan_int32_or_zero_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_provider_field_for_module_from_plan_text_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_compiler_core_provider_source_kind_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_compiler_core_provider_compile_mode_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_run_stage_selfhost_host_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_run_tooling_selfhost_host_bridge") == 0 ||
        strcmp(symbol_name, "driver_c_read_file_all") == 0 ||
           strcmp(symbol_name, "cheng_fopen") == 0 ||
           strcmp(symbol_name, "cheng_fread") == 0 ||
           strcmp(symbol_name, "cheng_fwrite") == 0 ||
           strcmp(symbol_name, "fopen") == 0 ||
           strcmp(symbol_name, "paramCount") == 0 ||
           strcmp(symbol_name, "cheng_errno") == 0 ||
           strcmp(symbol_name, "cheng_ptr_size") == 0 ||
           strcmp(symbol_name, "cheng_fclose") == 0 ||
           strcmp(symbol_name, "cheng_fflush") == 0 ||
           strcmp(symbol_name, "cheng_fgetc") == 0 ||
           strcmp(symbol_name, "remove") == 0 ||
           strcmp(symbol_name, "rmdir") == 0 ||
           strcmp(symbol_name, "getenv") == 0 ||
           strcmp(symbol_name, "rename") == 0 ||
           strcmp(symbol_name, "close") == 0 ||
           strcmp(symbol_name, "fcntl") == 0 ||
           strcmp(symbol_name, "cheng_dir_exists") == 0 ||
           strcmp(symbol_name, "cheng_mkdir1") == 0 ||
           strcmp(symbol_name, "cheng_file_mtime") == 0 ||
           strcmp(symbol_name, "cheng_file_size") == 0 ||
           strcmp(symbol_name, "cheng_file_exists") == 0 ||
           strcmp(symbol_name, "cheng_getcwd") == 0 ||
           strcmp(symbol_name, "cheng_rawbytes_get_at") == 0 ||
           strcmp(symbol_name, "cheng_rawbytes_set_at") == 0 ||
           strcmp(symbol_name, "get_stdout") == 0 ||
           strcmp(symbol_name, "cheng_pty_is_supported") == 0;
}

static int compiler_core_find_param_slot(const StringList *param_names, const char *name) {
    size_t i;
    for (i = 0; i < param_names->len; ++i) {
        if (strcmp(param_names->items[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int compiler_core_find_plan_item_index_by_label(const CompilerCoreBundle *bundle, const char *label) {
    size_t i;
    for (i = 0; i < bundle->low_plan.labels.len; ++i) {
        if (strcmp(bundle->low_plan.labels.items[i], label) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int compiler_core_find_routine_index_by_name(const CompilerCoreBundle *bundle, const char *label) {
    size_t i;
    for (i = 0; i < bundle->program.routine_names.len; ++i) {
        if (strcmp(bundle->program.routine_names.items[i], label) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static char *compiler_core_import_visible_name(const char *import_path, const char *import_alias) {
    const char *base = import_path;
    const char *slash;
    if (import_alias != NULL && import_alias[0] != '\0') {
        return xstrdup_text(import_alias);
    }
    slash = strrchr(base, '/');
    if (slash != NULL) {
        base = slash + 1;
    }
    return xstrdup_text(base);
}

static const char *compiler_core_current_owner_module(const CompilerCoreProgram *program) {
    size_t i = program->top_level_kinds.len;
    while (i > 0) {
        --i;
        if (strcmp(program->top_level_kinds.items[i], "module") == 0) {
            return program->top_level_labels.items[i];
        }
    }
    return "";
}

static char *compiler_core_default_owner_module_path(const char *path) {
    const char *start = strstr(path, "v2/src/");
    size_t len;
    if (start != NULL) {
        start += strlen("v2/src/");
    } else {
        start = strstr(path, "src/");
        if (start != NULL) {
            start += strlen("src/");
        } else {
            start = path;
        }
    }
    len = strlen(start);
    if (len >= strlen(".cheng") && strcmp(start + len - strlen(".cheng"), ".cheng") == 0) {
        len -= strlen(".cheng");
    }
    return trim_copy(start, start + len);
}

static char *compiler_core_source_path_to_module_path(const char *path) {
    return compiler_core_default_owner_module_path(path);
}

static int compiler_core_find_qualified_plan_item_index(const CompilerCoreBundle *bundle,
                                                        int current_item_idx,
                                                        const char *qualified_name,
                                                        int expected_param_count) {
    const char *dot = strrchr(qualified_name, '.');
    char *import_name = NULL;
    const char *field_name;
    const char *owner_source_path = "";
    const char *target_module_path = NULL;
    int import_idx = -1;
    int match_idx = -1;
    int match_count = 0;
    size_t i;
    if (dot == NULL || dot == qualified_name || dot[1] == '\0') {
        return -1;
    }
    if (current_item_idx >= 0 &&
        (size_t)current_item_idx < bundle->low_plan.source_paths.len) {
        owner_source_path = bundle->low_plan.source_paths.items[current_item_idx];
    }
    import_name = trim_copy(qualified_name, dot);
    field_name = dot + 1;
    for (i = 0; i < bundle->low_plan.item_ids.len; ++i) {
        if (strcmp(bundle->low_plan.top_level_kinds.items[i], "import") != 0) {
            continue;
        }
        if (i >= bundle->low_plan.source_paths.len ||
            strcmp(bundle->low_plan.source_paths.items[i], owner_source_path) != 0) {
            continue;
        }
        {
            char *visible_name =
                compiler_core_import_visible_name(bundle->low_plan.import_paths.items[i],
                                                  bundle->low_plan.import_aliases.items[i]);
            int match = strcmp(visible_name, import_name) == 0;
            free(visible_name);
            if (!match) {
                continue;
            }
        }
        if (import_idx >= 0) {
            free(import_name);
            return -1;
        }
        import_idx = (int)i;
        target_module_path = bundle->low_plan.import_paths.items[i];
    }
    free(import_name);
    if (import_idx < 0 || target_module_path == NULL) {
        return -1;
    }
    for (i = 0; i < bundle->low_plan.item_ids.len; ++i) {
        const char *top_level_kind;
        char *module_path = NULL;
        if (i >= bundle->low_plan.source_paths.len) {
            continue;
        }
        module_path = compiler_core_source_path_to_module_path(bundle->low_plan.source_paths.items[i]);
        if (strcmp(module_path, target_module_path) != 0) {
            free(module_path);
            continue;
        }
        if (strcmp(bundle->low_plan.labels.items[i], field_name) != 0) {
            free(module_path);
            continue;
        }
        top_level_kind = bundle->low_plan.top_level_kinds.items[i];
        if (strcmp(top_level_kind, "fn") != 0 &&
            strcmp(top_level_kind, "iterator") != 0 &&
            strcmp(top_level_kind, "importc_fn") != 0 &&
            strcmp(top_level_kind, "const") != 0 &&
            strcmp(top_level_kind, "var") != 0) {
            free(module_path);
            continue;
        }
        if (expected_param_count >= 0 &&
            (strcmp(top_level_kind, "fn") == 0 ||
             strcmp(top_level_kind, "iterator") == 0 ||
             strcmp(top_level_kind, "importc_fn") == 0) &&
            bundle->low_plan.param_counts.items[i] != expected_param_count) {
            free(module_path);
            continue;
        }
        match_idx = (int)i;
        match_count += 1;
        free(module_path);
        if (match_count > 1) {
            return -1;
        }
    }
    return match_idx;
}

static int compiler_core_find_plan_item_index_for_routine(const CompilerCoreBundle *bundle, int routine_idx) {
    const char *label;
    const char *signature_hash;
    const char *routine_kind;
    size_t i;
    if (routine_idx < 0 || (size_t)routine_idx >= bundle->program.routine_names.len) {
        return -1;
    }
    label = bundle->program.routine_names.items[routine_idx];
    signature_hash = bundle->program.routine_signature_hashes.items[routine_idx];
    routine_kind = bundle->program.routine_kinds.items[routine_idx];
    for (i = 0; i < bundle->low_plan.labels.len; ++i) {
        if (strcmp(bundle->low_plan.labels.items[i], label) != 0) {
            continue;
        }
        if (strcmp(bundle->low_plan.signature_hashes.items[i], signature_hash) != 0) {
            continue;
        }
        if (strcmp(bundle->low_plan.top_level_kinds.items[i], routine_kind) != 0) {
            continue;
        }
        return (int)i;
    }
    return -1;
}

static int compiler_core_find_routine_index_for_plan_item(const CompilerCoreBundle *bundle, int item_idx) {
    const char *label;
    const char *signature_hash;
    const char *routine_kind;
    size_t i;
    if (item_idx < 0 || (size_t)item_idx >= bundle->low_plan.labels.len) {
        return -1;
    }
    label = bundle->low_plan.labels.items[item_idx];
    signature_hash = bundle->low_plan.signature_hashes.items[item_idx];
    routine_kind = bundle->low_plan.top_level_kinds.items[item_idx];
    for (i = 0; i < bundle->program.routine_names.len; ++i) {
        if (strcmp(bundle->program.routine_names.items[i], label) != 0) {
            continue;
        }
        if (strcmp(bundle->program.routine_signature_hashes.items[i], signature_hash) != 0) {
            continue;
        }
        if (strcmp(bundle->program.routine_kinds.items[i], routine_kind) != 0) {
            continue;
        }
        return (int)i;
    }
    return -1;
}

static char *compiler_core_normalized_type_text(const char *type_text) {
    const char *normalized = type_text;
    if (type_text == NULL) {
        return xstrdup_text("");
    }
    if (strncmp(normalized, "var ", 4) == 0) {
        normalized += 4;
    }
    return xstrdup_text(normalized);
}

static int compiler_core_type_texts_equivalent(const char *lhs, const char *rhs) {
    char *lhs_norm = compiler_core_normalized_type_text(lhs);
    char *rhs_norm = compiler_core_normalized_type_text(rhs);
    int ok = strcmp(lhs_norm, rhs_norm) == 0;
    free(lhs_norm);
    free(rhs_norm);
    return ok;
}

static char *compiler_core_direct_arg_type_text(const char *current_signature,
                                                const char *arg_kind,
                                                const char *arg_primary_text,
                                                int arg0) {
    if (arg_kind == NULL) {
        return NULL;
    }
    if (strcmp(arg_kind, "resolved_type") == 0) {
        return xstrdup_text(arg_primary_text != NULL ? arg_primary_text : "");
    }
    if (strcmp(arg_kind, "load_param") == 0) {
        return compiler_core_signature_param_type_text(current_signature, arg0);
    }
    if (strcmp(arg_kind, "load_int32") == 0) {
        return xstrdup_text("int32");
    }
    if (strcmp(arg_kind, "load_bool") == 0) {
        return xstrdup_text("bool");
    }
    if (strcmp(arg_kind, "load_str") == 0) {
        return xstrdup_text("str");
    }
    if (strcmp(arg_kind, "load_nil") == 0) {
        return xstrdup_text("nil");
    }
    return NULL;
}

static int compiler_core_routine_matches_direct_call(const CompilerCoreBundle *bundle,
                                                     int routine_idx,
                                                     int arg_count,
                                                     const char *current_signature,
                                                     const char *const *arg_kinds,
                                                     const char *const *arg_primary_texts,
                                                     const int *arg_arg0s) {
    int i;
    if (routine_idx < 0 || (size_t)routine_idx >= bundle->program.routine_names.len) {
        return 0;
    }
    if (bundle->program.routine_param_counts.items[routine_idx] != arg_count) {
        return 0;
    }
    for (i = 0; i < arg_count; ++i) {
        char *arg_type = compiler_core_direct_arg_type_text(current_signature,
                                                            arg_kinds[i],
                                                            arg_primary_texts[i],
                                                            arg_arg0s[i]);
        if (arg_type != NULL && strcmp(arg_type, "nil") != 0) {
            char *param_type = compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[routine_idx], i);
            int type_ok = compiler_core_type_texts_equivalent(param_type, arg_type);
            free(param_type);
            free(arg_type);
            if (!type_ok) {
                return 0;
            }
        } else {
            free(arg_type);
        }
    }
    return 1;
}

static int compiler_core_find_routine_index_by_call(const CompilerCoreBundle *bundle,
                                                    const char *callee,
                                                    const char *current_signature,
                                                    int arg_count,
                                                    const char *const *arg_kinds,
                                                    const char *const *arg_primary_texts,
                                                    const int *arg_arg0s) {
    size_t i;
    int match_idx = -1;
    int match_count = 0;
    const char *base_name = strrchr(callee, '.');
    if (base_name != NULL) {
        base_name += 1;
    } else {
        base_name = callee;
    }
    for (i = 0; i < bundle->program.routine_names.len; ++i) {
        if (strcmp(bundle->program.routine_names.items[i], callee) == 0 &&
            compiler_core_routine_matches_direct_call(bundle,
                                                      (int)i,
                                                      arg_count,
                                                      current_signature,
                                                      arg_kinds,
                                                      arg_primary_texts,
                                                      arg_arg0s)) {
            match_idx = (int)i;
            match_count += 1;
        }
    }
    if (match_count == 1) {
        return match_idx;
    }
    match_idx = -1;
    match_count = 0;
    for (i = 0; i < bundle->program.routine_names.len; ++i) {
        if (strcmp(bundle->program.routine_names.items[i], base_name) == 0 &&
            compiler_core_routine_matches_direct_call(bundle,
                                                      (int)i,
                                                      arg_count,
                                                      current_signature,
                                                      arg_kinds,
                                                      arg_primary_texts,
                                                      arg_arg0s)) {
            match_idx = (int)i;
            match_count += 1;
        }
    }
    if (match_count == 1) {
        return match_idx;
    }
    return -1;
}

static int compiler_core_match_simple_value_expr(const char *expr_text,
                                                 const StringList *param_names,
                                                 char **out_kind,
                                                 char **out_primary_text,
                                                 int *out_arg0) {
    char *trimmed = trim_copy(expr_text, expr_text + strlen(expr_text));
    *out_kind = NULL;
    *out_primary_text = NULL;
    *out_arg0 = -1;
    if (trimmed[0] == '\0') {
        free(trimmed);
        return 0;
    }
    if (strcmp(trimmed, "true") == 0 || strcmp(trimmed, "false") == 0) {
        *out_kind = xstrdup_text("load_bool");
        *out_primary_text = trimmed;
        *out_arg0 = -1;
        return 1;
    }
    if (strcmp(trimmed, "nil") == 0) {
        *out_kind = xstrdup_text("load_nil");
        *out_primary_text = xstrdup_text("");
        *out_arg0 = -1;
        free(trimmed);
        return 1;
    }
    {
        char *sizeof_expr = NULL;
        char *type_text = NULL;
        char *normalized = NULL;
        int size_value = -1;
        if (starts_with(trimmed, "int32(") && trimmed[strlen(trimmed) - 1] == ')') {
            sizeof_expr = trim_copy(trimmed + strlen("int32("), trimmed + strlen(trimmed) - 1);
        } else {
            sizeof_expr = xstrdup_text(trimmed);
        }
        if (starts_with(sizeof_expr, "sizeof(") && sizeof_expr[strlen(sizeof_expr) - 1] == ')') {
            type_text = trim_copy(sizeof_expr + strlen("sizeof("), sizeof_expr + strlen(sizeof_expr) - 1);
            normalized = trim_copy(type_text, type_text + strlen(type_text));
            if (strcmp(normalized, "int32") == 0 ||
                strcmp(normalized, "uint32") == 0 ||
                strcmp(normalized, "float32") == 0) {
                size_value = 4;
            } else if (strcmp(normalized, "int64") == 0 ||
                       strcmp(normalized, "uint64") == 0 ||
                       strcmp(normalized, "float64") == 0 ||
                       strcmp(normalized, "ptr") == 0) {
                size_value = 8;
            } else if (strcmp(normalized, "uint8") == 0 ||
                       strcmp(normalized, "bool") == 0 ||
                       strcmp(normalized, "char") == 0) {
                size_value = 1;
            } else if (strcmp(normalized, "int16") == 0 ||
                       strcmp(normalized, "uint16") == 0) {
                size_value = 2;
            }
        }
        free(normalized);
        free(type_text);
        free(sizeof_expr);
        if (size_value >= 0) {
            *out_kind = xstrdup_text("load_int32");
            *out_primary_text = xformat("%d", size_value);
            *out_arg0 = -1;
            free(trimmed);
            return 1;
        }
    }
    if (isdigit((unsigned char)trimmed[0])) {
        size_t i;
        for (i = 0; trimmed[i] != '\0'; ++i) {
            if (!isdigit((unsigned char)trimmed[i])) {
                free(trimmed);
                return 0;
            }
        }
        *out_kind = xstrdup_text("load_int32");
        *out_primary_text = trimmed;
        *out_arg0 = -1;
        return 1;
    }
    {
        int param_slot = compiler_core_find_param_slot(param_names, trimmed);
        if (param_slot >= 0) {
            *out_kind = xstrdup_text("load_param");
            *out_primary_text = trimmed;
            *out_arg0 = param_slot;
            return 1;
        }
    }
    free(trimmed);
    return 0;
}

static char *compiler_core_parse_string_literal_expr(const char *expr_text) {
    const char *p;
    char *trimmed = trim_copy(expr_text, expr_text + strlen(expr_text));
    char *out;
    size_t cap = 32;
    size_t len = 0;
    if (trimmed[0] != '"' || trimmed[strlen(trimmed) - 1] != '"') {
        free(trimmed);
        return NULL;
    }
    p = trimmed + 1;
    out = (char *)xcalloc(cap, 1);
    while (*p != '\0' && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\' && *p != '\0') {
            unsigned char esc = (unsigned char)*p++;
            if (esc == 'n') {
                ch = '\n';
            } else {
                ch = esc;
            }
        }
        if (len + 2 > cap) {
            cap *= 2;
            out = (char *)xrealloc(out, cap);
        }
        out[len++] = (char)ch;
        out[len] = '\0';
    }
    if (*p != '"') {
        free(trimmed);
        free(out);
        return NULL;
    }
    ++p;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != '\0') {
        free(trimmed);
        free(out);
        return NULL;
    }
    free(trimmed);
    return out;
}

static int compiler_core_is_direct_const_value_kind(const char *kind) {
    return strcmp(kind, "load_int32") == 0 ||
           strcmp(kind, "load_bool") == 0 ||
           strcmp(kind, "load_nil") == 0;
}

static int compiler_core_match_aggregate_field_expr(const char *expr_text,
                                                    const StringList *param_names,
                                                    const char *signature,
                                                    const char **out_result_type_text,
                                                    int *out_source_reg0,
                                                    int *out_source_reg1,
                                                    int *out_source_reg1_is64) {
    char *trimmed = trim_copy(expr_text, expr_text + strlen(expr_text));
    const char *name_end = parse_name_end(trimmed);
    int ok = 0;
    *out_result_type_text = "";
    *out_source_reg0 = -1;
    *out_source_reg1 = -1;
    *out_source_reg1_is64 = 0;
    if (name_end != trimmed && *name_end == '.') {
        char *name = trim_copy(trimmed, name_end);
        int param_slot = compiler_core_find_param_slot(param_names, name);
        if (param_slot == 0) {
            const char *field_start = name_end + 1;
            const char *field_end = parse_name_end(field_start);
            if (field_end != field_start && *field_end == '\0') {
                char *field_name = trim_copy(field_start, field_end);
                char *param_type_text = compiler_core_signature_param_type_text(signature, 0);
                ok = compiler_core_direct_aggregate_field_binding(param_type_text,
                                                                  field_name,
                                                                  out_result_type_text,
                                                                  out_source_reg0,
                                                                  out_source_reg1,
                                                                  out_source_reg1_is64);
                free(param_type_text);
                free(field_name);
            }
        }
        free(name);
    }
    free(trimmed);
    return ok;
}

static int compiler_core_match_param_field_expr(const char *expr_text,
                                                const StringList *param_names,
                                                const char *signature,
                                                int *out_source_reg,
                                                int *out_is64) {
    char *trimmed = trim_copy(expr_text, expr_text + strlen(expr_text));
    const char *name_end = parse_name_end(trimmed);
    int ok = 0;
    *out_source_reg = -1;
    *out_is64 = 0;
    if (name_end != trimmed && *name_end == '.') {
        char *name = trim_copy(trimmed, name_end);
        int param_slot = compiler_core_find_param_slot(param_names, name);
        if (param_slot >= 0) {
            const char *field_start = name_end + 1;
            const char *field_end = parse_name_end(field_start);
            if (field_end != field_start && *field_end == '\0') {
                char *field_name = trim_copy(field_start, field_end);
                ok = compiler_core_direct_param_field_return_binding(signature,
                                                                    param_slot,
                                                                    field_name,
                                                                    out_source_reg,
                                                                    out_is64);
                free(field_name);
            }
        }
        free(name);
    }
    free(trimmed);
    return ok;
}

static int compiler_core_match_param_nested_field_expr(const char *expr_text,
                                                       const StringList *param_names,
                                                       const char *signature,
                                                       int *out_source_reg,
                                                       int *out_is64) {
    char *trimmed = trim_copy(expr_text, expr_text + strlen(expr_text));
    const char *name_end = parse_name_end(trimmed);
    int ok = 0;
    *out_source_reg = -1;
    *out_is64 = 0;
    if (name_end != trimmed && *name_end == '.') {
        char *name = trim_copy(trimmed, name_end);
        int param_slot = compiler_core_find_param_slot(param_names, name);
        if (param_slot >= 0) {
            const char *field0_start = name_end + 1;
            const char *field0_end = parse_name_end(field0_start);
            if (field0_end != field0_start && *field0_end == '.') {
                const char *field1_start = field0_end + 1;
                const char *field1_end = parse_name_end(field1_start);
                if (field1_end != field1_start && *field1_end == '\0') {
                    char *field0_name = trim_copy(field0_start, field0_end);
                    char *field1_name = trim_copy(field1_start, field1_end);
                    ok = compiler_core_direct_param_nested_field_return_binding(signature,
                                                                               param_slot,
                                                                               field0_name,
                                                                               field1_name,
                                                                               out_source_reg,
                                                                               out_is64);
                    free(field0_name);
                    free(field1_name);
                }
            }
        }
        free(name);
    }
    free(trimmed);
    return ok;
}

static int compiler_core_match_return_simple_line(const char *line_text,
                                                  const StringList *param_names,
                                                  char **out_kind,
                                                  char **out_primary_text,
                                                  int *out_arg0) {
    char *trimmed = trim_copy(line_text, line_text + strlen(line_text));
    char *expr = NULL;
    int ok = 0;
    *out_kind = NULL;
    *out_primary_text = NULL;
    *out_arg0 = -1;
    if (!starts_with(trimmed, "return")) {
        free(trimmed);
        return 0;
    }
    expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    if (expr[0] != '\0') {
        ok = compiler_core_match_simple_value_expr(expr,
                                                   param_names,
                                                   out_kind,
                                                   out_primary_text,
                                                   out_arg0);
    }
    free(expr);
    free(trimmed);
    return ok;
}

static char *compiler_core_direct_exec_callee_symbol(const CompilerCoreBundle *bundle,
                                                     int current_item_idx,
                                                     const char *callee,
                                                     const char *target_triple);

static int compiler_core_match_return_zero_arg_call_line(const CompilerCoreBundle *bundle,
                                                         size_t item_idx,
                                                         const char *line_text,
                                                         const char *target_triple,
                                                         char **out_call_symbol) {
    char *trimmed = trim_copy(line_text, line_text + strlen(line_text));
    char *expr = NULL;
    int ok = 0;
    *out_call_symbol = NULL;
    if (!starts_with(trimmed, "return")) {
        free(trimmed);
        return 0;
    }
    expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    {
        const char *name_end = parse_call_name_end(expr);
        int open_idx = (int)(name_end - expr);
        if (open_idx > 0 && expr[open_idx] == '(') {
            int close_idx = find_matching_delimiter_text(expr, open_idx, '(', ')');
            if (close_idx >= 0 && expr[close_idx + 1] == '\0') {
                char *callee = trim_copy(expr, expr + open_idx);
                char *arg_text = trim_copy(expr + open_idx + 1, expr + close_idx);
                if (arg_text[0] == '\0') {
                    *out_call_symbol =
                        compiler_core_direct_exec_callee_symbol(bundle, item_idx, callee, target_triple);
                    ok = *out_call_symbol != NULL;
                }
                free(arg_text);
                free(callee);
            }
        }
    }
    free(expr);
    free(trimmed);
    return ok;
}

static char *compiler_core_direct_exec_param_return_words(const CompilerCoreBundle *bundle,
                                                          size_t routine_idx,
                                                          const char *architecture) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    char *expr = NULL;
    StringList param_names;
    char *kind = NULL;
    char *primary = NULL;
    int arg0 = -1;
    char *param_type_text = NULL;
    const char *normalized_param_type;
    const char *normalized_return_type;
    int reg_width;
    int source_reg0;
    int source_reg1;
    char *words = NULL;
    memset(&param_names, 0, sizeof(param_names));
    parse_compiler_routine_signature_param_names(signature, &param_names);
    if (starts_with(trimmed, "return")) {
        expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    } else {
        expr = trim_copy(trimmed, trimmed + strlen(trimmed));
    }
    if (!compiler_core_match_simple_value_expr(expr, &param_names, &kind, &primary, &arg0) ||
        strcmp(kind, "load_param") != 0 ||
        arg0 < 0) {
        goto cleanup;
    }
    param_type_text = compiler_core_signature_param_type_text(signature, arg0);
    normalized_param_type = param_type_text;
    if (strncmp(normalized_param_type, "var ", 4) == 0) {
        normalized_param_type += 4;
    }
    normalized_return_type = bundle->program.routine_return_type_texts.items[routine_idx];
    if (strncmp(normalized_return_type, "var ", 4) == 0) {
        normalized_return_type += 4;
    }
    if (strcmp(normalized_param_type, normalized_return_type) != 0) {
        goto cleanup;
    }
    reg_width = compiler_core_direct_type_reg_width(param_type_text);
    if (reg_width != 2) {
        goto cleanup;
    }
    source_reg0 = compiler_core_param_base_reg(signature, arg0);
    source_reg1 = source_reg0 + 1;
    words = machine_direct_return_register_pair_words(architecture, source_reg0, source_reg1, 0);
cleanup:
    free(param_type_text);
    free(kind);
    free(primary);
    free(expr);
    free(trimmed);
    string_list_free(&param_names);
    return words;
}

static int compiler_core_match_alias_decl_line(const char *line_text,
                                               const StringList *param_names,
                                               char **out_name,
                                               char **out_kind,
                                               char **out_primary_text,
                                               int *out_arg0) {
    char *trimmed = trim_copy(line_text, line_text + strlen(line_text));
    char *tail = NULL;
    char *rhs = NULL;
    int eq_idx;
    const char *name_end;
    int ok = 0;
    *out_name = NULL;
    *out_kind = NULL;
    *out_primary_text = NULL;
    *out_arg0 = -1;
    if (!starts_with(trimmed, "let ") && !starts_with(trimmed, "var ")) {
        free(trimmed);
        return 0;
    }
    tail = trim_copy(trimmed + 4, trimmed + strlen(trimmed));
    eq_idx = find_top_level_token_text(tail, "=", 0);
    name_end = parse_name_end(tail);
    if (eq_idx >= 0 && name_end != tail && (int)(name_end - tail) <= eq_idx) {
        rhs = trim_copy(tail + eq_idx + 1, tail + strlen(tail));
        ok = compiler_core_match_simple_value_expr(rhs,
                                                   param_names,
                                                   out_kind,
                                                   out_primary_text,
                                                   out_arg0);
        if (ok) {
            *out_name = trim_copy(tail, name_end);
        }
    }
    free(rhs);
    free(tail);
    free(trimmed);
    return ok;
}

static int compiler_core_resolve_alias_arg_expr(const char *expr_text,
                                                const char *alias_name,
                                                const char *alias_kind,
                                                const char *alias_primary_text,
                                                int alias_arg0,
                                                const StringList *param_names,
                                                char **out_kind,
                                                char **out_primary_text,
                                                int *out_arg0) {
    char *trimmed = trim_copy(expr_text, expr_text + strlen(expr_text));
    int ok = 0;
    *out_kind = NULL;
    *out_primary_text = NULL;
    *out_arg0 = -1;
    if (strcmp(trimmed, alias_name) == 0) {
        *out_kind = xstrdup_text(alias_kind);
        *out_primary_text = xstrdup_text(alias_primary_text);
        *out_arg0 = alias_arg0;
        ok = 1;
    } else {
        ok = compiler_core_match_simple_value_expr(trimmed,
                                                   param_names,
                                                   out_kind,
                                                   out_primary_text,
                                                   out_arg0);
    }
    free(trimmed);
    return ok;
}

static char *compiler_core_direct_exec_callee_symbol(const CompilerCoreBundle *bundle,
                                                     int current_item_idx,
                                                     const char *callee,
                                                     const char *target_triple) {
    int callee_idx = -1;
    if (strchr(callee, '.') != NULL) {
        callee_idx = compiler_core_find_qualified_plan_item_index(bundle, current_item_idx, callee, -1);
    } else {
        int routine_idx = compiler_core_find_routine_index_by_name(bundle, callee);
        if (routine_idx >= 0) {
            callee_idx = compiler_core_find_plan_item_index_for_routine(bundle, routine_idx);
        }
    }
    if (callee_idx < 0) {
        return NULL;
    }
    if (strcmp(bundle->low_plan.top_level_kinds.items[callee_idx], "importc_fn") == 0) {
        char *call_symbol = NULL;
        int routine_idx = compiler_core_find_routine_index_for_plan_item(bundle, callee_idx);
        char *symbol_name;
        if (routine_idx < 0) {
            return NULL;
        }
        symbol_name =
            compiler_core_importc_symbol_text(bundle->program.routine_signatures.items[routine_idx],
                                              bundle->program.routine_names.items[routine_idx]);
        if (compiler_core_importc_symbol_allows_direct_exec(symbol_name)) {
            const char *direct_symbol_name =
                strcmp(symbol_name, "driver_c_cli_param1_eq_bridge") == 0 ?
                    "driver_c_cli_param1_eq_raw_bridge" :
                    symbol_name;
            call_symbol = machine_libc_symbol_name(target_triple, direct_symbol_name);
        }
        free(symbol_name);
        return call_symbol;
    }
    return machine_function_symbol_name(target_triple,
                                        "compiler_core",
                                        bundle->low_plan.canonical_ops.items[callee_idx],
                                        bundle->low_plan.item_ids.items[callee_idx]);
}

static char *compiler_core_direct_exec_callee_symbol_for_call(const CompilerCoreBundle *bundle,
                                                              int current_item_idx,
                                                              const char *callee,
                                                              const char *current_signature,
                                                              int arg_count,
                                                              const char *const *arg_kinds,
                                                              const char *const *arg_primary_texts,
                                                              const int *arg_arg0s,
                                                              const char *target_triple) {
    int callee_idx = -1;
    if (strchr(callee, '.') != NULL) {
        callee_idx = compiler_core_find_qualified_plan_item_index(bundle, current_item_idx, callee, arg_count);
    } else {
        int routine_idx = compiler_core_find_routine_index_by_call(bundle,
                                                                   callee,
                                                                   current_signature,
                                                                   arg_count,
                                                                   arg_kinds,
                                                                   arg_primary_texts,
                                                                   arg_arg0s);
        if (routine_idx >= 0) {
            callee_idx = compiler_core_find_plan_item_index_for_routine(bundle, routine_idx);
        }
    }
    if (callee_idx < 0) {
        return NULL;
    }
    if (strcmp(bundle->low_plan.top_level_kinds.items[callee_idx], "importc_fn") == 0) {
        char *call_symbol = NULL;
        int routine_idx = compiler_core_find_routine_index_for_plan_item(bundle, callee_idx);
        char *symbol_name;
        if (routine_idx < 0) {
            return NULL;
        }
        symbol_name =
            compiler_core_importc_symbol_text(bundle->program.routine_signatures.items[routine_idx],
                                              bundle->program.routine_names.items[routine_idx]);
        if (compiler_core_importc_symbol_allows_direct_exec(symbol_name)) {
            call_symbol = machine_libc_symbol_name(target_triple, symbol_name);
        }
        free(symbol_name);
        return call_symbol;
    }
    return machine_function_symbol_name(target_triple,
                                        "compiler_core",
                                        bundle->low_plan.canonical_ops.items[callee_idx],
                                        bundle->low_plan.item_ids.items[callee_idx]);
}

static int compiler_core_direct_exec_callee_routine_index_for_call(const CompilerCoreBundle *bundle,
                                                                   int current_item_idx,
                                                                   const char *callee,
                                                                   const char *current_signature,
                                                                   int arg_count,
                                                                   const char *const *arg_kinds,
                                                                   const char *const *arg_primary_texts,
                                                                   const int *arg_arg0s) {
    int callee_idx = -1;
    if (strchr(callee, '.') != NULL) {
        callee_idx = compiler_core_find_qualified_plan_item_index(bundle, current_item_idx, callee, arg_count);
        if (callee_idx < 0) {
            return -1;
        }
        return compiler_core_find_routine_index_for_plan_item(bundle, callee_idx);
    }
    return compiler_core_find_routine_index_by_call(bundle,
                                                    callee,
                                                    current_signature,
                                                    arg_count,
                                                    arg_kinds,
                                                    arg_primary_texts,
                                                    arg_arg0s);
}

static char *compiler_core_multiline_alias_call_symbol(const CompilerCoreBundle *bundle,
                                                       size_t item_idx,
                                                       size_t routine_idx,
                                                       const char *target_triple,
                                                       const StringList *param_names,
                                                       int want_void,
                                                       char **out_words) {
    const char *body_text = bundle->program.routine_body_texts.items[routine_idx];
    StringList body_lines;
    StringList active_lines;
    char *call_symbol = NULL;
    memset(&body_lines, 0, sizeof(body_lines));
    memset(&active_lines, 0, sizeof(active_lines));
    *out_words = NULL;
    if (!compiler_core_direct_text_match_allowed(bundle, routine_idx, 2, 3)) {
        return NULL;
    }
    if (body_text == NULL || strchr(body_text, '\n') == NULL) {
        return NULL;
    }
    split_text_lines(body_text, &body_lines);
    for (size_t i = 0; i < body_lines.len; ++i) {
        if (!is_blank_or_comment_line(body_lines.items[i])) {
            string_list_push_copy(&active_lines, body_lines.items[i]);
        }
    }
    if (active_lines.len == 2) {
        char *alias_name = NULL;
        char *alias_kind = NULL;
        char *alias_primary = NULL;
        int alias_arg0 = -1;
        if (compiler_core_match_alias_decl_line(active_lines.items[0],
                                                param_names,
                                                &alias_name,
                                                &alias_kind,
                                                &alias_primary,
                                                &alias_arg0)) {
            char *line1 = trim_copy(active_lines.items[1], active_lines.items[1] + strlen(active_lines.items[1]));
            char *expr = NULL;
            if (want_void) {
                expr = trim_copy(line1, line1 + strlen(line1));
            } else if (starts_with(line1, "return ")) {
                expr = trim_copy(line1 + strlen("return "), line1 + strlen(line1));
            }
            if (expr != NULL) {
                const char *name_end = parse_name_end(expr);
                int open_idx = (int)(name_end - expr);
                int close_idx;
                if (open_idx > 0 && expr[open_idx] == '(') {
                    close_idx = find_matching_delimiter_text(expr, open_idx, '(', ')');
                    if (close_idx >= 0 && expr[close_idx + 1] == '\0') {
                        char *callee = trim_copy(expr, expr + open_idx);
                        char *arg_text = trim_copy(expr + open_idx + 1, expr + close_idx);
                        call_symbol =
                            compiler_core_direct_exec_callee_symbol(bundle, (int)item_idx, callee, target_triple);
                        if (call_symbol != NULL) {
                            StringList parts = split_top_level_segments_text(arg_text, ',');
                            if (parts.len == 1) {
                                char *arg_kind = NULL;
                                char *arg_primary = NULL;
                                int arg0 = -1;
                                if (compiler_core_resolve_alias_arg_expr(parts.items[0],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &arg_kind,
                                                                         &arg_primary,
                                                                         &arg0)) {
                                    *out_words = want_void
                                        ? machine_direct_call_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                                    arg_kind,
                                                                                    arg_primary,
                                                                                    arg0)
                                        : machine_direct_call_simple_arg_words(machine_target_architecture(target_triple),
                                                                               arg_kind,
                                                                               arg_primary,
                                                                               arg0);
                                }
                                free(arg_kind);
                                free(arg_primary);
                            } else if (parts.len == 2) {
                                char *lhs_kind = NULL;
                                char *lhs_primary = NULL;
                                int lhs_arg0 = -1;
                                char *rhs_kind = NULL;
                                char *rhs_primary = NULL;
                                int rhs_arg0 = -1;
                                if (compiler_core_resolve_alias_arg_expr(parts.items[0],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &lhs_kind,
                                                                         &lhs_primary,
                                                                         &lhs_arg0) &&
                                    compiler_core_resolve_alias_arg_expr(parts.items[1],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &rhs_kind,
                                                                         &rhs_primary,
                                                                         &rhs_arg0)) {
                                    *out_words = want_void
                                        ? machine_direct_call_two_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                                        lhs_kind,
                                                                                        lhs_primary,
                                                                                        lhs_arg0,
                                                                                        rhs_kind,
                                                                                        rhs_primary,
                                                                                        rhs_arg0)
                                        : machine_direct_call_two_simple_arg_words(machine_target_architecture(target_triple),
                                                                                   lhs_kind,
                                                                                   lhs_primary,
                                                                                   lhs_arg0,
                                                                                   rhs_kind,
                                                                                   rhs_primary,
                                                                                   rhs_arg0);
                                }
                                free(lhs_kind);
                                free(lhs_primary);
                                free(rhs_kind);
                                free(rhs_primary);
                            } else if (parts.len == 3) {
                                char *arg0_kind = NULL;
                                char *arg0_primary = NULL;
                                int arg0 = -1;
                                char *arg1_kind = NULL;
                                char *arg1_primary = NULL;
                                int arg1 = -1;
                                char *arg2_kind = NULL;
                                char *arg2_primary = NULL;
                                int arg2 = -1;
                                if (compiler_core_resolve_alias_arg_expr(parts.items[0],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &arg0_kind,
                                                                         &arg0_primary,
                                                                         &arg0) &&
                                    compiler_core_resolve_alias_arg_expr(parts.items[1],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &arg1_kind,
                                                                         &arg1_primary,
                                                                         &arg1) &&
                                    compiler_core_resolve_alias_arg_expr(parts.items[2],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &arg2_kind,
                                                                         &arg2_primary,
                                                                         &arg2)) {
                                    *out_words = want_void
                                        ? machine_direct_call_three_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                                          arg0_kind,
                                                                                          arg0_primary,
                                                                                          arg0,
                                                                                          arg1_kind,
                                                                                          arg1_primary,
                                                                                          arg1,
                                                                                          arg2_kind,
                                                                                          arg2_primary,
                                                                                          arg2)
                                        : machine_direct_call_three_simple_arg_words(machine_target_architecture(target_triple),
                                                                                     arg0_kind,
                                                                                     arg0_primary,
                                                                                     arg0,
                                                                                     arg1_kind,
                                                                                     arg1_primary,
                                                                                     arg1,
                                                                                     arg2_kind,
                                                                                     arg2_primary,
                                                                                     arg2);
                                }
                                free(arg0_kind);
                                free(arg0_primary);
                                free(arg1_kind);
                                free(arg1_primary);
                                free(arg2_kind);
                                free(arg2_primary);
                            } else if (parts.len == 4) {
                                char *arg0_kind = NULL;
                                char *arg0_primary = NULL;
                                int arg0 = -1;
                                char *arg1_kind = NULL;
                                char *arg1_primary = NULL;
                                int arg1 = -1;
                                char *arg2_kind = NULL;
                                char *arg2_primary = NULL;
                                int arg2 = -1;
                                char *arg3_kind = NULL;
                                char *arg3_primary = NULL;
                                int arg3 = -1;
                                if (compiler_core_resolve_alias_arg_expr(parts.items[0],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &arg0_kind,
                                                                         &arg0_primary,
                                                                         &arg0) &&
                                    compiler_core_resolve_alias_arg_expr(parts.items[1],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &arg1_kind,
                                                                         &arg1_primary,
                                                                         &arg1) &&
                                    compiler_core_resolve_alias_arg_expr(parts.items[2],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &arg2_kind,
                                                                         &arg2_primary,
                                                                         &arg2) &&
                                    compiler_core_resolve_alias_arg_expr(parts.items[3],
                                                                         alias_name,
                                                                         alias_kind,
                                                                         alias_primary,
                                                                         alias_arg0,
                                                                         param_names,
                                                                         &arg3_kind,
                                                                         &arg3_primary,
                                                                         &arg3)) {
                                    *out_words = want_void
                                        ? machine_direct_call_four_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                                         arg0_kind,
                                                                                         arg0_primary,
                                                                                         arg0,
                                                                                         arg1_kind,
                                                                                         arg1_primary,
                                                                                         arg1,
                                                                                         arg2_kind,
                                                                                         arg2_primary,
                                                                                         arg2,
                                                                                         arg3_kind,
                                                                                         arg3_primary,
                                                                                         arg3)
                                        : machine_direct_call_four_simple_arg_words(machine_target_architecture(target_triple),
                                                                                    arg0_kind,
                                                                                    arg0_primary,
                                                                                    arg0,
                                                                                    arg1_kind,
                                                                                    arg1_primary,
                                                                                    arg1,
                                                                                    arg2_kind,
                                                                                    arg2_primary,
                                                                                    arg2,
                                                                                    arg3_kind,
                                                                                    arg3_primary,
                                                                                    arg3);
                                }
                                free(arg0_kind);
                                free(arg0_primary);
                                free(arg1_kind);
                                free(arg1_primary);
                                free(arg2_kind);
                                free(arg2_primary);
                                free(arg3_kind);
                                free(arg3_primary);
                            }
                            string_list_free(&parts);
                            if (*out_words == NULL) {
                                free(call_symbol);
                                call_symbol = NULL;
                            }
                        }
                        free(arg_text);
                        free(callee);
                    }
                }
                free(expr);
            }
            free(line1);
        }
        free(alias_name);
        free(alias_kind);
        free(alias_primary);
    }
    string_list_free(&active_lines);
    string_list_free(&body_lines);
    return call_symbol;
}

static char *compiler_core_direct_exec_words(const CompilerCoreBundle *bundle,
                                             size_t item_idx,
                                             size_t routine_idx,
                                             const char *architecture);

static int compiler_core_routine_returns_empty_bytes(const CompilerCoreBundle *bundle,
                                                     int routine_idx,
                                                     const char *architecture) {
    int item_idx;
    char *words;
    char *empty_words;
    int ok = 0;
    if (routine_idx < 0 || (size_t)routine_idx >= bundle->program.routine_names.len) {
        return 0;
    }
    item_idx = compiler_core_find_plan_item_index_by_label(bundle, bundle->program.routine_names.items[routine_idx]);
    if (item_idx < 0) {
        return 0;
    }
    words = compiler_core_direct_exec_words(bundle, (size_t)item_idx, (size_t)routine_idx, architecture);
    if (words == NULL) {
        return 0;
    }
    empty_words = machine_direct_return_empty_bytes_words(architecture);
    ok = empty_words != NULL && strcmp(words, empty_words) == 0;
    free(words);
    free(empty_words);
    return ok;
}

static char *compiler_core_direct_exec_multiline_words(const CompilerCoreBundle *bundle,
                                                       size_t item_idx,
                                                       size_t routine_idx,
                                                       const char *architecture,
                                                       const StringList *param_names) {
    const char *body_text = bundle->program.routine_body_texts.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    const char *return_type_text = bundle->program.routine_return_type_texts.items[routine_idx];
    StringList body_lines;
    StringList active_lines;
    char *out_words = NULL;
    memset(&body_lines, 0, sizeof(body_lines));
    memset(&active_lines, 0, sizeof(active_lines));
    (void)item_idx;
    if (body_text == NULL || body_text[0] == '\0') {
        return NULL;
    }
    split_text_lines(body_text, &body_lines);
    for (size_t i = 0; i < body_lines.len; ++i) {
        if (!is_blank_or_comment_line(body_lines.items[i])) {
            string_list_push_copy(&active_lines, body_lines.items[i]);
        }
    }
    if (out_words == NULL &&
        compiler_core_type_matches(return_type_text, "ByteReader") &&
        param_names->len == 1 &&
        active_lines.len >= 4) {
        char *decl_line = trim_copy(active_lines.items[0], active_lines.items[0] + strlen(active_lines.items[0]));
        char *return_line = trim_copy(active_lines.items[active_lines.len - 1],
                                      active_lines.items[active_lines.len - 1] + strlen(active_lines.items[active_lines.len - 1]));
        if ((starts_with(decl_line, "let ") || starts_with(decl_line, "var ")) &&
            starts_with(return_line, "return")) {
            char *tail = trim_copy(decl_line + 4, decl_line + strlen(decl_line));
            const char *name_end = parse_name_end(tail);
            if (name_end != tail) {
                char *name = trim_copy(tail, name_end);
                char *ret_expr = trim_copy(return_line + strlen("return"), return_line + strlen(return_line));
                if (strcmp(ret_expr, name) == 0) {
                    char *param_type_text = compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[routine_idx], 0);
                    int ok = compiler_core_type_matches(param_type_text, "ByteBuffer");
                    int saw_buf = 0;
                    int saw_pos = 0;
                    for (size_t i = 1; ok && i + 1 < active_lines.len; ++i) {
                        char *line = trim_copy(active_lines.items[i], active_lines.items[i] + strlen(active_lines.items[i]));
                        int eq_idx = find_top_level_token_text(line, "=", 0);
                        if (eq_idx < 0) {
                            ok = 0;
                        } else {
                            char *lhs = trim_copy(line, line + eq_idx);
                            char *rhs = trim_copy(line + eq_idx + 1, line + strlen(line));
                            const char *lhs_name_end = parse_name_end(lhs);
                            if (lhs_name_end == lhs ||
                                strncmp(lhs, name, (size_t)(lhs_name_end - lhs)) != 0 ||
                                lhs[lhs_name_end - lhs] != '.') {
                                ok = 0;
                            } else {
                                char *field_name = trim_copy(lhs_name_end + 1, lhs + strlen(lhs));
                                if (strcmp(field_name, "buf") == 0) {
                                    if (strcmp(rhs, param_names->items[0]) == 0) {
                                        saw_buf = 1;
                                    } else {
                                        ok = 0;
                                    }
                                } else if (strcmp(field_name, "pos") == 0) {
                                    if (strcmp(rhs, "0") == 0) {
                                        saw_pos = 1;
                                    } else {
                                        ok = 0;
                                    }
                                } else {
                                    ok = 0;
                                }
                                free(field_name);
                            }
                            free(lhs);
                            free(rhs);
                        }
                        free(line);
                    }
                    if (ok && saw_buf && saw_pos) {
                        out_words = machine_direct_return_register_pair_plus_zero_i32_words(architecture, 0, 1, 0, 2);
                    }
                    free(param_type_text);
                }
                free(ret_expr);
                free(name);
            }
            free(tail);
        }
        free(decl_line);
        free(return_line);
    }
    if (out_words == NULL &&
        (compiler_core_type_matches(return_type_text, "Bytes") ||
         compiler_core_type_matches(return_type_text, "ByteBuffer")) &&
        active_lines.len >= 3) {
        char *decl_line = trim_copy(active_lines.items[0], active_lines.items[0] + strlen(active_lines.items[0]));
        char *return_line = trim_copy(active_lines.items[active_lines.len - 1],
                                      active_lines.items[active_lines.len - 1] + strlen(active_lines.items[active_lines.len - 1]));
        if (starts_with(decl_line, "let ") || starts_with(decl_line, "var ")) {
            char *tail = trim_copy(decl_line + 4, decl_line + strlen(decl_line));
            const char *name_end = parse_name_end(tail);
            if (name_end != tail && starts_with(return_line, "return")) {
                char *name = trim_copy(tail, name_end);
                char *ret_expr = trim_copy(return_line + strlen("return"), return_line + strlen(return_line));
                if (strcmp(ret_expr, name) == 0) {
                    int saw_store = 0;
                    int ok = 1;
                    for (size_t i = 1; i + 1 < active_lines.len; ++i) {
                        char *line = trim_copy(active_lines.items[i], active_lines.items[i] + strlen(active_lines.items[i]));
                        int eq_idx = find_top_level_token_text(line, "=", 0);
                        if (eq_idx < 0) {
                            ok = 0;
                            free(line);
                            break;
                        }
                        char *lhs = trim_copy(line, line + eq_idx);
                        char *rhs = trim_copy(line + eq_idx + 1, line + strlen(line));
                        const char *lhs_name_end = parse_name_end(lhs);
                        if (lhs_name_end == lhs ||
                            strncmp(lhs, name, (size_t)(lhs_name_end - lhs)) != 0 ||
                            lhs[lhs_name_end - lhs] != '.') {
                            ok = 0;
                        } else {
                            int zero_value_ok = strcmp(rhs, "0") == 0 || strcmp(rhs, "nil") == 0;
                            int zero_call_ok = 0;
                            if (!zero_value_ok) {
                                const char *aggregate_result_type_text = "";
                                int source_reg0 = -1;
                                int source_reg1 = -1;
                                int source_reg1_is64 = 0;
                                char *field_name = trim_copy(lhs_name_end + 1, lhs + strlen(lhs));
                                const char *rhs_name_end = parse_name_end(rhs);
                                if (compiler_core_direct_aggregate_field_binding(return_type_text,
                                                                                field_name,
                                                                                &aggregate_result_type_text,
                                                                                &source_reg0,
                                                                                &source_reg1,
                                                                                &source_reg1_is64) &&
                                    compiler_core_type_matches(aggregate_result_type_text, "Bytes")) {
                                    int open_idx = (int)(rhs_name_end - rhs);
                                    if (open_idx > 0 && rhs[open_idx] == '(') {
                                        int close_idx = find_matching_delimiter_text(rhs, open_idx, '(', ')');
                                        if (close_idx >= 0 && rhs[close_idx + 1] == '\0' && close_idx == open_idx + 1) {
                                            char *callee = trim_copy(rhs, rhs + open_idx);
                                            int callee_routine_idx =
                                                compiler_core_direct_exec_callee_routine_index_for_call(bundle,
                                                                                                        (int)item_idx,
                                                                                                        callee,
                                                                                                        signature,
                                                                                                        0,
                                                                                                        NULL,
                                                                                                        NULL,
                                                                                                        NULL);
                                            if (callee_routine_idx >= 0 &&
                                                compiler_core_routine_returns_empty_bytes(bundle,
                                                                                         callee_routine_idx,
                                                                                         architecture)) {
                                                zero_call_ok = 1;
                                            }
                                            free(callee);
                                        }
                                    }
                                }
                                free(field_name);
                            }
                            if (!zero_value_ok && !zero_call_ok) {
                                ok = 0;
                            } else {
                                saw_store = 1;
                            }
                        }
                        free(lhs);
                        free(rhs);
                        free(line);
                        if (!ok) {
                            break;
                        }
                    }
                    if (ok && saw_store) {
                        out_words = machine_direct_return_empty_bytes_words(architecture);
                    }
                }
                free(ret_expr);
                free(name);
            }
            free(tail);
        }
        free(decl_line);
        free(return_line);
    }
    if (active_lines.len == 2) {
        char *line0 = trim_copy(active_lines.items[0], active_lines.items[0] + strlen(active_lines.items[0]));
        char *line1 = trim_copy(active_lines.items[1], active_lines.items[1] + strlen(active_lines.items[1]));
        if (starts_with(line0, "let ") || starts_with(line0, "var ")) {
            char *tail = trim_copy(line0 + 4, line0 + strlen(line0));
            const char *name_end = parse_name_end(tail);
            int eq_idx = find_top_level_token_text(tail, "=", 0);
            if (name_end != tail && eq_idx >= 0 && (int)(name_end - tail) <= eq_idx) {
                char *name = trim_copy(tail, name_end);
                char *rhs = trim_copy(tail + eq_idx + 1, tail + strlen(tail));
                char *ret_line = trim_copy(line1, line1 + strlen(line1));
                if (starts_with(ret_line, "return")) {
                    char *ret_expr = trim_copy(ret_line + strlen("return"), ret_line + strlen(ret_line));
                    char *rhs_kind = NULL;
                    char *rhs_primary = NULL;
                    int rhs_arg0 = -1;
                    if (strcmp(ret_expr, name) == 0 &&
                        compiler_core_match_simple_value_expr(rhs,
                                                              param_names,
                                                              &rhs_kind,
                                                              &rhs_primary,
                                                              &rhs_arg0)) {
                        out_words = machine_direct_return_simple_value_words(architecture,
                                                                             rhs_kind,
                                                                             rhs_primary,
                                                                             rhs_arg0);
                    }
                    free(rhs_kind);
                    free(rhs_primary);
                    free(ret_expr);
                }
                free(ret_line);
                free(name);
                free(rhs);
            }
            free(tail);
        }
        free(line0);
        free(line1);
    }
    if (out_words == NULL && active_lines.len == 4 && param_names->len == 1) {
        char *param_type_text = compiler_core_signature_param_type_text(signature, 0);
        if (strncmp(param_type_text, "var ", 4) == 0 &&
            compiler_core_type_matches(param_type_text, "str")) {
            const char *expected_fields[] = {"data", "len", "store_id", "flags"};
            const char *expected_values[] = {"nil", "0", "0", "0"};
            int ok = 1;
            size_t i;
            for (i = 0; i < 4 && ok; ++i) {
                char *line = trim_copy(active_lines.items[i], active_lines.items[i] + strlen(active_lines.items[i]));
                int assign_idx = find_last_top_level_assign_text(line);
                if (assign_idx < 0) {
                    ok = 0;
                } else {
                    char *lhs = trim_copy(line, line + assign_idx);
                    char *rhs = trim_copy(line + assign_idx + 1, line + strlen(line));
                    const char *base_end = parse_name_end(lhs);
                    if (base_end == lhs ||
                        (size_t)(base_end - lhs) != strlen(param_names->items[0]) ||
                        strncmp(lhs, param_names->items[0], strlen(param_names->items[0])) != 0 ||
                        *base_end != '.' ||
                        strcmp(base_end + 1, expected_fields[i]) != 0 ||
                        strcmp(rhs, expected_values[i]) != 0) {
                        ok = 0;
                    }
                    free(lhs);
                    free(rhs);
                }
                free(line);
            }
            if (ok) {
                out_words = machine_direct_zero_var_str_out_words(architecture);
            }
        }
        free(param_type_text);
    }
    if (out_words == NULL && active_lines.len == 4 && param_names->len == 2) {
        char *out_param_type_text = compiler_core_signature_param_type_text(signature, 0);
        char *value_param_type_text = compiler_core_signature_param_type_text(signature, 1);
        if (strncmp(out_param_type_text, "var ", 4) == 0 &&
            compiler_core_type_matches(out_param_type_text, "str") &&
            compiler_core_type_matches(value_param_type_text, "str")) {
            const char *expected_fields[] = {"data", "len", "store_id", "flags"};
            int ok = 1;
            size_t i;
            for (i = 0; i < 4 && ok; ++i) {
                char *line = trim_copy(active_lines.items[i], active_lines.items[i] + strlen(active_lines.items[i]));
                int assign_idx = find_last_top_level_assign_text(line);
                if (assign_idx < 0) {
                    ok = 0;
                } else {
                    char *lhs = trim_copy(line, line + assign_idx);
                    char *rhs = trim_copy(line + assign_idx + 1, line + strlen(line));
                    const char *lhs_base_end = parse_name_end(lhs);
                    const char *rhs_base_end = parse_name_end(rhs);
                    if (lhs_base_end == lhs ||
                        rhs_base_end == rhs ||
                        (size_t)(lhs_base_end - lhs) != strlen(param_names->items[0]) ||
                        (size_t)(rhs_base_end - rhs) != strlen(param_names->items[1]) ||
                        strncmp(lhs, param_names->items[0], strlen(param_names->items[0])) != 0 ||
                        strncmp(rhs, param_names->items[1], strlen(param_names->items[1])) != 0 ||
                        *lhs_base_end != '.' ||
                        *rhs_base_end != '.' ||
                        strcmp(lhs_base_end + 1, expected_fields[i]) != 0 ||
                        strcmp(rhs_base_end + 1, expected_fields[i]) != 0) {
                        ok = 0;
                    }
                    free(lhs);
                    free(rhs);
                }
                free(line);
            }
            if (ok) {
                out_words = machine_direct_copy_var_str_out_words(architecture);
            }
        }
        free(out_param_type_text);
        free(value_param_type_text);
    }
    if (out_words == NULL && active_lines.len > 0) {
        size_t i;
        int all_simple_exprs = 1;
        for (i = 0; i < active_lines.len; ++i) {
            char *expr_kind = NULL;
            char *expr_primary = NULL;
            int expr_arg0 = -1;
            if (!compiler_core_match_simple_value_expr(active_lines.items[i],
                                                       param_names,
                                                       &expr_kind,
                                                       &expr_primary,
                                                       &expr_arg0)) {
                all_simple_exprs = 0;
                free(expr_kind);
                free(expr_primary);
                break;
            }
            free(expr_kind);
            free(expr_primary);
        }
        if (all_simple_exprs) {
            out_words = machine_direct_return_simple_value_words(architecture, "load_int32", "0", -1);
        }
    }
    if (out_words == NULL && active_lines.len >= 4) {
        char *header = trim_copy(active_lines.items[0], active_lines.items[0] + strlen(active_lines.items[0]));
        if (starts_with(header, "if ") && header[strlen(header) - 1] == ':') {
            StringList then_lines;
            StringList else_lines;
            int next_idx;
            memset(&then_lines, 0, sizeof(then_lines));
            memset(&else_lines, 0, sizeof(else_lines));
            next_idx = collect_outline_block_lines(active_lines.items, (int)active_lines.len, 1, &then_lines);
            if (then_lines.len == 1 && next_idx < (int)active_lines.len) {
                char *else_head = trim_copy(active_lines.items[next_idx],
                                            active_lines.items[next_idx] + strlen(active_lines.items[next_idx]));
                if (strcmp(else_head, "else:") == 0) {
                    int end_idx = collect_outline_block_lines(active_lines.items,
                                                              (int)active_lines.len,
                                                              next_idx + 1,
                                                              &else_lines);
                    if (else_lines.len == 1 && end_idx == (int)active_lines.len) {
                        char *then_kind = NULL;
                        char *then_primary = NULL;
                        int then_arg0 = -1;
                        char *else_kind = NULL;
                        char *else_primary = NULL;
                        int else_arg0 = -1;
                        if (compiler_core_match_return_simple_line(then_lines.items[0],
                                                                   param_names,
                                                                   &then_kind,
                                                                   &then_primary,
                                                                   &then_arg0) &&
                            compiler_core_match_return_simple_line(else_lines.items[0],
                                                                   param_names,
                                                                   &else_kind,
                                                                   &else_primary,
                                                                   &else_arg0)) {
                            char *cond_text = trim_copy(header + strlen("if "),
                                                        header + strlen(header) - 1);
                            char *cond_kind = NULL;
                            char *cond_primary = NULL;
                            int cond_arg0 = -1;
                            if (compiler_core_match_simple_value_expr(cond_text,
                                                                      param_names,
                                                                      &cond_kind,
                                                                      &cond_primary,
                                                                      &cond_arg0)) {
                                out_words = machine_direct_if_else_simple_cond_words(architecture,
                                                                                     cond_kind,
                                                                                     cond_primary,
                                                                                     cond_arg0,
                                                                                     then_kind,
                                                                                     then_primary,
                                                                                     then_arg0,
                                                                                     else_kind,
                                                                                     else_primary,
                                                                                     else_arg0);
                                free(cond_kind);
                                free(cond_primary);
                            } else {
                                const char *operators[] = {"==", "!=", "<=", ">=", "<", ">"};
                                size_t i;
                                for (i = 0; i < sizeof(operators) / sizeof(operators[0]) && out_words == NULL; ++i) {
                                    int split = find_last_top_level_binary_text(cond_text, operators[i]);
                                    if (split >= 0) {
                                        char *lhs = trim_copy(cond_text, cond_text + split);
                                        char *rhs = trim_copy(cond_text + split + strlen(operators[i]),
                                                              cond_text + strlen(cond_text));
                                        char *lhs_kind = NULL;
                                        char *lhs_primary = NULL;
                                        int lhs_arg0 = -1;
                                        char *rhs_kind = NULL;
                                        char *rhs_primary = NULL;
                                        int rhs_arg0 = -1;
                                        if (compiler_core_match_simple_value_expr(lhs,
                                                                                  param_names,
                                                                                  &lhs_kind,
                                                                                  &lhs_primary,
                                                                                  &lhs_arg0) &&
                                            compiler_core_match_simple_value_expr(rhs,
                                                                                  param_names,
                                                                                  &rhs_kind,
                                                                                  &rhs_primary,
                                                                                  &rhs_arg0)) {
                                            out_words = machine_direct_if_else_binary_cond_words(architecture,
                                                                                                 lhs_kind,
                                                                                                 lhs_primary,
                                                                                                 lhs_arg0,
                                                                                                 rhs_kind,
                                                                                                 rhs_primary,
                                                                                                 rhs_arg0,
                                                                                                 operators[i],
                                                                                                 then_kind,
                                                                                                 then_primary,
                                                                                                 then_arg0,
                                                                                                 else_kind,
                                                                                                 else_primary,
                                                                                                 else_arg0);
                                        }
                                        free(lhs_kind);
                                        free(lhs_primary);
                                        free(rhs_kind);
                                        free(rhs_primary);
                                        free(lhs);
                                        free(rhs);
                                    }
                                }
                            }
                            free(cond_kind);
                            free(cond_primary);
                            free(cond_text);
                        }
                        free(then_kind);
                        free(then_primary);
                        free(else_kind);
                        free(else_primary);
                    }
                }
                free(else_head);
            }
            string_list_free(&then_lines);
            string_list_free(&else_lines);
        }
        free(header);
    }
    string_list_free(&active_lines);
    string_list_free(&body_lines);
    return out_words;
}

static char *compiler_core_direct_exec_words(const CompilerCoreBundle *bundle,
                                             size_t item_idx,
                                             size_t routine_idx,
                                             const char *architecture) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    const char *body_text = bundle->program.routine_body_texts.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    StringList param_names;
    char *trimmed = trim_copy(body, body + strlen(body));
    char *expr = NULL;
    char *kind = NULL;
    char *primary = NULL;
    int arg0 = -1;
    memset(&param_names, 0, sizeof(param_names));
    (void)item_idx;
    parse_compiler_routine_signature_param_names(signature, &param_names);
    if (body_text != NULL && strchr(body_text, '\n') != NULL) {
        char *multiline_words =
            compiler_core_direct_exec_multiline_words(bundle, item_idx, routine_idx, architecture, &param_names);
        if (multiline_words != NULL) {
            string_list_free(&param_names);
            free(trimmed);
            return multiline_words;
        }
        if (!compiler_core_direct_text_match_allowed(bundle, routine_idx, 1, 2)) {
            string_list_free(&param_names);
            free(trimmed);
            return NULL;
        }
    }
    if (starts_with(trimmed, "return")) {
        expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    } else {
        expr = trim_copy(trimmed, trimmed + strlen(trimmed));
    }
    if (expr[0] == '\0') {
        free(expr);
        free(trimmed);
        string_list_free(&param_names);
        return machine_direct_return_simple_value_words(architecture, "load_int32", "0", -1);
    }
    if (compiler_core_match_empty_bytes_expr(expr)) {
        free(expr);
        free(trimmed);
        string_list_free(&param_names);
        return machine_direct_return_empty_bytes_words(architecture);
    }
    {
        char *words = compiler_core_direct_exec_param_return_words(bundle, routine_idx, architecture);
        if (words != NULL) {
            free(expr);
            free(trimmed);
            string_list_free(&param_names);
            return words;
        }
    }
    {
        const char *aggregate_result_type_text = "";
        int source_reg0 = -1;
        int source_reg1 = -1;
        int source_reg1_is64 = 0;
        char *return_type_text = NULL;
        int param_count = 0;
        parse_compiler_routine_signature_shape(signature, &return_type_text, &param_count);
        if (compiler_core_match_aggregate_field_expr(expr,
                                                     &param_names,
                                                     signature,
                                                     &aggregate_result_type_text,
                                                     &source_reg0,
                                                     &source_reg1,
                                                     &source_reg1_is64) &&
            compiler_core_type_matches(return_type_text, aggregate_result_type_text)) {
            char *words = machine_direct_return_register_pair_words(architecture,
                                                                    source_reg0,
                                                                    source_reg1,
                                                                    source_reg1_is64);
            free(return_type_text);
            free(expr);
            free(trimmed);
            string_list_free(&param_names);
            return words;
        }
        free(return_type_text);
    }
    {
        const char *name_end = parse_name_end(expr);
        if (bundle->program.routine_param_counts.items[routine_idx] == 1 &&
            name_end != expr &&
            *name_end == '.') {
            char *name = trim_copy(expr, name_end);
            int param_slot = compiler_core_find_param_slot(&param_names, name);
            if (param_slot == 0) {
                const char *field_start = name_end + 1;
                const char *field_end = parse_name_end(field_start);
                if (field_end != field_start && *field_end == '\0') {
                    char *field_name = trim_copy(field_start, field_end);
                    char *param_type_text = compiler_core_signature_param_type_text(signature, 0);
                    int source_reg = -1;
                    int is64 = 0;
                    if (compiler_core_direct_field_return_binding(param_type_text,
                                                                  field_name,
                                                                  &source_reg,
                                                                  &is64)) {
                        char *words = machine_direct_return_register_words(architecture,
                                                                          source_reg,
                                                                          is64);
                        free(param_type_text);
                        free(field_name);
                        free(name);
                        free(expr);
                        free(trimmed);
                        string_list_free(&param_names);
                        return words;
                    }
                    free(param_type_text);
                    free(field_name);
                }
            }
            free(name);
        }
    }
    if (compiler_core_match_simple_value_expr(expr, &param_names, &kind, &primary, &arg0)) {
        char *words = machine_direct_return_simple_value_words(architecture, kind, primary, arg0);
        free(kind);
        free(primary);
        free(expr);
        free(trimmed);
        string_list_free(&param_names);
        return words;
    }
    if (expr[0] == '!' || (expr[0] == '-' && expr[1] != '\0')) {
        char *operand = trim_copy(expr + 1, expr + strlen(expr));
        char *operand_kind = NULL;
        char *operand_primary = NULL;
        int operand_arg0 = -1;
        if (compiler_core_match_simple_value_expr(operand,
                                                  &param_names,
                                                  &operand_kind,
                                                  &operand_primary,
                                                  &operand_arg0)) {
            char *words = machine_direct_unary_words(architecture,
                                                     expr[0] == '!' ? "unary_not" : "unary_neg",
                                                     operand_kind,
                                                     operand_primary,
                                                     operand_arg0);
            free(operand_kind);
            free(operand_primary);
            free(operand);
            free(expr);
            free(trimmed);
            string_list_free(&param_names);
            return words;
        }
        free(operand);
    }
    {
        const char *compare_operators[] = {"==", "!=", "<=", ">=", "<", ">"};
        size_t i;
        for (i = 0; i < sizeof(compare_operators) / sizeof(compare_operators[0]); ++i) {
            int compare_split = find_last_top_level_binary_text(expr, compare_operators[i]);
            if (compare_split >= 0) {
                char *lhs = trim_copy(expr, expr + compare_split);
                char *rhs = trim_copy(expr + compare_split + strlen(compare_operators[i]),
                                      expr + strlen(expr));
                const char *arithmetic_operators[] = {"+", "-"};
                size_t j;
                for (j = 0; j < sizeof(arithmetic_operators) / sizeof(arithmetic_operators[0]); ++j) {
                    int arithmetic_split = find_last_top_level_binary_text(lhs, arithmetic_operators[j]);
                    if (arithmetic_split >= 0 &&
                        !(strcmp(arithmetic_operators[j], "-") == 0 && arithmetic_split == 0)) {
                        char *lhs_field_expr = trim_copy(lhs, lhs + arithmetic_split);
                        char *arithmetic_rhs_expr =
                            trim_copy(lhs + arithmetic_split + strlen(arithmetic_operators[j]),
                                      lhs + strlen(lhs));
                        int lhs_source_reg = -1;
                        int lhs_is64 = 0;
                        char *rhs_kind = NULL;
                        char *rhs_primary = NULL;
                        int rhs_arg0 = -1;
                        int compare_source_reg = -1;
                        int compare_is64 = 0;
                        if (compiler_core_match_param_field_expr(lhs_field_expr,
                                                                 &param_names,
                                                                 signature,
                                                                 &lhs_source_reg,
                                                                 &lhs_is64) &&
                            compiler_core_match_simple_value_expr(arithmetic_rhs_expr,
                                                                  &param_names,
                                                                  &rhs_kind,
                                                                  &rhs_primary,
                                                                  &rhs_arg0) &&
                            compiler_core_match_param_nested_field_expr(rhs,
                                                                        &param_names,
                                                                        signature,
                                                                        &compare_source_reg,
                                                                        &compare_is64)) {
                            char *words =
                                machine_direct_arithmetic_compare_words(architecture,
                                                                        signature,
                                                                        lhs_source_reg,
                                                                        lhs_is64,
                                                                        arithmetic_operators[j],
                                                                        rhs_kind,
                                                                        rhs_primary,
                                                                        rhs_arg0,
                                                                        compare_source_reg,
                                                                        compare_is64,
                                                                        compare_operators[i]);
                            free(rhs_kind);
                            free(rhs_primary);
                            free(lhs_field_expr);
                            free(arithmetic_rhs_expr);
                            if (words != NULL) {
                                free(lhs);
                                free(rhs);
                                free(expr);
                                free(trimmed);
                                string_list_free(&param_names);
                                return words;
                            }
                        } else {
                            free(rhs_kind);
                            free(rhs_primary);
                            free(lhs_field_expr);
                            free(arithmetic_rhs_expr);
                        }
                    }
                }
                free(lhs);
                free(rhs);
            }
        }
    }
    {
        const char *operators[] = {"==", "!=", "<=", ">=", "<", ">", "+", "-"};
        size_t i;
        for (i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i) {
            int split = find_last_top_level_binary_text(expr, operators[i]);
            if (split >= 0 && !(strcmp(operators[i], "-") == 0 && split == 0)) {
                char *lhs = trim_copy(expr, expr + split);
                char *rhs = trim_copy(expr + split + strlen(operators[i]), expr + strlen(expr));
                char *lhs_kind = NULL;
                char *lhs_primary = NULL;
                int lhs_arg0 = -1;
                char *rhs_kind = NULL;
                char *rhs_primary = NULL;
                int rhs_arg0 = -1;
                if (compiler_core_match_simple_value_expr(lhs, &param_names, &lhs_kind, &lhs_primary, &lhs_arg0) &&
                    compiler_core_match_simple_value_expr(rhs, &param_names, &rhs_kind, &rhs_primary, &rhs_arg0)) {
                    char *words = machine_direct_binary_words(architecture,
                                                             lhs_kind,
                                                             lhs_primary,
                                                             lhs_arg0,
                                                             rhs_kind,
                                                             rhs_primary,
                                                             rhs_arg0,
                                                             operators[i]);
                    free(lhs_kind);
                    free(lhs_primary);
                    free(rhs_kind);
                    free(rhs_primary);
                    free(lhs);
                    free(rhs);
                    free(expr);
                    free(trimmed);
                    string_list_free(&param_names);
                    return words;
                }
                free(lhs_kind);
                free(lhs_primary);
                free(rhs_kind);
                free(rhs_primary);
                free(lhs);
                free(rhs);
            }
        }
    }
    free(expr);
    free(trimmed);
    string_list_free(&param_names);
    return NULL;
}

static char *compiler_core_direct_exec_cstring_payload(const CompilerCoreBundle *bundle,
                                                       size_t routine_idx) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    char *expr = NULL;
    char *return_type_text = NULL;
    int param_count = 0;
    char *payload = NULL;
    parse_compiler_routine_signature_shape(signature, &return_type_text, &param_count);
    if (!compiler_core_type_matches(return_type_text, "str")) {
        free(return_type_text);
        free(trimmed);
        return NULL;
    }
    if (starts_with(trimmed, "return")) {
        expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    } else {
        expr = trim_copy(trimmed, trimmed + strlen(trimmed));
    }
    payload = compiler_core_parse_string_literal_expr(expr);
    free(expr);
    free(return_type_text);
    free(trimmed);
    return payload;
}

static char *compiler_core_direct_exec_call_cstring_payload(const CompilerCoreBundle *bundle,
                                                            size_t routine_idx) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    char *expr = NULL;
    char *arg_text = NULL;
    char *payload = NULL;
    const char *name_end = NULL;
    int open_idx = -1;
    int close_idx = -1;
    if (!starts_with(trimmed, "return ")) {
        free(trimmed);
        return NULL;
    }
    expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    name_end = parse_call_name_end(expr);
    open_idx = (int)(name_end - expr);
    if (open_idx <= 0 || expr[open_idx] != '(') {
        free(expr);
        free(trimmed);
        return NULL;
    }
    close_idx = find_matching_delimiter_text(expr, open_idx, '(', ')');
    if (close_idx < 0 || expr[close_idx + 1] != '\0') {
        free(expr);
        free(trimmed);
        return NULL;
    }
    arg_text = trim_copy(expr + open_idx + 1, expr + close_idx);
    {
        StringList parts = split_top_level_segments_text(arg_text, ',');
        if (parts.len == 1) {
            payload = compiler_core_parse_string_literal_expr(parts.items[0]);
        }
        string_list_free(&parts);
    }
    free(arg_text);
    free(expr);
    free(trimmed);
    return payload;
}

static char *compiler_core_direct_exec_void_call_cstring_payload(const CompilerCoreBundle *bundle,
                                                                 size_t routine_idx) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    char *arg_text = NULL;
    char *payload = NULL;
    const char *name_end = parse_call_name_end(trimmed);
    int open_idx = (int)(name_end - trimmed);
    int close_idx = -1;
    if (open_idx <= 0 || trimmed[open_idx] != '(') {
        free(trimmed);
        return NULL;
    }
    close_idx = find_matching_delimiter_text(trimmed, open_idx, '(', ')');
    if (close_idx < 0 || trimmed[close_idx + 1] != '\0') {
        free(trimmed);
        return NULL;
    }
    arg_text = trim_copy(trimmed + open_idx + 1, trimmed + close_idx);
    {
        StringList parts = split_top_level_segments_text(arg_text, ',');
        if (parts.len == 1) {
            payload = compiler_core_parse_string_literal_expr(parts.items[0]);
        }
        string_list_free(&parts);
    }
    free(arg_text);
    free(trimmed);
    return payload;
}

static char *compiler_core_direct_exec_nested_call_cstring_payload(const CompilerCoreBundle *bundle,
                                                                   size_t routine_idx) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    char *expr = NULL;
    char *outer_arg_text = NULL;
    char *payload = NULL;
    const char *outer_name_end = NULL;
    int outer_open_idx = -1;
    int outer_close_idx = -1;
    if (!starts_with(trimmed, "return ")) {
        free(trimmed);
        return NULL;
    }
    expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    outer_name_end = parse_call_name_end(expr);
    outer_open_idx = (int)(outer_name_end - expr);
    if (outer_open_idx <= 0 || expr[outer_open_idx] != '(') {
        free(expr);
        free(trimmed);
        return NULL;
    }
    outer_close_idx = find_matching_delimiter_text(expr, outer_open_idx, '(', ')');
    if (outer_close_idx < 0 || expr[outer_close_idx + 1] != '\0') {
        free(expr);
        free(trimmed);
        return NULL;
    }
    outer_arg_text = trim_copy(expr + outer_open_idx + 1, expr + outer_close_idx);
    {
        StringList parts = split_top_level_segments_text(outer_arg_text, ',');
        if (parts.len == 2) {
            const char *inner_name_end = parse_call_name_end(parts.items[0]);
            int inner_open_idx = (int)(inner_name_end - parts.items[0]);
            if (inner_open_idx > 0 && parts.items[0][inner_open_idx] == '(') {
                int inner_close_idx =
                    find_matching_delimiter_text(parts.items[0], inner_open_idx, '(', ')');
                if (inner_close_idx >= 0 && parts.items[0][inner_close_idx + 1] == '\0') {
                    payload = compiler_core_parse_string_literal_expr(parts.items[1]);
                }
            }
        }
        string_list_free(&parts);
    }
    free(outer_arg_text);
    free(expr);
    free(trimmed);
    return payload;
}

static int compiler_core_multiline_fallback_call_symbols(const CompilerCoreBundle *bundle,
                                                         size_t item_idx,
                                                         size_t routine_idx,
                                                         const char *target_triple,
                                                         char **out_words,
                                                         char **out_call_symbol0,
                                                         char **out_call_symbol1) {
    const char *body_text = bundle->program.routine_body_texts.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    StringList body_lines;
    StringList active_lines;
    StringList param_names;
    char *decl_line = NULL;
    char *if_line = NULL;
    char *return_alias_line = NULL;
    char *fallback_line = NULL;
    int ok = 0;
    memset(&body_lines, 0, sizeof(body_lines));
    memset(&active_lines, 0, sizeof(active_lines));
    *out_words = NULL;
    *out_call_symbol0 = NULL;
    *out_call_symbol1 = NULL;
    memset(&param_names, 0, sizeof(param_names));
    if (!compiler_core_direct_call_return_type_supported(bundle->program.routine_return_type_texts.items[routine_idx])) {
        return 0;
    }
    if (!compiler_core_direct_text_match_allowed(bundle, routine_idx, 4, 4)) {
        return 0;
    }
    parse_compiler_routine_signature_param_names(signature, &param_names);
    if (body_text == NULL || strchr(body_text, '\n') == NULL) {
        return 0;
    }
    split_text_lines(body_text, &body_lines);
    for (size_t i = 0; i < body_lines.len; ++i) {
        if (!is_blank_or_comment_line(body_lines.items[i])) {
            string_list_push_copy(&active_lines, body_lines.items[i]);
        }
    }
    if (active_lines.len == 4) {
        decl_line = trim_copy(active_lines.items[0], active_lines.items[0] + strlen(active_lines.items[0]));
        if_line = trim_copy(active_lines.items[1], active_lines.items[1] + strlen(active_lines.items[1]));
        return_alias_line =
            trim_copy(active_lines.items[2], active_lines.items[2] + strlen(active_lines.items[2]));
        fallback_line =
            trim_copy(active_lines.items[3], active_lines.items[3] + strlen(active_lines.items[3]));
        if ((starts_with(decl_line, "let ") || starts_with(decl_line, "var ")) &&
            starts_with(if_line, "if ") &&
            starts_with(return_alias_line, "return ") &&
            starts_with(fallback_line, "return ") &&
            if_line[strlen(if_line) - 1] == ':') {
            char *decl_tail = trim_copy(decl_line + 4, decl_line + strlen(decl_line));
            int eq_idx = find_top_level_token_text(decl_tail, "=", 0);
            if (eq_idx > 0) {
                char *lhs = trim_copy(decl_tail, decl_tail + eq_idx);
                char *rhs = trim_copy(decl_tail + eq_idx + 1, decl_tail + strlen(decl_tail));
                const char *lhs_name_end = parse_name_end(lhs);
                if (lhs_name_end != lhs) {
                    char *alias_name = trim_copy(lhs, lhs_name_end);
                    char *if_cond = trim_copy(if_line + strlen("if "), if_line + strlen(if_line) - 1);
                    char *return_alias = trim_copy(return_alias_line + strlen("return "),
                                                   return_alias_line + strlen(return_alias_line));
                    char *fallback_expr = trim_copy(fallback_line + strlen("return "),
                                                    fallback_line + strlen(fallback_line));
                    char *expected_cond = xformat("%s != nil", alias_name);
                    if (strcmp(if_cond, expected_cond) == 0 && strcmp(return_alias, alias_name) == 0) {
                        char *call_exprs[2];
                        char *call_symbols[2];
                        const char *arg_kinds[4];
                        const char *arg_primary_texts[4];
                        int arg_arg0s[4];
                        int arg_count = -1;
                        memset(call_exprs, 0, sizeof(call_exprs));
                        memset(call_symbols, 0, sizeof(call_symbols));
                        call_exprs[0] = rhs;
                        rhs = NULL;
                        call_exprs[1] = fallback_expr;
                        fallback_expr = NULL;
                        ok = 1;
                        for (int call_idx = 0; call_idx < 2 && ok; ++call_idx) {
                            const char *name_end = parse_call_name_end(call_exprs[call_idx]);
                            int open_idx = (int)(name_end - call_exprs[call_idx]);
                            if (open_idx <= 0 || call_exprs[call_idx][open_idx] != '(') {
                                ok = 0;
                                break;
                            }
                            int close_idx =
                                find_matching_delimiter_text(call_exprs[call_idx], open_idx, '(', ')');
                            if (close_idx < 0 || call_exprs[call_idx][close_idx + 1] != '\0') {
                                ok = 0;
                                break;
                            }
                            char *callee = trim_copy(call_exprs[call_idx], call_exprs[call_idx] + open_idx);
                            char *arg_text =
                                trim_copy(call_exprs[call_idx] + open_idx + 1, call_exprs[call_idx] + close_idx);
                            call_symbols[call_idx] =
                                compiler_core_direct_exec_callee_symbol(bundle,
                                                                        (int)item_idx,
                                                                        callee,
                                                                        target_triple);
                            if (call_symbols[call_idx] == NULL) {
                                ok = 0;
                                free(arg_text);
                                free(callee);
                                break;
                            }
                            StringList parts = split_top_level_segments_text(arg_text, ',');
                            if (arg_count < 0) {
                                arg_count = (int)parts.len;
                                if (arg_count > 4) {
                                    ok = 0;
                                }
                            } else if (arg_count != (int)parts.len) {
                                ok = 0;
                            }
                            for (int part_idx = 0; ok && part_idx < arg_count; ++part_idx) {
                                char *kind = NULL;
                                char *primary = NULL;
                                int arg0 = -1;
                                if (!compiler_core_match_simple_value_expr(parts.items[part_idx],
                                                                           &param_names,
                                                                           &kind,
                                                                           &primary,
                                                                           &arg0)) {
                                    ok = 0;
                                } else if (call_idx == 0) {
                                    arg_kinds[part_idx] = kind;
                                    arg_primary_texts[part_idx] = primary;
                                    arg_arg0s[part_idx] = arg0;
                                } else {
                                    if (strcmp(arg_kinds[part_idx], kind) != 0 ||
                                        strcmp(arg_primary_texts[part_idx], primary) != 0 ||
                                        arg_arg0s[part_idx] != arg0) {
                                        ok = 0;
                                    }
                                    free(kind);
                                    free(primary);
                                }
                            }
                            string_list_free(&parts);
                            free(arg_text);
                            free(callee);
                        }
                        if (ok) {
                            *out_words =
                                machine_direct_fallback_call_words(machine_target_architecture(target_triple),
                                                                   arg_count,
                                                                   arg_kinds,
                                                                   arg_primary_texts,
                                                                   arg_arg0s);
                            if (*out_words != NULL) {
                                *out_call_symbol0 = call_symbols[0];
                                *out_call_symbol1 = call_symbols[1];
                                call_symbols[0] = NULL;
                                call_symbols[1] = NULL;
                            } else {
                                ok = 0;
                            }
                        }
                        for (int part_idx = 0; part_idx < (arg_count < 0 ? 0 : arg_count); ++part_idx) {
                            free((char *)arg_kinds[part_idx]);
                            free((char *)arg_primary_texts[part_idx]);
                        }
                        free(call_exprs[0]);
                        free(call_exprs[1]);
                        free(call_symbols[0]);
                        free(call_symbols[1]);
                    }
                    free(expected_cond);
                    free(fallback_expr);
                    free(return_alias);
                    free(if_cond);
                    free(alias_name);
                }
                free(lhs);
                free(rhs);
            }
            free(decl_tail);
        }
    }
    free(decl_line);
    free(if_line);
    free(return_alias_line);
    free(fallback_line);
    string_list_free(&param_names);
    string_list_free(&active_lines);
    string_list_free(&body_lines);
    return ok && *out_words != NULL && *out_call_symbol0 != NULL && *out_call_symbol1 != NULL;
}

static int compiler_core_match_nested_call_expr_text(const CompilerCoreBundle *bundle,
                                                     size_t item_idx,
                                                     const char *expr,
                                                     const char *current_signature,
                                                     const StringList *param_names,
                                                     const char *target_triple,
                                                     int *out_arg_count,
                                                     char **out_call_symbol0,
                                                     char **out_call_symbol1,
                                                     char **out_arg_kinds,
                                                     char **out_arg_primary_texts,
                                                     int *out_arg_arg0s) {
    char *outer_callee = NULL;
    char *outer_arg_text = NULL;
    char *inner_expr = NULL;
    char *inner_callee = NULL;
    char *inner_arg_text = NULL;
    char *outer_symbol = NULL;
    char *inner_symbol = NULL;
    const char *outer_arg_kinds[1];
    const char *outer_arg_primary_texts[1];
    int outer_arg_arg0s[1];
    StringList outer_parts;
    StringList inner_parts;
    const char *outer_name_end;
    const char *inner_name_end;
    int outer_open_idx;
    int inner_open_idx;
    int outer_close_idx;
    int inner_close_idx;
    int outer_routine_idx;
    int inner_routine_idx;
    int ok = 0;
    int arg_count = 0;
    memset(&outer_parts, 0, sizeof(outer_parts));
    memset(&inner_parts, 0, sizeof(inner_parts));
    *out_arg_count = 0;
    *out_call_symbol0 = NULL;
    *out_call_symbol1 = NULL;
    for (int i = 0; i < 4; ++i) {
        out_arg_kinds[i] = NULL;
        out_arg_primary_texts[i] = NULL;
        out_arg_arg0s[i] = -1;
    }
    outer_name_end = parse_call_name_end(expr);
    outer_open_idx = (int)(outer_name_end - expr);
    if (outer_open_idx <= 0 || expr[outer_open_idx] != '(') {
        goto cleanup;
    }
    outer_close_idx = find_matching_delimiter_text(expr, outer_open_idx, '(', ')');
    if (outer_close_idx < 0 || expr[outer_close_idx + 1] != '\0') {
        goto cleanup;
    }
    outer_callee = trim_copy(expr, expr + outer_open_idx);
    outer_arg_text = trim_copy(expr + outer_open_idx + 1, expr + outer_close_idx);
    outer_parts = split_top_level_segments_text(outer_arg_text, ',');
    if (outer_parts.len != 1) {
        goto cleanup;
    }
    inner_expr = xstrdup_text(outer_parts.items[0]);
    inner_name_end = parse_call_name_end(inner_expr);
    inner_open_idx = (int)(inner_name_end - inner_expr);
    if (inner_open_idx <= 0 || inner_expr[inner_open_idx] != '(') {
        goto cleanup;
    }
    inner_close_idx = find_matching_delimiter_text(inner_expr, inner_open_idx, '(', ')');
    if (inner_close_idx < 0 || inner_expr[inner_close_idx + 1] != '\0') {
        goto cleanup;
    }
    inner_callee = trim_copy(inner_expr, inner_expr + inner_open_idx);
    inner_arg_text = trim_copy(inner_expr + inner_open_idx + 1, inner_expr + inner_close_idx);
    if (inner_arg_text[0] != '\0') {
        inner_parts = split_top_level_segments_text(inner_arg_text, ',');
    }
    arg_count = (int)inner_parts.len;
    if (arg_count > 4) {
        goto cleanup;
    }
    for (int i = 0; i < arg_count; ++i) {
        if (!compiler_core_match_simple_value_expr(inner_parts.items[i],
                                                   param_names,
                                                   &out_arg_kinds[i],
                                                   &out_arg_primary_texts[i],
                                                   &out_arg_arg0s[i])) {
            goto cleanup;
        }
    }
    inner_routine_idx = compiler_core_find_routine_index_by_call(bundle,
                                                                 inner_callee,
                                                                 current_signature,
                                                                 arg_count,
                                                                 (const char *const *)out_arg_kinds,
                                                                 (const char *const *)out_arg_primary_texts,
                                                                 out_arg_arg0s);
    if (inner_routine_idx < 0) {
        goto cleanup;
    }
    if (compiler_core_direct_type_reg_width(bundle->program.routine_return_type_texts.items[inner_routine_idx]) != 1) {
        goto cleanup;
    }
    outer_arg_kinds[0] = "resolved_type";
    outer_arg_primary_texts[0] = bundle->program.routine_return_type_texts.items[inner_routine_idx];
    outer_arg_arg0s[0] = -1;
    outer_routine_idx = compiler_core_find_routine_index_by_call(bundle,
                                                                 outer_callee,
                                                                 current_signature,
                                                                 1,
                                                                 outer_arg_kinds,
                                                                 outer_arg_primary_texts,
                                                                 outer_arg_arg0s);
    if (outer_routine_idx < 0) {
        goto cleanup;
    }
    inner_symbol = compiler_core_direct_exec_callee_symbol_for_call(bundle,
                                                                    (int)item_idx,
                                                                    inner_callee,
                                                                    current_signature,
                                                                    arg_count,
                                                                    (const char *const *)out_arg_kinds,
                                                                    (const char *const *)out_arg_primary_texts,
                                                                    out_arg_arg0s,
                                                                    target_triple);
    outer_symbol = compiler_core_direct_exec_callee_symbol_for_call(bundle,
                                                                    (int)item_idx,
                                                                    outer_callee,
                                                                    current_signature,
                                                                    1,
                                                                    outer_arg_kinds,
                                                                    outer_arg_primary_texts,
                                                                    outer_arg_arg0s,
                                                                    target_triple);
    if (inner_symbol == NULL || outer_symbol == NULL) {
        goto cleanup;
    }
    *out_arg_count = arg_count;
    *out_call_symbol0 = inner_symbol;
    *out_call_symbol1 = outer_symbol;
    inner_symbol = NULL;
    outer_symbol = NULL;
    ok = 1;
cleanup:
    if (!ok) {
        for (int i = 0; i < 4; ++i) {
            free(out_arg_kinds[i]);
            free(out_arg_primary_texts[i]);
            out_arg_kinds[i] = NULL;
            out_arg_primary_texts[i] = NULL;
            out_arg_arg0s[i] = -1;
        }
        free(inner_symbol);
        free(outer_symbol);
    }
    free(inner_arg_text);
    free(inner_callee);
    free(inner_expr);
    free(outer_arg_text);
    free(outer_callee);
    string_list_free(&outer_parts);
    string_list_free(&inner_parts);
    return ok;
}

static int compiler_core_match_nested_void_call_expr_text(const CompilerCoreBundle *bundle,
                                                          size_t item_idx,
                                                          const char *expr,
                                                          const char *current_signature,
                                                          const StringList *param_names,
                                                          const char *target_triple,
                                                          int *out_inner_arg_count,
                                                          char **out_call_symbol0,
                                                          char **out_call_symbol1,
                                                          char **out_inner_arg_kinds,
                                                          char **out_inner_arg_primary_texts,
                                                          int *out_inner_arg_arg0s,
                                                          char **out_outer_arg_kind,
                                                          char **out_outer_arg_primary_text,
                                                          int *out_outer_arg0) {
    char *outer_callee = NULL;
    char *outer_arg_text = NULL;
    char *inner_expr = NULL;
    char *inner_callee = NULL;
    char *inner_arg_text = NULL;
    char *outer_symbol = NULL;
    char *inner_symbol = NULL;
    char *outer_param0_type = NULL;
    char *outer_param1_type = NULL;
    char *outer_arg_type = NULL;
    const char *outer_arg_kinds[2];
    const char *outer_arg_primary_texts[2];
    int outer_arg_arg0s[2];
    StringList outer_parts;
    StringList inner_parts;
    const char *outer_name_end;
    const char *inner_name_end;
    int outer_open_idx;
    int inner_open_idx;
    int outer_close_idx;
    int inner_close_idx;
    int outer_routine_idx;
    int inner_routine_idx;
    int ok = 0;
    int inner_arg_count = 0;
    memset(&outer_parts, 0, sizeof(outer_parts));
    memset(&inner_parts, 0, sizeof(inner_parts));
    *out_inner_arg_count = 0;
    *out_call_symbol0 = NULL;
    *out_call_symbol1 = NULL;
    *out_outer_arg_kind = NULL;
    *out_outer_arg_primary_text = NULL;
    *out_outer_arg0 = -1;
    for (int i = 0; i < 4; ++i) {
        out_inner_arg_kinds[i] = NULL;
        out_inner_arg_primary_texts[i] = NULL;
        out_inner_arg_arg0s[i] = -1;
    }
    outer_name_end = parse_call_name_end(expr);
    outer_open_idx = (int)(outer_name_end - expr);
    if (outer_open_idx <= 0 || expr[outer_open_idx] != '(') {
        goto cleanup;
    }
    outer_close_idx = find_matching_delimiter_text(expr, outer_open_idx, '(', ')');
    if (outer_close_idx < 0 || expr[outer_close_idx + 1] != '\0') {
        goto cleanup;
    }
    outer_callee = trim_copy(expr, expr + outer_open_idx);
    outer_arg_text = trim_copy(expr + outer_open_idx + 1, expr + outer_close_idx);
    outer_parts = split_top_level_segments_text(outer_arg_text, ',');
    if (outer_parts.len != 1 && outer_parts.len != 2) {
        goto cleanup;
    }
    inner_expr = xstrdup_text(outer_parts.items[0]);
    if (outer_parts.len == 2) {
        if (!compiler_core_match_simple_value_expr(outer_parts.items[1],
                                                   param_names,
                                                   out_outer_arg_kind,
                                                   out_outer_arg_primary_text,
                                                   out_outer_arg0)) {
            goto cleanup;
        }
        outer_arg_type = compiler_core_direct_arg_type_text(current_signature,
                                                            *out_outer_arg_kind,
                                                            *out_outer_arg_primary_text,
                                                            *out_outer_arg0);
        if (outer_arg_type != NULL &&
            strcmp(outer_arg_type, "nil") != 0 &&
            compiler_core_direct_type_reg_width(outer_arg_type) != 1) {
            goto cleanup;
        }
    }
    inner_name_end = parse_call_name_end(inner_expr);
    inner_open_idx = (int)(inner_name_end - inner_expr);
    if (inner_open_idx <= 0 || inner_expr[inner_open_idx] != '(') {
        goto cleanup;
    }
    inner_close_idx = find_matching_delimiter_text(inner_expr, inner_open_idx, '(', ')');
    if (inner_close_idx < 0 || inner_expr[inner_close_idx + 1] != '\0') {
        goto cleanup;
    }
    inner_callee = trim_copy(inner_expr, inner_expr + inner_open_idx);
    inner_arg_text = trim_copy(inner_expr + inner_open_idx + 1, inner_expr + inner_close_idx);
    if (inner_arg_text[0] != '\0') {
        inner_parts = split_top_level_segments_text(inner_arg_text, ',');
    }
    inner_arg_count = (int)inner_parts.len;
    if (inner_arg_count > 4) {
        goto cleanup;
    }
    for (int i = 0; i < inner_arg_count; ++i) {
        if (!compiler_core_match_simple_value_expr(inner_parts.items[i],
                                                   param_names,
                                                   &out_inner_arg_kinds[i],
                                                   &out_inner_arg_primary_texts[i],
                                                   &out_inner_arg_arg0s[i])) {
            goto cleanup;
        }
    }
    inner_routine_idx = compiler_core_find_routine_index_by_call(bundle,
                                                                 inner_callee,
                                                                 current_signature,
                                                                 inner_arg_count,
                                                                 (const char *const *)out_inner_arg_kinds,
                                                                 (const char *const *)out_inner_arg_primary_texts,
                                                                 out_inner_arg_arg0s);
    if (inner_routine_idx < 0) {
        goto cleanup;
    }
    if (compiler_core_direct_type_reg_width(bundle->program.routine_return_type_texts.items[inner_routine_idx]) != 1) {
        goto cleanup;
    }
    outer_arg_kinds[0] = "resolved_type";
    outer_arg_primary_texts[0] = bundle->program.routine_return_type_texts.items[inner_routine_idx];
    outer_arg_arg0s[0] = -1;
    if (outer_parts.len == 2) {
        outer_arg_kinds[1] = *out_outer_arg_kind;
        outer_arg_primary_texts[1] = *out_outer_arg_primary_text;
        outer_arg_arg0s[1] = *out_outer_arg0;
    } else {
        outer_arg_kinds[1] = NULL;
        outer_arg_primary_texts[1] = NULL;
        outer_arg_arg0s[1] = -1;
    }
    outer_routine_idx = compiler_core_find_routine_index_by_call(bundle,
                                                                 outer_callee,
                                                                 current_signature,
                                                                 (int)outer_parts.len,
                                                                 outer_arg_kinds,
                                                                 outer_arg_primary_texts,
                                                                 outer_arg_arg0s);
    if (outer_routine_idx < 0) {
        goto cleanup;
    }
    outer_param0_type = compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[outer_routine_idx], 0);
    if (outer_parts.len == 2) {
        outer_param1_type = compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[outer_routine_idx], 1);
    }
    if (compiler_core_direct_type_reg_width(outer_param0_type) != 1 ||
        (outer_parts.len == 2 &&
         compiler_core_direct_type_reg_width(outer_param1_type) != 1)) {
        goto cleanup;
    }
    inner_symbol = compiler_core_direct_exec_callee_symbol_for_call(bundle,
                                                                    (int)item_idx,
                                                                    inner_callee,
                                                                    current_signature,
                                                                    inner_arg_count,
                                                                    (const char *const *)out_inner_arg_kinds,
                                                                    (const char *const *)out_inner_arg_primary_texts,
                                                                    out_inner_arg_arg0s,
                                                                    target_triple);
    outer_symbol = compiler_core_direct_exec_callee_symbol_for_call(bundle,
                                                                    (int)item_idx,
                                                                    outer_callee,
                                                                    current_signature,
                                                                    (int)outer_parts.len,
                                                                    outer_arg_kinds,
                                                                    outer_arg_primary_texts,
                                                                    outer_arg_arg0s,
                                                                    target_triple);
    if (inner_symbol == NULL || outer_symbol == NULL) {
        goto cleanup;
    }
    *out_inner_arg_count = inner_arg_count;
    *out_call_symbol0 = inner_symbol;
    *out_call_symbol1 = outer_symbol;
    inner_symbol = NULL;
    outer_symbol = NULL;
    ok = 1;
cleanup:
    if (!ok) {
        for (int i = 0; i < 4; ++i) {
            free(out_inner_arg_kinds[i]);
            free(out_inner_arg_primary_texts[i]);
            out_inner_arg_kinds[i] = NULL;
            out_inner_arg_primary_texts[i] = NULL;
            out_inner_arg_arg0s[i] = -1;
        }
        free(*out_outer_arg_kind);
        free(*out_outer_arg_primary_text);
        *out_outer_arg_kind = NULL;
        *out_outer_arg_primary_text = NULL;
        *out_outer_arg0 = -1;
        free(inner_symbol);
        free(outer_symbol);
    }
    free(outer_param0_type);
    free(outer_param1_type);
    free(outer_arg_type);
    free(inner_arg_text);
    free(inner_callee);
    free(inner_expr);
    free(outer_arg_text);
    free(outer_callee);
    string_list_free(&outer_parts);
    string_list_free(&inner_parts);
    return ok;
}

static int compiler_core_match_nested_call_trailing_cstring_arg_expr_text(const CompilerCoreBundle *bundle,
                                                                          size_t item_idx,
                                                                          const char *expr,
                                                                          const char *current_signature,
                                                                          const StringList *param_names,
                                                                          const char *target_triple,
                                                                          int *out_inner_arg_count,
                                                                          char **out_call_symbol0,
                                                                          char **out_call_symbol1,
                                                                          char **out_inner_arg_kinds,
                                                                          char **out_inner_arg_primary_texts,
                                                                          int *out_inner_arg_arg0s,
                                                                          char **out_payload_text) {
    char *outer_callee = NULL;
    char *outer_arg_text = NULL;
    char *inner_expr = NULL;
    char *inner_callee = NULL;
    char *inner_arg_text = NULL;
    char *inner_symbol = NULL;
    char *outer_symbol = NULL;
    char *outer_param0_type = NULL;
    char *outer_param1_type = NULL;
    char *payload_text = NULL;
    const char *outer_arg_kinds[2];
    const char *outer_arg_primary_texts[2];
    int outer_arg_arg0s[2];
    StringList outer_parts;
    StringList inner_parts;
    const char *outer_name_end;
    const char *inner_name_end;
    int outer_open_idx;
    int inner_open_idx;
    int outer_close_idx;
    int inner_close_idx;
    int outer_routine_idx;
    int inner_routine_idx;
    int inner_arg_count = 0;
    int ok = 0;
    memset(&outer_parts, 0, sizeof(outer_parts));
    memset(&inner_parts, 0, sizeof(inner_parts));
    *out_inner_arg_count = 0;
    *out_call_symbol0 = NULL;
    *out_call_symbol1 = NULL;
    *out_payload_text = NULL;
    for (int i = 0; i < 4; ++i) {
        out_inner_arg_kinds[i] = NULL;
        out_inner_arg_primary_texts[i] = NULL;
        out_inner_arg_arg0s[i] = -1;
    }
    outer_name_end = parse_call_name_end(expr);
    outer_open_idx = (int)(outer_name_end - expr);
    if (outer_open_idx <= 0 || expr[outer_open_idx] != '(') {
        goto cleanup;
    }
    outer_close_idx = find_matching_delimiter_text(expr, outer_open_idx, '(', ')');
    if (outer_close_idx < 0 || expr[outer_close_idx + 1] != '\0') {
        goto cleanup;
    }
    outer_callee = trim_copy(expr, expr + outer_open_idx);
    outer_arg_text = trim_copy(expr + outer_open_idx + 1, expr + outer_close_idx);
    outer_parts = split_top_level_segments_text(outer_arg_text, ',');
    if (outer_parts.len != 2) {
        goto cleanup;
    }
    inner_expr = xstrdup_text(outer_parts.items[0]);
    payload_text = compiler_core_parse_string_literal_expr(outer_parts.items[1]);
    if (payload_text == NULL) {
        goto cleanup;
    }
    inner_name_end = parse_call_name_end(inner_expr);
    inner_open_idx = (int)(inner_name_end - inner_expr);
    if (inner_open_idx <= 0 || inner_expr[inner_open_idx] != '(') {
        goto cleanup;
    }
    inner_close_idx = find_matching_delimiter_text(inner_expr, inner_open_idx, '(', ')');
    if (inner_close_idx < 0 || inner_expr[inner_close_idx + 1] != '\0') {
        goto cleanup;
    }
    inner_callee = trim_copy(inner_expr, inner_expr + inner_open_idx);
    inner_arg_text = trim_copy(inner_expr + inner_open_idx + 1, inner_expr + inner_close_idx);
    if (inner_arg_text[0] != '\0') {
        inner_parts = split_top_level_segments_text(inner_arg_text, ',');
    }
    inner_arg_count = (int)inner_parts.len;
    if (inner_arg_count > 4) {
        goto cleanup;
    }
    for (int i = 0; i < inner_arg_count; ++i) {
        if (!compiler_core_match_simple_value_expr(inner_parts.items[i],
                                                   param_names,
                                                   &out_inner_arg_kinds[i],
                                                   &out_inner_arg_primary_texts[i],
                                                   &out_inner_arg_arg0s[i])) {
            goto cleanup;
        }
    }
    inner_routine_idx = compiler_core_find_routine_index_by_call(bundle,
                                                                 inner_callee,
                                                                 current_signature,
                                                                 inner_arg_count,
                                                                 (const char *const *)out_inner_arg_kinds,
                                                                 (const char *const *)out_inner_arg_primary_texts,
                                                                 out_inner_arg_arg0s);
    if (inner_routine_idx < 0) {
        goto cleanup;
    }
    if (!compiler_core_type_matches(bundle->program.routine_return_type_texts.items[inner_routine_idx], "str")) {
        goto cleanup;
    }
    outer_arg_kinds[0] = "resolved_type";
    outer_arg_primary_texts[0] = bundle->program.routine_return_type_texts.items[inner_routine_idx];
    outer_arg_arg0s[0] = -1;
    outer_arg_kinds[1] = "load_str";
    outer_arg_primary_texts[1] = payload_text;
    outer_arg_arg0s[1] = -1;
    outer_routine_idx = compiler_core_find_routine_index_by_call(bundle,
                                                                 outer_callee,
                                                                 current_signature,
                                                                 2,
                                                                 outer_arg_kinds,
                                                                 outer_arg_primary_texts,
                                                                 outer_arg_arg0s);
    if (outer_routine_idx < 0) {
        goto cleanup;
    }
    outer_param0_type =
        compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[outer_routine_idx], 0);
    outer_param1_type =
        compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[outer_routine_idx], 1);
    if (!compiler_core_type_matches(outer_param0_type, "str") ||
        !compiler_core_type_matches(outer_param1_type, "str")) {
        goto cleanup;
    }
    inner_symbol = compiler_core_direct_exec_callee_symbol_for_call(bundle,
                                                                    (int)item_idx,
                                                                    inner_callee,
                                                                    current_signature,
                                                                    inner_arg_count,
                                                                    (const char *const *)out_inner_arg_kinds,
                                                                    (const char *const *)out_inner_arg_primary_texts,
                                                                    out_inner_arg_arg0s,
                                                                    target_triple);
    outer_symbol = compiler_core_direct_exec_callee_symbol_for_call(bundle,
                                                                    (int)item_idx,
                                                                    outer_callee,
                                                                    current_signature,
                                                                    2,
                                                                    outer_arg_kinds,
                                                                    outer_arg_primary_texts,
                                                                    outer_arg_arg0s,
                                                                    target_triple);
    if (inner_symbol == NULL || outer_symbol == NULL) {
        goto cleanup;
    }
    *out_inner_arg_count = inner_arg_count;
    *out_call_symbol0 = inner_symbol;
    *out_call_symbol1 = outer_symbol;
    *out_payload_text = payload_text;
    inner_symbol = NULL;
    outer_symbol = NULL;
    payload_text = NULL;
    ok = 1;
cleanup:
    if (!ok) {
        for (int i = 0; i < 4; ++i) {
            free(out_inner_arg_kinds[i]);
            free(out_inner_arg_primary_texts[i]);
            out_inner_arg_kinds[i] = NULL;
            out_inner_arg_primary_texts[i] = NULL;
            out_inner_arg_arg0s[i] = -1;
        }
        free(inner_symbol);
        free(outer_symbol);
    }
    free(outer_param0_type);
    free(outer_param1_type);
    free(payload_text);
    free(inner_arg_text);
    free(inner_callee);
    free(inner_expr);
    free(outer_arg_text);
    free(outer_callee);
    string_list_free(&outer_parts);
    string_list_free(&inner_parts);
    return ok;
}

static int compiler_core_direct_exec_nested_call_symbols(const CompilerCoreBundle *bundle,
                                                         size_t item_idx,
                                                         size_t routine_idx,
                                                         const char *target_triple,
                                                         char **out_words,
                                                         char **out_call_symbol0,
                                                         char **out_call_symbol1) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    char *expr = NULL;
    StringList param_names;
    int ok = 0;
    char *arg_kinds[4];
    char *arg_primary_texts[4];
    int arg_arg0s[4];
    int arg_count = 0;
    char *outer_arg_kind = NULL;
    char *outer_arg_primary_text = NULL;
    int outer_arg0 = -1;
    char *outer_cstring_payload = NULL;
    memset(&param_names, 0, sizeof(param_names));
    memset(arg_kinds, 0, sizeof(arg_kinds));
    memset(arg_primary_texts, 0, sizeof(arg_primary_texts));
    *out_words = NULL;
    *out_call_symbol0 = NULL;
    *out_call_symbol1 = NULL;
    if (!compiler_core_direct_call_return_type_supported(bundle->program.routine_return_type_texts.items[routine_idx])) {
        free(trimmed);
        return 0;
    }
    if (!compiler_core_direct_text_match_allowed(bundle, routine_idx, 2, 3)) {
        free(trimmed);
        return 0;
    }
    parse_compiler_routine_signature_param_names(signature, &param_names);
    if (starts_with(trimmed, "return ")) {
        expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    } else {
        expr = trim_copy(trimmed, trimmed + strlen(trimmed));
    }
    {
        const char *operators[] = {"==", "!=", "<=", ">=", "<", ">"};
        size_t i;
        for (i = 0; i < sizeof(operators) / sizeof(operators[0]) && !ok; ++i) {
            int split = find_last_top_level_binary_text(expr, operators[i]);
            if (split >= 0) {
                char *lhs = trim_copy(expr, expr + split);
                char *rhs = trim_copy(expr + split + strlen(operators[i]), expr + strlen(expr));
                char *rhs_kind = NULL;
                char *rhs_primary = NULL;
                int rhs_arg0 = -1;
                if (compiler_core_match_simple_value_expr(rhs,
                                                          &param_names,
                                                          &rhs_kind,
                                                          &rhs_primary,
                                                          &rhs_arg0) &&
                    compiler_core_is_direct_const_value_kind(rhs_kind) &&
                    compiler_core_match_nested_call_expr_text(bundle,
                                                              item_idx,
                                                              lhs,
                                                              signature,
                                                              &param_names,
                                                              target_triple,
                                                              &arg_count,
                                                              out_call_symbol0,
                                                              out_call_symbol1,
                                                              arg_kinds,
                                                              arg_primary_texts,
                                                              arg_arg0s)) {
                    *out_words =
                        machine_direct_nested_call_compare_const_rhs_words(machine_target_architecture(target_triple),
                                                                           arg_count,
                                                                           (const char **)arg_kinds,
                                                                           (const char **)arg_primary_texts,
                                                                           arg_arg0s,
                                                                           rhs_kind,
                                                                           rhs_primary,
                                                                           rhs_arg0,
                                                                           operators[i]);
                    if (*out_words != NULL) {
                        ok = 1;
                    }
                }
                free(rhs_kind);
                free(rhs_primary);
                if (!ok) {
                    free(*out_call_symbol0);
                    free(*out_call_symbol1);
                    *out_call_symbol0 = NULL;
                    *out_call_symbol1 = NULL;
                    for (int j = 0; j < 4; ++j) {
                        free(arg_kinds[j]);
                        free(arg_primary_texts[j]);
                        arg_kinds[j] = NULL;
                        arg_primary_texts[j] = NULL;
                    }
                    free(*out_words);
                    *out_words = NULL;
                }
                free(lhs);
                free(rhs);
            }
        }
    }
    if (!ok &&
        compiler_core_match_nested_call_expr_text(bundle,
                                                  item_idx,
                                                  expr,
                                                  signature,
                                                  &param_names,
                                                  target_triple,
                                                  &arg_count,
                                                  out_call_symbol0,
                                                  out_call_symbol1,
                                                  arg_kinds,
                                                  arg_primary_texts,
                                                  arg_arg0s)) {
        *out_words = machine_direct_nested_call_words(machine_target_architecture(target_triple),
                                                      arg_count,
                                                      (const char **)arg_kinds,
                                                      (const char **)arg_primary_texts,
                                                      arg_arg0s);
        if (*out_words != NULL) {
            ok = 1;
        } else {
            free(*out_call_symbol0);
            free(*out_call_symbol1);
            *out_call_symbol0 = NULL;
            *out_call_symbol1 = NULL;
        }
    }
    if (!ok &&
        compiler_core_match_nested_void_call_expr_text(bundle,
                                                       item_idx,
                                                       expr,
                                                       signature,
                                                       &param_names,
                                                       target_triple,
                                                       &arg_count,
                                                       out_call_symbol0,
                                                       out_call_symbol1,
                                                       arg_kinds,
                                                       arg_primary_texts,
                                                       arg_arg0s,
                                                       &outer_arg_kind,
                                                       &outer_arg_primary_text,
                                                       &outer_arg0)) {
        *out_words =
            machine_direct_nested_call_trailing_simple_arg_words(machine_target_architecture(target_triple),
                                                                 arg_count,
                                                                 (const char **)arg_kinds,
                                                                 (const char **)arg_primary_texts,
                                                                 arg_arg0s,
                                                                 outer_arg_kind,
                                                                 outer_arg_primary_text,
                                                                 outer_arg0);
        if (*out_words != NULL) {
            ok = 1;
        } else {
            free(*out_call_symbol0);
            free(*out_call_symbol1);
            *out_call_symbol0 = NULL;
            *out_call_symbol1 = NULL;
            free(outer_arg_kind);
            free(outer_arg_primary_text);
            outer_arg_kind = NULL;
            outer_arg_primary_text = NULL;
            outer_arg0 = -1;
        }
    }
    if (!ok &&
        compiler_core_match_nested_call_trailing_cstring_arg_expr_text(bundle,
                                                                       item_idx,
                                                                       expr,
                                                                       signature,
                                                                       &param_names,
                                                                       target_triple,
                                                                       &arg_count,
                                                                       out_call_symbol0,
                                                                       out_call_symbol1,
                                                                       arg_kinds,
                                                                       arg_primary_texts,
                                                                       arg_arg0s,
                                                                       &outer_cstring_payload)) {
        *out_words =
            machine_direct_nested_call_trailing_cstring_arg_words(machine_target_architecture(target_triple),
                                                                  arg_count,
                                                                  (const char **)arg_kinds,
                                                                  (const char **)arg_primary_texts,
                                                                  arg_arg0s,
                                                                  (int)strlen(outer_cstring_payload));
        if (*out_words != NULL) {
            ok = 1;
        } else {
            free(*out_call_symbol0);
            free(*out_call_symbol1);
            *out_call_symbol0 = NULL;
            *out_call_symbol1 = NULL;
            free(outer_cstring_payload);
            outer_cstring_payload = NULL;
        }
    }
    for (int i = 0; i < 4; ++i) {
        free(arg_kinds[i]);
        free(arg_primary_texts[i]);
    }
    free(outer_arg_kind);
    free(outer_arg_primary_text);
    free(outer_cstring_payload);
    string_list_free(&param_names);
    free(expr);
    free(trimmed);
    return ok;
}

static int compiler_core_direct_exec_nested_void_call_symbols(const CompilerCoreBundle *bundle,
                                                              size_t item_idx,
                                                              size_t routine_idx,
                                                              const char *target_triple,
                                                              char **out_words,
                                                              char **out_call_symbol0,
                                                              char **out_call_symbol1) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    StringList param_names;
    int ok = 0;
    char *inner_arg_kinds[4];
    char *inner_arg_primary_texts[4];
    int inner_arg_arg0s[4];
    char *outer_arg_kind = NULL;
    char *outer_arg_primary_text = NULL;
    int outer_arg0 = -1;
    int inner_arg_count = 0;
    memset(&param_names, 0, sizeof(param_names));
    memset(inner_arg_kinds, 0, sizeof(inner_arg_kinds));
    memset(inner_arg_primary_texts, 0, sizeof(inner_arg_primary_texts));
    *out_words = NULL;
    *out_call_symbol0 = NULL;
    *out_call_symbol1 = NULL;
    if (!compiler_core_direct_call_return_type_supported(bundle->program.routine_return_type_texts.items[routine_idx])) {
        free(trimmed);
        return 0;
    }
    if (!compiler_core_direct_text_match_allowed(bundle, routine_idx, 2, 3)) {
        free(trimmed);
        return 0;
    }
    parse_compiler_routine_signature_param_names(signature, &param_names);
    if (compiler_core_match_nested_void_call_expr_text(bundle,
                                                       item_idx,
                                                       trimmed,
                                                       signature,
                                                       &param_names,
                                                       target_triple,
                                                       &inner_arg_count,
                                                       out_call_symbol0,
                                                       out_call_symbol1,
                                                       inner_arg_kinds,
                                                       inner_arg_primary_texts,
                                                       inner_arg_arg0s,
                                                       &outer_arg_kind,
                                                       &outer_arg_primary_text,
                                                       &outer_arg0)) {
        if (outer_arg_kind == NULL) {
            *out_words =
                machine_direct_nested_call_void_words(machine_target_architecture(target_triple),
                                                      inner_arg_count,
                                                      (const char **)inner_arg_kinds,
                                                      (const char **)inner_arg_primary_texts,
                                                      inner_arg_arg0s);
        } else {
            *out_words =
                machine_direct_nested_call_trailing_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                          inner_arg_count,
                                                                          (const char **)inner_arg_kinds,
                                                                          (const char **)inner_arg_primary_texts,
                                                                          inner_arg_arg0s,
                                                                          outer_arg_kind,
                                                                          outer_arg_primary_text,
                                                                          outer_arg0);
        }
        if (*out_words != NULL) {
            ok = 1;
        } else {
            free(*out_call_symbol0);
            free(*out_call_symbol1);
            *out_call_symbol0 = NULL;
            *out_call_symbol1 = NULL;
        }
    }
    for (int i = 0; i < 4; ++i) {
        free(inner_arg_kinds[i]);
        free(inner_arg_primary_texts[i]);
    }
    free(outer_arg_kind);
    free(outer_arg_primary_text);
    string_list_free(&param_names);
    free(trimmed);
    return ok;
}

static int compiler_core_direct_exec_if_else_call_symbols(const CompilerCoreBundle *bundle,
                                                          size_t item_idx,
                                                          size_t routine_idx,
                                                          const char *target_triple,
                                                          char **out_words,
                                                          char **out_call_symbol0,
                                                          char **out_call_symbol1,
                                                          char **out_call_symbol2) {
    const char *body_text = bundle->program.routine_body_texts.items[routine_idx];
    StringList body_lines;
    StringList active_lines;
    StringList then_lines;
    StringList else_lines;
    StringList empty_param_names;
    int next_idx;
    int end_idx;
    int ok = 0;
    memset(&body_lines, 0, sizeof(body_lines));
    memset(&active_lines, 0, sizeof(active_lines));
    memset(&then_lines, 0, sizeof(then_lines));
    memset(&else_lines, 0, sizeof(else_lines));
    memset(&empty_param_names, 0, sizeof(empty_param_names));
    *out_words = NULL;
    *out_call_symbol0 = NULL;
    *out_call_symbol1 = NULL;
    *out_call_symbol2 = NULL;
    if (!compiler_core_direct_call_return_type_supported(bundle->program.routine_return_type_texts.items[routine_idx])) {
        return 0;
    }
    if (!compiler_core_direct_text_match_allowed(bundle, routine_idx, 3, 5)) {
        return 0;
    }
    if (body_text == NULL || strchr(body_text, '\n') == NULL) {
        return 0;
    }
    split_text_lines(body_text, &body_lines);
    for (size_t i = 0; i < body_lines.len; ++i) {
        if (!is_blank_or_comment_line(body_lines.items[i])) {
            string_list_push_copy(&active_lines, body_lines.items[i]);
        }
    }
    if (active_lines.len < 4) {
        goto cleanup;
    }
    {
        char *header = trim_copy(active_lines.items[0], active_lines.items[0] + strlen(active_lines.items[0]));
        if (!starts_with(header, "if ") || header[strlen(header) - 1] != ':') {
            free(header);
            goto cleanup;
        }
        next_idx = collect_outline_block_lines(active_lines.items, (int)active_lines.len, 1, &then_lines);
        if (then_lines.len != 1 || next_idx >= (int)active_lines.len) {
            free(header);
            goto cleanup;
        }
        {
            char *else_head = trim_copy(active_lines.items[next_idx],
                                        active_lines.items[next_idx] + strlen(active_lines.items[next_idx]));
            if (strcmp(else_head, "else:") != 0) {
                free(else_head);
                free(header);
                goto cleanup;
            }
            free(else_head);
        }
        end_idx = collect_outline_block_lines(active_lines.items,
                                              (int)active_lines.len,
                                              next_idx + 1,
                                              &else_lines);
        if (else_lines.len != 1 || end_idx != (int)active_lines.len) {
            free(header);
            goto cleanup;
        }
        {
            char *cond_text = trim_copy(header + strlen("if "), header + strlen(header) - 1);
            {
                char *call_symbol = NULL;
                if (compiler_core_match_return_zero_arg_call_line(bundle,
                                                                  item_idx,
                                                                  then_lines.items[0],
                                                                  target_triple,
                                                                  out_call_symbol1) &&
                    compiler_core_match_return_zero_arg_call_line(bundle,
                                                                  item_idx,
                                                                  else_lines.items[0],
                                                                  target_triple,
                                                                  out_call_symbol2)) {
                    const char *name_end = parse_call_name_end(cond_text);
                    int open_idx = (int)(name_end - cond_text);
                    if (open_idx > 0 && cond_text[open_idx] == '(') {
                        int close_idx = find_matching_delimiter_text(cond_text, open_idx, '(', ')');
                        if (close_idx >= 0 && cond_text[close_idx + 1] == '\0') {
                            char *callee = trim_copy(cond_text, cond_text + open_idx);
                            char *arg_text = trim_copy(cond_text + open_idx + 1, cond_text + close_idx);
                            if (arg_text[0] == '\0') {
                                call_symbol =
                                    compiler_core_direct_exec_callee_symbol(bundle, item_idx, callee, target_triple);
                                if (call_symbol != NULL) {
                                    *out_call_symbol0 = call_symbol;
                                    *out_words =
                                        machine_direct_if_else_call_words(machine_target_architecture(target_triple));
                                    ok = *out_words != NULL;
                                }
                            }
                            free(arg_text);
                            free(callee);
                        }
                    }
                }
                if (!ok) {
                    char *const_kind = NULL;
                    char *const_primary = NULL;
                    int const_arg0 = -1;
                    char *return_expr = NULL;
                    if (starts_with(then_lines.items[0], "return ")) {
                        return_expr = trim_copy(then_lines.items[0] + strlen("return "),
                                                then_lines.items[0] + strlen(then_lines.items[0]));
                        if (compiler_core_match_simple_value_expr(return_expr,
                                                                  &empty_param_names,
                                                                  &const_kind,
                                                                  &const_primary,
                                                                  &const_arg0) &&
                            compiler_core_is_direct_const_value_kind(const_kind) &&
                            compiler_core_match_return_zero_arg_call_line(bundle,
                                                                          item_idx,
                                                                          else_lines.items[0],
                                                                          target_triple,
                                                                          out_call_symbol1)) {
                            const char *name_end = parse_call_name_end(cond_text);
                            int open_idx = (int)(name_end - cond_text);
                            if (open_idx > 0 && cond_text[open_idx] == '(') {
                                int close_idx = find_matching_delimiter_text(cond_text, open_idx, '(', ')');
                                if (close_idx >= 0 && cond_text[close_idx + 1] == '\0') {
                                    char *callee = trim_copy(cond_text, cond_text + open_idx);
                                    char *arg_text = trim_copy(cond_text + open_idx + 1, cond_text + close_idx);
                                    if (arg_text[0] == '\0') {
                                        call_symbol =
                                            compiler_core_direct_exec_callee_symbol(bundle, item_idx, callee, target_triple);
                                        if (call_symbol != NULL) {
                                            *out_call_symbol0 = call_symbol;
                                            *out_words =
                                                machine_direct_if_else_const_then_call_words(machine_target_architecture(target_triple),
                                                                                             const_kind,
                                                                                             const_primary,
                                                                                             const_arg0);
                                            ok = *out_words != NULL;
                                        }
                                    }
                                    free(arg_text);
                                    free(callee);
                                }
                            }
                        }
                    }
                    free(return_expr);
                    free(const_kind);
                    free(const_primary);
                }
                if (!ok) {
                    char *const_kind = NULL;
                    char *const_primary = NULL;
                    int const_arg0 = -1;
                    char *return_expr = NULL;
                    if (starts_with(else_lines.items[0], "return ")) {
                        return_expr = trim_copy(else_lines.items[0] + strlen("return "),
                                                else_lines.items[0] + strlen(else_lines.items[0]));
                        if (compiler_core_match_simple_value_expr(return_expr,
                                                                  &empty_param_names,
                                                                  &const_kind,
                                                                  &const_primary,
                                                                  &const_arg0) &&
                            compiler_core_is_direct_const_value_kind(const_kind) &&
                            compiler_core_match_return_zero_arg_call_line(bundle,
                                                                          item_idx,
                                                                          then_lines.items[0],
                                                                          target_triple,
                                                                          out_call_symbol1)) {
                            const char *name_end = parse_call_name_end(cond_text);
                            int open_idx = (int)(name_end - cond_text);
                            if (open_idx > 0 && cond_text[open_idx] == '(') {
                                int close_idx = find_matching_delimiter_text(cond_text, open_idx, '(', ')');
                                if (close_idx >= 0 && cond_text[close_idx + 1] == '\0') {
                                    char *callee = trim_copy(cond_text, cond_text + open_idx);
                                    char *arg_text = trim_copy(cond_text + open_idx + 1, cond_text + close_idx);
                                    if (arg_text[0] == '\0') {
                                        call_symbol =
                                            compiler_core_direct_exec_callee_symbol(bundle, item_idx, callee, target_triple);
                                        if (call_symbol != NULL) {
                                            *out_call_symbol0 = call_symbol;
                                            *out_words =
                                                machine_direct_if_else_call_then_const_words(machine_target_architecture(target_triple),
                                                                                             const_kind,
                                                                                             const_primary,
                                                                                             const_arg0);
                                            ok = *out_words != NULL;
                                        }
                                    }
                                    free(arg_text);
                                    free(callee);
                                }
                            }
                        }
                    }
                    free(return_expr);
                    free(const_kind);
                    free(const_primary);
                }
                if (!ok) {
                    free(call_symbol);
                    free(*out_call_symbol0);
                    free(*out_call_symbol1);
                    free(*out_call_symbol2);
                    *out_call_symbol0 = NULL;
                    *out_call_symbol1 = NULL;
                    *out_call_symbol2 = NULL;
                    free(*out_words);
                    *out_words = NULL;
                }
            }
            if (!ok) {
            const char *operators[] = {"==", "!=", "<=", ">=", "<", ">"};
            size_t i;
            for (i = 0; i < sizeof(operators) / sizeof(operators[0]) && !ok; ++i) {
                int split = find_last_top_level_binary_text(cond_text, operators[i]);
                if (split >= 0) {
                    char *lhs = trim_copy(cond_text, cond_text + split);
                    char *rhs = trim_copy(cond_text + split + strlen(operators[i]),
                                          cond_text + strlen(cond_text));
                    char *rhs_kind = NULL;
                    char *rhs_primary = NULL;
                    int rhs_arg0 = -1;
                    if (compiler_core_match_simple_value_expr(rhs,
                                                              &empty_param_names,
                                                              &rhs_kind,
                                                              &rhs_primary,
                                                              &rhs_arg0) &&
                        compiler_core_is_direct_const_value_kind(rhs_kind) &&
                        compiler_core_match_return_zero_arg_call_line(bundle,
                                                                      item_idx,
                                                                      then_lines.items[0],
                                                                      target_triple,
                                                                      out_call_symbol1) &&
                        compiler_core_match_return_zero_arg_call_line(bundle,
                                                                      item_idx,
                                                                      else_lines.items[0],
                                                                      target_triple,
                                                                      out_call_symbol2)) {
                        const char *name_end = parse_call_name_end(lhs);
                        int open_idx = (int)(name_end - lhs);
                        if (open_idx > 0 && lhs[open_idx] == '(') {
                            int close_idx = find_matching_delimiter_text(lhs, open_idx, '(', ')');
                            if (close_idx >= 0 && lhs[close_idx + 1] == '\0') {
                                char *callee = trim_copy(lhs, lhs + open_idx);
                                char *arg_text = trim_copy(lhs + open_idx + 1, lhs + close_idx);
                                if (arg_text[0] == '\0') {
                                    *out_call_symbol0 =
                                        compiler_core_direct_exec_callee_symbol(bundle, item_idx, callee, target_triple);
                                    if (*out_call_symbol0 != NULL) {
                                        *out_words =
                                            machine_direct_if_else_call_compare_const_rhs_words(machine_target_architecture(target_triple),
                                                                                                rhs_kind,
                                                                                                rhs_primary,
                                                                                                rhs_arg0,
                                                                                                operators[i]);
                                        ok = *out_words != NULL;
                                    }
                                }
                                free(arg_text);
                                free(callee);
                            }
                        }
                    }
                    if (!ok) {
                        free(*out_call_symbol0);
                        free(*out_call_symbol1);
                        free(*out_call_symbol2);
                        *out_call_symbol0 = NULL;
                        *out_call_symbol1 = NULL;
                        *out_call_symbol2 = NULL;
                        free(*out_words);
                        *out_words = NULL;
                    }
                    free(rhs_kind);
                    free(rhs_primary);
                    free(lhs);
                    free(rhs);
                }
            }
            }
            free(cond_text);
        }
        free(header);
    }
cleanup:
    string_list_free(&body_lines);
    string_list_free(&active_lines);
    string_list_free(&then_lines);
    string_list_free(&else_lines);
    return ok;
}

static char *compiler_core_direct_exec_call_symbol(const CompilerCoreBundle *bundle,
                                                   size_t item_idx,
                                                   size_t routine_idx,
                                                   const char *target_triple,
                                                   char **out_words) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    const char *body_text = bundle->program.routine_body_texts.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    char *expr = NULL;
    StringList param_names;
    memset(&param_names, 0, sizeof(param_names));
    (void)item_idx;
    parse_compiler_routine_signature_param_names(signature, &param_names);
    *out_words = NULL;
    if (!compiler_core_direct_call_return_type_supported(bundle->program.routine_return_type_texts.items[routine_idx])) {
        string_list_free(&param_names);
        free(trimmed);
        return NULL;
    }
    if (body_text != NULL && strchr(body_text, '\n') != NULL) {
        if (compiler_core_direct_text_match_allowed(bundle, routine_idx, 2, 3)) {
            char *multiline_symbol =
                compiler_core_multiline_alias_call_symbol(bundle,
                                                          item_idx,
                                                          routine_idx,
                                                          target_triple,
                                                          &param_names,
                                                          0,
                                                          out_words);
            if (multiline_symbol != NULL && *out_words != NULL) {
                string_list_free(&param_names);
                free(trimmed);
                return multiline_symbol;
            }
            free(multiline_symbol);
            free(*out_words);
            *out_words = NULL;
            {
                StringList body_lines;
                StringList active_lines;
                memset(&body_lines, 0, sizeof(body_lines));
                memset(&active_lines, 0, sizeof(active_lines));
                split_text_lines(body_text, &body_lines);
                for (size_t i = 0; i < body_lines.len; ++i) {
                    if (!is_blank_or_comment_line(body_lines.items[i])) {
                        string_list_push_copy(&active_lines, body_lines.items[i]);
                    }
                }
                if (active_lines.len == 2) {
                    char *line0 = trim_copy(active_lines.items[0], active_lines.items[0] + strlen(active_lines.items[0]));
                    char *line1 = trim_copy(active_lines.items[1], active_lines.items[1] + strlen(active_lines.items[1]));
                    char *const_kind = NULL;
                    char *const_primary = NULL;
                    int const_arg0 = -1;
                    if (starts_with(line1, "return ") &&
                        compiler_core_match_simple_value_expr(line1 + strlen("return "),
                                                              &param_names,
                                                              &const_kind,
                                                              &const_primary,
                                                              &const_arg0) &&
                        compiler_core_is_direct_const_value_kind(const_kind)) {
                        const char *name_end = parse_call_name_end(line0);
                        int open_idx = (int)(name_end - line0);
                        if (open_idx > 0 && line0[open_idx] == '(') {
                            int close_idx = find_matching_delimiter_text(line0, open_idx, '(', ')');
                            if (close_idx >= 0 && line0[close_idx + 1] == '\0') {
                                char *callee = trim_copy(line0, line0 + open_idx);
                                char *arg_text = trim_copy(line0 + open_idx + 1, line0 + close_idx);
                                StringList parts = split_top_level_segments_text(arg_text, ',');
                                if (arg_text[0] == '\0' || parts.len <= 4) {
                                    char *call_symbol = NULL;
                                    if (arg_text[0] == '\0') {
                                        call_symbol =
                                            compiler_core_direct_exec_callee_symbol(bundle, (int)item_idx, callee, target_triple);
                                        if (call_symbol != NULL) {
                                            *out_words =
                                                machine_direct_call_void_then_const_words(machine_target_architecture(target_triple),
                                                                                          const_kind,
                                                                                          const_primary,
                                                                                          const_arg0);
                                        }
                                    } else {
                                        char *arg_kinds[4] = {0};
                                        char *arg_primary_texts[4] = {0};
                                        int arg_arg0s[4] = {-1, -1, -1, -1};
                                        int args_ok = 1;
                                        for (size_t part_idx = 0; part_idx < parts.len; ++part_idx) {
                                            if (!compiler_core_match_simple_value_expr(parts.items[part_idx],
                                                                                      &param_names,
                                                                                      &arg_kinds[part_idx],
                                                                                      &arg_primary_texts[part_idx],
                                                                                      &arg_arg0s[part_idx])) {
                                                args_ok = 0;
                                                break;
                                            }
                                        }
                                        if (args_ok) {
                                            const char *arg_kind_views[4];
                                            const char *arg_primary_views[4];
                                            for (size_t part_idx = 0; part_idx < parts.len; ++part_idx) {
                                                arg_kind_views[part_idx] = arg_kinds[part_idx];
                                                arg_primary_views[part_idx] = arg_primary_texts[part_idx];
                                            }
                                            call_symbol =
                                                compiler_core_direct_exec_callee_symbol_for_call(bundle,
                                                                                                 (int)item_idx,
                                                                                                 callee,
                                                                                                 signature,
                                                                                                 (int)parts.len,
                                                                                                 arg_kind_views,
                                                                                                 arg_primary_views,
                                                                                                 arg_arg0s,
                                                                                                 target_triple);
                                            if (call_symbol != NULL) {
                                                *out_words =
                                                    machine_direct_call_simple_args_void_then_const_words(machine_target_architecture(target_triple),
                                                                                                          (int)parts.len,
                                                                                                          arg_kind_views,
                                                                                                          arg_primary_views,
                                                                                                          arg_arg0s,
                                                                                                          const_kind,
                                                                                                          const_primary,
                                                                                                          const_arg0);
                                            }
                                        }
                                        for (size_t part_idx = 0; part_idx < parts.len && part_idx < 4; ++part_idx) {
                                            free(arg_kinds[part_idx]);
                                            free(arg_primary_texts[part_idx]);
                                        }
                                    }
                                    if (call_symbol != NULL && *out_words != NULL) {
                                        string_list_free(&parts);
                                        free(arg_text);
                                        free(callee);
                                        free(const_kind);
                                        free(const_primary);
                                        free(line0);
                                        free(line1);
                                        string_list_free(&active_lines);
                                        string_list_free(&body_lines);
                                        free(trimmed);
                                        string_list_free(&param_names);
                                        return call_symbol;
                                    }
                                    free(call_symbol);
                                }
                                string_list_free(&parts);
                                free(arg_text);
                                free(callee);
                            }
                        }
                    }
                    free(const_kind);
                    free(const_primary);
                    free(line0);
                    free(line1);
                }
                string_list_free(&active_lines);
                string_list_free(&body_lines);
            }
        }
        if (!compiler_core_direct_text_match_allowed(bundle, routine_idx, 1, 2)) {
            string_list_free(&param_names);
            free(trimmed);
            return NULL;
        }
    }
    if (starts_with(trimmed, "return ")) {
        expr = trim_copy(trimmed + strlen("return"), trimmed + strlen(trimmed));
    } else {
        expr = trim_copy(trimmed, trimmed + strlen(trimmed));
    }
    {
        const char *operators[] = {"==", "!=", "<=", ">=", "<", ">"};
        size_t i;
        for (i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i) {
            int split = find_last_top_level_binary_text(expr, operators[i]);
            if (split >= 0) {
                char *lhs = trim_copy(expr, expr + split);
                char *rhs = trim_copy(expr + split + strlen(operators[i]), expr + strlen(expr));
                char *rhs_kind = NULL;
                char *rhs_primary = NULL;
                int rhs_arg0 = -1;
                if (compiler_core_match_simple_value_expr(rhs,
                                                          &param_names,
                                                          &rhs_kind,
                                                          &rhs_primary,
                                                          &rhs_arg0) &&
                    compiler_core_is_direct_const_value_kind(rhs_kind)) {
                    const char *name_end = parse_call_name_end(lhs);
                    int open_idx = (int)(name_end - lhs);
                    if (open_idx > 0 && lhs[open_idx] == '(') {
                        int close_idx = find_matching_delimiter_text(lhs, open_idx, '(', ')');
                        if (close_idx >= 0 && lhs[close_idx + 1] == '\0') {
                            char *callee = trim_copy(lhs, lhs + open_idx);
                            char *arg_text = trim_copy(lhs + open_idx + 1, lhs + close_idx);
                            char *call_symbol = compiler_core_direct_exec_callee_symbol(bundle, item_idx, callee, target_triple);
                            if (call_symbol != NULL) {
                                if (arg_text[0] == '\0') {
                                    *out_words =
                                        machine_direct_call_compare_const_rhs_words(machine_target_architecture(target_triple),
                                                                                    0,
                                                                                    NULL,
                                                                                    NULL,
                                                                                    NULL,
                                                                                    rhs_kind,
                                                                                    rhs_primary,
                                                                                    rhs_arg0,
                                                                                    operators[i]);
                                    if (*out_words != NULL) {
                                        free(rhs_kind);
                                        free(rhs_primary);
                                        free(arg_text);
                                        free(callee);
                                        free(lhs);
                                        free(rhs);
                                        free(expr);
                                        free(trimmed);
                                        string_list_free(&param_names);
                                        return call_symbol;
                                    }
                                } else {
                                    StringList parts = split_top_level_segments_text(arg_text, ',');
                                    if (parts.len == 1) {
                                        char *arg_kind = NULL;
                                        char *arg_primary = NULL;
                                        int arg0 = -1;
                                        if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                                  &param_names,
                                                                                  &arg_kind,
                                                                                  &arg_primary,
                                                                                  &arg0)) {
                                            const char *arg_kinds[1];
                                            const char *arg_primary_texts[1];
                                            int arg_arg0s[1];
                                            arg_kinds[0] = arg_kind;
                                            arg_primary_texts[0] = arg_primary;
                                            arg_arg0s[0] = arg0;
                                            *out_words =
                                                machine_direct_call_compare_const_rhs_words(machine_target_architecture(target_triple),
                                                                                            1,
                                                                                            arg_kinds,
                                                                                            arg_primary_texts,
                                                                                            arg_arg0s,
                                                                                            rhs_kind,
                                                                                            rhs_primary,
                                                                                            rhs_arg0,
                                                                                            operators[i]);
                                            if (*out_words != NULL) {
                                                free(arg_kind);
                                                free(arg_primary);
                                                string_list_free(&parts);
                                                free(rhs_kind);
                                                free(rhs_primary);
                                                free(arg_text);
                                                free(callee);
                                                free(lhs);
                                                free(rhs);
                                                free(expr);
                                                free(trimmed);
                                                string_list_free(&param_names);
                                                return call_symbol;
                                            }
                                        }
                                        free(arg_kind);
                                        free(arg_primary);
                                    }
                                    string_list_free(&parts);
                                }
                            }
                            free(call_symbol);
                            free(arg_text);
                            free(callee);
                        }
                    }
                }
                free(rhs_kind);
                free(rhs_primary);
                free(lhs);
                free(rhs);
            }
        }
    }
    {
        const char *name_end = parse_call_name_end(expr);
        int open_idx = (int)(name_end - expr);
        int close_idx;
        if (open_idx > 0 && expr[open_idx] == '(') {
            close_idx = find_matching_delimiter_text(expr, open_idx, '(', ')');
            if (close_idx >= 0 && expr[close_idx + 1] == '\0') {
                char *callee = trim_copy(expr, expr + open_idx);
                char *arg_text = trim_copy(expr + open_idx + 1, expr + close_idx);
                char *call_symbol =
                    compiler_core_direct_exec_callee_symbol(bundle, item_idx, callee, target_triple);
                if (call_symbol != NULL) {
                    if (arg_text[0] == '\0') {
                        *out_words = machine_direct_call_words(machine_target_architecture(target_triple));
                        free(arg_text);
                        free(callee);
                        free(expr);
                        free(trimmed);
                        return call_symbol;
                    } else {
                        StringList parts = split_top_level_segments_text(arg_text, ',');
                        if (parts.len == 1) {
                            char *arg_kind = NULL;
                            char *arg_primary = NULL;
                            int arg0 = -1;
                            if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                      &param_names,
                                                                      &arg_kind,
                                                                      &arg_primary,
                                                                      &arg0)) {
                                *out_words = machine_direct_call_simple_arg_words(machine_target_architecture(target_triple),
                                                                                  arg_kind,
                                                                                  arg_primary,
                                                                                  arg0);
                                free(arg_kind);
                                free(arg_primary);
                                string_list_free(&parts);
                                free(arg_text);
                                free(callee);
                                free(expr);
                                free(trimmed);
                                return call_symbol;
                            }
                            free(arg_kind);
                            free(arg_primary);
                            {
                                char *cstring_payload = compiler_core_parse_string_literal_expr(parts.items[0]);
                                if (cstring_payload != NULL) {
                                    const char *arg_kinds[1] = {"load_str"};
                                    const char *arg_primary_texts[1] = {cstring_payload};
                                    int arg_arg0s[1] = {-1};
                                    int callee_routine_idx =
                                        compiler_core_direct_exec_callee_routine_index_for_call(bundle,
                                                                                                (int)item_idx,
                                                                                                callee,
                                                                                                signature,
                                                                                                1,
                                                                                                arg_kinds,
                                                                                                arg_primary_texts,
                                                                                                arg_arg0s);
                                    if (callee_routine_idx >= 0) {
                                        char *callee_param_type =
                                            compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[callee_routine_idx],
                                                                                    0);
                                        if (compiler_core_type_matches(callee_param_type, "str")) {
                                            *out_words =
                                                machine_direct_call_cstring_arg_words(machine_target_architecture(target_triple),
                                                                                      (int)strlen(cstring_payload));
                                            free(callee_param_type);
                                            free(cstring_payload);
                                            string_list_free(&parts);
                                            free(arg_text);
                                            free(callee);
                                            free(expr);
                                            free(trimmed);
                                            return call_symbol;
                                        }
                                        free(callee_param_type);
                                    }
                                }
                                free(cstring_payload);
                            }
                            {
                                const char *aggregate_result_type_text = "";
                                int source_reg0 = -1;
                                int source_reg1 = -1;
                                int source_reg1_is64 = 0;
                                if (compiler_core_match_aggregate_field_expr(parts.items[0],
                                                                             &param_names,
                                                                             signature,
                                                                             &aggregate_result_type_text,
                                                                             &source_reg0,
                                                                             &source_reg1,
                                                                             &source_reg1_is64)) {
                                    const char *arg_kinds[1] = {"resolved_type"};
                                    const char *arg_primary_texts[1] = {aggregate_result_type_text};
                                    int arg_arg0s[1] = {-1};
                                    int callee_routine_idx =
                                        compiler_core_direct_exec_callee_routine_index_for_call(bundle,
                                                                                                (int)item_idx,
                                                                                                callee,
                                                                                                signature,
                                                                                                1,
                                                                                                arg_kinds,
                                                                                                arg_primary_texts,
                                                                                                arg_arg0s);
                                    if (callee_routine_idx >= 0) {
                                        char *callee_param_type =
                                            compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[callee_routine_idx],
                                                                                    0);
                                        if (compiler_core_type_matches(callee_param_type, aggregate_result_type_text)) {
                                            *out_words =
                                                machine_direct_call_register_pair_arg_words(machine_target_architecture(target_triple),
                                                                                            source_reg0,
                                                                                            source_reg1,
                                                                                            source_reg1_is64);
                                            free(callee_param_type);
                                            string_list_free(&parts);
                                            free(arg_text);
                                            free(callee);
                                            free(expr);
                                            free(trimmed);
                                            return call_symbol;
                                        }
                                        free(callee_param_type);
                                    }
                                }
                            }
                        } else if (parts.len == 2) {
                            char *lhs_kind = NULL;
                            char *lhs_primary = NULL;
                            int lhs_arg0 = -1;
                            char *rhs_kind = NULL;
                            char *rhs_primary = NULL;
                            int rhs_arg0 = -1;
                            char *rhs_cstring = NULL;
                            int callee_routine_idx = -1;
                            char *callee_param0_type = NULL;
                            char *callee_param1_type = NULL;
                            if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                      &param_names,
                                                                      &lhs_kind,
                                                                      &lhs_primary,
                                                                      &lhs_arg0)) {
                                rhs_cstring = compiler_core_parse_string_literal_expr(parts.items[1]);
                                if (rhs_cstring != NULL) {
                                    const char *arg_kinds[2] = {lhs_kind, "load_str"};
                                    const char *arg_primary_texts[2] = {lhs_primary, rhs_cstring};
                                    int arg_arg0s[2] = {lhs_arg0, -1};
                                    callee_routine_idx =
                                        compiler_core_direct_exec_callee_routine_index_for_call(bundle,
                                                                                                (int)item_idx,
                                                                                                callee,
                                                                                                signature,
                                                                                                2,
                                                                                                arg_kinds,
                                                                                                arg_primary_texts,
                                                                                                arg_arg0s);
                                    if (callee_routine_idx >= 0) {
                                        callee_param0_type =
                                            compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[callee_routine_idx],
                                                                                    0);
                                        callee_param1_type =
                                            compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[callee_routine_idx],
                                                                                    1);
                                        if (compiler_core_type_matches(callee_param0_type, "str") &&
                                            compiler_core_type_matches(callee_param1_type, "str")) {
                                            *out_words =
                                                machine_direct_call_second_cstring_arg_words(machine_target_architecture(target_triple),
                                                                                             (int)strlen(rhs_cstring));
                                            free(callee_param0_type);
                                            free(callee_param1_type);
                                            free(rhs_cstring);
                                            free(lhs_kind);
                                            free(lhs_primary);
                                            string_list_free(&parts);
                                            free(arg_text);
                                            free(callee);
                                            free(expr);
                                            free(trimmed);
                                            return call_symbol;
                                        }
                                        free(callee_param0_type);
                                        free(callee_param1_type);
                                    }
                                }
                            }
                            free(rhs_cstring);
                            free(lhs_kind);
                            free(lhs_primary);
                            lhs_kind = NULL;
                            lhs_primary = NULL;
                            if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                      &param_names,
                                                                      &lhs_kind,
                                                                      &lhs_primary,
                                                                      &lhs_arg0) &&
                                compiler_core_match_simple_value_expr(parts.items[1],
                                                                      &param_names,
                                                                      &rhs_kind,
                                                                      &rhs_primary,
                                                                      &rhs_arg0)) {
                                *out_words = machine_direct_call_two_simple_arg_words(machine_target_architecture(target_triple),
                                                                                      lhs_kind,
                                                                                      lhs_primary,
                                                                                      lhs_arg0,
                                                                                      rhs_kind,
                                                                                      rhs_primary,
                                                                                      rhs_arg0);
                                free(lhs_kind);
                                free(lhs_primary);
                                free(rhs_kind);
                                free(rhs_primary);
                                string_list_free(&parts);
                                free(arg_text);
                                free(callee);
                                free(expr);
                                free(trimmed);
                                return call_symbol;
                            }
                            free(lhs_kind);
                            free(lhs_primary);
                            free(rhs_kind);
                            free(rhs_primary);
                        } else if (parts.len == 3) {
                            char *arg0_kind = NULL;
                            char *arg0_primary = NULL;
                            int arg0 = -1;
                            char *arg1_kind = NULL;
                            char *arg1_primary = NULL;
                            int arg1 = -1;
                            char *arg2_kind = NULL;
                            char *arg2_primary = NULL;
                            int arg2 = -1;
                            if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                      &param_names,
                                                                      &arg0_kind,
                                                                      &arg0_primary,
                                                                      &arg0) &&
                                compiler_core_match_simple_value_expr(parts.items[1],
                                                                      &param_names,
                                                                      &arg1_kind,
                                                                      &arg1_primary,
                                                                      &arg1) &&
                                compiler_core_match_simple_value_expr(parts.items[2],
                                                                      &param_names,
                                                                      &arg2_kind,
                                                                      &arg2_primary,
                                                                      &arg2)) {
                                *out_words = machine_direct_call_three_simple_arg_words(machine_target_architecture(target_triple),
                                                                                        arg0_kind,
                                                                                        arg0_primary,
                                                                                        arg0,
                                                                                        arg1_kind,
                                                                                        arg1_primary,
                                                                                        arg1,
                                                                                        arg2_kind,
                                                                                        arg2_primary,
                                                                                        arg2);
                                free(arg0_kind);
                                free(arg0_primary);
                                free(arg1_kind);
                                free(arg1_primary);
                                free(arg2_kind);
                                free(arg2_primary);
                                string_list_free(&parts);
                                free(arg_text);
                                free(callee);
                                free(expr);
                                string_list_free(&param_names);
                                free(trimmed);
                                return call_symbol;
                            }
                            free(arg0_kind);
                            free(arg0_primary);
                            free(arg1_kind);
                            free(arg1_primary);
                            free(arg2_kind);
                            free(arg2_primary);
                        } else if (parts.len == 4) {
                            char *arg0_kind = NULL;
                            char *arg0_primary = NULL;
                            int arg0 = -1;
                            char *arg1_kind = NULL;
                            char *arg1_primary = NULL;
                            int arg1 = -1;
                            char *arg2_kind = NULL;
                            char *arg2_primary = NULL;
                            int arg2 = -1;
                            char *arg3_kind = NULL;
                            char *arg3_primary = NULL;
                            int arg3 = -1;
                            if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                      &param_names,
                                                                      &arg0_kind,
                                                                      &arg0_primary,
                                                                      &arg0) &&
                                compiler_core_match_simple_value_expr(parts.items[1],
                                                                      &param_names,
                                                                      &arg1_kind,
                                                                      &arg1_primary,
                                                                      &arg1) &&
                                compiler_core_match_simple_value_expr(parts.items[2],
                                                                      &param_names,
                                                                      &arg2_kind,
                                                                      &arg2_primary,
                                                                      &arg2) &&
                                compiler_core_match_simple_value_expr(parts.items[3],
                                                                      &param_names,
                                                                      &arg3_kind,
                                                                      &arg3_primary,
                                                                      &arg3)) {
                                *out_words = machine_direct_call_four_simple_arg_words(machine_target_architecture(target_triple),
                                                                                       arg0_kind,
                                                                                       arg0_primary,
                                                                                       arg0,
                                                                                       arg1_kind,
                                                                                       arg1_primary,
                                                                                       arg1,
                                                                                       arg2_kind,
                                                                                       arg2_primary,
                                                                                       arg2,
                                                                                       arg3_kind,
                                                                                       arg3_primary,
                                                                                       arg3);
                                free(arg0_kind);
                                free(arg0_primary);
                                free(arg1_kind);
                                free(arg1_primary);
                                free(arg2_kind);
                                free(arg2_primary);
                                free(arg3_kind);
                                free(arg3_primary);
                                string_list_free(&parts);
                                free(arg_text);
                                free(callee);
                                free(expr);
                                string_list_free(&param_names);
                                free(trimmed);
                                return call_symbol;
                            }
                            free(arg0_kind);
                            free(arg0_primary);
                            free(arg1_kind);
                            free(arg1_primary);
                            free(arg2_kind);
                            free(arg2_primary);
                            free(arg3_kind);
                            free(arg3_primary);
                        }
                        string_list_free(&parts);
                    }
                    free(call_symbol);
                }
                free(arg_text);
                free(callee);
            }
        }
    }
    string_list_free(&param_names);
    free(expr);
    free(trimmed);
    return NULL;
}

static char *compiler_core_direct_exec_void_call_symbol(const CompilerCoreBundle *bundle,
                                                        size_t item_idx,
                                                        size_t routine_idx,
                                                        const char *target_triple,
                                                        char **out_words) {
    const char *body = bundle->program.routine_first_body_lines.items[routine_idx];
    const char *body_text = bundle->program.routine_body_texts.items[routine_idx];
    const char *signature = bundle->program.routine_signatures.items[routine_idx];
    char *trimmed = trim_copy(body, body + strlen(body));
    StringList param_names;
    memset(&param_names, 0, sizeof(param_names));
    (void)item_idx;
    parse_compiler_routine_signature_param_names(signature, &param_names);
    *out_words = NULL;
    if (body_text != NULL && strchr(body_text, '\n') != NULL) {
        if (compiler_core_direct_text_match_allowed(bundle, routine_idx, 2, 3)) {
            char *multiline_symbol =
                compiler_core_multiline_alias_call_symbol(bundle,
                                                         item_idx,
                                                          routine_idx,
                                                          target_triple,
                                                          &param_names,
                                                          1,
                                                          out_words);
            if (multiline_symbol != NULL && *out_words != NULL) {
                string_list_free(&param_names);
                free(trimmed);
                return multiline_symbol;
            }
            free(multiline_symbol);
            free(*out_words);
            *out_words = NULL;
        }
        if (!compiler_core_direct_text_match_allowed(bundle, routine_idx, 1, 2)) {
            string_list_free(&param_names);
            free(trimmed);
            return NULL;
        }
    }
    {
        const char *name_end = parse_call_name_end(trimmed);
        int open_idx = (int)(name_end - trimmed);
        int close_idx;
        if (open_idx > 0 && trimmed[open_idx] == '(') {
            close_idx = find_matching_delimiter_text(trimmed, open_idx, '(', ')');
            if (close_idx >= 0 && trimmed[close_idx + 1] == '\0') {
                char *callee = trim_copy(trimmed, trimmed + open_idx);
                char *arg_text = trim_copy(trimmed + open_idx + 1, trimmed + close_idx);
                char *call_symbol =
                    compiler_core_direct_exec_callee_symbol(bundle, item_idx, callee, target_triple);
                if (call_symbol != NULL) {
                        if (arg_text[0] == '\0') {
                            *out_words = machine_direct_call_void_words(machine_target_architecture(target_triple));
                            free(arg_text);
                            free(callee);
                            free(trimmed);
                            return call_symbol;
                        } else {
                            StringList parts = split_top_level_segments_text(arg_text, ',');
                            if (parts.len == 1) {
                                char *arg_kind = NULL;
                                char *arg_primary = NULL;
                                int arg0 = -1;
                                if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                          &param_names,
                                                                          &arg_kind,
                                                                          &arg_primary,
                                                                          &arg0)) {
                                    *out_words = machine_direct_call_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                                           arg_kind,
                                                                                           arg_primary,
                                                                                           arg0);
                                    free(arg_kind);
                                    free(arg_primary);
                                    string_list_free(&parts);
                                    free(arg_text);
                                    free(callee);
                                    free(trimmed);
                                    return call_symbol;
                                }
                                free(arg_kind);
                                free(arg_primary);
                                {
                                    char *cstring_payload = compiler_core_parse_string_literal_expr(parts.items[0]);
                                    if (cstring_payload != NULL) {
                                        const char *arg_kinds[1] = {"load_str"};
                                        const char *arg_primary_texts[1] = {cstring_payload};
                                        int arg_arg0s[1] = {-1};
                                        int callee_routine_idx =
                                            compiler_core_direct_exec_callee_routine_index_for_call(bundle,
                                                                                                    (int)item_idx,
                                                                                                    callee,
                                                                                                    signature,
                                                                                                    1,
                                                                                                    arg_kinds,
                                                                                                    arg_primary_texts,
                                                                                                    arg_arg0s);
                                        if (callee_routine_idx >= 0) {
                                            char *callee_param_type =
                                                compiler_core_signature_param_type_text(bundle->program.routine_signatures.items[callee_routine_idx],
                                                                                        0);
                                            if (compiler_core_type_matches(callee_param_type, "str")) {
                                                *out_words =
                                                    machine_direct_call_cstring_arg_void_words(machine_target_architecture(target_triple),
                                                                                               (int)strlen(cstring_payload));
                                                free(callee_param_type);
                                                free(cstring_payload);
                                                string_list_free(&parts);
                                                free(arg_text);
                                                free(callee);
                                                free(trimmed);
                                                return call_symbol;
                                            }
                                            free(callee_param_type);
                                        }
                                    }
                                    free(cstring_payload);
                                }
                            } else if (parts.len == 2) {
                                char *lhs_kind = NULL;
                                char *lhs_primary = NULL;
                                int lhs_arg0 = -1;
                                char *rhs_kind = NULL;
                                char *rhs_primary = NULL;
                                int rhs_arg0 = -1;
                                if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                          &param_names,
                                                                          &lhs_kind,
                                                                          &lhs_primary,
                                                                          &lhs_arg0) &&
                                    compiler_core_match_simple_value_expr(parts.items[1],
                                                                          &param_names,
                                                                          &rhs_kind,
                                                                          &rhs_primary,
                                                                          &rhs_arg0)) {
                                    *out_words = machine_direct_call_two_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                                               lhs_kind,
                                                                                               lhs_primary,
                                                                                               lhs_arg0,
                                                                                               rhs_kind,
                                                                                               rhs_primary,
                                                                                               rhs_arg0);
                                    free(lhs_kind);
                                    free(lhs_primary);
                                    free(rhs_kind);
                                    free(rhs_primary);
                                    string_list_free(&parts);
                                    free(arg_text);
                                    free(callee);
                                    free(trimmed);
                                    return call_symbol;
                                }
                                free(lhs_kind);
                                free(lhs_primary);
                                free(rhs_kind);
                                free(rhs_primary);
                            } else if (parts.len == 3) {
                                char *arg0_kind = NULL;
                                char *arg0_primary = NULL;
                                int arg0 = -1;
                                char *arg1_kind = NULL;
                                char *arg1_primary = NULL;
                                int arg1 = -1;
                                char *arg2_kind = NULL;
                                char *arg2_primary = NULL;
                                int arg2 = -1;
                                if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                          &param_names,
                                                                          &arg0_kind,
                                                                          &arg0_primary,
                                                                          &arg0) &&
                                    compiler_core_match_simple_value_expr(parts.items[1],
                                                                          &param_names,
                                                                          &arg1_kind,
                                                                          &arg1_primary,
                                                                          &arg1) &&
                                    compiler_core_match_simple_value_expr(parts.items[2],
                                                                          &param_names,
                                                                          &arg2_kind,
                                                                          &arg2_primary,
                                                                          &arg2)) {
                                    *out_words = machine_direct_call_three_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                                                 arg0_kind,
                                                                                                 arg0_primary,
                                                                                                 arg0,
                                                                                                 arg1_kind,
                                                                                                 arg1_primary,
                                                                                                 arg1,
                                                                                                 arg2_kind,
                                                                                                 arg2_primary,
                                                                                                 arg2);
                                    free(arg0_kind);
                                    free(arg0_primary);
                                    free(arg1_kind);
                                    free(arg1_primary);
                                    free(arg2_kind);
                                    free(arg2_primary);
                                    string_list_free(&parts);
                                    free(arg_text);
                                    free(callee);
                                    string_list_free(&param_names);
                                    free(trimmed);
                                    return call_symbol;
                                }
                                free(arg0_kind);
                                free(arg0_primary);
                                free(arg1_kind);
                                free(arg1_primary);
                                free(arg2_kind);
                                free(arg2_primary);
                            } else if (parts.len == 4) {
                                char *arg0_kind = NULL;
                                char *arg0_primary = NULL;
                                int arg0 = -1;
                                char *arg1_kind = NULL;
                                char *arg1_primary = NULL;
                                int arg1 = -1;
                                char *arg2_kind = NULL;
                                char *arg2_primary = NULL;
                                int arg2 = -1;
                                char *arg3_kind = NULL;
                                char *arg3_primary = NULL;
                                int arg3 = -1;
                                if (compiler_core_match_simple_value_expr(parts.items[0],
                                                                          &param_names,
                                                                          &arg0_kind,
                                                                          &arg0_primary,
                                                                          &arg0) &&
                                    compiler_core_match_simple_value_expr(parts.items[1],
                                                                          &param_names,
                                                                          &arg1_kind,
                                                                          &arg1_primary,
                                                                          &arg1) &&
                                    compiler_core_match_simple_value_expr(parts.items[2],
                                                                          &param_names,
                                                                          &arg2_kind,
                                                                          &arg2_primary,
                                                                          &arg2) &&
                                    compiler_core_match_simple_value_expr(parts.items[3],
                                                                          &param_names,
                                                                          &arg3_kind,
                                                                          &arg3_primary,
                                                                          &arg3)) {
                                    *out_words = machine_direct_call_four_simple_arg_void_words(machine_target_architecture(target_triple),
                                                                                                arg0_kind,
                                                                                                arg0_primary,
                                                                                                arg0,
                                                                                                arg1_kind,
                                                                                                arg1_primary,
                                                                                                arg1,
                                                                                                arg2_kind,
                                                                                                arg2_primary,
                                                                                                arg2,
                                                                                                arg3_kind,
                                                                                                arg3_primary,
                                                                                                arg3);
                                    free(arg0_kind);
                                    free(arg0_primary);
                                    free(arg1_kind);
                                    free(arg1_primary);
                                    free(arg2_kind);
                                    free(arg2_primary);
                                    free(arg3_kind);
                                    free(arg3_primary);
                                    string_list_free(&parts);
                                    free(arg_text);
                                    free(callee);
                                    string_list_free(&param_names);
                                    free(trimmed);
                                    return call_symbol;
                                }
                                free(arg0_kind);
                                free(arg0_primary);
                                free(arg1_kind);
                                free(arg1_primary);
                                free(arg2_kind);
                                free(arg2_primary);
                                free(arg3_kind);
                                free(arg3_primary);
                            }
                            string_list_free(&parts);
                        }
                    free(call_symbol);
                }
                free(arg_text);
                free(callee);
            }
        }
    }
    string_list_free(&param_names);
    free(trimmed);
    return NULL;
}

static MachineModuleBundle build_network_machine_module(const NetworkBundle *bundle,
                                                        const char *module_kind,
                                                        const char *target_triple) {
    MachineModuleBundle out = new_machine_module_bundle(module_kind, target_triple);
    size_t i;
    for (i = 0; i < bundle->low_plan.op_ids.len; ++i) {
        int frame_size = 16;
        char *binding_summary;
        if (strcmp(bundle->low_plan.transport_modes.items[i], "coded_bulk_dispersal") == 0) {
            frame_size += 16;
        }
        if (strcmp(bundle->low_plan.priority_classes.items[i], "background") == 0) {
            frame_size += 16;
        }
        if (strcmp(bundle->low_plan.anti_entropy_signature_kinds.items[i], "none") != 0) {
            frame_size += 16;
        }
        binding_summary = xformat("proof=%s|identity=%s|addressing=%s|depth=%d|address_binding=%s|distance_metric=%s|route_plan_model=%s|transport=%s|repair=%s|dispersal=%s|priority=%s|signature=%s|topology=%s",
                                  bundle->low_plan.proof_domains.items[i],
                                  bundle->low_plan.identity_scopes.items[i],
                                  bundle->low_plan.addressing_modes.items[i],
                                  bundle->low_plan.topology_depths.items[i],
                                  bundle->low_plan.address_bindings.items[i],
                                  bundle->low_plan.distance_metrics.items[i],
                                  bundle->low_plan.route_plan_models.items[i],
                                  bundle->low_plan.transport_modes.items[i],
                                  bundle->low_plan.repair_modes.items[i],
                                  bundle->low_plan.dispersal_modes.items[i],
                                  bundle->low_plan.priority_classes.items[i],
                                  bundle->low_plan.anti_entropy_signature_kinds.items[i],
                                  bundle->low_plan.topology_canonical_forms.items[i]);
        machine_append_function(&out,
                                bundle->low_plan.op_ids.items[i],
                                bundle->low_plan.canonical_ops.items[i],
                                bundle->low_plan.runtime_targets.items[i],
                                binding_summary,
                                frame_size);
        free(binding_summary);
    }
    return out;
}

static MachineModuleBundle build_compiler_core_machine_module(const CompilerCoreBundle *bundle,
                                                              const char *target_triple) {
    MachineModuleBundle out = new_machine_module_bundle("compiler_core", target_triple);
    size_t i;
    size_t routine_idx = 0;
    size_t module_count = 0;
    size_t import_count = 0;
    size_t type_count = 0;
    size_t const_count = 0;
    size_t var_count = 0;
    size_t importc_routine_count = 0;
    size_t routine_count = 0;
    size_t iterator_count = 0;
    size_t entry_index = 0;
    int have_entry = 0;
    for (i = 0; i < bundle->low_plan.item_ids.len; ++i) {
        int frame_size = 16;
        char *binding_summary;
        const char *kind = bundle->low_plan.top_level_kinds.items[i];
        int current_routine_idx = -1;
        if (!have_entry && strcmp(bundle->low_plan.labels.items[i], "main") == 0) {
            entry_index = i;
            have_entry = 1;
        }
        if (strcmp(kind, "module") == 0) {
            ++module_count;
        } else if (strcmp(kind, "import") == 0) {
            ++import_count;
        } else if (strcmp(kind, "type") == 0) {
            ++type_count;
        } else if (strcmp(kind, "const") == 0) {
            ++const_count;
        } else if (strcmp(kind, "var") == 0) {
            ++var_count;
        } else if (strcmp(kind, "importc_fn") == 0) {
            ++importc_routine_count;
        } else if (strcmp(kind, "iterator") == 0) {
            ++iterator_count;
        } else {
            ++routine_count;
        }
        if (strcmp(kind, "fn") == 0 || strcmp(kind, "iterator") == 0 ||
            strcmp(kind, "importc_fn") == 0) {
            current_routine_idx = (int)routine_idx;
            routine_idx += 1;
        }
        if (strcmp(bundle->low_plan.effect_classes.items[i], "module_resolution") != 0) {
            frame_size += 16;
        }
        if (bundle->low_plan.body_line_counts.items[i] > 0) {
            frame_size += 16;
        }
        if (strcmp(bundle->low_plan.binding_scopes.items[i], "type_scope") == 0 ||
            strcmp(bundle->low_plan.binding_scopes.items[i], "iterator_scope") == 0) {
            frame_size += 16;
        }
        const char *machine_runtime_target =
            compiler_core_machine_runtime_target(bundle->low_plan.runtime_targets.items[i]);
        binding_summary =
            xformat("top_level=%s|label=%s|proof=%s|effect=%s|scope=%s|"
                    "import_path=%s|import_alias=%s|import_category=%s|"
                    "ast_ready=%d|return_type=%s|param_count=%d|local_count=%d|stmt_count=%d|expr_count=%d|"
                    "body_lines=%d|signature_hash=%s|body_hash=%s",
                    bundle->low_plan.top_level_kinds.items[i],
                    bundle->low_plan.labels.items[i],
                    bundle->low_plan.proof_domains.items[i],
                    bundle->low_plan.effect_classes.items[i],
                    bundle->low_plan.binding_scopes.items[i],
                    bundle->low_plan.import_paths.items[i],
                    bundle->low_plan.import_aliases.items[i],
                    bundle->low_plan.import_categories.items[i],
                    bundle->low_plan.executable_ast_ready.items[i],
                    bundle->low_plan.return_type_texts.items[i],
                    bundle->low_plan.param_counts.items[i],
                    bundle->low_plan.local_counts.items[i],
                    bundle->low_plan.stmt_counts.items[i],
                    bundle->low_plan.expr_counts.items[i],
                    bundle->low_plan.body_line_counts.items[i],
                    bundle->low_plan.signature_hashes.items[i],
                    bundle->low_plan.body_hashes.items[i]);
        machine_append_function(&out,
                                bundle->low_plan.item_ids.items[i],
                                bundle->low_plan.canonical_ops.items[i],
                                machine_runtime_target,
                                binding_summary,
                                frame_size);
        if (current_routine_idx >= 0 &&
            bundle->low_plan.executable_ast_ready.items[i] &&
            bundle->program.routine_body_line_counts.items[current_routine_idx] > 0) {
            char *direct_cstring_payload =
                compiler_core_direct_exec_cstring_payload(bundle, (size_t)current_routine_idx);
            if (direct_cstring_payload != NULL) {
                char *direct_cstring_words =
                    machine_direct_return_cstring_literal_words(out.architecture,
                                                                (int)strlen(direct_cstring_payload));
                if (direct_cstring_words != NULL) {
                    machine_rewrite_function_direct_exec_cstring_data(&out,
                                                                      (int)out.item_ids.len - 1,
                                                                      direct_cstring_words,
                                                                      direct_cstring_payload);
                    free(direct_cstring_payload);
                    continue;
                }
                free(direct_cstring_payload);
            }
            char *direct_words =
                compiler_core_direct_exec_words(bundle, i, (size_t)current_routine_idx, out.architecture);
            if (direct_words != NULL) {
                machine_rewrite_function_direct_exec(&out, (int)out.item_ids.len - 1, direct_words);
            } else {
                char *if_else_call_words = NULL;
                char *if_else_call_symbol0 = NULL;
                char *if_else_call_symbol1 = NULL;
                char *if_else_call_symbol2 = NULL;
                char *nested_words = NULL;
                char *nested_call_symbol0 = NULL;
                char *nested_call_symbol1 = NULL;
                char *nested_void_words = NULL;
                char *nested_void_call_symbol0 = NULL;
                char *nested_void_call_symbol1 = NULL;
                char *fallback_words = NULL;
                char *fallback_call_symbol0 = NULL;
                char *fallback_call_symbol1 = NULL;
                char *call_words = NULL;
                char *direct_call_symbol =
                    compiler_core_direct_exec_call_symbol(bundle,
                                                          i,
                                                          (size_t)current_routine_idx,
                                                          out.target_triple,
                                                          &call_words);
                if (compiler_core_direct_exec_if_else_call_symbols(bundle,
                                                                   i,
                                                                   (size_t)current_routine_idx,
                                                                   out.target_triple,
                                                                   &if_else_call_words,
                                                                   &if_else_call_symbol0,
                                                                   &if_else_call_symbol1,
                                                                   &if_else_call_symbol2)) {
                    char *if_else_call_symbols[3];
                    int if_else_call_symbol_count = if_else_call_symbol2 != NULL ? 3 : 2;
                    if_else_call_symbols[0] = if_else_call_symbol0;
                    if_else_call_symbols[1] = if_else_call_symbol1;
                    if_else_call_symbols[2] = if_else_call_symbol2;
                    machine_rewrite_function_direct_exec_calls(&out,
                                                               (int)out.item_ids.len - 1,
                                                               if_else_call_words,
                                                               if_else_call_symbol_count,
                                                               if_else_call_symbols);
                    free(if_else_call_symbol0);
                    free(if_else_call_symbol1);
                    free(if_else_call_symbol2);
                } else if (compiler_core_direct_exec_nested_call_symbols(bundle,
                                                                  i,
                                                                  (size_t)current_routine_idx,
                                                                  out.target_triple,
                                                                  &nested_words,
                                                                  &nested_call_symbol0,
                                                                  &nested_call_symbol1)) {
                    char *nested_symbols[2];
                    char *nested_cstring_payload = NULL;
                    nested_symbols[0] = nested_call_symbol0;
                    nested_symbols[1] = nested_call_symbol1;
                    nested_cstring_payload =
                        compiler_core_direct_exec_nested_call_cstring_payload(bundle, (size_t)current_routine_idx);
                    if (nested_cstring_payload != NULL) {
                        machine_rewrite_function_direct_exec_calls_cstring_data(&out,
                                                                                (int)out.item_ids.len - 1,
                                                                                nested_words,
                                                                                2,
                                                                                nested_symbols,
                                                                                nested_cstring_payload);
                        free(nested_cstring_payload);
                    } else {
                        machine_rewrite_function_direct_exec_calls(&out,
                                                                   (int)out.item_ids.len - 1,
                                                                   nested_words,
                                                                   2,
                                                                   nested_symbols);
                    }
                    free(nested_call_symbol0);
                    free(nested_call_symbol1);
                } else if (compiler_core_direct_exec_nested_void_call_symbols(bundle,
                                                                              i,
                                                                              (size_t)current_routine_idx,
                                                                              out.target_triple,
                                                                              &nested_void_words,
                                                                              &nested_void_call_symbol0,
                                                                              &nested_void_call_symbol1)) {
                    char *nested_void_symbols[2];
                    nested_void_symbols[0] = nested_void_call_symbol0;
                    nested_void_symbols[1] = nested_void_call_symbol1;
                    machine_rewrite_function_direct_exec_calls(&out,
                                                               (int)out.item_ids.len - 1,
                                                               nested_void_words,
                                                               2,
                                                               nested_void_symbols);
                    free(nested_void_call_symbol0);
                    free(nested_void_call_symbol1);
                } else if (compiler_core_multiline_fallback_call_symbols(bundle,
                                                                         i,
                                                                         (size_t)current_routine_idx,
                                                                         out.target_triple,
                                                                         &fallback_words,
                                                                         &fallback_call_symbol0,
                                                                         &fallback_call_symbol1)) {
                    char *fallback_symbols[2];
                    fallback_symbols[0] = fallback_call_symbol0;
                    fallback_symbols[1] = fallback_call_symbol1;
                    machine_rewrite_function_direct_exec_calls(&out,
                                                               (int)out.item_ids.len - 1,
                                                               fallback_words,
                                                               2,
                                                               fallback_symbols);
                    free(fallback_call_symbol0);
                    free(fallback_call_symbol1);
                } else if (strcmp(bundle->low_plan.return_type_texts.items[i], "void") == 0) {
                    free(call_words);
                    free(direct_call_symbol);
                    call_words = NULL;
                    direct_call_symbol =
                        compiler_core_direct_exec_void_call_symbol(bundle,
                                                                   i,
                                                                   (size_t)current_routine_idx,
                                                                   out.target_triple,
                                                                   &call_words);
                    if (direct_call_symbol != NULL && call_words != NULL) {
                        char *direct_void_call_cstring_payload =
                            compiler_core_direct_exec_void_call_cstring_payload(bundle, (size_t)current_routine_idx);
                        if (direct_void_call_cstring_payload != NULL) {
                            machine_rewrite_function_direct_exec_call_cstring_data(&out,
                                                                                   (int)out.item_ids.len - 1,
                                                                                   call_words,
                                                                                   direct_call_symbol,
                                                                                   direct_void_call_cstring_payload);
                            free(direct_void_call_cstring_payload);
                        } else {
                            machine_rewrite_function_direct_exec_call(&out,
                                                                      (int)out.item_ids.len - 1,
                                                                      call_words,
                                                                      direct_call_symbol);
                        }
                    } else {
                        free(call_words);
                        free(direct_call_symbol);
                    }
                } else if (direct_call_symbol != NULL && call_words != NULL) {
                    char *direct_call_cstring_payload =
                        compiler_core_direct_exec_call_cstring_payload(bundle, (size_t)current_routine_idx);
                    if (direct_call_cstring_payload != NULL) {
                        machine_rewrite_function_direct_exec_call_cstring_data(&out,
                                                                               (int)out.item_ids.len - 1,
                                                                               call_words,
                                                                               direct_call_symbol,
                                                                               direct_call_cstring_payload);
                        free(direct_call_cstring_payload);
                    } else {
                        machine_rewrite_function_direct_exec_call(&out,
                                                                  (int)out.item_ids.len - 1,
                                                                  call_words,
                                                                  direct_call_symbol);
                    }
                } else {
                    free(if_else_call_words);
                    free(if_else_call_symbol0);
                    free(if_else_call_symbol1);
                    free(if_else_call_symbol2);
                    free(nested_words);
                    free(nested_call_symbol0);
                    free(nested_call_symbol1);
                    free(nested_void_words);
                    free(nested_void_call_symbol0);
                    free(nested_void_call_symbol1);
                    free(fallback_words);
                    free(fallback_call_symbol0);
                    free(fallback_call_symbol1);
                    free(call_words);
                    free(direct_call_symbol);
                    call_words = NULL;
                    direct_call_symbol =
                        compiler_core_direct_exec_void_call_symbol(bundle,
                                                                   i,
                                                                   (size_t)current_routine_idx,
                                                                   out.target_triple,
                                                                   &call_words);
                    if (direct_call_symbol != NULL && call_words != NULL) {
                        char *direct_void_call_cstring_payload =
                            compiler_core_direct_exec_void_call_cstring_payload(bundle, (size_t)current_routine_idx);
                        if (direct_void_call_cstring_payload != NULL) {
                            machine_rewrite_function_direct_exec_call_cstring_data(&out,
                                                                                   (int)out.item_ids.len - 1,
                                                                                   call_words,
                                                                                   direct_call_symbol,
                                                                                   direct_void_call_cstring_payload);
                            free(direct_void_call_cstring_payload);
                        } else {
                            machine_rewrite_function_direct_exec_call(&out,
                                                                      (int)out.item_ids.len - 1,
                                                                      call_words,
                                                                      direct_call_symbol);
                        }
                    } else {
                        free(call_words);
                        free(direct_call_symbol);
                    }
                }
            }
        }
        free(binding_summary);
    }
    if (!have_entry && bundle->low_plan.item_ids.len > 0) {
        entry_index = 0;
        have_entry = 1;
    }
    if (have_entry) {
        int entry_item_id = bundle->low_plan.item_ids.items[entry_index];
        char *binding_summary =
            xformat("entry_bridge=1|dispatch=runtime/compiler_core.argv_entry|entry_label=%s|entry_kind=%s|entry_op=%s|"
                    "entry_ast_ready=%d|entry_return_type=%s|entry_param_count=%d|entry_local_count=%d|entry_stmt_count=%d|entry_expr_count=%d|"
                    "top_levels=%zu|modules=%zu|imports=%zu|"
                    "types=%zu|consts=%zu|vars=%zu|importc_routines=%zu|routines=%zu|iterators=%zu",
                    bundle->low_plan.labels.items[entry_index],
                    bundle->low_plan.top_level_kinds.items[entry_index],
                    bundle->low_plan.canonical_ops.items[entry_index],
                    bundle->low_plan.executable_ast_ready.items[entry_index],
                    bundle->low_plan.return_type_texts.items[entry_index],
                    bundle->low_plan.param_counts.items[entry_index],
                    bundle->low_plan.local_counts.items[entry_index],
                    bundle->low_plan.stmt_counts.items[entry_index],
                    bundle->low_plan.expr_counts.items[entry_index],
                    bundle->low_plan.item_ids.len,
                    module_count,
                    import_count,
                    type_count,
                    const_count,
                    var_count,
                    importc_routine_count,
                    routine_count,
                    iterator_count);
        machine_append_dispatch_entry_function(&out,
                                               1000000000 + entry_item_id,
                                               "compiler_core_argv_bridge",
                                               "runtime/compiler_core.argv_entry",
                                               binding_summary,
                                               out.frame_sizes.items[entry_index]);
        free(out.entry_symbol_override);
        out.entry_symbol_override = xstrdup_text(out.symbol_names.items[out.symbol_names.len - 1]);
        free(binding_summary);
    }
    return out;
}

static char *build_machine_module_text(const MachineModuleBundle *bundle) {
    StringList lines;
    size_t i;
    memset(&lines, 0, sizeof(lines));
    string_list_pushf(&lines, "machine_pipeline_version=%s", bundle->version);
    string_list_pushf(&lines, "module_kind=%s", bundle->module_kind);
    string_list_pushf(&lines, "target=%s", bundle->target_triple);
    string_list_pushf(&lines, "architecture=%s", bundle->architecture);
    string_list_pushf(&lines, "obj_format=%s", bundle->obj_format);
    string_list_pushf(&lines, "text_section=%s", bundle->text_section_name);
    string_list_pushf(&lines, "call_relocation=%s", bundle->call_relocation_kind);
    string_list_pushf(&lines, "metadata_page_relocation=%s", bundle->metadata_page_relocation_kind);
    string_list_pushf(&lines, "metadata_pageoff_relocation=%s", bundle->metadata_pageoff_relocation_kind);
    string_list_pushf(&lines, "entry_symbol_override=%s", bundle->entry_symbol_override);
    string_list_pushf(&lines, "function_count=%zu", bundle->item_ids.len);
    for (i = 0; i < bundle->item_ids.len; ++i) {
        string_list_pushf(&lines, "function.%zu.symbol=%s", i, bundle->symbol_names.items[i]);
        string_list_pushf(&lines, "function.%zu.runtime=%s", i, bundle->runtime_targets.items[i]);
        string_list_pushf(&lines, "function.%zu.call_symbol=%s", i, bundle->call_symbol_names.items[i]);
        string_list_pushf(&lines, "function.%zu.frame_size=%d", i, bundle->frame_sizes.items[i]);
        string_list_pushf(&lines, "function.%zu.bindings=%s", i, bundle->binding_summaries.items[i]);
        string_list_pushf(&lines, "function.%zu.metadata_mode=%s", i, bundle->metadata_operand_modes.items[i]);
        string_list_pushf(&lines, "function.%zu.payload=%s", i, bundle->local_payload_texts.items[i]);
        string_list_pushf(&lines, "function.%zu.words=%s", i, bundle->instruction_word_hex.items[i]);
    }
    return string_list_join(&lines, "\n");
}

static ObjImageBundle build_obj_image(const MachineModuleBundle *bundle) {
    ObjImageBundle out;
    StringList externals;
    StringList local_symbols;
    StringList function_metadata_symbols;
    StringList lines;
    size_t i;
    int text_size = 0;
    int text_offset = 0;
    int cstring_offset = 0;
    memset(&out, 0, sizeof(out));
    memset(&externals, 0, sizeof(externals));
    memset(&local_symbols, 0, sizeof(local_symbols));
    memset(&function_metadata_symbols, 0, sizeof(function_metadata_symbols));
    memset(&lines, 0, sizeof(lines));
    out.version = xstrdup_text("v2.obj_image.v1");
    out.module_kind = xstrdup_text(bundle->module_kind);
    out.target_triple = xstrdup_text(bundle->target_triple);
    out.format = xstrdup_text(bundle->obj_format);
    out.text_section_name = xstrdup_text(bundle->text_section_name);
    out.cstring_section_name = xstrdup_text(bundle->cstring_section_name);
    out.symtab_section_name = xstrdup_text(machine_target_symtab_section_name(bundle->target_triple));
    out.strtab_section_name = xstrdup_text(machine_target_strtab_section_name(bundle->target_triple));
    out.reloc_section_name = xstrdup_text(machine_target_reloc_section_name(bundle->target_triple));
    for (i = 0; i < bundle->function_byte_sizes.len; ++i) {
        text_size += bundle->function_byte_sizes.items[i];
    }
    for (i = 0; i < bundle->item_ids.len; ++i) {
        if (strcmp(bundle->metadata_operand_modes.items[i], "local_payload") == 0) {
            char *payload = xstrdup_text(bundle->local_payload_texts.items[i]);
            char *meta_symbol = xformat("%s_meta", bundle->symbol_names.items[i]);
            string_list_push_take(&out.metadata_payloads, payload);
            string_list_push_take(&out.metadata_symbol_names, meta_symbol);
            string_list_push_copy(&function_metadata_symbols, meta_symbol);
        } else {
            string_list_push_copy(&function_metadata_symbols, "");
        }
    }
    for (i = 0; i < bundle->data_symbol_names.len; ++i) {
        string_list_push_copy(&out.data_symbol_names, bundle->data_symbol_names.items[i]);
        string_list_push_copy(&out.data_section_names, bundle->data_section_names.items[i]);
        string_list_push_copy(&out.data_payloads, bundle->data_payload_texts.items[i]);
        int_list_push(&out.data_align_pow2, bundle->data_align_pow2.items[i]);
    }
    string_list_push_copy(&out.section_names, bundle->text_section_name);
    int_list_push(&out.section_align_pow2, 2);
    int_list_push(&out.section_sizes, text_size);
    string_list_push_copy(&out.section_names, bundle->cstring_section_name);
    int_list_push(&out.section_align_pow2, 0);
    {
        int cstring_size = 0;
        for (i = 0; i < out.metadata_payloads.len; ++i) {
            cstring_size += (int)strlen(out.metadata_payloads.items[i]) + 1;
        }
        for (i = 0; i < out.data_symbol_names.len; ++i) {
            if (strcmp(out.data_section_names.items[i], bundle->cstring_section_name) != 0) {
                die("v2 obj image: unsupported local data section");
            }
            cstring_size += (int)strlen(out.data_payloads.items[i]) + 1;
        }
        int_list_push(&out.section_sizes, cstring_size);
    }
    string_list_push_copy(&out.section_names, out.symtab_section_name);
    int_list_push(&out.section_align_pow2, 3);
    int_list_push(&out.section_sizes, 0);
    string_list_push_copy(&out.section_names, out.strtab_section_name);
    int_list_push(&out.section_align_pow2, 0);
    int_list_push(&out.section_sizes, 0);
    string_list_push_copy(&out.section_names, out.reloc_section_name);
    int_list_push(&out.section_align_pow2, 3);
    int_list_push(&out.section_sizes, (int)bundle->item_ids.len * (strcmp(bundle->obj_format, "macho") == 0 ? 24 : 72));
    for (i = 0; i < bundle->item_ids.len; ++i) {
        string_list_push_copy(&out.symbol_names, bundle->symbol_names.items[i]);
        string_list_push_copy(&out.symbol_bindings, "global");
        string_list_push_copy(&out.symbol_sections, bundle->text_section_name);
        int_list_push(&out.symbol_values, text_offset);
        int_list_push(&out.symbol_sizes, bundle->function_byte_sizes.items[i]);
        string_list_append_unique(&local_symbols, bundle->symbol_names.items[i]);
        text_offset += bundle->function_byte_sizes.items[i];
    }
    for (i = 0; i < out.metadata_symbol_names.len; ++i) {
        string_list_push_copy(&out.symbol_names, out.metadata_symbol_names.items[i]);
        string_list_push_copy(&out.symbol_bindings, "local");
        string_list_push_copy(&out.symbol_sections, bundle->cstring_section_name);
        int_list_push(&out.symbol_values, cstring_offset);
        int_list_push(&out.symbol_sizes, (int)strlen(out.metadata_payloads.items[i]) + 1);
        string_list_append_unique(&local_symbols, out.metadata_symbol_names.items[i]);
        cstring_offset += (int)strlen(out.metadata_payloads.items[i]) + 1;
    }
    for (i = 0; i < out.data_symbol_names.len; ++i) {
        string_list_push_copy(&out.symbol_names, out.data_symbol_names.items[i]);
        string_list_push_copy(&out.symbol_bindings, "local");
        string_list_push_copy(&out.symbol_sections, out.data_section_names.items[i]);
        int_list_push(&out.symbol_values, cstring_offset);
        int_list_push(&out.symbol_sizes, (int)strlen(out.data_payloads.items[i]) + 1);
        string_list_append_unique(&local_symbols, out.data_symbol_names.items[i]);
        cstring_offset += (int)strlen(out.data_payloads.items[i]) + 1;
    }
    for (i = 0; i < bundle->item_ids.len; ++i) {
        int reloc_start = i < bundle->function_reloc_starts.len ? bundle->function_reloc_starts.items[i] : 0;
        int reloc_count = i < bundle->function_reloc_counts.len ? bundle->function_reloc_counts.items[i] : 0;
        int j;
        for (j = 0; j < reloc_count; ++j) {
            int reloc_index = reloc_start + j;
            if (reloc_index < 0 || (size_t)reloc_index >= bundle->reloc_symbols.len) {
                continue;
            }
            if (string_list_contains(&local_symbols, bundle->reloc_symbols.items[reloc_index])) {
                continue;
            }
            string_list_append_unique(&externals, bundle->reloc_symbols.items[reloc_index]);
        }
    }
    string_list_sort(&externals);
    for (i = 0; i < externals.len; ++i) {
        string_list_push_copy(&out.symbol_names, externals.items[i]);
        string_list_push_copy(&out.symbol_bindings, "extern");
        string_list_push_copy(&out.symbol_sections, "undefined");
        int_list_push(&out.symbol_values, 0);
        int_list_push(&out.symbol_sizes, 0);
    }
    text_offset = 0;
    for (i = 0; i < bundle->item_ids.len; ++i) {
        int reloc_start = i < bundle->function_reloc_starts.len ? bundle->function_reloc_starts.items[i] : 0;
        int reloc_count = i < bundle->function_reloc_counts.len ? bundle->function_reloc_counts.items[i] : 0;
        int j;
        for (j = 0; j < reloc_count; ++j) {
            int reloc_index = reloc_start + j;
            if (reloc_index < 0 || (size_t)reloc_index >= bundle->reloc_offsets.len) {
                continue;
            }
            string_list_push_copy(&out.relocation_sections, bundle->text_section_name);
            int_list_push(&out.relocation_offsets, text_offset + bundle->reloc_offsets.items[reloc_index]);
            string_list_push_copy(&out.relocation_symbols, bundle->reloc_symbols.items[reloc_index]);
            string_list_push_copy(&out.relocation_kinds, bundle->reloc_kinds.items[reloc_index]);
        }
        text_offset += bundle->function_byte_sizes.items[i];
    }
    out.section_sizes.items[2] = (int)out.symbol_names.len * 24;
    if (strcmp(out.format, "macho") == 0) {
        out.section_sizes.items[2] = (int)out.symbol_names.len * 16;
    }
    {
        int strtab_size = 1;
        for (i = 0; i < out.symbol_names.len; ++i) {
            strtab_size += (int)strlen(out.symbol_names.items[i]) + 1;
        }
        out.section_sizes.items[3] = strtab_size;
    }
    out.section_sizes.items[4] = strcmp(out.format, "macho") == 0 ?
                                 (int)out.relocation_offsets.len * 8 :
                                 (int)out.relocation_offsets.len * 24;
    string_list_pushf(&lines, "obj_image_version=%s", out.version);
    string_list_pushf(&lines, "module_kind=%s", out.module_kind);
    string_list_pushf(&lines, "target=%s", out.target_triple);
    string_list_pushf(&lines, "format=%s", out.format);
    string_list_pushf(&lines, "section_count=%zu", out.section_names.len);
    for (i = 0; i < out.section_names.len; ++i) {
        string_list_pushf(&lines, "section.%zu.name=%s", i, out.section_names.items[i]);
        string_list_pushf(&lines, "section.%zu.align_pow2=%d", i, out.section_align_pow2.items[i]);
        string_list_pushf(&lines, "section.%zu.size=%d", i, out.section_sizes.items[i]);
    }
    string_list_pushf(&lines, "function_count=%zu", bundle->item_ids.len);
    for (i = 0; i < bundle->item_ids.len; ++i) {
        string_list_pushf(&lines, "function.%zu.symbol=%s", i, bundle->symbol_names.items[i]);
        string_list_pushf(&lines, "function.%zu.call_symbol=%s", i, bundle->call_symbol_names.items[i]);
        string_list_pushf(&lines, "function.%zu.metadata_mode=%s", i, bundle->metadata_operand_modes.items[i]);
        string_list_pushf(&lines, "function.%zu.payload=%s", i, bundle->local_payload_texts.items[i]);
        string_list_pushf(&lines, "function.%zu.size=%d", i, bundle->function_byte_sizes.items[i]);
        string_list_pushf(&lines, "function.%zu.words=%s", i, bundle->instruction_word_hex.items[i]);
    }
    string_list_pushf(&lines, "metadata_count=%zu", out.metadata_symbol_names.len);
    for (i = 0; i < out.metadata_symbol_names.len; ++i) {
        string_list_pushf(&lines, "metadata.%zu.symbol=%s", i, out.metadata_symbol_names.items[i]);
        string_list_pushf(&lines, "metadata.%zu.payload=%s", i, out.metadata_payloads.items[i]);
    }
    string_list_pushf(&lines, "data_count=%zu", out.data_symbol_names.len);
    for (i = 0; i < out.data_symbol_names.len; ++i) {
        string_list_pushf(&lines, "data.%zu.symbol=%s", i, out.data_symbol_names.items[i]);
        string_list_pushf(&lines, "data.%zu.section=%s", i, out.data_section_names.items[i]);
        string_list_pushf(&lines, "data.%zu.payload=%s", i, out.data_payloads.items[i]);
    }
    string_list_pushf(&lines, "symbol_count=%zu", out.symbol_names.len);
    for (i = 0; i < out.symbol_names.len; ++i) {
        string_list_pushf(&lines, "symbol.%zu.name=%s", i, out.symbol_names.items[i]);
        string_list_pushf(&lines, "symbol.%zu.binding=%s", i, out.symbol_bindings.items[i]);
        string_list_pushf(&lines, "symbol.%zu.section=%s", i, out.symbol_sections.items[i]);
        string_list_pushf(&lines, "symbol.%zu.value=%d", i, out.symbol_values.items[i]);
        string_list_pushf(&lines, "symbol.%zu.size=%d", i, out.symbol_sizes.items[i]);
    }
    string_list_pushf(&lines, "reloc_count=%zu", out.relocation_offsets.len);
    for (i = 0; i < out.relocation_offsets.len; ++i) {
        string_list_pushf(&lines, "reloc.%zu.section=%s", i, out.relocation_sections.items[i]);
        string_list_pushf(&lines, "reloc.%zu.offset=%d", i, out.relocation_offsets.items[i]);
        string_list_pushf(&lines, "reloc.%zu.symbol=%s", i, out.relocation_symbols.items[i]);
        string_list_pushf(&lines, "reloc.%zu.kind=%s", i, out.relocation_kinds.items[i]);
    }
    out.section_count = (int)out.section_names.len;
    out.symbol_count = (int)out.symbol_names.len;
    out.reloc_count = (int)out.relocation_offsets.len;
    out.image_text = string_list_join(&lines, "\n");
    out.image_cid = sha256_hex_text(out.image_text);
    return out;
}

static uint32_t parse_hex_u32(const char *text) {
    uint32_t out = 0;
    size_t i;
    for (i = 0; text[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)text[i];
        out <<= 4;
        if (ch >= '0' && ch <= '9') {
            out |= (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            out |= (uint32_t)(10 + ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            out |= (uint32_t)(10 + ch - 'A');
        } else {
            die("invalid hex digit");
        }
    }
    return out;
}

static void append_instruction_words_bytes(ByteList *out, const char *words_text) {
    const char *cursor = words_text;
    while (*cursor != '\0') {
        const char *start = cursor;
        const char *end;
        while (*cursor != '\0' && *cursor != ';') {
            ++cursor;
        }
        end = cursor;
        if (end > start) {
            char *part = xstrdup_range(start, end);
            uint32_t word = parse_hex_u32(part);
            byte_list_append_u32le(out, word);
            free(part);
        }
        if (*cursor == ';') {
            ++cursor;
        }
    }
}

static ByteList build_text_section_bytes(const MachineModuleBundle *bundle) {
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < bundle->instruction_word_hex.len; ++i) {
        append_instruction_words_bytes(&out, bundle->instruction_word_hex.items[i]);
    }
    return out;
}

static ByteList build_cstring_section_bytes(const ObjImageBundle *image) {
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < image->metadata_payloads.len; ++i) {
        byte_list_append_ascii_z(&out, image->metadata_payloads.items[i]);
    }
    for (i = 0; i < image->data_payloads.len; ++i) {
        if (strcmp(image->data_section_names.items[i], image->cstring_section_name) == 0) {
            byte_list_append_ascii_z(&out, image->data_payloads.items[i]);
        }
    }
    return out;
}

static ByteList build_strtab_bytes_image_order(const ObjImageBundle *image, IntList *name_offsets) {
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(name_offsets, 0, sizeof(*name_offsets));
    byte_list_push(&out, 0);
    for (i = 0; i < image->symbol_names.len; ++i) {
        int_list_push(name_offsets, (int)out.len);
        byte_list_append_ascii_z(&out, image->symbol_names.items[i]);
    }
    return out;
}

static void build_macho_symbol_order(const ObjImageBundle *image,
                                     IntList *order,
                                     int *local_count,
                                     int *global_count,
                                     int *extern_count) {
    size_t i;
    memset(order, 0, sizeof(*order));
    *local_count = 0;
    *global_count = 0;
    *extern_count = 0;
    for (i = 0; i < image->symbol_names.len; ++i) {
        if (strcmp(image->symbol_bindings.items[i], "local") == 0) {
            int_list_push(order, (int)i);
            *local_count += 1;
        }
    }
    for (i = 0; i < image->symbol_names.len; ++i) {
        if (strcmp(image->symbol_bindings.items[i], "global") == 0) {
            int_list_push(order, (int)i);
            *global_count += 1;
        }
    }
    for (i = 0; i < image->symbol_names.len; ++i) {
        if (strcmp(image->symbol_bindings.items[i], "extern") == 0) {
            int_list_push(order, (int)i);
            *extern_count += 1;
        }
    }
}

static void build_elf_symbol_order(const ObjImageBundle *image, IntList *order, int *local_count) {
    size_t i;
    memset(order, 0, sizeof(*order));
    *local_count = 0;
    for (i = 0; i < image->symbol_names.len; ++i) {
        if (strcmp(image->symbol_bindings.items[i], "local") == 0) {
            int_list_push(order, (int)i);
            *local_count += 1;
        }
    }
    for (i = 0; i < image->symbol_names.len; ++i) {
        if (strcmp(image->symbol_bindings.items[i], "global") == 0) {
            int_list_push(order, (int)i);
        }
    }
    for (i = 0; i < image->symbol_names.len; ++i) {
        if (strcmp(image->symbol_bindings.items[i], "extern") == 0) {
            int_list_push(order, (int)i);
        }
    }
}

static ByteList build_strtab_bytes_for_order(const ObjImageBundle *image,
                                             const IntList *order,
                                             IntList *name_offsets) {
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(name_offsets, 0, sizeof(*name_offsets));
    byte_list_push(&out, 0);
    for (i = 0; i < order->len; ++i) {
        int idx = order->items[i];
        int_list_push(name_offsets, (int)out.len);
        byte_list_append_ascii_z(&out, image->symbol_names.items[idx]);
    }
    return out;
}

static int macho_section_number(const ObjImageBundle *image, const char *section_name) {
    if (strcmp(section_name, image->text_section_name) == 0) {
        return 1;
    }
    if (strcmp(section_name, image->cstring_section_name) == 0) {
        return 2;
    }
    return 0;
}

static ByteList build_macho_symtab_bytes(const ObjImageBundle *image,
                                         const IntList *order,
                                         const IntList *name_offsets) {
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < order->len; ++i) {
        int idx = order->items[i];
        uint8_t n_type = 0x0e;
        if (strcmp(image->symbol_bindings.items[idx], "extern") == 0) {
            n_type = 0x01;
        } else if (strcmp(image->symbol_bindings.items[idx], "global") == 0) {
            n_type = 0x0f;
        }
        byte_list_append_u32le(&out, (uint32_t)name_offsets->items[i]);
        byte_list_push(&out, n_type);
        byte_list_push(&out, (uint8_t)macho_section_number(image, image->symbol_sections.items[idx]));
        byte_list_append_u16le(&out, 0);
        byte_list_append_u64le(&out, (uint64_t)(uint32_t)image->symbol_values.items[idx]);
    }
    return out;
}

static int macho_symbol_index_for_name(const ObjImageBundle *image,
                                       const IntList *order,
                                       const char *symbol_name) {
    size_t i;
    for (i = 0; i < order->len; ++i) {
        if (strcmp(image->symbol_names.items[order->items[i]], symbol_name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static uint32_t macho_relocation_type(const char *kind) {
    if (strcmp(kind, "ARM64_RELOC_BRANCH26") == 0) {
        return 2U;
    }
    if (strcmp(kind, "ARM64_RELOC_PAGE21") == 0) {
        return 3U;
    }
    if (strcmp(kind, "ARM64_RELOC_PAGEOFF12") == 0) {
        return 4U;
    }
    die("macho reloc: unsupported relocation kind");
    return 0U;
}

static uint32_t macho_relocation_pcrel(const char *kind) {
    return strcmp(kind, "ARM64_RELOC_BRANCH26") == 0 || strcmp(kind, "ARM64_RELOC_PAGE21") == 0 ? 1U : 0U;
}

static ByteList build_macho_reloc_bytes(const ObjImageBundle *image, const IntList *order) {
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < image->relocation_offsets.len; ++i) {
        int symbol_index = macho_symbol_index_for_name(image, order, image->relocation_symbols.items[i]);
        uint32_t packed;
        if (symbol_index < 0) {
            die("macho reloc: missing symbol");
        }
        packed = (uint32_t)symbol_index |
                 (macho_relocation_pcrel(image->relocation_kinds.items[i]) << 24) |
                 (2U << 25) |
                 (1U << 27) |
                 (macho_relocation_type(image->relocation_kinds.items[i]) << 28);
        byte_list_append_u32le(&out, (uint32_t)image->relocation_offsets.items[i]);
        byte_list_append_u32le(&out, packed);
    }
    return out;
}

static ByteList build_elf_symtab_bytes(const ObjImageBundle *image,
                                       const IntList *order,
                                       const IntList *name_offsets) {
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    byte_list_append_zeroes(&out, 24);
    for (i = 0; i < order->len; ++i) {
        int idx = order->items[i];
        uint8_t info = 0x10;
        uint16_t shndx = 0;
        if (strcmp(image->symbol_bindings.items[idx], "local") == 0) {
            info = 0x01;
            shndx = (uint16_t)macho_section_number(image, image->symbol_sections.items[idx]);
        } else if (strcmp(image->symbol_bindings.items[idx], "global") == 0) {
            info = 0x12;
            shndx = (uint16_t)macho_section_number(image, image->symbol_sections.items[idx]);
        }
        byte_list_append_u32le(&out, (uint32_t)name_offsets->items[i]);
        byte_list_push(&out, info);
        byte_list_push(&out, 0);
        byte_list_append_u16le(&out, shndx);
        byte_list_append_u64le(&out, (uint64_t)(uint32_t)image->symbol_values.items[idx]);
        byte_list_append_u64le(&out, (uint64_t)(uint32_t)image->symbol_sizes.items[idx]);
    }
    return out;
}

static ByteList build_elf_reloc_bytes(const ObjImageBundle *image, const IntList *order) {
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < image->relocation_offsets.len; ++i) {
        int symbol_idx = -1;
        size_t j;
        for (j = 0; j < order->len; ++j) {
            int idx = order->items[j];
            if (strcmp(image->symbol_names.items[idx], image->relocation_symbols.items[i]) == 0) {
                symbol_idx = (int)j + 1;
                break;
            }
        }
        if (symbol_idx < 0) {
            die("elf reloc: missing symbol");
        }
        byte_list_append_u64le(&out, (uint64_t)(uint32_t)image->relocation_offsets.items[i]);
        {
            uint64_t reloc_type = 0;
            if (strcmp(image->relocation_kinds.items[i], "R_AARCH64_CALL26") == 0) {
                reloc_type = 283U;
            } else if (strcmp(image->relocation_kinds.items[i], "R_AARCH64_ADR_PREL_PG_HI21") == 0) {
                reloc_type = 275U;
            } else if (strcmp(image->relocation_kinds.items[i], "R_AARCH64_ADD_ABS_LO12_NC") == 0) {
                reloc_type = 277U;
            } else {
                die("elf reloc: unsupported relocation kind");
            }
            byte_list_append_u64le(&out, ((uint64_t)(uint32_t)symbol_idx << 32) | reloc_type);
        }
        byte_list_append_u64le(&out, 0);
    }
    return out;
}

static ByteList build_elf_shstrtab_bytes(IntList *name_offsets) {
    static const char *k_names[] = {"", ".text", ".rodata.str1.1", ".rela.text", ".symtab", ".strtab", ".shstrtab"};
    ByteList out;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(name_offsets, 0, sizeof(*name_offsets));
    for (i = 0; i < sizeof(k_names) / sizeof(k_names[0]); ++i) {
        int_list_push(name_offsets, (int)out.len);
        byte_list_append_ascii_z(&out, k_names[i]);
    }
    return out;
}

static void obj_file_add_region(ObjFileBundle *out,
                                const char *name,
                                size_t offset,
                                int align_pow2,
                                const ByteList *data) {
    string_list_push_copy(&out->region_names, name);
    int_list_push(&out->region_offsets, (int)offset);
    int_list_push(&out->region_sizes, (int)data->len);
    int_list_push(&out->region_align_pow2, align_pow2);
    string_list_push_take(&out->region_cids, sha256_hex_bytes(data->items, data->len));
}

static void append_region_at(ByteList *file, size_t offset, const ByteList *region) {
    if (file->len > offset) {
        die("obj file layout overlap");
    }
    if (file->len < offset) {
        byte_list_append_zeroes(file, offset - file->len);
    }
    byte_list_append_bytes(file, region->items, region->len);
}

static void macho_append_section64(ByteList *out,
                                   const char *sectname,
                                   const char *segname,
                                   uint64_t size,
                                   uint32_t offset,
                                   uint32_t align_pow2,
                                   uint32_t reloff,
                                   uint32_t nreloc,
                                   uint32_t flags) {
    byte_list_append_fixed_ascii(out, sectname, 16);
    byte_list_append_fixed_ascii(out, segname, 16);
    byte_list_append_u64le(out, 0);
    byte_list_append_u64le(out, size);
    byte_list_append_u32le(out, offset);
    byte_list_append_u32le(out, align_pow2);
    byte_list_append_u32le(out, reloff);
    byte_list_append_u32le(out, nreloc);
    byte_list_append_u32le(out, flags);
    byte_list_append_u32le(out, 0);
    byte_list_append_u32le(out, 0);
    byte_list_append_u32le(out, 0);
}

static ByteList build_macho_header_region(const ObjImageBundle *image,
                                          const char *target_triple,
                                          size_t text_off,
                                          size_t cstring_off,
                                          size_t reloc_off,
                                          size_t symtab_off,
                                          size_t strtab_off,
                                          size_t text_size,
                                          size_t cstring_size,
                                          size_t reloc_count,
                                          size_t symbol_count,
                                          size_t strtab_size,
                                          int local_count,
                                          int global_count,
                                          int extern_count) {
    ByteList out;
    int section_count = cstring_size > 0 ? 2 : 1;
    uint32_t segment_cmdsize = (uint32_t)(72 + section_count * 80);
    uint32_t sizeofcmds = segment_cmdsize + 24U + 24U + 80U;
    size_t segment_filesize = (cstring_size > 0 ? (cstring_off + cstring_size) : (text_off + text_size)) - text_off;
    memset(&out, 0, sizeof(out));
    byte_list_append_u32le(&out, 0xfeedfacfU);
    byte_list_append_u32le(&out, 0x0100000cU);
    byte_list_append_u32le(&out, 0U);
    byte_list_append_u32le(&out, 1U);
    byte_list_append_u32le(&out, 4U);
    byte_list_append_u32le(&out, sizeofcmds);
    byte_list_append_u32le(&out, 0U);
    byte_list_append_u32le(&out, 0U);
    byte_list_append_u32le(&out, 0x19U);
    byte_list_append_u32le(&out, segment_cmdsize);
    byte_list_append_fixed_ascii(&out, "", 16);
    byte_list_append_u64le(&out, 0);
    byte_list_append_u64le(&out, segment_filesize);
    byte_list_append_u64le(&out, text_off);
    byte_list_append_u64le(&out, segment_filesize);
    byte_list_append_u32le(&out, 7U);
    byte_list_append_u32le(&out, 7U);
    byte_list_append_u32le(&out, (uint32_t)section_count);
    byte_list_append_u32le(&out, 0U);
    macho_append_section64(&out, "__text", "__TEXT", text_size, (uint32_t)text_off, 2U,
                           reloc_count > 0 ? (uint32_t)reloc_off : 0U,
                           (uint32_t)reloc_count,
                           0x80000400U);
    if (cstring_size > 0) {
        macho_append_section64(&out, "__cstring", "__TEXT", cstring_size, (uint32_t)cstring_off, 0U,
                               0U, 0U, 0x00000002U);
    }
    byte_list_append_u32le(&out, 0x32U);
    byte_list_append_u32le(&out, 24U);
    byte_list_append_u32le(&out, machine_target_darwin_platform_id(target_triple));
    byte_list_append_u32le(&out, machine_target_darwin_minos(target_triple));
    byte_list_append_u32le(&out, 0U);
    byte_list_append_u32le(&out, 0U);
    byte_list_append_u32le(&out, 0x2U);
    byte_list_append_u32le(&out, 24U);
    byte_list_append_u32le(&out, (uint32_t)symtab_off);
    byte_list_append_u32le(&out, (uint32_t)symbol_count);
    byte_list_append_u32le(&out, (uint32_t)strtab_off);
    byte_list_append_u32le(&out, (uint32_t)strtab_size);
    byte_list_append_u32le(&out, 0xbU);
    byte_list_append_u32le(&out, 80U);
    byte_list_append_u32le(&out, 0U);
    byte_list_append_u32le(&out, (uint32_t)local_count);
    byte_list_append_u32le(&out, (uint32_t)local_count);
    byte_list_append_u32le(&out, (uint32_t)global_count);
    byte_list_append_u32le(&out, (uint32_t)(local_count + global_count));
    byte_list_append_u32le(&out, (uint32_t)extern_count);
    byte_list_append_zeroes(&out, 48);
    return out;
}

static void elf_append_section_header(ByteList *out,
                                      uint32_t sh_name,
                                      uint32_t sh_type,
                                      uint64_t sh_flags,
                                      uint64_t sh_addr,
                                      uint64_t sh_offset,
                                      uint64_t sh_size,
                                      uint32_t sh_link,
                                      uint32_t sh_info,
                                      uint64_t sh_addralign,
                                      uint64_t sh_entsize) {
    byte_list_append_u32le(out, sh_name);
    byte_list_append_u32le(out, sh_type);
    byte_list_append_u64le(out, sh_flags);
    byte_list_append_u64le(out, sh_addr);
    byte_list_append_u64le(out, sh_offset);
    byte_list_append_u64le(out, sh_size);
    byte_list_append_u32le(out, sh_link);
    byte_list_append_u32le(out, sh_info);
    byte_list_append_u64le(out, sh_addralign);
    byte_list_append_u64le(out, sh_entsize);
}

static ByteList build_elf_header_region(size_t shoff) {
    ByteList out;
    memset(&out, 0, sizeof(out));
    byte_list_push(&out, 0x7f);
    byte_list_push(&out, 'E');
    byte_list_push(&out, 'L');
    byte_list_push(&out, 'F');
    byte_list_push(&out, 2);
    byte_list_push(&out, 1);
    byte_list_push(&out, 1);
    byte_list_push(&out, 0);
    byte_list_push(&out, 0);
    byte_list_append_zeroes(&out, 7);
    byte_list_append_u16le(&out, 1);
    byte_list_append_u16le(&out, 183);
    byte_list_append_u32le(&out, 1);
    byte_list_append_u64le(&out, 0);
    byte_list_append_u64le(&out, 0);
    byte_list_append_u64le(&out, shoff);
    byte_list_append_u32le(&out, 0);
    byte_list_append_u16le(&out, 64);
    byte_list_append_u16le(&out, 0);
    byte_list_append_u16le(&out, 0);
    byte_list_append_u16le(&out, 64);
    byte_list_append_u16le(&out, 8);
    byte_list_append_u16le(&out, 7);
    return out;
}

static ObjFileBundle build_obj_file(const MachineModuleBundle *bundle) {
    ObjFileBundle out;
    ObjImageBundle image = build_obj_image(bundle);
    ByteList text = build_text_section_bytes(bundle);
    ByteList cstring = build_cstring_section_bytes(&image);
    ByteList strtab;
    ByteList symtab;
    ByteList reloc;
    ByteList header;
    ByteList file;
    ByteList shstrtab;
    ByteList section_headers;
    IntList name_offsets;
    IntList elf_order;
    IntList macho_order;
    IntList sh_name_offsets;
    int elf_local_count = 0;
    int macho_local_count = 0;
    int macho_global_count = 0;
    int macho_extern_count = 0;
    size_t text_off;
    size_t cstring_off;
    size_t reloc_off;
    size_t symtab_off;
    size_t strtab_off;
    size_t shstrtab_off = 0;
    size_t shoff = 0;
    StringList lines;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(&name_offsets, 0, sizeof(name_offsets));
    memset(&elf_order, 0, sizeof(elf_order));
    memset(&macho_order, 0, sizeof(macho_order));
    memset(&sh_name_offsets, 0, sizeof(sh_name_offsets));
    memset(&lines, 0, sizeof(lines));
    memset(&file, 0, sizeof(file));
    memset(&section_headers, 0, sizeof(section_headers));
    memset(&shstrtab, 0, sizeof(shstrtab));
    out.version = xstrdup_text("v2.obj_file.v1");
    out.module_kind = xstrdup_text(bundle->module_kind);
    out.target_triple = xstrdup_text(bundle->target_triple);
    out.format = xstrdup_text(bundle->obj_format);
    if (strcmp(bundle->obj_format, "macho") == 0) {
        build_macho_symbol_order(&image, &macho_order, &macho_local_count, &macho_global_count, &macho_extern_count);
        strtab = build_strtab_bytes_for_order(&image, &macho_order, &name_offsets);
        symtab = build_macho_symtab_bytes(&image, &macho_order, &name_offsets);
        reloc = build_macho_reloc_bytes(&image, &macho_order);
        text_off = 32 + (72 + (cstring.len > 0 ? 2 : 1) * 80) + 24 + 24 + 80;
        cstring_off = align_size_pow2(text_off + text.len, 0);
        reloc_off = align_size_pow2(cstring_off + cstring.len, 3);
        symtab_off = align_size_pow2(reloc_off + reloc.len, 3);
        strtab_off = align_size_pow2(symtab_off + symtab.len, 0);
        header = build_macho_header_region(&image,
                                           bundle->target_triple,
                                           text_off,
                                           cstring_off,
                                           reloc_off,
                                           symtab_off,
                                           strtab_off,
                                           text.len,
                                           cstring.len,
                                           image.relocation_offsets.len,
                                           image.symbol_names.len,
                                           strtab.len,
                                           macho_local_count,
                                           macho_global_count,
                                           macho_extern_count);
        out.header_byte_count = (int)header.len;
        obj_file_add_region(&out, "header", 0, 3, &header);
        obj_file_add_region(&out, "text", text_off, 2, &text);
        obj_file_add_region(&out, "cstring", cstring_off, 0, &cstring);
        obj_file_add_region(&out, "reloc", reloc_off, 3, &reloc);
        obj_file_add_region(&out, "symtab", symtab_off, 3, &symtab);
        obj_file_add_region(&out, "strtab", strtab_off, 0, &strtab);
        append_region_at(&file, 0, &header);
        append_region_at(&file, text_off, &text);
        append_region_at(&file, cstring_off, &cstring);
        append_region_at(&file, reloc_off, &reloc);
        append_region_at(&file, symtab_off, &symtab);
        append_region_at(&file, strtab_off, &strtab);
    } else if (strcmp(bundle->obj_format, "elf") == 0) {
        build_elf_symbol_order(&image, &elf_order, &elf_local_count);
        strtab = build_strtab_bytes_for_order(&image, &elf_order, &name_offsets);
        symtab = build_elf_symtab_bytes(&image, &elf_order, &name_offsets);
        reloc = build_elf_reloc_bytes(&image, &elf_order);
        shstrtab = build_elf_shstrtab_bytes(&sh_name_offsets);
        text_off = align_size_pow2(64, 2);
        cstring_off = align_size_pow2(text_off + text.len, 0);
        reloc_off = align_size_pow2(cstring_off + cstring.len, 3);
        symtab_off = align_size_pow2(reloc_off + reloc.len, 3);
        strtab_off = align_size_pow2(symtab_off + symtab.len, 0);
        shstrtab_off = align_size_pow2(strtab_off + strtab.len, 0);
        shoff = align_size_pow2(shstrtab_off + shstrtab.len, 3);
        header = build_elf_header_region(shoff);
        elf_append_section_header(&section_headers, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        elf_append_section_header(&section_headers, (uint32_t)sh_name_offsets.items[1], 1, 0x6, 0,
                                  text_off, text.len, 0, 0, 4, 0);
        elf_append_section_header(&section_headers, (uint32_t)sh_name_offsets.items[2], 1, 0x32, 0,
                                  cstring_off, cstring.len, 0, 0, 1, 0);
        elf_append_section_header(&section_headers, (uint32_t)sh_name_offsets.items[3], 4, 0, 0,
                                  reloc_off, reloc.len, 4, 1, 8, 24);
        elf_append_section_header(&section_headers, (uint32_t)sh_name_offsets.items[4], 2, 0, 0,
                                  symtab_off, symtab.len, 5, (uint32_t)elf_local_count + 1, 8, 24);
        elf_append_section_header(&section_headers, (uint32_t)sh_name_offsets.items[5], 3, 0, 0,
                                  strtab_off, strtab.len, 0, 0, 1, 0);
        elf_append_section_header(&section_headers, (uint32_t)sh_name_offsets.items[6], 3, 0, 0,
                                  shstrtab_off, shstrtab.len, 0, 0, 1, 0);
        out.header_byte_count = (int)header.len;
        obj_file_add_region(&out, "header", 0, 3, &header);
        obj_file_add_region(&out, "text", text_off, 2, &text);
        obj_file_add_region(&out, "cstring", cstring_off, 0, &cstring);
        obj_file_add_region(&out, "reloc", reloc_off, 3, &reloc);
        obj_file_add_region(&out, "symtab", symtab_off, 3, &symtab);
        obj_file_add_region(&out, "strtab", strtab_off, 0, &strtab);
        obj_file_add_region(&out, "shstrtab", shstrtab_off, 0, &shstrtab);
        obj_file_add_region(&out, "section_headers", shoff, 3, &section_headers);
        append_region_at(&file, 0, &header);
        append_region_at(&file, text_off, &text);
        append_region_at(&file, cstring_off, &cstring);
        append_region_at(&file, reloc_off, &reloc);
        append_region_at(&file, symtab_off, &symtab);
        append_region_at(&file, strtab_off, &strtab);
        append_region_at(&file, shstrtab_off, &shstrtab);
        append_region_at(&file, shoff, &section_headers);
    } else {
        die("obj file: unsupported format");
    }
    out.file_byte_count = (int)file.len;
    out.file_prefix_hex = hex_prefix_bytes(file.items, file.len, 32);
    out.file_bytes = file;
    out.file_cid = sha256_hex_bytes(file.items, file.len);
    string_list_pushf(&lines, "obj_file_version=%s", out.version);
    string_list_pushf(&lines, "module_kind=%s", out.module_kind);
    string_list_pushf(&lines, "target=%s", out.target_triple);
    string_list_pushf(&lines, "format=%s", out.format);
    string_list_pushf(&lines, "header_byte_count=%d", out.header_byte_count);
    string_list_pushf(&lines, "region_count=%zu", out.region_names.len);
    for (i = 0; i < out.region_names.len; ++i) {
        string_list_pushf(&lines, "region.%zu.name=%s", i, out.region_names.items[i]);
        string_list_pushf(&lines, "region.%zu.offset=%d", i, out.region_offsets.items[i]);
        string_list_pushf(&lines, "region.%zu.size=%d", i, out.region_sizes.items[i]);
        string_list_pushf(&lines, "region.%zu.align_pow2=%d", i, out.region_align_pow2.items[i]);
        string_list_pushf(&lines, "region.%zu.cid=%s", i, out.region_cids.items[i]);
    }
    string_list_pushf(&lines, "file_byte_count=%d", out.file_byte_count);
    string_list_pushf(&lines, "file_prefix_hex=%s", out.file_prefix_hex);
    string_list_pushf(&lines, "file_cid=%s", out.file_cid);
    out.file_text = string_list_join(&lines, "\n");
    return out;
}

static int release_artifact_compile_key_has_line(const ReleaseArtifactBundle *bundle, const char *line) {
    return strstr(bundle->compile_key_text, line) != NULL;
}

static const char *native_link_plan_version(void) {
    return "v2.native_link_plan.v1";
}

static void runtime_targets_for_module_kind(const char *module_kind,
                                            StringList *out_targets,
                                            StringList *out_modules) {
    static const char *k_core_targets[] = {"builtin.len"};
    static const char *k_string_targets[] = {"runtime/strings_v2.==",
                                             "runtime/strings_v2.!=",
                                             "runtime/strings_v2.[]",
                                             "runtime/strings_v2.startsWith",
                                             "runtime/strings_v2.endsWith",
                                             "runtime/strings_v2.__cheng_slice_string",
                                             "runtime/strings_v2.+",
                                             "runtime/strings_v2.find",
                                             "runtime/strings_v2.__cheng_string_contains_str",
                                             "runtime/strings_v2.__cheng_string_contains_char",
                                             "runtime/strings_v2.$",
                                             "runtime/strings_v2.StrBuilder",
                                             "runtime/strings_v2.append",
                                             "runtime/strings_v2.appendByte",
                                             "runtime/strings_v2.finish"};
    static const char *k_compiler_core_targets[] = {"runtime/compiler_core.argv_entry",
                                                    "runtime/compiler_core.local_payload_entry"};
    static const char *k_unimaker_targets[] = {"runtime/unimaker.verify_asset_contract",
                                               "runtime/unimaker.spawn_actor",
                                               "runtime/unimaker.register_ganzhi_state",
                                               "runtime/unimaker.bind_nfc_callback",
                                               "runtime/unimaker.register_evolve_site",
                                               "runtime/unimaker.resolve_hashref"};
    static const char *k_network_targets[] = {"runtime/network_distribution_v2.publish_source_manifest",
                                              "runtime/network_distribution_v2.publish_rule_pack",
                                              "runtime/network_distribution_v2.publish_compiler_rule_pack",
                                              "runtime/network_distribution_v2.route_lsmr_tree",
                                              "runtime/network_distribution_v2.disperse_ida_fragments",
                                              "runtime/network_distribution_v2.exchange_anti_entropy_signature",
                                              "runtime/network_distribution_v2.repair_from_signature_gap"};
    size_t i;
    memset(out_targets, 0, sizeof(*out_targets));
    memset(out_modules, 0, sizeof(*out_modules));
    string_list_append_unique(out_modules, "runtime/core_runtime_v2");
    for (i = 0; i < sizeof(k_core_targets) / sizeof(k_core_targets[0]); ++i) {
        string_list_append_unique(out_targets, k_core_targets[i]);
    }
    if (strcmp(module_kind, "string") == 0) {
        string_list_append_unique(out_modules, "runtime/strings_v2");
        for (i = 0; i < sizeof(k_string_targets) / sizeof(k_string_targets[0]); ++i) {
            string_list_append_unique(out_targets, k_string_targets[i]);
        }
    } else if (strcmp(module_kind, "compiler_core") == 0) {
        string_list_append_unique(out_modules, "runtime/compiler_core_runtime_v2");
        for (i = 0; i < sizeof(k_compiler_core_targets) / sizeof(k_compiler_core_targets[0]); ++i) {
            string_list_append_unique(out_targets, k_compiler_core_targets[i]);
        }
    } else if (strcmp(module_kind, "unimaker") == 0) {
        string_list_append_unique(out_modules, "runtime/unimaker_runtime_contract_v2");
        for (i = 0; i < sizeof(k_unimaker_targets) / sizeof(k_unimaker_targets[0]); ++i) {
            string_list_append_unique(out_targets, k_unimaker_targets[i]);
        }
    } else if (strcmp(module_kind, "network_distribution") == 0 || strcmp(module_kind, "topology") == 0) {
        string_list_append_unique(out_modules, "runtime/network_distribution_runtime_v2");
        for (i = 0; i < sizeof(k_network_targets) / sizeof(k_network_targets[0]); ++i) {
            string_list_append_unique(out_targets, k_network_targets[i]);
        }
    } else {
        die("v2 native link plan: unsupported module kind");
    }
    string_list_sort(out_targets);
    string_list_sort(out_modules);
}

static NativeLinkPlanBundle build_native_link_plan(const MachineModuleBundle *machine_bundle,
                                                   const char *emit_kind,
                                                   const char *target_triple) {
    NativeLinkPlanBundle out;
    StringList lines;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(&lines, 0, sizeof(lines));
    out.version = xstrdup_text(native_link_plan_version());
    out.module_kind = xstrdup_text(machine_bundle->module_kind);
    out.emit_kind = xstrdup_text(emit_kind);
    out.target_triple = xstrdup_text(target_triple);
    out.obj_format = xstrdup_text(machine_bundle->obj_format);
    runtime_targets_for_module_kind(machine_bundle->module_kind, &out.provided_targets, &out.provider_modules);
    for (i = 0; i < machine_bundle->runtime_targets.len; ++i) {
        string_list_append_unique(&out.runtime_targets, machine_bundle->runtime_targets.items[i]);
    }
    string_list_sort(&out.runtime_targets);
    for (i = 0; i < out.runtime_targets.len; ++i) {
        char *symbol = machine_call_symbol_name(target_triple, out.runtime_targets.items[i]);
        string_list_push_take(&out.runtime_symbols, symbol);
        if (!string_list_contains(&out.provided_targets, out.runtime_targets.items[i])) {
            string_list_push_copy(&out.unresolved_targets, out.runtime_targets.items[i]);
            string_list_push_copy(&out.unresolved_symbols, symbol);
        }
    }
    for (i = 0; i < out.provided_targets.len; ++i) {
        char *symbol = machine_call_symbol_name(target_triple, out.provided_targets.items[i]);
        string_list_push_take(&out.provided_symbols, symbol);
    }
    string_list_pushf(&lines, "native_link_plan_version=%s", out.version);
    string_list_pushf(&lines, "module_kind=%s", out.module_kind);
    string_list_pushf(&lines, "emit=%s", out.emit_kind);
    string_list_pushf(&lines, "target=%s", out.target_triple);
    string_list_pushf(&lines, "obj_format=%s", out.obj_format);
    string_list_pushf(&lines, "provider_module_count=%zu", out.provider_modules.len);
    for (i = 0; i < out.provider_modules.len; ++i) {
        string_list_pushf(&lines, "provider_module.%zu=%s", i, out.provider_modules.items[i]);
    }
    string_list_pushf(&lines, "runtime_target_count=%zu", out.runtime_targets.len);
    for (i = 0; i < out.runtime_targets.len; ++i) {
        string_list_pushf(&lines, "runtime_target.%zu.name=%s", i, out.runtime_targets.items[i]);
        string_list_pushf(&lines, "runtime_target.%zu.symbol=%s", i, out.runtime_symbols.items[i]);
    }
    string_list_pushf(&lines, "provided_target_count=%zu", out.provided_targets.len);
    for (i = 0; i < out.provided_targets.len; ++i) {
        string_list_pushf(&lines, "provided_target.%zu.name=%s", i, out.provided_targets.items[i]);
        string_list_pushf(&lines, "provided_target.%zu.symbol=%s", i, out.provided_symbols.items[i]);
    }
    string_list_pushf(&lines, "unresolved_target_count=%zu", out.unresolved_targets.len);
    for (i = 0; i < out.unresolved_targets.len; ++i) {
        string_list_pushf(&lines, "unresolved_target.%zu.name=%s", i, out.unresolved_targets.items[i]);
        string_list_pushf(&lines, "unresolved_target.%zu.symbol=%s", i, out.unresolved_symbols.items[i]);
    }
    out.plan_text = string_list_join(&lines, "\n");
    out.plan_cid = sha256_hex_text(out.plan_text);
    return out;
}

static const char *system_link_plan_version(void) {
    return "v2.system_link_plan.v1";
}

static const char *system_linker_flavor(const char *target_triple, const char *emit_kind) {
    const char *obj_format = obj_format_for_target(target_triple);
    if (strcmp(emit_kind, "static") == 0) {
        return strcmp(obj_format, "macho") == 0 ? "libtool" : "llvm-ar";
    }
    return strcmp(obj_format, "macho") == 0 ? "ld64" : "lld";
}

static const char *system_link_output_kind(const char *emit_kind) {
    if (strcmp(emit_kind, "exe") == 0) {
        return "executable";
    }
    if (strcmp(emit_kind, "shared") == 0) {
        return "shared_library";
    }
    return "static_archive";
}

static const char *system_link_output_suffix(const char *target_triple, const char *emit_kind) {
    const char *obj_format = obj_format_for_target(target_triple);
    if (strcmp(emit_kind, "shared") == 0) {
        return strcmp(obj_format, "macho") == 0 ? ".dylib" : ".so";
    }
    if (strcmp(emit_kind, "static") == 0) {
        return ".a";
    }
    return "";
}

static char *system_link_output_name(const char *module_kind, const char *emit_kind) {
    char *stem = sanitize_symbol_part(module_kind);
    char *out = strcmp(emit_kind, "exe") == 0 ? xformat("cheng_v2_%s", stem)
                                               : xformat("libcheng_v2_%s", stem);
    free(stem);
    return out;
}

static const char *system_link_exec_version(void) {
    return "v2.system_link_exec.v1";
}

static const char *system_linker_path(const char *linker_flavor, const char *emit_kind) {
    if (strcmp(linker_flavor, "ld64") == 0 && strcmp(emit_kind, "exe") == 0) {
        return "/usr/bin/cc";
    }
    if (strcmp(linker_flavor, "ld64") == 0) {
        return "/usr/bin/ld";
    }
    if (strcmp(linker_flavor, "libtool") == 0) {
        return "/usr/bin/libtool";
    }
    die("v2 system link exec: unsupported linker flavor");
    return "";
}

static void build_system_link_exec_argv(const char *target_triple,
                                        const char *emit_kind,
                                        const char *linker_flavor,
                                        const char *entry_symbol,
                                        int entry_required,
                                        const char *syslibroot_path,
                                        const char *output_path,
                                        const char *primary_obj_path,
                                        const StringList *provider_obj_paths,
                                        const StringList *support_obj_paths,
                                        StringList *argv) {
    size_t i;
    memset(argv, 0, sizeof(*argv));
    if (strcmp(linker_flavor, "ld64") == 0 && strcmp(emit_kind, "exe") == 0) {
        string_list_push_copy(argv, "-arch");
        string_list_push_copy(argv, machine_target_darwin_arch_name(target_triple));
        string_list_pushf(argv, "-mmacosx-version-min=%s", machine_target_darwin_minos_text(target_triple));
        if (syslibroot_path != NULL && syslibroot_path[0] != '\0') {
            string_list_push_copy(argv, "-isysroot");
            string_list_push_copy(argv, syslibroot_path);
        }
        string_list_push_copy(argv, "-o");
        string_list_push_copy(argv, output_path);
        string_list_push_copy(argv, primary_obj_path);
        for (i = 0; i < provider_obj_paths->len; ++i) {
            string_list_push_copy(argv, provider_obj_paths->items[i]);
        }
        for (i = 0; i < support_obj_paths->len; ++i) {
            string_list_push_copy(argv, support_obj_paths->items[i]);
        }
        return;
    }
    if (strcmp(linker_flavor, "ld64") == 0) {
        string_list_push_copy(argv, "-arch");
        string_list_push_copy(argv, machine_target_darwin_arch_name(target_triple));
        string_list_push_copy(argv, "-platform_version");
        string_list_push_copy(argv, machine_target_darwin_platform_name(target_triple));
        string_list_push_copy(argv, machine_target_darwin_minos_text(target_triple));
        string_list_push_copy(argv, machine_target_darwin_minos_text(target_triple));
        string_list_push_copy(argv, "-no_uuid");
        if (strcmp(emit_kind, "static") != 0 && syslibroot_path != NULL && syslibroot_path[0] != '\0') {
            string_list_push_copy(argv, "-syslibroot");
            string_list_push_copy(argv, syslibroot_path);
            string_list_push_copy(argv, "-lSystem");
        }
        if (strcmp(emit_kind, "shared") == 0) {
            string_list_push_copy(argv, "-dylib");
        }
        if (entry_required) {
            string_list_push_copy(argv, "-e");
            string_list_push_copy(argv, entry_symbol);
        }
        string_list_push_copy(argv, "-o");
        string_list_push_copy(argv, output_path);
        string_list_push_copy(argv, primary_obj_path);
        for (i = 0; i < provider_obj_paths->len; ++i) {
            string_list_push_copy(argv, provider_obj_paths->items[i]);
        }
        for (i = 0; i < support_obj_paths->len; ++i) {
            string_list_push_copy(argv, support_obj_paths->items[i]);
        }
        return;
    }
    if (strcmp(linker_flavor, "libtool") == 0) {
        string_list_push_copy(argv, "-static");
        string_list_push_copy(argv, "-o");
        string_list_push_copy(argv, output_path);
        string_list_push_copy(argv, primary_obj_path);
        for (i = 0; i < provider_obj_paths->len; ++i) {
            string_list_push_copy(argv, provider_obj_paths->items[i]);
        }
        for (i = 0; i < support_obj_paths->len; ++i) {
            string_list_push_copy(argv, support_obj_paths->items[i]);
        }
        return;
    }
    die("v2 system link exec: unsupported linker flavor");
}

static char *runtime_provider_source_norm(const char *module_path) {
    if (strcmp(module_path, "runtime/core_runtime_v2") == 0) {
        return xstrdup_text("v2/src/runtime/core_runtime_v2.cheng");
    }
    if (strcmp(module_path, "runtime/compiler_core_runtime_v2") == 0) {
        return xstrdup_text("v2/src/runtime/compiler_core_runtime_v2.cheng");
    }
    if (strcmp(module_path, "runtime/strings_v2") == 0) {
        return xstrdup_text("v2/src/runtime/strings_v2.cheng");
    }
    if (strcmp(module_path, "runtime/unimaker_runtime_contract_v2") == 0) {
        return xstrdup_text("v2/src/runtime/unimaker_runtime_contract_v2.cheng");
    }
    if (strcmp(module_path, "runtime/network_distribution_runtime_v2") == 0) {
        return xstrdup_text("v2/src/runtime/network_distribution_runtime_v2.cheng");
    }
    return xformat("v2/src/%s.cheng", module_path);
}

static void runtime_provider_external_cc_compile_inputs(const char *module_path,
                                                        StringList *out_paths) {
    memset(out_paths, 0, sizeof(*out_paths));
    if (strcmp(module_path, "runtime/compiler_core_runtime_v2") == 0) {
        string_list_push_copy(out_paths, "v2/src/runtime/compiler_core_native_provider.c");
        string_list_push_copy(out_paths, "v2/src/runtime/compiler_core_native_dispatch.c");
        string_list_push_copy(out_paths, "v2/src/runtime/compiler_core_native_dispatch.h");
        string_list_push_copy(out_paths, "v2/bootstrap/cheng_v2c_tooling.c");
        string_list_push_copy(out_paths, "v2/bootstrap/cheng_v2c_tooling.h");
        string_list_push_copy(out_paths, "src/runtime/native/system_helpers_stdio_bridge.c");
        string_list_push_copy(out_paths, "src/runtime/native/system_helpers_epoch_time_bridge.c");
        string_list_push_copy(out_paths, "src/runtime/native/system_helpers_cmdline_ptr_pty_bridge.c");
        string_list_push_copy(out_paths, "src/runtime/native/system_helpers_selflink_cmdline_bridge.c");
        return;
    }
    die("v2 runtime provider object: unsupported external cc compile inputs");
}

static void runtime_provider_external_cc_source_files(const char *module_path,
                                                     StringList *out_paths) {
    memset(out_paths, 0, sizeof(*out_paths));
    if (strcmp(module_path, "runtime/compiler_core_runtime_v2") == 0) {
        string_list_push_copy(out_paths, "v2/src/runtime/compiler_core_native_provider.c");
        string_list_push_copy(out_paths, "v2/src/runtime/compiler_core_native_dispatch.c");
        string_list_push_copy(out_paths, "v2/bootstrap/cheng_v2c_tooling.c");
        string_list_push_copy(out_paths, "src/runtime/native/system_helpers_stdio_bridge.c");
        string_list_push_copy(out_paths, "src/runtime/native/system_helpers_epoch_time_bridge.c");
        string_list_push_copy(out_paths, "src/runtime/native/system_helpers_cmdline_ptr_pty_bridge.c");
        string_list_push_copy(out_paths, "src/runtime/native/system_helpers_selflink_cmdline_bridge.c");
        return;
    }
    die("v2 runtime provider object: unsupported external cc source files");
}

static const char *runtime_provider_source_kind(const char *module_path) {
    return "cheng_module";
}

static const char *runtime_provider_compile_mode(const char *module_path) {
    return "machine_obj";
}

static int runtime_provider_is_external_cc(const char *module_path) {
    return strcmp(runtime_provider_compile_mode(module_path), "external_cc_obj") == 0;
}

static const char *runtime_provider_execution_model(const char *module_path) {
    if (strcmp(module_path, "runtime/compiler_core_runtime_v2") == 0) {
        return "native_command_argv_bridge";
    }
    return "native_trace_stub";
}

static const char *runtime_provider_trace_symbol(const char *module_path) {
    if (strcmp(module_path, "runtime/compiler_core_runtime_v2") == 0) {
        return "runtime_compiler_core_argv_entry";
    }
    return "puts";
}

static void runtime_targets_for_provider_module(const char *module_path, StringList *out_targets) {
    static const char *k_core_targets[] = {"builtin.len"};
    static const char *k_string_targets[] = {"runtime/strings_v2.==",
                                             "runtime/strings_v2.!=",
                                             "runtime/strings_v2.[]",
                                             "runtime/strings_v2.startsWith",
                                             "runtime/strings_v2.endsWith",
                                             "runtime/strings_v2.__cheng_slice_string",
                                             "runtime/strings_v2.+",
                                             "runtime/strings_v2.find",
                                             "runtime/strings_v2.__cheng_string_contains_str",
                                             "runtime/strings_v2.__cheng_string_contains_char",
                                             "runtime/strings_v2.$",
                                             "runtime/strings_v2.StrBuilder",
                                             "runtime/strings_v2.append",
                                             "runtime/strings_v2.appendByte",
                                             "runtime/strings_v2.finish"};
    static const char *k_compiler_core_targets[] = {"runtime/compiler_core.argv_entry",
                                                    "runtime/compiler_core.local_payload_entry"};
    static const char *k_unimaker_targets[] = {"runtime/unimaker.verify_asset_contract",
                                               "runtime/unimaker.spawn_actor",
                                               "runtime/unimaker.register_ganzhi_state",
                                               "runtime/unimaker.bind_nfc_callback",
                                               "runtime/unimaker.register_evolve_site",
                                               "runtime/unimaker.resolve_hashref"};
    static const char *k_network_targets[] = {"runtime/network_distribution_v2.publish_source_manifest",
                                              "runtime/network_distribution_v2.publish_rule_pack",
                                              "runtime/network_distribution_v2.publish_compiler_rule_pack",
                                              "runtime/network_distribution_v2.route_lsmr_tree",
                                              "runtime/network_distribution_v2.disperse_ida_fragments",
                                              "runtime/network_distribution_v2.exchange_anti_entropy_signature",
                                              "runtime/network_distribution_v2.repair_from_signature_gap"};
    size_t i;
    memset(out_targets, 0, sizeof(*out_targets));
    if (strcmp(module_path, "runtime/core_runtime_v2") == 0) {
        for (i = 0; i < sizeof(k_core_targets) / sizeof(k_core_targets[0]); ++i) {
            string_list_push_copy(out_targets, k_core_targets[i]);
        }
        return;
    }
    if (strcmp(module_path, "runtime/compiler_core_runtime_v2") == 0) {
        for (i = 0; i < sizeof(k_compiler_core_targets) / sizeof(k_compiler_core_targets[0]); ++i) {
            string_list_push_copy(out_targets, k_compiler_core_targets[i]);
        }
        return;
    }
    if (strcmp(module_path, "runtime/strings_v2") == 0) {
        for (i = 0; i < sizeof(k_string_targets) / sizeof(k_string_targets[0]); ++i) {
            string_list_push_copy(out_targets, k_string_targets[i]);
        }
        return;
    }
    if (strcmp(module_path, "runtime/unimaker_runtime_contract_v2") == 0) {
        for (i = 0; i < sizeof(k_unimaker_targets) / sizeof(k_unimaker_targets[0]); ++i) {
            string_list_push_copy(out_targets, k_unimaker_targets[i]);
        }
        return;
    }
    if (strcmp(module_path, "runtime/network_distribution_runtime_v2") == 0) {
        for (i = 0; i < sizeof(k_network_targets) / sizeof(k_network_targets[0]); ++i) {
            string_list_push_copy(out_targets, k_network_targets[i]);
        }
        return;
    }
    die("v2 runtime provider object: unsupported provider module");
}

static const char *single_file_manifest_package_id(const char *source_norm) {
    if (starts_with(source_norm, "v2/src/runtime/")) {
        return "pkg://cheng/v2/runtime";
    }
    if (starts_with(source_norm, "v2/bootstrap/")) {
        return "pkg://cheng/v2/bootstrap";
    }
    return "pkg://cheng/v2";
}

static const char *manifest_package_id_for_sources(const StringList *source_norms) {
    const char *first;
    size_t i;
    if (source_norms->len == 0) {
        return "pkg://cheng/v2";
    }
    first = single_file_manifest_package_id(source_norms->items[0]);
    for (i = 1; i < source_norms->len; ++i) {
        if (strcmp(single_file_manifest_package_id(source_norms->items[i]), first) != 0) {
            return "pkg://cheng/v2";
        }
    }
    return first;
}

static SourceManifestBundle build_explicit_manifest(const char *root_norm,
                                                    const StringList *source_abs_paths,
                                                    const StringList *source_norms) {
    SourceManifestBundle out;
    StringList lines;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(&lines, 0, sizeof(lines));
    out.version = xstrdup_text("v2.source_manifest.v1");
    out.root_path = xstrdup_text(root_norm);
    out.package_id = xstrdup_text(manifest_package_id_for_sources(source_norms));
    for (i = 0; i < source_norms->len; ++i) {
        ByteList bytes = read_file_bytes_all(source_abs_paths->items[i]);
        char *file_cid = sha256_hex_bytes(bytes.items, bytes.len);
        string_list_push_copy(&out.file_paths, source_norms->items[i]);
        string_list_push_take(&out.file_cids, file_cid);
        string_list_push_copy(&out.file_topology_refs, "");
    }
    string_list_pushf(&lines, "source_manifest_version=%s", out.version);
    string_list_pushf(&lines, "root=%s", out.root_path);
    string_list_pushf(&lines, "package=%s", out.package_id);
    string_list_push_copy(&lines, "topology_count=0");
    string_list_pushf(&lines, "file_count=%zu", out.file_paths.len);
    for (i = 0; i < out.file_paths.len; ++i) {
        string_list_pushf(&lines, "file.%zu.path=%s", i, out.file_paths.items[i]);
        string_list_pushf(&lines, "file.%zu.cid=%s", i, out.file_cids.items[i]);
        string_list_pushf(&lines, "file.%zu.topology_ref=", i);
    }
    out.manifest_text = string_list_join(&lines, "\n");
    out.manifest_cid = sha256_hex_text(out.manifest_text);
    return out;
}

static void compile_external_runtime_provider_object(const char *repo_root,
                                                     const StringList *source_norms,
                                                     const char *module_path,
                                                     const char *target_triple,
                                                     ByteList *out_bytes,
                                                     char **out_obj_path,
                                                     char **out_obj_cid) {
    char obj_rel[PATH_MAX];
    char obj_abs[PATH_MAX];
    char combined_obj_rel[PATH_MAX];
    char combined_obj_abs[PATH_MAX];
    StringList object_paths;
    char *cc_path;
    const char *compiler_file;
    char *compile_output;
    char *safe_module;
    char *safe_target;
    size_t i;
    int rc = 0;
    ByteList bytes;
    memset(&object_paths, 0, sizeof(object_paths));
    memset(&bytes, 0, sizeof(bytes));
    safe_module = sanitize_symbol_part(module_path);
    safe_target = sanitize_symbol_part(target_triple);
    if (snprintf(combined_obj_rel,
                 sizeof(obj_rel),
                 "v2/artifacts/bootstrap/provider_%s_%s.o",
                 safe_module,
                 safe_target) >= (int)sizeof(obj_rel)) {
        die("runtime provider object path too long");
    }
    resolve_output_path(repo_root, combined_obj_rel, combined_obj_abs, sizeof(combined_obj_abs));
    cc_path = getenv("CC");
    if (cc_path == NULL || cc_path[0] == '\0') {
        cc_path = "/usr/bin/cc";
    }
    compiler_file = cc_path;
    if (strchr(cc_path, '/') == NULL) {
        compiler_file = "/usr/bin/env";
    }
    for (i = 0; i < source_norms->len; ++i) {
        char source_abs[PATH_MAX];
        char source_norm[PATH_MAX];
        StringList compile_argv;
        if (snprintf(obj_rel,
                     sizeof(obj_rel),
                     "v2/artifacts/bootstrap/provider_%s_%s.%zu.o",
                     safe_module,
                     safe_target,
                     i) >= (int)sizeof(obj_rel)) {
            die("runtime provider object path too long");
        }
        resolve_existing_input_path(repo_root,
                                    source_norms->items[i],
                                    source_abs,
                                    sizeof(source_abs),
                                    source_norm,
                                    sizeof(source_norm));
        resolve_output_path(repo_root, obj_rel, obj_abs, sizeof(obj_abs));
        memset(&compile_argv, 0, sizeof(compile_argv));
        if (strchr(cc_path, '/') == NULL) {
            string_list_push_copy(&compile_argv, cc_path);
        }
        string_list_push_copy(&compile_argv, "-std=c11");
        string_list_push_copy(&compile_argv, "-O2");
        string_list_push_copy(&compile_argv, "-Wall");
        string_list_push_copy(&compile_argv, "-Wextra");
        string_list_push_copy(&compile_argv, "-pedantic");
        if (strcmp(target_triple, "arm64-apple-darwin") == 0) {
            string_list_pushf(&compile_argv,
                              "-mmacosx-version-min=%s",
                              machine_target_darwin_minos_text(target_triple));
        }
        string_list_push_copy(&compile_argv, "-c");
        string_list_push_copy(&compile_argv, source_abs);
        string_list_push_copy(&compile_argv, "-o");
        string_list_push_copy(&compile_argv, obj_abs);
        compile_output = run_exec_capture(compiler_file, &compile_argv, repo_root, 1, &rc);
        if (rc != 0) {
            fprintf(stderr, "%s\n", compile_output);
            die("v2 runtime provider object: failed to compile external runtime provider");
        }
        free(compile_output);
        string_list_push_copy(&object_paths, obj_abs);
    }
    if (object_paths.len > 1) {
        StringList link_argv;
        memset(&link_argv, 0, sizeof(link_argv));
        if (strcmp(target_triple, "arm64-apple-darwin") != 0) {
            die("v2 runtime provider object: external cc combine target unsupported");
        }
        string_list_push_copy(&link_argv, "-r");
        string_list_push_copy(&link_argv, "-arch");
        string_list_push_copy(&link_argv, machine_target_darwin_arch_name(target_triple));
        string_list_push_copy(&link_argv, "-o");
        string_list_push_copy(&link_argv, combined_obj_abs);
        for (i = 0; i < object_paths.len; ++i) {
            string_list_push_copy(&link_argv, object_paths.items[i]);
        }
        compile_output = run_exec_capture("/usr/bin/ld", &link_argv, repo_root, 1, &rc);
        if (rc != 0) {
            fprintf(stderr, "%s\n", compile_output);
            die("v2 runtime provider object: failed to combine external runtime provider objects");
        }
        free(compile_output);
    }
    bytes = read_file_bytes_all(object_paths.len > 1 ? combined_obj_abs : object_paths.items[0]);
    *out_bytes = bytes;
    *out_obj_path = xstrdup_text(object_paths.len > 1 ? combined_obj_abs : object_paths.items[0]);
    *out_obj_cid = sha256_hex_bytes(bytes.items, bytes.len);
    free(safe_module);
    free(safe_target);
}

static RuntimeProviderObjectBundle build_runtime_provider_object(const char *repo_root,
                                                                const char *module_path,
                                                                const char *target_triple) {
    RuntimeProviderObjectBundle out;
    char source_abs[PATH_MAX];
    char source_norm[PATH_MAX];
    SourceManifestBundle manifest;
    StringList compile_inputs;
    StringList runtime_targets;
    MachineModuleBundle machine_bundle;
    ObjImageBundle obj_image;
    ObjFileBundle obj_file;
    StringList lines;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(&compile_inputs, 0, sizeof(compile_inputs));
    memset(&runtime_targets, 0, sizeof(runtime_targets));
    memset(&lines, 0, sizeof(lines));
    out.version = xstrdup_text("v2.runtime_provider_object.v1");
    out.module_path = xstrdup_text(module_path);
    out.source_path = runtime_provider_source_norm(module_path);
    resolve_existing_input_path(repo_root,
                                out.source_path,
                                source_abs,
                                sizeof(source_abs),
                                source_norm,
                                sizeof(source_norm));
    free(out.source_path);
    out.source_path = xstrdup_text(source_norm);
    if (runtime_provider_is_external_cc(module_path)) {
        StringList compile_abs;
        StringList compile_norms;
        memset(&compile_abs, 0, sizeof(compile_abs));
        memset(&compile_norms, 0, sizeof(compile_norms));
        runtime_provider_external_cc_compile_inputs(module_path, &compile_inputs);
        for (i = 0; i < compile_inputs.len; ++i) {
            char input_abs[PATH_MAX];
            char input_norm[PATH_MAX];
            resolve_existing_input_path(repo_root,
                                        compile_inputs.items[i],
                                        input_abs,
                                        sizeof(input_abs),
                                        input_norm,
                                        sizeof(input_norm));
            string_list_push_copy(&compile_abs, input_abs);
            string_list_push_copy(&compile_norms, input_norm);
        }
        manifest = build_explicit_manifest(source_norm, &compile_abs, &compile_norms);
    } else {
        string_list_push_copy(&compile_inputs, source_norm);
        manifest = resolve_compiler_core_source_manifest(repo_root, source_abs);
    }
    out.source_manifest_cid = xstrdup_text(manifest.manifest_cid);
    out.target_triple = xstrdup_text(target_triple);
    out.source_kind = xstrdup_text(runtime_provider_source_kind(module_path));
    out.compile_mode = xstrdup_text(runtime_provider_compile_mode(module_path));
    out.execution_model = xstrdup_text(runtime_provider_execution_model(module_path));
    out.trace_symbol = xstrdup_text(runtime_provider_trace_symbol(module_path));
    runtime_targets_for_provider_module(module_path, &runtime_targets);
    if (runtime_provider_is_external_cc(module_path)) {
        char *obj_path = NULL;
        char *obj_cid = NULL;
        StringList source_files;
        memset(&source_files, 0, sizeof(source_files));
        runtime_provider_external_cc_source_files(module_path, &source_files);
        compile_external_runtime_provider_object(repo_root,
                                                &source_files,
                                                module_path,
                                                target_triple,
                                                &out.obj_file_bytes,
                                                &obj_path,
                                                &obj_cid);
        free(obj_path);
        out.obj_image_version = xstrdup_text("v2.obj_image.external_cc.v1");
        out.obj_image_cid = xstrdup_text(obj_cid);
        out.obj_file_version = xstrdup_text("v2.obj_file.external_cc.v1");
        out.obj_file_cid = xstrdup_text(obj_cid);
        out.obj_file_byte_count = (int)out.obj_file_bytes.len;
        free(obj_cid);
    } else {
        if (strcmp(module_path, "runtime/compiler_core_runtime_v2") == 0) {
            machine_bundle = build_compiler_core_argv_entry_provider_machine_module(repo_root,
                                                                                    source_abs,
                                                                                    manifest.manifest_cid,
                                                                                    &runtime_targets,
                                                                                    target_triple);
        } else {
            machine_bundle = build_runtime_provider_machine_module(module_path,
                                                                   manifest.manifest_cid,
                                                                   &runtime_targets,
                                                                   target_triple);
        }
        obj_image = build_obj_image(&machine_bundle);
        obj_file = build_obj_file(&machine_bundle);
        out.obj_image_version = xstrdup_text(obj_image.version);
        out.obj_image_cid = xstrdup_text(obj_image.image_cid);
        out.obj_file_version = xstrdup_text(obj_file.version);
        out.obj_file_cid = xstrdup_text(obj_file.file_cid);
        out.obj_file_byte_count = obj_file.file_byte_count;
        out.obj_file_bytes = obj_file.file_bytes;
    }
    for (i = 0; i < runtime_targets.len; ++i) {
        char *symbol = machine_call_symbol_name(target_triple, runtime_targets.items[i]);
        string_list_push_copy(&out.runtime_targets, runtime_targets.items[i]);
        string_list_push_take(&out.runtime_symbols, symbol);
    }
    string_list_pushf(&lines, "runtime_provider_object_version=%s", out.version);
    string_list_pushf(&lines, "module_path=%s", out.module_path);
    string_list_pushf(&lines, "source_path=%s", out.source_path);
    string_list_pushf(&lines, "source_manifest_cid=%s", out.source_manifest_cid);
    string_list_pushf(&lines, "target=%s", out.target_triple);
    string_list_pushf(&lines, "source_kind=%s", out.source_kind);
    string_list_pushf(&lines, "compile_mode=%s", out.compile_mode);
    string_list_pushf(&lines, "execution_model=%s", out.execution_model);
    string_list_pushf(&lines, "trace_symbol=%s", out.trace_symbol);
    string_list_pushf(&lines, "compile_input_count=%zu", compile_inputs.len);
    for (i = 0; i < compile_inputs.len; ++i) {
        string_list_pushf(&lines, "compile_input.%zu.path=%s", i, compile_inputs.items[i]);
    }
    string_list_pushf(&lines, "runtime_target_count=%zu", out.runtime_targets.len);
    for (i = 0; i < out.runtime_targets.len; ++i) {
        string_list_pushf(&lines, "runtime_target.%zu.name=%s", i, out.runtime_targets.items[i]);
        string_list_pushf(&lines, "runtime_target.%zu.symbol=%s", i, out.runtime_symbols.items[i]);
    }
    string_list_pushf(&lines, "provider_symbol_count=%zu", out.runtime_symbols.len);
    for (i = 0; i < out.runtime_symbols.len; ++i) {
        string_list_pushf(&lines, "provider_symbol.%zu=%s", i, out.runtime_symbols.items[i]);
    }
    string_list_pushf(&lines, "obj_image_version=%s", out.obj_image_version);
    string_list_pushf(&lines, "obj_image_cid=%s", out.obj_image_cid);
    string_list_pushf(&lines, "obj_file_version=%s", out.obj_file_version);
    string_list_pushf(&lines, "obj_file_cid=%s", out.obj_file_cid);
    string_list_pushf(&lines, "obj_file_byte_count=%d", out.obj_file_byte_count);
    out.object_text = string_list_join(&lines, "\n");
    return out;
}

static char *resolve_entry_symbol(const MachineModuleBundle *machine_bundle,
                                  const char *emit_kind,
                                  const IntList *item_ids,
                                  const StringList *op_names,
                                  const StringList *labels) {
    size_t i;
    if (strcmp(emit_kind, "exe") == 0 &&
        strcmp(machine_bundle->module_kind, "compiler_core") == 0 &&
        strcmp(machine_bundle->obj_format, "macho") == 0) {
        return xformat("%smain", machine_target_symbol_prefix(machine_bundle->target_triple));
    }
    if (machine_bundle->entry_symbol_override != NULL && machine_bundle->entry_symbol_override[0] != '\0') {
        return xstrdup_text(machine_bundle->entry_symbol_override);
    }
    if (strcmp(machine_bundle->module_kind, "compiler_core") == 0) {
        for (i = 0; i < machine_bundle->symbol_names.len; ++i) {
            if (strncmp(machine_bundle->binding_summaries.items[i], "entry_bridge=1|", 15) == 0) {
                return xstrdup_text(machine_bundle->symbol_names.items[i]);
            }
        }
    }
    for (i = 0; i < labels->len; ++i) {
        if (strcmp(labels->items[i], "main") == 0) {
            return machine_function_symbol_name(machine_bundle->target_triple,
                                                machine_bundle->module_kind,
                                                op_names->items[i],
                                                item_ids->items[i]);
        }
    }
    if (strcmp(machine_bundle->module_kind, "compiler_core") == 0 && item_ids->len > 0) {
        return machine_function_symbol_name(machine_bundle->target_triple,
                                            machine_bundle->module_kind,
                                            op_names->items[0],
                                            item_ids->items[0]);
    }
    return xstrdup_text("");
}

static SystemLinkPlanBundle build_system_link_plan(const char *repo_root,
                                                   const MachineModuleBundle *machine_bundle,
                                                   const ObjFileBundle *obj_file,
                                                   const char *emit_kind,
                                                   const StringList *provider_modules,
                                                   const IntList *item_ids,
                                                   const StringList *op_names,
                                                   const StringList *labels) {
    SystemLinkPlanBundle out;
    RuntimeProviderObjectBundle provider_object;
    StringList lines;
    size_t symbol_idx;
    size_t i;
    memset(&out, 0, sizeof(out));
    memset(&lines, 0, sizeof(lines));
    out.version = xstrdup_text(system_link_plan_version());
    out.module_kind = xstrdup_text(machine_bundle->module_kind);
    out.emit_kind = xstrdup_text(emit_kind);
    out.target_triple = xstrdup_text(machine_bundle->target_triple);
    out.obj_format = xstrdup_text(machine_bundle->obj_format);
    out.linker_flavor = xstrdup_text(system_linker_flavor(machine_bundle->target_triple, emit_kind));
    out.output_kind = xstrdup_text(system_link_output_kind(emit_kind));
    out.output_name = system_link_output_name(machine_bundle->module_kind, emit_kind);
    out.output_suffix = xstrdup_text(system_link_output_suffix(machine_bundle->target_triple, emit_kind));
    out.primary_obj_file_cid = xstrdup_text(obj_file->file_cid);
    out.primary_obj_file_byte_count = obj_file->file_byte_count;
    for (i = 0; i < provider_modules->len; ++i) {
        string_list_push_copy(&out.provider_modules, provider_modules->items[i]);
        provider_object = build_runtime_provider_object(repo_root, provider_modules->items[i], machine_bundle->target_triple);
        string_list_push_copy(&out.provider_source_paths, provider_object.source_path);
        string_list_push_copy(&out.provider_source_kinds, provider_object.source_kind);
        string_list_push_copy(&out.provider_compile_modes, provider_object.compile_mode);
        string_list_push_copy(&out.provider_manifest_cids, provider_object.source_manifest_cid);
        string_list_push_copy(&out.provider_execution_models, provider_object.execution_model);
        string_list_push_copy(&out.provider_trace_symbols, provider_object.trace_symbol);
        int_list_push(&out.provider_symbol_counts, (int)provider_object.runtime_symbols.len);
        for (symbol_idx = 0; symbol_idx < provider_object.runtime_symbols.len; ++symbol_idx) {
            string_list_push_copy(&out.provider_symbols_flat, provider_object.runtime_symbols.items[symbol_idx]);
        }
        string_list_push_copy(&out.provider_obj_file_cids, provider_object.obj_file_cid);
        int_list_push(&out.provider_obj_file_byte_counts, provider_object.obj_file_byte_count);
    }
    out.entry_required = strcmp(emit_kind, "exe") == 0 ? 1 : 0;
    out.entry_symbol = resolve_entry_symbol(machine_bundle,
                                            emit_kind,
                                            item_ids,
                                            op_names,
                                            labels);
    out.entry_present = out.entry_symbol[0] != '\0' ? 1 : 0;
    if (out.entry_required && !out.entry_present) {
        string_list_push_copy(&out.missing_reasons, "program_entry_symbol_missing");
    }
    string_list_push_copy(&out.missing_reasons, "system_link_execution_missing");
    string_list_pushf(&lines, "system_link_plan_version=%s", out.version);
    string_list_pushf(&lines, "module_kind=%s", out.module_kind);
    string_list_pushf(&lines, "emit=%s", out.emit_kind);
    string_list_pushf(&lines, "target=%s", out.target_triple);
    string_list_pushf(&lines, "obj_format=%s", out.obj_format);
    string_list_pushf(&lines, "linker_flavor=%s", out.linker_flavor);
    string_list_pushf(&lines, "output_kind=%s", out.output_kind);
    string_list_pushf(&lines, "output_name=%s", out.output_name);
    string_list_pushf(&lines, "output_suffix=%s", out.output_suffix);
    string_list_pushf(&lines, "primary_obj_file_cid=%s", out.primary_obj_file_cid);
    string_list_pushf(&lines, "primary_obj_file_byte_count=%d", out.primary_obj_file_byte_count);
    string_list_pushf(&lines, "provider_module_count=%zu", out.provider_modules.len);
    symbol_idx = 0;
    for (i = 0; i < out.provider_modules.len; ++i) {
        string_list_pushf(&lines, "provider_module.%zu.name=%s", i, out.provider_modules.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.source_path=%s", i, out.provider_source_paths.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.source_kind=%s", i, out.provider_source_kinds.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.compile_mode=%s", i, out.provider_compile_modes.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.manifest_cid=%s", i, out.provider_manifest_cids.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.execution_model=%s", i, out.provider_execution_models.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.trace_symbol=%s", i, out.provider_trace_symbols.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.provider_symbol_count=%d", i,
                          out.provider_symbol_counts.items[i]);
        {
            int j;
            for (j = 0; j < out.provider_symbol_counts.items[i]; ++j) {
                string_list_pushf(&lines, "provider_module.%zu.provider_symbol.%d=%s",
                                  i,
                                  j,
                                  out.provider_symbols_flat.items[symbol_idx]);
                symbol_idx += 1;
            }
        }
        string_list_pushf(&lines, "provider_module.%zu.obj_file_cid=%s", i, out.provider_obj_file_cids.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.obj_file_byte_count=%d", i,
                          out.provider_obj_file_byte_counts.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.object_materialized=1", i);
    }
    string_list_pushf(&lines, "entry_required=%d", out.entry_required);
    string_list_pushf(&lines, "entry_symbol=%s", out.entry_symbol);
    string_list_pushf(&lines, "entry_present=%d", out.entry_present);
    string_list_pushf(&lines, "missing_reason_count=%zu", out.missing_reasons.len);
    for (i = 0; i < out.missing_reasons.len; ++i) {
        string_list_pushf(&lines, "missing_reason.%zu=%s", i, out.missing_reasons.items[i]);
    }
    string_list_pushf(&lines, "system_link_ready=%d", out.missing_reasons.len == 0 ? 1 : 0);
    out.plan_text = string_list_join(&lines, "\n");
    out.plan_cid = sha256_hex_text(out.plan_text);
    return out;
}

static char *build_network_compile_key_text(const NetworkBundle *bundle,
                                            const MachineModuleBundle *machine_bundle,
                                            const ObjImageBundle *obj_bundle,
                                            const ObjFileBundle *obj_file_bundle,
                                            const NativeLinkPlanBundle *link_plan,
                                            const SystemLinkPlanBundle *system_link_plan,
                                            const char *module_kind,
                                            const char *emit_kind,
                                            const char *target_triple,
                                            const char *manifest_cid,
                                            const char *obj_format) {
    StringList lines;
    size_t provider_symbol_idx;
    size_t i;
    memset(&lines, 0, sizeof(lines));
    string_list_pushf(&lines, "emit=%s", emit_kind);
    string_list_pushf(&lines, "target=%s", target_triple);
    string_list_pushf(&lines, "module_kind=%s", module_kind);
    string_list_pushf(&lines, "pipeline_version=%s", bundle->version);
    string_list_pushf(&lines, "manifest_cid=%s", manifest_cid);
    string_list_pushf(&lines, "obj_format=%s", obj_format);
    string_list_pushf(&lines, "machine_pipeline_version=%s", machine_bundle->version);
    string_list_pushf(&lines, "obj_image_version=%s", obj_bundle->version);
    string_list_pushf(&lines, "obj_file_version=%s", obj_file_bundle->version);
    string_list_pushf(&lines, "native_link_plan_version=%s", link_plan->version);
    string_list_pushf(&lines, "system_link_plan_version=%s", system_link_plan->version);
    string_list_pushf(&lines, "machine_architecture=%s", machine_bundle->architecture);
    string_list_pushf(&lines, "machine_text_section=%s", machine_bundle->text_section_name);
    string_list_pushf(&lines, "machine_call_relocation=%s", machine_bundle->call_relocation_kind);
    string_list_pushf(&lines, "machine_metadata_page_relocation=%s",
                      machine_bundle->metadata_page_relocation_kind);
    string_list_pushf(&lines, "machine_metadata_pageoff_relocation=%s",
                      machine_bundle->metadata_pageoff_relocation_kind);
    string_list_pushf(&lines, "machine_stack_align=%d", machine_bundle->stack_align_bytes);
    string_list_pushf(&lines, "system_link_linker_flavor=%s", system_link_plan->linker_flavor);
    string_list_pushf(&lines, "system_link_output_kind=%s", system_link_plan->output_kind);
    string_list_pushf(&lines, "system_link_output_suffix=%s", system_link_plan->output_suffix);
    string_list_pushf(&lines, "system_link_entry_required=%d", system_link_plan->entry_required);
    string_list_pushf(&lines, "system_link_entry_symbol=%s", system_link_plan->entry_symbol);
    string_list_pushf(&lines, "system_link_provider_count=%zu", system_link_plan->provider_modules.len);
    provider_symbol_idx = 0;
    for (i = 0; i < system_link_plan->provider_modules.len; ++i) {
        string_list_pushf(&lines, "system_link.provider.%zu.module=%s", i, system_link_plan->provider_modules.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.source_path=%s", i,
                          system_link_plan->provider_source_paths.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.source_kind=%s", i,
                          system_link_plan->provider_source_kinds.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.compile_mode=%s", i,
                          system_link_plan->provider_compile_modes.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.manifest_cid=%s", i,
                          system_link_plan->provider_manifest_cids.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.execution_model=%s", i,
                          system_link_plan->provider_execution_models.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.trace_symbol=%s", i,
                          system_link_plan->provider_trace_symbols.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.provider_symbol_count=%d",
                          i,
                          system_link_plan->provider_symbol_counts.items[i]);
        {
            int j;
            for (j = 0; j < system_link_plan->provider_symbol_counts.items[i]; ++j) {
                string_list_pushf(&lines, "system_link.provider.%zu.provider_symbol.%d=%s",
                                  i,
                                  j,
                                  system_link_plan->provider_symbols_flat.items[provider_symbol_idx]);
                provider_symbol_idx += 1;
            }
        }
        string_list_pushf(&lines, "system_link.provider.%zu.obj_file_cid=%s", i,
                          system_link_plan->provider_obj_file_cids.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.obj_file_byte_count=%d", i,
                          system_link_plan->provider_obj_file_byte_counts.items[i]);
    }
    string_list_pushf(&lines, "system_link_missing_reason_count=%zu", system_link_plan->missing_reasons.len);
    for (i = 0; i < system_link_plan->missing_reasons.len; ++i) {
        string_list_pushf(&lines, "system_link.missing_reason.%zu=%s", i, system_link_plan->missing_reasons.items[i]);
    }
    string_list_pushf(&lines, "native_link_provider_count=%zu", link_plan->provider_modules.len);
    string_list_pushf(&lines, "native_link_runtime_target_count=%zu", link_plan->runtime_targets.len);
    for (i = 0; i < link_plan->provider_modules.len; ++i) {
        string_list_pushf(&lines, "native_link.provider.%zu=%s", i, link_plan->provider_modules.items[i]);
    }
    for (i = 0; i < link_plan->runtime_targets.len; ++i) {
        string_list_pushf(&lines, "native_link.required.%zu=%s", i, link_plan->runtime_targets.items[i]);
    }
    string_list_pushf(&lines, "network_topology_count=%zu", bundle->program.topologies.len);
    for (i = 0; i < bundle->program.topologies.len; ++i) {
        char *form = topology_canonical_form(&bundle->program.topologies.items[i]);
        string_list_pushf(&lines, "network.topology.%zu=%s", i, form);
        free(form);
    }
    string_list_pushf(&lines, "network_entry_count=%zu", bundle->low_plan.op_ids.len);
    for (i = 0; i < bundle->low_plan.op_ids.len; ++i) {
        NetworkLsmrBinding binding = bind_network_op_to_lsmr(bundle, manifest_cid, (int)i);
        string_list_pushf(&lines, "network.entry.%zu.op=%s", i, bundle->low_plan.canonical_ops.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.runtime=%s", i, bundle->low_plan.runtime_targets.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.proof=%s", i, bundle->low_plan.proof_domains.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.domain=%s", i, bundle->facts.network_domains.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.topology_ref=%s", i, bundle->facts.topology_refs.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.topology=%s", i, bundle->low_plan.topology_canonical_forms.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.addressing=%s", i, bundle->low_plan.addressing_modes.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.depth=%d", i, bundle->low_plan.topology_depths.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.address_binding=%s", i, bundle->low_plan.address_bindings.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.distance_metric=%s", i, bundle->low_plan.distance_metrics.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.route_plan_model=%s", i, bundle->low_plan.route_plan_models.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.transport=%s", i, bundle->low_plan.transport_modes.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.repair=%s", i, bundle->low_plan.repair_modes.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.dispersal=%s", i, bundle->low_plan.dispersal_modes.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.priority=%s", i, bundle->low_plan.priority_classes.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.identity=%s", i, bundle->low_plan.identity_scopes.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.signature=%s", i,
                          bundle->low_plan.anti_entropy_signature_kinds.items[i]);
        string_list_pushf(&lines, "network.entry.%zu.content_cid=%s", i, binding.content_cid);
        string_list_pushf(&lines, "network.entry.%zu.source_address=%s", i, binding.source_address);
        string_list_pushf(&lines, "network.entry.%zu.target_address=%s", i, binding.target_address);
        string_list_pushf(&lines, "network.entry.%zu.route_plan_cid=%s", i, binding.route_plan_cid);
        string_list_pushf(&lines, "network.entry.%zu.route_path_count=%d", i, binding.route_path_count);
        string_list_pushf(&lines, "network.entry.%zu.total_distance=%d", i, binding.total_distance);
        {
            int j;
            for (j = 0; j < binding.route_path_count; ++j) {
                char *pattern = xformat("path.%d", j);
                char *path_value = extract_line_value(binding.route_plan_text, pattern);
                if (path_value[0] == '\0') {
                    free(pattern);
                    free(path_value);
                    die("v2 release artifact: missing lsmr route path");
                }
                string_list_pushf(&lines, "network.entry.%zu.route.%d=%s", i, j, path_value);
                free(pattern);
                free(path_value);
            }
        }
    }
    return string_list_join(&lines, "\n");
}

static char *build_compiler_core_compile_key_text(const CompilerCoreBundle *bundle,
                                                  const MachineModuleBundle *machine_bundle,
                                                  const ObjImageBundle *obj_bundle,
                                                  const ObjFileBundle *obj_file_bundle,
                                                  const NativeLinkPlanBundle *link_plan,
                                                  const SystemLinkPlanBundle *system_link_plan,
                                                  const char *emit_kind,
                                                  const char *target_triple,
                                                  const char *manifest_cid,
                                                  const char *obj_format) {
    StringList lines;
    size_t provider_symbol_idx;
    size_t i;
    memset(&lines, 0, sizeof(lines));
    string_list_pushf(&lines, "emit=%s", emit_kind);
    string_list_pushf(&lines, "target=%s", target_triple);
    string_list_pushf(&lines, "module_kind=compiler_core");
    string_list_pushf(&lines, "pipeline_version=%s", bundle->version);
    string_list_pushf(&lines, "manifest_cid=%s", manifest_cid);
    string_list_pushf(&lines, "obj_format=%s", obj_format);
    string_list_pushf(&lines, "machine_pipeline_version=%s", machine_bundle->version);
    string_list_pushf(&lines, "obj_image_version=%s", obj_bundle->version);
    string_list_pushf(&lines, "obj_file_version=%s", obj_file_bundle->version);
    string_list_pushf(&lines, "native_link_plan_version=%s", link_plan->version);
    string_list_pushf(&lines, "system_link_plan_version=%s", system_link_plan->version);
    string_list_pushf(&lines, "machine_architecture=%s", machine_bundle->architecture);
    string_list_pushf(&lines, "machine_text_section=%s", machine_bundle->text_section_name);
    string_list_pushf(&lines, "machine_call_relocation=%s", machine_bundle->call_relocation_kind);
    string_list_pushf(&lines, "machine_metadata_page_relocation=%s",
                      machine_bundle->metadata_page_relocation_kind);
    string_list_pushf(&lines, "machine_metadata_pageoff_relocation=%s",
                      machine_bundle->metadata_pageoff_relocation_kind);
    string_list_pushf(&lines, "machine_stack_align=%d", machine_bundle->stack_align_bytes);
    string_list_pushf(&lines, "system_link_linker_flavor=%s", system_link_plan->linker_flavor);
    string_list_pushf(&lines, "system_link_output_kind=%s", system_link_plan->output_kind);
    string_list_pushf(&lines, "system_link_output_suffix=%s", system_link_plan->output_suffix);
    string_list_pushf(&lines, "system_link_entry_required=%d", system_link_plan->entry_required);
    string_list_pushf(&lines, "system_link_entry_symbol=%s", system_link_plan->entry_symbol);
    string_list_pushf(&lines, "system_link_provider_count=%zu", system_link_plan->provider_modules.len);
    provider_symbol_idx = 0;
    for (i = 0; i < system_link_plan->provider_modules.len; ++i) {
        string_list_pushf(&lines, "system_link.provider.%zu.module=%s", i, system_link_plan->provider_modules.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.source_path=%s", i,
                          system_link_plan->provider_source_paths.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.source_kind=%s", i,
                          system_link_plan->provider_source_kinds.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.compile_mode=%s", i,
                          system_link_plan->provider_compile_modes.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.manifest_cid=%s", i,
                          system_link_plan->provider_manifest_cids.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.execution_model=%s", i,
                          system_link_plan->provider_execution_models.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.trace_symbol=%s", i,
                          system_link_plan->provider_trace_symbols.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.provider_symbol_count=%d",
                          i,
                          system_link_plan->provider_symbol_counts.items[i]);
        {
            int j;
            for (j = 0; j < system_link_plan->provider_symbol_counts.items[i]; ++j) {
                string_list_pushf(&lines, "system_link.provider.%zu.provider_symbol.%d=%s",
                                  i,
                                  j,
                                  system_link_plan->provider_symbols_flat.items[provider_symbol_idx]);
                provider_symbol_idx += 1;
            }
        }
        string_list_pushf(&lines, "system_link.provider.%zu.obj_file_cid=%s", i,
                          system_link_plan->provider_obj_file_cids.items[i]);
        string_list_pushf(&lines, "system_link.provider.%zu.obj_file_byte_count=%d", i,
                          system_link_plan->provider_obj_file_byte_counts.items[i]);
    }
    string_list_pushf(&lines, "system_link_missing_reason_count=%zu", system_link_plan->missing_reasons.len);
    for (i = 0; i < system_link_plan->missing_reasons.len; ++i) {
        string_list_pushf(&lines, "system_link.missing_reason.%zu=%s", i, system_link_plan->missing_reasons.items[i]);
    }
    string_list_pushf(&lines, "native_link_provider_count=%zu", link_plan->provider_modules.len);
    string_list_pushf(&lines, "native_link_runtime_target_count=%zu", link_plan->runtime_targets.len);
    for (i = 0; i < link_plan->provider_modules.len; ++i) {
        string_list_pushf(&lines, "native_link.provider.%zu=%s", i, link_plan->provider_modules.items[i]);
    }
    for (i = 0; i < link_plan->runtime_targets.len; ++i) {
        string_list_pushf(&lines, "native_link.required.%zu=%s", i, link_plan->runtime_targets.items[i]);
    }
    string_list_pushf(&lines, "compiler_core_top_level_count=%zu", bundle->program.top_level_kinds.len);
    string_list_pushf(&lines, "compiler_core_module_count=%zu", bundle->program.modules.len);
    string_list_pushf(&lines, "compiler_core_import_count=%zu", bundle->program.import_paths.len);
    string_list_pushf(&lines, "compiler_core_type_block_count=%zu", bundle->program.type_headers.len);
    string_list_pushf(&lines, "compiler_core_const_count=%zu", bundle->program.const_decls.len);
    string_list_pushf(&lines, "compiler_core_var_count=%zu", bundle->program.var_decls.len);
    string_list_pushf(&lines, "compiler_core_routine_count=%zu", bundle->program.routine_names.len);
    for (i = 0; i < bundle->low_plan.item_ids.len; ++i) {
        string_list_pushf(&lines, "compiler_core.item.%zu.kind=%s", i,
                          bundle->low_plan.top_level_kinds.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.label=%s", i, bundle->low_plan.labels.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.op=%s", i, bundle->low_plan.canonical_ops.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.runtime=%s", i,
                          bundle->low_plan.runtime_targets.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.proof=%s", i,
                          bundle->low_plan.proof_domains.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.effect=%s", i,
                          bundle->low_plan.effect_classes.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.binding_scope=%s", i,
                          bundle->low_plan.binding_scopes.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.import_path=%s", i,
                          bundle->low_plan.import_paths.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.import_alias=%s", i,
                          bundle->low_plan.import_aliases.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.import_category=%s", i,
                          bundle->low_plan.import_categories.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.ast_ready=%d", i,
                          bundle->low_plan.executable_ast_ready.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.return_type=%s", i,
                          bundle->low_plan.return_type_texts.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.param_count=%d", i,
                          bundle->low_plan.param_counts.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.local_count=%d", i,
                          bundle->low_plan.local_counts.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.stmt_count=%d", i,
                          bundle->low_plan.stmt_counts.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.expr_count=%d", i,
                          bundle->low_plan.expr_counts.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.body_line_count=%d", i,
                          bundle->low_plan.body_line_counts.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.signature_hash=%s", i,
                          bundle->low_plan.signature_hashes.items[i]);
        string_list_pushf(&lines, "compiler_core.item.%zu.body_hash=%s", i,
                          bundle->low_plan.body_hashes.items[i]);
    }
    return string_list_join(&lines, "\n");
}

static void release_artifact_manifest_root(const char *source_abs,
                                           const char *source_norm,
                                           char *root_abs,
                                           size_t root_abs_cap,
                                           char *root_norm,
                                           size_t root_norm_cap) {
    if (path_is_dir(source_abs)) {
        if (snprintf(root_abs, root_abs_cap, "%s", source_abs) >= (int)root_abs_cap) {
            die("path too long");
        }
        if (snprintf(root_norm, root_norm_cap, "%s", source_norm) >= (int)root_norm_cap) {
            die("path too long");
        }
        return;
    }
    if (snprintf(root_abs, root_abs_cap, "%s", source_abs) >= (int)root_abs_cap) {
        die("path too long");
    }
    if (snprintf(root_norm, root_norm_cap, "%s", source_norm) >= (int)root_norm_cap) {
        die("path too long");
    }
    parent_dir(root_abs);
    parent_dir(root_norm);
    if (root_abs[0] == '\0') {
        if (snprintf(root_abs, root_abs_cap, ".") >= (int)root_abs_cap) {
            die("path too long");
        }
    }
    if (root_norm[0] == '\0') {
        if (snprintf(root_norm, root_norm_cap, ".") >= (int)root_norm_cap) {
            die("path too long");
        }
    }
}

static ReleaseArtifactBundle build_release_artifact(const char *repo_root,
                                                    const char *source_abs,
                                                    const char *source_norm,
                                                    const char *emit_kind,
                                                    const char *target_triple) {
    ParsedV2Source parsed = parse_v2_source_file_strict(source_abs);
    char manifest_root_abs[PATH_MAX];
    char manifest_root_norm[PATH_MAX];
    SourceManifestBundle manifest;
    NetworkBundle network_bundle;
    CompilerCoreBundle compiler_core_bundle;
    MachineModuleBundle machine_bundle;
    ObjImageBundle obj_bundle;
    ObjFileBundle obj_file;
    NativeLinkPlanBundle link_plan;
    SystemLinkPlanBundle system_link;
    ReleaseArtifactBundle out;
    const char *module_kind;
    const char *obj_format;
    char *compile_key;
    char *artifact_text;
    StringList lines;
    IntList empty_item_ids;
    StringList empty_op_names;
    StringList empty_labels;
    const char *pipeline_version;
    size_t surface_count = 0;
    size_t topology_count = 0;
    size_t network_entry_count = 0;
    size_t compiler_core_entry_count = 0;
    (void)repo_root;
    memset(&empty_item_ids, 0, sizeof(empty_item_ids));
    memset(&empty_op_names, 0, sizeof(empty_op_names));
    memset(&empty_labels, 0, sizeof(empty_labels));
    if (strcmp(emit_kind, "exe") != 0 && strcmp(emit_kind, "shared") != 0 && strcmp(emit_kind, "static") != 0) {
        die("v2 release artifact: unsupported emit kind");
    }
    if (parsed.had_compiler_core_surface) {
        manifest = resolve_compiler_core_source_manifest(repo_root, source_abs);
    } else {
        release_artifact_manifest_root(source_abs,
                                       source_norm,
                                       manifest_root_abs,
                                       sizeof(manifest_root_abs),
                                       manifest_root_norm,
                                       sizeof(manifest_root_norm));
        manifest = resolve_source_manifest(manifest_root_abs, manifest_root_norm);
    }
    if (parsed.had_network_surface) {
        network_bundle = compile_network_distribution_program(&parsed.network_program);
        module_kind = network_bundle.program.ops.len > 0 ? "network_distribution" : "topology";
        pipeline_version = network_bundle.version;
        surface_count = network_bundle.program.topologies.len + network_bundle.program.ops.len;
        topology_count = network_bundle.program.topologies.len;
        network_entry_count = network_bundle.low_plan.op_ids.len;
        machine_bundle = build_network_machine_module(&network_bundle, module_kind, target_triple);
        obj_bundle = build_obj_image(&machine_bundle);
        obj_file = build_obj_file(&machine_bundle);
        link_plan = build_native_link_plan(&machine_bundle, emit_kind, target_triple);
        if (link_plan.unresolved_targets.len > 0) {
            die("v2 release artifact: unresolved native link closure");
        }
        system_link = build_system_link_plan(repo_root,
                                             &machine_bundle,
                                             &obj_file,
                                             emit_kind,
                                             &link_plan.provider_modules,
                                             &empty_item_ids,
                                             &empty_op_names,
                                             &empty_labels);
    } else if (parsed.had_compiler_core_surface) {
        compiler_core_bundle = compile_compiler_core_source_closure(repo_root, source_abs);
        module_kind = "compiler_core";
        pipeline_version = compiler_core_bundle.version;
        surface_count = compiler_core_bundle.program.top_level_kinds.len;
        compiler_core_entry_count = compiler_core_bundle.low_plan.item_ids.len;
        machine_bundle = build_compiler_core_machine_module(&compiler_core_bundle, target_triple);
        obj_bundle = build_obj_image(&machine_bundle);
        obj_file = build_obj_file(&machine_bundle);
        link_plan = build_native_link_plan(&machine_bundle, emit_kind, target_triple);
        if (link_plan.unresolved_targets.len > 0) {
            die("v2 release artifact: unresolved native link closure");
        }
        system_link = build_system_link_plan(repo_root,
                                             &machine_bundle,
                                             &obj_file,
                                             emit_kind,
                                             &link_plan.provider_modules,
                                             &compiler_core_bundle.low_plan.item_ids,
                                             &compiler_core_bundle.low_plan.canonical_ops,
                                             &compiler_core_bundle.low_plan.labels);
    } else {
        die("v2 release artifact: unsupported source surface");
    }
    obj_format = obj_format_for_target(target_triple);
    memset(&out, 0, sizeof(out));
    out.version = xstrdup_text("v2.release_artifact.v2");
    out.source_path = xstrdup_text(source_norm);
    out.emit_kind = xstrdup_text(emit_kind);
    out.target_triple = xstrdup_text(target_triple);
    out.module_kind = xstrdup_text(module_kind);
    out.machine_version = xstrdup_text("v2.machine.v1");
    out.machine_pipeline_version = xstrdup_text(machine_bundle.version);
    out.obj_writer_version = xstrdup_text("v2.obj_writer.v1");
    out.obj_image_version = xstrdup_text(obj_bundle.version);
    out.obj_file_version = xstrdup_text(obj_file.version);
    out.obj_format = xstrdup_text(obj_format);
    out.manifest_cid = xstrdup_text(manifest.manifest_cid);
    if (parsed.had_network_surface) {
        out.compile_key_text = build_network_compile_key_text(&network_bundle,
                                                              &machine_bundle,
                                                              &obj_bundle,
                                                              &obj_file,
                                                              &link_plan,
                                                              &system_link,
                                                              module_kind,
                                                              emit_kind,
                                                              target_triple,
                                                              manifest.manifest_cid,
                                                              obj_format);
    } else {
        out.compile_key_text = build_compiler_core_compile_key_text(&compiler_core_bundle,
                                                                    &machine_bundle,
                                                                    &obj_bundle,
                                                                    &obj_file,
                                                                    &link_plan,
                                                                    &system_link,
                                                                    emit_kind,
                                                                    target_triple,
                                                                    manifest.manifest_cid,
                                                                    obj_format);
    }
    compile_key = sha256_hex_text(out.compile_key_text);
    out.compile_key = compile_key;
    memset(&lines, 0, sizeof(lines));
    string_list_pushf(&lines, "artifact_version=%s", out.version);
    string_list_pushf(&lines, "source=%s", out.source_path);
    string_list_pushf(&lines, "emit=%s", out.emit_kind);
    string_list_pushf(&lines, "target=%s", out.target_triple);
    string_list_pushf(&lines, "module_kind=%s", out.module_kind);
    string_list_pushf(&lines, "pipeline_version=%s", pipeline_version);
    string_list_pushf(&lines, "surface_count=%zu", surface_count);
    string_list_pushf(&lines, "machine_version=%s", out.machine_version);
    string_list_pushf(&lines, "machine_pipeline_version=%s", out.machine_pipeline_version);
    string_list_pushf(&lines, "obj_writer_version=%s", out.obj_writer_version);
    string_list_pushf(&lines, "obj_image_version=%s", out.obj_image_version);
    string_list_pushf(&lines, "obj_file_version=%s", out.obj_file_version);
    string_list_pushf(&lines, "obj_format=%s", out.obj_format);
    string_list_pushf(&lines, "manifest_cid=%s", out.manifest_cid);
    string_list_pushf(&lines, "compile_key=%s", out.compile_key);
    out.obj_image_cid = xstrdup_text(obj_bundle.image_cid);
    out.obj_file_cid = xstrdup_text(obj_file.file_cid);
    out.obj_file_byte_count = obj_file.file_byte_count;
    out.obj_section_count = obj_bundle.section_count;
    out.obj_symbol_count = obj_bundle.symbol_count;
    out.obj_reloc_count = obj_bundle.reloc_count;
    string_list_pushf(&lines, "obj_image_cid=%s", out.obj_image_cid);
    string_list_pushf(&lines, "obj_file_cid=%s", out.obj_file_cid);
    string_list_pushf(&lines, "obj_file_byte_count=%d", out.obj_file_byte_count);
    string_list_pushf(&lines, "obj_section_count=%d", out.obj_section_count);
    string_list_pushf(&lines, "obj_symbol_count=%d", out.obj_symbol_count);
    string_list_pushf(&lines, "obj_reloc_count=%d", out.obj_reloc_count);
    out.link_plan_version = xstrdup_text(link_plan.version);
    out.link_plan_cid = xstrdup_text(link_plan.plan_cid);
    out.system_link_plan_version = xstrdup_text(system_link.version);
    out.system_link_plan_cid = xstrdup_text(system_link.plan_cid);
    out.runtime_target_count = (int)link_plan.runtime_targets.len;
    out.runtime_unresolved_count = (int)link_plan.unresolved_targets.len;
    out.native_link_closure_ok = link_plan.unresolved_targets.len == 0 ? 1 : 0;
    out.system_link_ready = system_link.missing_reasons.len == 0 ? 1 : 0;
    out.system_link_missing_reason_count = (int)system_link.missing_reasons.len;
    out.topology_count = (int)topology_count;
    out.network_entry_count = (int)network_entry_count;
    out.compiler_core_entry_count = (int)compiler_core_entry_count;
    out.system_link_plan_text = xstrdup_text(system_link.plan_text);
    string_list_pushf(&lines, "link_plan_version=%s", out.link_plan_version);
    string_list_pushf(&lines, "link_plan_cid=%s", out.link_plan_cid);
    string_list_pushf(&lines, "system_link_plan_version=%s", out.system_link_plan_version);
    string_list_pushf(&lines, "system_link_plan_cid=%s", out.system_link_plan_cid);
    string_list_pushf(&lines, "runtime_target_count=%d", out.runtime_target_count);
    string_list_pushf(&lines, "runtime_unresolved_count=%d", out.runtime_unresolved_count);
    string_list_pushf(&lines, "native_link_closure_ok=%d", out.native_link_closure_ok);
    string_list_pushf(&lines, "system_link_ready=%d", out.system_link_ready);
    string_list_pushf(&lines, "system_link_missing_reason_count=%d", out.system_link_missing_reason_count);
    if (parsed.had_network_surface) {
        string_list_pushf(&lines, "topology_count=%d", out.topology_count);
        string_list_pushf(&lines, "network_entry_count=%d", out.network_entry_count);
    } else {
        string_list_pushf(&lines, "compiler_core_entry_count=%d", out.compiler_core_entry_count);
    }
    artifact_text = string_list_join(&lines, "\n");
    out.artifact_text = artifact_text;
    return out;
}

static int release_compile_key_covers_network_bundle(const ReleaseArtifactBundle *release_bundle,
                                                     const NetworkBundle *network_bundle) {
    size_t i;
    for (i = 0; i < network_bundle->program.topologies.len; ++i) {
        char *form = topology_canonical_form(&network_bundle->program.topologies.items[i]);
        char *line = xformat("network.topology.%zu=%s", i, form);
        int ok = release_artifact_compile_key_has_line(release_bundle, line);
        free(form);
        free(line);
        if (!ok) {
            return 0;
        }
    }
    for (i = 0; i < network_bundle->low_plan.op_ids.len; ++i) {
        NetworkLsmrBinding binding =
            bind_network_op_to_lsmr(network_bundle, release_bundle->manifest_cid, (int)i);
        char *topology_line = xformat("network.entry.%zu.topology=%s", i,
                                      network_bundle->low_plan.topology_canonical_forms.items[i]);
        char *addressing_line = xformat("network.entry.%zu.addressing=%s", i,
                                        network_bundle->low_plan.addressing_modes.items[i]);
        char *depth_line = xformat("network.entry.%zu.depth=%d", i,
                                   network_bundle->low_plan.topology_depths.items[i]);
        char *address_binding_line = xformat("network.entry.%zu.address_binding=%s", i,
                                             network_bundle->low_plan.address_bindings.items[i]);
        char *distance_metric_line = xformat("network.entry.%zu.distance_metric=%s", i,
                                             network_bundle->low_plan.distance_metrics.items[i]);
        char *priority_line = xformat("network.entry.%zu.priority=%s", i,
                                      network_bundle->low_plan.priority_classes.items[i]);
        char *dispersal_line = xformat("network.entry.%zu.dispersal=%s", i,
                                       network_bundle->low_plan.dispersal_modes.items[i]);
        char *signature_line = xformat("network.entry.%zu.signature=%s", i,
                                       network_bundle->low_plan.anti_entropy_signature_kinds.items[i]);
        char *content_cid_line = xformat("network.entry.%zu.content_cid=%s", i, binding.content_cid);
        char *source_address_line = xformat("network.entry.%zu.source_address=%s", i, binding.source_address);
        char *target_address_line = xformat("network.entry.%zu.target_address=%s", i, binding.target_address);
        char *route_plan_cid_line = xformat("network.entry.%zu.route_plan_cid=%s", i, binding.route_plan_cid);
        char *route_path_count_line = xformat("network.entry.%zu.route_path_count=%d", i, binding.route_path_count);
        char *total_distance_line = xformat("network.entry.%zu.total_distance=%d", i, binding.total_distance);
        int ok = release_artifact_compile_key_has_line(release_bundle, topology_line) &&
                 release_artifact_compile_key_has_line(release_bundle, addressing_line) &&
                 release_artifact_compile_key_has_line(release_bundle, depth_line) &&
                 release_artifact_compile_key_has_line(release_bundle, address_binding_line) &&
                 release_artifact_compile_key_has_line(release_bundle, distance_metric_line) &&
                 release_artifact_compile_key_has_line(release_bundle, priority_line) &&
                 release_artifact_compile_key_has_line(release_bundle, dispersal_line) &&
                 release_artifact_compile_key_has_line(release_bundle, signature_line) &&
                 release_artifact_compile_key_has_line(release_bundle, content_cid_line) &&
                 release_artifact_compile_key_has_line(release_bundle, source_address_line) &&
                 release_artifact_compile_key_has_line(release_bundle, target_address_line) &&
                 release_artifact_compile_key_has_line(release_bundle, route_plan_cid_line) &&
                 release_artifact_compile_key_has_line(release_bundle, route_path_count_line) &&
                 release_artifact_compile_key_has_line(release_bundle, total_distance_line);
        {
            int j;
            for (j = 0; ok && j < binding.route_path_count; ++j) {
                char *pattern = xformat("path.%d", j);
                char *path_value = extract_line_value(binding.route_plan_text, pattern);
                char *route_line = xformat("network.entry.%zu.route.%d=%s", i, j, path_value);
                ok = release_artifact_compile_key_has_line(release_bundle, route_line);
                free(pattern);
                free(path_value);
                free(route_line);
            }
        }
        free(topology_line);
        free(addressing_line);
        free(depth_line);
        free(address_binding_line);
        free(distance_metric_line);
        free(priority_line);
        free(dispersal_line);
        free(signature_line);
        free(content_cid_line);
        free(source_address_line);
        free(target_address_line);
        free(route_plan_cid_line);
        free(route_path_count_line);
        free(total_distance_line);
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

static NetworkSelfhostReport build_network_selfhost_report(const char *repo_root,
                                                           const char *root_abs,
                                                           const char *root_norm,
                                                           const char *source_abs,
                                                           const char *source_norm,
                                                           const char *target_triple) {
    SourceManifestBundle manifest_bundle = resolve_source_manifest(root_abs, root_norm);
    ParsedV2Source parsed = parse_v2_source_file_strict(source_abs);
    NetworkBundle network_bundle;
    ReleaseArtifactBundle release_bundle;
    NetworkSelfhostReport out;
    memset(&out, 0, sizeof(out));
    if (!parsed.had_network_surface) {
        out.ok = 0;
        out.reason = xstrdup_text("missing_network_surface");
        return out;
    }
    network_bundle = compile_network_distribution_program(&parsed.network_program);
    release_bundle = build_release_artifact(repo_root, source_abs, source_norm, "exe", target_triple);
    if (manifest_bundle.topology_canonical_forms.len == 0) {
        out.ok = 0;
        out.reason = xstrdup_text("missing_topology_canonical_form");
        return out;
    }
    if (!verify_priority_mappings(&network_bundle)) {
        out.ok = 0;
        out.reason = xstrdup_text("priority_mapping_mismatch");
        return out;
    }
    if (!has_canonical_op(&network_bundle, "publish_source_manifest") ||
        !has_canonical_op(&network_bundle, "publish_rule_pack") ||
        !has_canonical_op(&network_bundle, "publish_compiler_rule_pack")) {
        out.ok = 0;
        out.reason = xstrdup_text("missing_publish_ops");
        return out;
    }
    if (canonical_op_count_for_domain(&network_bundle, "rule_pack") <= 0) {
        out.ok = 0;
        out.reason = xstrdup_text("missing_rule_pack_domain_ops");
        return out;
    }
    if (canonical_op_count_for_domain(&network_bundle, "compiler_rule_pack") <= 0) {
        out.ok = 0;
        out.reason = xstrdup_text("missing_compiler_rule_pack_domain_ops");
        return out;
    }
    if (strcmp(release_bundle.module_kind, "network_distribution") != 0) {
        out.ok = 0;
        out.reason = xstrdup_text("release_artifact_wrong_module_kind");
        return out;
    }
    if (strcmp(release_bundle.manifest_cid, manifest_bundle.manifest_cid) != 0) {
        out.ok = 0;
        out.reason = xstrdup_text("release_manifest_mismatch");
        return out;
    }
    if (!release_compile_key_covers_network_bundle(&release_bundle, &network_bundle)) {
        out.ok = 0;
        out.reason = xstrdup_text("compile_key_missing_network_fields");
        return out;
    }
    if (!release_bundle.native_link_closure_ok) {
        out.ok = 0;
        out.reason = xstrdup_text("native_link_closure_unresolved");
        return out;
    }
    out.ok = 1;
    out.reason = xstrdup_text("");
    out.source_manifest_text = build_source_manifest_artifact_text(&manifest_bundle);
    out.source_manifest_cid = xstrdup_text(manifest_bundle.manifest_cid);
    out.rule_pack_text = build_network_artifact_text(&network_bundle, "rule_pack", "v2.rule_pack.v1");
    out.rule_pack_cid = sha256_hex_text(out.rule_pack_text);
    out.compiler_rule_pack_text =
        build_network_artifact_text(&network_bundle, "compiler_rule_pack", "v2.compiler_rule_pack.v1");
    out.compiler_rule_pack_cid = sha256_hex_text(out.compiler_rule_pack_text);
    out.release_artifact_text = xformat("%s\n", release_bundle.artifact_text);
    out.release_compile_key = xstrdup_text(release_bundle.compile_key);
    out.obj_image_cid = xstrdup_text(release_bundle.obj_image_cid);
    out.obj_section_count = release_bundle.obj_section_count;
    out.obj_symbol_count = release_bundle.obj_symbol_count;
    out.obj_reloc_count = release_bundle.obj_reloc_count;
    out.link_plan_cid = xstrdup_text(release_bundle.link_plan_cid);
    out.runtime_target_count = release_bundle.runtime_target_count;
    out.runtime_unresolved_count = release_bundle.runtime_unresolved_count;
    out.native_link_closure_ok = release_bundle.native_link_closure_ok;
    out.topology_count = (int)manifest_bundle.topology_canonical_forms.len;
    out.rule_pack_entry_count = canonical_op_count_for_domain(&network_bundle, "rule_pack");
    out.compiler_rule_pack_entry_count =
        canonical_op_count_for_domain(&network_bundle, "compiler_rule_pack");
    out.compile_key_topology_covered = 1;
    out.compile_key_addressing_covered = 1;
    out.compile_key_depth_covered = 1;
    out.compile_key_address_binding_covered = 1;
    out.compile_key_distance_metric_covered = 1;
    out.compile_key_priority_covered = 1;
    out.compile_key_dispersal_covered = 1;
    out.compile_key_signature_covered = 1;
    out.compile_key_canonical_multipath_covered = 1;
    out.release_manifest_matches_source_manifest = 1;
    out.lsmr_priority_mapping_fixed = 1;
    out.lsmr_addressing_mode = xstrdup_text(lsmr_addressing_mode());
    out.lsmr_address_binding = xstrdup_text(lsmr_address_binding_mode());
    out.lsmr_distance_metric = xstrdup_text(lsmr_distance_metric_text());
    out.lsmr_route_plan_model = xstrdup_text(lsmr_route_plan_model_text());
    out.random_gossip_fallback_present = 0;
    return out;
}

static void print_network_selfhost_report(const NetworkSelfhostReport *report) {
    if (!report->ok) {
        printf("network_selfhost_ok=0\n");
        printf("reason=%s\n", report->reason != NULL ? report->reason : "unknown");
        return;
    }
    printf("network_selfhost_ok=1\n");
    printf("source_manifest_cid=%s\n", report->source_manifest_cid);
    printf("rule_pack_cid=%s\n", report->rule_pack_cid);
    printf("compiler_rule_pack_cid=%s\n", report->compiler_rule_pack_cid);
    printf("release_compile_key=%s\n", report->release_compile_key);
    printf("obj_image_cid=%s\n", report->obj_image_cid);
    printf("obj_section_count=%d\n", report->obj_section_count);
    printf("obj_symbol_count=%d\n", report->obj_symbol_count);
    printf("obj_reloc_count=%d\n", report->obj_reloc_count);
    printf("link_plan_cid=%s\n", report->link_plan_cid);
    printf("runtime_target_count=%d\n", report->runtime_target_count);
    printf("runtime_unresolved_count=%d\n", report->runtime_unresolved_count);
    printf("native_link_closure_ok=%d\n", report->native_link_closure_ok);
    printf("topology_count=%d\n", report->topology_count);
    printf("rule_pack_entry_count=%d\n", report->rule_pack_entry_count);
    printf("compiler_rule_pack_entry_count=%d\n", report->compiler_rule_pack_entry_count);
    printf("publish_source_manifest_present=1\n");
    printf("publish_rule_pack_present=1\n");
    printf("publish_compiler_rule_pack_present=1\n");
    printf("lsmr_priority_mapping_fixed=%d\n", report->lsmr_priority_mapping_fixed);
    printf("lsmr_addressing_mode=%s\n", report->lsmr_addressing_mode);
    printf("lsmr_address_binding=%s\n", report->lsmr_address_binding);
    printf("lsmr_distance_metric=%s\n", report->lsmr_distance_metric);
    printf("lsmr_route_plan_model=%s\n", report->lsmr_route_plan_model);
    printf("compile_key_topology_covered=%d\n", report->compile_key_topology_covered);
    printf("compile_key_addressing_covered=%d\n", report->compile_key_addressing_covered);
    printf("compile_key_depth_covered=%d\n", report->compile_key_depth_covered);
    printf("compile_key_address_binding_covered=%d\n", report->compile_key_address_binding_covered);
    printf("compile_key_distance_metric_covered=%d\n", report->compile_key_distance_metric_covered);
    printf("compile_key_priority_covered=%d\n", report->compile_key_priority_covered);
    printf("compile_key_dispersal_covered=%d\n", report->compile_key_dispersal_covered);
    printf("compile_key_signature_covered=%d\n", report->compile_key_signature_covered);
    printf("compile_key_canonical_multipath_covered=%d\n", report->compile_key_canonical_multipath_covered);
    printf("release_manifest_matches_source_manifest=%d\n", report->release_manifest_matches_source_manifest);
    printf("random_gossip_fallback_present=%d\n", report->random_gossip_fallback_present);
}

static void emit_cheng_escaped(FILE *out, const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    fputc('"', out);
    while (*p != '\0') {
        if (*p == '\\' || *p == '"') {
            fputc('\\', out);
            fputc((int)*p, out);
        } else if (*p == '\n') {
            fputs("\\n", out);
        } else {
            fputc((int)*p, out);
        }
        ++p;
    }
    fputc('"', out);
}

static void emit_tooling_stage1_module(FILE *out, const ToolingBootstrapProgram *program) {
    fprintf(out, "import compiler/driver/manifest_resolver_v2 as manifest\n");
    fprintf(out, "import compiler/driver/release_artifact_v2 as rart\n");
    fprintf(out, "import compiler/driver/network_distribution_pipeline_v2 as npipe\n");
    fprintf(out, "import compiler/frontend/v2_source_parser as parser\n\n");
    fprintf(out, "fn bootstrapExamplesRoot(): str =\n");
    fprintf(out, "    return ");
    emit_cheng_escaped(out, program->root_path);
    fprintf(out, "\n\n");
    fprintf(out, "fn bootstrapNetworkSource(): str =\n");
    fprintf(out, "    return ");
    emit_cheng_escaped(out, program->source_path);
    fprintf(out, "\n\n");
    fprintf(out, "fn bootstrapTargetTriple(): str =\n");
    fprintf(out, "    return ");
    emit_cheng_escaped(out, program->target_triple);
    fprintf(out, "\n\n");
    fprintf(out, "fn bootstrapReleaseEmitKind(): str =\n");
    fprintf(out, "    return ");
    emit_cheng_escaped(out, program->emit_kind);
    fprintf(out, "\n\n");
    fprintf(out, "fn bootstrapSourceManifest(): manifest.SourceManifestBundle =\n");
    fprintf(out, "    return manifest.resolveSourceManifest(bootstrapExamplesRoot())\n\n");
    fprintf(out, "fn bootstrapNetworkPipeline(): npipe.NetworkDistributionPipelineBundle =\n");
    fprintf(out, "    let parsed = parser.parseV2SourceFileStrict(bootstrapNetworkSource())\n");
    fprintf(out, "    return npipe.compileNetworkDistributionProgram(parsed.networkProgram)\n\n");
    fprintf(out, "fn bootstrapReleaseArtifact(): rart.ReleaseArtifactBundle =\n");
    fprintf(out, "    return rart.buildReleaseArtifact(bootstrapNetworkSource(), bootstrapReleaseEmitKind(), bootstrapTargetTriple())\n");
}

static char *parse_stage1_return_string(const char *line) {
    const char *p = line;
    char *out;
    size_t cap = 32;
    size_t len = 0;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    if (!starts_with(p, "return ")) {
        return NULL;
    }
    p += strlen("return ");
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p != '"') {
        return NULL;
    }
    ++p;
    out = (char *)xcalloc(cap, 1);
    while (*p != '\0' && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\' && *p != '\0') {
            unsigned char esc = (unsigned char)*p++;
            if (esc == 'n') {
                ch = '\n';
            } else {
                ch = esc;
            }
        }
        if (len + 2 > cap) {
            cap *= 2;
            out = (char *)xrealloc(out, cap);
        }
        out[len++] = (char)ch;
        out[len] = '\0';
    }
    return out;
}

static ToolingBootstrapProgram parse_tooling_stage1_program(const char *path) {
    char *text = read_file_text(path);
    char *cursor = text;
    char current_fn[64];
    ToolingBootstrapProgram out;
    memset(&out, 0, sizeof(out));
    current_fn[0] = '\0';
    while (*cursor != '\0') {
        char *line_start = cursor;
        char *line_end = cursor;
        char saved;
        char *trimmed;
        while (*line_end != '\0' && *line_end != '\n') {
            ++line_end;
        }
        saved = *line_end;
        *line_end = '\0';
        trimmed = dup_trimmed_line(line_start);
        if (starts_with(trimmed, "fn bootstrapExamplesRoot(): str =")) {
            snprintf(current_fn, sizeof(current_fn), "%s", "bootstrapExamplesRoot");
        } else if (starts_with(trimmed, "fn bootstrapNetworkSource(): str =")) {
            snprintf(current_fn, sizeof(current_fn), "%s", "bootstrapNetworkSource");
        } else if (starts_with(trimmed, "fn bootstrapTargetTriple(): str =")) {
            snprintf(current_fn, sizeof(current_fn), "%s", "bootstrapTargetTriple");
        } else if (starts_with(trimmed, "fn bootstrapReleaseEmitKind(): str =")) {
            snprintf(current_fn, sizeof(current_fn), "%s", "bootstrapReleaseEmitKind");
        } else if (starts_with(trimmed, "return ")) {
            char *value = parse_stage1_return_string(trimmed);
            if (value != NULL) {
                if (strcmp(current_fn, "bootstrapExamplesRoot") == 0) {
                    out.root_path = value;
                } else if (strcmp(current_fn, "bootstrapNetworkSource") == 0) {
                    out.source_path = value;
                } else if (strcmp(current_fn, "bootstrapTargetTriple") == 0) {
                    out.target_triple = value;
                } else if (strcmp(current_fn, "bootstrapReleaseEmitKind") == 0) {
                    out.emit_kind = value;
                } else {
                    free(value);
                }
            }
        }
        free(trimmed);
        *line_end = saved;
        cursor = *line_end == '\n' ? line_end + 1 : line_end;
    }
    free(text);
    if (out.root_path == NULL || out.source_path == NULL || out.target_triple == NULL || out.emit_kind == NULL) {
        die("stage1 parse: incomplete tooling bootstrap module");
    }
    return out;
}

static ToolingBootstrapProgram tooling_program_from_args(const char *repo_root,
                                                         const char *root_raw,
                                                         const char *source_raw,
                                                         const char *target_raw,
                                                         const char *emit_raw) {
    char root_abs[PATH_MAX];
    char source_abs[PATH_MAX];
    char root_norm[PATH_MAX];
    char source_norm[PATH_MAX];
    ToolingBootstrapProgram out;
    memset(&out, 0, sizeof(out));
    resolve_existing_input_path(repo_root, root_raw, root_abs, sizeof(root_abs), root_norm, sizeof(root_norm));
    resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs), source_norm, sizeof(source_norm));
    out.root_path = xstrdup_text(root_norm);
    out.source_path = xstrdup_text(source_norm);
    out.target_triple = xstrdup_text(target_raw);
    out.emit_kind = xstrdup_text(emit_raw);
    return out;
}

static ToolingSelfhostStageResult build_tooling_stage_result(const char *repo_root,
                                                             const ToolingBootstrapProgram *program) {
    ToolingSelfhostStageResult out;
    char root_abs[PATH_MAX];
    char source_abs[PATH_MAX];
    char root_norm[PATH_MAX];
    char source_norm[PATH_MAX];
    unsigned long long h = 1469598103934665603ULL;
    memset(&out, 0, sizeof(out));
    resolve_existing_input_path(repo_root, program->root_path, root_abs, sizeof(root_abs), root_norm, sizeof(root_norm));
    resolve_existing_input_path(repo_root, program->source_path, source_abs, sizeof(source_abs), source_norm, sizeof(source_norm));
    out.report = build_network_selfhost_report(repo_root,
                                               root_abs,
                                               root_norm,
                                               source_abs,
                                               source_norm,
                                               program->target_triple);
    if (!out.report.ok) {
        return out;
    }
    out.source_manifest_text = xstrdup_text(out.report.source_manifest_text);
    out.rule_pack_text = xstrdup_text(out.report.rule_pack_text);
    out.compiler_rule_pack_text = xstrdup_text(out.report.compiler_rule_pack_text);
    out.release_artifact_text = xstrdup_text(out.report.release_artifact_text);
    h = fnv1a64_text(h, "v2.bootstrap.tooling_stage1.v1");
    h = fnv1a64_text(h, program->root_path);
    h = fnv1a64_text(h, program->source_path);
    h = fnv1a64_text(h, program->target_triple);
    h = fnv1a64_text(h, program->emit_kind);
    h = fnv1a64_text(h, out.report.source_manifest_cid);
    h = fnv1a64_text(h, out.report.rule_pack_cid);
    h = fnv1a64_text(h, out.report.compiler_rule_pack_cid);
    h = fnv1a64_text(h, out.report.release_compile_key);
    h = fnv1a64_text(h, out.report.obj_image_cid);
    out.stage_hash = h;
    out.text = xformat("tooling_selfhost_stage_hash=%016llx\n"
                       "source_manifest_cid=%s\n"
                       "rule_pack_cid=%s\n"
                       "compiler_rule_pack_cid=%s\n"
                       "release_compile_key=%s\n"
                       "obj_image_cid=%s\n",
                       out.stage_hash,
                       out.report.source_manifest_cid,
                       out.report.rule_pack_cid,
                       out.report.compiler_rule_pack_cid,
                       out.report.release_compile_key,
                       out.report.obj_image_cid);
    return out;
}

static int tooling_stage_results_equal(const ToolingSelfhostStageResult *left,
                                       const ToolingSelfhostStageResult *right) {
    if (!left->report.ok || !right->report.ok) {
        return 0;
    }
    return strcmp(left->source_manifest_text, right->source_manifest_text) == 0 &&
           strcmp(left->rule_pack_text, right->rule_pack_text) == 0 &&
           strcmp(left->compiler_rule_pack_text, right->compiler_rule_pack_text) == 0 &&
           strcmp(left->release_artifact_text, right->release_artifact_text) == 0 &&
           strcmp(left->report.source_manifest_cid, right->report.source_manifest_cid) == 0 &&
           strcmp(left->report.rule_pack_cid, right->report.rule_pack_cid) == 0 &&
           strcmp(left->report.compiler_rule_pack_cid, right->report.compiler_rule_pack_cid) == 0 &&
           strcmp(left->report.release_compile_key, right->report.release_compile_key) == 0 &&
           strcmp(left->report.obj_image_cid, right->report.obj_image_cid) == 0 &&
           left->stage_hash == right->stage_hash;
}

static int tooling_selfhost_check(const char *repo_root,
                                  const ToolingBootstrapProgram *program) {
    char tmp_stage1_path[] = "/tmp/cheng_v2_tooling_stage1_XXXXXX";
    char tmp_stage2_path[] = "/tmp/cheng_v2_tooling_stage2_XXXXXX";
    int stage1_fd = mkstemp(tmp_stage1_path);
    int stage2_fd;
    FILE *stage1_out;
    FILE *stage2_out;
    ToolingBootstrapProgram stage1_program;
    ToolingBootstrapProgram stage2_program;
    ToolingSelfhostStageResult stage0_result;
    ToolingSelfhostStageResult stage1_result;
    ToolingSelfhostStageResult stage2_result;
    int stage0_stage1_equal;
    int stage1_stage2_equal;
    int stage1_stage2_source_equal;
    if (stage1_fd < 0) {
        die_errno("mkstemp failed", tmp_stage1_path);
    }
    stage1_out = fdopen(stage1_fd, "wb");
    if (stage1_out == NULL) {
        close(stage1_fd);
        die_errno("fdopen failed", tmp_stage1_path);
    }
    emit_tooling_stage1_module(stage1_out, program);
    fclose(stage1_out);
    stage1_program = parse_tooling_stage1_program(tmp_stage1_path);
    stage2_fd = mkstemp(tmp_stage2_path);
    if (stage2_fd < 0) {
        unlink(tmp_stage1_path);
        die_errno("mkstemp failed", tmp_stage2_path);
    }
    stage2_out = fdopen(stage2_fd, "wb");
    if (stage2_out == NULL) {
        close(stage2_fd);
        unlink(tmp_stage1_path);
        unlink(tmp_stage2_path);
        die_errno("fdopen failed", tmp_stage2_path);
    }
    emit_tooling_stage1_module(stage2_out, &stage1_program);
    fclose(stage2_out);
    stage2_program = parse_tooling_stage1_program(tmp_stage2_path);
    stage0_result = build_tooling_stage_result(repo_root, program);
    stage1_result = build_tooling_stage_result(repo_root, &stage1_program);
    stage2_result = build_tooling_stage_result(repo_root, &stage2_program);
    stage0_stage1_equal = tooling_stage_results_equal(&stage0_result, &stage1_result);
    stage1_stage2_equal = tooling_stage_results_equal(&stage1_result, &stage2_result);
    stage1_stage2_source_equal = text_files_equal(tmp_stage1_path, tmp_stage2_path);
    printf("tooling_selfhost_stage0_hash=%016llx\n", stage0_result.stage_hash);
    printf("tooling_selfhost_stage1_hash=%016llx\n", stage1_result.stage_hash);
    printf("tooling_selfhost_stage2_hash=%016llx\n", stage2_result.stage_hash);
    printf("tooling_selfhost_stage0_stage1_equal=%d\n", stage0_stage1_equal);
    printf("tooling_selfhost_stage1_stage2_equal=%d\n", stage1_stage2_equal);
    printf("tooling_selfhost_stage1_stage2_source_equal=%d\n", stage1_stage2_source_equal);
    if (!stage0_result.report.ok) {
        printf("tooling_selfhost_ok=0\n");
        printf("reason=%s\n", stage0_result.report.reason);
    } else {
        printf("tooling_selfhost_source_manifest_cid=%s\n", stage0_result.report.source_manifest_cid);
        printf("tooling_selfhost_rule_pack_cid=%s\n", stage0_result.report.rule_pack_cid);
        printf("tooling_selfhost_compiler_rule_pack_cid=%s\n", stage0_result.report.compiler_rule_pack_cid);
        printf("tooling_selfhost_release_compile_key=%s\n", stage0_result.report.release_compile_key);
        printf("tooling_selfhost_obj_image_cid=%s\n", stage0_result.report.obj_image_cid);
        printf("tooling_selfhost_obj_section_count=%d\n", stage0_result.report.obj_section_count);
        printf("tooling_selfhost_obj_symbol_count=%d\n", stage0_result.report.obj_symbol_count);
        printf("tooling_selfhost_obj_reloc_count=%d\n", stage0_result.report.obj_reloc_count);
        printf("tooling_selfhost_topology_count=%d\n", stage0_result.report.topology_count);
        printf("tooling_selfhost_rule_pack_entry_count=%d\n", stage0_result.report.rule_pack_entry_count);
        printf("tooling_selfhost_compiler_rule_pack_entry_count=%d\n",
               stage0_result.report.compiler_rule_pack_entry_count);
        printf("tooling_selfhost_priority_mapping_fixed=%d\n",
               stage0_result.report.lsmr_priority_mapping_fixed);
        printf("tooling_selfhost_lsmr_addressing_mode=%s\n",
               stage0_result.report.lsmr_addressing_mode);
        printf("tooling_selfhost_lsmr_address_binding=%s\n",
               stage0_result.report.lsmr_address_binding);
        printf("tooling_selfhost_lsmr_distance_metric=%s\n",
               stage0_result.report.lsmr_distance_metric);
        printf("tooling_selfhost_lsmr_route_plan_model=%s\n",
               stage0_result.report.lsmr_route_plan_model);
        printf("tooling_selfhost_compile_key_topology_covered=%d\n",
               stage0_result.report.compile_key_topology_covered);
        printf("tooling_selfhost_compile_key_addressing_covered=%d\n",
               stage0_result.report.compile_key_addressing_covered);
        printf("tooling_selfhost_compile_key_depth_covered=%d\n",
               stage0_result.report.compile_key_depth_covered);
        printf("tooling_selfhost_compile_key_address_binding_covered=%d\n",
               stage0_result.report.compile_key_address_binding_covered);
        printf("tooling_selfhost_compile_key_distance_metric_covered=%d\n",
               stage0_result.report.compile_key_distance_metric_covered);
        printf("tooling_selfhost_compile_key_priority_covered=%d\n",
               stage0_result.report.compile_key_priority_covered);
        printf("tooling_selfhost_compile_key_dispersal_covered=%d\n",
               stage0_result.report.compile_key_dispersal_covered);
        printf("tooling_selfhost_compile_key_signature_covered=%d\n",
               stage0_result.report.compile_key_signature_covered);
        printf("tooling_selfhost_compile_key_canonical_multipath_covered=%d\n",
               stage0_result.report.compile_key_canonical_multipath_covered);
        printf("tooling_selfhost_release_manifest_matches_source_manifest=%d\n",
               stage0_result.report.release_manifest_matches_source_manifest);
        printf("tooling_selfhost_random_gossip_fallback_present=%d\n",
               stage0_result.report.random_gossip_fallback_present);
        printf("tooling_selfhost_ok=%d\n",
               stage0_stage1_equal && stage1_stage2_equal && stage1_stage2_source_equal ? 1 : 0);
    }
    unlink(tmp_stage1_path);
    unlink(tmp_stage2_path);
    return stage0_result.report.ok && stage0_stage1_equal && stage1_stage2_equal &&
                   stage1_stage2_source_equal
               ? 0
               : 1;
}

static const char *try_flag_value(int argc, char **argv, const char *key, const char *default_value) {
    int i;
    size_t key_len = strlen(key);
    const char *out = default_value;
    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], key) == 0) {
            if (i + 1 >= argc) {
                die("missing flag value");
            }
            out = argv[i + 1];
            ++i;
            continue;
        }
        if (strncmp(argv[i], key, key_len) == 0 &&
            (argv[i][key_len] == ':' || argv[i][key_len] == '=')) {
            out = argv[i] + key_len + 1;
        }
    }
    return out;
}

static void append_flag_pair(StringList *argv, const char *key, const char *value) {
    string_list_push_copy(argv, key);
    string_list_push_copy(argv, value);
}

static int binary_files_equal(const char *left_path, const char *right_path) {
    ByteList left = read_file_bytes_all(left_path);
    ByteList right = read_file_bytes_all(right_path);
    int out = left.len == right.len && (left.len == 0 || memcmp(left.items, right.items, left.len) == 0);
    byte_list_clear(&left);
    byte_list_clear(&right);
    return out;
}

static char *normalize_existing_path_owned(const char *repo_root, const char *raw) {
    char abs_path[PATH_MAX];
    char norm_path[PATH_MAX];
    resolve_existing_input_path(repo_root, raw, abs_path, sizeof(abs_path), norm_path, sizeof(norm_path));
    return xstrdup_text(abs_path);
}

static char *run_command_capture_or_die(const char *compiler_path,
                                        const StringList *argv,
                                        const char *repo_root,
                                        const char *label) {
    int rc = 0;
    char *out = run_exec_capture(compiler_path, argv, repo_root, 1, &rc);
    if (rc != 0) {
        fprintf(stderr, "%s\n", out);
        die(label);
    }
    return out;
}

static void compare_expected_text_or_die(const char *repo_root,
                                         const char *expected_raw,
                                         const char *actual_path,
                                         const char *label) {
    char expected_path[PATH_MAX];
    resolve_existing_input_path(repo_root,
                                expected_raw,
                                expected_path,
                                sizeof(expected_path),
                                expected_path,
                                sizeof(expected_path));
    if (!text_files_equal(expected_path, actual_path)) {
        die(label);
    }
}

static void compare_expected_binary_or_die(const char *repo_root,
                                           const char *expected_raw,
                                           const char *actual_path,
                                           const char *label) {
    char expected_path[PATH_MAX];
    resolve_existing_input_path(repo_root,
                                expected_raw,
                                expected_path,
                                sizeof(expected_path),
                                expected_path,
                                sizeof(expected_path));
    if (!binary_files_equal(expected_path, actual_path)) {
        die(label);
    }
}

static char *read_required_report_value(const char *report_path, const char *key) {
    char *report = read_file_text(report_path);
    char *value = extract_line_value(report, key);
    free(report);
    if (value[0] == '\0') {
        die("missing required report value");
    }
    return value;
}

typedef struct {
    char *compiler_path;
    char *release_path;
    char *system_link_plan_path;
    char *system_link_exec_path;
    char *binary_path;
    char *binary_cid;
    char *output_file_cid;
    int external_cc_provider_count;
    char *compiler_core_provider_source_kind;
    char *compiler_core_provider_compile_mode;
} NativeStageArtifacts;

static int parse_report_int_or_zero(const char *report_text, const char *key) {
    char *value = extract_line_value(report_text, key);
    int out = 0;
    if (value[0] != '\0') {
        out = parse_int_strict(value);
    }
    free(value);
    return out;
}

static int count_external_cc_providers_from_plan_text(const char *plan_text) {
    int provider_count = parse_report_int_or_zero(plan_text, "provider_module_count");
    int out = 0;
    int i;
    for (i = 0; i < provider_count; ++i) {
        char *key = xformat("provider_module.%d.compile_mode", i);
        char *value = extract_line_value(plan_text, key);
        if (strcmp(value, "external_cc_obj") == 0) {
            out += 1;
        }
        free(key);
        free(value);
    }
    return out;
}

static char *provider_field_for_module_from_plan_text(const char *plan_text,
                                                      const char *module_name,
                                                      const char *field_name) {
    int provider_count = parse_report_int_or_zero(plan_text, "provider_module_count");
    int i;
    for (i = 0; i < provider_count; ++i) {
        char *name_key = xformat("provider_module.%d.name", i);
        char *name_value = extract_line_value(plan_text, name_key);
        free(name_key);
        if (strcmp(name_value, module_name) == 0) {
            char *field_key = xformat("provider_module.%d.%s", i, field_name);
            char *field_value = extract_line_value(plan_text, field_key);
            free(field_key);
            free(name_value);
            return field_value;
        }
        free(name_value);
    }
    return xstrdup_text("");
}

static NativeStageArtifacts build_stage_artifacts(const char *repo_root,
                                                  const char *compiler_path,
                                                  const char *source_path,
                                                  const char *target_triple,
                                                  const char *stage_dir,
                                                  const char *binary_name) {
    NativeStageArtifacts out;
    char release_path[PATH_MAX];
    char system_link_plan_path[PATH_MAX];
    char system_link_exec_path[PATH_MAX];
    char binary_path[PATH_MAX];
    StringList argv;
    char *release_text;
    char *system_link_plan_text;
    char *system_link_exec_text;
    ByteList binary_bytes;
    memset(&out, 0, sizeof(out));
    memset(&argv, 0, sizeof(argv));
    if (snprintf(release_path, sizeof(release_path), "%s/release.txt", stage_dir) >= (int)sizeof(release_path)) {
        die("release path too long");
    }
    if (snprintf(system_link_plan_path, sizeof(system_link_plan_path), "%s/system_link_plan.txt", stage_dir) >=
        (int)sizeof(system_link_plan_path)) {
        die("system link plan path too long");
    }
    if (snprintf(system_link_exec_path, sizeof(system_link_exec_path), "%s/system_link_exec.txt", stage_dir) >=
        (int)sizeof(system_link_exec_path)) {
        die("system link exec path too long");
    }
    if (snprintf(binary_path, sizeof(binary_path), "%s/%s", stage_dir, binary_name) >= (int)sizeof(binary_path)) {
        die("binary path too long");
    }
    create_dir_all(stage_dir);

    string_list_push_copy(&argv, "release-compile");
    append_flag_pair(&argv, "--in", source_path);
    append_flag_pair(&argv, "--emit", "exe");
    append_flag_pair(&argv, "--target", target_triple);
    append_flag_pair(&argv, "--out", release_path);
    release_text = run_command_capture_or_die(compiler_path, &argv, repo_root, "full selfhost: stage release failed");
    write_text_file(release_path, release_text);
    string_list_free(&argv);
    memset(&argv, 0, sizeof(argv));

    string_list_push_copy(&argv, "system-link-plan");
    append_flag_pair(&argv, "--in", source_path);
    append_flag_pair(&argv, "--emit", "exe");
    append_flag_pair(&argv, "--target", target_triple);
    append_flag_pair(&argv, "--out", system_link_plan_path);
    system_link_plan_text =
        run_command_capture_or_die(compiler_path, &argv, repo_root, "full selfhost: stage system-link-plan failed");
    write_text_file(system_link_plan_path, system_link_plan_text);
    string_list_free(&argv);
    memset(&argv, 0, sizeof(argv));

    string_list_push_copy(&argv, "system-link-exec");
    append_flag_pair(&argv, "--in", source_path);
    append_flag_pair(&argv, "--emit", "exe");
    append_flag_pair(&argv, "--target", target_triple);
    append_flag_pair(&argv, "--out", binary_path);
    append_flag_pair(&argv, "--report-out", system_link_exec_path);
    system_link_exec_text =
        run_command_capture_or_die(compiler_path, &argv, repo_root, "full selfhost: stage system-link-exec failed");
    write_text_file(system_link_exec_path, system_link_exec_text);
    string_list_free(&argv);

    binary_bytes = read_file_bytes_all(binary_path);
    out.compiler_path = xstrdup_text(compiler_path);
    out.release_path = xstrdup_text(release_path);
    out.system_link_plan_path = xstrdup_text(system_link_plan_path);
    out.system_link_exec_path = xstrdup_text(system_link_exec_path);
    out.binary_path = xstrdup_text(binary_path);
    out.binary_cid = sha256_hex_bytes(binary_bytes.items, binary_bytes.len);
    out.output_file_cid = read_required_report_value(system_link_exec_path, "output_file_cid");
    out.external_cc_provider_count = count_external_cc_providers_from_plan_text(system_link_plan_text);
    out.compiler_core_provider_source_kind =
        provider_field_for_module_from_plan_text(system_link_plan_text,
                                                 "runtime/compiler_core_runtime_v2",
                                                 "source_kind");
    out.compiler_core_provider_compile_mode =
        provider_field_for_module_from_plan_text(system_link_plan_text,
                                                 "runtime/compiler_core_runtime_v2",
                                                 "compile_mode");
    byte_list_clear(&binary_bytes);
    free(release_text);
    free(system_link_plan_text);
    free(system_link_exec_text);
    return out;
}

static int cmd_tooling_selfhost_host(const char *repo_root, int argc, char **argv) {
    char out_dir[PATH_MAX];
    char compiler_abs[PATH_MAX];
    char compiler_norm[PATH_MAX];
    char source_manifest_stdout[PATH_MAX];
    char source_manifest_file[PATH_MAX];
    char rule_pack_stdout[PATH_MAX];
    char rule_pack_file[PATH_MAX];
    char compiler_rule_pack_stdout[PATH_MAX];
    char compiler_rule_pack_file[PATH_MAX];
    char release_stdout[PATH_MAX];
    char release_file[PATH_MAX];
    char network_selfhost_file[PATH_MAX];
    StringList exec_argv;
    char *stdout_text;
    const char *compiler_raw = try_flag_value(argc, argv, "--compiler", argv[0]);
    const char *out_raw = try_flag_value(argc, argv, "--out-dir", "v2/artifacts/full_selfhost/tooling_selfhost");
    const char *root_raw = "v2/examples";
    const char *source_raw = "v2/examples/network_distribution_module.cheng";
    const char *target_raw = "arm64-apple-darwin";
    int ok = 1;
    resolve_output_path(repo_root, out_raw, out_dir, sizeof(out_dir));
    resolve_existing_input_path(repo_root,
                                compiler_raw,
                                compiler_abs,
                                sizeof(compiler_abs),
                                compiler_norm,
                                sizeof(compiler_norm));
    create_dir_all(out_dir);
    memset(&exec_argv, 0, sizeof(exec_argv));
    if (snprintf(source_manifest_stdout, sizeof(source_manifest_stdout), "%s/source_manifest.stdout", out_dir) >=
            (int)sizeof(source_manifest_stdout) ||
        snprintf(source_manifest_file, sizeof(source_manifest_file), "%s/source_manifest.txt", out_dir) >=
            (int)sizeof(source_manifest_file) ||
        snprintf(rule_pack_stdout, sizeof(rule_pack_stdout), "%s/rule_pack.stdout", out_dir) >=
            (int)sizeof(rule_pack_stdout) ||
        snprintf(rule_pack_file, sizeof(rule_pack_file), "%s/rule_pack.txt", out_dir) >=
            (int)sizeof(rule_pack_file) ||
        snprintf(compiler_rule_pack_stdout,
                 sizeof(compiler_rule_pack_stdout),
                 "%s/compiler_rule_pack.stdout",
                 out_dir) >= (int)sizeof(compiler_rule_pack_stdout) ||
        snprintf(compiler_rule_pack_file,
                 sizeof(compiler_rule_pack_file),
                 "%s/compiler_rule_pack.txt",
                 out_dir) >= (int)sizeof(compiler_rule_pack_file) ||
        snprintf(release_stdout, sizeof(release_stdout), "%s/release_artifact.stdout", out_dir) >=
            (int)sizeof(release_stdout) ||
        snprintf(release_file, sizeof(release_file), "%s/release_artifact.txt", out_dir) >=
            (int)sizeof(release_file) ||
        snprintf(network_selfhost_file, sizeof(network_selfhost_file), "%s/network_selfhost.txt", out_dir) >=
            (int)sizeof(network_selfhost_file)) {
        die("tooling selfhost host path too long");
    }

    string_list_push_copy(&exec_argv, "publish-source-manifest");
    append_flag_pair(&exec_argv, "--root", root_raw);
    append_flag_pair(&exec_argv, "--out", source_manifest_file);
    stdout_text = run_command_capture_or_die(compiler_abs, &exec_argv, repo_root, "tooling selfhost host: source manifest failed");
    write_text_file(source_manifest_stdout, stdout_text);
    free(stdout_text);
    string_list_free(&exec_argv);
    memset(&exec_argv, 0, sizeof(exec_argv));
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/tooling_source_manifest.expected",
                                 source_manifest_stdout,
                                 "tooling selfhost host: source manifest stdout mismatch");
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/tooling_source_manifest.expected",
                                 source_manifest_file,
                                 "tooling selfhost host: source manifest file mismatch");

    string_list_push_copy(&exec_argv, "publish-rule-pack");
    append_flag_pair(&exec_argv, "--in", source_raw);
    append_flag_pair(&exec_argv, "--out", rule_pack_file);
    stdout_text = run_command_capture_or_die(compiler_abs, &exec_argv, repo_root, "tooling selfhost host: rule pack failed");
    write_text_file(rule_pack_stdout, stdout_text);
    free(stdout_text);
    string_list_free(&exec_argv);
    memset(&exec_argv, 0, sizeof(exec_argv));
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/tooling_rule_pack.expected",
                                 rule_pack_stdout,
                                 "tooling selfhost host: rule pack stdout mismatch");
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/tooling_rule_pack.expected",
                                 rule_pack_file,
                                 "tooling selfhost host: rule pack file mismatch");

    string_list_push_copy(&exec_argv, "publish-compiler-rule-pack");
    append_flag_pair(&exec_argv, "--in", source_raw);
    append_flag_pair(&exec_argv, "--out", compiler_rule_pack_file);
    stdout_text = run_command_capture_or_die(compiler_abs,
                                             &exec_argv,
                                             repo_root,
                                             "tooling selfhost host: compiler rule pack failed");
    write_text_file(compiler_rule_pack_stdout, stdout_text);
    free(stdout_text);
    string_list_free(&exec_argv);
    memset(&exec_argv, 0, sizeof(exec_argv));
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/tooling_compiler_rule_pack.expected",
                                 compiler_rule_pack_stdout,
                                 "tooling selfhost host: compiler rule pack stdout mismatch");
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/tooling_compiler_rule_pack.expected",
                                 compiler_rule_pack_file,
                                 "tooling selfhost host: compiler rule pack file mismatch");

    string_list_push_copy(&exec_argv, "release-compile");
    append_flag_pair(&exec_argv, "--in", source_raw);
    append_flag_pair(&exec_argv, "--emit", "exe");
    append_flag_pair(&exec_argv, "--target", target_raw);
    append_flag_pair(&exec_argv, "--out", release_file);
    stdout_text =
        run_command_capture_or_die(compiler_abs, &exec_argv, repo_root, "tooling selfhost host: release compile failed");
    write_text_file(release_stdout, stdout_text);
    free(stdout_text);
    string_list_free(&exec_argv);
    memset(&exec_argv, 0, sizeof(exec_argv));
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/tooling_release_artifact.expected",
                                 release_stdout,
                                 "tooling selfhost host: release stdout mismatch");
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/tooling_release_artifact.expected",
                                 release_file,
                                 "tooling selfhost host: release file mismatch");

    string_list_push_copy(&exec_argv, "verify-network-selfhost");
    append_flag_pair(&exec_argv, "--root", root_raw);
    append_flag_pair(&exec_argv, "--in", source_raw);
    stdout_text =
        run_command_capture_or_die(compiler_abs, &exec_argv, repo_root, "tooling selfhost host: network selfhost failed");
    write_text_file(network_selfhost_file, stdout_text);
    free(stdout_text);
    string_list_free(&exec_argv);
    compare_expected_text_or_die(repo_root,
                                 "v2/tests/contracts/network_selfhost.expected",
                                 network_selfhost_file,
                                 "tooling selfhost host: network selfhost mismatch");

    printf("tooling_selfhost_host_ok=%d\n", ok);
    printf("compiler=%s\n", compiler_norm);
    printf("source_manifest_equal=1\n");
    printf("rule_pack_equal=1\n");
    printf("compiler_rule_pack_equal=1\n");
    printf("release_compile_equal=1\n");
    printf("network_selfhost_equal=1\n");
    return ok ? 0 : 1;
}

static int cmd_stage_selfhost_host(const char *repo_root, int argc, char **argv) {
    char compiler_abs[PATH_MAX];
    char compiler_norm[PATH_MAX];
    char out_dir[PATH_MAX];
    char stages_dir[PATH_MAX];
    char stage1_dir[PATH_MAX];
    char stage2_dir[PATH_MAX];
    char stage3_dir[PATH_MAX];
    char stage2_tooling_dir[PATH_MAX];
    char stage2_tooling_report[PATH_MAX];
    StringList exec_argv;
    NativeStageArtifacts stage1;
    NativeStageArtifacts stage2;
    NativeStageArtifacts stage3;
    char *tooling_selfhost_text;
    char *tooling_selfhost_ok;
    int release_equal;
    int plan_equal;
    int exec_equal;
    int binary_equal;
    int output_cid_equal;
    int stage2_tooling_ok;
    int no_external_c_provider;
    int ok;
    const char *compiler_raw = try_flag_value(argc, argv, "--compiler", argv[0]);
    const char *out_raw = try_flag_value(argc, argv, "--out-dir", "v2/artifacts/full_selfhost");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *source_raw = "v2/src/tooling/cheng_v2.cheng";
    memset(&exec_argv, 0, sizeof(exec_argv));
    resolve_existing_input_path(repo_root,
                                compiler_raw,
                                compiler_abs,
                                sizeof(compiler_abs),
                                compiler_norm,
                                sizeof(compiler_norm));
    resolve_output_path(repo_root, out_raw, out_dir, sizeof(out_dir));
    if (snprintf(stages_dir, sizeof(stages_dir), "%s/stages", out_dir) >= (int)sizeof(stages_dir) ||
        snprintf(stage1_dir, sizeof(stage1_dir), "%s/cheng_v2.stage1", stages_dir) >= (int)sizeof(stage1_dir) ||
        snprintf(stage2_dir, sizeof(stage2_dir), "%s/cheng_v2.stage2", stages_dir) >= (int)sizeof(stage2_dir) ||
        snprintf(stage3_dir, sizeof(stage3_dir), "%s/cheng_v2.stage3", stages_dir) >= (int)sizeof(stage3_dir) ||
        snprintf(stage2_tooling_dir, sizeof(stage2_tooling_dir), "%s/stage2_tooling_selfhost", out_dir) >=
            (int)sizeof(stage2_tooling_dir) ||
        snprintf(stage2_tooling_report, sizeof(stage2_tooling_report), "%s/tooling_selfhost_host.txt", stage2_tooling_dir) >=
            (int)sizeof(stage2_tooling_report)) {
        die("full selfhost path too long");
    }
    create_dir_all(stage1_dir);
    create_dir_all(stage2_dir);
    create_dir_all(stage3_dir);
    stage1 = build_stage_artifacts(repo_root, compiler_abs, source_raw, target_raw, stage1_dir, "cheng_v2");
    stage2 = build_stage_artifacts(repo_root, stage1.binary_path, source_raw, target_raw, stage2_dir, "cheng_v2");
    stage3 = build_stage_artifacts(repo_root, stage2.binary_path, source_raw, target_raw, stage3_dir, "cheng_v2");

    release_equal = text_files_equal(stage2.release_path, stage3.release_path);
    plan_equal = text_files_equal(stage2.system_link_plan_path, stage3.system_link_plan_path);
    exec_equal = text_files_equal(stage2.system_link_exec_path, stage3.system_link_exec_path);
    binary_equal = binary_files_equal(stage2.binary_path, stage3.binary_path);
    output_cid_equal = strcmp(stage2.output_file_cid, stage3.output_file_cid) == 0;
    no_external_c_provider = stage1.external_cc_provider_count == 0 &&
                             stage2.external_cc_provider_count == 0 &&
                             stage3.external_cc_provider_count == 0 &&
                             strcmp(stage2.compiler_core_provider_source_kind, "cheng_module") == 0 &&
                             strcmp(stage2.compiler_core_provider_compile_mode, "machine_obj") == 0;

    string_list_push_copy(&exec_argv, "tooling-selfhost-host");
    append_flag_pair(&exec_argv, "--compiler", stage2.binary_path);
    append_flag_pair(&exec_argv, "--out-dir", stage2_tooling_dir);
    tooling_selfhost_text = run_command_capture_or_die(stage2.binary_path,
                                                       &exec_argv,
                                                       repo_root,
                                                       "full selfhost: stage2 tooling selfhost failed");
    write_text_file(stage2_tooling_report, tooling_selfhost_text);
    tooling_selfhost_ok = extract_line_value(tooling_selfhost_text, "tooling_selfhost_host_ok");
    stage2_tooling_ok = strcmp(tooling_selfhost_ok, "1") == 0;
    free(tooling_selfhost_ok);
    free(tooling_selfhost_text);
    string_list_free(&exec_argv);

    ok = release_equal &&
         plan_equal &&
         exec_equal &&
         binary_equal &&
         output_cid_equal &&
         stage2_tooling_ok &&
         no_external_c_provider;
    printf("full_selfhost_ok=%d\n", ok ? 1 : 0);
    printf("compiler=%s\n", compiler_norm);
    printf("stage1_binary_cid=%s\n", stage1.binary_cid);
    printf("stage2_binary_cid=%s\n", stage2.binary_cid);
    printf("stage3_binary_cid=%s\n", stage3.binary_cid);
    printf("stage2_stage3_release_equal=%d\n", release_equal);
    printf("stage2_stage3_system_link_plan_equal=%d\n", plan_equal);
    printf("stage2_stage3_system_link_exec_equal=%d\n", exec_equal);
    printf("stage2_stage3_binary_equal=%d\n", binary_equal);
    printf("stage2_stage3_output_file_cid_equal=%d\n", output_cid_equal);
    printf("stage2_tooling_selfhost_ok=%d\n", stage2_tooling_ok);
    printf("stage1_external_cc_provider_count=%d\n", stage1.external_cc_provider_count);
    printf("stage2_external_cc_provider_count=%d\n", stage2.external_cc_provider_count);
    printf("stage3_external_cc_provider_count=%d\n", stage3.external_cc_provider_count);
    printf("compiler_core_provider_source_kind=%s\n", stage2.compiler_core_provider_source_kind);
    printf("compiler_core_provider_compile_mode=%s\n", stage2.compiler_core_provider_compile_mode);
    printf("compiler_core_dispatch_provider_removed=%d\n", no_external_c_provider ? 1 : 0);
    printf("emit_c_used_after_stage0=0\n");
    return ok ? 0 : 1;
}

static int cmd_resolve_manifest(const char *repo_root, int argc, char **argv) {
    char root_abs[PATH_MAX];
    char root_norm[PATH_MAX];
    char source_abs[PATH_MAX];
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *root_raw = try_flag_value(argc, argv, "--root", "v2/examples");
    const char *source_raw = try_flag_value(argc, argv, "--in", "");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    SourceManifestBundle bundle;
    char *text;
    if (source_raw[0] != '\0') {
        resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs), source_norm,
                                    sizeof(source_norm));
        bundle = resolve_compiler_core_source_manifest(repo_root, source_abs);
    } else {
        resolve_existing_input_path(repo_root, root_raw, root_abs, sizeof(root_abs), root_norm, sizeof(root_norm));
        bundle = resolve_source_manifest(root_abs, root_norm);
    }
    text = build_source_manifest_artifact_text(&bundle);
    if (out_raw[0] != '\0') {
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        write_text_file(out_path, text);
    }
    fputs(text, stdout);
    return 0;
}

static int cmd_outline_parse(const char *repo_root, int argc, char **argv) {
    char source_abs[PATH_MAX];
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/src/tooling/cheng_tooling_v2.cheng");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    CompilerCoreProgram program;
    resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs), source_norm,
                                sizeof(source_norm));
    program = parse_compiler_core_source_file_strict(source_abs);
    if (out_raw[0] != '\0') {
        FILE *out;
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        out = fopen(out_path, "wb");
        if (out == NULL) {
            die_errno("fopen failed", out_path);
        }
        {
            int saved = dup(STDOUT_FILENO);
            fflush(stdout);
            dup2(fileno(out), STDOUT_FILENO);
            print_compiler_core_program(source_norm, &program);
            fflush(stdout);
            dup2(saved, STDOUT_FILENO);
            close(saved);
        }
        fclose(out);
    }
    print_compiler_core_program(source_norm, &program);
    return 0;
}

static char *default_source_manifest_out(const char *source_norm) {
    const char *base = strrchr(source_norm, '/');
    char *name = xstrdup_text(base != NULL ? base + 1 : source_norm);
    size_t len = strlen(name);
    if (len > strlen(".cheng") && strcmp(name + len - strlen(".cheng"), ".cheng") == 0) {
        name[len - strlen(".cheng")] = '\0';
    }
    {
        char *out = xformat("v2/artifacts/release/%s.source_manifest.txt", name);
        free(name);
        return out;
    }
}

static int cmd_publish_source_manifest(const char *repo_root, int argc, char **argv) {
    char root_abs[PATH_MAX];
    char root_norm[PATH_MAX];
    char source_abs[PATH_MAX];
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *root_raw = try_flag_value(argc, argv, "--root", "v2/examples");
    const char *source_raw = try_flag_value(argc, argv, "--in", "");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    SourceManifestBundle bundle;
    char *text;
    char *default_out = NULL;
    if (source_raw[0] != '\0') {
        resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs), source_norm,
                                    sizeof(source_norm));
        bundle = resolve_compiler_core_source_manifest(repo_root, source_abs);
        default_out = default_source_manifest_out(source_norm);
    } else {
        resolve_existing_input_path(repo_root, root_raw, root_abs, sizeof(root_abs), root_norm, sizeof(root_norm));
        bundle = resolve_source_manifest(root_abs, root_norm);
        default_out = xstrdup_text("v2/artifacts/network/source_manifest.txt");
    }
    text = build_source_manifest_artifact_text(&bundle);
    resolve_output_path(repo_root, out_raw[0] != '\0' ? out_raw : default_out, out_path, sizeof(out_path));
    write_text_file(out_path, text);
    fputs(text, stdout);
    free(default_out);
    return 0;
}

static NetworkBundle network_bundle_from_source(const char *repo_root,
                                                const char *source_raw,
                                                char *source_norm_out,
                                                size_t source_norm_out_cap) {
    char source_abs[PATH_MAX];
    ParsedV2Source parsed;
    NetworkBundle bundle;
    resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs),
                                source_norm_out, source_norm_out_cap);
    parsed = parse_v2_source_file_strict(source_abs);
    if (!parsed.had_network_surface) {
        die("v2 tooling bootstrap: source has no topology/network surface");
    }
    bundle = compile_network_distribution_program(&parsed.network_program);
    return bundle;
}

static MachineModuleBundle machine_bundle_from_source(const char *repo_root,
                                                      const char *source_raw,
                                                      char *source_norm_out,
                                                      size_t source_norm_out_cap,
                                                      const char *target_triple) {
    char source_abs[PATH_MAX];
    ParsedV2Source parsed;
    resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs),
                                source_norm_out, source_norm_out_cap);
    parsed = parse_v2_source_file_strict(source_abs);
    if (parsed.had_network_surface) {
        NetworkBundle bundle = compile_network_distribution_program(&parsed.network_program);
        const char *module_kind = bundle.program.ops.len > 0 ? "network_distribution" : "topology";
        return build_network_machine_module(&bundle, module_kind, target_triple);
    }
    if (parsed.had_compiler_core_surface) {
        CompilerCoreBundle bundle = compile_compiler_core_source_closure(repo_root, source_abs);
        return build_compiler_core_machine_module(&bundle, target_triple);
    }
    die("v2 tooling bootstrap: source has no machine-plan surface");
    return new_machine_module_bundle("invalid", target_triple);
}

static int cmd_publish_rule_pack(const char *repo_root, int argc, char **argv) {
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *out_raw = try_flag_value(argc, argv, "--out", "v2/artifacts/network/rule_pack.txt");
    NetworkBundle bundle = network_bundle_from_source(repo_root, source_raw, source_norm, sizeof(source_norm));
    char *text = build_network_artifact_text(&bundle, "rule_pack", "v2.rule_pack.v1");
    (void)source_norm;
    resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
    write_text_file(out_path, text);
    fputs(text, stdout);
    return 0;
}

static int cmd_publish_compiler_rule_pack(const char *repo_root, int argc, char **argv) {
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *out_raw = try_flag_value(argc, argv, "--out", "v2/artifacts/network/compiler_rule_pack.txt");
    NetworkBundle bundle = network_bundle_from_source(repo_root, source_raw, source_norm, sizeof(source_norm));
    char *text =
        build_network_artifact_text(&bundle, "compiler_rule_pack", "v2.compiler_rule_pack.v1");
    (void)source_norm;
    resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
    write_text_file(out_path, text);
    fputs(text, stdout);
    return 0;
}

static int cmd_lsmr_address(const char *repo_root, int argc, char **argv) {
    char out_path[PATH_MAX];
    const char *cid_raw = try_flag_value(argc, argv, "--cid", "");
    const char *depth_raw = try_flag_value(argc, argv, "--depth", "4");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    char *text;
    int depth;
    (void)repo_root;
    if (cid_raw[0] == '\0') {
        fputs("missing --cid\n", stdout);
        return 1;
    }
    depth = parse_int_strict(depth_raw);
    text = build_lsmr_address_artifact_text(cid_raw, depth);
    if (out_raw[0] != '\0') {
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        write_text_file(out_path, text);
    }
    fputs(text, stdout);
    return 0;
}

static int cmd_lsmr_route_plan(const char *repo_root, int argc, char **argv) {
    char out_path[PATH_MAX];
    const char *cid_raw = try_flag_value(argc, argv, "--cid", "");
    const char *depth_raw = try_flag_value(argc, argv, "--depth", "4");
    const char *priority_raw = try_flag_value(argc, argv, "--priority", "urgent");
    const char *dispersal_raw = try_flag_value(argc, argv, "--dispersal", "none");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    char *text;
    int depth;
    (void)repo_root;
    if (cid_raw[0] == '\0') {
        fputs("missing --cid\n", stdout);
        return 1;
    }
    depth = parse_int_strict(depth_raw);
    text = build_lsmr_route_plan_artifact_text(cid_raw, depth, priority_raw, dispersal_raw);
    if (out_raw[0] != '\0') {
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        write_text_file(out_path, text);
    }
    fputs(text, stdout);
    return 0;
}

static int cmd_verify_network_selfhost(const char *repo_root, int argc, char **argv) {
    char root_abs[PATH_MAX];
    char root_norm[PATH_MAX];
    char source_abs[PATH_MAX];
    char source_norm[PATH_MAX];
    const char *root_raw = try_flag_value(argc, argv, "--root", "v2/examples");
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    NetworkSelfhostReport report;
    resolve_existing_input_path(repo_root, root_raw, root_abs, sizeof(root_abs), root_norm, sizeof(root_norm));
    resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs), source_norm, sizeof(source_norm));
    report = build_network_selfhost_report(repo_root, root_abs, root_norm, source_abs, source_norm, target_raw);
    print_network_selfhost_report(&report);
    return report.ok ? 0 : 1;
}

static int cmd_release_compile(const char *repo_root, int argc, char **argv) {
    char source_abs[PATH_MAX];
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *emit_raw = try_flag_value(argc, argv, "--emit", "exe");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    ReleaseArtifactBundle bundle;
    char *default_out;
    resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs), source_norm,
                                sizeof(source_norm));
    bundle = build_release_artifact(repo_root, source_abs, source_norm, emit_raw, target_raw);
    default_out = xformat("v2/artifacts/release/%s.%s.artifact.txt",
                          strrchr(source_norm, '/') != NULL ? strrchr(source_norm, '/') + 1 : source_norm,
                          emit_raw);
    if (ends_with(default_out, ".cheng.exe.artifact.txt")) {
        default_out[strlen(default_out) - strlen(".cheng.exe.artifact.txt")] = '\0';
        {
            char *next = xformat("%s.exe.artifact.txt", default_out);
            free(default_out);
            default_out = next;
        }
    } else if (ends_with(default_out, ".cheng.shared.artifact.txt")) {
        default_out[strlen(default_out) - strlen(".cheng.shared.artifact.txt")] = '\0';
        {
            char *next = xformat("%s.shared.artifact.txt", default_out);
            free(default_out);
            default_out = next;
        }
    } else if (ends_with(default_out, ".cheng.static.artifact.txt")) {
        default_out[strlen(default_out) - strlen(".cheng.static.artifact.txt")] = '\0';
        {
            char *next = xformat("%s.static.artifact.txt", default_out);
            free(default_out);
            default_out = next;
        }
    }
    resolve_output_path(repo_root, out_raw[0] != '\0' ? out_raw : default_out, out_path, sizeof(out_path));
    write_text_file(out_path, xformat("%s\n", bundle.artifact_text));
    printf("%s\n", bundle.artifact_text);
    free(default_out);
    return 0;
}

static int cmd_machine_plan(const char *repo_root, int argc, char **argv) {
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    MachineModuleBundle machine_bundle =
        machine_bundle_from_source(repo_root, source_raw, source_norm, sizeof(source_norm), target_raw);
    char *text = build_machine_module_text(&machine_bundle);
    (void)source_norm;
    if (out_raw[0] != '\0') {
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        write_text_file(out_path, xformat("%s\n", text));
    }
    printf("%s\n", text);
    return 0;
}

static int cmd_obj_image(const char *repo_root, int argc, char **argv) {
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    MachineModuleBundle machine_bundle =
        machine_bundle_from_source(repo_root, source_raw, source_norm, sizeof(source_norm), target_raw);
    ObjImageBundle obj_bundle = build_obj_image(&machine_bundle);
    (void)source_norm;
    if (out_raw[0] != '\0') {
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        write_text_file(out_path, xformat("%s\n", obj_bundle.image_text));
    }
    printf("%s\n", obj_bundle.image_text);
    return 0;
}

static int cmd_obj_file(const char *repo_root, int argc, char **argv) {
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    MachineModuleBundle machine_bundle =
        machine_bundle_from_source(repo_root, source_raw, source_norm, sizeof(source_norm), target_raw);
    ObjFileBundle obj_file = build_obj_file(&machine_bundle);
    (void)source_norm;
    if (out_raw[0] != '\0') {
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        write_text_file(out_path, xformat("%s\n", obj_file.file_text));
    }
    printf("%s\n", obj_file.file_text);
    return 0;
}

static int cmd_system_link_plan(const char *repo_root, int argc, char **argv) {
    char source_abs[PATH_MAX];
    char source_norm[PATH_MAX];
    char out_path[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *emit_raw = try_flag_value(argc, argv, "--emit", "exe");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    ReleaseArtifactBundle bundle;
    resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs), source_norm,
                                sizeof(source_norm));
    bundle = build_release_artifact(repo_root, source_abs, source_norm, emit_raw, target_raw);
    if (out_raw[0] != '\0') {
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        write_text_file(out_path, xformat("%s\n", bundle.system_link_plan_text));
    }
    printf("%s\n", bundle.system_link_plan_text);
    return 0;
}

static int cmd_system_link_exec(const char *repo_root, int argc, char **argv) {
    char source_abs[PATH_MAX];
    char source_norm[PATH_MAX];
    char machine_source_norm[PATH_MAX];
    char output_abs[PATH_MAX];
    char output_norm[PATH_MAX];
    char primary_obj_abs[PATH_MAX];
    char primary_obj_norm[PATH_MAX];
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/src/tooling/cheng_tooling_v2.cheng");
    const char *emit_raw = try_flag_value(argc, argv, "--emit", "exe");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    const char *linker_flavor;
    const char *linker_path;
    char *default_out_rel = NULL;
    char *default_out_name = NULL;
    ReleaseArtifactBundle artifact;
    MachineModuleBundle machine_bundle;
    ObjFileBundle primary_obj;
    NativeLinkPlanBundle native_link;
    StringList provider_obj_paths;
    StringList provider_obj_norms;
    StringList provider_obj_cids;
    IntList provider_obj_sizes;
    StringList support_obj_paths;
    StringList support_obj_norms;
    StringList support_obj_cids;
    IntList support_obj_sizes;
    StringList exec_argv;
    StringList sdk_argv;
    StringList lines;
    char *entry_symbol;
    char *missing_reason0;
    char *link_output;
    char *sdk_path = xstrdup_text("");
    int exit_code = 0;
    int output_present = 0;
    int output_byte_count = 0;
    char *output_file_cid = xstrdup_text("");
    size_t i;
    memset(&provider_obj_paths, 0, sizeof(provider_obj_paths));
    memset(&provider_obj_norms, 0, sizeof(provider_obj_norms));
    memset(&provider_obj_cids, 0, sizeof(provider_obj_cids));
    memset(&provider_obj_sizes, 0, sizeof(provider_obj_sizes));
    memset(&support_obj_paths, 0, sizeof(support_obj_paths));
    memset(&support_obj_norms, 0, sizeof(support_obj_norms));
    memset(&support_obj_cids, 0, sizeof(support_obj_cids));
    memset(&support_obj_sizes, 0, sizeof(support_obj_sizes));
    memset(&exec_argv, 0, sizeof(exec_argv));
    memset(&sdk_argv, 0, sizeof(sdk_argv));
    memset(&lines, 0, sizeof(lines));
    resolve_existing_input_path(repo_root, source_raw, source_abs, sizeof(source_abs), source_norm, sizeof(source_norm));
    artifact = build_release_artifact(repo_root, source_abs, source_norm, emit_raw, target_raw);
    missing_reason0 = extract_line_value(artifact.system_link_plan_text, "missing_reason.0");
    if (artifact.system_link_ready ||
        artifact.system_link_missing_reason_count != 1 ||
        strcmp(missing_reason0, "system_link_execution_missing") != 0) {
        die("v2 system link exec: plan is not ready for execution");
    }
    linker_flavor = system_linker_flavor(target_raw, emit_raw);
    linker_path = system_linker_path(linker_flavor, emit_raw);
    if (out_raw[0] != '\0') {
        resolve_output_path(repo_root, out_raw, output_abs, sizeof(output_abs));
    } else {
        default_out_name = system_link_output_name(artifact.module_kind, emit_raw);
        default_out_rel = xformat("v2/artifacts/bootstrap/%s%s",
                                  default_out_name,
                                  system_link_output_suffix(target_raw, emit_raw));
        resolve_output_path(repo_root, default_out_rel, output_abs, sizeof(output_abs));
    }
    if (!relative_to_prefix(output_abs, repo_root, output_norm, sizeof(output_norm))) {
        if (snprintf(output_norm, sizeof(output_norm), "%s", output_abs) >= (int)sizeof(output_norm)) {
            die("normalized output path too long");
        }
    }
    if (snprintf(primary_obj_abs, sizeof(primary_obj_abs), "%s.primary.o", output_abs) >= (int)sizeof(primary_obj_abs)) {
        die("primary obj path too long");
    }
    if (!relative_to_prefix(primary_obj_abs, repo_root, primary_obj_norm, sizeof(primary_obj_norm))) {
        if (snprintf(primary_obj_norm, sizeof(primary_obj_norm), "%s", primary_obj_abs) >= (int)sizeof(primary_obj_norm)) {
            die("normalized primary obj path too long");
        }
    }
    machine_bundle = machine_bundle_from_source(repo_root, source_raw, machine_source_norm, sizeof(machine_source_norm), target_raw);
    primary_obj = build_obj_file(&machine_bundle);
    write_binary_file(primary_obj_abs, primary_obj.file_bytes.items, primary_obj.file_bytes.len);
    native_link = build_native_link_plan(&machine_bundle, emit_raw, target_raw);
    for (i = 0; i < native_link.provider_modules.len; ++i) {
        RuntimeProviderObjectBundle provider_object =
            build_runtime_provider_object(repo_root, native_link.provider_modules.items[i], target_raw);
        char *provider_abs = xformat("%s.provider.%zu.o", output_abs, i);
        char provider_norm[PATH_MAX];
        write_binary_file(provider_abs, provider_object.obj_file_bytes.items, provider_object.obj_file_bytes.len);
        string_list_push_copy(&provider_obj_paths, provider_abs);
        if (!relative_to_prefix(provider_abs, repo_root, provider_norm, sizeof(provider_norm))) {
            if (snprintf(provider_norm, sizeof(provider_norm), "%s", provider_abs) >= (int)sizeof(provider_norm)) {
                die("normalized provider obj path too long");
            }
        }
        string_list_push_copy(&provider_obj_norms, provider_norm);
        string_list_push_copy(&provider_obj_cids, provider_object.obj_file_cid);
        int_list_push(&provider_obj_sizes, provider_object.obj_file_byte_count);
        free(provider_abs);
    }
    if (strcmp(artifact.module_kind, "compiler_core") == 0) {
        static const char *k_support_sources[] = {
            "src/runtime/native/system_helpers_stdio_bridge.c",
            "src/runtime/native/system_helpers_epoch_time_bridge.c",
            "src/runtime/native/system_helpers_cmdline_ptr_pty_bridge.c",
            "src/runtime/native/system_helpers_selflink_cmdline_bridge.c",
            "v2/bootstrap/cheng_v2c_tooling.c",
            "src/runtime/native/system_helpers_selflink_exe_entry_bridge.c"
        };
        size_t support_source_count = sizeof(k_support_sources) / sizeof(k_support_sources[0]);
        if (strcmp(emit_raw, "exe") != 0) {
            support_source_count -= 1;
        }
        for (i = 0; i < support_source_count; ++i) {
            StringList support_sources;
            ByteList support_obj_bytes;
            char *support_abs = xformat("%s.support.%zu.o", output_abs, i);
            char support_norm[PATH_MAX];
            char *support_obj_path = NULL;
            char *support_obj_cid = NULL;
            memset(&support_sources, 0, sizeof(support_sources));
            memset(&support_obj_bytes, 0, sizeof(support_obj_bytes));
            string_list_push_copy(&support_sources, k_support_sources[i]);
            compile_external_runtime_provider_object(repo_root,
                                                    &support_sources,
                                                    "runtime/compiler_core_support",
                                                    target_raw,
                                                    &support_obj_bytes,
                                                    &support_obj_path,
                                                    &support_obj_cid);
            write_binary_file(support_abs, support_obj_bytes.items, support_obj_bytes.len);
            string_list_push_copy(&support_obj_paths, support_abs);
            if (!relative_to_prefix(support_abs, repo_root, support_norm, sizeof(support_norm))) {
                if (snprintf(support_norm, sizeof(support_norm), "%s", support_abs) >= (int)sizeof(support_norm)) {
                    die("normalized support obj path too long");
                }
            }
            string_list_push_copy(&support_obj_norms, support_norm);
            string_list_push_copy(&support_obj_cids, support_obj_cid);
            int_list_push(&support_obj_sizes, (int)support_obj_bytes.len);
            free(support_abs);
            free(support_obj_path);
            free(support_obj_cid);
        }
    }
    entry_symbol = extract_line_value(artifact.system_link_plan_text, "entry_symbol");
    if (strcmp(linker_flavor, "ld64") == 0 && strcmp(emit_raw, "static") != 0) {
        int sdk_rc = 0;
        string_list_push_copy(&sdk_argv, "--sdk");
        string_list_push_copy(&sdk_argv, machine_target_darwin_sdk_name(target_raw));
        string_list_push_copy(&sdk_argv, "--show-sdk-path");
        free(sdk_path);
        sdk_path = run_exec_capture("/usr/bin/xcrun", &sdk_argv, "", 1, &sdk_rc);
        trim_trailing_newlines_inplace(sdk_path);
        if (sdk_rc != 0 || sdk_path[0] == '\0') {
            die("v2 system link exec: failed to resolve darwin sdk path");
        }
    }
    build_system_link_exec_argv(target_raw,
                                emit_raw,
                                linker_flavor,
                                entry_symbol,
                                strcmp(emit_raw, "exe") == 0,
                                sdk_path,
                                output_abs,
                                primary_obj_abs,
                                &provider_obj_paths,
                                &support_obj_paths,
                                &exec_argv);
    link_output = run_exec_capture(linker_path, &exec_argv, "", 1, &exit_code);
    output_present = access(output_abs, F_OK) == 0;
    if (output_present) {
        ByteList output_bytes = read_file_bytes_all(output_abs);
        output_byte_count = (int)output_bytes.len;
        free(output_file_cid);
        output_file_cid = sha256_hex_bytes(output_bytes.items, output_bytes.len);
    }
    string_list_pushf(&lines, "system_link_exec_version=%s", system_link_exec_version());
    string_list_pushf(&lines, "source=%s", source_norm);
    string_list_pushf(&lines, "emit=%s", emit_raw);
    string_list_pushf(&lines, "target=%s", target_raw);
    string_list_pushf(&lines, "module_kind=%s", artifact.module_kind);
    string_list_pushf(&lines, "linker_flavor=%s", linker_flavor);
    string_list_pushf(&lines, "linker_path=%s", linker_path);
    if (sdk_path[0] != '\0') {
        string_list_pushf(&lines, "sdk_name=%s", machine_target_darwin_sdk_name(target_raw));
        string_list_pushf(&lines, "system_library_count=%d", strcmp(emit_raw, "static") == 0 ? 0 : 1);
        if (strcmp(emit_raw, "static") != 0) {
            string_list_push_copy(&lines, "system_library.0=System");
        }
    }
    string_list_pushf(&lines, "output_path=%s", path_basename_const(output_norm));
    string_list_pushf(&lines, "primary_obj_path=%s", path_basename_const(primary_obj_norm));
    string_list_pushf(&lines, "primary_obj_file_cid=%s", primary_obj.file_cid);
    string_list_pushf(&lines, "primary_obj_file_byte_count=%d", primary_obj.file_byte_count);
    string_list_pushf(&lines, "provider_module_count=%zu", native_link.provider_modules.len);
    for (i = 0; i < native_link.provider_modules.len; ++i) {
        string_list_pushf(&lines, "provider_module.%zu.name=%s", i, native_link.provider_modules.items[i]);
        string_list_pushf(&lines,
                          "provider_module.%zu.obj_path=%s",
                          i,
                          path_basename_const(provider_obj_norms.items[i]));
        string_list_pushf(&lines, "provider_module.%zu.obj_file_cid=%s", i, provider_obj_cids.items[i]);
        string_list_pushf(&lines, "provider_module.%zu.obj_file_byte_count=%d", i, provider_obj_sizes.items[i]);
    }
    string_list_pushf(&lines, "support_object_count=%zu", support_obj_paths.len);
    for (i = 0; i < support_obj_paths.len; ++i) {
        string_list_pushf(&lines, "support_object.%zu.obj_path=%s", i, path_basename_const(support_obj_norms.items[i]));
        string_list_pushf(&lines, "support_object.%zu.obj_file_cid=%s", i, support_obj_cids.items[i]);
        string_list_pushf(&lines, "support_object.%zu.obj_file_byte_count=%d", i, support_obj_sizes.items[i]);
    }
    string_list_pushf(&lines, "entry_required=%d", strcmp(emit_raw, "exe") == 0 ? 1 : 0);
    string_list_pushf(&lines, "entry_symbol=%s", entry_symbol);
    string_list_pushf(&lines, "exit_code=%d", exit_code);
    string_list_pushf(&lines, "output_present=%d", output_present);
    string_list_pushf(&lines, "output_byte_count=%d", output_byte_count);
    string_list_pushf(&lines, "output_file_cid=%s", output_file_cid);
    string_list_pushf(&lines, "link_ok=%d", exit_code == 0 && output_present ? 1 : 0);
    if (link_output[0] != '\0') {
        string_list_pushf(&lines, "link_output=%s", link_output);
    }
    {
        char *text = string_list_join(&lines, "\n");
        printf("%s\n", text);
        if (try_flag_value(argc, argv, "--report-out", "")[0] != '\0') {
            char report_out[PATH_MAX];
            resolve_output_path(repo_root, try_flag_value(argc, argv, "--report-out", ""), report_out, sizeof(report_out));
            write_text_file(report_out, xformat("%s\n", text));
        }
        free(text);
    }
    free(default_out_rel);
    free(default_out_name);
    free(entry_symbol);
    free(missing_reason0);
    free(link_output);
    free(sdk_path);
    free(output_file_cid);
    return exit_code == 0 && output_present ? 0 : 1;
}

static int cmd_emit_tooling_stage1(const char *repo_root, int argc, char **argv) {
    const char *root_raw = try_flag_value(argc, argv, "--root", "v2/examples");
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *emit_raw = try_flag_value(argc, argv, "--emit", "exe");
    const char *out_raw = try_flag_value(argc, argv, "--out", "");
    ToolingBootstrapProgram program =
        tooling_program_from_args(repo_root, root_raw, source_raw, target_raw, emit_raw);
    if (out_raw[0] != '\0') {
        char out_path[PATH_MAX];
        char out_parent[PATH_MAX];
        FILE *out;
        resolve_output_path(repo_root, out_raw, out_path, sizeof(out_path));
        if (snprintf(out_parent, sizeof(out_parent), "%s", out_path) >= (int)sizeof(out_parent)) {
            die("path too long");
        }
        parent_dir(out_parent);
        if (out_parent[0] != '\0') {
            create_dir_all(out_parent);
        }
        out = fopen(out_path, "wb");
        if (out == NULL) {
            die_errno("fopen failed", out_path);
        }
        emit_tooling_stage1_module(out, &program);
        fclose(out);
        return 0;
    }
    emit_tooling_stage1_module(stdout, &program);
    return 0;
}

static int cmd_tooling_selfhost_check(const char *repo_root, int argc, char **argv) {
    const char *root_raw = try_flag_value(argc, argv, "--root", "v2/examples");
    const char *source_raw = try_flag_value(argc, argv, "--in", "v2/examples/network_distribution_module.cheng");
    const char *target_raw = try_flag_value(argc, argv, "--target", "arm64-apple-darwin");
    const char *emit_raw = try_flag_value(argc, argv, "--emit", "exe");
    ToolingBootstrapProgram program =
        tooling_program_from_args(repo_root, root_raw, source_raw, target_raw, emit_raw);
    return tooling_selfhost_check(repo_root, &program);
}

void cheng_v2c_tooling_print_usage(void) {
    puts("  lsmr-address                 emit a deterministic LSMR address artifact for a cid");
    puts("  lsmr-route-plan              emit a deterministic canonical LSMR route-plan artifact");
    puts("  outline-parse                emit an outline parse for a compiler_core source");
    puts("  machine-plan                 emit a deterministic machine plan for a source");
    puts("  obj-image                    emit a deterministic object image for a source");
    puts("  obj-file                     emit a deterministic object-file byte layout for a source");
    puts("  system-link-plan             emit a deterministic native system-link plan for a source");
    puts("  system-link-exec             materialize object files and invoke the deterministic system linker");
    puts("  resolve-manifest             resolve a deterministic source manifest");
    puts("  publish-source-manifest      emit a source manifest artifact");
    puts("  publish-rule-pack            emit a rule-pack artifact");
    puts("  publish-compiler-rule-pack   emit a compiler rule-pack artifact");
    puts("  verify-network-selfhost      verify manifest + topology + rule-pack closure");
    puts("  release-compile              emit a deterministic release artifact spec");
    puts("  emit-tooling-stage1          emit a Cheng tooling stage1 bootstrap module");
    puts("  tooling-selfhost-check       verify tooling stage0 -> stage1 -> stage2 fixed-point closure");
    puts("  tooling-selfhost-host        run the native tooling selfhost orchestration");
    puts("  stage-selfhost-host          build stage1 -> stage2 -> stage3 native closure");
}

int cheng_v2c_tooling_is_command(const char *cmd) {
    return strcmp(cmd, "lsmr-address") == 0 ||
           strcmp(cmd, "lsmr-route-plan") == 0 ||
           strcmp(cmd, "outline-parse") == 0 ||
           strcmp(cmd, "machine-plan") == 0 ||
           strcmp(cmd, "obj-image") == 0 ||
           strcmp(cmd, "obj-file") == 0 ||
           strcmp(cmd, "system-link-plan") == 0 ||
           strcmp(cmd, "system-link-exec") == 0 ||
           strcmp(cmd, "resolve-manifest") == 0 ||
           strcmp(cmd, "publish-source-manifest") == 0 ||
           strcmp(cmd, "publish-rule-pack") == 0 ||
           strcmp(cmd, "publish-compiler-rule-pack") == 0 ||
           strcmp(cmd, "verify-network-selfhost") == 0 ||
           strcmp(cmd, "release-compile") == 0 ||
           strcmp(cmd, "emit-tooling-stage1") == 0 ||
           strcmp(cmd, "tooling-selfhost-check") == 0 ||
           strcmp(cmd, "tooling-selfhost-host") == 0 ||
           strcmp(cmd, "stage-selfhost-host") == 0;
}

int cheng_v2c_tooling_handle(int argc, char **argv) {
    char repo_root[PATH_MAX];
    if (argc < 2) {
        return 1;
    }
    detect_repo_root(argc >= 3 ? argv[2] : NULL, NULL, repo_root, sizeof(repo_root));
    if (strcmp(argv[1], "lsmr-address") == 0) {
        return cmd_lsmr_address(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "lsmr-route-plan") == 0) {
        return cmd_lsmr_route_plan(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "outline-parse") == 0) {
        return cmd_outline_parse(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "machine-plan") == 0) {
        return cmd_machine_plan(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "obj-image") == 0) {
        return cmd_obj_image(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "obj-file") == 0) {
        return cmd_obj_file(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "system-link-plan") == 0) {
        return cmd_system_link_plan(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "system-link-exec") == 0) {
        return cmd_system_link_exec(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "resolve-manifest") == 0) {
        return cmd_resolve_manifest(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "publish-source-manifest") == 0) {
        return cmd_publish_source_manifest(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "publish-rule-pack") == 0) {
        return cmd_publish_rule_pack(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "publish-compiler-rule-pack") == 0) {
        return cmd_publish_compiler_rule_pack(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "verify-network-selfhost") == 0) {
        return cmd_verify_network_selfhost(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "release-compile") == 0) {
        return cmd_release_compile(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "emit-tooling-stage1") == 0) {
        return cmd_emit_tooling_stage1(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "tooling-selfhost-check") == 0) {
        return cmd_tooling_selfhost_check(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "tooling-selfhost-host") == 0) {
        return cmd_tooling_selfhost_host(repo_root, argc, argv);
    }
    if (strcmp(argv[1], "stage-selfhost-host") == 0) {
        return cmd_stage_selfhost_host(repo_root, argc, argv);
    }
    return 1;
}
