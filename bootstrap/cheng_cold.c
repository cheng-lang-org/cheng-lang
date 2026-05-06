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

#include <ctype.h>
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
#include <time.h>
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

#define COLD_MAX_VARIANT_FIELDS 8
#define COLD_MAX_OBJECT_FIELDS 16

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
    if (s.len <= 0) return (Span){0};
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
    "system-link-exec",
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

static int32_t cold_span_find_text(Span span, const char *needle) {
    int32_t n = (int32_t)strlen(needle);
    if (n <= 0 || span.len < n) return -1;
    for (int32_t i = 0; i <= span.len - n; i++) {
        if (memcmp(span.ptr + i, needle, (size_t)n) == 0) return i;
    }
    return -1;
}

static int32_t cold_span_find_char(Span span, char needle) {
    for (int32_t i = 0; i < span.len; i++) {
        if (span.ptr[i] == (uint8_t)needle) return i;
    }
    return -1;
}

static Span cold_span_strip_trailing_colon(Span span) {
    Span out = span_trim(span);
    if (out.len > 0 && out.ptr[out.len - 1] == ':') out.len--;
    return span_trim(out);
}

static int32_t cold_line_indent_width(Span line) {
    int32_t indent = 0;
    for (int32_t i = 0; i < line.len; i++) {
        if (line.ptr[i] == ' ') indent++;
        else if (line.ptr[i] == '\t') indent += 4;
        else break;
    }
    return indent;
}

static void cold_write_span(FILE *file, Span span) {
    if (span.len > 0) fwrite(span.ptr, 1, (size_t)span.len, file);
}

static char cold_field_kind_code_from_type(Span type);
static char cold_param_kind_code_from_type(Span type);
static bool cold_parse_i32_array_type(Span type, int32_t *len_out);
static bool cold_parse_i32_seq_type(Span type);
static void cold_write_param_specs(FILE *file, Span params) {
    int32_t pos = 0;
    int32_t count = 0;
    while (pos < params.len) {
        while (pos < params.len && (params.ptr[pos] == ' ' || params.ptr[pos] == '\t' || params.ptr[pos] == ',')) pos++;
        int32_t start = pos;
        while (pos < params.len) {
            uint8_t c = params.ptr[pos];
            if (!(isalnum(c) || c == '_')) break;
            pos++;
        }
        if (pos > start) {
            Span name = span_sub(params, start, pos);
            char kind_code = 'i';
            Span param_type = {0};
            while (pos < params.len && (params.ptr[pos] == ' ' || params.ptr[pos] == '\t')) pos++;
            if (pos < params.len && params.ptr[pos] == ':') {
                pos++;
                while (pos < params.len && (params.ptr[pos] == ' ' || params.ptr[pos] == '\t')) pos++;
                int32_t type_start = pos;
                int32_t square_depth = 0;
                while (pos < params.len) {
                    uint8_t c = params.ptr[pos];
                    if (c == '[') square_depth++;
                    else if (c == ']') {
                        if (square_depth <= 0) break;
                        square_depth--;
                    } else if (c == ',' && square_depth == 0) {
                        break;
                    }
                    pos++;
                }
                if (pos > type_start) {
                    param_type = span_trim(span_sub(params, type_start, pos));
                    kind_code = cold_param_kind_code_from_type(param_type);
                }
            }
            if (count > 0) fputc(',', file);
            cold_write_span(file, name);
            if ((kind_code == 'v' || kind_code == 'q') && param_type.len > 0 && !span_eq(param_type, "v")) {
                fputc(':', file);
                cold_write_span(file, param_type);
            } else {
                fprintf(file, ":%c", kind_code);
            }
            count++;
        }
        while (pos < params.len && params.ptr[pos] != ',') pos++;
    }
}

static Span cold_binding_name_from_head(Span head) {
    head = span_trim(head);
    int32_t end = 0;
    while (end < head.len) {
        uint8_t c = head.ptr[end];
        if (!(isalnum(c) || c == '_')) break;
        end++;
    }
    return span_sub(head, 0, end);
}

static char cold_field_kind_code_from_type(Span type) {
    type = span_trim(type);
    if (span_eq(type, "str") || span_eq(type, "cstring")) return 's';
    if (cold_parse_i32_seq_type(type)) die("cold ADT payload field cannot be dynamic int32[] yet");
    if (type.len > 1 && type.ptr[0] >= 'A' && type.ptr[0] <= 'Z') return 'v';
    return 'i';
}

static char cold_param_kind_code_from_type(Span type) {
    type = span_trim(type);
    if (span_eq(type, "str") || span_eq(type, "cstring")) return 's';
    if (span_eq(type, "int32") || span_eq(type, "int")) return 'i';
    if (cold_parse_i32_seq_type(type)) return 'q';
    if (type.len > 1 && type.ptr[0] >= 'A' && type.ptr[0] <= 'Z') return 'v';
    die("unsupported cold parameter type");
    return 'i';
}

static bool cold_skip_balanced_span(Span span, int32_t *pos, char open, char close) {
    if (*pos >= span.len || span.ptr[*pos] != (uint8_t)open) return false;
    int32_t depth = 1;
    (*pos)++;
    while (*pos < span.len && depth > 0) {
        uint8_t c = span.ptr[*pos];
        if (c == (uint8_t)open) depth++;
        else if (c == (uint8_t)close) depth--;
        (*pos)++;
    }
    return depth == 0;
}

static void cold_skip_type_ws(Span span, int32_t *pos) {
    while (*pos < span.len) {
        uint8_t c = span.ptr[*pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        (*pos)++;
    }
}

static bool cold_emit_csg_variant_specs(FILE *file, Span variants) {
    int32_t pos = 0;
    int32_t emitted = 0;
    while (pos < variants.len) {
        cold_skip_type_ws(variants, &pos);
        if (pos < variants.len && variants.ptr[pos] == '|') {
            pos++;
            continue;
        }
        Span variant_name = {0};
        if (!cold_parse_ident_at(variants, &pos, &variant_name)) return false;
        int32_t field_count = 0;
        char field_kinds[COLD_MAX_VARIANT_FIELDS];
        cold_skip_type_ws(variants, &pos);
        if (pos < variants.len && variants.ptr[pos] == '(') {
            pos++;
            cold_skip_type_ws(variants, &pos);
            while (pos < variants.len && variants.ptr[pos] != ')') {
                if (field_count >= COLD_MAX_VARIANT_FIELDS) return false;
                Span field_name = {0};
                if (!cold_parse_ident_at(variants, &pos, &field_name)) return false;
                cold_skip_type_ws(variants, &pos);
                if (pos >= variants.len || variants.ptr[pos] != ':') return false;
                pos++;
                int32_t type_start = pos;
                int32_t depth_square = 0;
                int32_t depth_paren = 0;
                while (pos < variants.len) {
                    uint8_t c = variants.ptr[pos];
                    if (c == '[') depth_square++;
                    else if (c == ']') {
                        if (depth_square <= 0) return false;
                        depth_square--;
                    } else if (c == '(') {
                        depth_paren++;
                    } else if (c == ')') {
                        if (depth_paren == 0 && depth_square == 0) break;
                        if (depth_paren <= 0) return false;
                        depth_paren--;
                    } else if (c == ',' && depth_square == 0 && depth_paren == 0) {
                        break;
                    }
                    pos++;
                }
                Span field_type = span_trim(span_sub(variants, type_start, pos));
                if (field_type.len <= 0) return false;
                field_kinds[field_count++] = cold_field_kind_code_from_type(field_type);
                cold_skip_type_ws(variants, &pos);
                if (pos < variants.len && variants.ptr[pos] == ',') {
                    pos++;
                    cold_skip_type_ws(variants, &pos);
                    continue;
                }
                if (pos >= variants.len || variants.ptr[pos] != ')') return false;
            }
            if (pos >= variants.len || variants.ptr[pos] != ')') return false;
            pos++;
        }
        if (emitted > 0) fputc(',', file);
        cold_write_span(file, variant_name);
        fprintf(file, ":%d", field_count);
        if (field_count > 0) {
            fputc(':', file);
            for (int32_t i = 0; i < field_count; i++) fputc(field_kinds[i], file);
        }
        emitted++;
        cold_skip_type_ws(variants, &pos);
        if (pos < variants.len) {
            if (variants.ptr[pos] != '|') return false;
            pos++;
        }
    }
    return emitted > 0;
}

static bool cold_write_object_field_spec(FILE *file, Span type) {
    type = span_trim(type);
    int32_t array_len = 0;
    if (cold_parse_i32_array_type(type, &array_len)) {
        fprintf(file, "a%d", array_len);
        return true;
    }
    if (cold_parse_i32_seq_type(type)) return false;
    char kind = cold_field_kind_code_from_type(type);
    fputc(kind, file);
    return true;
}

static bool cold_emit_csg_field_decl_spec(FILE *file, Span decl, int32_t *emitted) {
    decl = span_trim(decl);
    if (decl.len <= 0) return true;
    int32_t colon = cold_span_find_char(decl, ':');
    if (colon <= 0) return false;
    Span name = span_trim(span_sub(decl, 0, colon));
    Span type = span_trim(span_sub(decl, colon + 1, decl.len));
    int32_t eq = cold_span_find_char(type, '=');
    if (eq >= 0) type = span_trim(span_sub(type, 0, eq));
    if (name.len <= 0 || type.len <= 0) return false;
    if (*emitted > 0) fputc(',', file);
    cold_write_span(file, name);
    fputc(':', file);
    if (!cold_write_object_field_spec(file, type)) return false;
    (*emitted)++;
    return true;
}

static bool cold_emit_csg_object_specs(FILE *file, Span fields) {
    int32_t pos = 0;
    int32_t emitted = 0;
    while (pos < fields.len) {
        int32_t start = pos;
        while (pos < fields.len && fields.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < fields.len) pos++;
        Span line = span_trim(span_sub(fields, start, end));
        if (line.len <= 0) continue;
        if (!cold_emit_csg_field_decl_spec(file, line, &emitted)) return false;
    }
    return emitted > 0;
}

static bool cold_emit_csg_tuple_specs(FILE *file, Span fields) {
    int32_t pos = 0;
    int32_t start = 0;
    int32_t emitted = 0;
    int32_t depth_square = 0;
    int32_t depth_paren = 0;
    for (; pos <= fields.len; pos++) {
        uint8_t c = pos < fields.len ? fields.ptr[pos] : ',';
        if (pos < fields.len) {
            if (c == '[') depth_square++;
            else if (c == ']') {
                if (depth_square <= 0) return false;
                depth_square--;
            } else if (c == '(') depth_paren++;
            else if (c == ')') {
                if (depth_paren <= 0) return false;
                depth_paren--;
            }
        }
        if (pos < fields.len && !(c == ',' || c == ';')) continue;
        if (depth_square != 0 || depth_paren != 0) continue;
        Span part = span_trim(span_sub(fields, start, pos));
        if (part.len > 0 && !cold_emit_csg_field_decl_spec(file, part, &emitted)) return false;
        start = pos + 1;
    }
    return emitted > 0;
}

static bool cold_parse_type_entry_parts(Span trimmed, Span *type_name, Span *rhs) {
    int32_t p = 0;
    if (!cold_parse_ident_at(trimmed, &p, type_name)) return false;
    cold_skip_inline_ws(trimmed, &p);
    if (p < trimmed.len && trimmed.ptr[p] == '[') {
        if (!cold_skip_balanced_span(trimmed, &p, '[', ']')) return false;
        cold_skip_inline_ws(trimmed, &p);
    }
    if (p >= trimmed.len || trimmed.ptr[p] != '=') return false;
    p++;
    *rhs = span_trim(span_sub(trimmed, p, trimmed.len));
    return rhs->len > 0;
}

static bool cold_emit_csg_tuple_object_row(FILE *file, Span type_name, Span rhs) {
    int32_t open = cold_span_find_char(rhs, '[');
    if (open <= 0 || rhs.len <= open + 1 || rhs.ptr[rhs.len - 1] != ']') return false;
    Span fields = span_trim(span_sub(rhs, open + 1, rhs.len - 1));
    fprintf(file, "cold_csg_object\t");
    cold_write_span(file, type_name);
    fputc('\t', file);
    if (!cold_emit_csg_tuple_specs(file, fields)) return false;
    fputc('\n', file);
    return true;
}

static bool cold_emit_csg_type_rows(FILE *file, Span source) {
    int32_t pos = 0;
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
        Span trimmed = span_trim(line);
        if (!cold_span_is_exact_or_prefix_space(trimmed, "type")) continue;
        if (span_eq(trimmed, "type")) {
            int32_t scan = pos;
            int32_t block_indent = -1;
            while (scan < source.len) {
                int32_t entry_start = scan;
                while (scan < source.len && source.ptr[scan] != '\n') scan++;
                int32_t entry_end = scan;
                if (scan < source.len) scan++;
                Span entry_line = span_sub(source, entry_start, entry_end);
                Span entry_trimmed = span_trim(entry_line);
                if (entry_trimmed.len <= 0) continue;
                if (cold_line_top_level(entry_line)) {
                    scan = entry_start;
                    break;
                }
                int32_t entry_indent = cold_line_indent_width(entry_line);
                if (block_indent < 0) block_indent = entry_indent;
                if (entry_indent < block_indent) {
                    scan = entry_start;
                    break;
                }
                if (entry_indent > block_indent) return false;
                Span entry_name = {0};
                Span rhs = {0};
                if (!cold_parse_type_entry_parts(entry_trimmed, &entry_name, &rhs)) return false;
                if (cold_span_starts_with(rhs, "object")) {
                    int32_t field_start = scan;
                    int32_t field_scan = scan;
                    int32_t field_end = scan;
                    while (field_scan < source.len) {
                        int32_t fs = field_scan;
                        while (field_scan < source.len && source.ptr[field_scan] != '\n') field_scan++;
                        int32_t fe = field_scan;
                        if (field_scan < source.len) field_scan++;
                        Span field_line = span_sub(source, fs, fe);
                        Span field_trimmed = span_trim(field_line);
                        if (field_trimmed.len <= 0) continue;
                        if (cold_line_top_level(field_line) ||
                            cold_line_indent_width(field_line) <= entry_indent) {
                            field_scan = fs;
                            break;
                        }
                        field_end = fe;
                    }
                    fprintf(file, "cold_csg_object\t");
                    cold_write_span(file, entry_name);
                    fputc('\t', file);
                    if (!cold_emit_csg_object_specs(file, span_trim(span_sub(source, field_start, field_end)))) return false;
                    fputc('\n', file);
                    scan = field_scan;
                    continue;
                }
                if (cold_span_starts_with(rhs, "tuple[")) {
                    if (!cold_emit_csg_tuple_object_row(file, entry_name, rhs)) return false;
                    continue;
                }
                fprintf(file, "cold_csg_type\t");
                cold_write_span(file, entry_name);
                fputc('\t', file);
                if (!cold_emit_csg_variant_specs(file, rhs)) return false;
                fputc('\n', file);
            }
            pos = scan;
            continue;
        }
        int32_t p = 0;
        Span keyword = {0};
        Span type_name = {0};
        if (!cold_parse_ident_at(trimmed, &p, &keyword) || !span_eq(keyword, "type")) return false;
        cold_skip_inline_ws(trimmed, &p);
        if (!cold_parse_ident_at(trimmed, &p, &type_name)) return false;
        cold_skip_inline_ws(trimmed, &p);
        if (p < trimmed.len && trimmed.ptr[p] == '[') {
            if (!cold_skip_balanced_span(trimmed, &p, '[', ']')) return false;
            cold_skip_inline_ws(trimmed, &p);
        }
        if (p >= trimmed.len || trimmed.ptr[p] != '=') return false;
        p++;
        Span variants = span_trim(span_sub(trimmed, p, trimmed.len));
        if (cold_span_starts_with(variants, "object")) {
            int32_t field_start = pos;
            int32_t scan = pos;
            int32_t field_end = pos;
            while (scan < source.len) {
                int32_t ls = scan;
                while (scan < source.len && source.ptr[scan] != '\n') scan++;
                int32_t le = scan;
                if (scan < source.len) scan++;
                Span field_line = span_sub(source, ls, le);
                Span field_trimmed = span_trim(field_line);
                if (field_trimmed.len <= 0) continue;
                if (cold_line_top_level(field_line)) {
                    scan = ls;
                    break;
                }
                field_end = le;
            }
            fprintf(file, "cold_csg_object\t");
            cold_write_span(file, type_name);
            fputc('\t', file);
            if (!cold_emit_csg_object_specs(file, span_trim(span_sub(source, field_start, field_end)))) return false;
            fputc('\n', file);
            pos = scan;
            continue;
        }
        if (cold_span_starts_with(variants, "tuple[")) {
            if (!cold_emit_csg_tuple_object_row(file, type_name, variants)) return false;
            continue;
        }
        if (variants.len <= 0) {
            int32_t variant_start = pos;
            int32_t scan = pos;
            int32_t variant_end = pos;
            while (scan < source.len) {
                int32_t ls = scan;
                while (scan < source.len && source.ptr[scan] != '\n') scan++;
                int32_t le = scan;
                if (scan < source.len) scan++;
                Span variant_line = span_sub(source, ls, le);
                Span variant_trimmed = span_trim(variant_line);
                if (variant_trimmed.len <= 0) continue;
                if (cold_line_top_level(variant_line)) {
                    scan = ls;
                    break;
                }
                variant_end = le;
            }
            variants = span_trim(span_sub(source, variant_start, variant_end));
            pos = scan;
        }
        fprintf(file, "cold_csg_type\t");
        cold_write_span(file, type_name);
        fputc('\t', file);
        if (!cold_emit_csg_variant_specs(file, variants)) return false;
        fputc('\n', file);
    }
    return true;
}

static bool cold_emit_csg_function_rows(FILE *file, Span source) {
    int32_t pos = 0;
    int32_t line_no = 0;
    int32_t fn_index = 0;
    bool in_triple = false;
    while (pos < source.len) {
        int32_t start = pos;
        while (pos < source.len && source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < source.len) pos++;
        line_no++;
        Span line = span_sub(source, start, end);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        Span trimmed = span_trim(line);
        if (!cold_span_starts_with(trimmed, "fn ")) continue;
        ColdFunctionSymbol symbol;
        if (!cold_parse_function_symbol_at(source, start, line_no, &symbol)) return false;
        fprintf(file, "cold_csg_function\t%d\t", fn_index++);
        cold_write_span(file, symbol.name);
        fputc('\t', file);
        cold_write_param_specs(file, symbol.params);
        fputc('\t', file);
        cold_write_span(file, symbol.return_type);
        fputc('\n', file);
    }
    return fn_index > 0;
}

static bool cold_emit_csg_statement_row(FILE *file, int32_t fn_index, int32_t indent, Span trimmed) {
    if (span_eq(trimmed, "}") || span_eq(trimmed, "{")) {
        return true;
    }
    int32_t arrow = cold_span_find_text(trimmed, "=>");
    if (arrow > 0) {
        Span arm = span_trim(span_sub(trimmed, 0, arrow));
        Span body = span_trim(span_sub(trimmed, arrow + 2, trimmed.len));
        int32_t open = cold_span_find_char(arm, '(');
        Span variant = arm;
        Span bind = {0};
        if (open > 0) {
            if (arm.len <= open + 1 || arm.ptr[arm.len - 1] != ')') return false;
            variant = span_trim(span_sub(arm, 0, open));
            bind = span_trim(span_sub(arm, open + 1, arm.len - 1));
        }
        fprintf(file, "cold_csg_stmt\t%d\t%d\tcase\t", fn_index, indent);
        cold_write_span(file, variant);
        fputc('\t', file);
        cold_write_span(file, bind);
        fputc('\n', file);
        if (body.len > 0) {
            return cold_emit_csg_statement_row(file, fn_index, indent + 4, body);
        }
        return true;
    }
    if (cold_span_starts_with(trimmed, "match ")) {
        Span rest = span_trim(span_sub(trimmed, 6, trimmed.len));
        if (rest.len > 0 && rest.ptr[rest.len - 1] == '{') {
            rest = span_trim(span_sub(rest, 0, rest.len - 1));
        }
        int32_t colon = cold_span_find_char(rest, ':');
        Span target;
        if (colon > 0) {
            target = span_trim(span_sub(rest, 0, colon));
            Span after = span_trim(span_sub(rest, colon + 1, rest.len));
            if (after.len > 0) {
                fprintf(file, "cold_csg_stmt\t%d\t%d\tmatch\t", fn_index, indent);
                cold_write_span(file, target);
                fputc('\n', file);
                return cold_emit_csg_statement_row(file, fn_index, indent + 4, after);
            }
        } else {
            target = span_trim(rest);
        }
        fprintf(file, "cold_csg_stmt\t%d\t%d\tmatch\t", fn_index, indent);
        cold_write_span(file, target);
        fputc('\n', file);
        return true;
    }
    if (cold_span_starts_with(trimmed, "return ")) {
        Span expr = span_trim(span_sub(trimmed, 7, trimmed.len));
        if (expr.len > 1 && expr.ptr[expr.len - 1] == '?') {
            Span inner = span_sub(expr, 0, expr.len - 1);
            int32_t depth = 0;
            for (int32_t i = 0; i < inner.len; i++) {
                if (inner.ptr[i] == '(') depth++;
                else if (inner.ptr[i] == ')') depth--;
            }
            if (depth == 0) {
                return false;
            }
        }
        fprintf(file, "cold_csg_stmt\t%d\t%d\treturn\t", fn_index, indent);
        cold_write_span(file, expr);
        fputc('\n', file);
        return true;
    }
    if (cold_span_starts_with(trimmed, "let ") || cold_span_starts_with(trimmed, "var ")) {
        Span rest = span_trim(span_sub(trimmed, 4, trimmed.len));
        int32_t eq = cold_span_find_char(rest, '=');
        if (eq <= 0) {
            int32_t colon = cold_span_find_char(rest, ':');
            if (colon <= 0) return false;
            Span name = cold_binding_name_from_head(span_sub(rest, 0, colon));
            Span type = span_trim(span_sub(rest, colon + 1, rest.len));
            if (name.len <= 0 || type.len <= 0) return false;
            fprintf(file, "cold_csg_stmt\t%d\t%d\tdefault\t", fn_index, indent);
            cold_write_span(file, name);
            fputc('\t', file);
            cold_write_span(file, type);
            fputc('\n', file);
            return true;
        }
        Span head = span_trim(span_sub(rest, 0, eq));
        Span type = {0};
        int32_t colon = cold_span_find_char(head, ':');
        Span name = colon > 0 ? cold_binding_name_from_head(span_sub(head, 0, colon))
                              : cold_binding_name_from_head(head);
        if (colon > 0) type = span_trim(span_sub(head, colon + 1, head.len));
        Span expr = span_trim(span_sub(rest, eq + 1, rest.len));
        bool has_question = false;
        if (expr.len > 1 && expr.ptr[expr.len - 1] == '?') {
            Span inner = span_sub(expr, 0, expr.len - 1);
            int32_t depth = 0;
            for (int32_t i = 0; i < inner.len; i++) {
                if (inner.ptr[i] == '(') depth++;
                else if (inner.ptr[i] == ')') depth--;
            }
            if (depth == 0) {
                has_question = true;
                expr = span_trim(inner);
            }
        }
        if (type.len > 0) {
            fprintf(file, "cold_csg_stmt\t%d\t%d\t%s\t", fn_index, indent, has_question ? "let_q_t" : "var_t");
            cold_write_span(file, name);
            fputc('\t', file);
            cold_write_span(file, type);
            fputc('\t', file);
            cold_write_span(file, expr);
            fputc('\n', file);
        } else {
            fprintf(file, "cold_csg_stmt\t%d\t%d\t%s\t", fn_index, indent, has_question ? "let_q" : "var");
            cold_write_span(file, name);
            fputc('\t', file);
            cold_write_span(file, expr);
            fputc('\n', file);
        }
        return true;
    }
    if (cold_span_starts_with(trimmed, "if ")) {
        Span condition = cold_span_strip_trailing_colon(span_sub(trimmed, 3, trimmed.len));
        fprintf(file, "cold_csg_stmt\t%d\t%d\tif\t", fn_index, indent);
        cold_write_span(file, condition);
        fputc('\n', file);
        return true;
    }
    if (cold_span_starts_with(trimmed, "elif ")) {
        Span condition = cold_span_strip_trailing_colon(span_sub(trimmed, 5, trimmed.len));
        fprintf(file, "cold_csg_stmt\t%d\t%d\telif\t", fn_index, indent);
        cold_write_span(file, condition);
        fputc('\n', file);
        return true;
    }
    if (span_eq(cold_span_strip_trailing_colon(trimmed), "else")) {
        fprintf(file, "cold_csg_stmt\t%d\t%d\telse\n", fn_index, indent);
        return true;
    }
    if (cold_span_starts_with(trimmed, "while ")) {
        Span condition = cold_span_strip_trailing_colon(span_sub(trimmed, 6, trimmed.len));
        fprintf(file, "cold_csg_stmt\t%d\t%d\twhile\t", fn_index, indent);
        cold_write_span(file, condition);
        fputc('\n', file);
        return true;
    }
    if (cold_span_starts_with(trimmed, "for ")) {
        Span payload = cold_span_strip_trailing_colon(span_sub(trimmed, 4, trimmed.len));
        int32_t in_pos = cold_span_find_text(payload, " in ");
        if (in_pos <= 0) return false;
        Span iter = span_trim(span_sub(payload, 0, in_pos));
        Span range = span_trim(span_sub(payload, in_pos + 4, payload.len));
        int32_t op = cold_span_find_text(range, "..<");
        const char *mode = "lt";
        int32_t op_len = 3;
        if (op < 0) {
            op = cold_span_find_text(range, "..");
            mode = "le";
            op_len = 2;
        }
        if (op <= 0) return false;
        Span start = span_trim(span_sub(range, 0, op));
        Span end = span_trim(span_sub(range, op + op_len, range.len));
        fprintf(file, "cold_csg_stmt\t%d\t%d\tfor_range\t", fn_index, indent);
        cold_write_span(file, iter);
        fputc('\t', file);
        cold_write_span(file, start);
        fputc('\t', file);
        cold_write_span(file, end);
        fprintf(file, "\t%s\n", mode);
        return true;
    }
    if (span_eq(trimmed, "break")) {
        fprintf(file, "cold_csg_stmt\t%d\t%d\tbreak\n", fn_index, indent);
        return true;
    }
    if (span_eq(trimmed, "continue")) {
        fprintf(file, "cold_csg_stmt\t%d\t%d\tcontinue\n", fn_index, indent);
        return true;
    }
    int32_t eq = cold_span_find_char(trimmed, '=');
    if (eq > 0 &&
        !(eq + 1 < trimmed.len && trimmed.ptr[eq + 1] == '=') &&
        !(eq > 0 && (trimmed.ptr[eq - 1] == '!' || trimmed.ptr[eq - 1] == '<' || trimmed.ptr[eq - 1] == '>'))) {
        Span name = span_trim(span_sub(trimmed, 0, eq));
        Span expr = span_trim(span_sub(trimmed, eq + 1, trimmed.len));
        fprintf(file, "cold_csg_stmt\t%d\t%d\tassign\t", fn_index, indent);
        cold_write_span(file, name);
        fputc('\t', file);
        cold_write_span(file, expr);
        fputc('\n', file);
        return true;
    }
    int32_t open = cold_span_find_char(trimmed, '(');
    if (open > 0 && trimmed.len > open + 1) {
        int32_t close = trimmed.len - 1;
        bool has_question = false;
        if (trimmed.ptr[close] == '?') {
            has_question = true;
            close--;
        }
        if (close > open && trimmed.ptr[close] == ')') {
            Span callee = span_trim(span_sub(trimmed, 0, open));
            Span args = span_trim(span_sub(trimmed, open + 1, close));
            fprintf(file, "cold_csg_stmt\t%d\t%d\t%s\t", fn_index, indent, has_question ? "call_q" : "call");
            cold_write_span(file, callee);
            fputc('\t', file);
            cold_write_span(file, args);
            fputc('\n', file);
            return true;
        }
    }
    return false;
}

