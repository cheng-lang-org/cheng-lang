/* cheng_cold.c -- cold bootstrap compiler prototype
 *
 * Minimal Cheng subset:
 *   type Option[T] = Some(value: T) | None
 *   type Result[T, E] = Ok(value: T) | Err(error: E)
 *   fn main(): int32 =
 *       let result = Ok(7)
 *       match result {
 *           Ok(value) => return value
 *           Err(e) => return 0
 *       }
 *
 * Pipeline: mmap source -> span tokens -> arena SoA BodyIR -> ARM64 -> direct Mach-O.
 *
 * Build: cc -std=c11 -Wall -Wextra -pedantic -O2 -o /tmp/cheng_cold bootstrap/cheng_cold.c
 */

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "macho_direct.h"

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif

#define COLD_EMBEDDED_CONTRACT_CAP 8192
#define COLD_EMBEDDED_SOURCE_PATH_CAP 4096

__attribute__((used)) static char ColdEmbeddedContract[COLD_EMBEDDED_CONTRACT_CAP] =
    "CHENG_COLD_EMBEDDED_CONTRACT_V1\n";
__attribute__((used)) static char ColdEmbeddedSourcePath[COLD_EMBEDDED_SOURCE_PATH_CAP] =
    "CHENG_COLD_EMBEDDED_SOURCE_PATH_V1\n";

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

typedef struct {
    ArenaPage *head;
    ArenaPage *current;
    size_t used;
} Arena;

static void die(const char *msg) {
    fprintf(stderr, "cheng_cold: %s\n", msg);
    exit(2);
}

static int32_t align_i32(int32_t v, int32_t a) {
    return (v + a - 1) & ~(a - 1);
}

static void *arena_alloc(Arena *a, size_t size) {
    size = (size + 7u) & ~7u;
    if (!a->current || a->current->ptr + size > a->current->end) {
        size_t payload = size > ARENA_PAGE ? size + ARENA_PAGE : ARENA_PAGE;
        ArenaPage *page = mmap(0, sizeof(ArenaPage) + payload,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANON, -1, 0);
        if (page == MAP_FAILED) die("arena mmap failed");
        page->next = 0;
        page->base = (uint8_t *)(page + 1);
        page->ptr = page->base;
        page->end = page->base + payload;
        if (a->current) a->current->next = page;
        else a->head = page;
        a->current = page;
    }
    void *result = a->current->ptr;
    a->current->ptr += size;
    a->used += size;
    memset(result, 0, size);
    return result;
}

/* ================================================================
 * Source spans
 * ================================================================ */
typedef struct {
    const uint8_t *ptr;
    int32_t len;
} Span;

static Span source_open(const char *path) {
    Span source = {0};
    int fd = open(path, O_RDONLY);
    if (fd < 0) return source;
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return source;
    }
    if (st.st_size == 0) {
        close(fd);
        return source;
    }
    void *mem = mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mem == MAP_FAILED) return (Span){0};
    source.ptr = mem;
    source.len = (int32_t)st.st_size;
    return source;
}

static Span span_sub(Span s, int32_t start, int32_t end) {
    if (start < 0) start = 0;
    if (end > s.len) end = s.len;
    if (start >= end) return (Span){0};
    return (Span){s.ptr + start, end - start};
}

static bool span_eq(Span s, const char *text) {
    size_t n = strlen(text);
    return (size_t)s.len == n && memcmp(s.ptr, text, n) == 0;
}

static bool span_same(Span a, Span b) {
    return a.len == b.len && a.len >= 0 && memcmp(a.ptr, b.ptr, (size_t)a.len) == 0;
}

static bool span_is_i32(Span s) {
    if (s.len <= 0) return false;
    int32_t i = 0;
    if (s.ptr[0] == '-') i = 1;
    if (i >= s.len) return false;
    for (; i < s.len; i++) {
        if (s.ptr[i] < '0' || s.ptr[i] > '9') return false;
    }
    return true;
}

static int32_t span_i32(Span s) {
    int32_t i = 0;
    int32_t sign = 1;
    int32_t value = 0;
    if (s.len > 0 && s.ptr[0] == '-') {
        sign = -1;
        i = 1;
    }
    for (; i < s.len; i++) {
        uint8_t c = s.ptr[i];
        if (c < '0' || c > '9') break;
        value = value * 10 + (int32_t)(c - '0');
    }
    return sign * value;
}

static Span span_trim(Span s) {
    int32_t start = 0;
    int32_t end = s.len;
    while (start < end) {
        uint8_t c = s.ptr[start];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        start++;
    }
    while (end > start) {
        uint8_t c = s.ptr[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        end--;
    }
    return (Span){s.ptr + start, end - start};
}

static void span_write(FILE *file, Span s) {
    if (s.len > 0) fwrite(s.ptr, 1, (size_t)s.len, file);
}

/* ================================================================
 * Cold bootstrap contract parser
 * ================================================================ */
#define COLD_CONTRACT_MAX_FIELDS 128

typedef struct {
    Span key;
    Span value;
} ContractField;

typedef struct {
    Span source;
    const char *source_path;
    ContractField fields[COLD_CONTRACT_MAX_FIELDS];
    int32_t count;
} ColdContract;

static const char *ColdRequiredKeysV2[] = {
    "syntax",
    "bootstrap_name",
    "bootstrap_entry",
    "compiler_class",
    "target",
    "bootstrap_manifest",
    "supported_commands",
};

static const char *ColdSupportedCommandsV2[] = {
    "print-contract",
    "self-check",
    "compile-bootstrap",
    "bootstrap-bridge",
    "build-backend-driver",
};

static const char *ColdRequiredManifestKeys[] = {
    "compiler_entry_source",
    "compiler_runtime_source",
    "compiler_support_matrix_source",
    "compiler_request_source",
    "tooling_gate_source",
    "lang_parser_source",
    "lang_typed_expr_source",
    "core_ir_types_source",
    "core_ir_type_abi_source",
    "core_ir_low_uir_source",
    "core_ir_body_ir_loop_source",
    "core_ir_body_ir_noalias_source",
    "core_ir_body_ir_opt_source",
    "backend_system_link_plan_source",
    "backend_lowering_plan_source",
    "backend_primary_object_plan_source",
    "backend_primary_object_emit_source",
    "backend_direct_object_emit_source",
    "backend_object_buffer_source",
    "backend_object_symbols_source",
    "backend_object_relocs_source",
    "backend_macho_object_writer_source",
    "backend_object_plan_source",
    "backend_native_link_plan_source",
    "backend_native_link_exec_source",
    "backend_system_link_exec_source",
    "backend_system_link_exec_runtime_source",
    "backend_line_map_source",
    "runtime_core_runtime_source",
    "runtime_compiler_runtime_source",
    "runtime_debug_runtime_source",
    "runtime_program_support_get_env_provider_source",
    "tooling_bootstrap_contract_source",
    "tooling_hotpath_scan_source",
    "tooling_debug_tools_gate_source",
    "tooling_export_visibility_gate_source",
    "tooling_host_bridge_audit_gate_source",
    "tooling_host_smoke_gate_source",
    "tooling_object_debug_report_source",
    "backend_build_plan_source",
};

static const char *cold_flag_value(int argc, char **argv, const char *flag) {
    size_t flag_len = strlen(flag);
    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];
        if (strncmp(arg, flag, flag_len) != 0) continue;
        if (arg[flag_len] == ':' || arg[flag_len] == '=') return arg + flag_len + 1;
        if (arg[flag_len] == '\0' && i + 1 < argc) return argv[i + 1];
    }
    return 0;
}

static bool cold_span_eq_cstr(Span span, const char *text) {
    return span_eq(span, text);
}

static bool cold_contract_has_key(ColdContract *contract, const char *key) {
    for (int32_t i = 0; i < contract->count; i++) {
        if (cold_span_eq_cstr(contract->fields[i].key, key)) return true;
    }
    return false;
}

static Span cold_contract_get(ColdContract *contract, const char *key) {
    for (int32_t i = 0; i < contract->count; i++) {
        if (cold_span_eq_cstr(contract->fields[i].key, key)) return contract->fields[i].value;
    }
    return (Span){0};
}

static bool cold_contract_add(ColdContract *contract, Span key, Span value) {
    if (key.len <= 0 || value.len <= 0) {
        fprintf(stderr, "[cheng_cold] invalid bootstrap field\n");
        return false;
    }
    if (contract->count >= COLD_CONTRACT_MAX_FIELDS) {
        fprintf(stderr, "[cheng_cold] too many bootstrap fields\n");
        return false;
    }
    for (int32_t i = 0; i < contract->count; i++) {
        if (span_same(contract->fields[i].key, key)) {
            fprintf(stderr, "[cheng_cold] duplicate bootstrap field: ");
            span_write(stderr, key);
            fputc('\n', stderr);
            return false;
        }
    }
    contract->fields[contract->count++] = (ContractField){key, value};
    return true;
}

static bool cold_contract_parse(ColdContract *contract, const char *path) {
    memset(contract, 0, sizeof(*contract));
    contract->source_path = path;
    contract->source = source_open(path);
    if (contract->source.len <= 0) {
        fprintf(stderr, "[cheng_cold] cannot read bootstrap contract: %s\n", path);
        return false;
    }
    int32_t pos = 0;
    while (pos < contract->source.len) {
        int32_t start = pos;
        while (pos < contract->source.len && contract->source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < contract->source.len) pos++;
        Span line = span_sub(contract->source, start, end);
        for (int32_t i = 0; i < line.len; i++) {
            if (line.ptr[i] == '#') {
                line.len = i;
                break;
            }
        }
        line = span_trim(line);
        if (line.len == 0) continue;
        int32_t eq = -1;
        for (int32_t i = 0; i < line.len; i++) {
            if (line.ptr[i] == '=') {
                eq = i;
                break;
            }
        }
        if (eq < 0) {
            fprintf(stderr, "[cheng_cold] invalid bootstrap line: ");
            span_write(stderr, line);
            fputc('\n', stderr);
            return false;
        }
        Span key = span_trim((Span){line.ptr, eq});
        Span value = span_trim((Span){line.ptr + eq + 1, line.len - eq - 1});
        if (!cold_contract_add(contract, key, value)) return false;
    }
    return true;
}

static bool cold_contract_parse_span(ColdContract *contract, const char *source_path, Span text) {
    memset(contract, 0, sizeof(*contract));
    contract->source_path = source_path;
    contract->source = text;
    int32_t pos = 0;
    while (pos < text.len) {
        int32_t start = pos;
        while (pos < text.len && text.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < text.len) pos++;
        Span line = span_sub(text, start, end);
        for (int32_t i = 0; i < line.len; i++) {
            if (line.ptr[i] == '#') {
                line.len = i;
                break;
            }
        }
        line = span_trim(line);
        if (line.len == 0) continue;
        int32_t eq = -1;
        for (int32_t i = 0; i < line.len; i++) {
            if (line.ptr[i] == '=') {
                eq = i;
                break;
            }
        }
        if (eq < 0) {
            fprintf(stderr, "[cheng_cold] invalid bootstrap line: ");
            span_write(stderr, line);
            fputc('\n', stderr);
            return false;
        }
        Span key = span_trim((Span){line.ptr, eq});
        Span value = span_trim((Span){line.ptr + eq + 1, line.len - eq - 1});
        if (!cold_contract_add(contract, key, value)) return false;
    }
    return true;
}

static bool cold_csv_has_token(Span csv, const char *token) {
    int32_t pos = 0;
    while (pos <= csv.len) {
        int32_t start = pos;
        while (pos < csv.len && csv.ptr[pos] != ',') pos++;
        Span part = span_trim((Span){csv.ptr + start, pos - start});
        if (span_eq(part, token)) return true;
        pos++;
    }
    return false;
}

static int32_t cold_csv_token_count(Span csv) {
    int32_t pos = 0;
    int32_t count = 0;
    while (pos <= csv.len) {
        int32_t start = pos;
        while (pos < csv.len && csv.ptr[pos] != ',') pos++;
        Span part = span_trim((Span){csv.ptr + start, pos - start});
        if (part.len > 0) count++;
        pos++;
    }
    return count;
}

static bool cold_csv_exact_supported(Span csv) {
    int32_t expected_count = (int32_t)(sizeof(ColdSupportedCommandsV2) / sizeof(ColdSupportedCommandsV2[0]));
    if (cold_csv_token_count(csv) != expected_count) {
        fprintf(stderr, "[cheng_cold] supported_commands token count drift\n");
        return false;
    }
    for (int32_t i = 0; i < expected_count; i++) {
        if (!cold_csv_has_token(csv, ColdSupportedCommandsV2[i])) {
            fprintf(stderr, "[cheng_cold] supported_commands missing token: %s\n", ColdSupportedCommandsV2[i]);
            return false;
        }
    }
    return true;
}

static void cold_absolute_path(const char *path, char *out, size_t cap) {
    if (path[0] == '/') {
        snprintf(out, cap, "%s", path);
        return;
    }
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(out, cap, "%s", path);
        return;
    }
    snprintf(out, cap, "%s/%s", cwd, path);
}

static void cold_join_path(char *out, size_t cap, const char *a, const char *b) {
    size_t alen = strlen(a);
    snprintf(out, cap, "%s%s%s", a, (alen > 0 && a[alen - 1] == '/') ? "" : "/", b);
}

static bool cold_parent_dir(const char *path, char *out, size_t cap) {
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/') len--;
    while (len > 0 && path[len - 1] != '/') len--;
    if (len == 0) return false;
    if (len >= cap) len = cap - 1;
    memcpy(out, path, len);
    out[len] = '\0';
    return true;
}

static bool cold_mkdir_p(const char *path) {
    char buf[PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) return false;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i < len; i++) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        if (buf[0] != '\0' && mkdir(buf, 0755) != 0) {
            struct stat st;
            if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
        }
        buf[i] = '/';
    }
    if (mkdir(buf, 0755) != 0) {
        struct stat st;
        if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    }
    return true;
}

static void cold_contract_workspace_root(ColdContract *contract, char *out, size_t cap) {
    char abs_path[PATH_MAX];
    char bootstrap_dir[PATH_MAX];
    cold_absolute_path(contract->source_path, abs_path, sizeof(abs_path));
    if (!cold_parent_dir(abs_path, bootstrap_dir, sizeof(bootstrap_dir)) ||
        !cold_parent_dir(bootstrap_dir, out, cap)) {
        out[0] = '\0';
    }
}

static bool cold_contract_manifest_path(ColdContract *contract, char *out, size_t cap) {
    Span manifest = cold_contract_get(contract, "bootstrap_manifest");
    if (manifest.len <= 0) return false;
    char root[PATH_MAX];
    cold_contract_workspace_root(contract, root, sizeof(root));
    if (root[0] == '\0') return false;
    snprintf(out, cap, "%s%s", root, root[strlen(root) - 1] == '/' ? "" : "/");
    size_t used = strlen(out);
    if (used + (size_t)manifest.len >= cap) return false;
    memcpy(out + used, manifest.ptr, (size_t)manifest.len);
    out[used + (size_t)manifest.len] = '\0';
    return true;
}

static bool cold_manifest_validate(ColdContract *contract) {
    char manifest_path[PATH_MAX];
    if (!cold_contract_manifest_path(contract, manifest_path, sizeof(manifest_path))) {
        fprintf(stderr, "[cheng_cold] missing bootstrap_manifest\n");
        return false;
    }
    ColdContract manifest;
    if (!cold_contract_parse(&manifest, manifest_path)) return false;
    int32_t required_count = (int32_t)(sizeof(ColdRequiredManifestKeys) / sizeof(ColdRequiredManifestKeys[0]));
    for (int32_t i = 0; i < required_count; i++) {
        if (!cold_contract_has_key(&manifest, ColdRequiredManifestKeys[i])) {
            fprintf(stderr, "[cheng_cold] bootstrap manifest missing field: %s\n", ColdRequiredManifestKeys[i]);
            return false;
        }
    }
    return true;
}

static bool cold_contract_validate(ColdContract *contract) {
    int32_t required_count = (int32_t)(sizeof(ColdRequiredKeysV2) / sizeof(ColdRequiredKeysV2[0]));
    for (int32_t i = 0; i < required_count; i++) {
        if (!cold_contract_has_key(contract, ColdRequiredKeysV2[i])) {
            fprintf(stderr, "[cheng_cold] missing bootstrap field: %s\n", ColdRequiredKeysV2[i]);
            return false;
        }
    }
    if (!span_eq(cold_contract_get(contract, "syntax"), "bootstrap-v2")) {
        fprintf(stderr, "[cheng_cold] unsupported bootstrap syntax\n");
        return false;
    }
    if (!span_eq(cold_contract_get(contract, "compiler_class"), "cold_bootstrap")) {
        fprintf(stderr, "[cheng_cold] bootstrap compiler_class must be cold_bootstrap\n");
        return false;
    }
    Span target = cold_contract_get(contract, "target");
    if (!span_eq(target, "arm64-apple-darwin") && !span_eq(target, "x86_64-unknown-linux-gnu")) {
        fprintf(stderr, "[cheng_cold] bootstrap target must be arm64-apple-darwin\n");
        return false;
    }
    if (!cold_csv_exact_supported(cold_contract_get(contract, "supported_commands"))) return false;
    return cold_manifest_validate(contract);
}

static uint64_t cold_fnv1a64_update(uint64_t hash, Span span) {
    for (int32_t i = 0; i < span.len; i++) {
        hash ^= (uint64_t)span.ptr[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void cold_print_normalized(ColdContract *contract, FILE *file, uint64_t *hash_out) {
    uint64_t hash = 1469598103934665603ULL;
    int32_t required_count = (int32_t)(sizeof(ColdRequiredKeysV2) / sizeof(ColdRequiredKeysV2[0]));
    for (int32_t i = 0; i < required_count; i++) {
        Span value = cold_contract_get(contract, ColdRequiredKeysV2[i]);
        if (file) {
            fputs(ColdRequiredKeysV2[i], file);
            fputc('=', file);
            span_write(file, value);
            fputc('\n', file);
        }
        hash = cold_fnv1a64_update(hash, (Span){(const uint8_t *)ColdRequiredKeysV2[i], (int32_t)strlen(ColdRequiredKeysV2[i])});
        hash = cold_fnv1a64_update(hash, (Span){(const uint8_t *)"=", 1});
        hash = cold_fnv1a64_update(hash, value);
        hash = cold_fnv1a64_update(hash, (Span){(const uint8_t *)"\n", 1});
    }
    if (hash_out) *hash_out = hash;
}

static size_t cold_normalized_size(ColdContract *contract) {
    size_t total = 0;
    int32_t required_count = (int32_t)(sizeof(ColdRequiredKeysV2) / sizeof(ColdRequiredKeysV2[0]));
    for (int32_t i = 0; i < required_count; i++) {
        Span value = cold_contract_get(contract, ColdRequiredKeysV2[i]);
        total += strlen(ColdRequiredKeysV2[i]) + 1u + (size_t)value.len + 1u;
    }
    return total;
}

static char *cold_normalized_alloc(ColdContract *contract, size_t *len_out, uint64_t *hash_out) {
    size_t cap = cold_normalized_size(contract) + 1u;
    char *text = malloc(cap);
    if (!text) die("out of memory normalizing contract");
    size_t used = 0;
    uint64_t hash = 1469598103934665603ULL;
    int32_t required_count = (int32_t)(sizeof(ColdRequiredKeysV2) / sizeof(ColdRequiredKeysV2[0]));
    for (int32_t i = 0; i < required_count; i++) {
        const char *key = ColdRequiredKeysV2[i];
        Span value = cold_contract_get(contract, key);
        size_t key_len = strlen(key);
        memcpy(text + used, key, key_len);
        used += key_len;
        text[used++] = '=';
        if (value.len > 0) {
            memcpy(text + used, value.ptr, (size_t)value.len);
            used += (size_t)value.len;
        }
        text[used++] = '\n';
        hash = cold_fnv1a64_update(hash, (Span){(const uint8_t *)key, (int32_t)key_len});
        hash = cold_fnv1a64_update(hash, (Span){(const uint8_t *)"=", 1});
        hash = cold_fnv1a64_update(hash, value);
        hash = cold_fnv1a64_update(hash, (Span){(const uint8_t *)"\n", 1});
    }
    text[used] = '\0';
    if (len_out) *len_out = used;
    if (hash_out) *hash_out = hash;
    return text;
}

#define COLD_ENTRY_CHAIN_CAP 8
#define COLD_COMMAND_CASE_CAP 16
#define COLD_NAME_CAP 128
#define COLD_ENTRY_ERROR_CAP 256

typedef struct {
    char function[COLD_NAME_CAP];
    char kind[64];
    char target[COLD_NAME_CAP];
    char aux[192];
} ColdEntryChainStep;

typedef struct {
    int32_t code;
    char target[COLD_NAME_CAP];
} ColdDispatchCase;

typedef struct {
    char text[64];
    int32_t code;
} ColdCommandCase;

typedef struct {
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

typedef struct {
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

typedef struct {
    Span name;
    Span params;
    Span return_type;
    Span body;
    Span source_span;
    int32_t line;
    int32_t has_body;
} ColdFunctionSymbol;

static uint64_t cold_fnv1a64_update_cstr(uint64_t hash, const char *text);

static bool cold_span_starts_with(Span span, const char *prefix) {
    size_t len = strlen(prefix);
    return span.len >= (int32_t)len && memcmp(span.ptr, prefix, len) == 0;
}

static bool cold_span_is_exact_or_prefix_space(Span span, const char *word) {
    size_t len = strlen(word);
    if (span.len < (int32_t)len || memcmp(span.ptr, word, len) != 0) return false;
    return span.len == (int32_t)len || span.ptr[len] == ' ' || span.ptr[len] == '\t';
}

static bool cold_ident_char(uint8_t c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

static bool cold_parse_fn_name(Span line, Span *name_out) {
    int32_t pos = 0;
    if (cold_span_starts_with(line, "fn ")) {
        pos = 3;
    } else if (cold_span_starts_with(line, "async fn ")) {
        pos = 9;
    } else {
        return false;
    }
    while (pos < line.len && (line.ptr[pos] == ' ' || line.ptr[pos] == '\t')) pos++;
    int32_t start = pos;
    while (pos < line.len && cold_ident_char(line.ptr[pos])) pos++;
    if (pos <= start) return false;
    *name_out = (Span){line.ptr + start, pos - start};
    return true;
}

static void cold_span_copy(char *out, size_t cap, Span span) {
    if (cap == 0) return;
    size_t len = span.len > 0 ? (size_t)span.len : 0u;
    if (len >= cap) len = cap - 1;
    if (len > 0) memcpy(out, span.ptr, len);
    out[len] = '\0';
}

static bool cold_line_top_level(Span line) {
    return line.len > 0 && line.ptr[0] != ' ' && line.ptr[0] != '\t';
}

static bool cold_line_has_triple_quote(Span line) {
    for (int32_t i = 0; i + 2 < line.len; i++) {
        if (line.ptr[i] == '"' && line.ptr[i + 1] == '"' && line.ptr[i + 2] == '"') return true;
    }
    return false;
}

static bool cold_top_level_decl_like(Span line) {
    Span trimmed = span_trim(line);
    if (trimmed.len == 0) return false;
    if (trimmed.ptr[0] == '@') return true;
    return cold_span_starts_with(trimmed, "import ") ||
           cold_span_starts_with(trimmed, "fn ") ||
           cold_span_starts_with(trimmed, "async fn ") ||
           cold_span_is_exact_or_prefix_space(trimmed, "type") ||
           cold_span_is_exact_or_prefix_space(trimmed, "const") ||
           cold_span_is_exact_or_prefix_space(trimmed, "let") ||
           cold_span_is_exact_or_prefix_space(trimmed, "var") ||
           cold_span_is_exact_or_prefix_space(trimmed, "module");
}

static int32_t cold_span_offset(Span source, Span part) {
    if (!part.ptr) return -1;
    if (part.ptr < source.ptr || part.ptr > source.ptr + source.len) return -1;
    return (int32_t)(part.ptr - source.ptr);
}

static int32_t cold_line_after_pos(Span source, int32_t pos) {
    if (pos < 0) pos = 0;
    while (pos < source.len && source.ptr[pos] != '\n') pos++;
    if (pos < source.len) pos++;
    return pos;
}

static int32_t cold_line_end_from(Span source, int32_t pos) {
    while (pos < source.len && source.ptr[pos] != '\n') pos++;
    return pos;
}

static int32_t cold_find_matching_paren(Span source, int32_t open_pos) {
    if (open_pos < 0 || open_pos >= source.len || source.ptr[open_pos] != '(') return -1;
    int32_t depth = 0;
    bool in_string = false;
    uint8_t string_quote = 0;
    for (int32_t i = open_pos; i < source.len; i++) {
        uint8_t c = source.ptr[i];
        if (in_string) {
            if (c == '\\' && i + 1 < source.len) {
                i++;
                continue;
            }
            if (c == string_quote) in_string = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = true;
            string_quote = c;
            continue;
        }
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0) return i;
            if (depth < 0) return -1;
        }
    }
    return -1;
}

static int32_t cold_next_top_level_decl(Span source, int32_t pos) {
    bool in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        Span line = span_sub(source, start, end);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        if (cold_top_level_decl_like(line)) return start;
    }
    return source.len;
}

static bool cold_parse_function_symbol_at(Span source, int32_t fn_start, int32_t line_no,
                                          ColdFunctionSymbol *symbol) {
    memset(symbol, 0, sizeof(*symbol));
    int32_t line_end = cold_line_end_from(source, fn_start);
    Span line = span_sub(source, fn_start, line_end);
    Span trimmed = span_trim(line);
    Span name = {0};
    if (!cold_parse_fn_name(trimmed, &name)) return false;
    int32_t name_end = cold_span_offset(source, name) + name.len;
    int32_t open = name_end;
    while (open < source.len && (source.ptr[open] == ' ' || source.ptr[open] == '\t')) open++;
    if (open >= source.len || source.ptr[open] != '(') return false;
    int32_t close = cold_find_matching_paren(source, open);
    if (close < 0) return false;
    int32_t next_decl = cold_next_top_level_decl(source, cold_line_after_pos(source, close));

    int32_t eq = -1;
    int32_t colon = -1;
    for (int32_t i = close + 1; i < next_decl; i++) {
        if (source.ptr[i] == ':' && colon < 0) colon = i;
        if (source.ptr[i] == '=') {
            eq = i;
            break;
        }
        if (source.ptr[i] == '\n' && eq < 0 && colon < 0) {
            continue;
        }
    }

    Span params = span_trim((Span){source.ptr + open + 1, close - open - 1});
    Span ret = {0};
    if (colon >= 0) {
        int32_t ret_end = eq >= 0 ? eq : cold_line_end_from(source, colon);
        ret = span_trim(span_sub(source, colon + 1, ret_end));
    }

    int32_t source_end = next_decl;
    while (source_end > fn_start) {
        uint8_t c = source.ptr[source_end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        source_end--;
    }
    Span source_span = span_sub(source, fn_start, source_end);

    Span body = {0};
    int32_t has_body = 0;
    if (eq >= 0) {
        int32_t body_start = eq + 1;
        while (body_start < source_end) {
            uint8_t c = source.ptr[body_start];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            body_start++;
        }
        int32_t body_end = source_end;
        body = span_sub(source, body_start, body_end);
        has_body = body.len > 0 ? 1 : 0;
    }

    symbol->name = name;
    symbol->params = params;
    symbol->return_type = ret;
    symbol->body = body;
    symbol->source_span = source_span;
    symbol->line = line_no;
    symbol->has_body = has_body;
    return true;
}

static void cold_scan_cheng_source(Span source, Span rel_path,
                                   Span entry_rel, Span entry_name,
                                   ColdSourceScanStats *scan) {
    memset(scan, 0, sizeof(*scan));
    scan->byte_count = (uint64_t)source.len;
    scan->declaration_hash = 1469598103934665603ULL;
    bool is_entry_source = span_same(rel_path, entry_rel);
    int32_t pos = 0;
    int32_t line_no = 0;
    bool in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        line_no++;
        scan->line_count++;
        Span line = span_sub(source, start, end);
        Span trimmed = span_trim(line);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line) && cold_line_has_triple_quote(line)) {
            in_triple = true;
            continue;
        }
        if (trimmed.len == 0) continue;
        if (cold_span_starts_with(trimmed, "//") || cold_span_starts_with(trimmed, "#")) continue;
        if (cold_line_top_level(line) && cold_span_starts_with(trimmed, "@importc")) {
            scan->importc_count++;
            scan->declaration_count++;
            scan->declaration_hash = cold_fnv1a64_update(scan->declaration_hash, rel_path);
            scan->declaration_hash = cold_fnv1a64_update_cstr(scan->declaration_hash, ":importc\n");
            continue;
        }
        if (!cold_line_top_level(line)) continue;
        if (cold_span_starts_with(trimmed, "import ")) {
            scan->import_count++;
            scan->declaration_count++;
            scan->declaration_hash = cold_fnv1a64_update(scan->declaration_hash, rel_path);
            scan->declaration_hash = cold_fnv1a64_update_cstr(scan->declaration_hash, ":import:");
            scan->declaration_hash = cold_fnv1a64_update(scan->declaration_hash, trimmed);
            scan->declaration_hash = cold_fnv1a64_update_cstr(scan->declaration_hash, "\n");
            continue;
        }
        if (cold_span_is_exact_or_prefix_space(trimmed, "type")) {
            scan->type_block_count++;
            scan->declaration_count++;
            scan->declaration_hash = cold_fnv1a64_update(scan->declaration_hash, rel_path);
            scan->declaration_hash = cold_fnv1a64_update_cstr(scan->declaration_hash, ":type\n");
            continue;
        }
        if (cold_span_is_exact_or_prefix_space(trimmed, "const")) {
            scan->const_block_count++;
            scan->declaration_count++;
            scan->declaration_hash = cold_fnv1a64_update(scan->declaration_hash, rel_path);
            scan->declaration_hash = cold_fnv1a64_update_cstr(scan->declaration_hash, ":const\n");
            continue;
        }
        Span fn_name = {0};
        if (cold_parse_fn_name(trimmed, &fn_name)) {
            scan->function_count++;
            scan->declaration_count++;
            ColdFunctionSymbol symbol;
            bool symbol_ok = cold_parse_function_symbol_at(source, start, line_no, &symbol);
            if (symbol_ok) scan->function_symbol_count++;
            else scan->function_symbol_error_count++;
            scan->declaration_hash = cold_fnv1a64_update(scan->declaration_hash, rel_path);
            scan->declaration_hash = cold_fnv1a64_update_cstr(scan->declaration_hash, ":fn:");
            scan->declaration_hash = cold_fnv1a64_update(scan->declaration_hash, fn_name);
            scan->declaration_hash = cold_fnv1a64_update_cstr(scan->declaration_hash, "\n");
            if (is_entry_source && span_same(fn_name, entry_name)) {
                scan->entry_function_found = 1;
                scan->entry_function_line = line_no;
                if (symbol_ok) {
                    scan->entry_function_source_start = cold_span_offset(source, symbol.source_span);
                    scan->entry_function_source_len = symbol.source_span.len;
                    scan->entry_function_params_start = cold_span_offset(source, symbol.params);
                    scan->entry_function_params_len = symbol.params.len;
                    scan->entry_function_return_start = cold_span_offset(source, symbol.return_type);
                    scan->entry_function_return_len = symbol.return_type.len;
                    scan->entry_function_body_start = cold_span_offset(source, symbol.body);
                    scan->entry_function_body_len = symbol.body.len;
                    scan->entry_function_has_body = symbol.has_body;
                }
            }
        }
    }
}

static bool cold_span_is_safe_relative_path(Span path) {
    if (path.len <= 0 || path.ptr[0] == '/') return false;
    int32_t seg_start = 0;
    for (int32_t i = 0; i <= path.len; i++) {
        if (i < path.len && path.ptr[i] != '/') continue;
        int32_t seg_len = i - seg_start;
        if (seg_len <= 0) return false;
        if (seg_len == 2 &&
            path.ptr[seg_start] == '.' &&
            path.ptr[seg_start + 1] == '.') return false;
        seg_start = i + 1;
    }
    return true;
}

static bool cold_join_root_span(char *out, size_t cap, const char *root, Span rel) {
    if (!cold_span_is_safe_relative_path(rel)) return false;
    size_t root_len = strlen(root);
    size_t need_sep = (root_len > 0 && root[root_len - 1] == '/') ? 0u : 1u;
    if (root_len + need_sep + (size_t)rel.len + 1u > cap) return false;
    memcpy(out, root, root_len);
    size_t used = root_len;
    if (need_sep) out[used++] = '/';
    memcpy(out + used, rel.ptr, (size_t)rel.len);
    used += (size_t)rel.len;
    out[used] = '\0';
    return true;
}

static uint64_t cold_fnv1a64_update_cstr(uint64_t hash, const char *text) {
    return cold_fnv1a64_update(hash, (Span){(const uint8_t *)text, (int32_t)strlen(text)});
}

static Span cold_cstr_span(const char *text) {
    return (Span){(const uint8_t *)text, (int32_t)strlen(text)};
}

static void cold_span_copy_buf(char *out, size_t cap, Span span) {
    cold_span_copy(out, cap, span);
}

static void cold_skip_inline_ws(Span span, int32_t *pos) {
    while (*pos < span.len && (span.ptr[*pos] == ' ' || span.ptr[*pos] == '\t')) (*pos)++;
}

static bool cold_parse_text_at(Span span, int32_t *pos, const char *text) {
    int32_t len = (int32_t)strlen(text);
    if (*pos < 0 || *pos + len > span.len) return false;
    if (memcmp(span.ptr + *pos, text, (size_t)len) != 0) return false;
    *pos += len;
    return true;
}

static bool cold_parse_ident_at(Span span, int32_t *pos, Span *ident) {
    if (*pos >= span.len) return false;
    uint8_t c = span.ptr[*pos];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) return false;
    int32_t start = *pos;
    (*pos)++;
    while (*pos < span.len && cold_ident_char(span.ptr[*pos])) (*pos)++;
    *ident = (Span){span.ptr + start, *pos - start};
    return true;
}

static bool cold_parse_i32_at(Span span, int32_t *pos, int32_t *value) {
    int32_t sign = 1;
    int32_t v = 0;
    if (*pos < span.len && span.ptr[*pos] == '-') {
        sign = -1;
        (*pos)++;
    }
    int32_t start = *pos;
    while (*pos < span.len && span.ptr[*pos] >= '0' && span.ptr[*pos] <= '9') {
        v = v * 10 + (int32_t)(span.ptr[*pos] - '0');
        (*pos)++;
    }
    if (*pos == start) return false;
    *value = sign * v;
    return true;
}