static bool cold_emit_csg_statement_rows(FILE *file, Span source) {
    int32_t pos = 0;
    int32_t fn_index = -1;
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
        if (cold_line_top_level(line)) {
            Span trimmed_top = span_trim(line);
            if (cold_span_starts_with(trimmed_top, "fn ")) fn_index++;
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        if (fn_index < 0) continue;
        Span trimmed = span_trim(line);
        if (trimmed.len <= 0 || cold_span_starts_with(trimmed, "#") || cold_span_starts_with(trimmed, "//")) continue;
        int32_t indent = cold_line_indent_width(line);
        if (!cold_emit_csg_statement_row(file, fn_index, indent, trimmed)) return false;
    }
    return true;
}

static bool cold_emit_csg_facts_from_source_path(const char *source_path, const char *csg_out_path) {
    Span source = source_open(source_path);
    if (source.len <= 0) return false;
    char parent[PATH_MAX];
    if (cold_parent_dir(csg_out_path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) {
        munmap((void *)source.ptr, (size_t)source.len);
        return false;
    }
    char tmp_path[PATH_MAX];
    int tmp_len = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", csg_out_path, (long)getpid());
    if (tmp_len < 0 || (size_t)tmp_len >= sizeof(tmp_path)) {
        munmap((void *)source.ptr, (size_t)source.len);
        return false;
    }
    FILE *file = fopen(tmp_path, "w");
    if (!file) {
        munmap((void *)source.ptr, (size_t)source.len);
        return false;
    }
    fputs("cold_csg_version=1\n", file);
    fputs("cold_csg_entry=main\n", file);
    bool ok = cold_emit_csg_type_rows(file, source) &&
              cold_emit_csg_function_rows(file, source) &&
              cold_emit_csg_statement_rows(file, source);
    if (fclose(file) != 0) ok = false;
    munmap((void *)source.ptr, (size_t)source.len);
    if (!ok) {
        unlink(tmp_path);
        return false;
    }
    if (rename(tmp_path, csg_out_path) != 0) {
        unlink(tmp_path);
        return false;
    }
    return ok;
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

static uint64_t cold_now_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void cold_print_elapsed_ms(FILE *file, const char *key, uint64_t elapsed_us) {
    fprintf(file, "%s=%llu.%03llu\n",
            key,
            (unsigned long long)(elapsed_us / 1000ULL),
            (unsigned long long)(elapsed_us % 1000ULL));
}

static bool cold_file_contains_text(const char *path, const char *needle) {
    Span haystack = source_open(path);
    int32_t n = (int32_t)strlen(needle);
    bool ok = false;
    if (haystack.len > 0 && n > 0 && haystack.len >= n) {
        for (int32_t i = 0; i <= haystack.len - n; i++) {
            if (memcmp(haystack.ptr + i, needle, (size_t)n) == 0) {
                ok = true;
                break;
            }
        }
    }
    if (haystack.len > 0) munmap((void *)haystack.ptr, (size_t)haystack.len);
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

static bool cold_run_executable_noargs_rc(const char *path, int32_t expected_rc) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execl(path, path, (char *)0);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == expected_rc;
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
                                      uint64_t hash, uint64_t elapsed_us) {
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
    cold_print_elapsed_ms(file, "cold_compile_elapsed_ms", elapsed_us);
    fclose(file);
    return true;
}

static int cold_cmd_compile_bootstrap(int argc, char **argv, const char *self_path) {
    uint64_t start_us = cold_now_us();
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
    uint64_t end_us = cold_now_us();
    uint64_t elapsed_us = start_us > 0 && end_us >= start_us ? end_us - start_us : 0;
    if (report_path && report_path[0] != '\0') {
        if (!cold_write_compile_report(report_path, &contract, abs_in, out_path, hash, elapsed_us)) {
            fprintf(stderr, "[cheng_cold] failed to write report: %s\n", report_path);
            free(image);
            free(normalized);
            return 1;
        }
    }
    printf("compiled=%s\n", out_path);
    printf("contract_hash=%016llx\n", (unsigned long long)hash);
    cold_print_elapsed_ms(stdout, "cold_compile_elapsed_ms", elapsed_us);
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
    uint64_t start_us = cold_now_us();
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

    uint64_t end_us = cold_now_us();
    uint64_t elapsed_us = start_us > 0 && end_us >= start_us ? end_us - start_us : 0;
    printf("bootstrap_bridge=ok\n");
    printf("out_dir=%s\n", abs_out_dir);
    printf("stage0=%s\n", stage0);
    printf("stage1=%s\n", stage1);
    printf("stage2=%s\n", stage2);
    printf("stage3=%s\n", stage3);
    printf("fixed_point=stage2_stage3_contract_match\n");
    cold_print_elapsed_ms(stdout, "cold_bootstrap_bridge_elapsed_ms", elapsed_us);
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
                                             uint64_t contract_hash,
                                             uint64_t elapsed_us) {
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    char manifest_path[PATH_MAX];
    if (!cold_contract_manifest_path(contract, manifest_path, sizeof(manifest_path))) return false;
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fputs("backend_driver_candidate=cold_subset_backend\n", file);
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
    fputs("cold_frontend_entry_dispatch_output=stdout_and_exit_code\n", file);
    fputs("cold_frontend_entry_system_link_report=1\n", file);
    fputs("cold_frontend_entry_system_link_report_verified=1\n", file);
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
    fputs("real_backend_codegen=1\n", file);
    fputs("system_link_exec=1\n", file);
    fputs("system_link_exec_scope=cold_subset_direct_macho\n", file);
    fputs("full_backend_codegen=0\n", file);
    fputs("cold_system_link_exec_smoke=1\n", file);
    fputs("cold_system_link_exec_smoke_exit=77\n", file);
    fputs("cold_system_link_exec_smoke_csg_lowering=1\n", file);
    cold_print_elapsed_ms(file, "cold_build_backend_driver_elapsed_ms", elapsed_us);
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
    fputs("cold_frontend_entry_dispatch_output=stdout_and_exit_code\n", file);
    fputs("cold_frontend_entry_system_link_report=1\n", file);
    fputs("cold_frontend_entry_system_link_report_verified=1\n", file);
    fprintf(file, "cold_frontend_declaration_hash=%016llx\n", (unsigned long long)stats->declaration_hash);
    fputs("command_surface=print-contract,self-check,compile-bootstrap,bootstrap-bridge,build-backend-driver,system-link-exec\n", file);
    fputs("real_backend_codegen=1\n", file);
    fputs("system_link_exec=1\n", file);
    fputs("system_link_exec_scope=cold_subset_direct_macho\n", file);
    fputs("full_backend_codegen=0\n", file);
    fputs("cold_system_link_exec_smoke=1\n", file);
    fputs("cold_system_link_exec_smoke_csg_lowering=1\n", file);
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
    uint64_t start_us = cold_now_us();
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

    if (!cold_write_normalized_contract_file(expected_contract_path, &contract)) {
        fprintf(stderr, "[cheng_cold] failed to write expected contract: %s\n", expected_contract_path);
        return 1;
    }

    char cmd[PATH_MAX * 24];
    if (!cold_bridge_command(cmd, sizeof(cmd), out_path, "self-check", 0, 0, 0, self_log) ||
        !cold_run_shell(cmd, "backend driver candidate self-check")) return 1;
    if (!cold_bridge_command(cmd, sizeof(cmd), out_path, "print-contract", 0, 0, 0, actual_contract_path) ||
        !cold_run_shell(cmd, "backend driver candidate print-contract")) return 1;
    if (!cold_files_equal(expected_contract_path, actual_contract_path)) {
        fprintf(stderr, "[cheng_cold] backend driver candidate contract mismatch\n");
        return 1;
    }

    char smoke_source[PATH_MAX];
    char smoke_csg[PATH_MAX];
    char smoke_exe[PATH_MAX];
    char smoke_report[PATH_MAX];
    char smoke_stdout[PATH_MAX];
    if (!cold_path_with_suffix(smoke_source, sizeof(smoke_source), out_path, ".system-link-smoke.cheng")) return 1;
    if (!cold_path_with_suffix(smoke_csg, sizeof(smoke_csg), out_path, ".system-link-smoke.csg.txt")) return 1;
    if (!cold_path_with_suffix(smoke_exe, sizeof(smoke_exe), out_path, ".system-link-smoke")) return 1;
    if (!cold_path_with_suffix(smoke_report, sizeof(smoke_report), out_path, ".system-link-smoke.report.txt")) return 1;
    if (!cold_path_with_suffix(smoke_stdout, sizeof(smoke_stdout), out_path, ".system-link-smoke.stdout")) return 1;
    if (!cold_write_text_file(smoke_source,
                              "fn touch(x: int32): int32 =\n"
                              "    return x\n"
                              "\n"
                              "fn value(seed: int32): int32 =\n"
                              "    var x = seed\n"
                              "    touch(x)\n"
                              "    var guard = 0\n"
                              "    while guard < 2 && x < 70:\n"
                              "        x = x + 1\n"
                              "        guard = guard + 1\n"
                              "    for i in 0..<5:\n"
                              "        if i == 1:\n"
                              "            continue\n"
                              "        if i == 4:\n"
                              "            break\n"
                              "        x = x + i\n"
                              "    x = x + 5\n"
                              "    if !(x < 0) && x > 70:\n"
                              "        x = x + 5\n"
                              "        return x\n"
                              "    else:\n"
                              "        return 1\n"
                              "\n"
                              "fn main(): int32 =\n"
                              "    return value(60)\n")) {
        fprintf(stderr, "[cheng_cold] failed to write system-link smoke source: %s\n", smoke_source);
        return 1;
    }
    char q_out[PATH_MAX * 5 + 8];
    char q_in_flag[PATH_MAX * 5 + 64];
    char q_csg_flag[PATH_MAX * 5 + 64];
    char q_out_flag[PATH_MAX * 5 + 64];
    char q_report_flag[PATH_MAX * 5 + 64];
    char q_smoke_stdout[PATH_MAX * 5 + 8];
    char in_flag[PATH_MAX + 16];
    char smoke_out_flag[PATH_MAX + 16];
    char smoke_report_flag[PATH_MAX + 32];
    char smoke_csg_flag[PATH_MAX + 16];
    snprintf(in_flag, sizeof(in_flag), "--in:%s", smoke_source);
    snprintf(smoke_csg_flag, sizeof(smoke_csg_flag), "--csg-out:%s", smoke_csg);
    snprintf(smoke_out_flag, sizeof(smoke_out_flag), "--out:%s", smoke_exe);
    snprintf(smoke_report_flag, sizeof(smoke_report_flag), "--report-out:%s", smoke_report);
    if (!cold_shell_quote(q_out, sizeof(q_out), out_path) ||
        !cold_shell_quote(q_in_flag, sizeof(q_in_flag), in_flag) ||
        !cold_shell_quote(q_csg_flag, sizeof(q_csg_flag), smoke_csg_flag) ||
        !cold_shell_quote(q_out_flag, sizeof(q_out_flag), smoke_out_flag) ||
        !cold_shell_quote(q_report_flag, sizeof(q_report_flag), smoke_report_flag) ||
        !cold_shell_quote(q_smoke_stdout, sizeof(q_smoke_stdout), smoke_stdout)) return 1;
    snprintf(cmd, sizeof(cmd), "%s system-link-exec %s %s %s %s > %s",
             q_out, q_in_flag, q_csg_flag, q_out_flag, q_report_flag, q_smoke_stdout);
    if (!cold_run_shell(cmd, "backend driver candidate system-link-exec")) return 1;
    if (!cold_run_executable_noargs_rc(smoke_exe, 77)) {
        fprintf(stderr, "[cheng_cold] system-link smoke executable exit mismatch: %s\n", smoke_exe);
        return 1;
    }
    if (!cold_file_contains_text(smoke_report, "system_link_exec=1\n") ||
        !cold_file_contains_text(smoke_report, "real_backend_codegen=1\n") ||
        !cold_file_contains_text(smoke_report, "cold_csg_lowering=1\n")) {
        fprintf(stderr, "[cheng_cold] system-link smoke report mismatch: %s\n", smoke_report);
        return 1;
    }
    if (!cold_file_contains_text(smoke_csg, "cold_csg_stmt\t1\t4\tfor_range\ti\t0\t5\tlt\n") ||
        !cold_file_contains_text(smoke_csg, "cold_csg_stmt\t1\t4\tif\t!(x < 0) && x > 70\n")) {
        fprintf(stderr, "[cheng_cold] generated system-link smoke csg mismatch: %s\n", smoke_csg);
        return 1;
    }

    uint64_t end_us = cold_now_us();
    uint64_t elapsed_us = start_us > 0 && end_us >= start_us ? end_us - start_us : 0;
    if (!cold_write_backend_driver_report(report_path, &contract, &stats,
                                          abs_contract, out_path, dispatch_path, map_path,
                                          index_path,
                                          contract_hash, elapsed_us)) {
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

    printf("backend_driver_candidate=ok\n");
    printf("candidate_kind=cold_subset_backend\n");
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
    printf("cold_frontend_entry_system_link_report=1\n");
    printf("cold_frontend_entry_system_link_report_verified=1\n");
    printf("cold_system_link_exec_smoke=1\n");
    printf("cold_system_link_exec_smoke_exit=77\n");
    printf("cold_system_link_exec_smoke_csg_lowering=1\n");
    printf("real_backend_codegen=1\n");
    printf("system_link_exec=1\n");
    printf("system_link_exec_scope=cold_subset_direct_macho\n");
    printf("full_backend_codegen=0\n");
    cold_print_elapsed_ms(stdout, "cold_build_backend_driver_elapsed_ms", elapsed_us);
    return 0;
}

static bool cold_is_reserved_unimplemented_command(const char *cmd) {
    return strcmp(cmd, "status") == 0 ||
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
    puts("  cheng_cold system-link-exec --in:<source> [--csg-in:<facts>|--csg-out:<facts>] --out:<path> [--emit:exe] [--target:arm64-apple-darwin] [--report-out:<path>]");
    puts("  reserved backend commands hard-fail until their real paths exist");
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
};

#define COLD_MAX_I32_PARAMS 8

static int32_t cold_slot_kind_from_code(char code) {
    if (code == 's') return SLOT_STR;
    if (code == 'i') return SLOT_I32;
    if (code == 'v') return SLOT_VARIANT;
    die("unknown cold field kind code");
    return SLOT_I32;
}

static int32_t cold_slot_kind_from_type(Span type) {
    return cold_slot_kind_from_code(cold_field_kind_code_from_type(type));
}

static int32_t cold_slot_size_for_kind(int32_t kind) {
    if (kind == SLOT_STR) return 16;
    if (kind == SLOT_PTR) return 8;
    if (kind == SLOT_I32) return 4;
    if (kind == SLOT_VARIANT) return 64;
    if (kind == SLOT_OBJECT) return 16;
    if (kind == SLOT_ARRAY_I32) return 4;
    if (kind == SLOT_SEQ_I32) return 16;
    die("unknown cold slot kind size");
    return 4;
}

static int32_t cold_slot_align_for_kind(int32_t kind) {
    return cold_slot_size_for_kind(kind) >= 8 ? 8 : 4;
}

typedef struct {
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
    int32_t slot_count;
    int32_t slot_cap;
    int32_t frame_size;

    int32_t *switch_tag;
    int32_t *switch_block;
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
    int32_t return_kind;
    int32_t return_size;
    Span return_type;
    int32_t sret_slot;

    Arena *arena;
} BodyIR;

static BodyIR *body_new(Arena *arena) {
    BodyIR *body = arena_alloc(arena, sizeof(BodyIR));
    body->arena = arena;
    body->return_kind = SLOT_I32;
    body->return_size = 4;
    body->sret_slot = -1;
    return body;
}

static void body_ensure_slots(BodyIR *body) {
    if (body->slot_count < body->slot_cap) return;
    int32_t next = body->slot_cap ? body->slot_cap * 2 : 32;
    int32_t *kind = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *offset = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *size = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *aux = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    Span *type = arena_alloc(body->arena, (size_t)next * sizeof(Span));
    if (body->slot_count > 0) {
        memcpy(kind, body->slot_kind, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(offset, body->slot_offset, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(size, body->slot_size, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(aux, body->slot_aux, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(type, body->slot_type, (size_t)body->slot_count * sizeof(Span));
    }
    body->slot_kind = kind;
    body->slot_offset = offset;
    body->slot_size = size;
    body->slot_aux = aux;
    body->slot_type = type;
    body->slot_cap = next;
}

static void body_ensure_ops(BodyIR *body) {
    if (body->op_count < body->op_cap) return;
    int32_t next = body->op_cap ? body->op_cap * 2 : 64;
    int32_t *kind = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *dst = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *a = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *b = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *c = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->op_count > 0) {
        memcpy(kind, body->op_kind, (size_t)body->op_count * sizeof(int32_t));
        memcpy(dst, body->op_dst, (size_t)body->op_count * sizeof(int32_t));
        memcpy(a, body->op_a, (size_t)body->op_count * sizeof(int32_t));
        memcpy(b, body->op_b, (size_t)body->op_count * sizeof(int32_t));
        memcpy(c, body->op_c, (size_t)body->op_count * sizeof(int32_t));
    }
    body->op_kind = kind;
    body->op_dst = dst;
    body->op_a = a;
    body->op_b = b;
    body->op_c = c;
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

static void body_ensure_call_args(BodyIR *body) {
    if (body->call_arg_count < body->call_arg_cap) return;
    int32_t next = body->call_arg_cap ? body->call_arg_cap * 2 : 32;
    int32_t *slot = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    int32_t *offset = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->call_arg_count > 0) {
        memcpy(slot, body->call_arg_slot, (size_t)body->call_arg_count * sizeof(int32_t));
        memcpy(offset, body->call_arg_offset, (size_t)body->call_arg_count * sizeof(int32_t));
    }
    body->call_arg_slot = slot;
    body->call_arg_offset = offset;
    body->call_arg_cap = next;
}

static void body_ensure_string_literals(BodyIR *body) {
    if (body->string_literal_count < body->string_literal_cap) return;
    int32_t next = body->string_literal_cap ? body->string_literal_cap * 2 : 16;
    Span *fresh = arena_alloc(body->arena, (size_t)next * sizeof(Span));
    if (body->string_literal_count > 0) {
        memcpy(fresh, body->string_literal, (size_t)body->string_literal_count * sizeof(Span));
    }
    body->string_literal = fresh;
    body->string_literal_cap = next;
}

static int32_t body_slot(BodyIR *body, int32_t kind, int32_t size) {
    body_ensure_slots(body);
    int32_t align = size >= 8 ? 8 : 4;
    body->frame_size = align_i32(body->frame_size, align);
    int32_t idx = body->slot_count++;
    body->slot_kind[idx] = kind;
    body->slot_offset[idx] = body->frame_size;
    body->slot_size[idx] = size;
    body->slot_aux[idx] = 0;
    body->slot_type[idx] = (Span){0};
    body->frame_size += align_i32(size, align);
    return idx;
}

static void body_slot_set_type(BodyIR *body, int32_t slot, Span type) {
    if (slot < 0 || slot >= body->slot_count) die("invalid slot type target");
    body->slot_type[slot] = type;
}

static void body_slot_set_array_len(BodyIR *body, int32_t slot, int32_t len) {
    if (slot < 0 || slot >= body->slot_count) die("invalid slot array target");
    body->slot_aux[slot] = len;
}

static int32_t body_op3(BodyIR *body, int32_t kind, int32_t dst,
                        int32_t a, int32_t b, int32_t c) {
    body_ensure_ops(body);
    int32_t idx = body->op_count++;
    body->op_kind[idx] = kind;
    body->op_dst[idx] = dst;
    body->op_a[idx] = a;
    body->op_b[idx] = b;
    body->op_c[idx] = c;
    return idx;
}

static int32_t body_op(BodyIR *body, int32_t kind, int32_t dst, int32_t a, int32_t b) {
    return body_op3(body, kind, dst, a, b, 0);
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

static void body_reopen_block(BodyIR *body, int32_t block) {
    if (block < 0 || block >= body->block_count) die("invalid block reopen");
    body->block_op_start[block] = body->op_count;
    body->block_op_count[block] = 0;
    body->block_term[block] = -1;
}

static int32_t body_switch_case(BodyIR *body, int32_t tag, int32_t block) {
    body_ensure_switches(body);
    int32_t idx = body->switch_count++;
    body->switch_tag[idx] = tag;
    body->switch_block[idx] = block;
    return idx;
}

static int32_t body_call_arg(BodyIR *body, int32_t slot) {
    body_ensure_call_args(body);
    int32_t idx = body->call_arg_count++;
    body->call_arg_slot[idx] = slot;
    body->call_arg_offset[idx] = 0;
    return idx;
}

static int32_t body_call_arg_with_offset(BodyIR *body, int32_t slot, int32_t offset) {
    body_ensure_call_args(body);
    int32_t idx = body->call_arg_count++;
    body->call_arg_slot[idx] = slot;
    body->call_arg_offset[idx] = offset;
    return idx;
}

static int32_t body_string_literal(BodyIR *body, Span literal) {
    body_ensure_string_literals(body);
    int32_t idx = body->string_literal_count++;
    body->string_literal[idx] = literal;
    return idx;
}

/* ================================================================
 * Symbol table and local table
 * ================================================================ */
typedef struct {
    Span name;
    int32_t tag;
    int32_t field_count;
    int32_t field_kind[COLD_MAX_VARIANT_FIELDS];
    int32_t field_offset[COLD_MAX_VARIANT_FIELDS];
} Variant;

typedef struct {
    Span name;
    Variant *variants;
    int32_t variant_count;
    int32_t max_field_count;
    int32_t max_slot_size;
} TypeDef;

typedef struct {
    Span name;
    int32_t kind;
    int32_t offset;
    int32_t size;
    int32_t array_len;
    Span type_name;
} ObjectField;

typedef struct {
    Span name;
    ObjectField *fields;
    int32_t field_count;
    int32_t slot_size;
} ObjectDef;

typedef struct {
    Span name;
    int32_t arity;
    int32_t param_kind[COLD_MAX_I32_PARAMS];
    int32_t param_size[COLD_MAX_I32_PARAMS];
    Span ret;
} FnDef;

typedef struct {
    FnDef *functions;
    int32_t function_count;
    int32_t function_cap;
    TypeDef *types;
    int32_t type_count;
    int32_t type_cap;
    ObjectDef *objects;
    int32_t object_count;
    int32_t object_cap;
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
    symbols->object_cap = 16;
    symbols->functions = arena_alloc(arena, (size_t)symbols->function_cap * sizeof(FnDef));
    symbols->types = arena_alloc(arena, (size_t)symbols->type_cap * sizeof(TypeDef));
    symbols->objects = arena_alloc(arena, (size_t)symbols->object_cap * sizeof(ObjectDef));
    return symbols;
}

static int32_t symbols_find_fn(Symbols *symbols, Span name, int32_t arity) {
    for (int32_t i = 0; i < symbols->function_count; i++) {
        if (symbols->functions[i].arity == arity &&
            span_same(symbols->functions[i].name, name)) return i;
    }
    return -1;
}

static int32_t symbols_add_fn(Symbols *symbols, Span name, int32_t arity,
                              const int32_t *param_kinds,
                              const int32_t *param_sizes,
                              Span ret) {
    int32_t existing = symbols_find_fn(symbols, name, arity);
    if (existing >= 0) {
        if (!span_same(symbols->functions[existing].ret, ret)) {
            die("function return kind mismatch");
        }
        if (param_kinds) {
            for (int32_t i = 0; i < arity; i++) {
                if (symbols->functions[existing].param_kind[i] != param_kinds[i]) {
                    die("function parameter kind mismatch");
                }
                if (param_sizes && param_sizes[i] > 0) {
                    int32_t existing_size = symbols->functions[existing].param_size[i];
                    if (existing_size == 0) {
                        symbols->functions[existing].param_size[i] = param_sizes[i];
                    } else if (existing_size != param_sizes[i]) {
                        die("function parameter size mismatch");
                    }
                }
            }
        }
        return existing;
    }
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
    for (int32_t i = 0; i < arity; i++) {
        fn->param_kind[i] = param_kinds ? param_kinds[i] : SLOT_I32;
        fn->param_size[i] = param_sizes ? param_sizes[i] : cold_slot_size_for_kind(fn->param_kind[i]);
    }
    fn->ret = ret;
    return symbols->function_count - 1;
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

static TypeDef *symbols_find_type(Symbols *symbols, Span name) {
    for (int32_t i = 0; i < symbols->type_count; i++) {
        if (span_same(symbols->types[i].name, name)) return &symbols->types[i];
    }
    return 0;
}

static ObjectDef *symbols_find_object(Symbols *symbols, Span name) {
    for (int32_t i = 0; i < symbols->object_count; i++) {
        if (span_same(symbols->objects[i].name, name)) return &symbols->objects[i];
    }
    return 0;
}

static ObjectField *object_find_field(ObjectDef *object, Span name) {
    for (int32_t i = 0; i < object->field_count; i++) {
        if (span_same(object->fields[i].name, name)) return &object->fields[i];
    }
    return 0;
}

static ObjectDef *symbols_add_object(Symbols *symbols, Span name, int32_t field_count) {
    if (symbols_find_object(symbols, name)) die("duplicate cold object type");
    if (symbols->object_count >= symbols->object_cap) {
        int32_t next = symbols->object_cap * 2;
        ObjectDef *fresh = arena_alloc(symbols->arena, (size_t)next * sizeof(ObjectDef));
        memcpy(fresh, symbols->objects, (size_t)symbols->object_count * sizeof(ObjectDef));
        symbols->objects = fresh;
        symbols->object_cap = next;
    }
    ObjectDef *object = &symbols->objects[symbols->object_count++];
    object->name = name;
    object->field_count = field_count;
    object->fields = arena_alloc(symbols->arena, (size_t)field_count * sizeof(ObjectField));
    return object;
}

static TypeDef *symbols_find_variant_type(Symbols *symbols, Variant *variant) {
    for (int32_t ti = 0; ti < symbols->type_count; ti++) {
        TypeDef *type = &symbols->types[ti];
        for (int32_t vi = 0; vi < type->variant_count; vi++) {
            if (&type->variants[vi] == variant) return type;
        }
    }
    return 0;
}

static void variant_finalize_layout(TypeDef *type, Variant *variant) {
    int32_t offset = 8;
    for (int32_t i = 0; i < variant->field_count; i++) {
        int32_t align = cold_slot_align_for_kind(variant->field_kind[i]);
        offset = align_i32(offset, align);
        variant->field_offset[i] = offset;
        offset += cold_slot_size_for_kind(variant->field_kind[i]);
    }
    int32_t slot_size = align_i32(offset, 8);
    if (slot_size < 16) slot_size = 16;
    if (slot_size > type->max_slot_size) type->max_slot_size = slot_size;
    if (variant->field_count > type->max_field_count) type->max_field_count = variant->field_count;
}

static int32_t symbols_variant_slot_size(Symbols *symbols, Variant *variant) {
    TypeDef *type = symbols_find_variant_type(symbols, variant);
    if (!type) die("variant has no parent type");
    return type->max_slot_size > 0 ? type->max_slot_size : 16;
}

static int32_t symbols_type_slot_size(TypeDef *type) {
    if (!type) die("missing type slot size");
    return type->max_slot_size > 0 ? type->max_slot_size : 16;
}

static int32_t symbols_object_slot_size(ObjectDef *object) {
    if (!object) die("missing object slot size");
    return object->slot_size > 0 ? object->slot_size : 16;
}

static bool cold_parse_i32_array_type(Span type, int32_t *len_out) {
    type = span_trim(type);
    if (!cold_span_starts_with(type, "int32[")) return false;
    if (type.len < 7 || type.ptr[type.len - 1] != ']') return false;
    Span count = span_trim(span_sub(type, 6, type.len - 1));
    if (!span_is_i32(count)) return false;
    int32_t len = span_i32(count);
    if (len <= 0) die("cold fixed array length must be positive");
    if (len_out) *len_out = len;
    return true;
}

static bool cold_parse_i32_seq_type(Span type) {
    type = span_trim(type);
    return span_eq(type, "int32[]") || span_eq(type, "int[]");
}

static int32_t cold_slot_kind_from_type_with_symbols(Symbols *symbols, Span type) {
    type = span_trim(type);
    int32_t array_len = 0;
    if (cold_parse_i32_array_type(type, &array_len)) return SLOT_ARRAY_I32;
    if (cold_parse_i32_seq_type(type)) return SLOT_SEQ_I32;
    if (span_eq(type, "str") || span_eq(type, "cstring") || span_eq(type, "s")) return SLOT_STR;
    if (span_eq(type, "int32") || span_eq(type, "int") || span_eq(type, "i") || type.len == 0) return SLOT_I32;
    if (span_eq(type, "v")) return SLOT_VARIANT;
    if (symbols && symbols_find_object(symbols, type)) return SLOT_OBJECT;
    if (symbols && symbols_find_type(symbols, type)) return SLOT_VARIANT;
    if (type.len > 1 && type.ptr[0] >= 'A' && type.ptr[0] <= 'Z') return SLOT_VARIANT;
    die("unsupported cold type");
    return SLOT_I32;
}

static int32_t cold_slot_size_from_type_with_symbols(Symbols *symbols, Span type, int32_t kind) {
    type = span_trim(type);
    int32_t array_len = 0;
    if (kind == SLOT_ARRAY_I32) {
        if (!cold_parse_i32_array_type(type, &array_len)) die("cold array type missing length");
        return align_i32(array_len * 4, 8);
    }
    if (kind == SLOT_SEQ_I32) {
        if (!cold_parse_i32_seq_type(type)) die("cold seq type missing []");
        return 16;
    }
    if (kind == SLOT_OBJECT) return symbols_object_slot_size(symbols_find_object(symbols, type));
    if (kind == SLOT_VARIANT && symbols && !span_eq(type, "v")) {
        TypeDef *def = symbols_find_type(symbols, type);
        if (!def) die("unknown cold variant type");
        return symbols_type_slot_size(def);
    }
    return cold_slot_size_for_kind(kind);
}

static int32_t cold_return_kind_from_span(Symbols *symbols, Span ret) {
    ret = span_trim(ret);
    if (span_eq(ret, "str") || span_eq(ret, "cstring")) return SLOT_STR;
    if (span_eq(ret, "int32") || span_eq(ret, "int") || ret.len == 0) return SLOT_I32;
    if (cold_parse_i32_seq_type(ret)) die("cold function cannot return stack-backed int32[] yet");
    if (symbols_find_type(symbols, ret)) return SLOT_VARIANT;
    if (symbols_find_object(symbols, ret)) return SLOT_OBJECT;
    die("unknown cold return type");
    return SLOT_I32;
}

static int32_t cold_return_slot_size(Symbols *symbols, Span ret, int32_t kind) {
    if (kind == SLOT_STR) return 16;
    if (kind == SLOT_VARIANT) return symbols_type_slot_size(symbols_find_type(symbols, span_trim(ret)));
    if (kind == SLOT_OBJECT) return symbols_object_slot_size(symbols_find_object(symbols, span_trim(ret)));
    return 4;
}

static bool cold_kind_is_composite(int32_t kind) {
    return kind == SLOT_STR || kind == SLOT_VARIANT || kind == SLOT_OBJECT ||
           kind == SLOT_ARRAY_I32 || kind == SLOT_SEQ_I32;
}

static int32_t body_default_slot(BodyIR *body, Symbols *symbols, Span type, int32_t *kind_out) {
    int32_t kind = cold_slot_kind_from_type_with_symbols(symbols, type);
    int32_t size = cold_slot_size_from_type_with_symbols(symbols, type, kind);
    int32_t slot = body_slot(body, kind, size);
    body_slot_set_type(body, slot, span_trim(type));
    if (kind == SLOT_ARRAY_I32) {
        int32_t len = 0;
        if (!cold_parse_i32_array_type(type, &len)) die("cold default array missing length");
        body_slot_set_array_len(body, slot, len);
    }
    if (kind == SLOT_I32) {
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
    } else {
        body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, -1, 0);
    }
    if (kind_out) *kind_out = kind;
    return slot;
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

static TypeDef *cold_question_result_type(Symbols *symbols, BodyIR *body, int32_t variant_slot) {
    if (variant_slot < 0 || variant_slot >= body->slot_count) die("invalid ? variant slot");
    if (body->slot_kind[variant_slot] != SLOT_VARIANT) die("? target must be a variant");
    Span type_name = body->slot_type[variant_slot];
    if (type_name.len <= 0) die("? target variant type is unknown");
    TypeDef *type = symbols_find_type(symbols, type_name);
    if (!type) die("? target type is not a known ADT");
    if (body->return_kind != SLOT_VARIANT) die("? requires function return type to be the same Result ADT");
    if (body->return_type.len <= 0 || !span_same(body->return_type, type->name)) {
        die("? Result type must match function return type");
    }
    if (type->variant_count != 2) die("? requires a two-variant Result ADT");
    if (!span_eq(type->variants[0].name, "Ok") || !span_eq(type->variants[1].name, "Err")) {
        die("? requires Result variants named Ok and Err");
    }
    return type;
}

static int32_t cold_lower_question_result(Symbols *symbols, BodyIR *body, Locals *locals,
                                          int32_t block, int32_t variant_slot,
                                          Span bind_name, bool bind_value,
                                          int32_t declared_kind, Span declared_type) {
    TypeDef *type = cold_question_result_type(symbols, body, variant_slot);
    Variant *ok_variant = &type->variants[0];
    if (bind_value && ok_variant->field_count != 1) die("? binding requires Ok to carry exactly one payload");
    int32_t tag_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_TAG_LOAD, tag_slot, variant_slot, 0);
    int32_t err_tag_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_I32_CONST, err_tag_slot, 1, 0);
    int32_t ok_block = body_block(body);
    int32_t err_block = body_block(body);
    int32_t cbr_term = body_term(body, BODY_TERM_CBR, tag_slot, COND_EQ, err_tag_slot, err_block, ok_block);
    body_end_block(body, block, cbr_term);
    body_reopen_block(body, err_block);
    int32_t ret_term = body_term(body, BODY_TERM_RET, variant_slot, -1, 0, -1, -1);
    body_end_block(body, err_block, ret_term);
    body_reopen_block(body, ok_block);
    if (bind_value) {
        int32_t payload_kind = ok_variant->field_kind[0];
        if (declared_kind >= 0 && declared_kind != payload_kind) die("? binding declared type mismatch");
        int32_t payload_slot = body_slot(body, payload_kind, cold_slot_size_for_kind(payload_kind));
        if (declared_type.len > 0) body_slot_set_type(body, payload_slot, declared_type);
        body_op(body, BODY_OP_PAYLOAD_LOAD, payload_slot, variant_slot, ok_variant->field_offset[0]);
        locals_add(locals, bind_name, payload_slot, payload_kind);
    }
    return ok_block;
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
    if (c == '"') {
        int32_t start = parser->pos++;
        while (parser->pos < parser->source.len) {
            c = parser->source.ptr[parser->pos++];
            if (c == '\n' || c == '\r') die("unterminated string literal");
            if (c == '\\') {
                if (parser->pos >= parser->source.len) die("unterminated string escape");
                parser->pos++;
                continue;
            }
            if (c == '"') return span_sub(parser->source, start, parser->pos);
        }
        die("unterminated string literal");
    }
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
    if ((c == '&' || c == '|') &&
        parser->pos + 1 < parser->source.len &&
        parser->source.ptr[parser->pos + 1] == c) {
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

static void parser_inline_ws(Parser *parser) {
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c != ' ' && c != '\t') break;
        parser->pos++;
    }
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

static Span parser_take_type_span(Parser *parser) {
    parser_ws(parser);
    int32_t type_start = parser->pos;
    Span first = parser_token(parser);
    if (first.len <= 0) die("expected type");
    if (span_eq(parser_peek(parser), "[")) parser_skip_balanced(parser, "[", "]");
    return span_trim(span_sub(parser->source, type_start, parser->pos));
}

static int32_t cold_param_kind_from_type(Span type) {
    type = span_trim(type);
    if (type.len == 0 || span_eq(type, "i") || span_eq(type, "int32") || span_eq(type, "int")) {
        return SLOT_I32;
    }
    if (span_eq(type, "s") || span_eq(type, "str") || span_eq(type, "cstring")) {
        return SLOT_STR;
    }
    if (span_eq(type, "v")) return SLOT_VARIANT;
    if (cold_parse_i32_seq_type(type)) return SLOT_SEQ_I32;
    if (type.len > 1 && type.ptr[0] >= 'A' && type.ptr[0] <= 'Z') return SLOT_VARIANT;
    die("unsupported cold parameter type");
    return SLOT_I32;
}

static int32_t cold_param_size_from_type(Symbols *symbols, Span type, int32_t kind) {
    type = span_trim(type);
    if (kind == SLOT_I32) return 4;
    if (kind == SLOT_STR) return 16;
    if (kind == SLOT_OBJECT) return symbols ? symbols_object_slot_size(symbols_find_object(symbols, type)) : 0;
    if (kind == SLOT_ARRAY_I32) return cold_slot_size_from_type_with_symbols(symbols, type, kind);
    if (kind == SLOT_SEQ_I32) return 16;
    if (kind == SLOT_VARIANT) {
        if (symbols && !span_eq(type, "v")) {
            TypeDef *def = symbols_find_type(symbols, type);
            if (!def) die("unknown cold variant parameter type");
            return symbols_type_slot_size(def);
        }
        return type.len > 0 && !span_eq(type, "v") ? 0 : cold_slot_size_for_kind(SLOT_VARIANT);
    }
    die("unsupported cold parameter slot size");
    return 4;
}

static int32_t cold_arg_reg_count(int32_t kind, int32_t size) {
    if (kind == SLOT_I32) return 1;
    if (kind == SLOT_STR) return 2;
    if (kind == SLOT_VARIANT) return size > 16 ? 1 : 2;
    if (kind == SLOT_OBJECT || kind == SLOT_ARRAY_I32) return size > 16 ? 1 : 2;
    if (kind == SLOT_SEQ_I32) return 2;
    die("unsupported cold call ABI kind");
    return 1;
}

static int32_t parse_param_specs(Symbols *symbols, Span params, Span *names,
                                 int32_t *kinds, int32_t *sizes, Span *types,
                                 int32_t cap) {
    Parser parser = {params, 0, 0, 0};
    int32_t count = 0;
    while (parser.pos < parser.source.len) {
        Span name = parser_token(&parser);
        if (name.len == 0) break;
        if (span_eq(name, ",")) continue;
        if (count >= cap) die("cold prototype supports at most eight params");
        int32_t kind = SLOT_I32;
        Span type = cold_cstr_span("int32");
        if (span_eq(parser_peek(&parser), ":")) {
            (void)parser_token(&parser);
            type = parser_take_type_span(&parser);
            kind = cold_param_kind_from_type(type);
            if (symbols) kind = cold_slot_kind_from_type_with_symbols(symbols, type);
        }
        names[count] = name;
        if (kinds) kinds[count] = kind;
        if (sizes) sizes[count] = cold_param_size_from_type(symbols, type, kind);
        if (types) types[count] = span_trim(type);
        count++;
        while (parser.pos < parser.source.len &&
               !span_eq(parser_peek(&parser), ",") &&
               !span_eq(parser_peek(&parser), ")")) {
            (void)parser_token(&parser);
        }
        if (span_eq(parser_peek(&parser), ",")) (void)parser_token(&parser);
    }
    return count;
}

static void cold_collect_function_signatures(Symbols *symbols, Span source) {
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
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        Span trimmed = span_trim(line);
        if (!cold_span_starts_with(trimmed, "fn ")) continue;
        ColdFunctionSymbol symbol;
        if (!cold_parse_function_symbol_at(source, start, line_no, &symbol)) {
            die("cannot parse cold function signature");
        }
        Span param_names[COLD_MAX_I32_PARAMS];
        int32_t param_kinds[COLD_MAX_I32_PARAMS];
        int32_t param_sizes[COLD_MAX_I32_PARAMS];
        int32_t arity = parse_param_specs(0, symbol.params, param_names, param_kinds,
                                          param_sizes, 0, COLD_MAX_I32_PARAMS);
        symbols_add_fn(symbols, symbol.name, arity, param_kinds, param_sizes, symbol.return_type);
    }
}

static void parse_type(Parser *parser) {
    int32_t line_start = parser->pos;
    int32_t line_end = line_start;
    while (line_end < parser->source.len && parser->source.ptr[line_end] != '\n') line_end++;

    int32_t type_body_indent = -1;
    if (line_end < parser->source.len) line_end++;
    while (line_end < parser->source.len) {
        int32_t indent = 0;
        int32_t pos = line_end;
        while (pos < parser->source.len && parser->source.ptr[pos] == ' ') { indent++; pos++; }
        if (pos >= parser->source.len || parser->source.ptr[pos] == '\n') break;
        if (type_body_indent < 0) type_body_indent = indent;
        if (indent < type_body_indent) break;
        if (indent == 0) break;
        while (line_end < parser->source.len && parser->source.ptr[line_end] != '\n') line_end++;
        if (line_end < parser->source.len) line_end++;
    }

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
        int32_t field_kinds[COLD_MAX_VARIANT_FIELDS];
        if (span_eq(parser_peek(&line), "(")) {
            parser_take(&line, "(");
            while (!span_eq(parser_peek(&line), ")")) {
                if (field_count >= COLD_MAX_VARIANT_FIELDS) die("too many variant fields");
                Span field = parser_token(&line);
                if (field.len == 0) die("unterminated variant fields");
                if (!parser_take(&line, ":")) die("expected : in variant field");
                Span field_type = parser_take_type_span(&line);
                if (field_type.len <= 0) die("expected variant field type");
                field_kinds[field_count++] = cold_slot_kind_from_type(field_type);
                if (span_eq(parser_peek(&line), ",")) (void)parser_token(&line);
            }
            parser_take(&line, ")");
        }
        Variant *variant = &type->variants[vi];
        variant->name = variant_name;
        variant->tag = vi;
        variant->field_count = field_count;
        for (int32_t i = 0; i < field_count; i++) variant->field_kind[i] = field_kinds[i];
        variant_finalize_layout(type, variant);
    }
    parser->pos = line_end;
    parser_line(parser);
}

static int32_t parse_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);

static void cold_validate_call_args(BodyIR *body, FnDef *fn, int32_t arg_start, int32_t arg_count) {
    if (arg_count != fn->arity) die("cold function arity mismatch");
    int32_t reg = 0;
    for (int32_t i = 0; i < arg_count; i++) {
        int32_t arg_slot = body->call_arg_slot[arg_start + i];
        int32_t arg_kind = body->slot_kind[arg_slot];
        if (arg_kind != fn->param_kind[i]) die("cold function arg kind mismatch");
        int32_t arg_size = body->slot_size[arg_slot];
        int32_t param_size = fn->param_size[i] > 0 ? fn->param_size[i] : arg_size;
        if ((arg_kind == SLOT_VARIANT || arg_kind == SLOT_OBJECT || arg_kind == SLOT_ARRAY_I32) &&
            arg_size != param_size) {
            die("cold function variant arg size mismatch");
        }
        reg += cold_arg_reg_count(arg_kind, param_size);
        if (reg > 8) die("cold call exceeds arm64 argument registers");
    }
}

static int32_t parse_call_after_name(Parser *parser, BodyIR *body, Locals *locals,
                                     Span name, int32_t *kind_out) {
    if (!parser_take(parser, "(")) die("expected ( after function name");
    int32_t arg_start = body->call_arg_count;
    int32_t arg_count = 0;
    while (!span_eq(parser_peek(parser), ")")) {
        int32_t arg_kind = SLOT_I32;
        int32_t arg_slot = parse_expr(parser, body, locals, &arg_kind);
        if (arg_kind != SLOT_I32 && arg_kind != SLOT_STR && arg_kind != SLOT_VARIANT &&
            arg_kind != SLOT_OBJECT && arg_kind != SLOT_ARRAY_I32 &&
            arg_kind != SLOT_SEQ_I32) die("unsupported cold function call arg kind");
        body_call_arg(body, arg_slot);
        arg_count++;
        if (span_eq(parser_peek(parser), ",")) (void)parser_token(parser);
        else break;
    }
    if (!parser_take(parser, ")")) die("expected ) after function call");
    int32_t fn_index = symbols_find_fn(parser->symbols, name, arg_count);
    if (fn_index < 0) die("unknown function call");
    FnDef *fn = &parser->symbols->functions[fn_index];
    cold_validate_call_args(body, fn, arg_start, arg_count);
    int32_t ret_kind = cold_return_kind_from_span(parser->symbols, fn->ret);
    int32_t slot = body_slot(body, ret_kind, cold_return_slot_size(parser->symbols, fn->ret, ret_kind));
    body_slot_set_type(body, slot, fn->ret);
    bool has_composite = (ret_kind != SLOT_I32);
    for (int32_t pi = 0; !has_composite && pi < fn->arity; pi++) {
        if (fn->param_kind[pi] != SLOT_I32) has_composite = true;
    }
    if (!has_composite) {
        body_op(body, BODY_OP_CALL_I32, slot, fn_index, arg_start);
    } else {
        body_op3(body, BODY_OP_CALL_COMPOSITE, slot, fn_index, arg_start, arg_count);
    }
    if (kind_out) *kind_out = ret_kind;
    return slot;
}

static int32_t parse_call_from_args_span(Parser *owner, BodyIR *body, Locals *locals,
                                         Span name, Span args, int32_t *kind_out) {
    Parser arg_parser = {args, 0, owner->arena, owner->symbols};
    int32_t arg_start = body->call_arg_count;
    int32_t arg_count = 0;
    parser_ws(&arg_parser);
    while (arg_parser.pos < arg_parser.source.len) {
        int32_t arg_kind = SLOT_I32;
        int32_t arg_slot = parse_expr(&arg_parser, body, locals, &arg_kind);
        if (arg_kind != SLOT_I32 && arg_kind != SLOT_STR && arg_kind != SLOT_VARIANT &&
            arg_kind != SLOT_OBJECT && arg_kind != SLOT_ARRAY_I32 &&
            arg_kind != SLOT_SEQ_I32) die("unsupported cold function call arg kind");
        body_call_arg(body, arg_slot);
        arg_count++;
        if (span_eq(parser_peek(&arg_parser), ",")) (void)parser_token(&arg_parser);
        else break;
        parser_ws(&arg_parser);
    }
    parser_ws(&arg_parser);
    if (arg_parser.pos != arg_parser.source.len) die("unsupported call arg tokens");
    int32_t fn_index = symbols_find_fn(owner->symbols, name, arg_count);
    if (fn_index < 0) die("unknown function call");
    FnDef *fn = &owner->symbols->functions[fn_index];
    cold_validate_call_args(body, fn, arg_start, arg_count);
    int32_t ret_kind = cold_return_kind_from_span(owner->symbols, fn->ret);
    int32_t slot = body_slot(body, ret_kind, cold_return_slot_size(owner->symbols, fn->ret, ret_kind));
    body_slot_set_type(body, slot, fn->ret);
    bool has_composite = (ret_kind != SLOT_I32);
    for (int32_t pi = 0; !has_composite && pi < fn->arity; pi++) {
        if (fn->param_kind[pi] != SLOT_I32) has_composite = true;
    }
    if (!has_composite) {
        body_op(body, BODY_OP_CALL_I32, slot, fn_index, arg_start);
    } else {
        body_op3(body, BODY_OP_CALL_COMPOSITE, slot, fn_index, arg_start, arg_count);
    }
    if (kind_out) *kind_out = ret_kind;
    return slot;
}

static int32_t parse_constructor(Parser *parser, BodyIR *body, Locals *locals,
                                 Variant *variant) {
    int32_t payload_count = 0;
    int32_t payload_slots[COLD_MAX_VARIANT_FIELDS];
    int32_t payload_offsets[COLD_MAX_VARIANT_FIELDS];
    if (span_eq(parser_peek(parser), "(")) {
        parser_take(parser, "(");
        while (!span_eq(parser_peek(parser), ")")) {
            if (payload_count >= COLD_MAX_VARIANT_FIELDS) die("too many cold variant payload fields");
            if (payload_count >= variant->field_count) die("too many variant constructor arguments");
            int32_t payload_kind = SLOT_I32;
            int32_t payload_slot = parse_expr(parser, body, locals, &payload_kind);
            if (payload_kind != variant->field_kind[payload_count]) die("variant constructor payload kind mismatch");
            payload_slots[payload_count] = payload_slot;
            payload_offsets[payload_count] = variant->field_offset[payload_count];
            payload_count++;
            if (span_eq(parser_peek(parser), ",")) {
                (void)parser_token(parser);
                continue;
            }
            break;
        }
        if (!parser_take(parser, ")")) die("expected ) after constructor");
    }
    if (payload_count != variant->field_count) {
        if (variant->field_count > 0) {
            die("variant constructor payload count mismatch");
        } else if (payload_count > 0) {
            die("empty variant cannot take constructor arguments");
        }
    }
    int32_t payload_start = payload_count > 0 ? body->call_arg_count : -1;
    for (int32_t i = 0; i < payload_count; i++) {
        body_call_arg_with_offset(body, payload_slots[i], payload_offsets[i]);
    }
    int32_t slot = body_slot(body, SLOT_VARIANT, symbols_variant_slot_size(parser->symbols, variant));
    TypeDef *variant_type = symbols_find_variant_type(parser->symbols, variant);
    if (!variant_type) die("variant constructor has no parent type");
    body_slot_set_type(body, slot, variant_type->name);
    body_op3(body, BODY_OP_MAKE_VARIANT, slot, variant->tag, payload_start, payload_count);
    return slot;
}

static int32_t body_slot_for_object_field(BodyIR *body, ObjectField *field) {
    int32_t slot = body_slot(body, field->kind, field->size);
    body_slot_set_type(body, slot, field->type_name);
    if (field->kind == SLOT_ARRAY_I32) body_slot_set_array_len(body, slot, field->array_len);
    return slot;
}

static int32_t parse_object_constructor(Parser *parser, BodyIR *body, Locals *locals,
                                        ObjectDef *object) {
    if (!parser_take(parser, "(")) die("expected ( after object constructor");
    int32_t payload_count = 0;
    int32_t payload_slots[COLD_MAX_OBJECT_FIELDS];
    int32_t payload_offsets[COLD_MAX_OBJECT_FIELDS];
    bool seen[COLD_MAX_OBJECT_FIELDS];
    memset(seen, 0, sizeof(seen));
    while (!span_eq(parser_peek(parser), ")")) {
        Span field_name = parser_token(parser);
        if (field_name.len <= 0) die("expected object field name");
        ObjectField *field = object_find_field(object, field_name);
        if (!field) die("unknown object constructor field");
        int32_t field_index = (int32_t)(field - object->fields);
        if (field_index < 0 || field_index >= object->field_count) die("object field index out of range");
        if (seen[field_index]) die("duplicate object constructor field");
        seen[field_index] = true;
        if (!parser_take(parser, ":")) die("expected : in object constructor field");
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
        if (value_kind != field->kind) die("object constructor field kind mismatch");
        if (field->kind == SLOT_ARRAY_I32 && body->slot_aux[value_slot] != field->array_len) {
            die("object constructor array length mismatch");
        }
        payload_slots[payload_count] = value_slot;
        payload_offsets[payload_count] = field->offset;
        payload_count++;
        if (span_eq(parser_peek(parser), ",")) {
            (void)parser_token(parser);
            continue;
        }
        break;
    }
    if (!parser_take(parser, ")")) die("expected ) after object constructor");
    int32_t payload_start = payload_count > 0 ? body->call_arg_count : -1;
    for (int32_t i = 0; i < payload_count; i++) {
        body_call_arg_with_offset(body, payload_slots[i], payload_offsets[i]);
    }
    int32_t slot = body_slot(body, SLOT_OBJECT, symbols_object_slot_size(object));
    body_slot_set_type(body, slot, object->name);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, payload_count);
    return slot;
}

static int32_t parse_i32_array_literal(Parser *parser, BodyIR *body, Locals *locals,
                                       int32_t *kind) {
    int32_t count = 0;
    int32_t element_slots[64];
    while (!span_eq(parser_peek(parser), "]")) {
        if (count >= 64) die("cold array literal too large");
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
        if (value_kind != SLOT_I32) die("cold array literal supports int32 only");
        element_slots[count] = value_slot;
        count++;
        if (span_eq(parser_peek(parser), ",")) {
            (void)parser_token(parser);
            continue;
        }
        break;
    }
    if (!parser_take(parser, "]")) die("expected ] after array literal");
    if (count <= 0) die("cold array literal cannot be empty without type");
    int32_t payload_start = body->call_arg_count;
    for (int32_t i = 0; i < count; i++) {
        body_call_arg_with_offset(body, element_slots[i], i * 4);
    }
    int32_t slot = body_slot(body, SLOT_ARRAY_I32, align_i32(count * 4, 8));
    body_slot_set_array_len(body, slot, count);
    body_op3(body, BODY_OP_MAKE_COMPOSITE, slot, 0, payload_start, count);
    *kind = SLOT_ARRAY_I32;
    return slot;
}

static int32_t parse_i32_seq_literal(Parser *parser, BodyIR *body, Locals *locals,
                                     int32_t *kind) {
    if (!parser_take(parser, "[")) die("expected [ before int32[] literal");
    int32_t count = 0;
    int32_t element_slots[64];
    while (!span_eq(parser_peek(parser), "]")) {
        if (count >= 64) die("cold int32[] literal too large");
        int32_t value_kind = SLOT_I32;
        int32_t value_slot = parse_expr(parser, body, locals, &value_kind);
        if (value_kind != SLOT_I32) die("cold int32[] literal supports int32 only");
        element_slots[count++] = value_slot;
        if (span_eq(parser_peek(parser), ",")) {
            (void)parser_token(parser);
            continue;
        }
        break;
    }
    if (!parser_take(parser, "]")) die("expected ] after int32[] literal");

    int32_t data_slot = -1;
    if (count > 0) {
        int32_t payload_start = body->call_arg_count;
        for (int32_t i = 0; i < count; i++) {
            body_call_arg_with_offset(body, element_slots[i], i * 4);
        }
        data_slot = body_slot(body, SLOT_ARRAY_I32, align_i32(count * 4, 8));
        body_slot_set_array_len(body, data_slot, count);
        body_op3(body, BODY_OP_MAKE_COMPOSITE, data_slot, 0, payload_start, count);
    }

    int32_t slot = body_slot(body, SLOT_SEQ_I32, 16);
    body_slot_set_type(body, slot, cold_cstr_span("int32[]"));
    body_slot_set_array_len(body, slot, count);
    body_op3(body, BODY_OP_MAKE_SEQ_I32, slot, data_slot, -1, count);
    *kind = SLOT_SEQ_I32;
    return slot;
}

static int32_t parse_match_bindings(Parser *parser, Span *bind_names, int32_t cap) {
    int32_t bind_count = 0;
    if (!span_eq(parser_peek(parser), "(")) return 0;
    parser_take(parser, "(");
    while (!span_eq(parser_peek(parser), ")")) {
        if (bind_count >= cap) die("too many cold match payload bindings");
        Span bind = parser_token(parser);
        if (bind.len <= 0) die("expected payload binding name");
        bind_names[bind_count++] = bind;
        if (span_eq(parser_peek(parser), ",")) {
            (void)parser_token(parser);
            continue;
        }
        break;
    }
    if (!parser_take(parser, ")")) die("expected ) in match arm");
    return bind_count;
}

static int32_t parse_primary(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    Span token = parser_token(parser);
    if (token.len == 0) die("expected expression");
    if (span_eq(token, "[")) {
        return parse_i32_array_literal(parser, body, locals, kind);
    }
    if (token.len >= 2 && token.ptr[0] == '"' && token.ptr[token.len - 1] == '"') {
        Span literal = span_sub(token, 1, token.len - 1);
        int32_t literal_index = body_string_literal(body, literal);
        int32_t slot = body_slot(body, SLOT_STR, 16);
        body_op(body, BODY_OP_STR_LITERAL, slot, literal_index, 0);
        *kind = SLOT_STR;
        return slot;
    }
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
    ObjectDef *object = symbols_find_object(parser->symbols, token);
    if (object && span_eq(parser_peek(parser), "(")) {
        *kind = SLOT_OBJECT;
        return parse_object_constructor(parser, body, locals, object);
    }
    if (span_eq(token, "len") && span_eq(parser_peek(parser), "(")) {
        parser_take(parser, "(");
        int32_t inner_kind = SLOT_I32;
        int32_t inner_slot = parse_expr(parser, body, locals, &inner_kind);
        if (inner_kind != SLOT_STR) die("len expects str");
        if (!parser_take(parser, ")")) die("expected ) after len");
        int32_t slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_STR_LEN, slot, inner_slot, 0);
        *kind = SLOT_I32;
        return slot;
    }
    if (span_eq(parser_peek(parser), "(")) {
        return parse_call_after_name(parser, body, locals, token, kind);
    }
    Local *local = locals_find(locals, token);
    if (!local) die("unknown identifier");
    *kind = local->kind;
    return local->slot;
}

static int32_t parse_postfix(Parser *parser, BodyIR *body, int32_t slot, int32_t *kind) {
    for (;;) {
        if (span_eq(parser_peek(parser), ".")) {
            (void)parser_token(parser);
            Span field_name = parser_token(parser);
            if (field_name.len <= 0) die("expected field name");
            if (*kind == SLOT_ARRAY_I32 && span_eq(field_name, "len")) {
                int32_t len_slot = body_slot(body, SLOT_I32, 4);
                body_op(body, BODY_OP_I32_CONST, len_slot, body->slot_aux[slot], 0);
                slot = len_slot;
                *kind = SLOT_I32;
                continue;
            }
            if (*kind == SLOT_SEQ_I32 && span_eq(field_name, "len")) {
                int32_t len_slot = body_slot(body, SLOT_I32, 4);
                body_op3(body, BODY_OP_PAYLOAD_LOAD, len_slot, slot, 0, 4);
                slot = len_slot;
                *kind = SLOT_I32;
                continue;
            }
            if (*kind != SLOT_OBJECT) die("field access target must be object");
            ObjectDef *object = symbols_find_object(parser->symbols, body->slot_type[slot]);
            if (!object) die("object slot missing type");
            ObjectField *field = object_find_field(object, field_name);
            if (!field) die("unknown object field");
            int32_t dst = body_slot_for_object_field(body, field);
            body_op3(body, BODY_OP_PAYLOAD_LOAD, dst, slot, field->offset, field->size);
            slot = dst;
            *kind = field->kind;
            continue;
        }
        if (span_eq(parser_peek(parser), "[")) {
            if (*kind != SLOT_ARRAY_I32 && *kind != SLOT_SEQ_I32) die("index target must be int32 array or int32[]");
            (void)parser_token(parser);
            Span index = parser_token(parser);
            if (!span_is_i32(index)) die("cold array index must be constant int32");
            int32_t value = span_i32(index);
            if (value < 0 || value >= body->slot_aux[slot]) die("cold array index out of bounds");
            if (!parser_take(parser, "]")) die("expected ] after array index");
            int32_t dst = body_slot(body, SLOT_I32, 4);
            if (*kind == SLOT_ARRAY_I32) {
                body_op3(body, BODY_OP_PAYLOAD_LOAD, dst, slot, value * 4, 4);
            } else {
                body_op3(body, BODY_OP_SEQ_I32_INDEX, dst, slot, value * 4, 4);
            }
            slot = dst;
            *kind = SLOT_I32;
            continue;
        }
        break;
    }
    return slot;
}

static int32_t parse_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind) {
    int32_t left_kind = SLOT_I32;
    int32_t left = parse_primary(parser, body, locals, &left_kind);
    left = parse_postfix(parser, body, left, &left_kind);
    while (span_eq(parser_peek(parser), "+") || span_eq(parser_peek(parser), "-")) {
        Span op = parser_token(parser);
        int32_t right_kind = SLOT_I32;
        int32_t right = parse_primary(parser, body, locals, &right_kind);
        right = parse_postfix(parser, body, right, &right_kind);
        if (left_kind != SLOT_I32 || right_kind != SLOT_I32) die("arithmetic operands must be int32");
        int32_t dst = body_slot(body, SLOT_I32, 4);
        body_op(body, span_eq(op, "+") ? BODY_OP_I32_ADD : BODY_OP_I32_SUB, dst, left, right);
        left = dst;
        left_kind = SLOT_I32;
    }
    *kind = left_kind;
    return left;
}

static void parse_return(Parser *parser, BodyIR *body, Locals *locals, int32_t block) {
    int32_t kind = SLOT_I32;
    int32_t slot = parse_expr(parser, body, locals, &kind);
    parser_inline_ws(parser);
    if (parser->pos < parser->source.len && parser->source.ptr[parser->pos] == '?') {
        die("return expr? is unsupported; bind the unwrapped value before return");
    }
    if (kind != body->return_kind) die("cold return kind mismatch");
    if (kind == SLOT_I32) body_op(body, BODY_OP_LOAD_I32, slot, 0, 0);
    int32_t term = body_term(body, BODY_TERM_RET, slot, -1, 0, -1, -1);
    body_end_block(body, block, term);
}

static int32_t parse_let(Parser *parser, BodyIR *body, Locals *locals, int32_t block) {
    Span name = parser_token(parser);
    Span type = {0};
    if (span_eq(parser_peek(parser), ":")) {
        (void)parser_token(parser);
        type = parser_take_type_span(parser);
    }
    if (!parser_take(parser, "=")) {
        if (type.len <= 0) die("expected = after let binding");
        int32_t kind = SLOT_I32;
        int32_t slot = body_default_slot(body, parser->symbols, type, &kind);
        locals_add(locals, name, slot, kind);
        return block;
    }
    int32_t kind = SLOT_I32;
    int32_t slot = -1;
    if (type.len > 0 && cold_parse_i32_seq_type(type) && span_eq(parser_peek(parser), "[")) {
        slot = parse_i32_seq_literal(parser, body, locals, &kind);
    } else {
        slot = parse_expr(parser, body, locals, &kind);
    }
    if (span_eq(parser_peek(parser), "?")) {
        (void)parser_token(parser);
        int32_t declared_kind = -1;
        Span declared_type = {0};
        if (type.len > 0) {
            declared_type = span_trim(type);
            declared_kind = cold_slot_kind_from_type_with_symbols(parser->symbols, declared_type);
        }
        return cold_lower_question_result(parser->symbols, body, locals, block, slot,
                                          name, true, declared_kind, declared_type);
    }
        if (type.len > 0) {
            int32_t declared_kind = cold_slot_kind_from_type_with_symbols(parser->symbols, type);
            if (kind != declared_kind) die("typed let initializer kind mismatch");
            if (kind == SLOT_ARRAY_I32) {
                int32_t declared_len = 0;
                if (!cold_parse_i32_array_type(type, &declared_len)) die("typed array let missing length");
                if (body->slot_aux[slot] != declared_len) die("typed array let length mismatch");
            } else if (kind == SLOT_SEQ_I32) {
                if (!cold_parse_i32_seq_type(type)) die("typed int32[] let missing dynamic sequence type");
            }
            body_slot_set_type(body, slot, type);
        }
    locals_add(locals, name, slot, kind);
    return block;
}

static void parse_assign(Parser *parser, BodyIR *body, Locals *locals, Span name) {
    Local *local = locals_find(locals, name);
    if (!local) die("assignment target must be a local");
    if (local->kind != SLOT_I32) die("cold assignment target must be int32");
    if (!parser_take(parser, "=")) die("expected = in assignment");
    int32_t kind = SLOT_I32;
    int32_t slot = parse_expr(parser, body, locals, &kind);
    if (kind != SLOT_I32) die("cold assignment value must be int32");
    body_op(body, BODY_OP_COPY_I32, local->slot, slot, 0);
}

typedef struct LoopCtx LoopCtx;

static int32_t parser_next_indent(Parser *parser);
static int32_t body_branch_to(BodyIR *body, int32_t block, int32_t target_block);
static int32_t parse_statement(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t block, LoopCtx *loop);
static Span parser_take_condition_span(Parser *parser);
static Span parser_take_match_target_span(Parser *parser);
static Span condition_strip_outer(Span condition);

static int32_t cold_match_eval_target(Parser *parser, BodyIR *body, Locals *locals,
                                      Span expr_span, int32_t *variant_slot) {
    Parser expr_parser = {expr_span, 0, parser->arena, parser->symbols};
    int32_t kind = SLOT_I32;
    int32_t slot = parse_expr(&expr_parser, body, locals, &kind);
    parser_ws(&expr_parser);
    if (expr_parser.pos != expr_parser.source.len) die("unsupported match target expression");
    if (kind != SLOT_VARIANT) die("match target must be a variant");
    *variant_slot = slot;
    return kind;
}

static int32_t parse_match_arm(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t matched_slot, int32_t arm_indent,
                               TypeDef *match_type, LoopCtx *loop,
                               bool *is_complete) {
    *is_complete = false;
    Span variant_name = parser_token(parser);
    Variant *variant = symbols_find_variant(parser->symbols, variant_name);
    if (!variant) die("unknown match variant");
    TypeDef *variant_type = symbols_find_variant_type(parser->symbols, variant);
    if (!variant_type) die("match variant has no parent type");
    if (match_type && variant_type != match_type) die("match arms must use one variant type");

    Span bind_names[COLD_MAX_VARIANT_FIELDS];
    int32_t bind_count = parse_match_bindings(parser, bind_names, COLD_MAX_VARIANT_FIELDS);
    if (bind_count != variant->field_count) die("match arm payload binding count mismatch");
    if (!parser_take(parser, "=>")) die("expected => in match arm");

    int32_t arm_block = body_block(body);
    body_switch_case(body, variant->tag, arm_block);
    int32_t saved_local_count = locals->count;
    for (int32_t field_index = 0; field_index < bind_count; field_index++) {
        int32_t field_kind = variant->field_kind[field_index];
        int32_t payload_slot = body_slot(body, field_kind, cold_slot_size_for_kind(field_kind));
        body_op(body, BODY_OP_PAYLOAD_LOAD, payload_slot, matched_slot,
                variant->field_offset[field_index]);
        locals_add(locals, bind_names[field_index], payload_slot, field_kind);
    }

    int32_t body_indent = parser_next_indent(parser);
    if (body_indent < 0 || body_indent <= arm_indent) {
        die("match arm body is empty");
    }
    int32_t arm_end = arm_block;
    while (parser->pos < parser->source.len && body->block_term[arm_end] < 0) {
        int32_t next_indent = parser_next_indent(parser);
        if (next_indent < 0 || next_indent < body_indent) break;
        if (next_indent == arm_indent) {
            Span next = parser_peek(parser);
            Variant *next_v = symbols_find_variant(parser->symbols, next);
            if (next_v && (!match_type || symbols_find_variant_type(parser->symbols, next_v) == match_type)) break;
        }
        arm_end = parse_statement(parser, body, locals, arm_end, loop);
    }
    locals->count = saved_local_count;
    *is_complete = true;
    return arm_end;
}

static int32_t parse_match(Parser *parser, BodyIR *body, Locals *locals,
                           int32_t block, LoopCtx *loop) {
    Span matched_expr = parser_take_match_target_span(parser);
    Span expr = condition_strip_outer(matched_expr);

    int32_t matched_slot = -1;
    Local *matched = locals_find(locals, expr);
    if (matched) {
        if (matched->kind != SLOT_VARIANT) die("match target must be a variant value");
        matched_slot = matched->slot;
    } else {
        cold_match_eval_target(parser, body, locals, expr, &matched_slot);
    }

    int32_t tag_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_TAG_LOAD, tag_slot, matched_slot, 0);
    int32_t case_start = body->switch_count;
    int32_t switch_term = body_term(body, BODY_TERM_SWITCH, tag_slot, case_start, 0, -1, -1);
    body_end_block(body, block, switch_term);

#define COLD_MATCH_VARIANT_CAP 128
#define COLD_MATCH_FALLTHROUGH_CAP 128
    int32_t case_count = 0;
    int32_t fallthrough_terms[COLD_MATCH_FALLTHROUGH_CAP];
    int32_t fallthrough_count = 0;
    bool seen_tags[COLD_MATCH_VARIANT_CAP];
    memset(seen_tags, 0, sizeof(seen_tags));
    TypeDef *match_type = 0;

    int32_t stmt_indent = parser_next_indent(parser);
    bool single_line = (parser->pos < parser->source.len &&
                        parser->source.ptr[parser->pos] != '\n');

    if (single_line) {
        while (parser->pos < parser->source.len && parser->source.ptr[parser->pos] != '\n') {
            Span next = parser_peek(parser);
            Variant *variant = symbols_find_variant(parser->symbols, next);
            if (!variant) break;
            if (match_type && symbols_find_variant_type(parser->symbols, variant) != match_type) break;
            if (!match_type) match_type = symbols_find_variant_type(parser->symbols, variant);
            if (seen_tags[variant->tag]) die("duplicate match arm");
            seen_tags[variant->tag] = true;
            case_count++;
            (void)parser_token(parser);

            Span bind_names[COLD_MAX_VARIANT_FIELDS];
            int32_t bind_count = parse_match_bindings(parser, bind_names, COLD_MAX_VARIANT_FIELDS);
            if (bind_count != variant->field_count) die("match arm payload binding count mismatch");
            if (!parser_take(parser, "=>")) die("expected => in match arm");

            int32_t arm_block = body_block(body);
            body_switch_case(body, variant->tag, arm_block);
            int32_t saved_local_count = locals->count;
            for (int32_t fi = 0; fi < bind_count; fi++) {
                int32_t fk = variant->field_kind[fi];
                int32_t ps = body_slot(body, fk, cold_slot_size_for_kind(fk));
                body_op(body, BODY_OP_PAYLOAD_LOAD, ps, matched_slot, variant->field_offset[fi]);
                locals_add(locals, bind_names[fi], ps, fk);
            }

            Span rest = (Span){parser->source.ptr + parser->pos,
                               parser->source.len - parser->pos};
            int32_t nl = 0;
            while (nl < rest.len && rest.ptr[nl] != '\n') nl++;
            Span arm_body = span_trim(span_sub(rest, 0, nl));
            Parser arm_parser = {arm_body, 0, parser->arena, parser->symbols};
            int32_t end_block = arm_block;
            while (arm_parser.pos < arm_parser.source.len &&
                   body->block_term[end_block] < 0) {
                end_block = parse_statement(&arm_parser, body, locals, end_block, loop);
            }
            if (body->block_term[end_block] < 0) {
                if (fallthrough_count >= COLD_MATCH_FALLTHROUGH_CAP) die("too many fallthrough");
                fallthrough_terms[fallthrough_count++] = body_branch_to(body, end_block, -1);
            }
            parser->pos += nl;
            if (parser->pos < parser->source.len) parser->pos++;
            locals->count = saved_local_count;
        }
    } else {
        int32_t arm_indent = stmt_indent;
        if (arm_indent < 0) arm_indent = 4;

        while (parser->pos < parser->source.len) {
            int32_t next_indent = parser_next_indent(parser);
            if (next_indent < 0) break;
            if (next_indent < arm_indent) break;
            if (next_indent != arm_indent) break;

            Span next = parser_peek(parser);
            Variant *variant = symbols_find_variant(parser->symbols, next);
            if (!variant) break;
            if (match_type && symbols_find_variant_type(parser->symbols, variant) != match_type) break;
            if (!match_type) match_type = symbols_find_variant_type(parser->symbols, variant);

            if (variant->tag < 0 || variant->tag >= COLD_MATCH_VARIANT_CAP) die("match tag out of range");
            if (seen_tags[variant->tag]) die("duplicate match arm");
            seen_tags[variant->tag] = true;
            case_count++;

            bool arm_complete = false;
            int32_t arm_end = parse_match_arm(parser, body, locals, matched_slot,
                                               arm_indent, match_type, loop, &arm_complete);
            if (!arm_complete) die("match arm incomplete");
            if (body->block_term[arm_end] < 0) {
                if (fallthrough_count >= COLD_MATCH_FALLTHROUGH_CAP) die("too many match fallthrough arms");
                fallthrough_terms[fallthrough_count++] = body_branch_to(body, arm_end, -1);
            }
        }
    }

    body->term_case_count[switch_term] = case_count;
    if (!match_type || case_count != match_type->variant_count) die("cold match must be exhaustive");
    int32_t result_block = block;
    if (fallthrough_count > 0) {
        int32_t join_block = body_block(body);
        for (int32_t i = 0; i < fallthrough_count; i++) {
            body->term_true_block[fallthrough_terms[i]] = join_block;
        }
        result_block = join_block;
    }
#undef COLD_MATCH_VARIANT_CAP
#undef COLD_MATCH_FALLTHROUGH_CAP
    int32_t saved = parser->pos;
    parser_ws(parser);
    if (span_eq(parser_peek(parser), "}")) {
        (void)parser_token(parser);
    } else {
        parser->pos = saved;
    }
    return result_block;
}

static int32_t parse_compare_op(Parser *parser) {
    Span op = parser_token(parser);
    if (span_eq(op, "==")) return COND_EQ;
    if (span_eq(op, "!=")) return COND_NE;
    if (span_eq(op, ">=")) return COND_GE;
    if (span_eq(op, "<")) return COND_LT;
    if (span_eq(op, ">")) return COND_GT;
    if (span_eq(op, "<=")) return COND_LE;
    die("unsupported condition comparison");
    return COND_EQ;
}

static Span parser_take_condition_span(Parser *parser) {
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c != ' ' && c != '\t') break;
        parser->pos++;
    }
    int32_t start = parser->pos;
    int32_t depth = 0;
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c == '\n' || c == '\r') die("expected : after condition");
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth < 0) die("unbalanced condition parens");
        } else if (c == ':' && depth == 0) {
            Span condition = span_trim(span_sub(parser->source, start, parser->pos));
            parser->pos++;
            if (condition.len <= 0) die("empty condition");
            return condition;
        }
        parser->pos++;
    }
    die("expected : after condition");
    return (Span){0};
}

static Span parser_take_match_target_span(Parser *parser) {
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c != ' ' && c != '\t') break;
        parser->pos++;
    }
    int32_t start = parser->pos;
    int32_t depth = 0;
    while (parser->pos < parser->source.len) {
        uint8_t c = parser->source.ptr[parser->pos];
        if (c == '\n' || c == '\r') die("expected : or { after match target");
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth < 0) die("unbalanced match target parens");
        } else if ((c == ':' || c == '{') && depth == 0) {
            Span target = span_trim(span_sub(parser->source, start, parser->pos));
            parser->pos++;
            if (target.len <= 0) die("empty match target");
            return target;
        }
        parser->pos++;
    }
    die("expected : or { after match target");
    return (Span){0};
}