static bool cold_trailing_ws_only(Span span, int32_t pos) {
    while (pos < span.len) {
        uint8_t c = span.ptr[pos++];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return true;
}

static int32_t cold_collect_body_lines(Span body, Span *lines, int32_t cap) {
    int32_t pos = 0;
    int32_t count = 0;
    while (pos < body.len) {
        int32_t start = pos;
        while (pos < body.len && body.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < body.len) pos++;
        Span line = span_trim((Span){body.ptr + start, end - start});
        if (line.len == 0) continue;
        if (cold_span_starts_with(line, "//") || cold_span_starts_with(line, "#")) continue;
        if (count >= cap) return -1;
        lines[count++] = line;
    }
    return count;
}

static bool cold_parse_call_expr(Span expr, Span *callee, Span *arg) {
    int32_t pos = 0;
    cold_skip_inline_ws(expr, &pos);
    if (!cold_parse_ident_at(expr, &pos, callee)) return false;
    cold_skip_inline_ws(expr, &pos);
    if (pos >= expr.len || expr.ptr[pos] != '(') return false;
    int32_t open = pos;
    int32_t close = cold_find_matching_paren(expr, open);
    if (close < 0) return false;
    if (arg) *arg = span_trim((Span){expr.ptr + open + 1, close - open - 1});
    pos = close + 1;
    cold_skip_inline_ws(expr, &pos);
    return pos == expr.len;
}

static bool cold_parse_return_call_line(Span line, Span *callee, Span *arg) {
    int32_t pos = 0;
    Span keyword = {0};
    if (!cold_parse_ident_at(line, &pos, &keyword) || !span_eq(keyword, "return")) return false;
    if (pos >= line.len || (line.ptr[pos] != ' ' && line.ptr[pos] != '\t')) return false;
    Span expr = span_trim((Span){line.ptr + pos, line.len - pos});
    return cold_parse_call_expr(expr, callee, arg);
}

static bool cold_parse_return_i32_line(Span line, int32_t *value) {
    int32_t pos = 0;
    Span keyword = {0};
    if (!cold_parse_ident_at(line, &pos, &keyword) || !span_eq(keyword, "return")) return false;
    if (pos >= line.len || (line.ptr[pos] != ' ' && line.ptr[pos] != '\t')) return false;
    cold_skip_inline_ws(line, &pos);
    if (cold_span_starts_with((Span){line.ptr + pos, line.len - pos}, "int32")) {
        Span callee = {0};
        Span arg = {0};
        if (!cold_parse_call_expr((Span){line.ptr + pos, line.len - pos}, &callee, &arg)) return false;
        if (!span_eq(callee, "int32")) return false;
        int32_t arg_pos = 0;
        if (!cold_parse_i32_at(arg, &arg_pos, value)) return false;
        return cold_trailing_ws_only(arg, arg_pos);
    }
    if (!cold_parse_i32_at(line, &pos, value)) return false;
    return cold_trailing_ws_only(line, pos);
}

static bool cold_parse_let_call_line(Span line, Span *local, Span *callee, Span *arg) {
    int32_t pos = 0;
    Span keyword = {0};
    if (!cold_parse_ident_at(line, &pos, &keyword) || !span_eq(keyword, "let")) return false;
    if (pos >= line.len || (line.ptr[pos] != ' ' && line.ptr[pos] != '\t')) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_ident_at(line, &pos, local)) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_text_at(line, &pos, "=")) return false;
    Span expr = span_trim((Span){line.ptr + pos, line.len - pos});
    return cold_parse_call_expr(expr, callee, arg);
}

static bool cold_parse_if_i32_line(Span line, const char *expected_local,
                                   const char *expected_op, int32_t *value) {
    int32_t pos = 0;
    Span keyword = {0};
    Span local = {0};
    if (!cold_parse_ident_at(line, &pos, &keyword) || !span_eq(keyword, "if")) return false;
    if (pos >= line.len || (line.ptr[pos] != ' ' && line.ptr[pos] != '\t')) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_ident_at(line, &pos, &local) || !span_eq(local, expected_local)) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_text_at(line, &pos, expected_op)) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_i32_at(line, &pos, value)) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_text_at(line, &pos, ":")) return false;
    return cold_trailing_ws_only(line, pos);
}

static bool cold_parse_command_header_line(Span line, int32_t *length) {
    int32_t pos = 0;
    Span keyword = {0};
    Span local = {0};
    if (!cold_parse_ident_at(line, &pos, &keyword) || !span_eq(keyword, "if")) return false;
    if (pos >= line.len || (line.ptr[pos] != ' ' && line.ptr[pos] != '\t')) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_ident_at(line, &pos, &local) || !span_eq(local, "n")) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_text_at(line, &pos, "==")) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_i32_at(line, &pos, length)) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_text_at(line, &pos, "&&")) return false;
    return cold_trailing_ws_only(line, pos);
}

static bool cold_parse_command_char_line(Span line, int32_t *index, char *ch, bool *last) {
    int32_t pos = 0;
    Span target = {0};
    if (!cold_parse_ident_at(line, &pos, &target) || !span_eq(target, "cmd")) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_text_at(line, &pos, "[")) return false;
    if (!cold_parse_i32_at(line, &pos, index)) return false;
    if (!cold_parse_text_at(line, &pos, "]")) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_text_at(line, &pos, "==")) return false;
    cold_skip_inline_ws(line, &pos);
    if (!cold_parse_text_at(line, &pos, "'")) return false;
    if (pos >= line.len) return false;
    *ch = (char)line.ptr[pos++];
    if (!cold_parse_text_at(line, &pos, "'")) return false;
    cold_skip_inline_ws(line, &pos);
    if (cold_parse_text_at(line, &pos, "&&")) {
        *last = false;
    } else if (cold_parse_text_at(line, &pos, ":")) {
        *last = true;
    } else {
        return false;
    }
    return cold_trailing_ws_only(line, pos);
}

static bool cold_find_function_symbol_by_name(Span source, Span name,
                                              ColdFunctionSymbol *symbol) {
    int32_t pos = 0;
    int32_t line_no = 0;
    bool in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        line_no++;
        Span line = span_sub(source, start, end);
        Span trimmed = span_trim(line);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line) && cold_line_has_triple_quote(line)) {
            in_triple = true;
            continue;
        }
        if (!cold_line_top_level(line)) continue;
        Span fn_name = {0};
        if (!cold_parse_fn_name(trimmed, &fn_name)) continue;
        if (!span_same(fn_name, name)) continue;
        return cold_parse_function_symbol_at(source, start, line_no, symbol);
    }
    return false;
}

static void cold_entry_step_set(ColdManifestStats *stats, int32_t index,
                                const char *function, const char *kind,
                                const char *target, const char *aux) {
    if (index < 0 || index >= COLD_ENTRY_CHAIN_CAP) return;
    snprintf(stats->entry_chain[index].function, sizeof(stats->entry_chain[index].function), "%s", function);
    snprintf(stats->entry_chain[index].kind, sizeof(stats->entry_chain[index].kind), "%s", kind);
    snprintf(stats->entry_chain[index].target, sizeof(stats->entry_chain[index].target), "%s", target ? target : "");
    snprintf(stats->entry_chain[index].aux, sizeof(stats->entry_chain[index].aux), "%s", aux ? aux : "");
    if (stats->entry_chain_step_count <= index) stats->entry_chain_step_count = index + 1;
}

static bool cold_entry_semantics_error(ColdManifestStats *stats, const char *message) {
    snprintf(stats->entry_semantics_error, sizeof(stats->entry_semantics_error), "%s", message);
    return false;
}

static bool cold_recognize_argc_guard_dispatch(Span body,
                                               char *dispatch_target, size_t dispatch_cap,
                                               char *aux, size_t aux_cap) {
    Span lines[8];
    int32_t count = cold_collect_body_lines(body, lines, 8);
    if (count != 4) return false;

    Span local = {0};
    Span callee = {0};
    Span arg = {0};
    if (!cold_parse_let_call_line(lines[0], &local, &callee, &arg)) return false;
    if (!span_eq(local, "count") || !span_eq(callee, "BackendDriverDispatchMinParamCount") || arg.len != 0) return false;

    int32_t guard_value = 0;
    if (!cold_parse_if_i32_line(lines[1], "count", "<=", &guard_value) || guard_value != 0) return false;

    Span help_target = {0};
    Span help_arg = {0};
    if (!cold_parse_return_call_line(lines[2], &help_target, &help_arg)) return false;
    if (!span_eq(help_target, "BackendDriverDispatchMinHelp") || help_arg.len != 0) return false;

    Span final_target = {0};
    Span final_arg = {0};
    if (!cold_parse_return_call_line(lines[3], &final_target, &final_arg)) return false;
    if (!span_eq(final_target, "BackendDriverDispatchMinRunCommandWithCmd")) return false;

    Span param_target = {0};
    Span param_arg = {0};
    if (!cold_parse_call_expr(final_arg, &param_target, &param_arg)) return false;
    if (!span_eq(param_target, "BackendDriverDispatchMinParamStr")) return false;
    int32_t param_pos = 0;
    int32_t param_index = 0;
    if (!cold_parse_i32_at(param_arg, &param_pos, &param_index) ||
        !cold_trailing_ws_only(param_arg, param_pos) ||
        param_index != 1) return false;

    cold_span_copy_buf(dispatch_target, dispatch_cap, final_target);
    snprintf(aux, aux_cap, "argc_call:BackendDriverDispatchMinParamCount,empty_arg:BackendDriverDispatchMinHelp,arg_call:BackendDriverDispatchMinParamStr,index:1");
    return true;
}

static bool cold_recognize_dispatch_chain(Span body, ColdManifestStats *stats,
                                          char *code_target, size_t code_cap) {
    Span lines[32];
    int32_t count = cold_collect_body_lines(body, lines, 32);
    if (count < 4) return false;

    Span local = {0};
    Span callee = {0};
    Span arg = {0};
    if (!cold_parse_let_call_line(lines[0], &local, &callee, &arg)) return false;
    if (!span_eq(local, "code") || !span_eq(callee, "BackendDriverDispatchMinCommandCode") ||
        !span_eq(arg, "cmd")) return false;
    cold_span_copy_buf(code_target, code_cap, callee);

    int32_t line_index = 1;
    int32_t case_count = 0;
    while (line_index + 1 < count) {
        int32_t code = 0;
        if (!cold_parse_if_i32_line(lines[line_index], "code", "==", &code)) break;
        Span target = {0};
        Span call_arg = {0};
        if (!cold_parse_return_call_line(lines[line_index + 1], &target, &call_arg)) return false;
        if (call_arg.len != 0) return false;
        if (case_count >= COLD_COMMAND_CASE_CAP) return false;
        stats->entry_dispatch_cases[case_count].code = code;
        cold_span_copy_buf(stats->entry_dispatch_cases[case_count].target,
                           sizeof(stats->entry_dispatch_cases[case_count].target),
                           target);
        case_count++;
        line_index += 2;
    }

    int32_t default_value = 0;
    if (line_index != count - 1 || !cold_parse_return_i32_line(lines[line_index], &default_value)) return false;
    if (case_count != 6 || default_value != 2) return false;
    for (int32_t i = 0; i < case_count; i++) {
        if (stats->entry_dispatch_cases[i].code != i) return false;
    }
    stats->entry_dispatch_case_count = case_count;
    stats->entry_dispatch_default = default_value;
    return true;
}

static bool cold_recognize_command_code_table(Span body, ColdManifestStats *stats) {
    Span lines[192];
    int32_t count = cold_collect_body_lines(body, lines, 192);
    if (count < 4) return false;

    Span local = {0};
    Span callee = {0};
    Span arg = {0};
    if (!cold_parse_let_call_line(lines[0], &local, &callee, &arg)) return false;
    if (!span_eq(local, "n") || !span_eq(callee, "len") || !span_eq(arg, "cmd")) return false;

    int32_t line_index = 1;
    int32_t case_count = 0;
    while (line_index < count) {
        int32_t command_len = 0;
        if (!cold_parse_command_header_line(lines[line_index], &command_len)) break;
        if (command_len <= 0 || command_len >= (int32_t)sizeof(stats->entry_command_cases[0].text)) return false;
        line_index++;

        char text[64];
        int32_t char_count = 0;
        bool last = false;
        while (!last) {
            if (line_index >= count) return false;
            int32_t index = 0;
            char ch = 0;
            if (!cold_parse_command_char_line(lines[line_index], &index, &ch, &last)) return false;
            if (index != char_count || char_count >= command_len) return false;
            text[char_count++] = ch;
            line_index++;
        }
        if (char_count != command_len) return false;
        text[char_count] = '\0';
        if (line_index >= count) return false;
        int32_t code = 0;
        if (!cold_parse_return_i32_line(lines[line_index], &code)) return false;
        line_index++;
        if (case_count >= COLD_COMMAND_CASE_CAP) return false;
        snprintf(stats->entry_command_cases[case_count].text,
                 sizeof(stats->entry_command_cases[case_count].text), "%s", text);
        stats->entry_command_cases[case_count].code = code;
        case_count++;
    }

    int32_t default_value = 0;
    if (line_index != count - 1 || !cold_parse_return_i32_line(lines[line_index], &default_value)) return false;
    if (case_count != 8 || default_value != -1) return false;
    static const char *expected_texts[] = {
        "help",
        "-h",
        "--help",
        "status",
        "print-build-plan",
        "verify-export-visibility",
        "verify-export-visibility-parallel",
        "system-link-exec",
    };
    static const int32_t expected_codes[] = {0, 0, 0, 1, 2, 3, 4, 5};
    for (int32_t i = 0; i < case_count; i++) {
        if (strcmp(stats->entry_command_cases[i].text, expected_texts[i]) != 0 ||
            stats->entry_command_cases[i].code != expected_codes[i]) return false;
    }
    for (int32_t i = 0; i < case_count; i++) {
        int32_t code = stats->entry_command_cases[i].code;
        bool has_dispatch = false;
        for (int32_t j = 0; j < stats->entry_dispatch_case_count; j++) {
            if (stats->entry_dispatch_cases[j].code == code) {
                has_dispatch = true;
                break;
            }
        }
        if (!has_dispatch) return false;
    }
    stats->entry_command_case_count = case_count;
    stats->entry_command_default = default_value;
    return true;
}

static bool cold_entry_semantics_analyze(ColdContract *contract, ColdManifestStats *stats) {
    char manifest_path[PATH_MAX];
    char root[PATH_MAX];
    if (!cold_contract_manifest_path(contract, manifest_path, sizeof(manifest_path))) {
        return cold_entry_semantics_error(stats, "entry manifest path unavailable");
    }
    cold_contract_workspace_root(contract, root, sizeof(root));
    if (root[0] == '\0') return cold_entry_semantics_error(stats, "workspace root unavailable");

    ColdContract manifest;
    if (!cold_contract_parse(&manifest, manifest_path)) {
        return cold_entry_semantics_error(stats, "entry manifest parse failed");
    }
    Span entry_rel = cold_contract_get(&manifest, "compiler_entry_source");
    if (entry_rel.len <= 0) return cold_entry_semantics_error(stats, "entry source missing");

    char entry_path[PATH_MAX];
    if (!cold_join_root_span(entry_path, sizeof(entry_path), root, entry_rel)) {
        return cold_entry_semantics_error(stats, "entry source path invalid");
    }
    Span source = source_open(entry_path);
    if (source.len <= 0) return cold_entry_semantics_error(stats, "entry source unreadable");

    ColdFunctionSymbol main_symbol;
    Span entry_name = cold_contract_get(contract, "bootstrap_entry");
    bool ok = cold_find_function_symbol_by_name(source, entry_name, &main_symbol);
    if (!ok) {
        munmap((void *)source.ptr, (size_t)source.len);
        return cold_entry_semantics_error(stats, "entry function symbol unavailable");
    }

    Span main_target = {0};
    Span main_arg = {0};
    if (!cold_parse_return_call_line(main_symbol.body, &main_target, &main_arg) || main_arg.len != 0) {
        munmap((void *)source.ptr, (size_t)source.len);
        return cold_entry_semantics_error(stats, "entry body is not strict return-call");
    }
    char main_name_buf[COLD_NAME_CAP];
    char main_target_buf[COLD_NAME_CAP];
    cold_span_copy_buf(main_name_buf, sizeof(main_name_buf), entry_name);
    cold_span_copy_buf(main_target_buf, sizeof(main_target_buf), main_target);
    cold_entry_step_set(stats, 0, main_name_buf, "return_call", main_target_buf, "");

    ColdFunctionSymbol run_symbol;
    if (!cold_find_function_symbol_by_name(source, main_target, &run_symbol)) {
        munmap((void *)source.ptr, (size_t)source.len);
        return cold_entry_semantics_error(stats, "entry return target symbol unavailable");
    }
    char dispatch_target[COLD_NAME_CAP];
    char argc_aux[192];
    if (!cold_recognize_argc_guard_dispatch(run_symbol.body, dispatch_target, sizeof(dispatch_target),
                                            argc_aux, sizeof(argc_aux))) {
        munmap((void *)source.ptr, (size_t)source.len);
        return cold_entry_semantics_error(stats, "run command body is not strict argc guard dispatch");
    }
    cold_entry_step_set(stats, 1, main_target_buf, "argc_guard_dispatch", dispatch_target, argc_aux);

    ColdFunctionSymbol dispatch_symbol;
    if (!cold_find_function_symbol_by_name(source, cold_cstr_span(dispatch_target), &dispatch_symbol)) {
        munmap((void *)source.ptr, (size_t)source.len);
        return cold_entry_semantics_error(stats, "dispatch target symbol unavailable");
    }
    char code_target[COLD_NAME_CAP];
    if (!cold_recognize_dispatch_chain(dispatch_symbol.body, stats, code_target, sizeof(code_target))) {
        munmap((void *)source.ptr, (size_t)source.len);
        return cold_entry_semantics_error(stats, "dispatch body is not strict code switch");
    }
    char dispatch_aux[64];
    snprintf(dispatch_aux, sizeof(dispatch_aux), "cases:%d,default:%d",
             stats->entry_dispatch_case_count, stats->entry_dispatch_default);
    cold_entry_step_set(stats, 2, dispatch_target, "int_code_dispatch", code_target, dispatch_aux);

    ColdFunctionSymbol code_symbol;
    if (!cold_find_function_symbol_by_name(source, cold_cstr_span(code_target), &code_symbol)) {
        munmap((void *)source.ptr, (size_t)source.len);
        return cold_entry_semantics_error(stats, "command code symbol unavailable");
    }
    if (!cold_recognize_command_code_table(code_symbol.body, stats)) {
        munmap((void *)source.ptr, (size_t)source.len);
        return cold_entry_semantics_error(stats, "command code body is not strict string table");
    }
    char command_aux[64];
    snprintf(command_aux, sizeof(command_aux), "commands:%d,default:%d",
             stats->entry_command_case_count, stats->entry_command_default);
    cold_entry_step_set(stats, 3, code_target, "string_command_code", "", command_aux);
    stats->entry_semantics_ok = 1;
    munmap((void *)source.ptr, (size_t)source.len);
    return true;
}