static bool condition_outer_parens(Span condition) {
    if (condition.len < 2 || condition.ptr[0] != '(' || condition.ptr[condition.len - 1] != ')') return false;
    int32_t depth = 0;
    for (int32_t i = 0; i < condition.len; i++) {
        uint8_t c = condition.ptr[i];
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0 && i != condition.len - 1) return false;
            if (depth < 0) return false;
        }
    }
    return depth == 0;
}

static Span condition_strip_outer(Span condition) {
    Span result = span_trim(condition);
    while (condition_outer_parens(result)) {
        result = span_trim(span_sub(result, 1, result.len - 1));
    }
    return result;
}

static int32_t condition_find_top_binary(Span condition, const char *op) {
    int32_t depth = 0;
    int32_t op_len = (int32_t)strlen(op);
    for (int32_t i = 0; i <= condition.len - op_len; i++) {
        uint8_t c = condition.ptr[i];
        if (c == '(') {
            depth++;
            continue;
        }
        if (c == ')') {
            depth--;
            if (depth < 0) die("unbalanced condition parens");
            continue;
        }
        if (depth == 0 && memcmp(condition.ptr + i, op, (size_t)op_len) == 0) return i;
    }
    return -1;
}

static void parse_condition_span(Parser *owner, BodyIR *body, Locals *locals,
                                 Span condition, int32_t block,
                                 int32_t true_block, int32_t false_block) {
    condition = condition_strip_outer(condition);
    int32_t op_pos = condition_find_top_binary(condition, "||");
    if (op_pos >= 0) {
        int32_t rhs_block = body_block(body);
        parse_condition_span(owner, body, locals,
                             span_sub(condition, 0, op_pos),
                             block, true_block, rhs_block);
        body_reopen_block(body, rhs_block);
        parse_condition_span(owner, body, locals,
                             span_sub(condition, op_pos + 2, condition.len),
                             rhs_block, true_block, false_block);
        return;
    }
    op_pos = condition_find_top_binary(condition, "&&");
    if (op_pos >= 0) {
        int32_t rhs_block = body_block(body);
        parse_condition_span(owner, body, locals,
                             span_sub(condition, 0, op_pos),
                             block, rhs_block, false_block);
        body_reopen_block(body, rhs_block);
        parse_condition_span(owner, body, locals,
                             span_sub(condition, op_pos + 2, condition.len),
                             rhs_block, true_block, false_block);
        return;
    }
    if (condition.len > 0 && condition.ptr[0] == '!' &&
        !(condition.len > 1 && condition.ptr[1] == '=')) {
        parse_condition_span(owner, body, locals,
                             span_sub(condition, 1, condition.len),
                             block, false_block, true_block);
        return;
    }

    Parser leaf = {condition, 0, owner->arena, owner->symbols};
    int32_t left_kind = SLOT_I32;
    int32_t left = parse_expr(&leaf, body, locals, &left_kind);
    int32_t cond = parse_compare_op(&leaf);
    int32_t right_kind = SLOT_I32;
    int32_t right = parse_expr(&leaf, body, locals, &right_kind);
    parser_ws(&leaf);
    if (leaf.pos != leaf.source.len) die("unsupported trailing condition tokens");
    if (left_kind != SLOT_I32 || right_kind != SLOT_I32) die("condition needs int32 operands");
    int32_t term = body_term(body, BODY_TERM_CBR, left, cond, right,
                             true_block, false_block);
    body_end_block(body, block, term);
}