static bool cold_manifest_stats(ColdContract *contract, ColdManifestStats *stats) {
    memset(stats, 0, sizeof(*stats));
    stats->tree_hash = 1469598103934665603ULL;
    stats->declaration_hash = 1469598103934665603ULL;

    char manifest_path[PATH_MAX];
    char root[PATH_MAX];
    if (!cold_contract_manifest_path(contract, manifest_path, sizeof(manifest_path))) return false;
    cold_contract_workspace_root(contract, root, sizeof(root));
    if (root[0] == '\0') return false;

    ColdContract manifest;
    if (!cold_contract_parse(&manifest, manifest_path)) return false;
    Span entry_rel = cold_contract_get(&manifest, "compiler_entry_source");
    Span entry_name = cold_contract_get(contract, "bootstrap_entry");
    cold_span_copy(stats->entry_function, sizeof(stats->entry_function), entry_name);
    if (entry_rel.len > 0) {
        (void)cold_join_root_span(stats->entry_source, sizeof(stats->entry_source), root, entry_rel);
    }

    for (int32_t i = 0; i < manifest.count; i++) {
        Span rel = manifest.fields[i].value;
        char path[PATH_MAX];
        if (!cold_join_root_span(path, sizeof(path), root, rel)) {
            stats->missing_count++;
            if (stats->first_missing[0] == '\0') {
                snprintf(stats->first_missing, sizeof(stats->first_missing), "<invalid manifest path>");
            }
            continue;
        }

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            stats->missing_count++;
            if (stats->first_missing[0] == '\0') snprintf(stats->first_missing, sizeof(stats->first_missing), "%s", path);
            continue;
        }

        stats->source_count++;
        stats->total_bytes += (uint64_t)st.st_size;
        stats->tree_hash = cold_fnv1a64_update(stats->tree_hash, manifest.fields[i].key);
        stats->tree_hash = cold_fnv1a64_update_cstr(stats->tree_hash, "=");
        stats->tree_hash = cold_fnv1a64_update(stats->tree_hash, rel);
        stats->tree_hash = cold_fnv1a64_update_cstr(stats->tree_hash, "\n");

        if (st.st_size == 0) continue;
        Span source = source_open(path);
        if (source.len <= 0) {
            stats->missing_count++;
            if (stats->first_missing[0] == '\0') snprintf(stats->first_missing, sizeof(stats->first_missing), "%s", path);
            continue;
        }
        for (int32_t j = 0; j < source.len; j++) {
            if (source.ptr[j] == '\n') stats->total_lines++;
        }
        if (source.len > 0 && source.ptr[source.len - 1] != '\n') stats->total_lines++;
        stats->tree_hash = cold_fnv1a64_update(stats->tree_hash, source);
        stats->tree_hash = cold_fnv1a64_update_cstr(stats->tree_hash, "\n");
        ColdSourceScanStats scan;
        cold_scan_cheng_source(source, rel, entry_rel, entry_name, &scan);
        stats->import_count += scan.import_count;
        stats->function_count += scan.function_count;
        stats->type_block_count += scan.type_block_count;
        stats->const_block_count += scan.const_block_count;
        stats->importc_count += scan.importc_count;
        stats->declaration_count += scan.declaration_count;
        stats->function_symbol_count += scan.function_symbol_count;
        stats->function_symbol_error_count += scan.function_symbol_error_count;
        stats->declaration_hash = cold_fnv1a64_update(stats->declaration_hash, rel);
        stats->declaration_hash = cold_fnv1a64_update_cstr(stats->declaration_hash, "=");
        char hash_text[32];
        snprintf(hash_text, sizeof(hash_text), "%016llx\n", (unsigned long long)scan.declaration_hash);
        stats->declaration_hash = cold_fnv1a64_update_cstr(stats->declaration_hash, hash_text);
        if (scan.entry_function_found) {
            stats->entry_function_found = 1;
            stats->entry_function_line = scan.entry_function_line;
            stats->entry_function_source_start = scan.entry_function_source_start;
            stats->entry_function_source_len = scan.entry_function_source_len;
            stats->entry_function_params_start = scan.entry_function_params_start;
            stats->entry_function_params_len = scan.entry_function_params_len;
            stats->entry_function_return_start = scan.entry_function_return_start;
            stats->entry_function_return_len = scan.entry_function_return_len;
            stats->entry_function_body_start = scan.entry_function_body_start;
            stats->entry_function_body_len = scan.entry_function_body_len;
            stats->entry_function_has_body = scan.entry_function_has_body;
        }
        munmap((void *)source.ptr, (size_t)source.len);
    }

    if (stats->missing_count == 0 && !stats->entry_function_found) {
        snprintf(stats->first_missing, sizeof(stats->first_missing),
                 "entry function not found: %s", stats->entry_function);
        return false;
    }
    if (stats->missing_count == 0 && stats->function_symbol_error_count > 0) {
        snprintf(stats->first_missing, sizeof(stats->first_missing),
                 "function symbol parse errors: %d", stats->function_symbol_error_count);
        return false;
    }
    if (stats->missing_count == 0 && !cold_entry_semantics_analyze(contract, stats)) {
        snprintf(stats->first_missing, sizeof(stats->first_missing),
                 "entry semantics: %s", stats->entry_semantics_error);
        return false;
    }
    return stats->missing_count == 0;
}

static bool cold_load_contract_from_cli(int argc, char **argv, ColdContract *contract) {
    const char *in_path = cold_flag_value(argc, argv, "--in");
    if (in_path && in_path[0] != '\0') {
        return cold_contract_parse(contract, in_path);
    }
    const char *magic = "CHENG_COLD_EMBEDDED_CONTRACT_V1\n";
    size_t magic_len = strlen(magic);
    if (memcmp(ColdEmbeddedContract, magic, magic_len) == 0 &&
        ColdEmbeddedContract[magic_len] != '\0') {
        const char *path_magic = "CHENG_COLD_EMBEDDED_SOURCE_PATH_V1\n";
        size_t path_magic_len = strlen(path_magic);
        const char *source_path = "<embedded>";
        if (memcmp(ColdEmbeddedSourcePath, path_magic, path_magic_len) == 0 &&
            ColdEmbeddedSourcePath[path_magic_len] != '\0') {
            source_path = ColdEmbeddedSourcePath + path_magic_len;
        } else if (strncmp(ColdEmbeddedSourcePath, "CHENG_COLD_EMBEDDED_SOURCE_PATH_V1",
                           strlen("CHENG_COLD_EMBEDDED_SOURCE_PATH_V1")) == 0) {
            source_path = "<embedded>";
        }
        Span text = {(const uint8_t *)ColdEmbeddedContract + magic_len,
                     (int32_t)strnlen(ColdEmbeddedContract + magic_len,
                                      COLD_EMBEDDED_CONTRACT_CAP - magic_len)};
        return cold_contract_parse_span(contract, source_path, text);
    }
    fprintf(stderr, "[cheng_cold] missing --in:<path>\n");
    return false;
}

static int cold_cmd_print_contract(int argc, char **argv) {
    ColdContract contract;
    if (!cold_load_contract_from_cli(argc, argv, &contract)) return 1;
    if (!cold_contract_validate(&contract)) return 1;
    cold_print_normalized(&contract, stdout, 0);
    return 0;
}

static int cold_cmd_self_check(int argc, char **argv) {
    ColdContract contract;
    if (!cold_load_contract_from_cli(argc, argv, &contract)) return 1;
    if (!cold_contract_validate(&contract)) return 1;
    uint64_t hash = 0;
    cold_print_normalized(&contract, 0, &hash);
    char manifest_path[PATH_MAX];
    printf("bootstrap_name=");
    span_write(stdout, cold_contract_get(&contract, "bootstrap_name"));
    fputc('\n', stdout);
    printf("target=");
    span_write(stdout, cold_contract_get(&contract, "target"));
    fputc('\n', stdout);
    if (cold_contract_manifest_path(&contract, manifest_path, sizeof(manifest_path))) {
        printf("bootstrap_manifest=%s\n", manifest_path);
    }
    printf("contract_hash=%016llx\n", (unsigned long long)hash);
    puts("cheng_bootstrap_self_check=ok");
    return 0;
}

static int64_t cold_find_patch_slot(uint8_t *buf, size_t len, const char *magic, size_t slot_cap) {
    size_t magic_len = strlen(magic);
    int64_t found = -1;
    if (slot_cap < magic_len || len < slot_cap) return -1;
    for (size_t i = 0; i + slot_cap <= len; i++) {
        if (memcmp(buf + i, magic, magic_len) != 0) continue;
        size_t zero_count = 0;
        for (size_t j = magic_len; j < slot_cap; j++) {
            if (buf[i + j] == 0) zero_count++;
        }
        if (zero_count > slot_cap / 2) found = (int64_t)i;
    }
    return found;
}

static bool cold_write_file_bytes(const char *path, const uint8_t *data, size_t len) {
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) return false;
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n <= 0) {
            close(fd);
            return false;
        }
        written += (size_t)n;
    }
    if (fsync(fd) != 0) {
        close(fd);
        return false;
    }
    close(fd);
    chmod(path, 0755);
    return true;
}

static bool cold_write_text_file(const char *path, const char *text) {
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    size_t len = strlen(text);
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, text + written, len - written);
        if (n <= 0) {
            close(fd);
            return false;
        }
        written += (size_t)n;
    }
    close(fd);
    return true;
}

static bool cold_files_equal(const char *left, const char *right) {
    Span a = source_open(left);
    Span b = source_open(right);
    bool ok = a.len > 0 && b.len > 0 &&
              a.len == b.len &&
              memcmp(a.ptr, b.ptr, (size_t)a.len) == 0;
    if (a.len > 0) munmap((void *)a.ptr, (size_t)a.len);
    if (b.len > 0) munmap((void *)b.ptr, (size_t)b.len);
    return ok;
}

static bool cold_run_shell(const char *cmd, const char *label) {
    int rc = system(cmd);
    if (rc == 0) return true;
    if (WIFEXITED(rc)) {
        fprintf(stderr, "[cheng_cold] %s failed rc=%d\n", label, WEXITSTATUS(rc));
    } else {
        fprintf(stderr, "[cheng_cold] %s failed\n", label);
    }
    return false;
}

static bool cold_shell_quote(char *out, size_t cap, const char *text) {
    size_t used = 0;
    if (cap < 3) return false;
    out[used++] = '\'';
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\'') {
            if (used + 4 >= cap) return false;
            out[used++] = '\'';
            out[used++] = '\\';
            out[used++] = '\'';
            out[used++] = '\'';
        } else {
            if (used + 1 >= cap) return false;
            out[used++] = text[i];
        }
    }
    if (used + 1 >= cap) return false;
    out[used++] = '\'';
    out[used] = '\0';
    return true;
}

static bool cold_sign_executable(const char *path) {
    char q_path[PATH_MAX * 5 + 8];
    char cmd[PATH_MAX * 5 + 128];
    if (!cold_shell_quote(q_path, sizeof(q_path), path)) return false;
    snprintf(cmd, sizeof(cmd), "codesign --force -s - %s 2>/dev/null", q_path);
    return cold_run_shell(cmd, "codesign");
}

static bool cold_write_compile_report(const char *path, ColdContract *contract,
                                      const char *source_path, const char *out_path,
                                      uint64_t hash) {
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fputs("compiler_class=", file);
    span_write(file, cold_contract_get(contract, "compiler_class"));
    fputc('\n', file);
    fputs("target=", file);
    span_write(file, cold_contract_get(contract, "target"));
    fputc('\n', file);
    fprintf(file, "source=%s\n", source_path);
    fprintf(file, "output=%s\n", out_path);
    fprintf(file, "contract_hash=%016llx\n", (unsigned long long)hash);
    fputs("cold_compiler=cheng_cold\n", file);
    fputs("direct_macho=1\n", file);
    fputs("self_image_emit=1\n", file);
    fputs("embedded_contract=1\n", file);
    fclose(file);
    return true;
}

static int cold_cmd_compile_bootstrap(int argc, char **argv, const char *self_path) {
    const char *out_path = cold_flag_value(argc, argv, "--out");
    const char *report_path = cold_flag_value(argc, argv, "--report-out");
    if (!out_path || out_path[0] == '\0') {
        fprintf(stderr, "[cheng_cold] missing --out:<path>\n");
        return 1;
    }

    ColdContract contract;
    if (!cold_load_contract_from_cli(argc, argv, &contract)) return 1;
    if (!cold_contract_validate(&contract)) return 1;

    size_t normalized_len = 0;
    uint64_t hash = 0;
    char *normalized = cold_normalized_alloc(&contract, &normalized_len, &hash);
    const char *contract_magic = "CHENG_COLD_EMBEDDED_CONTRACT_V1\n";
    const char *path_magic = "CHENG_COLD_EMBEDDED_SOURCE_PATH_V1\n";
    size_t contract_magic_len = strlen(contract_magic);
    if (contract_magic_len + normalized_len + 1u > COLD_EMBEDDED_CONTRACT_CAP) {
        fprintf(stderr, "[cheng_cold] normalized contract too large for embedded slot\n");
        free(normalized);
        return 1;
    }

    char abs_in[PATH_MAX];
    cold_absolute_path(contract.source_path, abs_in, sizeof(abs_in));
    size_t path_magic_len = strlen(path_magic);
    if (path_magic_len + strlen(abs_in) + 1u > COLD_EMBEDDED_SOURCE_PATH_CAP) {
        fprintf(stderr, "[cheng_cold] source path too long for embedded slot\n");
        free(normalized);
        return 1;
    }

    Span self = source_open(self_path);
    if (self.len <= 0) {
        fprintf(stderr, "[cheng_cold] cannot read self image: %s\n", self_path);
        free(normalized);
        return 1;
    }
    uint8_t *image = malloc((size_t)self.len);
    if (!image) die("out of memory copying self image");
    memcpy(image, self.ptr, (size_t)self.len);
    munmap((void *)self.ptr, (size_t)self.len);

    int64_t contract_slot = cold_find_patch_slot(image, (size_t)self.len,
                                                 contract_magic,
                                                 COLD_EMBEDDED_CONTRACT_CAP);
    int64_t path_slot = cold_find_patch_slot(image, (size_t)self.len,
                                             path_magic,
                                             COLD_EMBEDDED_SOURCE_PATH_CAP);
    if (contract_slot < 0 || path_slot < 0) {
        fprintf(stderr, "[cheng_cold] embedded patch slots missing\n");
        free(image);
        free(normalized);
        return 1;
    }

    memset(image + contract_slot, 0, COLD_EMBEDDED_CONTRACT_CAP);
    memcpy(image + contract_slot, contract_magic, contract_magic_len);
    memcpy(image + contract_slot + contract_magic_len, normalized, normalized_len);

    memset(image + path_slot, 0, COLD_EMBEDDED_SOURCE_PATH_CAP);
    memcpy(image + path_slot, path_magic, path_magic_len);
    memcpy(image + path_slot + path_magic_len, abs_in, strlen(abs_in));

    if (!cold_write_file_bytes(out_path, image, (size_t)self.len)) {
        fprintf(stderr, "[cheng_cold] failed to write output: %s\n", out_path);
        free(image);
        free(normalized);
        return 1;
    }
    if (!cold_sign_executable(out_path)) {
        fprintf(stderr, "[cheng_cold] failed to codesign output: %s\n", out_path);
        free(image);
        free(normalized);
        return 1;
    }
    if (report_path && report_path[0] != '\0') {
        if (!cold_write_compile_report(report_path, &contract, abs_in, out_path, hash)) {
            fprintf(stderr, "[cheng_cold] failed to write report: %s\n", report_path);
            free(image);
            free(normalized);
            return 1;
        }
    }
    printf("compiled=%s\n", out_path);
    printf("contract_hash=%016llx\n", (unsigned long long)hash);
    free(image);
    free(normalized);
    return 0;
}

static bool cold_embedded_contract_available(void) {
    const char *magic = "CHENG_COLD_EMBEDDED_CONTRACT_V1\n";
    size_t magic_len = strlen(magic);
    return memcmp(ColdEmbeddedContract, magic, magic_len) == 0 &&
           ColdEmbeddedContract[magic_len] != '\0';
}

static bool cold_bridge_command(char *out, size_t cap, const char *exe,
                                const char *subcommand,
                                const char *in_path,
                                const char *out_path,
                                const char *report_path,
                                const char *redirect_path) {
    char q_exe[PATH_MAX * 5 + 8];
    char q_in[PATH_MAX * 5 + 8];
    char q_out[PATH_MAX * 5 + 8];
    char q_report[PATH_MAX * 5 + 8];
    char q_redirect[PATH_MAX * 5 + 8];
    if (!cold_shell_quote(q_exe, sizeof(q_exe), exe)) return false;
    if (in_path && !cold_shell_quote(q_in, sizeof(q_in), in_path)) return false;
    if (out_path && !cold_shell_quote(q_out, sizeof(q_out), out_path)) return false;
    if (report_path && !cold_shell_quote(q_report, sizeof(q_report), report_path)) return false;
    if (redirect_path && !cold_shell_quote(q_redirect, sizeof(q_redirect), redirect_path)) return false;

    int n = 0;
    if (strcmp(subcommand, "compile-bootstrap") == 0) {
        n = snprintf(out, cap, "%s compile-bootstrap%s%s --out:%s --report-out:%s%s%s",
                     q_exe,
                     in_path ? " --in:" : "",
                     in_path ? q_in : "",
                     q_out,
                     q_report,
                     redirect_path ? " > " : "",
                     redirect_path ? q_redirect : "");
    } else {
        n = snprintf(out, cap, "%s %s%s%s",
                     q_exe,
                     subcommand,
                     redirect_path ? " > " : "",
                     redirect_path ? q_redirect : "");
    }
    if (n < 0 || (size_t)n >= cap) return false;
    if (redirect_path) {
        size_t used = strlen(out);
        if (used + 5 >= cap) return false;
        memcpy(out + used, " 2>&1", 6);
    }
    return true;
}

static int cold_cmd_bootstrap_bridge(int argc, char **argv, const char *self_path) {
    const char *cli_in = cold_flag_value(argc, argv, "--in");
    const char *out_dir = cold_flag_value(argc, argv, "--out-dir");
    if (!out_dir || out_dir[0] == '\0') out_dir = "artifacts/bootstrap-cold";

    char abs_out_dir[PATH_MAX];
    cold_absolute_path(out_dir, abs_out_dir, sizeof(abs_out_dir));
    if (!cold_mkdir_p(abs_out_dir)) {
        fprintf(stderr, "[cheng_cold] failed to create out dir: %s\n", abs_out_dir);
        return 1;
    }

    bool use_embedded_input = false;
    const char *stage0_in = cli_in;
    if (!stage0_in || stage0_in[0] == '\0') {
        if (cold_embedded_contract_available()) {
            use_embedded_input = true;
        } else {
            stage0_in = "bootstrap/stage1_bootstrap.cheng";
        }
    }

    ColdContract contract;
    if (use_embedded_input) {
        if (!cold_load_contract_from_cli(argc, argv, &contract)) return 1;
    } else {
        if (!cold_contract_parse(&contract, stage0_in)) return 1;
    }
    if (!cold_contract_validate(&contract)) return 1;

    char abs_stage0_in[PATH_MAX];
    const char *stage0_in_arg = 0;
    if (!use_embedded_input) {
        cold_absolute_path(stage0_in, abs_stage0_in, sizeof(abs_stage0_in));
        stage0_in_arg = abs_stage0_in;
    }

    char stage0[PATH_MAX], stage1[PATH_MAX], stage2[PATH_MAX], stage3[PATH_MAX];
    char stage0_report[PATH_MAX], stage1_report[PATH_MAX], stage2_report[PATH_MAX], stage3_report[PATH_MAX];
    char stage0_compile_log[PATH_MAX], stage1_compile_log[PATH_MAX], stage2_compile_log[PATH_MAX], stage3_compile_log[PATH_MAX];
    char stage0_self_log[PATH_MAX], stage1_self_log[PATH_MAX], stage2_self_log[PATH_MAX], stage3_self_log[PATH_MAX];
    char stage2_contract[PATH_MAX], stage3_contract[PATH_MAX], env_path[PATH_MAX];

    cold_join_path(stage0, sizeof(stage0), abs_out_dir, "cheng.stage0");
    cold_join_path(stage1, sizeof(stage1), abs_out_dir, "cheng.stage1");
    cold_join_path(stage2, sizeof(stage2), abs_out_dir, "cheng.stage2");
    cold_join_path(stage3, sizeof(stage3), abs_out_dir, "cheng.stage3");
    cold_join_path(stage0_report, sizeof(stage0_report), abs_out_dir, "cheng.stage0.report.txt");
    cold_join_path(stage1_report, sizeof(stage1_report), abs_out_dir, "cheng.stage1.report.txt");
    cold_join_path(stage2_report, sizeof(stage2_report), abs_out_dir, "cheng.stage2.report.txt");
    cold_join_path(stage3_report, sizeof(stage3_report), abs_out_dir, "cheng.stage3.report.txt");
    cold_join_path(stage0_compile_log, sizeof(stage0_compile_log), abs_out_dir, "cheng.stage0.compile.log");
    cold_join_path(stage1_compile_log, sizeof(stage1_compile_log), abs_out_dir, "cheng.stage1.compile.log");
    cold_join_path(stage2_compile_log, sizeof(stage2_compile_log), abs_out_dir, "cheng.stage2.compile.log");
    cold_join_path(stage3_compile_log, sizeof(stage3_compile_log), abs_out_dir, "cheng.stage3.compile.log");
    cold_join_path(stage0_self_log, sizeof(stage0_self_log), abs_out_dir, "cheng.stage0.self-check.log");
    cold_join_path(stage1_self_log, sizeof(stage1_self_log), abs_out_dir, "cheng.stage1.self-check.log");
    cold_join_path(stage2_self_log, sizeof(stage2_self_log), abs_out_dir, "cheng.stage2.self-check.log");
    cold_join_path(stage3_self_log, sizeof(stage3_self_log), abs_out_dir, "cheng.stage3.self-check.log");
    cold_join_path(stage2_contract, sizeof(stage2_contract), abs_out_dir, "cheng.stage2.contract.txt");
    cold_join_path(stage3_contract, sizeof(stage3_contract), abs_out_dir, "cheng.stage3.contract.txt");
    cold_join_path(env_path, sizeof(env_path), abs_out_dir, "bootstrap.env");

    char abs_self[PATH_MAX];
    cold_absolute_path(self_path, abs_self, sizeof(abs_self));

    char cmd[PATH_MAX * 36];
    if (!cold_bridge_command(cmd, sizeof(cmd), abs_self, "compile-bootstrap",
                             stage0_in_arg, stage0, stage0_report, stage0_compile_log) ||
        !cold_run_shell(cmd, "stage0 compile-bootstrap")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage0, "self-check",
                             0, 0, 0, stage0_self_log) ||
        !cold_run_shell(cmd, "stage0 self-check")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage0, "compile-bootstrap",
                             0, stage1, stage1_report, stage1_compile_log) ||
        !cold_run_shell(cmd, "stage1 compile-bootstrap")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage1, "self-check",
                             0, 0, 0, stage1_self_log) ||
        !cold_run_shell(cmd, "stage1 self-check")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage1, "compile-bootstrap",
                             0, stage2, stage2_report, stage2_compile_log) ||
        !cold_run_shell(cmd, "stage2 compile-bootstrap")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage2, "self-check",
                             0, 0, 0, stage2_self_log) ||
        !cold_run_shell(cmd, "stage2 self-check")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage2, "compile-bootstrap",
                             0, stage3, stage3_report, stage3_compile_log) ||
        !cold_run_shell(cmd, "stage3 compile-bootstrap")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage3, "self-check",
                             0, 0, 0, stage3_self_log) ||
        !cold_run_shell(cmd, "stage3 self-check")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage2, "print-contract",
                             0, 0, 0, stage2_contract) ||
        !cold_run_shell(cmd, "stage2 print-contract")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), stage3, "print-contract",
                             0, 0, 0, stage3_contract) ||
        !cold_run_shell(cmd, "stage3 print-contract")) return 1;

    if (!cold_files_equal(stage2_contract, stage3_contract)) {
        fprintf(stderr, "[cheng_cold] stage2/stage3 contract fixed point mismatch\n");
        return 1;
    }

    char env_text[PATH_MAX * 6];
    snprintf(env_text, sizeof(env_text),
             "out_dir=%s\n"
             "stage0=%s\n"
             "stage1=%s\n"
             "stage2=%s\n"
             "stage3=%s\n"
             "fixed_point=stage2_stage3_contract_match\n",
             abs_out_dir, stage0, stage1, stage2, stage3);
    if (!cold_write_text_file(env_path, env_text)) {
        fprintf(stderr, "[cheng_cold] failed to write env: %s\n", env_path);
        return 1;
    }

    printf("bootstrap_bridge=ok\n");
    printf("out_dir=%s\n", abs_out_dir);
    printf("stage0=%s\n", stage0);
    printf("stage1=%s\n", stage1);
    printf("stage2=%s\n", stage2);
    printf("stage3=%s\n", stage3);
    printf("fixed_point=stage2_stage3_contract_match\n");
    return 0;
}

static bool cold_path_with_suffix(char *out, size_t cap, const char *base, const char *suffix) {
    size_t base_len = strlen(base);
    size_t suffix_len = strlen(suffix);
    if (base_len + suffix_len + 1u > cap) return false;
    memcpy(out, base, base_len);
    memcpy(out + base_len, suffix, suffix_len);
    out[base_len + suffix_len] = '\0';
    return true;
}

static bool cold_write_normalized_contract_file(const char *path, ColdContract *contract) {
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    FILE *file = fopen(path, "w");
    if (!file) return false;
    cold_print_normalized(contract, file, 0);
    fclose(file);
    return true;
}

static bool cold_write_backend_driver_report(const char *path,
                                             ColdContract *contract,
                                             ColdManifestStats *stats,
                                             const char *contract_path,
                                             const char *out_path,
                                             const char *dispatch_path,
                                             const char *map_path,
                                             const char *index_path,
                                             uint64_t contract_hash) {
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    char manifest_path[PATH_MAX];
    if (!cold_contract_manifest_path(contract, manifest_path, sizeof(manifest_path))) return false;
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fputs("backend_driver_candidate=contract_only\n", file);
    fputs("compiler_class=", file);
    span_write(file, cold_contract_get(contract, "compiler_class"));
    fputc('\n', file);
    fputs("target=", file);
    span_write(file, cold_contract_get(contract, "target"));
    fputc('\n', file);
    fprintf(file, "contract=%s\n", contract_path);
    fprintf(file, "manifest=%s\n", manifest_path);
    fprintf(file, "output=%s\n", out_path);
    fprintf(file, "entry_dispatch_executable=%s\n", dispatch_path);
    fprintf(file, "map=%s\n", map_path);
    fprintf(file, "cold_frontend_index=%s\n", index_path);
    fprintf(file, "contract_hash=%016llx\n", (unsigned long long)contract_hash);
    fprintf(file, "manifest_source_count=%d\n", stats->source_count);
    fprintf(file, "manifest_source_bytes=%llu\n", (unsigned long long)stats->total_bytes);
    fprintf(file, "manifest_source_lines=%llu\n", (unsigned long long)stats->total_lines);
    fprintf(file, "manifest_source_tree_hash=%016llx\n", (unsigned long long)stats->tree_hash);
    fprintf(file, "cold_frontend_declaration_count=%d\n", stats->declaration_count);
    fprintf(file, "cold_frontend_import_count=%d\n", stats->import_count);
    fprintf(file, "cold_frontend_function_count=%d\n", stats->function_count);
    fprintf(file, "cold_frontend_function_symbol_count=%d\n", stats->function_symbol_count);
    fprintf(file, "cold_frontend_function_symbol_error_count=%d\n", stats->function_symbol_error_count);
    fprintf(file, "cold_frontend_type_block_count=%d\n", stats->type_block_count);
    fprintf(file, "cold_frontend_const_block_count=%d\n", stats->const_block_count);
    fprintf(file, "cold_frontend_importc_count=%d\n", stats->importc_count);
    fprintf(file, "cold_frontend_declaration_hash=%016llx\n", (unsigned long long)stats->declaration_hash);
    fprintf(file, "cold_frontend_entry_source=%s\n", stats->entry_source);
    fprintf(file, "cold_frontend_entry_function=%s\n", stats->entry_function);
    fprintf(file, "cold_frontend_entry_function_line=%d\n", stats->entry_function_line);
    fprintf(file, "cold_frontend_entry_found=%d\n", stats->entry_function_found);
    fprintf(file, "cold_frontend_entry_source_span_start=%d\n", stats->entry_function_source_start);
    fprintf(file, "cold_frontend_entry_source_span_len=%d\n", stats->entry_function_source_len);
    fprintf(file, "cold_frontend_entry_params_span_start=%d\n", stats->entry_function_params_start);
    fprintf(file, "cold_frontend_entry_params_span_len=%d\n", stats->entry_function_params_len);
    fprintf(file, "cold_frontend_entry_return_span_start=%d\n", stats->entry_function_return_start);
    fprintf(file, "cold_frontend_entry_return_span_len=%d\n", stats->entry_function_return_len);
    fprintf(file, "cold_frontend_entry_body_span_start=%d\n", stats->entry_function_body_start);
    fprintf(file, "cold_frontend_entry_body_span_len=%d\n", stats->entry_function_body_len);
    fprintf(file, "cold_frontend_entry_has_body=%d\n", stats->entry_function_has_body);
    fprintf(file, "cold_frontend_entry_semantics_ok=%d\n", stats->entry_semantics_ok);
    fprintf(file, "cold_frontend_entry_chain_step_count=%d\n", stats->entry_chain_step_count);
    fprintf(file, "cold_frontend_entry_dispatch_case_count=%d\n", stats->entry_dispatch_case_count);
    fprintf(file, "cold_frontend_entry_dispatch_default=%d\n", stats->entry_dispatch_default);
    fprintf(file, "cold_frontend_entry_command_case_count=%d\n", stats->entry_command_case_count);
    fprintf(file, "cold_frontend_entry_command_default=%d\n", stats->entry_command_default);
    fputs("cold_frontend_entry_dispatch_codegen=1\n", file);
    fputs("cold_frontend_entry_dispatch_verified=1\n", file);
    fputs("cold_frontend_entry_dispatch_output=exit_code_only\n", file);
    for (int32_t i = 0; i < stats->entry_chain_step_count; i++) {
        fprintf(file, "cold_frontend_entry_chain[%d]=function:%s,kind:%s,target:%s,aux:%s\n",
                i,
                stats->entry_chain[i].function,
                stats->entry_chain[i].kind,
                stats->entry_chain[i].target,
                stats->entry_chain[i].aux);
    }
    for (int32_t i = 0; i < stats->entry_dispatch_case_count; i++) {
        fprintf(file, "cold_frontend_entry_dispatch_case[%d]=code:%d,target:%s\n",
                i,
                stats->entry_dispatch_cases[i].code,
                stats->entry_dispatch_cases[i].target);
    }
    for (int32_t i = 0; i < stats->entry_command_case_count; i++) {
        fprintf(file, "cold_frontend_entry_command_case[%d]=text:%s,code:%d\n",
                i,
                stats->entry_command_cases[i].text,
                stats->entry_command_cases[i].code);
    }
    fputs("cold_compiler=cheng_cold\n", file);
    fputs("direct_macho=1\n", file);
    fputs("self_image_emit=1\n", file);
    fputs("embedded_contract=1\n", file);
    fputs("real_backend_codegen=0\n", file);
    fputs("system_link_exec=0\n", file);
    fclose(file);
    return true;
}

static bool cold_write_backend_driver_map(const char *path,
                                          ColdContract *contract,
                                          ColdManifestStats *stats,
                                          const char *out_path,
                                          const char *dispatch_path,
                                          const char *index_path,
                                          uint64_t contract_hash) {
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    char manifest_path[PATH_MAX];
    if (!cold_contract_manifest_path(contract, manifest_path, sizeof(manifest_path))) return false;
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fputs("map_format=cold_backend_driver_contract_candidate_v1\n", file);
    fprintf(file, "candidate=%s\n", out_path);
    fprintf(file, "entry_dispatch_executable=%s\n", dispatch_path);
    fprintf(file, "manifest=%s\n", manifest_path);
    fprintf(file, "cold_frontend_index=%s\n", index_path);
    fprintf(file, "contract_hash=%016llx\n", (unsigned long long)contract_hash);
    fprintf(file, "manifest_source_count=%d\n", stats->source_count);
    fprintf(file, "manifest_source_bytes=%llu\n", (unsigned long long)stats->total_bytes);
    fprintf(file, "manifest_source_tree_hash=%016llx\n", (unsigned long long)stats->tree_hash);
    fprintf(file, "cold_frontend_declaration_count=%d\n", stats->declaration_count);
    fprintf(file, "cold_frontend_function_count=%d\n", stats->function_count);
    fprintf(file, "cold_frontend_function_symbol_count=%d\n", stats->function_symbol_count);
    fprintf(file, "cold_frontend_entry_semantics_ok=%d\n", stats->entry_semantics_ok);
    fprintf(file, "cold_frontend_entry_chain_step_count=%d\n", stats->entry_chain_step_count);
    fprintf(file, "cold_frontend_entry_command_case_count=%d\n", stats->entry_command_case_count);
    fprintf(file, "cold_frontend_entry_dispatch_case_count=%d\n", stats->entry_dispatch_case_count);
    fputs("cold_frontend_entry_dispatch_codegen=1\n", file);
    fputs("cold_frontend_entry_dispatch_verified=1\n", file);
    fprintf(file, "cold_frontend_declaration_hash=%016llx\n", (unsigned long long)stats->declaration_hash);
    fputs("command_surface=print-contract,self-check,compile-bootstrap,bootstrap-bridge,build-backend-driver\n", file);
    fputs("real_backend_codegen=0\n", file);
    fclose(file);
    return true;
}

static void cold_write_function_symbols_for_source(FILE *file, Span source,
                                                   Span rel, int32_t *ordinal) {
    int32_t pos = 0;
    int32_t line_no = 0;
    bool in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        line_no++;
        Span line = span_sub(source, start, end);
        Span trimmed = span_trim(line);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line) && cold_line_has_triple_quote(line)) {
            in_triple = true;
            continue;
        }
        if (!cold_line_top_level(line)) continue;
        Span fn_name = {0};
        if (!cold_parse_fn_name(trimmed, &fn_name)) continue;
        ColdFunctionSymbol symbol;
        if (!cold_parse_function_symbol_at(source, start, line_no, &symbol)) continue;
        int32_t source_start = cold_span_offset(source, symbol.source_span);
        int32_t params_start = cold_span_offset(source, symbol.params);
        int32_t return_start = cold_span_offset(source, symbol.return_type);
        int32_t body_start = cold_span_offset(source, symbol.body);
        fprintf(file, "fn_symbol[%d]=source:", *ordinal);
        span_write(file, rel);
        fprintf(file, ",name:");
        span_write(file, symbol.name);
        fprintf(file,
                ",line:%d,decl_start:%d,decl_len:%d,params_start:%d,params_len:%d,return_start:%d,return_len:%d,body_start:%d,body_len:%d,has_body:%d\n",
                symbol.line,
                source_start,
                symbol.source_span.len,
                params_start,
                symbol.params.len,
                return_start,
                symbol.return_type.len,
                body_start,
                symbol.body.len,
                symbol.has_body);
        (*ordinal)++;
    }
}