#define COLD_LOOP_PATCH_CAP 128

struct LoopCtx {
    int32_t break_terms[COLD_LOOP_PATCH_CAP];
    int32_t break_count;
    int32_t continue_terms[COLD_LOOP_PATCH_CAP];
    int32_t continue_count;
    struct LoopCtx *parent;
};

static int32_t parser_next_indent(Parser *parser) {
    int32_t saved = parser->pos;
    parser_ws(parser);
    int32_t pos = parser->pos;
    parser->pos = saved;
    if (pos >= parser->source.len) return -1;
    int32_t line = pos;
    while (line > 0 && parser->source.ptr[line - 1] != '\n') line--;
    int32_t indent = 0;
    while (line < parser->source.len) {
        uint8_t c = parser->source.ptr[line];
        if (c == ' ') indent++;
        else if (c == '\t') indent += 4;
        else break;
        line++;
    }
    return indent;
}

static int32_t body_branch_to(BodyIR *body, int32_t block, int32_t target_block) {
    int32_t term = body_term(body, BODY_TERM_BR, -1, -1, 0, target_block, -1);
    body_end_block(body, block, term);
    return term;
}

static void loop_add_break(LoopCtx *loop, int32_t term) {
    if (!loop) die("break outside loop");
    if (loop->break_count >= COLD_LOOP_PATCH_CAP) die("too many loop breaks");
    loop->break_terms[loop->break_count++] = term;
}

static void loop_add_continue(LoopCtx *loop, int32_t term) {
    if (!loop) die("continue outside loop");
    if (loop->continue_count >= COLD_LOOP_PATCH_CAP) die("too many loop continues");
    loop->continue_terms[loop->continue_count++] = term;
}

static int32_t parse_statement(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t block, LoopCtx *loop);

static int32_t parse_statements_until(Parser *parser, BodyIR *body, Locals *locals,
                                      int32_t block, int32_t min_indent,
                                      const char *stop_token, LoopCtx *loop) {
    int32_t current = block;
    while (parser->pos < parser->source.len && body->block_term[current] < 0) {
        int32_t next_indent = parser_next_indent(parser);
        if (next_indent < 0) break;
        if (min_indent >= 0 && next_indent < min_indent) break;
        if (stop_token) {
            Span next = parser_peek(parser);
            if (span_eq(next, stop_token)) break;
            if (strcmp(stop_token, "else") == 0 && span_eq(next, "elif")) break;
        }
        current = parse_statement(parser, body, locals, current, loop);
    }
    return current;
}

static void cold_require_suite_indent(Parser *parser, int32_t parent_indent, const char *kind) {
    int32_t indent = parser_next_indent(parser);
    if (indent <= parent_indent) {
        fprintf(stderr, "[cheng_cold] %s suite must be indented\n", kind);
        exit(2);
    }
}

static int32_t parse_if(Parser *parser, BodyIR *body, Locals *locals,
                        int32_t block, int32_t stmt_indent, LoopCtx *loop) {
    Span condition = parser_take_condition_span(parser);
    int32_t true_block = body_block(body);
    int32_t false_block = body_block(body);
    parse_condition_span(parser, body, locals, condition, block, true_block, false_block);

    body_reopen_block(body, true_block);
    int32_t saved_local_count = locals->count;
    cold_require_suite_indent(parser, stmt_indent, "if");
    int32_t true_indent = parser_next_indent(parser);
    int32_t true_end = parse_statements_until(parser, body, locals, true_block, true_indent, "else", loop);
    locals->count = saved_local_count;
    int32_t join_block = -1;
    if (body->block_term[true_end] < 0) {
        join_block = body_block(body);
        body_branch_to(body, true_end, join_block);
    }

    body_reopen_block(body, false_block);
    int32_t false_end = false_block;
    int32_t branch_indent = parser_next_indent(parser);
    Span branch_token = parser_peek(parser);
    if (branch_indent == stmt_indent && span_eq(branch_token, "elif")) {
        parser_take(parser, "elif");
        false_end = parse_if(parser, body, locals, false_block, stmt_indent, loop);
    } else {
        if (branch_indent == stmt_indent && span_eq(branch_token, "else")) {
            parser_take(parser, "else");
            if (!parser_take(parser, ":")) die("expected : after else");
            cold_require_suite_indent(parser, stmt_indent, "else");
            int32_t false_indent = parser_next_indent(parser);
            false_end = parse_statements_until(parser, body, locals, false_block, false_indent, 0, loop);
        }
    }
    locals->count = saved_local_count;

    if (body->block_term[false_end] < 0) {
        if (join_block < 0) join_block = body_block(body);
        body_branch_to(body, false_end, join_block);
    }
    if (join_block >= 0) {
        body_reopen_block(body, join_block);
        return join_block;
    }
    return true_end;
}

static int32_t parse_for(Parser *parser, BodyIR *body, Locals *locals,
                         int32_t block, int32_t stmt_indent, LoopCtx *parent_loop) {
    Span name = parser_token(parser);
    if (name.len == 0) die("expected loop variable after for");
    if (!parser_take(parser, "in")) die("expected in after loop variable");
    int32_t start_kind = SLOT_I32;
    int32_t start_slot = parse_expr(parser, body, locals, &start_kind);
    if (start_kind != SLOT_I32) die("for range start must be int32");
    if (!parser_take(parser, ".") || !parser_take(parser, ".")) die("expected .. in for range");
    int32_t cond = COND_LE;
    if (span_eq(parser_peek(parser), "<")) {
        parser_take(parser, "<");
        cond = COND_LT;
    }
    int32_t end_kind = SLOT_I32;
    int32_t end_slot = parse_expr(parser, body, locals, &end_kind);
    if (end_kind != SLOT_I32) die("for range end must be int32");
    if (!parser_take(parser, ":")) die("expected : after for range");

    int32_t iter_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_COPY_I32, iter_slot, start_slot, 0);

    int32_t cond_block = body_block(body);
    int32_t loop_block = body_block(body);
    body_branch_to(body, block, cond_block);

    int32_t cond_term = body_term(body, BODY_TERM_CBR, iter_slot, cond, end_slot,
                                  loop_block, -1);
    body_end_block(body, cond_block, cond_term);

    cold_require_suite_indent(parser, stmt_indent, "for");
    int32_t loop_indent = parser_next_indent(parser);
    int32_t saved_local_count = locals->count;
    locals_add(locals, name, iter_slot, SLOT_I32);
    LoopCtx loop = {0};
    loop.parent = parent_loop;
    int32_t loop_end = parse_statements_until(parser, body, locals, loop_block, loop_indent, 0, &loop);
    locals->count = saved_local_count;

    int32_t increment_block = body_block(body);
    if (body->block_term[loop_end] < 0) body_branch_to(body, loop_end, increment_block);
    int32_t one_slot = body_slot(body, SLOT_I32, 4);
    int32_t next_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_I32_CONST, one_slot, 1, 0);
    body_op(body, BODY_OP_I32_ADD, next_slot, iter_slot, one_slot);
    body_op(body, BODY_OP_COPY_I32, iter_slot, next_slot, 0);
    body_branch_to(body, increment_block, cond_block);
    int32_t after_block = body_block(body);
    body->term_false_block[cond_term] = after_block;
    for (int32_t i = 0; i < loop.break_count; i++) {
        body->term_true_block[loop.break_terms[i]] = after_block;
    }
    for (int32_t i = 0; i < loop.continue_count; i++) {
        body->term_true_block[loop.continue_terms[i]] = increment_block;
    }
    return after_block;
}

static int32_t parse_while(Parser *parser, BodyIR *body, Locals *locals,
                           int32_t block, int32_t stmt_indent, LoopCtx *parent_loop) {
    Span condition = parser_take_condition_span(parser);
    int32_t cond_block = body_block(body);
    int32_t loop_block = body_block(body);
    int32_t after_block = body_block(body);

    body_branch_to(body, block, cond_block);
    body_reopen_block(body, cond_block);
    parse_condition_span(parser, body, locals, condition, cond_block, loop_block, after_block);

    body_reopen_block(body, loop_block);
    cold_require_suite_indent(parser, stmt_indent, "while");
    int32_t loop_indent = parser_next_indent(parser);
    int32_t saved_local_count = locals->count;
    LoopCtx loop = {0};
    loop.parent = parent_loop;
    int32_t loop_end = parse_statements_until(parser, body, locals, loop_block, loop_indent, 0, &loop);
    locals->count = saved_local_count;

    if (body->block_term[loop_end] < 0) body_branch_to(body, loop_end, cond_block);
    for (int32_t i = 0; i < loop.break_count; i++) {
        body->term_true_block[loop.break_terms[i]] = after_block;
    }
    for (int32_t i = 0; i < loop.continue_count; i++) {
        body->term_true_block[loop.continue_terms[i]] = cond_block;
    }
    body_reopen_block(body, after_block);
    return after_block;
}

static int32_t parse_statement(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t block, LoopCtx *loop) {
    int32_t stmt_indent = parser_next_indent(parser);
    Span kw = parser_token(parser);
    if (kw.len == 0) return block;
    if (span_eq(kw, "let") || span_eq(kw, "var")) {
        return parse_let(parser, body, locals, block);
    } else if (span_eq(kw, "return")) {
        parse_return(parser, body, locals, block);
        return block;
    } else if (span_eq(kw, "match")) {
        return parse_match(parser, body, locals, block, loop);
    } else if (span_eq(kw, "if")) {
        return parse_if(parser, body, locals, block, stmt_indent, loop);
    } else if (span_eq(kw, "for")) {
        return parse_for(parser, body, locals, block, stmt_indent, loop);
    } else if (span_eq(kw, "while")) {
        return parse_while(parser, body, locals, block, stmt_indent, loop);
    } else if (span_eq(kw, "break")) {
        int32_t term = body_branch_to(body, block, -1);
        loop_add_break(loop, term);
        return block;
    } else if (span_eq(kw, "continue")) {
        int32_t term = body_branch_to(body, block, -1);
        loop_add_continue(loop, term);
        return block;
    } else if (span_eq(parser_peek(parser), "(")) {
        int32_t result_kind = SLOT_I32;
        int32_t result_slot = parse_call_after_name(parser, body, locals, kw, &result_kind);
        if (span_eq(parser_peek(parser), "?")) {
            (void)parser_token(parser);
            return cold_lower_question_result(parser->symbols, body, locals, block, result_slot,
                                              (Span){0}, false, -1, (Span){0});
        }
        return block;
    } else if (span_eq(parser_peek(parser), "=")) {
        parse_assign(parser, body, locals, kw);
        return block;
    } else {
        die("unsupported statement in cold prototype");
    }
    return block;
}

static BodyIR *parse_fn(Parser *parser, int32_t *symbol_index_out) {
    if (!parser_take(parser, "fn")) die("expected fn");
    Span fn_name = parser_token(parser);
    if (!parser_take(parser, "(")) die("expected ( after fn name");
    int32_t arity = 0;
    Span param_names[COLD_MAX_I32_PARAMS];
    Span param_types[COLD_MAX_I32_PARAMS];
    int32_t param_kinds[COLD_MAX_I32_PARAMS];
    int32_t param_sizes[COLD_MAX_I32_PARAMS];
    while (!span_eq(parser_peek(parser), ")")) {
        Span param = parser_token(parser);
        if (param.len == 0) die("unterminated params");
        if (arity >= COLD_MAX_I32_PARAMS) die("cold prototype supports at most eight params");
        param_names[arity] = param;
        param_types[arity] = (Span){0};
        param_kinds[arity] = SLOT_I32;
        param_sizes[arity] = 4;
        if (span_eq(parser_peek(parser), ":")) {
            (void)parser_token(parser);
            Span param_type = parser_take_type_span(parser);
            param_types[arity] = param_type;
            param_kinds[arity] = cold_param_kind_from_type(param_type);
            param_sizes[arity] = cold_param_size_from_type(parser->symbols, param_type, param_kinds[arity]);
        }
        arity++;
        while (!span_eq(parser_peek(parser), ",") &&
               !span_eq(parser_peek(parser), ")")) (void)parser_token(parser);
        if (span_eq(parser_peek(parser), ",")) (void)parser_token(parser);
    }
    parser_take(parser, ")");
    Span ret = {0};
    if (parser_take(parser, ":")) ret = parser_take_type_span(parser);
    if (!parser_take(parser, "=")) die("expected = before fn body");
    int32_t symbol_index = symbols_add_fn(parser->symbols, fn_name, arity,
                                          param_kinds, param_sizes, ret);
    if (symbol_index_out) *symbol_index_out = symbol_index;

    BodyIR *body = body_new(parser->arena);
    body->return_kind = cold_return_kind_from_span(parser->symbols, ret);
    body->return_size = cold_return_slot_size(parser->symbols, ret, body->return_kind);
    body->return_type = span_trim(ret);
    Locals locals;
    locals_init(&locals, parser->arena);
    if (cold_kind_is_composite(body->return_kind)) {
        body->sret_slot = body_slot(body, SLOT_PTR, 8);
    }
    body->param_count = arity;
    for (int32_t i = 0; i < arity; i++) {
        int32_t slot = body_slot(body, param_kinds[i], param_sizes[i]);
        body_slot_set_type(body, slot, param_types[i]);
        body->param_slot[i] = slot;
        locals_add(&locals, param_names[i], slot, param_kinds[i]);
    }
    int32_t block = body_block(body);

    int32_t body_indent = parser_next_indent(parser);
    int32_t end_block = parse_statements_until(parser, body, &locals, block, body_indent, 0, 0);
    if (body->block_term[end_block] < 0) die("function body has no terminator");
    return body;
}

/* ================================================================
 * Cold CSG body facts -> SoA BodyIR
 * ================================================================ */
typedef struct {
    int32_t fn_index;
    int32_t indent;
    Span kind;
    Span a;
    Span b;
    Span c;
    Span d;
} ColdCsgStmt;

typedef struct {
    Span name;
    Span params;
    Span ret;
    int32_t stmt_start;
    int32_t stmt_count;
    int32_t symbol_index;
} ColdCsgFunction;

typedef struct {
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

typedef struct {
    ColdCsg *csg;
    int32_t fn_index;
    int32_t cursor;
    int32_t end;
    Parser owner;
} ColdCsgLower;

static bool cold_span_starts_with_span(Span span, const char *prefix) {
    size_t len = strlen(prefix);
    return span.len >= (int32_t)len && memcmp(span.ptr, prefix, len) == 0;
}

static Span cold_span_after_prefix(Span span, const char *prefix) {
    size_t len = strlen(prefix);
    if (!cold_span_starts_with_span(span, prefix)) return (Span){0};
    return span_sub(span, (int32_t)len, span.len);
}

static int32_t cold_split_tabs(Span line, Span *fields, int32_t cap) {
    int32_t count = 0;
    int32_t start = 0;
    for (int32_t i = 0; i <= line.len; i++) {
        if (i < line.len && line.ptr[i] != '\t') continue;
        if (count >= cap) die("too many cold csg fields");
        fields[count++] = span_sub(line, start, i);
        start = i + 1;
    }
    return count;
}

static void cold_csg_ensure_functions(ColdCsg *csg) {
    if (csg->function_count < csg->function_cap) return;
    int32_t next = csg->function_cap ? csg->function_cap * 2 : 32;
    ColdCsgFunction *fresh = arena_alloc(csg->arena, (size_t)next * sizeof(ColdCsgFunction));
    if (csg->function_count > 0) {
        memcpy(fresh, csg->functions, (size_t)csg->function_count * sizeof(ColdCsgFunction));
    }
    csg->functions = fresh;
    csg->function_cap = next;
}

static void cold_csg_ensure_stmts(ColdCsg *csg) {
    if (csg->stmt_count < csg->stmt_cap) return;
    int32_t next = csg->stmt_cap ? csg->stmt_cap * 2 : 128;
    ColdCsgStmt *fresh = arena_alloc(csg->arena, (size_t)next * sizeof(ColdCsgStmt));
    if (csg->stmt_count > 0) {
        memcpy(fresh, csg->stmts, (size_t)csg->stmt_count * sizeof(ColdCsgStmt));
    }
    csg->stmts = fresh;
    csg->stmt_cap = next;
}

static int32_t cold_csg_count_variant_specs(Span specs) {
    int32_t count = 0;
    int32_t start = 0;
    for (int32_t i = 0; i <= specs.len; i++) {
        if (i < specs.len && specs.ptr[i] != ',') continue;
        Span part = span_trim(span_sub(specs, start, i));
        if (part.len > 0) count++;
        start = i + 1;
    }
    return count;
}

static void cold_csg_add_type(ColdCsg *csg, Span *fields, int32_t field_count) {
    if (field_count < 3) die("cold csg type row requires three fields");
    Span specs = fields[2];
    int32_t variant_count = cold_csg_count_variant_specs(specs);
    if (variant_count <= 0) die("cold csg type has no variants");
    TypeDef *type = symbols_add_type(csg->symbols, fields[1], variant_count);
    int32_t start = 0;
    int32_t vi = 0;
    for (int32_t i = 0; i <= specs.len; i++) {
        if (i < specs.len && specs.ptr[i] != ',') continue;
        Span part = span_trim(span_sub(specs, start, i));
        start = i + 1;
        if (part.len <= 0) continue;
        int32_t colon = cold_span_find_char(part, ':');
        if (colon <= 0) die("cold csg variant spec requires field count");
        Span name = span_trim(span_sub(part, 0, colon));
        Span rest = span_trim(span_sub(part, colon + 1, part.len));
        int32_t kind_colon = cold_span_find_char(rest, ':');
        Span count_span = kind_colon >= 0 ? span_trim(span_sub(rest, 0, kind_colon)) : rest;
        Span kind_codes = kind_colon >= 0 ? span_trim(span_sub(rest, kind_colon + 1, rest.len)) : (Span){0};
        if (!span_is_i32(count_span)) die("cold csg variant field count must be int32");
        int32_t fields_for_variant = span_i32(count_span);
        if (fields_for_variant < 0) die("cold csg variant field count must be non-negative");
        if (fields_for_variant > COLD_MAX_VARIANT_FIELDS) die("too many cold csg variant fields");
        if (kind_codes.len > 0 && kind_codes.len != fields_for_variant) die("cold csg variant kind count mismatch");
        if (vi >= variant_count) die("cold csg variant count overflow");
        Variant *variant = &type->variants[vi];
        variant->name = name;
        variant->tag = vi;
        variant->field_count = fields_for_variant;
        for (int32_t field_index = 0; field_index < fields_for_variant; field_index++) {
            char code = kind_codes.len > 0 ? (char)kind_codes.ptr[field_index] : 'i';
            variant->field_kind[field_index] = cold_slot_kind_from_code(code);
        }
        variant_finalize_layout(type, variant);
        vi++;
    }
    if (vi != variant_count) die("cold csg variant count mismatch");
}

static int32_t cold_csg_count_object_fields(Span specs) {
    int32_t count = 0;
    int32_t start = 0;
    for (int32_t i = 0; i <= specs.len; i++) {
        if (i < specs.len && specs.ptr[i] != ',') continue;
        Span part = span_trim(span_sub(specs, start, i));
        if (part.len > 0) count++;
        start = i + 1;
    }
    return count;
}

static void object_finalize_layout(ObjectDef *object) {
    int32_t offset = 0;
    for (int32_t i = 0; i < object->field_count; i++) {
        ObjectField *field = &object->fields[i];
        int32_t align = field->size >= 8 ? 8 : 4;
        offset = align_i32(offset, align);
        field->offset = offset;
        offset += align_i32(field->size, align);
    }
    object->slot_size = align_i32(offset, 8);
    if (object->slot_size < 16) object->slot_size = 16;
}

static void cold_csg_add_object(ColdCsg *csg, Span *fields, int32_t field_count) {
    if (field_count < 3) die("cold csg object row requires three fields");
    Span specs = fields[2];
    int32_t object_field_count = cold_csg_count_object_fields(specs);
    if (object_field_count <= 0) die("cold csg object has no fields");
    if (object_field_count > COLD_MAX_OBJECT_FIELDS) die("too many cold object fields");
    ObjectDef *object = symbols_add_object(csg->symbols, fields[1], object_field_count);
    int32_t start = 0;
    int32_t fi = 0;
    for (int32_t i = 0; i <= specs.len; i++) {
        if (i < specs.len && specs.ptr[i] != ',') continue;
        Span part = span_trim(span_sub(specs, start, i));
        start = i + 1;
        if (part.len <= 0) continue;
        int32_t colon = cold_span_find_char(part, ':');
        if (colon <= 0) die("cold csg object field requires kind");
        Span name = span_trim(span_sub(part, 0, colon));
        Span code = span_trim(span_sub(part, colon + 1, part.len));
        if (name.len <= 0 || code.len <= 0) die("cold csg object field malformed");
        ObjectField *field = &object->fields[fi++];
        field->name = name;
        field->type_name = (Span){0};
        field->array_len = 0;
        if (code.ptr[0] == 'a') {
            Span len_span = span_trim(span_sub(code, 1, code.len));
            if (!span_is_i32(len_span)) die("cold csg array field length must be int32");
            field->kind = SLOT_ARRAY_I32;
            field->array_len = span_i32(len_span);
            if (field->array_len <= 0) die("cold csg array length must be positive");
            field->size = align_i32(field->array_len * 4, 8);
        } else {
            field->kind = cold_slot_kind_from_code((char)code.ptr[0]);
            field->size = cold_slot_size_for_kind(field->kind);
        }
    }
    if (fi != object_field_count) die("cold csg object field count mismatch");
    object_finalize_layout(object);
}

static void cold_csg_add_function(ColdCsg *csg, Span *fields, int32_t field_count) {
    if (field_count < 5) die("cold csg function row requires five fields");
    int32_t index = span_i32(fields[1]);
    if (index != csg->function_count) die("cold csg functions must be dense and ordered");
    cold_csg_ensure_functions(csg);
    ColdCsgFunction *fn = &csg->functions[csg->function_count++];
    fn->name = fields[2];
    fn->params = fields[3];
    fn->ret = fields[4];
    fn->stmt_start = -1;
    fn->stmt_count = 0;
    fn->symbol_index = -1;
}

static void cold_csg_add_stmt(ColdCsg *csg, Span *fields, int32_t field_count) {
    if (field_count < 4) die("cold csg statement row requires four fields");
    cold_csg_ensure_stmts(csg);
    ColdCsgStmt *stmt = &csg->stmts[csg->stmt_count++];
    stmt->fn_index = span_i32(fields[1]);
    stmt->indent = span_i32(fields[2]);
    stmt->kind = fields[3];
    stmt->a = field_count > 4 ? fields[4] : (Span){0};
    stmt->b = field_count > 5 ? fields[5] : (Span){0};
    stmt->c = field_count > 6 ? fields[6] : (Span){0};
    stmt->d = field_count > 7 ? fields[7] : (Span){0};
}

static void cold_csg_finalize(ColdCsg *csg) {
    if (csg->function_count <= 0) die("cold csg has no functions");
    for (int32_t i = 0; i < csg->function_count; i++) {
        Span names[COLD_MAX_I32_PARAMS];
        int32_t kinds[COLD_MAX_I32_PARAMS];
        int32_t sizes[COLD_MAX_I32_PARAMS];
        int32_t arity = parse_param_specs(csg->symbols, csg->functions[i].params,
                                          names, kinds, sizes, 0, COLD_MAX_I32_PARAMS);
        csg->functions[i].symbol_index = symbols_add_fn(csg->symbols,
                                                        csg->functions[i].name,
                                                        arity,
                                                        kinds,
                                                        sizes,
                                                        csg->functions[i].ret);
    }
    int32_t prev_fn = -1;
    for (int32_t i = 0; i < csg->stmt_count; i++) {
        ColdCsgStmt *stmt = &csg->stmts[i];
        if (stmt->fn_index < 0 || stmt->fn_index >= csg->function_count) die("cold csg statement fn index invalid");
        if (stmt->fn_index < prev_fn) die("cold csg statements must be grouped by function");
        prev_fn = stmt->fn_index;
        ColdCsgFunction *fn = &csg->functions[stmt->fn_index];
        if (fn->stmt_start < 0) fn->stmt_start = i;
        fn->stmt_count++;
    }
}

static bool cold_csg_load(ColdCsg *csg, Arena *arena, Symbols *symbols, Span text) {
    memset(csg, 0, sizeof(*csg));
    csg->arena = arena;
    csg->symbols = symbols;
    bool saw_version = false;
    int32_t pos = 0;
    while (pos < text.len) {
        int32_t start = pos;
        while (pos < text.len && text.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < text.len) pos++;
        Span line = span_trim(span_sub(text, start, end));
        if (line.len <= 0 || line.ptr[0] == '#') continue;
        if (span_eq(line, "cold_csg_version=1")) {
            saw_version = true;
            continue;
        }
        if (cold_span_starts_with_span(line, "cold_csg_entry=")) {
            csg->entry = cold_span_after_prefix(line, "cold_csg_entry=");
            continue;
        }
        Span fields[8];
        int32_t field_count = cold_split_tabs(line, fields, 8);
        if (field_count <= 0) continue;
        if (span_eq(fields[0], "cold_csg_type")) {
            cold_csg_add_type(csg, fields, field_count);
            continue;
        }
        if (span_eq(fields[0], "cold_csg_object")) {
            cold_csg_add_object(csg, fields, field_count);
            continue;
        }
        if (span_eq(fields[0], "cold_csg_function")) {
            cold_csg_add_function(csg, fields, field_count);
            continue;
        }
        if (span_eq(fields[0], "cold_csg_stmt")) {
            cold_csg_add_stmt(csg, fields, field_count);
            continue;
        }
        die("unknown cold csg row");
    }
    if (!saw_version) die("cold csg version missing");
    cold_csg_finalize(csg);
    return true;
}

static int32_t csg_next_indent(ColdCsgLower *lower) {
    if (lower->cursor >= lower->end) return -1;
    return lower->csg->stmts[lower->cursor].indent;
}

static int32_t csg_parse_statement(ColdCsgLower *lower, BodyIR *body,
                                   Locals *locals, int32_t block, LoopCtx *loop);

static bool csg_is_branch_row(ColdCsgStmt *stmt) {
    return span_eq(stmt->kind, "elif") ||
           span_eq(stmt->kind, "else") ||
           span_eq(stmt->kind, "case");
}

static int32_t csg_parse_statements_until(ColdCsgLower *lower, BodyIR *body,
                                          Locals *locals, int32_t block,
                                          int32_t min_indent,
                                          int32_t branch_indent,
                                          LoopCtx *loop) {
    int32_t current = block;
    while (lower->cursor < lower->end && body->block_term[current] < 0) {
        ColdCsgStmt *next = &lower->csg->stmts[lower->cursor];
        if (min_indent >= 0 && next->indent < min_indent) break;
        if (branch_indent >= 0 && next->indent == branch_indent && csg_is_branch_row(next)) break;
        current = csg_parse_statement(lower, body, locals, current, loop);
    }
    return current;
}

static int32_t csg_parse_expr_span(ColdCsgLower *lower, BodyIR *body,
                                   Locals *locals, Span text, int32_t *kind) {
    Parser parser = {text, 0, lower->owner.arena, lower->owner.symbols};
    int32_t slot = parse_expr(&parser, body, locals, kind);
    parser_ws(&parser);
    if (parser.pos != parser.source.len) die("unsupported cold csg expression tokens");
    return slot;
}

static void csg_parse_return(ColdCsgLower *lower, BodyIR *body,
                             Locals *locals, int32_t block, Span expr) {
    int32_t kind = SLOT_I32;
    int32_t slot = csg_parse_expr_span(lower, body, locals, expr, &kind);
    if (kind != body->return_kind) die("cold csg return kind mismatch");
    if (kind == SLOT_I32) body_op(body, BODY_OP_LOAD_I32, slot, 0, 0);
    int32_t term = body_term(body, BODY_TERM_RET, slot, -1, 0, -1, -1);
    body_end_block(body, block, term);
}

static void csg_parse_let(ColdCsgLower *lower, BodyIR *body,
                          Locals *locals, Span name, Span expr) {
    int32_t kind = SLOT_I32;
    int32_t slot = csg_parse_expr_span(lower, body, locals, expr, &kind);
    locals_add(locals, name, slot, kind);
}

static int32_t csg_parse_typed_expr_span(ColdCsgLower *lower, BodyIR *body,
                                         Locals *locals, Span type,
                                         Span expr, int32_t *kind) {
    int32_t declared_kind = cold_slot_kind_from_type_with_symbols(lower->csg->symbols, type);
    int32_t slot = -1;
    if (declared_kind == SLOT_SEQ_I32) {
        Parser parser = {expr, 0, lower->owner.arena, lower->owner.symbols};
        parser_ws(&parser);
        if (!span_eq(parser_peek(&parser), "[")) die("cold csg int32[] initializer must be a literal");
        slot = parse_i32_seq_literal(&parser, body, locals, kind);
        parser_ws(&parser);
        if (parser.pos != parser.source.len) die("unsupported cold csg int32[] initializer tokens");
    } else {
        slot = csg_parse_expr_span(lower, body, locals, expr, kind);
    }
    if (*kind != declared_kind) die("cold csg typed initializer kind mismatch");
    if (declared_kind == SLOT_ARRAY_I32) {
        int32_t declared_len = 0;
        if (!cold_parse_i32_array_type(type, &declared_len)) die("cold csg typed array missing length");
        if (body->slot_aux[slot] != declared_len) die("cold csg typed array length mismatch");
    } else if (declared_kind == SLOT_SEQ_I32) {
        if (!cold_parse_i32_seq_type(type)) die("cold csg typed int32[] missing dynamic sequence type");
    }
    body_slot_set_type(body, slot, span_trim(type));
    return slot;
}

static void csg_parse_let_t(ColdCsgLower *lower, BodyIR *body,
                            Locals *locals, Span name, Span type, Span expr) {
    int32_t kind = SLOT_I32;
    int32_t slot = csg_parse_typed_expr_span(lower, body, locals, type, expr, &kind);
    locals_add(locals, name, slot, kind);
}

static void csg_parse_default(ColdCsgLower *lower, BodyIR *body,
                              Locals *locals, Span name, Span type) {
    int32_t kind = SLOT_I32;
    int32_t slot = body_default_slot(body, lower->csg->symbols, type, &kind);
    locals_add(locals, name, slot, kind);
}

static void csg_parse_assign(ColdCsgLower *lower, BodyIR *body,
                             Locals *locals, Span name, Span expr) {
    Local *local = locals_find(locals, name);
    if (!local) die("cold csg assignment target must be a local");
    if (local->kind != SLOT_I32) die("cold csg assignment target must be int32");
    int32_t kind = SLOT_I32;
    int32_t slot = csg_parse_expr_span(lower, body, locals, expr, &kind);
    if (kind != SLOT_I32) die("cold csg assignment value must be int32");
    body_op(body, BODY_OP_COPY_I32, local->slot, slot, 0);
}

static int32_t csg_parse_binding_csv(Span text, Span *bind_names, int32_t cap) {
    Parser parser = {text, 0, 0, 0};
    int32_t count = 0;
    parser_ws(&parser);
    while (parser.pos < parser.source.len) {
        if (count >= cap) die("too many cold csg match payload bindings");
        Span bind = parser_token(&parser);
        if (bind.len <= 0) die("expected cold csg payload binding");
        bind_names[count++] = bind;
        parser_ws(&parser);
        if (parser.pos >= parser.source.len) break;
        if (!parser_take(&parser, ",")) die("expected comma between cold csg payload bindings");
        parser_ws(&parser);
    }
    return count;
}

static int32_t csg_parse_match(ColdCsgLower *lower, BodyIR *body, Locals *locals,
                               int32_t block, ColdCsgStmt stmt, LoopCtx *loop) {
    int32_t matched_slot = -1;
    Local *matched = locals_find(locals, stmt.a);
    if (matched) {
        if (matched->kind != SLOT_VARIANT) die("cold csg match target must be a variant value");
        matched_slot = matched->slot;
    } else {
        int32_t kind = SLOT_I32;
        matched_slot = csg_parse_expr_span(lower, body, locals, stmt.a, &kind);
        if (kind != SLOT_VARIANT) die("cold csg match target must be a variant");
    }

    int32_t tag_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_TAG_LOAD, tag_slot, matched_slot, 0);
    int32_t case_start = body->switch_count;
    int32_t switch_term = body_term(body, BODY_TERM_SWITCH, tag_slot, case_start, 0, -1, -1);
    body_end_block(body, block, switch_term);

#define COLD_CSG_MATCH_VARIANT_CAP 128
#define COLD_CSG_MATCH_FALLTHROUGH_CAP 128
    int32_t case_count = 0;
    int32_t fallthrough_terms[COLD_CSG_MATCH_FALLTHROUGH_CAP];
    int32_t fallthrough_count = 0;
    bool seen_tags[COLD_CSG_MATCH_VARIANT_CAP];
    memset(seen_tags, 0, sizeof(seen_tags));
    TypeDef *match_type = 0;
    int32_t arm_indent = csg_next_indent(lower);
    if (arm_indent <= stmt.indent) die("cold csg match must have indented arms");
    while (lower->cursor < lower->end) {
        ColdCsgStmt *arm = &lower->csg->stmts[lower->cursor];
        if (arm->indent <= stmt.indent) break;
        if (arm->indent != arm_indent || !span_eq(arm->kind, "case")) {
            die("cold csg match arm must be a case row");
        }
        lower->cursor++;

        Variant *variant = symbols_find_variant(lower->csg->symbols, arm->a);
        if (!variant) die("unknown cold csg match variant");
        TypeDef *variant_type = symbols_find_variant_type(lower->csg->symbols, variant);
        if (!variant_type) die("cold csg match variant has no parent type");
        if (!match_type) {
            match_type = variant_type;
            if (match_type->variant_count > COLD_CSG_MATCH_VARIANT_CAP) die("too many cold csg match variants");
        } else if (match_type != variant_type) {
            die("cold csg match arms must use one variant type");
        }
        if (variant->tag < 0 || variant->tag >= COLD_CSG_MATCH_VARIANT_CAP) die("cold csg match tag out of range");
        if (seen_tags[variant->tag]) die("duplicate cold csg match arm");
        seen_tags[variant->tag] = true;

        int32_t arm_block = body_block(body);
        body_switch_case(body, variant->tag, arm_block);
        case_count++;
        int32_t saved_local_count = locals->count;
        Span bind_names[COLD_MAX_VARIANT_FIELDS];
        int32_t bind_count = csg_parse_binding_csv(arm->b, bind_names, COLD_MAX_VARIANT_FIELDS);
        if (bind_count != variant->field_count) die("cold csg match arm payload binding count mismatch");
        for (int32_t field_index = 0; field_index < bind_count; field_index++) {
            int32_t field_kind = variant->field_kind[field_index];
            int32_t payload_slot = body_slot(body, field_kind, cold_slot_size_for_kind(field_kind));
            body_op(body, BODY_OP_PAYLOAD_LOAD, payload_slot, matched_slot,
                    variant->field_offset[field_index]);
            locals_add(locals, bind_names[field_index], payload_slot, field_kind);
        }

        int32_t body_indent = csg_next_indent(lower);
        if (body_indent <= arm_indent) die("cold csg match arm body is empty");
        int32_t arm_end = csg_parse_statements_until(lower, body, locals, arm_block,
                                                     body_indent, arm_indent, loop);
        if (body->block_term[arm_end] < 0) {
            if (fallthrough_count >= COLD_CSG_MATCH_FALLTHROUGH_CAP) {
                die("too many cold csg match fallthrough arms");
            }
            fallthrough_terms[fallthrough_count++] = body_branch_to(body, arm_end, -1);
        }
        locals->count = saved_local_count;
    }
    body->term_case_count[switch_term] = case_count;
    if (!match_type || case_count != match_type->variant_count) die("cold csg match must be exhaustive");
    int32_t result_block = block;
    if (fallthrough_count > 0) {
        int32_t join_block = body_block(body);
        for (int32_t i = 0; i < fallthrough_count; i++) {
            body->term_true_block[fallthrough_terms[i]] = join_block;
        }
        result_block = join_block;
    }
#undef COLD_CSG_MATCH_VARIANT_CAP
#undef COLD_CSG_MATCH_FALLTHROUGH_CAP
    return result_block;
}

static int32_t csg_parse_if(ColdCsgLower *lower, BodyIR *body, Locals *locals,
                            int32_t block, ColdCsgStmt stmt, LoopCtx *loop) {
    int32_t true_block = body_block(body);
    int32_t false_block = body_block(body);
    parse_condition_span(&lower->owner, body, locals, stmt.a, block, true_block, false_block);

    body_reopen_block(body, true_block);
    int32_t saved_local_count = locals->count;
    int32_t true_indent = csg_next_indent(lower);
    if (true_indent <= stmt.indent) die("cold csg if suite must be indented");
    int32_t true_end = csg_parse_statements_until(lower, body, locals, true_block,
                                                  true_indent, stmt.indent, loop);
    locals->count = saved_local_count;
    int32_t join_block = -1;
    if (body->block_term[true_end] < 0) {
        join_block = body_block(body);
        body_branch_to(body, true_end, join_block);
    }

    body_reopen_block(body, false_block);
    int32_t false_end = false_block;
    if (lower->cursor < lower->end) {
        ColdCsgStmt *branch = &lower->csg->stmts[lower->cursor];
        if (branch->indent == stmt.indent && span_eq(branch->kind, "elif")) {
            ColdCsgStmt elif_stmt = *branch;
            lower->cursor++;
            false_end = csg_parse_if(lower, body, locals, false_block, elif_stmt, loop);
        } else if (branch->indent == stmt.indent && span_eq(branch->kind, "else")) {
            lower->cursor++;
            int32_t false_indent = csg_next_indent(lower);
            if (false_indent <= stmt.indent) die("cold csg else suite must be indented");
            false_end = csg_parse_statements_until(lower, body, locals, false_block,
                                                   false_indent, -1, loop);
        }
    }
    locals->count = saved_local_count;

    if (body->block_term[false_end] < 0) {
        if (join_block < 0) join_block = body_block(body);
        body_branch_to(body, false_end, join_block);
    }
    if (join_block >= 0) {
        body_reopen_block(body, join_block);
        return join_block;
    }
    return true_end;
}

static int32_t csg_parse_for(ColdCsgLower *lower, BodyIR *body, Locals *locals,
                             int32_t block, ColdCsgStmt stmt, LoopCtx *parent_loop) {
    int32_t start_kind = SLOT_I32;
    int32_t start_slot = csg_parse_expr_span(lower, body, locals, stmt.b, &start_kind);
    int32_t end_kind = SLOT_I32;
    int32_t end_slot = csg_parse_expr_span(lower, body, locals, stmt.c, &end_kind);
    if (start_kind != SLOT_I32 || end_kind != SLOT_I32) die("cold csg for range bounds must be int32");
    int32_t cond = span_eq(stmt.d, "lt") ? COND_LT : COND_LE;

    int32_t iter_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_COPY_I32, iter_slot, start_slot, 0);
    int32_t cond_block = body_block(body);
    int32_t loop_block = body_block(body);
    body_branch_to(body, block, cond_block);
    int32_t cond_term = body_term(body, BODY_TERM_CBR, iter_slot, cond, end_slot,
                                  loop_block, -1);
    body_end_block(body, cond_block, cond_term);

    int32_t loop_indent = csg_next_indent(lower);
    if (loop_indent <= stmt.indent) die("cold csg for suite must be indented");
    int32_t saved_local_count = locals->count;
    locals_add(locals, stmt.a, iter_slot, SLOT_I32);
    LoopCtx loop = {0};
    loop.parent = parent_loop;
    int32_t loop_end = csg_parse_statements_until(lower, body, locals, loop_block,
                                                  loop_indent, -1, &loop);
    locals->count = saved_local_count;

    int32_t increment_block = body_block(body);
    if (body->block_term[loop_end] < 0) body_branch_to(body, loop_end, increment_block);
    int32_t one_slot = body_slot(body, SLOT_I32, 4);
    int32_t next_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_I32_CONST, one_slot, 1, 0);
    body_op(body, BODY_OP_I32_ADD, next_slot, iter_slot, one_slot);
    body_op(body, BODY_OP_COPY_I32, iter_slot, next_slot, 0);
    body_branch_to(body, increment_block, cond_block);
    int32_t after_block = body_block(body);
    body->term_false_block[cond_term] = after_block;
    for (int32_t i = 0; i < loop.break_count; i++) body->term_true_block[loop.break_terms[i]] = after_block;
    for (int32_t i = 0; i < loop.continue_count; i++) body->term_true_block[loop.continue_terms[i]] = increment_block;
    return after_block;
}

static int32_t csg_parse_while(ColdCsgLower *lower, BodyIR *body, Locals *locals,
                               int32_t block, ColdCsgStmt stmt, LoopCtx *parent_loop) {
    int32_t cond_block = body_block(body);
    int32_t loop_block = body_block(body);
    int32_t after_block = body_block(body);
    body_branch_to(body, block, cond_block);
    body_reopen_block(body, cond_block);
    parse_condition_span(&lower->owner, body, locals, stmt.a, cond_block, loop_block, after_block);

    body_reopen_block(body, loop_block);
    int32_t loop_indent = csg_next_indent(lower);
    if (loop_indent <= stmt.indent) die("cold csg while suite must be indented");
    int32_t saved_local_count = locals->count;
    LoopCtx loop = {0};
    loop.parent = parent_loop;
    int32_t loop_end = csg_parse_statements_until(lower, body, locals, loop_block,
                                                  loop_indent, -1, &loop);
    locals->count = saved_local_count;
    if (body->block_term[loop_end] < 0) body_branch_to(body, loop_end, cond_block);
    for (int32_t i = 0; i < loop.break_count; i++) body->term_true_block[loop.break_terms[i]] = after_block;
    for (int32_t i = 0; i < loop.continue_count; i++) body->term_true_block[loop.continue_terms[i]] = cond_block;
    body_reopen_block(body, after_block);
    return after_block;
}

static int32_t csg_parse_statement(ColdCsgLower *lower, BodyIR *body,
                                   Locals *locals, int32_t block, LoopCtx *loop) {
    ColdCsgStmt stmt = lower->csg->stmts[lower->cursor++];
    if (span_eq(stmt.kind, "let") || span_eq(stmt.kind, "var")) {
        csg_parse_let(lower, body, locals, stmt.a, stmt.b);
        return block;
    }
    if (span_eq(stmt.kind, "var_t")) {
        csg_parse_let_t(lower, body, locals, stmt.a, stmt.b, stmt.c);
        return block;
    }
    if (span_eq(stmt.kind, "default")) {
        csg_parse_default(lower, body, locals, stmt.a, stmt.b);
        return block;
    }
    if (span_eq(stmt.kind, "assign")) {
        csg_parse_assign(lower, body, locals, stmt.a, stmt.b);
        return block;
    }
    if (span_eq(stmt.kind, "return")) {
        csg_parse_return(lower, body, locals, block, stmt.a);
        return block;
    }
    if (span_eq(stmt.kind, "return_q")) {
        die("cold csg return_q is unsupported; bind the unwrapped value before return");
    }
    if (span_eq(stmt.kind, "call")) {
        int32_t ignored_kind = SLOT_I32;
        (void)parse_call_from_args_span(&lower->owner, body, locals, stmt.a, stmt.b, &ignored_kind);
        return block;
    }
    if (span_eq(stmt.kind, "let_q") || span_eq(stmt.kind, "call_q")) {
        bool is_call = span_eq(stmt.kind, "call_q");
        int32_t result_kind = SLOT_I32;
        int32_t variant_slot;
        if (is_call) {
            variant_slot = parse_call_from_args_span(&lower->owner, body, locals, stmt.a, stmt.b, &result_kind);
        } else {
            variant_slot = csg_parse_expr_span(lower, body, locals, stmt.b, &result_kind);
        }
        if (result_kind != SLOT_VARIANT) die("cold csg ? requires variant value");
        return cold_lower_question_result(lower->csg->symbols, body, locals, block, variant_slot,
                                          stmt.a, !is_call, -1, (Span){0});
    }
    if (span_eq(stmt.kind, "let_q_t")) {
        int32_t result_kind = SLOT_I32;
        int32_t variant_slot = csg_parse_expr_span(lower, body, locals, stmt.c, &result_kind);
        if (result_kind != SLOT_VARIANT) die("cold csg typed ? requires variant value");
        int32_t declared_kind = cold_slot_kind_from_type_with_symbols(lower->csg->symbols, stmt.b);
        return cold_lower_question_result(lower->csg->symbols, body, locals, block, variant_slot,
                                          stmt.a, true, declared_kind, span_trim(stmt.b));
    }
    if (span_eq(stmt.kind, "match")) return csg_parse_match(lower, body, locals, block, stmt, loop);
    if (span_eq(stmt.kind, "if")) return csg_parse_if(lower, body, locals, block, stmt, loop);
    if (span_eq(stmt.kind, "while")) return csg_parse_while(lower, body, locals, block, stmt, loop);
    if (span_eq(stmt.kind, "for_range")) return csg_parse_for(lower, body, locals, block, stmt, loop);
    if (span_eq(stmt.kind, "case")) die("cold csg case outside match");
    if (span_eq(stmt.kind, "break")) {
        int32_t term = body_branch_to(body, block, -1);
        loop_add_break(loop, term);
        return block;
    }
    if (span_eq(stmt.kind, "continue")) {
        int32_t term = body_branch_to(body, block, -1);
        loop_add_continue(loop, term);
        return block;
    }
    die("unsupported cold csg statement kind");
    return block;
}