static bool cold_write_frontend_index_file(const char *path,
                                           ColdContract *contract,
                                           ColdManifestStats *stats) {
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    char manifest_path[PATH_MAX];
    char root[PATH_MAX];
    if (!cold_contract_manifest_path(contract, manifest_path, sizeof(manifest_path))) return false;
    cold_contract_workspace_root(contract, root, sizeof(root));
    if (root[0] == '\0') return false;

    ColdContract manifest;
    if (!cold_contract_parse(&manifest, manifest_path)) return false;
    Span entry_rel = cold_contract_get(&manifest, "compiler_entry_source");
    Span entry_name = cold_contract_get(contract, "bootstrap_entry");

    FILE *file = fopen(path, "w");
    if (!file) return false;
    fputs("index_format=cold_frontend_source_index_v1\n", file);
    fprintf(file, "manifest=%s\n", manifest_path);
    fprintf(file, "source_count=%d\n", stats->source_count);
    fprintf(file, "declaration_count=%d\n", stats->declaration_count);
    fprintf(file, "import_count=%d\n", stats->import_count);
    fprintf(file, "function_count=%d\n", stats->function_count);
    fprintf(file, "function_symbol_count=%d\n", stats->function_symbol_count);
    fprintf(file, "function_symbol_error_count=%d\n", stats->function_symbol_error_count);
    fprintf(file, "type_block_count=%d\n", stats->type_block_count);
    fprintf(file, "const_block_count=%d\n", stats->const_block_count);
    fprintf(file, "importc_count=%d\n", stats->importc_count);
    fprintf(file, "declaration_hash=%016llx\n", (unsigned long long)stats->declaration_hash);
    fprintf(file, "entry_source=%s\n", stats->entry_source);
    fprintf(file, "entry_function=%s\n", stats->entry_function);
    fprintf(file, "entry_function_line=%d\n", stats->entry_function_line);
    fprintf(file, "entry_found=%d\n", stats->entry_function_found);
    fprintf(file, "entry_source_span_start=%d\n", stats->entry_function_source_start);
    fprintf(file, "entry_source_span_len=%d\n", stats->entry_function_source_len);
    fprintf(file, "entry_params_span_start=%d\n", stats->entry_function_params_start);
    fprintf(file, "entry_params_span_len=%d\n", stats->entry_function_params_len);
    fprintf(file, "entry_return_span_start=%d\n", stats->entry_function_return_start);
    fprintf(file, "entry_return_span_len=%d\n", stats->entry_function_return_len);
    fprintf(file, "entry_body_span_start=%d\n", stats->entry_function_body_start);
    fprintf(file, "entry_body_span_len=%d\n", stats->entry_function_body_len);
    fprintf(file, "entry_has_body=%d\n", stats->entry_function_has_body);
    fprintf(file, "entry_semantics_ok=%d\n", stats->entry_semantics_ok);
    fprintf(file, "entry_chain_step_count=%d\n", stats->entry_chain_step_count);
    fprintf(file, "entry_dispatch_case_count=%d\n", stats->entry_dispatch_case_count);
    fprintf(file, "entry_dispatch_default=%d\n", stats->entry_dispatch_default);
    fprintf(file, "entry_command_case_count=%d\n", stats->entry_command_case_count);
    fprintf(file, "entry_command_default=%d\n", stats->entry_command_default);
    for (int32_t i = 0; i < stats->entry_chain_step_count; i++) {
        fprintf(file, "entry_chain[%d]=function:%s,kind:%s,target:%s,aux:%s\n",
                i,
                stats->entry_chain[i].function,
                stats->entry_chain[i].kind,
                stats->entry_chain[i].target,
                stats->entry_chain[i].aux);
    }
    for (int32_t i = 0; i < stats->entry_dispatch_case_count; i++) {
        fprintf(file, "entry_dispatch_case[%d]=code:%d,target:%s\n",
                i,
                stats->entry_dispatch_cases[i].code,
                stats->entry_dispatch_cases[i].target);
    }
    for (int32_t i = 0; i < stats->entry_command_case_count; i++) {
        fprintf(file, "entry_command_case[%d]=text:%s,code:%d\n",
                i,
                stats->entry_command_cases[i].text,
                stats->entry_command_cases[i].code);
    }

    int32_t function_ordinal = 0;
    for (int32_t i = 0; i < manifest.count; i++) {
        Span rel = manifest.fields[i].value;
        char source_path[PATH_MAX];
        if (!cold_join_root_span(source_path, sizeof(source_path), root, rel)) {
            fclose(file);
            return false;
        }
        Span source = source_open(source_path);
        if (source.len <= 0) {
            fclose(file);
            return false;
        }
        ColdSourceScanStats scan;
        cold_scan_cheng_source(source, rel, entry_rel, entry_name, &scan);
        fprintf(file,
                "source[%d]=path:", i);
        span_write(file, rel);
        fprintf(file,
                ",lines:%llu,bytes:%llu,imports:%d,functions:%d,function_symbols:%d,type_blocks:%d,const_blocks:%d,importc:%d,declarations:%d,declaration_hash:%016llx,entry:%d,entry_line:%d\n",
                (unsigned long long)scan.line_count,
                (unsigned long long)scan.byte_count,
                scan.import_count,
                scan.function_count,
                scan.function_symbol_count,
                scan.type_block_count,
                scan.const_block_count,
                scan.importc_count,
                scan.declaration_count,
                (unsigned long long)scan.declaration_hash,
                scan.entry_function_found,
                scan.entry_function_line);
        cold_write_function_symbols_for_source(file, source, rel, &function_ordinal);
        munmap((void *)source.ptr, (size_t)source.len);
    }
    fprintf(file, "function_symbol_written_count=%d\n", function_ordinal);
    fclose(file);
    return true;
}

static bool cold_write_entry_dispatch_executable(const char *path, ColdManifestStats *stats);
static bool cold_verify_entry_dispatch_executable(const char *path, ColdManifestStats *stats);

static int cold_cmd_build_backend_driver(int argc, char **argv, const char *self_path) {
    const char *contract_path = cold_flag_value(argc, argv, "--in");
    if (!contract_path || contract_path[0] == '\0') contract_path = cold_flag_value(argc, argv, "--contract");
    if (!contract_path || contract_path[0] == '\0') contract_path = "bootstrap/driver_bootstrap_contract.cheng";

    const char *out_dir_arg = cold_flag_value(argc, argv, "--out-dir");
    const char *out_arg = cold_flag_value(argc, argv, "--out");

    char abs_contract[PATH_MAX];
    char abs_out_dir[PATH_MAX];
    char out_path[PATH_MAX];
    cold_absolute_path(contract_path, abs_contract, sizeof(abs_contract));

    if (out_arg && out_arg[0] != '\0') {
        cold_absolute_path(out_arg, out_path, sizeof(out_path));
        char parent[PATH_MAX];
        if (cold_parent_dir(out_path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) {
            fprintf(stderr, "[cheng_cold] failed to create output parent: %s\n", parent);
            return 1;
        }
    } else {
        if (!out_dir_arg || out_dir_arg[0] == '\0') out_dir_arg = "artifacts/backend_driver-cold";
        cold_absolute_path(out_dir_arg, abs_out_dir, sizeof(abs_out_dir));
        if (!cold_mkdir_p(abs_out_dir)) {
            fprintf(stderr, "[cheng_cold] failed to create out dir: %s\n", abs_out_dir);
            return 1;
        }
        cold_join_path(out_path, sizeof(out_path), abs_out_dir, "cheng");
    }

    char report_path[PATH_MAX];
    char map_path[PATH_MAX];
    char index_path[PATH_MAX];
    char dispatch_path[PATH_MAX];
    char self_log[PATH_MAX];
    char actual_contract_path[PATH_MAX];
    char expected_contract_path[PATH_MAX];
    const char *report_arg = cold_flag_value(argc, argv, "--report-out");
    const char *map_arg = cold_flag_value(argc, argv, "--map-out");
    const char *index_arg = cold_flag_value(argc, argv, "--index-out");
    if (report_arg && report_arg[0] != '\0') cold_absolute_path(report_arg, report_path, sizeof(report_path));
    else if (!cold_path_with_suffix(report_path, sizeof(report_path), out_path, ".report.txt")) return 1;
    if (map_arg && map_arg[0] != '\0') cold_absolute_path(map_arg, map_path, sizeof(map_path));
    else if (!cold_path_with_suffix(map_path, sizeof(map_path), out_path, ".map")) return 1;
    if (index_arg && index_arg[0] != '\0') cold_absolute_path(index_arg, index_path, sizeof(index_path));
    else if (!cold_path_with_suffix(index_path, sizeof(index_path), out_path, ".source-index.txt")) return 1;
    if (!cold_path_with_suffix(dispatch_path, sizeof(dispatch_path), out_path, ".entry-dispatch")) return 1;
    if (!cold_path_with_suffix(self_log, sizeof(self_log), out_path, ".self-check.log")) return 1;
    if (!cold_path_with_suffix(actual_contract_path, sizeof(actual_contract_path), out_path, ".contract.txt")) return 1;
    if (!cold_path_with_suffix(expected_contract_path, sizeof(expected_contract_path), out_path, ".expected-contract.txt")) return 1;

    ColdContract contract;
    if (!cold_contract_parse(&contract, abs_contract)) return 1;
    if (!cold_contract_validate(&contract)) return 1;

    ColdManifestStats stats;
    if (!cold_manifest_stats(&contract, &stats)) {
        fprintf(stderr, "[cheng_cold] backend driver manifest source missing: %s\n",
                stats.first_missing[0] ? stats.first_missing : "<unknown>");
        return 1;
    }

    size_t normalized_len = 0;
    uint64_t contract_hash = 0;
    char *normalized = cold_normalized_alloc(&contract, &normalized_len, &contract_hash);
    free(normalized);

    char in_arg[PATH_MAX + 16];
    char out_flag[PATH_MAX + 16];
    char report_flag[PATH_MAX + 16];
    snprintf(in_arg, sizeof(in_arg), "--in:%s", abs_contract);
    snprintf(out_flag, sizeof(out_flag), "--out:%s", out_path);
    snprintf(report_flag, sizeof(report_flag), "--report-out:%s", report_path);
    char *compile_argv[] = {
        argv[0],
        "compile-bootstrap",
        in_arg,
        out_flag,
        report_flag,
    };
    int compile_rc = cold_cmd_compile_bootstrap(5, compile_argv, self_path);
    if (compile_rc != 0) return compile_rc;

    if (!span_eq(cold_contract_get(&contract, "target"), "arm64-apple-darwin")) {
        fprintf(stderr, "[cheng_cold] entry dispatch executable only supports arm64-apple-darwin\n");
        return 1;
    }
    if (!cold_write_entry_dispatch_executable(dispatch_path, &stats)) {
        fprintf(stderr, "[cheng_cold] failed to write entry dispatch executable: %s\n", dispatch_path);
        return 1;
    }
    if (!cold_verify_entry_dispatch_executable(dispatch_path, &stats)) {
        fprintf(stderr, "[cheng_cold] entry dispatch executable verification failed: %s\n", dispatch_path);
        return 1;
    }

    if (!cold_write_backend_driver_report(report_path, &contract, &stats,
                                          abs_contract, out_path, dispatch_path, map_path,
                                          index_path,
                                          contract_hash)) {
        fprintf(stderr, "[cheng_cold] failed to write backend driver report: %s\n", report_path);
        return 1;
    }
    if (!cold_write_backend_driver_map(map_path, &contract, &stats, out_path, dispatch_path, index_path, contract_hash)) {
        fprintf(stderr, "[cheng_cold] failed to write backend driver map: %s\n", map_path);
        return 1;
    }
    if (!cold_write_frontend_index_file(index_path, &contract, &stats)) {
        fprintf(stderr, "[cheng_cold] failed to write frontend index: %s\n", index_path);
        return 1;
    }
    if (!cold_write_normalized_contract_file(expected_contract_path, &contract)) {
        fprintf(stderr, "[cheng_cold] failed to write expected contract: %s\n", expected_contract_path);
        return 1;
    }

    char cmd[PATH_MAX * 12];
    if (!cold_bridge_command(cmd, sizeof(cmd), out_path, "self-check", 0, 0, 0, self_log) ||
        !cold_run_shell(cmd, "backend driver candidate self-check")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), out_path, "print-contract", 0, 0, 0, actual_contract_path) ||
        !cold_run_shell(cmd, "backend driver candidate print-contract")) return 1;
    if (!cold_files_equal(expected_contract_path, actual_contract_path)) {
        fprintf(stderr, "[cheng_cold] backend driver candidate contract mismatch\n");
        return 1;
    }

    printf("backend_driver_candidate=ok\n");
    printf("candidate_kind=contract_only\n");
    printf("out=%s\n", out_path);
    printf("entry_dispatch_executable=%s\n", dispatch_path);
    printf("report=%s\n", report_path);
    printf("map=%s\n", map_path);
    printf("cold_frontend_index=%s\n", index_path);
    printf("manifest_source_count=%d\n", stats.source_count);
    printf("manifest_source_bytes=%llu\n", (unsigned long long)stats.total_bytes);
    printf("manifest_source_tree_hash=%016llx\n", (unsigned long long)stats.tree_hash);
    printf("cold_frontend_declaration_count=%d\n", stats.declaration_count);
    printf("cold_frontend_function_count=%d\n", stats.function_count);
    printf("cold_frontend_function_symbol_count=%d\n", stats.function_symbol_count);
    printf("cold_frontend_entry_function_line=%d\n", stats.entry_function_line);
    printf("cold_frontend_entry_body_span_len=%d\n", stats.entry_function_body_len);
    printf("cold_frontend_entry_semantics_ok=%d\n", stats.entry_semantics_ok);
    printf("cold_frontend_entry_chain_step_count=%d\n", stats.entry_chain_step_count);
    printf("cold_frontend_entry_command_case_count=%d\n", stats.entry_command_case_count);
    printf("cold_frontend_entry_dispatch_case_count=%d\n", stats.entry_dispatch_case_count);
    printf("cold_frontend_entry_dispatch_codegen=1\n");
    printf("cold_frontend_entry_dispatch_verified=1\n");
    printf("real_backend_codegen=0\n");
    return 0;
}

static bool cold_is_reserved_unimplemented_command(const char *cmd) {
    return strcmp(cmd, "system-link-exec") == 0 ||
           strcmp(cmd, "status") == 0 ||
           strcmp(cmd, "run-host-smokes") == 0 ||
           strcmp(cmd, "run-production-regression") == 0 ||
           strcmp(cmd, "print-build-plan") == 0 ||
           strcmp(cmd, "verify-export-visibility") == 0 ||
           strcmp(cmd, "verify-export-visibility-parallel") == 0 ||
           strcmp(cmd, "release-compile") == 0;
}

static int cold_cmd_unimplemented(const char *cmd) {
    fprintf(stderr, "[cheng_cold] command is not implemented in cold prototype: %s\n", cmd);
    return 1;
}

static void cold_usage(void) {
    puts("cheng_cold");
    puts("usage:");
    puts("  cheng_cold print-contract --in:<path>");
    puts("  cheng_cold self-check --in:<path>");
    puts("  cheng_cold compile-bootstrap --in:<path> --out:<path> [--report-out:<path>]");
    puts("  cheng_cold bootstrap-bridge [--in:<path>] [--out-dir:<path>]");
    puts("  cheng_cold build-backend-driver [--in:<contract>] [--out:<path>] [--out-dir:<dir>] [--report-out:<path>] [--map-out:<path>] [--index-out:<path>]");
    puts("  reserved backend commands hard-fail until real backend codegen exists");
    puts("  cheng_cold <out> [source]");
}

/* ================================================================
 * SoA BodyIR
 * ================================================================ */
enum {
    BODY_OP_I32_CONST = 1,
    BODY_OP_LOAD_I32 = 2,
    BODY_OP_MAKE_VARIANT = 3,
    BODY_OP_TAG_LOAD = 4,
    BODY_OP_PAYLOAD_LOAD = 5,
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
};

typedef struct {
    int32_t *op_kind;
    int32_t *op_dst;
    int32_t *op_a;
    int32_t *op_b;
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
    int32_t slot_count;
    int32_t slot_cap;
    int32_t frame_size;

    int32_t *switch_tag;
    int32_t *switch_block;
    int32_t switch_count;
    int32_t switch_cap;

    Arena *arena;
} BodyIR;

static BodyIR *body_new(Arena *arena) {
    BodyIR *body = arena_alloc(arena, sizeof(BodyIR));
    body->arena = arena;
    return body;
}

static void body_ensure_slots(BodyIR *body) {
    if (body->slot_count < body->slot_cap) return;
    int32_t next = body->slot_cap ? body->slot_cap * 2 : 32;
    int32_t *kind = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *offset = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *size = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->slot_count > 0) {
        memcpy(kind, body->slot_kind, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(offset, body->slot_offset, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(size, body->slot_size, (size_t)body->slot_count * sizeof(int32_t));
    }
    body->slot_kind = kind;
    body->slot_offset = offset;
    body->slot_size = size;
    body->slot_cap = next;
}

static void body_ensure_ops(BodyIR *body) {
    if (body->op_count < body->op_cap) return;
    int32_t next = body->op_cap ? body->op_cap * 2 : 64;
    int32_t *kind = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *dst = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *a = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *b = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->op_count > 0) {
        memcpy(kind, body->op_kind, (size_t)body->op_count * sizeof(int32_t));
        memcpy(dst, body->op_dst, (size_t)body->op_count * sizeof(int32_t));
        memcpy(a, body->op_a, (size_t)body->op_count * sizeof(int32_t));
        memcpy(b, body->op_b, (size_t)body->op_count * sizeof(int32_t));
    }
    body->op_kind = kind;
    body->op_dst = dst;
    body->op_a = a;
    body->op_b = b;
    body->op_cap = next;
}

static void body_ensure_terms(BodyIR *body) {
    if (body->term_count < body->term_cap) return;
    int32_t next = body->term_cap ? body->term_cap * 2 : 16;
    int32_t *kind = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *value = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *case_start = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *case_count = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *true_block = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *false_block = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->term_count > 0) {
        memcpy(kind, body->term_kind, (size_t)body->term_count * sizeof(int32_t));
        memcpy(value, body->term_value, (size_t)body->term_count * sizeof(int32_t));
        memcpy(case_start, body->term_case_start, (size_t)body->term_count * sizeof(int32_t));
        memcpy(case_count, body->term_case_count, (size_t)body->term_count * sizeof(int32_t));
        memcpy(true_block, body->term_true_block, (size_t)body->term_count * sizeof(int32_t));
        memcpy(false_block, body->term_false_block, (size_t)body->term_count * sizeof(int32_t));
    }
    body->term_kind = kind;
    body->term_value = value;
    body->term_case_start = case_start;
    body->term_case_count = case_count;
    body->term_true_block = true_block;
    body->term_false_block = false_block;
    body->term_cap = next;
}

static void body_ensure_blocks(BodyIR *body) {
    if (body->block_count < body->block_cap) return;
    int32_t next = body->block_cap ? body->block_cap * 2 : 16;
    int32_t *op_start = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *op_count = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *term = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->block_count > 0) {
        memcpy(op_start, body->block_op_start, (size_t)body->block_count * sizeof(int32_t));
        memcpy(op_count, body->block_op_count, (size_t)body->block_count * sizeof(int32_t));
        memcpy(term, body->block_term, (size_t)body->block_count * sizeof(int32_t));
    }
    body->block_op_start = op_start;
    body->block_op_count = op_count;
    body->block_term = term;
    body->block_cap = next;
}

static void body_ensure_switches(BodyIR *body) {
    if (body->switch_count < body->switch_cap) return;
    int32_t next = body->switch_cap ? body->switch_cap * 2 : 16;
    int32_t *tag = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *block = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->switch_count > 0) {
        memcpy(tag, body->switch_tag, (size_t)body->switch_count * sizeof(int32_t));
        memcpy(block, body->switch_block, (size_t)body->switch_count * sizeof(int32_t));
    }
    body->switch_tag = tag;
    body->switch_block = block;
    body->switch_cap = next;
}

static int32_t body_slot(BodyIR *body, int32_t kind, int32_t size) {
    body_ensure_slots(body);
    int32_t align = size >= 8 ? 8 : 4;
    body->frame_size = align_i32(body->frame_size, align);
    int32_t idx = body->slot_count++;
    body->slot_kind[idx] = kind;
    body->slot_offset[idx] = body->frame_size;
    body->slot_size[idx] = size;
    body->frame_size += align_i32(size, align);
    return idx;
}

static int32_t body_op(BodyIR *body, int32_t kind, int32_t dst, int32_t a, int32_t b) {
    body_ensure_ops(body);
    int32_t idx = body->op_count++;
    body->op_kind[idx] = kind;
    body->op_dst[idx] = dst;
    body->op_a[idx] = a;
    body->op_b[idx] = b;
    return idx;
}

static int32_t body_term(BodyIR *body, int32_t kind, int32_t value,
                         int32_t case_start, int32_t case_count,
                         int32_t true_block, int32_t false_block) {
    body_ensure_terms(body);
    int32_t idx = body->term_count++;
    body->term_kind[idx] = kind;
    body->term_value[idx] = value;
    body->term_case_start[idx] = case_start;
    body->term_case_count[idx] = case_count;
    body->term_true_block[idx] = true_block;
    body->term_false_block[idx] = false_block;
    return idx;
}

static int32_t body_block(BodyIR *body) {
    body_ensure_blocks(body);
    int32_t idx = body->block_count++;
    body->block_op_start[idx] = body->op_count;
    body->block_op_count[idx] = 0;
    body->block_term[idx] = -1;
    return idx;
}

static void body_end_block(BodyIR *body, int32_t block, int32_t term) {
    if (block < 0 || block >= body->block_count) die("invalid block end");
    body->block_op_count[block] = body->op_count - body->block_op_start[block];
    body->block_term[block] = term;
}

static int32_t body_switch_case(BodyIR *body, int32_t tag, int32_t block) {
    body_ensure_switches(body);
    int32_t idx = body->switch_count++;
    body->switch_tag[idx] = tag;
    body->switch_block[idx] = block;
    return idx;
}

/* ================================================================
 * Symbol table and local table
 * ================================================================ */
typedef struct {
    Span name;
    int32_t tag;
    int32_t field_count;
} Variant;

typedef struct {
    Span name;
    Variant *variants;
    int32_t variant_count;
} TypeDef;

typedef struct {
    Span name;
    int32_t arity;
    Span ret;
} FnDef;

typedef struct {
    FnDef *functions;
    int32_t function_count;
    int32_t function_cap;
    TypeDef *types;
    int32_t type_count;
    int32_t type_cap;
    Arena *arena;
} Symbols;

typedef struct {
    Span name;
    int32_t slot;
    int32_t kind;
} Local;

typedef struct {
    Local *items;
    int32_t count;
    int32_t cap;
    Arena *arena;
} Locals;

static Symbols *symbols_new(Arena *arena) {
    Symbols *symbols = arena_alloc(arena, sizeof(Symbols));
    symbols->arena = arena;
    symbols->function_cap = 32;
    symbols->type_cap = 16;
    symbols->functions = arena_alloc(arena, (size_t)symbols->function_cap * sizeof(FnDef));
    symbols->types = arena_alloc(arena, (size_t)symbols->type_cap * sizeof(TypeDef));
    return symbols;
}

static void symbols_add_fn(Symbols *symbols, Span name, int32_t arity, Span ret) {
    if (symbols->function_count >= symbols->function_cap) {
        int32_t next = symbols->function_cap * 2;
        FnDef *fresh = arena_alloc(symbols->arena, (size_t)next * sizeof(FnDef));
        memcpy(fresh, symbols->functions, (size_t)symbols->function_count * sizeof(FnDef));
        symbols->functions = fresh;
        symbols->function_cap = next;
    }
    FnDef *fn = &symbols->functions[symbols->function_count++];
    fn->name = name;
    fn->arity = arity;
    fn->ret = ret;
}

static TypeDef *symbols_add_type(Symbols *symbols, Span name, int32_t variant_count) {
    if (symbols->type_count >= symbols->type_cap) {
        int32_t next = symbols->type_cap * 2;
        TypeDef *fresh = arena_alloc(symbols->arena, (size_t)next * sizeof(TypeDef));
        memcpy(fresh, symbols->types, (size_t)symbols->type_count * sizeof(TypeDef));
        symbols->types = fresh;
        symbols->type_cap = next;
    }
    TypeDef *type = &symbols->types[symbols->type_count++];
    type->name = name;
    type->variant_count = variant_count;
    type->variants = arena_alloc(symbols->arena, (size_t)variant_count * sizeof(Variant));
    return type;
}

static Variant *symbols_find_variant(Symbols *symbols, Span name) {
    for (int32_t ti = 0; ti < symbols->type_count; ti++) {
        TypeDef *type = &symbols->types[ti];
        for (int32_t vi = 0; vi < type->variant_count; vi++) {
            if (span_same(type->variants[vi].name, name)) return &type->variants[vi];
        }
    }
    return 0;
}

static void locals_init(Locals *locals, Arena *arena) {
    memset(locals, 0, sizeof(*locals));
    locals->arena = arena;
    locals->cap = 32;
    locals->items = arena_alloc(arena, (size_t)locals->cap * sizeof(Local));
}

static void locals_add(Locals *locals, Span name, int32_t slot, int32_t kind) {
    if (locals->count >= locals->cap) {
        int32_t next = locals->cap * 2;
        Local *fresh = arena_alloc(locals->arena, (size_t)next * sizeof(Local));
        memcpy(fresh, locals->items, (size_t)locals->count * sizeof(Local));
        locals->items = fresh;
        locals->cap = next;
    }
    locals->items[locals->count++] = (Local){name, slot, kind};
}

static Local *locals_find(Locals *locals, Span name) {
    for (int32_t i = locals->count - 1; i >= 0; i--) {
        if (span_same(locals->items[i].name, name)) return &locals->items[i];
    }
    return 0;
}

/* ================================================================
 * Parser
 * ================================================================ */
typedef struct {
    Span source;
    int32_t pos;
    Arena *arena;
    Symbols *symbols;
} Parser;

static void parser_ws(Parser *parser) {
    for (;;) {
        while (parser->pos < parser->source.len) {
            uint8_t c = parser->source.ptr[parser->pos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            parser->pos++;
        }
        if (parser->pos + 1 < parser->source.len &&
            parser->source.ptr[parser->pos] == '/' &&
            parser->source.ptr[parser->pos + 1] == '/') {
            while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
            continue;
        }
        break;
    }
}

static void parser_line(Parser *parser) {
    while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') parser->pos++;
    if (parser->pos < parser->source.len) parser->pos++;
}

static Span parser_token(Parser *parser) {
    parser_ws(parser);
    if (parser->pos >= parser->source.len) return (Span){0};
    uint8_t c = parser->source.ptr[parser->pos];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
        int32_t start = parser->pos++;
        while (parser->pos < parser->source.len) {
            c = parser->source.ptr[parser->pos];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) break;
            parser->pos++;
        }
        return span_sub(parser->source, start, parser->pos);
    }
    if ((c >= '0' && c <= '9') ||
        (c == '-' && parser->pos + 1 < parser->source.len &&
         parser->source.ptr[parser->pos + 1] >= '0' &&
         parser->source.ptr[parser->pos + 1] <= '9')) {
        int32_t start = parser->pos++;
        while (parser->pos < parser->source.len &&
               parser->source.ptr[parser->pos] >= '0' &&
               parser->source.ptr[parser->pos] <= '9') parser->pos++;
        return span_sub(parser->source, start, parser->pos);
    }
    if (c == '=' && parser->pos + 1 < parser->source.len && parser->source.ptr[parser->pos + 1] == '>') {
        int32_t start = parser->pos;
        parser->pos += 2;
        return span_sub(parser->source, start, parser->pos);
    }
    if ((c == '=' || c == '!' || c == '<' || c == '>') &&
        parser->pos + 1 < parser->source.len &&
        parser->source.ptr[parser->pos + 1] == '=') {
        int32_t start = parser->pos;
        parser->pos += 2;
        return span_sub(parser->source, start, parser->pos);
    }
    int32_t start = parser->pos++;
    return span_sub(parser->source, start, parser->pos);
}

static Span parser_peek(Parser *parser) {
    int32_t saved = parser->pos;
    Span token = parser_token(parser);
    parser->pos = saved;
    return token;
}

static bool parser_take(Parser *parser, const char *text) {
    Span token = parser_token(parser);
    return span_eq(token, text);
}

static void parser_skip_balanced(Parser *parser, const char *open, const char *close) {
    if (!parser_take(parser, open)) return;
    int32_t depth = 1;
    while (depth > 0 && parser->pos < parser->source.len) {
        Span token = parser_token(parser);
        if (span_eq(token, open)) depth++;
        else if (span_eq(token, close)) depth--;
        else if (token.len == 0) break;
    }
}

static void parse_type(Parser *parser) {
    int32_t line_start = parser->pos;
    int32_t line_end = line_start;
    while (line_end < parser->source.len && parser->source.ptr[line_end] != '\n') line_end++;
    Parser line = {span_sub(parser->source, line_start, line_end), 0, parser->arena, parser->symbols};
    if (!parser_take(&line, "type")) die("expected type");
    Span type_name = parser_token(&line);
    if (span_eq(parser_peek(&line), "[")) parser_skip_balanced(&line, "[", "]");
    if (!parser_take(&line, "=")) die("expected = in type declaration");

    int32_t saved = line.pos;
    int32_t variant_count = 0;
    while (line.pos < line.source.len) {
        Span token = parser_token(&line);
        if (token.len == 0) break;
        if (span_eq(token, "|")) continue;
        variant_count++;
        if (span_eq(parser_peek(&line), "(")) parser_skip_balanced(&line, "(", ")");
    }
    if (variant_count <= 0) die("type has no variants");

    line.pos = saved;
    TypeDef *type = symbols_add_type(parser->symbols, type_name, variant_count);
    for (int32_t vi = 0; vi < variant_count; vi++) {
        if (vi > 0 && !parser_take(&line, "|")) die("expected | between variants");
        Span variant_name = parser_token(&line);
        int32_t field_count = 0;
        if (span_eq(parser_peek(&line), "(")) {
            parser_take(&line, "(");
            while (!span_eq(parser_peek(&line), ")")) {
                Span field = parser_token(&line);
                if (field.len == 0) die("unterminated variant fields");
                if (!parser_take(&line, ":")) die("expected : in variant field");
                (void)parser_token(&line);
                field_count++;
                if (span_eq(parser_peek(&line), ",")) (void)parser_token(&line);
            }
            parser_take(&line, ")");
        }
        type->variants[vi] = (Variant){variant_name, vi, field_count};
    }
    parser->pos = line_end;
    parser_line(parser);
}

static int32_t parse_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);

static int32_t parse_constructor(Parser *parser, BodyIR *body, Locals *locals,
                                 Variant *variant) {
    int32_t payload_slot = -1;
    if (span_eq(parser_peek(parser), "(")) {
        parser_take(parser, "(");
        if (!span_eq(parser_peek(parser), ")")) {
            int32_t payload_kind = SLOT_I32;
            payload_slot = parse_expr(parser, body, locals, &payload_kind);
            if (span_eq(parser_peek(parser), ",")) die("only one payload field is supported in cold prototype");
        }
        if (!parser_take(parser, ")")) die("expected ) after constructor");
    } else if (variant->field_count > 0) {
        die("payload variant requires constructor arguments");
    }
    int32_t slot = body_slot(body, SLOT_VARIANT, 16);
    body_op(body, BODY_OP_MAKE_VARIANT, slot, variant->tag, payload_slot);
    return slot;
}

static int32_t parse_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    Span token = parser_token(parser);
    if (token.len == 0) die("expected expression");
    if (span_is_i32(token)) {
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, slot, span_i32(token), 0);
        *kind = SLOT_I32;
        return slot;
    }
    Variant *variant = symbols_find_variant(parser->symbols, token);
    if (variant) {
        *kind = SLOT_VARIANT;
        return parse_constructor(parser, body, locals, variant);
    }
    Local *local = locals_find(locals, token);
    if (!local) die("unknown identifier");
    *kind = local->kind;
    return local->slot;
}

static void parse_return(Parser *parser, BodyIR *body, Locals *locals, int32_t block) {
    int32_t kind = SLOT_I32;
    int32_t slot = parse_expr(parser, body, locals, &kind);
    if (kind != SLOT_I32) die("cold prototype can only return int32");
    body_op(body, BODY_OP_LOAD_I32, slot, 0, 0);
    int32_t term = body_term(body, BODY_TERM_RET, slot, -1, 0, -1, -1);
    body_end_block(body, block, term);
}

static void parse_let(Parser *parser, BodyIR *body, Locals *locals) {
    Span name = parser_token(parser);
    if (!parser_take(parser, "=")) die("expected = after let binding");
    int32_t kind = SLOT_I32;
    int32_t slot = parse_expr(parser, body, locals, &kind);
    locals_add(locals, name, slot, kind);
}

static void parse_match(Parser *parser, BodyIR *body, Locals *locals, int32_t block) {
    Span matched_name = parser_token(parser);
    Local *matched = locals_find(locals, matched_name);
    if (!matched || matched->kind != SLOT_VARIANT) die("match target must be a variant value");
    if (!parser_take(parser, "{")) die("expected { after match target");

    int32_t tag_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_TAG_LOAD, tag_slot, matched->slot, 0);
    int32_t case_start = body->switch_count;
    int32_t switch_term = body_term(body, BODY_TERM_SWITCH, tag_slot, case_start, 0, -1, -1);
    body_end_block(body, block, switch_term);

    int32_t case_count = 0;
    while (!span_eq(parser_peek(parser), "}")) {
        Span variant_name = parser_token(parser);
        Variant *variant = symbols_find_variant(parser->symbols, variant_name);
        if (!variant) die("unknown match variant");
        Span bind_name = {0};
        bool binds_payload = false;
        if (span_eq(parser_peek(parser), "(")) {
            parser_take(parser, "(");
            if (!span_eq(parser_peek(parser), ")")) {
                bind_name = parser_token(parser);
                binds_payload = true;
            }
            if (!parser_take(parser, ")")) die("expected ) in match arm");
        }
        if (!parser_take(parser, "=>")) die("expected => in match arm");

        int32_t arm_block = body_block(body);
        body_switch_case(body, variant->tag, arm_block);
        case_count++;
        int32_t saved_local_count = locals->count;
        if (binds_payload) {
            int32_t payload_slot = body_slot(body, SLOT_I32, 4);
            body_op(body, BODY_OP_PAYLOAD_LOAD, payload_slot, matched->slot, 8);
            locals_add(locals, bind_name, payload_slot, SLOT_I32);
        }
        if (!parser_take(parser, "return")) die("cold match arm must return");
        parse_return(parser, body, locals, arm_block);
        locals->count = saved_local_count;
    }
    parser_take(parser, "}");
    body->term_case_count[switch_term] = case_count;
}

static int32_t parse_compare_op(Parser *parser) {
    Span op = parser_token(parser);
    if (span_eq(op, "==")) return COND_EQ;
    if (span_eq(op, "!=")) return COND_NE;
    if (span_eq(op, ">=")) return COND_GE;
    if (span_eq(op, "<")) return COND_LT;
    if (span_eq(op, ">")) return COND_GT;
    if (span_eq(op, "<=")) return COND_LE;
    die("unsupported if comparison");
    return COND_EQ;
}

static void parse_return_statement(Parser *parser, BodyIR *body, Locals *locals, int32_t block) {
    if (!parser_take(parser, "return")) die("expected return");
    parse_return(parser, body, locals, block);
}

static void parse_if(Parser *parser, BodyIR *body, Locals *locals, int32_t block) {
    int32_t left_kind = SLOT_I32;
    int32_t left = parse_expr(parser, body, locals, &left_kind);
    int32_t cond = parse_compare_op(parser);
    int32_t right_kind = SLOT_I32;
    int32_t right = parse_expr(parser, body, locals, &right_kind);
    if (left_kind != SLOT_I32 || right_kind != SLOT_I32) die("if condition needs int32 operands");
    if (!parser_take(parser, ":")) die("expected : after if condition");

    int32_t term = body_term(body, BODY_TERM_CBR, left, cond, right, -1, -1);
    body_end_block(body, block, term);

    int32_t true_block = body_block(body);
    parse_return_statement(parser, body, locals, true_block);

    int32_t false_block = body_block(body);
    if (!parser_take(parser, "else")) die("cold if requires else");
    if (!parser_take(parser, ":")) die("expected : after else");
    parse_return_statement(parser, body, locals, false_block);

    body->term_true_block[term] = true_block;
    body->term_false_block[term] = false_block;
}