static BodyIR *cold_csg_lower_function(ColdCsg *csg, int32_t fn_index) {
    ColdCsgFunction *fn = &csg->functions[fn_index];
    BodyIR *body = body_new(csg->arena);
    body->return_kind = cold_return_kind_from_span(csg->symbols, fn->ret);
    body->return_size = cold_return_slot_size(csg->symbols, fn->ret, body->return_kind);
    body->return_type = span_trim(fn->ret);
    Locals locals;
    locals_init(&locals, csg->arena);
    if (cold_kind_is_composite(body->return_kind)) {
        body->sret_slot = body_slot(body, SLOT_PTR, 8);
    }
    Span param_names[COLD_MAX_I32_PARAMS];
    int32_t param_kinds[COLD_MAX_I32_PARAMS];
    int32_t param_sizes[COLD_MAX_I32_PARAMS];
    Span param_types[COLD_MAX_I32_PARAMS];
    int32_t arity = parse_param_specs(csg->symbols, fn->params, param_names,
                                      param_kinds, param_sizes, param_types,
                                      COLD_MAX_I32_PARAMS);
    body->param_count = arity;
    for (int32_t i = 0; i < arity; i++) {
        int32_t slot = body_slot(body, param_kinds[i], param_sizes[i]);
        body_slot_set_type(body, slot, param_types[i]);
        if (param_kinds[i] == SLOT_ARRAY_I32) {
            int32_t len = 0;
            if (!cold_parse_i32_array_type(param_types[i], &len)) die("cold array param missing length");
            body_slot_set_array_len(body, slot, len);
        }
        body->param_slot[i] = slot;
        locals_add(&locals, param_names[i], slot, param_kinds[i]);
    }
    int32_t block = body_block(body);
    ColdCsgLower lower = {0};
    lower.csg = csg;
    lower.fn_index = fn_index;
    lower.cursor = fn->stmt_start >= 0 ? fn->stmt_start : 0;
    lower.end = lower.cursor + fn->stmt_count;
    lower.owner = (Parser){(Span){0}, 0, csg->arena, csg->symbols};
    int32_t body_indent = csg_next_indent(&lower);
    int32_t end_block = csg_parse_statements_until(&lower, body, &locals, block, body_indent, -1, 0);
    if (lower.cursor != lower.end) die("cold csg function body did not consume all statements");
    if (body->block_term[end_block] < 0) die("cold csg function body has no terminator");
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
    R9 = 9,
    SP = 31,
    LR = 30,
    FP = 29,
};

static uint32_t a64_ret(void) { return 0xD65F03C0u; }
static uint32_t a64_brk(int imm) { return 0xD4200000u | ((uint32_t)(imm & 0xFFFF) << 5); }
static uint32_t a64_movz(int rd, uint16_t value, int shift) {
    return 0x52800000u | ((uint32_t)shift << 21) | ((uint32_t)value << 5) | (uint32_t)rd;
}
static uint32_t a64_movz_x(int rd, uint16_t value, int shift) {
    return 0xD2800000u | ((uint32_t)shift << 21) | ((uint32_t)value << 5) | (uint32_t)rd;
}
static uint32_t a64_adr(int rd, int32_t offset_bytes) {
    uint32_t imm = (uint32_t)offset_bytes & 0x1FFFFFu;
    uint32_t immlo = imm & 0x3u;
    uint32_t immhi = (imm >> 2) & 0x7FFFFu;
    return 0x10000000u | (immlo << 29) | (immhi << 5) | (uint32_t)rd;
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
static uint32_t a64_add_reg(int rd, int rn, int rm) {
    return 0x0B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_sub_reg(int rd, int rn, int rm) {
    return 0x4B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_b(int32_t offset_words) {
    return 0x14000000u | ((uint32_t)offset_words & 0x03FFFFFFu);
}
static uint32_t a64_bl(int32_t offset_words) {
    return 0x94000000u | ((uint32_t)offset_words & 0x03FFFFFFu);
}
static uint32_t a64_bcond(int32_t offset_words, int cond) {
    return 0x54000000u | (((uint32_t)offset_words & 0x7FFFFu) << 5) | ((uint32_t)cond & 0xFu);
}
static uint32_t a64_cbz(int rt, int32_t offset_words, bool nonzero) {
    return (nonzero ? 0x35000000u : 0x34000000u) |
           (((uint32_t)offset_words & 0x7FFFFu) << 5) |
           (uint32_t)rt;
}
static uint32_t a64_svc(uint16_t imm) {
    return 0xD4000001u | ((uint32_t)imm << 5);
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

typedef struct {
    int32_t pos;
    int32_t target_function;
} FunctionPatch;

typedef struct {
    FunctionPatch *items;
    int32_t count;
    int32_t cap;
    Arena *arena;
} FunctionPatchList;

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

static int32_t code_append_bytes(Code *code, const char *bytes, int32_t len) {
    int32_t start_bytes = code->count * 4;
    int32_t pos = 0;
    while (pos < len) {
        uint32_t word = 0;
        for (int32_t b = 0; b < 4 && pos < len; b++, pos++) {
            word |= ((uint32_t)(uint8_t)bytes[pos]) << (uint32_t)(b * 8);
        }
        code_emit(code, word);
    }
    return start_bytes;
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

static void function_patches_add(FunctionPatchList *patches, int32_t pos, int32_t target_function) {
    if (patches->count >= patches->cap) {
        int32_t next = patches->cap ? patches->cap * 2 : 32;
        FunctionPatch *fresh = arena_alloc(patches->arena, (size_t)next * sizeof(FunctionPatch));
        if (patches->items) memcpy(fresh, patches->items, (size_t)patches->count * sizeof(FunctionPatch));
        patches->items = fresh;
        patches->cap = next;
    }
    patches->items[patches->count++] = (FunctionPatch){pos, target_function};
}

static void emit_epilogue(Code *code, int32_t frame_size) {
    if (frame_size > 0) code_emit(code, a64_add_imm(SP, SP, (uint16_t)frame_size, true));
    code_emit(code, a64_ldp_post(FP, LR, SP, 16));
    code_emit(code, a64_ret());
}

static void codegen_store_params(Code *code, BodyIR *body) {
    int32_t reg = 0;
    for (int32_t i = 0; i < body->param_count; i++) {
        int32_t slot = body->param_slot[i];
        int32_t kind = body->slot_kind[slot];
        int32_t size = body->slot_size[slot];
        if (kind == SLOT_I32) {
            code_emit(code, a64_str_imm(reg, SP, body->slot_offset[slot], false));
            reg++;
        } else if (kind == SLOT_STR) {
            code_emit(code, a64_str_imm(reg, SP, body->slot_offset[slot], true));
            code_emit(code, a64_str_imm(reg + 1, SP, body->slot_offset[slot] + 8, true));
            reg += 2;
        } else if (kind == SLOT_VARIANT) {
            if (size > 16) {
                for (int32_t off = 0; off < size; off += 8) {
                    code_emit(code, a64_ldr_imm(R9, reg, off, true));
                    code_emit(code, a64_str_imm(R9, SP, body->slot_offset[slot] + off, true));
                }
                reg++;
            } else {
                code_emit(code, a64_str_imm(reg, SP, body->slot_offset[slot], true));
                code_emit(code, a64_str_imm(reg + 1, SP, body->slot_offset[slot] + 8, true));
                reg += 2;
            }
        } else if (kind == SLOT_OBJECT || kind == SLOT_ARRAY_I32 || kind == SLOT_SEQ_I32) {
            if (size > 16) {
                for (int32_t off = 0; off < size; off += 8) {
                    code_emit(code, a64_ldr_imm(R9, reg, off, true));
                    code_emit(code, a64_str_imm(R9, SP, body->slot_offset[slot] + off, true));
                }
                reg++;
            } else {
                code_emit(code, a64_str_imm(reg, SP, body->slot_offset[slot], true));
                code_emit(code, a64_str_imm(reg + 1, SP, body->slot_offset[slot] + 8, true));
                reg += 2;
            }
        } else {
            die("unsupported cold parameter kind");
        }
        if (reg > 8) die("cold parameter ABI exceeds arm64 argument registers");
    }
}

static void codegen_load_call_args(Code *code, BodyIR *body, FnDef *fn, int32_t arg_start) {
    int32_t reg = 0;
    for (int32_t i = 0; i < fn->arity; i++) {
        int32_t arg_slot = body->call_arg_slot[arg_start + i];
        int32_t arg_kind = body->slot_kind[arg_slot];
        if (arg_kind != fn->param_kind[i]) die("cold call arg kind changed after lowering");
        int32_t arg_size = body->slot_size[arg_slot];
        int32_t param_size = fn->param_size[i] > 0 ? fn->param_size[i] : arg_size;
        if (arg_kind == SLOT_VARIANT && arg_size != param_size) die("cold call arg size changed after lowering");
        if (arg_kind == SLOT_I32) {
            code_emit(code, a64_ldr_imm(reg, SP, body->slot_offset[arg_slot], false));
            reg++;
        } else if (arg_kind == SLOT_STR) {
            code_emit(code, a64_ldr_imm(reg, SP, body->slot_offset[arg_slot], true));
            code_emit(code, a64_ldr_imm(reg + 1, SP, body->slot_offset[arg_slot] + 8, true));
            reg += 2;
        } else if (arg_kind == SLOT_VARIANT) {
            if (param_size > 16) {
                code_emit(code, a64_add_imm(reg, SP, (uint16_t)body->slot_offset[arg_slot], true));
                reg++;
            } else {
                code_emit(code, a64_ldr_imm(reg, SP, body->slot_offset[arg_slot], true));
                code_emit(code, a64_ldr_imm(reg + 1, SP, body->slot_offset[arg_slot] + 8, true));
                reg += 2;
            }
        } else if (arg_kind == SLOT_OBJECT || arg_kind == SLOT_ARRAY_I32 || arg_kind == SLOT_SEQ_I32) {
            if (param_size > 16) {
                code_emit(code, a64_add_imm(reg, SP, (uint16_t)body->slot_offset[arg_slot], true));
                reg++;
            } else {
                code_emit(code, a64_ldr_imm(reg, SP, body->slot_offset[arg_slot], true));
                code_emit(code, a64_ldr_imm(reg + 1, SP, body->slot_offset[arg_slot] + 8, true));
                reg += 2;
            }
        } else {
            die("unsupported cold call arg kind");
        }
        if (reg > 8) die("cold call ABI exceeds arm64 argument registers");
    }
}

static void codegen_zero_slot(Code *code, BodyIR *body, int32_t slot) {
    code_emit(code, a64_movz_x(R0, 0, 0));
    for (int32_t off = 0; off < body->slot_size[slot]; off += 8) {
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[slot] + off, true));
    }
}

static void codegen_copy_slot_from_offset(Code *code, BodyIR *body,
                                          int32_t dst_slot, int32_t src_slot,
                                          int32_t src_offset) {
    int32_t kind = body->slot_kind[dst_slot];
    if (kind == SLOT_I32) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[src_slot] + src_offset, false));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst_slot], false));
        return;
    }
    int32_t total = body->slot_size[dst_slot];
    for (int32_t off = 0; off < total; off += 8) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[src_slot] + src_offset + off, true));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst_slot] + off, true));
    }
}

static void codegen_store_slot_to_offset(Code *code, BodyIR *body,
                                         int32_t dst_slot, int32_t dst_offset,
                                         int32_t src_slot) {
    int32_t kind = body->slot_kind[src_slot];
    if (kind == SLOT_I32) {
        code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[src_slot], false));
        code_emit(code, a64_str_imm(R1, SP, body->slot_offset[dst_slot] + dst_offset, false));
        return;
    }
    int32_t total = body->slot_size[src_slot];
    for (int32_t off = 0; off < total; off += 8) {
        code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[src_slot] + off, true));
        code_emit(code, a64_str_imm(R1, SP, body->slot_offset[dst_slot] + dst_offset + off, true));
    }
}

static void codegen_op(Code *code, BodyIR *body, Symbols *symbols,
                       FunctionPatchList *function_patches, int32_t op) {
    int32_t kind = body->op_kind[op];
    int32_t dst = body->op_dst[op];
    int32_t a = body->op_a[op];
    int32_t b = body->op_b[op];
    int32_t c = body->op_c[op];
    if (kind == BODY_OP_I32_CONST) {
        code_emit(code, a64_movz(R0, (uint16_t)(a & 0xFFFF), 0));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_LOAD_I32) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_MAKE_VARIANT) {
        codegen_zero_slot(code, body, dst);
        code_emit(code, a64_movz_x(R0, (uint16_t)a, 0));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], true));
        for (int32_t field_index = 0; field_index < c; field_index++) {
            int32_t arg_slot = body->call_arg_slot[b + field_index];
            int32_t payload_offset = body->call_arg_offset[b + field_index];
            if (body->slot_kind[arg_slot] == SLOT_STR) {
                code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[arg_slot], true));
                code_emit(code, a64_str_imm(R1, SP, body->slot_offset[dst] + payload_offset, true));
                code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[arg_slot] + 8, true));
                code_emit(code, a64_str_imm(R1, SP, body->slot_offset[dst] + payload_offset + 8, true));
            } else if (body->slot_kind[arg_slot] == SLOT_I32) {
                code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[arg_slot], false));
                code_emit(code, a64_str_imm(R1, SP, body->slot_offset[dst] + payload_offset, false));
            } else if (body->slot_kind[arg_slot] == SLOT_VARIANT) {
                if (body->slot_size[arg_slot] > cold_slot_size_for_kind(SLOT_VARIANT)) {
                    die("nested variant payload too large for cold v field");
                }
                for (int32_t off = 0; off < body->slot_size[arg_slot]; off += 8) {
                    code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[arg_slot] + off, true));
                    code_emit(code, a64_str_imm(R1, SP, body->slot_offset[dst] + payload_offset + off, true));
                }
            } else {
                die("unsupported variant payload slot kind");
            }
        }
    } else if (kind == BODY_OP_TAG_LOAD) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a], false));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_PAYLOAD_LOAD) {
        codegen_copy_slot_from_offset(code, body, dst, a, b);
    } else if (kind == BODY_OP_MAKE_COMPOSITE) {
        codegen_zero_slot(code, body, dst);
        for (int32_t item_index = 0; item_index < c; item_index++) {
            int32_t src_slot = body->call_arg_slot[b + item_index];
            int32_t dst_offset = body->call_arg_offset[b + item_index];
            codegen_store_slot_to_offset(code, body, dst, dst_offset, src_slot);
        }
    } else if (kind == BODY_OP_CALL_I32) {
        if (a < 0 || a >= symbols->function_count) die("invalid function call target");
        FnDef *fn = &symbols->functions[a];
        codegen_load_call_args(code, body, fn, b);
        int32_t call_pos = code->count;
        code_emit(code, a64_bl(0));
        function_patches_add(function_patches, call_pos, a);
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_COPY_I32) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a], false));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_I32_ADD || kind == BODY_OP_I32_SUB) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a], false));
        code_emit(code, a64_ldr_imm(R1, SP, body->slot_offset[b], false));
        code_emit(code, kind == BODY_OP_I32_ADD ? a64_add_reg(R0, R0, R1) : a64_sub_reg(R0, R0, R1));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_STR_LITERAL) {
        if (a < 0 || a >= body->string_literal_count) die("invalid string literal index");
        Span literal = body->string_literal[a];
        int32_t adr_pos = code->count;
        code_emit(code, a64_adr(R0, 0));
        int32_t skip_pos = code->count;
        code_emit(code, a64_b(0));
        int32_t data_offset = code_append_bytes(code, (const char *)literal.ptr, literal.len);
        int32_t after_data = code->count;
        code->words[adr_pos] = a64_adr(R0, data_offset - adr_pos * 4);
        code->words[skip_pos] = a64_b(after_data - skip_pos);
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], true));
        code_emit(code, a64_movz_x(R0, (uint16_t)literal.len, 0));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst] + 8, true));
    } else if (kind == BODY_OP_STR_LEN) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a] + 8, true));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_MAKE_SEQ_I32) {
        codegen_zero_slot(code, body, dst);
        if (c > 0) {
            if (a < 0 || body->slot_kind[a] != SLOT_ARRAY_I32) die("int32[] literal missing backing array");
            code_emit(code, a64_movz(R0, (uint16_t)c, 0));
            code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
            code_emit(code, a64_movz(R0, (uint16_t)c, 0));
            code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst] + 4, false));
            code_emit(code, a64_add_imm(R0, SP, (uint16_t)body->slot_offset[a], true));
            code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst] + 8, true));
        }
    } else if (kind == BODY_OP_SEQ_I32_INDEX) {
        if (body->slot_kind[a] != SLOT_SEQ_I32) die("int32[] index target kind mismatch");
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a] + 8, true));
        code_emit(code, a64_ldr_imm(R0, R0, b, false));
        code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
    } else if (kind == BODY_OP_CALL_COMPOSITE) {
        if (a < 0 || a >= symbols->function_count) die("invalid function call target");
        FnDef *fn = &symbols->functions[a];
        codegen_load_call_args(code, body, fn, b);
        int32_t ret_kind = cold_return_kind_from_span(symbols, fn->ret);
        if (ret_kind != SLOT_I32) {
            code_emit(code, a64_add_imm(8, SP, (uint16_t)body->slot_offset[dst], true));
        }
        int32_t call_pos = code->count;
        code_emit(code, a64_bl(0));
        function_patches_add(function_patches, call_pos, a);
        if (ret_kind == SLOT_I32) {
            code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], false));
        }
    } else if (kind == BODY_OP_COPY_COMPOSITE) {
        int32_t src_kind = body->slot_kind[a];
        if (src_kind == SLOT_STR) {
            code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a], true));
            code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst], true));
            code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a] + 8, true));
            code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst] + 8, true));
        } else if (src_kind == SLOT_VARIANT || src_kind == SLOT_OBJECT ||
                   src_kind == SLOT_ARRAY_I32 || src_kind == SLOT_SEQ_I32) {
            int32_t total = body->slot_size[dst];
            for (int32_t off = 0; off < total; off += 8) {
                code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[a] + off, true));
                code_emit(code, a64_str_imm(R0, SP, body->slot_offset[dst] + off, true));
            }
        } else {
            die("unsupported copy composite slot kind");
        }
    } else if (kind == BODY_OP_UNWRAP_OR_RETURN) {
        code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[dst], true));
        code_emit(code, a64_cmp_imm(R0, (uint16_t)b));
        int32_t trap_pos = code->count;
        code_emit(code, a64_bcond(0, COND_NE));
        code_emit(code, a64_brk(1));
        code->words[trap_pos] = (code->words[trap_pos] & 0xFF00001Fu) | (2u << 5);
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

static void codegen_func(Code *code, BodyIR *body, Symbols *symbols,
                         FunctionPatchList *function_patches) {
    int32_t frame_size = align_i32(body->frame_size, 16);
    code_emit(code, a64_stp_pre(FP, LR, SP, -16));
    code_emit(code, a64_add_imm(FP, SP, 0, true));
    if (frame_size > 0) code_emit(code, a64_sub_imm(SP, SP, (uint16_t)frame_size, true));
    if (body->sret_slot >= 0) {
        code_emit(code, a64_str_imm(8, SP, body->slot_offset[body->sret_slot], true));
    }
    codegen_store_params(code, body);

    int32_t *block_pos = arena_alloc(body->arena, (size_t)body->block_count * sizeof(int32_t));
    PatchList patches = {0};
    patches.arena = body->arena;

    for (int32_t block = 0; block < body->block_count; block++) {
        block_pos[block] = code->count;
        int32_t start = body->block_op_start[block];
        int32_t end = start + body->block_op_count[block];
        for (int32_t op = start; op < end; op++) {
            codegen_op(code, body, symbols, function_patches, op);
        }

        int32_t term = body->block_term[block];
        if (term < 0) continue;
        int32_t kind = body->term_kind[term];
        if (kind == BODY_TERM_RET) {
            int32_t value_slot = body->term_value[term];
            int32_t value_kind = body->slot_kind[value_slot];
            if (value_kind == SLOT_I32) {
                code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[value_slot], false));
            } else if (value_kind == SLOT_STR) {
                if (body->sret_slot < 0) die("missing sret slot for str return");
                code_emit(code, a64_ldr_imm(8, SP, body->slot_offset[body->sret_slot], true));
                code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[value_slot], true));
                code_emit(code, a64_str_imm(R0, 8, 0, true));
                code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[value_slot] + 8, true));
                code_emit(code, a64_str_imm(R0, 8, 8, true));
            } else if (value_kind == SLOT_VARIANT || value_kind == SLOT_OBJECT) {
                if (body->sret_slot < 0) die("missing sret slot for composite return");
                code_emit(code, a64_ldr_imm(8, SP, body->slot_offset[body->sret_slot], true));
                int32_t total = body->return_size;
                if (body->slot_size[value_slot] < total) die("composite return slot smaller than return type");
                for (int32_t off = 0; off < total; off += 8) {
                    code_emit(code, a64_ldr_imm(R0, SP, body->slot_offset[value_slot] + off, true));
                    code_emit(code, a64_str_imm(R0, 8, off, true));
                }
            } else {
                die("unsupported return slot kind");
            }
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

static void codegen_program(Code *code, BodyIR **function_bodies,
                            int32_t function_count, int32_t entry_function,
                            Symbols *symbols) {
    if (entry_function < 0 || entry_function >= function_count ||
        !function_bodies[entry_function]) die("missing entry function body");

    int32_t *function_pos = arena_alloc(code->arena, (size_t)function_count * sizeof(int32_t));
    for (int32_t i = 0; i < function_count; i++) function_pos[i] = -1;

    FunctionPatchList function_patches = {0};
    function_patches.arena = code->arena;

    function_pos[entry_function] = code->count;
    codegen_func(code, function_bodies[entry_function], symbols, &function_patches);
    for (int32_t i = 0; i < function_count; i++) {
        if (i == entry_function || !function_bodies[i]) continue;
        function_pos[i] = code->count;
        codegen_func(code, function_bodies[i], symbols, &function_patches);
    }

    for (int32_t i = 0; i < function_patches.count; i++) {
        FunctionPatch patch = function_patches.items[i];
        if (patch.target_function < 0 || patch.target_function >= function_count ||
            function_pos[patch.target_function] < 0) {
            die("function call target has no body");
        }
        int32_t delta = function_pos[patch.target_function] - patch.pos;
        uint32_t ins = code->words[patch.pos];
        code->words[patch.pos] = (ins & 0xFC000000u) | ((uint32_t)delta & 0x03FFFFFFu);
    }
}

static void code_patch_bcond(Code *code, int32_t pos, int32_t target) {
    int32_t delta = target - pos;
    uint32_t ins = code->words[pos];
    code->words[pos] = (ins & 0xFF00001Fu) | (((uint32_t)delta & 0x7FFFFu) << 5);
}

static void code_patch_b(Code *code, int32_t pos, int32_t target) {
    int32_t delta = target - pos;
    uint32_t ins = code->words[pos];
    code->words[pos] = (ins & 0xFC000000u) | ((uint32_t)delta & 0x03FFFFFFu);
}

static void code_emit_return_i32(Code *code, int32_t value) {
    code_emit(code, a64_movz(R0, (uint16_t)(value & 0xFFFF), 0));
    code_emit(code, a64_ret());
}

static int32_t cold_entry_dispatch_exit_code(int32_t command_code) {
    if (command_code == 0 || command_code == 1 || command_code == 2) return 0;
    return 2;
}

static const char *cold_entry_dispatch_output_text(int32_t command_code) {
    if (command_code == 0) {
        return "usage: cheng <command> [args]\n"
               "core commands: status, print-build-plan, system-link-exec\n";
    }
    if (command_code == 1) {
        return "cheng\n"
               "version=cheng.compiler_runtime.v1\n"
               "execution=argv_control_plane\n"
               "bootstrap_mode=selfhost\n"
               "compiler_entry=src/core/tooling/backend_driver_dispatch_min.cheng\n"
               "ordinary_command=system-link-exec\n"
               "ordinary_pipeline=canonical_csg_verified_primary_object_codegen_ready\n"
               "stage3=artifacts/bootstrap/cheng.stage3\n";
    }
    if (command_code == 2) {
        return "target=arm64-apple-darwin\n"
               "linker=system_link\n"
               "stage2_compiler=artifacts/bootstrap/cheng.stage2\n"
               "entry=src/core/tooling/backend_driver_dispatch_min.cheng\n"
               "output=artifacts/backend_driver/cheng\n"
               "source[0]=backend_driver_entry_source:src/core/tooling/backend_driver_dispatch_min.cheng\n"
               "source[1]=backend_system_link_plan_source:src/core/backend/system_link_plan.cheng\n"
               "source[2]=backend_lowering_plan_source:src/core/backend/lowering_plan.cheng\n"
               "source[3]=backend_primary_object_plan_source:src/core/backend/primary_object_plan.cheng\n"
               "source[4]=backend_aarch64_encode_source:src/core/backend/aarch64_encode.cheng\n"
               "source[5]=backend_direct_object_emit_source:src/core/backend/direct_object_emit.cheng\n"
               "source[6]=backend_line_map_source:src/core/backend/line_map.cheng\n"
               "source[7]=backend_system_link_exec_runtime_source:src/core/backend/system_link_exec_runtime.cheng\n"
               "source[8]=backend_system_link_exec_runtime_direct_source:src/core/backend/system_link_exec_runtime_direct.cheng\n"
               "source[9]=backend_build_plan_source:src/core/backend/build_plan.cheng\n";
    }
    return "";
}

static const char *cold_entry_dispatch_expected_marker(int32_t command_code) {
    if (command_code == 0) return "usage: cheng <command> [args]";
    if (command_code == 1) return "ordinary_command=system-link-exec";
    if (command_code == 2) return "source[9]=backend_build_plan_source:src/core/backend/build_plan.cheng";
    return 0;
}

static const char *cold_entry_system_link_report_text(void) {
    return "system_link_exec_runtime_execute=0\n"
           "system_link_exec=0\n"
           "real_backend_codegen=0\n"
           "cold_frontend_entry_dispatch_report=1\n"
           "error=cheng_cold entry-dispatch: system-link-exec backend codegen is not implemented\n";
}

static void cold_emit_output_and_return(Code *code, const char *output, int32_t exit_code) {
    int32_t len = (int32_t)strlen(output);
    if (len > 0) {
        int32_t adr_pos = code->count;
        code_emit(code, a64_adr(R1, 0));
        code_emit(code, a64_movz_x(R0, 1, 0));
        code_emit(code, a64_movz_x(R2, (uint16_t)len, 0));
        code_emit(code, a64_movz_x(16, 4, 0));
        code_emit(code, a64_svc(0x80));
        code_emit_return_i32(code, exit_code);
        int32_t data_offset = code_append_bytes(code, output, len);
        code->words[adr_pos] = a64_adr(R1, data_offset - adr_pos * 4);
        return;
    }
    code_emit_return_i32(code, exit_code);
}

static void cold_emit_arg_string_compare(Code *code, int ptr_reg, const char *text,
                                         bool include_nul,
                                         int32_t *fail_patches,
                                         int32_t *fail_count,
                                         int32_t fail_cap) {
    int32_t len = (int32_t)strlen(text);
    int32_t count = include_nul ? len + 1 : len;
    if (*fail_count + count > fail_cap) die("cold entry dispatch compare patch overflow");
    for (int32_t i = 0; i < count; i++) {
        int32_t expected = i < len ? (uint8_t)text[i] : 0;
        code_emit(code, a64_ldrb_imm(R3, ptr_reg, i));
        code_emit(code, a64_cmp_imm(R3, (uint16_t)expected));
        fail_patches[(*fail_count)++] = code->count;
        code_emit(code, a64_bcond(0, COND_NE));
    }
}

static void cold_patch_bcond_list(Code *code, int32_t *patches, int32_t count, int32_t target) {
    for (int32_t i = 0; i < count; i++) code_patch_bcond(code, patches[i], target);
}

static void cold_emit_system_link_report_and_return(Code *code) {
    int32_t write_branches[4];
    int32_t write_branch_count = 0;
    int32_t loop_start = 0;
    int32_t no_report_patches[8];
    int32_t no_report_count = 0;

    code_emit(code, a64_sub_imm(4, R0, 2, true));
    code_emit(code, a64_add_imm(5, R1, 16, true));

    loop_start = code->count;
    code_emit(code, a64_cmp_imm(4, 0));
    no_report_patches[no_report_count++] = code->count;
    code_emit(code, a64_bcond(0, COND_LE));
    code_emit(code, a64_ldr_imm(7, 5, 0, true));

    {
        int32_t fail_patches[32];
        int32_t fail_count = 0;
        cold_emit_arg_string_compare(code, 7, "--report-out:", false,
                                     fail_patches, &fail_count, 32);
        code_emit(code, a64_add_imm(8, 7, 13, true));
        write_branches[write_branch_count++] = code->count;
        code_emit(code, a64_b(0));
        cold_patch_bcond_list(code, fail_patches, fail_count, code->count);
    }
    {
        int32_t fail_patches[32];
        int32_t fail_count = 0;
        cold_emit_arg_string_compare(code, 7, "--report-out=", false,
                                     fail_patches, &fail_count, 32);
        code_emit(code, a64_add_imm(8, 7, 13, true));
        write_branches[write_branch_count++] = code->count;
        code_emit(code, a64_b(0));
        cold_patch_bcond_list(code, fail_patches, fail_count, code->count);
    }
    {
        int32_t fail_patches[32];
        int32_t fail_count = 0;
        cold_emit_arg_string_compare(code, 7, "--report-out", true,
                                     fail_patches, &fail_count, 32);
        code_emit(code, a64_cmp_imm(4, 1));
        no_report_patches[no_report_count++] = code->count;
        code_emit(code, a64_bcond(0, COND_LE));
        code_emit(code, a64_ldr_imm(8, 5, 8, true));
        write_branches[write_branch_count++] = code->count;
        code_emit(code, a64_b(0));
        cold_patch_bcond_list(code, fail_patches, fail_count, code->count);
    }

    code_emit(code, a64_add_imm(5, 5, 8, true));
    code_emit(code, a64_sub_imm(4, 4, 1, true));
    code_emit(code, a64_b(loop_start - code->count));

    int32_t no_report_target = code->count;
    code_emit_return_i32(code, 2);

    int32_t write_report_target = code->count;
    for (int32_t i = 0; i < write_branch_count; i++) code_patch_b(code, write_branches[i], write_report_target);
    cold_patch_bcond_list(code, no_report_patches, no_report_count, no_report_target);

    code_emit(code, a64_add_imm(R0, 8, 0, true));
    code_emit(code, a64_movz_x(R1, 0x0601, 0));
    code_emit(code, a64_movz_x(R2, 0644, 0));
    code_emit(code, a64_movz_x(16, 5, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_add_imm(9, R0, 0, true));

    const char *report = cold_entry_system_link_report_text();
    int32_t report_len = (int32_t)strlen(report);
    int32_t adr_pos = code->count;
    code_emit(code, a64_adr(R1, 0));
    code_emit(code, a64_add_imm(R0, 9, 0, true));
    code_emit(code, a64_movz_x(R2, (uint16_t)report_len, 0));
    code_emit(code, a64_movz_x(16, 4, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_add_imm(R0, 9, 0, true));
    code_emit(code, a64_movz_x(16, 6, 0));
    code_emit(code, a64_svc(0x80));
    code_emit_return_i32(code, 2);
    int32_t data_offset = code_append_bytes(code, report, report_len);
    code->words[adr_pos] = a64_adr(R1, data_offset - adr_pos * 4);
}

static void cold_emit_argv_string_case(Code *code, const char *text,
                                       const char *output, int32_t exit_code,
                                       int32_t command_code) {
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
    if (command_code == 5) cold_emit_system_link_report_and_return(code);
    else cold_emit_output_and_return(code, output, exit_code);
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
    cold_emit_output_and_return(code, cold_entry_dispatch_output_text(0), 0);
    code_patch_bcond(code, has_arg_patch, code->count);

    code_emit(code, a64_ldr_imm(R2, R1, 8, true));
    for (int32_t i = 0; i < stats->entry_command_case_count; i++) {
        int32_t exit_code = cold_entry_dispatch_exit_code(stats->entry_command_cases[i].code);
        const char *output = cold_entry_dispatch_output_text(stats->entry_command_cases[i].code);
        cold_emit_argv_string_case(code, stats->entry_command_cases[i].text, output,
                                   exit_code, stats->entry_command_cases[i].code);
    }
    code_emit_return_i32(code, 2);
    return macho_write_exec(path, code->words, code->count);
}

static bool cold_buffer_contains(const char *haystack, const char *needle) {
    if (!needle || needle[0] == '\0') return true;
    return strstr(haystack, needle) != 0;
}

static bool cold_span_contains_cstr(Span haystack, const char *needle) {
    int32_t n = (int32_t)strlen(needle);
    if (n <= 0) return true;
    if (haystack.len < n) return false;
    for (int32_t i = 0; i <= haystack.len - n; i++) {
        if (memcmp(haystack.ptr + i, needle, (size_t)n) == 0) return true;
    }
    return false;
}

static bool cold_run_system_link_report_probe(const char *path, const char *report_path) {
    unlink(report_path);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        char report_arg[PATH_MAX + 32];
        snprintf(report_arg, sizeof(report_arg), "--report-out:%s", report_path);
        execl(path, path, "system-link-exec", "--root:/tmp", "--in:/tmp/none.cheng",
              "--out:/tmp/none", report_arg, (char *)0);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 2) return false;
    Span report = source_open(report_path);
    bool ok = report.len > 0 &&
              cold_span_contains_cstr(report, "system_link_exec=0\n") &&
              cold_span_contains_cstr(report, "cold_frontend_entry_dispatch_report=1\n") &&
              cold_span_contains_cstr(report, "real_backend_codegen=0\n");
    if (report.len > 0) munmap((void *)report.ptr, (size_t)report.len);
    return ok;
}

static bool cold_run_executable_expect(const char *path, const char *arg,
                                       int32_t expected_rc,
                                       const char *expected_stdout_marker) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        if (arg) execl(path, path, arg, (char *)0);
        else execl(path, path, (char *)0);
        _exit(127);
    }
    close(pipefd[1]);
    char out[8192];
    int32_t used = 0;
    for (;;) {
        ssize_t n = read(pipefd[0], out + used, (size_t)(sizeof(out) - 1 - (size_t)used));
        if (n <= 0) break;
        used += (int32_t)n;
        if (used >= (int32_t)sizeof(out) - 1) break;
    }
    out[used] = '\0';
    close(pipefd[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) &&
           WEXITSTATUS(status) == expected_rc &&
           cold_buffer_contains(out, expected_stdout_marker);
}

static bool cold_verify_entry_dispatch_executable(const char *path, ColdManifestStats *stats) {
    if (!cold_run_executable_expect(path, 0, 0, cold_entry_dispatch_expected_marker(0))) return false;
    for (int32_t i = 0; i < stats->entry_command_case_count; i++) {
        int32_t exit_code = cold_entry_dispatch_exit_code(stats->entry_command_cases[i].code);
        const char *marker = cold_entry_dispatch_expected_marker(stats->entry_command_cases[i].code);
        if (!cold_run_executable_expect(path, stats->entry_command_cases[i].text, exit_code, marker)) return false;
    }
    if (!cold_run_executable_expect(path, "__cheng_cold_unknown_command__", 2, 0)) return false;
    char report_path[PATH_MAX];
    if (!cold_path_with_suffix(report_path, sizeof(report_path), path, ".system-link-exec.report.txt")) return false;
    if (!cold_run_system_link_report_probe(path, report_path)) return false;
    return true;
}

/* ================================================================
 * Output
 * ================================================================ */
static int output_direct_macho(const char *path, Code *code) {
    return macho_write_exec(path, code->words, code->count) ? 0 : 1;
}

typedef struct {
    int32_t function_count;
    int32_t type_count;
    int32_t op_count;
    int32_t block_count;
    int32_t switch_count;
    int32_t call_count;
    int32_t csg_lowering;
    int32_t csg_statement_count;
    int32_t code_words;
    size_t arena_kb;
    uint64_t elapsed_us;
} ColdCompileStats;

static void cold_collect_body_stats(BodyIR **function_bodies, int32_t function_count,
                                    ColdCompileStats *stats) {
    for (int32_t i = 0; i < function_count; i++) {
        BodyIR *body = function_bodies[i];
        if (!body) continue;
        stats->op_count += body->op_count;
        stats->block_count += body->block_count;
        stats->switch_count += body->switch_count;
        for (int32_t op = 0; op < body->op_count; op++) {
            if (body->op_kind[op] == BODY_OP_CALL_I32 ||
                body->op_kind[op] == BODY_OP_CALL_COMPOSITE) {
                stats->call_count++;
            }
        }
    }
}

static bool cold_compile_csg_path_to_macho(const char *out_path,
                                           const char *csg_path,
                                           ColdCompileStats *stats) {
    uint64_t start_us = cold_now_us();
    if (stats) memset(stats, 0, sizeof(*stats));
    Arena *arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (arena == MAP_FAILED) die("arena root mmap failed");
    Span csg_text = source_open(csg_path);
    if (csg_text.len <= 0) return false;

    Symbols *symbols = symbols_new(arena);
    ColdCsg csg;
    if (!cold_csg_load(&csg, arena, symbols, csg_text)) return false;
    BodyIR **function_bodies = arena_alloc(arena, (size_t)symbols->function_cap * sizeof(BodyIR *));
    int32_t entry_function = -1;
    for (int32_t i = 0; i < csg.function_count; i++) {
        int32_t symbol_index = csg.functions[i].symbol_index;
        if (symbol_index < 0 || symbol_index >= symbols->function_cap) die("cold csg function body table overflow");
        function_bodies[symbol_index] = cold_csg_lower_function(&csg, i);
        if ((csg.entry.len > 0 && span_same(csg.functions[i].name, csg.entry)) ||
            (csg.entry.len == 0 && span_eq(csg.functions[i].name, "main"))) {
            entry_function = symbol_index;
        }
    }
    if (entry_function < 0) return false;

    Code *code = code_new(arena, 256);
    codegen_program(code, function_bodies, symbols->function_count, entry_function, symbols);
    if (output_direct_macho(out_path, code) != 0) return false;

    if (stats) {
        stats->function_count = symbols->function_count;
        stats->type_count = symbols->type_count + symbols->object_count;
        stats->csg_lowering = 1;
        stats->csg_statement_count = csg.stmt_count;
        cold_collect_body_stats(function_bodies, symbols->function_count, stats);
        stats->code_words = code->count;
        stats->arena_kb = arena->used / 1024;
        uint64_t end_us = cold_now_us();
        if (start_us > 0 && end_us >= start_us) stats->elapsed_us = end_us - start_us;
    }
    munmap((void *)csg_text.ptr, (size_t)csg_text.len);
    return true;
}

static bool cold_compile_source_path_to_macho(const char *out_path,
                                              const char *src_path,
                                              bool allow_demo,
                                              ColdCompileStats *stats) {
    uint64_t start_us = cold_now_us();
    if (stats) memset(stats, 0, sizeof(*stats));
    Arena *arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (arena == MAP_FAILED) die("arena root mmap failed");

    Symbols *symbols = symbols_new(arena);
    BodyIR **function_bodies = 0;
    int32_t body_cap = 0;
    int32_t main_function = -1;
    int32_t first_function = -1;
    BodyIR *demo_body = 0;
    Span mapped_source = {0};

    if (src_path && src_path[0] != '\0') {
        mapped_source = source_open(src_path);
        if (mapped_source.len <= 0) return false;
        cold_collect_function_signatures(symbols, mapped_source);
        body_cap = symbols->function_cap;
        function_bodies = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
        Parser parser = {mapped_source, 0, arena, symbols};
        while (parser.pos < mapped_source.len) {
            parser_ws(&parser);
            if (parser.pos >= mapped_source.len) break;
            Span next = parser_peek(&parser);
            if (span_eq(next, "type")) {
                parse_type(&parser);
            } else if (span_eq(next, "fn")) {
                int32_t symbol_index = -1;
                BodyIR *body = parse_fn(&parser, &symbol_index);
                if (symbol_index < 0 || symbol_index >= body_cap) die("function body table overflow");
                function_bodies[symbol_index] = body;
                FnDef *fn = &symbols->functions[symbol_index];
                if (first_function < 0) first_function = symbol_index;
                if (span_eq(fn->name, "main")) {
                    main_function = symbol_index;
                }
            } else {
                parser_line(&parser);
            }
        }
        if (main_function < 0) {
            if (!allow_demo) {
                munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
                return false;
            }
            main_function = first_function;
        }
    }

    if (main_function < 0) {
        if (!allow_demo) {
            if (mapped_source.len > 0) munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
            return false;
        }
        demo_body = body_new(arena);
        int32_t block = body_block(demo_body);
        int32_t slot = body_slot(demo_body, SLOT_I32, 4);
        body_op(demo_body, BODY_OP_I32_CONST, slot, 42, 0);
        body_op(demo_body, BODY_OP_LOAD_I32, slot, 0, 0);
        int32_t term = body_term(demo_body, BODY_TERM_RET, slot, -1, 0, -1, -1);
        body_end_block(demo_body, block, term);
    }

    Code *code = code_new(arena, 256);
    if (demo_body) {
        FunctionPatchList function_patches = {0};
        function_patches.arena = arena;
        codegen_func(code, demo_body, symbols, &function_patches);
    } else {
        codegen_program(code, function_bodies, symbols->function_count, main_function, symbols);
    }
    if (output_direct_macho(out_path, code) != 0) {
        if (mapped_source.len > 0) munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
        return false;
    }

    if (stats) {
        stats->function_count = symbols->function_count;
        stats->type_count = symbols->type_count + symbols->object_count;
        if (demo_body) {
            stats->op_count = demo_body->op_count;
            stats->block_count = demo_body->block_count;
            stats->switch_count = demo_body->switch_count;
        } else {
            cold_collect_body_stats(function_bodies, symbols->function_count, stats);
        }
        stats->code_words = code->count;
        stats->arena_kb = arena->used / 1024;
        uint64_t end_us = cold_now_us();
        if (start_us > 0 && end_us >= start_us) stats->elapsed_us = end_us - start_us;
    }
    if (mapped_source.len > 0) munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
    return true;
}

static bool cold_write_system_link_exec_report(const char *path,
                                               bool success,
                                               const char *source_path,
                                               const char *csg_path,
                                               const char *out_path,
                                               const char *target,
                                               const char *emit,
                                               ColdCompileStats *stats,
                                               const char *error) {
    if (!path || path[0] == '\0') return true;
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fprintf(file, "system_link_exec_runtime_execute=%d\n", success ? 1 : 0);
    fprintf(file, "system_link_exec=%d\n", success ? 1 : 0);
    fprintf(file, "real_backend_codegen=%d\n", success ? 1 : 0);
    fputs("cold_system_link_exec=1\n", file);
    fputs("system_link_exec_scope=cold_subset_direct_macho\n", file);
    fputs("full_backend_codegen=0\n", file);
    fprintf(file, "target=%s\n", target ? target : "");
    fprintf(file, "emit=%s\n", emit ? emit : "");
    fprintf(file, "source=%s\n", source_path ? source_path : "");
    fprintf(file, "csg_input=%s\n", csg_path ? csg_path : "");
    fprintf(file, "output=%s\n", out_path ? out_path : "");
    fprintf(file, "direct_macho=%d\n", success ? 1 : 0);
    fprintf(file, "provider_object_count=0\n");
    if (stats) {
        fprintf(file, "cold_csg_lowering=%d\n", stats->csg_lowering);
        fprintf(file, "cold_csg_statement_count=%d\n", stats->csg_statement_count);
        fprintf(file, "cold_frontend_function_count=%d\n", stats->function_count);
        fprintf(file, "cold_frontend_type_count=%d\n", stats->type_count);
        fprintf(file, "cold_body_op_count=%d\n", stats->op_count);
        fprintf(file, "cold_body_block_count=%d\n", stats->block_count);
        fprintf(file, "cold_body_switch_count=%d\n", stats->switch_count);
        fprintf(file, "cold_body_call_count=%d\n", stats->call_count);
        fprintf(file, "cold_codegen_words=%d\n", stats->code_words);
        fprintf(file, "cold_arena_kb=%zu\n", stats->arena_kb);
        cold_print_elapsed_ms(file, "cold_compile_elapsed_ms", stats->elapsed_us);
    }
    if (error && error[0] != '\0') fprintf(file, "error=%s\n", error);
    fclose(file);
    return true;
}

static int cold_cmd_system_link_exec(int argc, char **argv) {
    const char *source_path = cold_flag_value(argc, argv, "--in");
    const char *csg_in_path = cold_flag_value(argc, argv, "--csg-in");
    const char *csg_out_path = cold_flag_value(argc, argv, "--csg-out");
    const char *out_path = cold_flag_value(argc, argv, "--out");
    const char *report_path = cold_flag_value(argc, argv, "--report-out");
    const char *target = cold_flag_value(argc, argv, "--target");
    const char *emit = cold_flag_value(argc, argv, "--emit");
    if (!target || target[0] == '\0') target = "arm64-apple-darwin";
    if (!emit || emit[0] == '\0') emit = "exe";
    const char *effective_csg_path = csg_in_path;

    if (csg_in_path && csg_in_path[0] != '\0' && csg_out_path && csg_out_path[0] != '\0') {
        cold_write_system_link_exec_report(report_path, false, source_path, csg_in_path, out_path,
                                           target, emit, 0, "cannot combine --csg-in and --csg-out");
        fprintf(stderr, "[cheng_cold] cannot combine --csg-in and --csg-out\n");
        return 2;
    }
    if ((!source_path || source_path[0] == '\0') && (!csg_in_path || csg_in_path[0] == '\0')) {
        cold_write_system_link_exec_report(report_path, false, source_path, csg_in_path, out_path,
                                           target, emit, 0, "missing --in");
        fprintf(stderr, "[cheng_cold] missing --in:<source> or --csg-in:<facts>\n");
        return 2;
    }
    if (csg_out_path && csg_out_path[0] != '\0') {
        if (!source_path || source_path[0] == '\0') {
            cold_write_system_link_exec_report(report_path, false, source_path, csg_out_path, out_path,
                                               target, emit, 0, "missing --in for --csg-out");
            fprintf(stderr, "[cheng_cold] --csg-out requires --in:<source>\n");
            return 2;
        }
        effective_csg_path = csg_out_path;
    }
    if (!out_path || out_path[0] == '\0') {
        cold_write_system_link_exec_report(report_path, false, source_path, effective_csg_path, out_path,
                                           target, emit, 0, "missing --out");
        fprintf(stderr, "[cheng_cold] missing --out:<path>\n");
        return 2;
    }
    if (strcmp(target, "arm64-apple-darwin") != 0) {
        cold_write_system_link_exec_report(report_path, false, source_path, effective_csg_path, out_path,
                                           target, emit, 0, "unsupported target");
        fprintf(stderr, "[cheng_cold] unsupported target: %s\n", target);
        return 2;
    }
    if (strcmp(emit, "exe") != 0) {
        cold_write_system_link_exec_report(report_path, false, source_path, effective_csg_path, out_path,
                                           target, emit, 0, "unsupported emit");
        fprintf(stderr, "[cheng_cold] unsupported emit: %s\n", emit);
        return 2;
    }
    if (csg_out_path && csg_out_path[0] != '\0') {
        if (!cold_emit_csg_facts_from_source_path(source_path, csg_out_path)) {
            cold_write_system_link_exec_report(report_path, false, source_path, csg_out_path, out_path,
                                               target, emit, 0, "cold csg emit failed");
            fprintf(stderr, "[cheng_cold] failed to emit cold csg facts: %s\n", csg_out_path);
            return 2;
        }
    }

    ColdCompileStats stats = {0};
    bool compiled = false;
    if (effective_csg_path && effective_csg_path[0] != '\0') {
        compiled = cold_compile_csg_path_to_macho(out_path, effective_csg_path, &stats);
    } else {
        compiled = cold_compile_source_path_to_macho(out_path, source_path, false, &stats);
    }
    if (!compiled) {
        cold_write_system_link_exec_report(report_path, false, source_path, effective_csg_path, out_path,
                                           target, emit, &stats, "cold subset compile failed");
        fprintf(stderr, "[cheng_cold] system-link-exec compile failed: %s\n",
                effective_csg_path && effective_csg_path[0] != '\0' ? effective_csg_path : source_path);
        return 2;
    }
    if (!cold_write_system_link_exec_report(report_path, true, source_path, effective_csg_path, out_path,
                                            target, emit, &stats, 0)) {
        fprintf(stderr, "[cheng_cold] failed to write system-link report: %s\n", report_path);
        return 2;
    }
    printf("system_link_exec=1\n");
    printf("real_backend_codegen=1\n");
    printf("system_link_exec_scope=cold_subset_direct_macho\n");
    if (stats.csg_lowering) printf("cold_csg_lowering=1\n");
    cold_print_elapsed_ms(stdout, "cold_compile_elapsed_ms", stats.elapsed_us);
    printf("output=%s\n", out_path);
    return 0;
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
    if (argc >= 2 && strcmp(argv[1], "system-link-exec") == 0) {
        return cold_cmd_system_link_exec(argc, argv);
    }
    if (argc >= 2 && cold_is_reserved_unimplemented_command(argv[1])) {
        return cold_cmd_unimplemented(argv[1]);
    }
    const char *out_path = argc > 1 ? argv[1] : "/tmp/cheng_cold_out";
    const char *src_path = argc > 2 ? argv[2] : 0;

    ColdCompileStats stats = {0};
    bool ok = cold_compile_source_path_to_macho(out_path, src_path, true, &stats);
    int rc = ok ? 0 : 1;
    printf("cheng_cold: %s src=%s fns=%d types=%d ops=%d blocks=%d switches=%d code=%dw arena=%zuKB\n",
           rc ? "FAIL" : "OK",
           src_path ? src_path : "(demo)",
           stats.function_count,
           stats.type_count,
           stats.op_count,
           stats.block_count,
           stats.switch_count,
           stats.code_words,
           stats.arena_kb);
    cold_print_elapsed_ms(stdout, "cold_compile_elapsed_ms", stats.elapsed_us);
    return rc;
}