static BodyIR *parse_fn(Parser *parser) {
    if (!parser_take(parser, "fn")) die("expected fn");
    Span fn_name = parser_token(parser);
    if (!parser_take(parser, "(")) die("expected ( after fn name");
    int32_t arity = 0;
    while (!span_eq(parser_peek(parser), ")")) {
        Span param = parser_token(parser);
        if (param.len == 0) die("unterminated params");
        arity++;
        while (!span_eq(parser_peek(parser), ",") &&
               !span_eq(parser_peek(parser), ")")) (void)parser_token(parser);
        if (span_eq(parser_peek(parser), ",")) (void)parser_token(parser);
    }
    parser_take(parser, ")");
    Span ret = {0};
    if (parser_take(parser, ":")) ret = parser_token(parser);
    if (!parser_take(parser, "=")) die("expected = before fn body");
    symbols_add_fn(parser->symbols, fn_name, arity, ret);

    BodyIR *body = body_new(parser->arena);
    Locals locals;
    locals_init(&locals, parser->arena);
    int32_t block = body_block(body);

    while (parser->pos < parser->source.len && body->block_term[block] < 0) {
        Span kw = parser_token(parser);
        if (kw.len == 0) break;
        if (span_eq(kw, "let")) {
            parse_let(parser, body, &locals);
        } else if (span_eq(kw, "return")) {
            parse_return(parser, body, &locals, block);
        } else if (span_eq(kw, "match")) {
            parse_match(parser, body, &locals, block);
        } else if (span_eq(kw, "if")) {
            parse_if(parser, body, &locals, block);
        } else {
            die("unsupported statement in cold prototype");
        }
    }
    if (body->block_term[block] < 0) die("function body has no terminator");
    return body;
}

/* ================================================================
 * ARM64 encoder
 * ================================================================ */
enum {
    R0 = 0,
    R1 = 1,
    R2 = 2,
    R3 = 3,
    SP = 31,
    LR = 30,
    FP = 29,
};

static uint32_t a64_ret(void) { return 0xD65F03C0u; }
static uint32_t a64_movz(int rd, uint16_t value, int shift) {
    return 0x52800000u | ((uint32_t)shift << 21) | ((uint32_t)value << 5) | (uint32_t)rd;
}
static uint32_t a64_add_imm(int rd, int rn, uint16_t value, bool x) {
    return (x ? 0x91000000u : 0x11000000u) | ((uint32_t)value << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_sub_imm(int rd, int rn, uint16_t value, bool x) {
    return (x ? 0xD1000000u : 0x51000000u) | ((uint32_t)value << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_str_imm(int rt, int rn, int32_t offset, bool x) {
    int32_t scale = x ? 3 : 2;
    uint32_t imm = (uint32_t)((offset >> scale) & 0xFFF);
    return (x ? 0xF9000000u : 0xB9000000u) | (imm << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t a64_ldr_imm(int rt, int rn, int32_t offset, bool x) {
    int32_t scale = x ? 3 : 2;
    uint32_t imm = (uint32_t)((offset >> scale) & 0xFFF);
    return (x ? 0xF9400000u : 0xB9400000u) | (imm << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t a64_ldrb_imm(int rt, int rn, int32_t offset) {
    uint32_t imm = (uint32_t)(offset & 0xFFF);
    return 0x39400000u | (imm << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t a64_stp_pre(int a, int b, int n, int32_t offset) {
    uint32_t imm = (uint32_t)((offset >> 3) & 0x7F);
    return 0xA9800000u | (imm << 15) | ((uint32_t)b << 10) | ((uint32_t)n << 5) | (uint32_t)a;
}
static uint32_t a64_ldp_post(int a, int b, int n, int32_t offset) {
    uint32_t imm = (uint32_t)((offset >> 3) & 0x7F);
    return 0xA8C00000u | (imm << 15) | ((uint32_t)b << 10) | ((uint32_t)n << 5) | (uint32_t)a;
}
static uint32_t a64_cmp_imm(int rn, uint16_t value) {
    return 0x7100001Fu | ((uint32_t)value << 10) | ((uint32_t)rn << 5);
}
static uint32_t a64_cmp_reg(int rn, int rm) {
    return 0x6B00001Fu | ((uint32_t)rm << 16) | ((uint32_t)rn << 5);
}
static uint32_t a64_b(int32_t offset_words) {
    return 0x14000000u | ((uint32_t)offset_words & 0x03FFFFFFu);
}
static uint32_t a64_bcond(int32_t offset_words, int cond) {
    return 0x54000000u | (((uint32_t)offset_words & 0x7FFFFu) << 5) | ((uint32_t)cond & 0xFu);
}
static uint32_t a64_cbz(int rt, int32_t offset_words, bool nonzero) {
    return (nonzero ? 0x35000000u : 0x34000000u) |
           (((uint32_t)offset_words & 0x7FFFFu) << 5) |
           (uint32_t)rt;
}

/* ================================================================
 * Code generation
 * ================================================================ */
typedef struct {
    uint32_t *words;
    int32_t count;
    int32_t cap;
    Arena *arena;
} Code;

typedef struct {
    int32_t pos;
    int32_t target_block;
    int32_t kind;
} Patch;

typedef struct {
    Patch *items;
    int32_t count;
    int32_t cap;
    Arena *arena;
} PatchList;

static Code *code_new(Arena *arena, int32_t cap) {
    Code *code = arena_alloc(arena, sizeof(Code));
    code->words = arena_alloc(arena, (size_t)cap * sizeof(uint32_t));
    code->cap = cap;
    code->arena = arena;
    return code;
}

static void code_emit(Code *code, uint32_t word) {
    if (code->count >= code->cap) {
        int32_t next = code->cap * 2;
        uint32_t *fresh = arena_alloc(code->arena, (size_t)next * sizeof(uint32_t));
        memcpy(fresh, code->words, (size_t)code->count * sizeof(uint32_t));
        code->words = fresh;
        code->cap = next;
    }
    code->words[code->count++] = word;
}

static void patches_add(PatchList *patches, int32_t pos, int32_t target_block, int32_t kind) {
    if (patches->count >= patches->cap) {
        int32_t next = patches->cap ? patches->cap * 2 : 32;
        Patch *fresh = arena_alloc(patches->arena, (size_t)next * sizeof(Patch));
        if (patches->items) memcpy(fresh, patches->items, (size_t)patches->count * sizeof(Patch));
        patches->items = fresh;
        patches->cap = next;
    }
    patches->items[patches->count++] = (Patch){pos, target_block, kind};
}

static void emit_epilogue(Code *code, int32_t frame_size) {
    if (frame_size > 0) code_emit(code, a64_add_imm(SP, SP, (uint16_t)frame_size, true));
    code_emit(code, a64_ldp_post(FP, LR, SP, 16));
    code_emit(code, a64_ret());
}

static void codegen_op(Code *code, BodyIR *body, int32_t op) {
    int32_t kind = body->op_kind[op];
    int32_t dst = body->op_dst[op];
    int32_t a = body->op_a[op];
    int32_t b = body->op_b[op];
    if (kind == BODY_OP_I32_CONST) {
        code_emit(code, a64_movz(R0, (uint16_t)(a & 0xFFFF), 0));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_LOAD_I32) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_MAKE_VARIANT) {
        code_emit(code, a64_movz(R0, (uint16_t)a, 0));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
        if (b >= 0) {
            code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[b], false));
            code_emit(code, a64_str_imm(R1, SP, body->slot_offset[dst] + 8, false));
        }
    } else if (kind == BODY_OP_TAG_LOAD) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a], false));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_PAYLOAD_LOAD) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a] + b, false));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else {
        die("unknown BodyIR op");
    }
}

static void codegen_switch(Code *code, BodyIR *body, PatchList *patches, int32_t term) {
    int32_t tag_slot = body->term_value[term];
    int32_t start = body->term_case_start[term];
    int32_t count = body->term_case_count[term];
    code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[tag_slot], false));

    if (count == 2 &&
        ((body->switch_tag[start] == 0 && body->switch_tag[start + 1] == 1) ||
         (body->switch_tag[start] == 1 && body->switch_tag[start + 1] == 0))) {
        int32_t zero_block = body->switch_tag[start] == 0 ? body->switch_block[start] : body->switch_block[start + 1];
        int32_t one_block = body->switch_tag[start] == 1 ? body->switch_block[start] : body->switch_block[start + 1];
        int32_t cbz_pos = code->count;
        code_emit(code, a64_cbz(R0, 0, false));
        patches_add(patches, cbz_pos, zero_block, 2);
        int32_t b_pos = code->count;
        code_emit(code, a64_b(0));
        patches_add(patches, b_pos, one_block, 1);
        return;
    }

    for (int32_t i = 0; i < count; i++) {
        code_emit(code, a64_cmp_imm(R0, (uint16_t)body->switch_tag[start + i]));
        int32_t pos = code->count;
        code_emit(code, a64_bcond(0, 0));
        patches_add(patches, pos, body->switch_block[start + i], 0);
    }
}

static void codegen_func(Code *code, BodyIR *body) {
    int32_t frame_size = align_i32(body->frame_size, 16);
    code_emit(code, a64_stp_pre(FP, LR, SP, -16));
    code_emit(code, a64_add_imm(FP, SP, 0, true));
    if (frame_size > 0) code_emit(code, a64_sub_imm(SP, SP, (uint16_t)frame_size, true));

    int32_t *block_pos = arena_alloc(body->arena, (size_t)body->block_count * sizeof(int32_t));
    PatchList patches = {0};
    patches.arena = body->arena;

    for (int32_t block = 0; block < body->block_count; block++) {
        block_pos[block] = code->count;
        int32_t start = body->block_op_start[block];
        int32_t end = start + body->block_op_count[block];
        for (int32_t op = start; op < end; op++) codegen_op(code, body, op);

        int32_t term = body->block_term[block];
        if (term < 0) continue;
        int32_t kind = body->term_kind[term];
        if (kind == BODY_TERM_RET) {
            code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[body->term_value[term]], false));
            emit_epilogue(code, frame_size);
        } else if (kind == BODY_TERM_BR) {
            int32_t pos = code->count;
            code_emit(code, a64_b(0));
            patches_add(&patches, pos, body->term_true_block[term], 1);
        } else if (kind == BODY_TERM_CBR) {
            int32_t left = body->term_value[term];
            int32_t cond = body->term_case_start[term];
            int32_t right = body->term_case_count[term];
            code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[left], false));
            code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[right], false));
            code_emit(code, a64_cmp_reg(R0, R1));
            int32_t true_pos = code->count;
            code_emit(code, a64_bcond(0, cond));
            patches_add(&patches, true_pos, body->term_true_block[term], 0);
            int32_t false_pos = code->count;
            code_emit(code, a64_b(0));
            patches_add(&patches, false_pos, body->term_false_block[term], 1);
        } else if (kind == BODY_TERM_SWITCH) {
            codegen_switch(code, body, &patches, term);
        } else {
            die("unsupported terminator");
        }
    }

    for (int32_t i = 0; i < patches.count; i++) {
        Patch patch = patches.items[i];
        int32_t target = patch.target_block >= 0 && patch.target_block < body->block_count
                             ? block_pos[patch.target_block]
                             : code->count;
        int32_t delta = target - patch.pos;
        uint32_t ins = code->words[patch.pos];
        if (patch.kind == 0) {
            code->words[patch.pos] = (ins & 0xFF00001Fu) | (((uint32_t)delta & 0x7FFFFu) << 5);
        } else if (patch.kind == 2) {
            code->words[patch.pos] = (ins & 0xFF00001Fu) | (((uint32_t)delta & 0x7FFFFu) << 5);
        } else {
            code->words[patch.pos] = (ins & 0xFC000000u) | ((uint32_t)delta & 0x03FFFFFFu);
        }
    }
}

static void code_patch_bcond(Code *code, int32_t pos, int32_t target) {
    int32_t delta = target - pos;
    uint32_t ins = code->words[pos];
    code->words[pos] = (ins & 0xFF00001Fu) | (((uint32_t)delta & 0x7FFFFu) << 5);
}

static void code_emit_return_i32(Code *code, int32_t value) {
    code_emit(code, a64_movz(R0, (uint16_t)(value & 0xFFFF), 0));
    code_emit(code, a64_ret());
}

static int32_t cold_entry_dispatch_exit_code(int32_t command_code) {
    if (command_code == 0 || command_code == 1 || command_code == 2) return 0;
    return 2;
}

static void cold_emit_argv_string_case(Code *code, const char *text, int32_t exit_code) {
    int32_t fail_patches[96];
    int32_t fail_count = 0;
    int32_t len = (int32_t)strlen(text);
    if (len + 1 > (int32_t)(sizeof(fail_patches) / sizeof(fail_patches[0]))) {
        die("cold entry dispatch command too long");
    }
    for (int32_t i = 0; i <= len; i++) {
        int32_t expected = i < len ? (uint8_t)text[i] : 0;
        code_emit(code, a64_ldrb_imm(R3, R2, i));
        code_emit(code, a64_cmp_imm(R3, (uint16_t)expected));
        fail_patches[fail_count++] = code->count;
        code_emit(code, a64_bcond(0, COND_NE));
    }
    code_emit_return_i32(code, exit_code);
    int32_t fail_target = code->count;
    for (int32_t i = 0; i < fail_count; i++) {
        code_patch_bcond(code, fail_patches[i], fail_target);
    }
}

static bool cold_write_entry_dispatch_executable(const char *path, ColdManifestStats *stats) {
    Arena *arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (arena == MAP_FAILED) return false;
    Code *code = code_new(arena, 1024);

    code_emit(code, a64_cmp_imm(R0, 2));
    int32_t has_arg_patch = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit_return_i32(code, 0);
    code_patch_bcond(code, has_arg_patch, code->count);

    code_emit(code, a64_ldr_imm(R2, R1, 8, true));
    for (int32_t i = 0; i < stats->entry_command_case_count; i++) {
        int32_t exit_code = cold_entry_dispatch_exit_code(stats->entry_command_cases[i].code);
        cold_emit_argv_string_case(code, stats->entry_command_cases[i].text, exit_code);
    }
    code_emit_return_i32(code, 2);
    return macho_write_exec(path, code->words, code->count);
}

static bool cold_run_executable_expect_rc(const char *path, const char *arg, int32_t expected_rc) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        if (arg) execl(path, path, arg, (char *)0);
        else execl(path, path, (char *)0);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == expected_rc;
}

static bool cold_verify_entry_dispatch_executable(const char *path, ColdManifestStats *stats) {
    if (!cold_run_executable_expect_rc(path, 0, 0)) return false;
    for (int32_t i = 0; i < stats->entry_command_case_count; i++) {
        int32_t exit_code = cold_entry_dispatch_exit_code(stats->entry_command_cases[i].code);
        if (!cold_run_executable_expect_rc(path, stats->entry_command_cases[i].text, exit_code)) return false;
    }
    if (!cold_run_executable_expect_rc(path, "__cheng_cold_unknown_command__", 2)) return false;
    return true;
}

/* ================================================================
 * Output
 * ================================================================ */
static int output_direct_macho(const char *path, Code *code) {
    return macho_write_exec(path, code->words, code->count) ? 0 : 1;
}

/* ================================================================
 * Main
 * ================================================================ */
int main(int argc, char **argv) {
    if (argc >= 2 && (strcmp(argv[1], "help") == 0 ||
                      strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "-h") == 0)) {
        cold_usage();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "print-contract") == 0) {
        return cold_cmd_print_contract(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "self-check") == 0) {
        return cold_cmd_self_check(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "compile-bootstrap") == 0) {
        return cold_cmd_compile_bootstrap(argc, argv, argv[0]);
    }
    if (argc >= 2 && strcmp(argv[1], "bootstrap-bridge") == 0) {
        return cold_cmd_bootstrap_bridge(argc, argv, argv[0]);
    }
    if (argc >= 2 && strcmp(argv[1], "build-backend-driver") == 0) {
        return cold_cmd_build_backend_driver(argc, argv, argv[0]);
    }
    if (argc >= 2 && cold_is_reserved_unimplemented_command(argv[1])) {
        return cold_cmd_unimplemented(argv[1]);
    }
    const char *out_path = argc > 1 ? argv[1] : "/tmp/cheng_cold_out";
    const char *src_path = argc > 2 ? argv[2] : 0;

    Arena *arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (arena == MAP_FAILED) die("arena root mmap failed");

    Symbols *symbols = symbols_new(arena);
    BodyIR *main_body = 0;

    if (src_path) {
        Span source = source_open(src_path);
        if (source.len <= 0) die("cannot read source");
        Parser parser = {source, 0, arena, symbols};
        while (parser.pos < source.len) {
            parser_ws(&parser);
            if (parser.pos >= source.len) break;
            Span next = parser_peek(&parser);
            if (span_eq(next, "type")) {
                parse_type(&parser);
            } else if (span_eq(next, "fn")) {
                BodyIR *body = parse_fn(&parser);
                FnDef *fn = &symbols->functions[symbols->function_count - 1];
                if (span_eq(fn->name, "main") || !main_body) main_body = body;
            } else {
                parser_line(&parser);
            }
        }
        munmap((void *)source.ptr, (size_t)source.len);
    }

    if (!main_body) {
        main_body = body_new(arena);
        int32_t block = body_block(main_body);
        int32_t slot = body_slot(main_body, SLOT_I32, 4);
        body_op(main_body, BODY_OP_I32_CONST, slot, 42, 0);
        body_op(main_body, BODY_OP_LOAD_I32, slot, 0, 0);
        int32_t term = body_term(main_body, BODY_TERM_RET, slot, -1, 0, -1, -1);
        body_end_block(main_body, block, term);
    }

    Code *code = code_new(arena, 256);
    codegen_func(code, main_body);
    int rc = output_direct_macho(out_path, code);
    printf("cheng_cold: %s src=%s fns=%d types=%d ops=%d blocks=%d switches=%d code=%dw arena=%zuKB\n",
           rc ? "FAIL" : "OK",
           src_path ? src_path : "(demo)",
           symbols->function_count,
           symbols->type_count,
           main_body->op_count,
           main_body->block_count,
           main_body->switch_count,
           code->count,
           arena->used / 1024);
    return rc;
}
